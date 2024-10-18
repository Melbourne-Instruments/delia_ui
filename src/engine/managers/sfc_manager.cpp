/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  sfc_manager.cpp
 * @brief Surface Manager implementation
 *-----------------------------------------------------------------------------
 */
#include <cstring>
#include <atomic>
#include <type_traits>
#include <sys/reboot.h>
#include "event.h"
#include "event_router.h"
#include "sw_manager.h"
#include "sfc_manager.h"
#include "utils.h"
#include "logger.h"

// Constants
constexpr uint POLL_THRESH_COUNT                    = 300;
constexpr uint SMALL_MOVEMENT_THRESHOLD_MAX_COUNT   = 20;
constexpr uint KNOB_MOVED_TO_TARGET_POLL_SKIP_COUNT = 6;
constexpr uint TAP_DETECTED_POLL_SKIP_COUNT         = 10;
constexpr uint SFC_HW_POLL_SECONDS                  = 0;
constexpr uint SFC_HW_POLL_NANOSECONDS              = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(30)).count();
constexpr uint KNOB_LARGE_MOVEMENT_TIME_THRESHOLD   = std::chrono::milliseconds(2000).count();
constexpr uint SWITCH_PUSH_LATCH_THRESHOLD          = std::chrono::milliseconds(500).count();
constexpr uint SWITCH_TOGGLE_LED_DUTY               = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(500)).count();
constexpr uint SWITCH_TOGGLE_RELEASE_THRESHOLD      = std::chrono::milliseconds(500).count();
constexpr uint SWITCH_TOGGLE_HOLD_THRESHOLD         = std::chrono::milliseconds(1000).count();
constexpr uint SWITCH_GROUPED_PUSH_HOLD_THRESHOLD   = std::chrono::milliseconds(1000).count();

// Static functions
static void *_process_sfc_control(void* data);

//----------------------------------------------------------------------------
// SfcManager
//----------------------------------------------------------------------------
SfcManager::SfcManager(EventRouter *event_router) :
    BaseManager(MoniqueModule::SFC_CONTROL, "SurfaceControlManager", event_router)
{
    // Initialise class data
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++) {
        _knob_controls[i].num = i;
        _knob_controls[i].position = 0;
        _knob_controls[i].position_delta = 0;
        _knob_controls[i].use_large_movement_threshold = true;
        _knob_controls[i].poll_skip_count = 0;
        _knob_controls[i].small_movement_threshold_count = 0;
        _knob_controls[i].polls_since_last_threshold_hit = 0;
        _knob_controls[i].moving_to_target = false;
        _knob_controls[i].moving_to_large_threshold = false;
    }
    for (uint i=0; i<NUM_PHYSICAL_SWITCHES; i++) {
        _switch_controls[i].num = i;
        _switch_controls[i].logical_state = 0.0;
        _switch_controls[i].physical_state = 0.0;
        _switch_controls[i].led_pulse_timer = nullptr;
        _switch_controls[i].latched = false;
        _switch_controls[i].push_time_processed = true;
        _switch_controls[i].grouped_push = false;
        _switch_controls[i].processed = false;
    }
    _param_changed_listener = 0;
    _sfc_func_listener = 0;
    _reload_presets_listener = 0;
    _exit_sfc_control_thread = false;
    _preset_reloaded = false;

    // Register the Surface params
    _register_params();

#ifndef NO_XENOMAI
    // Initialise Xenomai
    utils::init_xenomai();
#endif
}

//----------------------------------------------------------------------------
// ~SfcManager
//----------------------------------------------------------------------------
SfcManager::~SfcManager()
{
    // Stop the switch LED pulse timer tasks (if any)
    for (auto sc : _switch_controls) {
        if (sc.led_pulse_timer) {
            sc.led_pulse_timer->stop();
            delete sc.led_pulse_timer;
            sc.led_pulse_timer = nullptr;
        }
    }
    
    // Clean up allocated data
    if (_param_changed_listener)
        delete _param_changed_listener;
    if (_sfc_func_listener)
        delete _sfc_func_listener;
    if (_reload_presets_listener)
        delete _reload_presets_listener;
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool SfcManager::start()
{
    // We need firstly to check if any panel and/or controller firmware updates available, and perform
    // those update via an external script
    static_cast<SwManager *>(utils::get_manager(MoniqueModule::SOFTWARE))->check_pc_fw_available();
    static_cast<SwManager *>(utils::get_manager(MoniqueModule::SOFTWARE))->check_mc_fw_available();

    // Initialise the surface control
    int res = sfc::init();
    if (res < 0)
    {
        // Could not initialise the surface control, show an error
        MSG("ERROR: Could not initialise the surface control");
        MONIQUE_LOG_CRITICAL(module(), "Could not initialise the Surface Control: {}", res);
    }
    else
    {
        // If we are not in maintenance mode
        if (!utils::maintenance_mode()) {
            // Set the initial knob haptic modes (physical knobs only)
            for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
            {
                // Get the knob param
                auto param = utils::get_param(KnobParam::ParamPath(i));
                if (param)
                {
                    // Set the knob control haptic mode
                    _set_knob_control_haptic_mode(static_cast<KnobParam *>(param));
                }
            }

            // Set the AT Sensitivity
            auto param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::AT_SENSITIVITY_PARAM_ID);
            if (param) {
                utils::set_at_sensitivity(param);
            }
        }

        // The Surface Control was successfully initialised
        _sfc_control_init = true;        

        // Switch all LEDs on and then off
        sfc::set_all_switch_led_states(true);
        _commit_led_control_states();
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        sfc::set_all_switch_led_states(false);
        _commit_led_control_states();
        std::this_thread::sleep_for(std::chrono::milliseconds(600));

        // If we are not in maintenance mode
        if (!utils::maintenance_mode()) {
            // Before starting the Surface Manager polling loop, process all the preset values
            _process_reload_presets(false);              
        }

#ifndef NO_XENOMAI
        // Create the real-time task to start process the surface control
        // Note: run in secondary mode      
        res = utils::create_rt_task(&_sfc_control_thread, _process_sfc_control, this, SCHED_OTHER);
        if (res < 0) {
            // Error creating the RT thread, show the error
            MSG("ERROR: Could not start the surface control processing thread: " << errno);
            return false;        
        }
#else
        // Create a SCHED_FIFO thread to process the surface control - so that it has
        // priority over other threads in this app
        int policy;
        struct sched_param param;
        _sfc_control_thread = new std::thread(_process_sfc_control, this);
        pthread_getschedparam(_sfc_control_thread->native_handle(), &policy, &param);
        param.sched_priority = 1;
        pthread_setschedparam(_sfc_control_thread->native_handle(), SCHED_FIFO, &param);
#endif
    }

    // Call the base manager method
    auto ret = BaseManager::start();

    // Send a system function message to clear the boot warning screen now the Surface Manager has started and
    // performed any initialisation of the surface controls
    _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::SFC_INIT, module())));
    return ret;
}

//----------------------------------------------------------------------------
// stop
//----------------------------------------------------------------------------
void SfcManager::stop()
{
    // Call the base manager
    BaseManager::stop();

    // Surface control task running?
    if (_sfc_control_thread != 0) {
        // Stop the surface control real-time task
        _exit_sfc_control_thread = true;
#ifndef NO_XENOMAI
        utils::stop_rt_task(&_sfc_control_thread);
#else
		if (_sfc_control_thread->joinable())
			_sfc_control_thread->join(); 
#endif
        _sfc_control_thread = 0;       
    }

    // De-initialise the surface control
    sfc::deinit();
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void SfcManager::process()
{
    // Create and add the listeners
    _param_changed_listener = new EventListener(MoniqueModule::SYSTEM, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_param_changed_listener);  
    _sfc_func_listener = new EventListener(MoniqueModule::SYSTEM, EventType::SFC_FUNC, this);
    _event_router->register_event_listener(_sfc_func_listener);  
    _reload_presets_listener = new EventListener(MoniqueModule::SYSTEM, EventType::RELOAD_PRESETS, this);
    _event_router->register_event_listener(_reload_presets_listener);  

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void SfcManager::process_event(const BaseEvent * event)
{
    // Get the Surface control mutex
    std::lock_guard<std::mutex> lock(_sfc_mutex);

    // Process the event depending on the type
    switch (event->type())
    {
        case EventType::PARAM_CHANGED:
            // Process the param changed event
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            break;

        case EventType::RELOAD_PRESETS:
            // Process reloading of the presets
            _process_reload_presets(static_cast<const ReloadPresetsEvent *>(event)->from_layer_toggle());
            break;

        case EventType::SFC_FUNC:
            // Process the surface control function
            _process_sfc_func(static_cast<const SfcFuncEvent *>(event)->sfc_func());
            break;

		default:
            // Event unknown, we can ignore it
            break;
	}
}

//----------------------------------------------------------------------------
// process_sfc_control
// Note: RT thread
//----------------------------------------------------------------------------
void SfcManager::process_sfc_control()
{
    struct timespec poll_time;
    sfc::KnobState knob_states[NUM_PHYSICAL_KNOBS];    
    bool switch_physical_states[NUM_PHYSICAL_SWITCHES];
    int knob_states_res;
    int switch_states_res;
    std::chrono::system_clock::time_point start;
    bool poweroff_initiated = false;
    bool poweroff = false;

    // Initialise the knob and switch state arrays to zeros
    std::memset(knob_states, 0, sizeof(knob_states));
    std::memset(switch_physical_states, 0, sizeof(switch_physical_states));

    // Set the thread poll time (in nano-seconds)
    std::memset(&poll_time, 0, sizeof(poll_time));
    poll_time.tv_sec = SFC_HW_POLL_SECONDS;
    poll_time.tv_nsec = SFC_HW_POLL_NANOSECONDS;   

#ifndef NO_XENOMAI
    utils::rt_task_nanosleep(&poll_time); 
#else
    ::nanosleep(&poll_time, NULL);
#endif

    // Loop forever until exited
    while (!_exit_sfc_control_thread && !poweroff) {
        {
            // Get the Surface control mutex
            std::lock_guard<std::mutex> lock(_sfc_mutex);
            bool morph_params_changed = false;

            // If not in maintenance or demo modes
            if (!utils::maintenance_mode() && !utils::demo_mode()) {
                // Are we currently morphing?
                // Lock before performing this action so that the File Manager controller doesn't clash with this
                // processing      
                utils::morph_lock();
                if (utils::get_morph_state() || utils::get_prev_morph_state()) {
                    // Yes - if we are in morph dance mode, retrieve the state params
                    if (utils::morph_mode() == Monique::MorphMode::DANCE) {
                        // Dance mode, retrieve the params
                        auto s = std::chrono::steady_clock::now();
                        morph_params_changed = static_cast<DawManager *>(utils::get_manager(MoniqueModule::DAW))->get_layer_patch_state_params();
                        auto f = std::chrono::steady_clock::now();
                        float tt = std::chrono::duration_cast<std::chrono::microseconds>(f - s).count();
                        if(tt > 15000) {
                            MSG("get_layer_patch_state_params time (us): " << tt);
                        }
                    }
                }
                utils::set_prev_morph_state();
                utils::morph_unlock();
            }

            // Lock the Surface Control
            sfc::lock();

            // Read the switch states
            switch_states_res = sfc::read_switch_states(switch_physical_states);

            // Request the knob states
            knob_states_res = sfc::request_knob_states(); 
            if (knob_states_res == 0) {
                // Read the knob states
                knob_states_res = sfc::read_knob_states(knob_states);            
            }

            // Unlock the Surface Control
            sfc::unlock();

            // If not in maintenance or demo mode
            if (!utils::maintenance_mode() && !utils::demo_mode() && utils::get_morph_knob_param()) {
                // Always process the moph knob FIRST
                auto param = utils::get_morph_knob_param();
                _process_physical_knob(param, _knob_controls[param->control_num()], knob_states[param->control_num()]);
            }

            // Process each knob control
            for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++) {
                // Don't process the morph knob - this has already been done above  
                if (sfc::knob_is_active(i) && (i != utils::get_morph_knob_param()->control_num())) {
                    _process_knob_control(i, knob_states[i], morph_params_changed);
                }
            }

            // Switches read OK?
            if (switch_states_res == 0) {
                // Process each switch control
                for (uint i=0; i<NUM_PHYSICAL_SWITCHES; i++) {
                    _process_switch_control(i, switch_physical_states, morph_params_changed);
                }

                // Are the soft button 1 and the BANK buttons pressed?
                if (_switch_controls[0].physical_state && _switch_controls[4].physical_state) {
                    if (!poweroff) {
                        if (!poweroff_initiated) {
                            start = std::chrono::high_resolution_clock::now();
                            poweroff_initiated = true;
                        }
                        else {
                            auto end = std::chrono::high_resolution_clock::now();
                            if (std::chrono::duration_cast<std::chrono::seconds>(end -  start).count() > 3) {
                                // Poweroff the beast!
                                poweroff = true;
                                MSG("Power-off DELIA....");
                                sync();
                                reboot(RB_POWER_OFF);
                            }
                        }
                    }
                }
                else {
                    poweroff_initiated = false;
                }
            }

            // Update the read knob positions for the next poll processing
            for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
                _knob_controls[i].position = knob_states[i].position;

            // Reset the processing flag for the next poll processing
            for (uint i=0; i<NUM_PHYSICAL_SWITCHES; i++)
                _switch_controls[i].processed = false;

            // Reset the preset loaded flag, if set
            if (_preset_reloaded) {
                _preset_reloaded = false;
            }

            // Always commit the LED states so that they are guaranteed to be in the correct state
            if (!utils::maintenance_mode()) {
                _commit_led_control_states();
            }
        }

#ifndef NO_XENOMAI
        utils::rt_task_nanosleep(&poll_time); 
#else
        ::nanosleep(&poll_time, NULL);
#endif
    }
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void SfcManager::_process_param_changed_event(const ParamChange &param_change)
{
    // If this is a Surface Control param change
    Param *param = utils::get_param(param_change.param->path().c_str());
    if (param && (param->module() == MoniqueModule::SFC_CONTROL))
    {
        // Process the Surface Control param change
        _process_sfc_param_changed(param);
    }
}

//----------------------------------------------------------------------------
// _process_reload_presets
//----------------------------------------------------------------------------
void SfcManager::_process_reload_presets(bool from_layer_toggle)
{
    bool morphing = true;

    // Get the Morphing state
    auto morph_value_param = utils::get_morph_knob_param();

    // If from a Layer toggle OR we are in DJ mode or the Morph Value indicates not morphing
    if (from_layer_toggle ||
        ((utils::morph_mode() == Monique::MorphMode::DJ) ||
         (morph_value_param && 
          (((utils::get_current_layer_info().layer_state() == LayerState::STATE_A) && (morph_value_param->value() == 0.0f)) || 
           ((utils::get_current_layer_info().layer_state() == LayerState::STATE_B) && (morph_value_param->value() == 1.0f)))))) {
        morphing = false;
    }

    // Process the initial state and preset value for each knob
    for (uint i=1; i<NUM_PHYSICAL_KNOBS; i++) {
        // Process this knob if:
        // - It is not morphable OR
        // - We are not currently morphing OR in DJ mode
        const KnobParam *param = static_cast<const KnobParam *>(utils::get_param(KnobParam::ParamPath(i).c_str()));
        if (param && (!param->morphable() || !morphing)) {
            // Set the knob control position from the knob param preset
            _set_knob_control_position_from_preset(param);
        }
    }

    // Process the initial state and preset value for each switch
    for (uint i=0; i<NUM_PHYSICAL_SWITCHES; i++) {
        // Process this switch if:
        // - It is not morphable OR
        // - We are not currently morphing OR in DJ mode
        const SwitchParam *param = static_cast<const SwitchParam *>(utils::get_param(SwitchParam::ParamPath(i).c_str()));
        if (param && (!param->morphable() || !morphing)) {  
            // Set the switch control value from the switch param preset
            _set_switch_control_value_from_preset(param);
        }
    }

    // Indicate the preset has been reloaded
    _preset_reloaded = true;    
}

//----------------------------------------------------------------------------
// _process_sfc_func
//----------------------------------------------------------------------------
void SfcManager::_process_sfc_func(const SfcFunc &sfc_func)
{
    // Parse the function
    switch (sfc_func.type) {
        case SfcFuncType::SET_SWITCH_VALUE: {
            // Param specified?
            if (sfc_func.param) {
                // Set the new switch value
                sfc_func.param->set_value(sfc_func.switch_value);                
                _switch_controls[sfc_func.param->param_id()].logical_state = sfc_func.param->value();

                // Set the switch LED state
                _set_switch_led_state(static_cast<SwitchParam *>(sfc_func.param));

                // If this is a multi-function switch
                //if (param->multifn_switch) {
                //    // Indicate it has been latched
                //    _switch_controls[param->param_id].latched = sfc_func.set_switch ? true : false;
                //}
            }
            break;            
        }

        case SfcFuncType::SET_SWITCH_LED_STATE: {
            // Param specified?
            if (sfc_func.param) {
                // Set the switch LED state
                _set_switch_led_state(static_cast<SwitchParam *>(sfc_func.param), static_cast<SwitchValue>(sfc_func.switch_value));
            }
            break;            
        }

        case SfcFuncType::RESET_MULTIFN_SWITCHES: {
            // Reset the multi-function switches to the default state
            auto params = utils::get_multifn_switch_params();
            for (SwitchParam *sp : params) {
                // Always reset the latched state
                _switch_controls[sp->param_id()].latched = false;

                // Force the switch into the OFF state
                _switch_controls[sp->param_id()].logical_state = OFF;

                // Set the switch LED state
                sfc::set_switch_led_state(_switch_controls[sp->param_id()].num, false);

                // Update the switch parameter value
                sp->set_value(OFF);
            }
            break;            
        }

        case SfcFuncType::SET_MULTIFN_SWITCH: {
            // Parse all multi-function switches
            auto params = utils::get_multifn_switch_params();
            for (SwitchParam *sp : params) {              
                // Is this the required index?
                if ((uint)sp->multifn_switch_index() == sfc_func.num) {
                    // Set the specified switch to ON
                    _switch_controls[sp->param_id()].logical_state = ON;

                    // Set the switch LED state
                    sfc::set_switch_led_state(_switch_controls[sp->param_id()].num, true);

                    // Update the switch parameter value
                    sp->set_value(ON);
                }
                // Reset any other associated switches if required
                else if (sfc_func.reset_associated_switches && (_switch_controls[sp->param_id()].logical_state)) {
                    // Set the specified switch to OFF
                    _switch_controls[sp->param_id()].logical_state = OFF;

                    // Set the switch LED state OFF
                    sfc::set_switch_led_state(_switch_controls[sp->param_id()].num, OFF);

                    // Update the switch parameter value
                    sp->set_value(OFF);
                }
                //else if (sp->param_id() == sfc_func.param->param_id()) {
                    // This is the selected position
                    //sp->is_selected_position = true;
                //}
            }
            break;
        }

        case SfcFuncType::SET_CONTROL_HAPTIC_MODE: {
            // Param and haptic mode specified?
            if (sfc_func.param && (sfc_func.haptic_mode.size() > 0)) {
                // Make sure this param is a Surface Control param
                if (sfc_func.param->module() == module()) {
                    // Set the control haptic mode
                    sfc_func.param->set_haptic_mode(sfc_func.haptic_mode);
                        
                    // Are we changing a knob control state?
                    if (sfc_func.param->control_type() == sfc::ControlType::KNOB) {
                        // Set the knob control haptic mode
                        _set_knob_control_haptic_mode(static_cast<KnobParam *>(sfc_func.param));                                 
                    }
                }
            }
            break;
        }

        case SfcFuncType::SET_CONTROLS_STATE: {
            // Get all the params associated with state to set
            auto params = utils::get_params_with_state(sfc_func.controls_state);

            // Process the state params
            for (SfcControlParam *p : params) {
                // Set the new control state
                if (p->set_control_state(sfc_func.controls_state)) {
                    // Are we changing a knob control state?
                    if (p->control_type() == sfc::ControlType::KNOB) {
                        // Set the knob control haptic mode
                        _set_knob_control_haptic_mode(static_cast<KnobParam *>(p));
                                                        
                        // Now set the knob position from the param
                        _set_knob_control_position(static_cast<KnobParam *>(p));

                        // Create the control param change event
                        //auto mapped_param_change = ParamChange(p, module());
                        //_event_router->post_param_changed_event(new ParamChangedEvent(mapped_param_change));                         
                    }
                    // Are we changing a switch control state?
                    else if (p->control_type() == sfc::ControlType::SWITCH) {
                        auto sp = static_cast<SwitchParam *>(p);

                        // Save the switch value from the param (ignore push manual LED switches)
                        if (sp->haptic_mode().switch_mode != sfc::SwitchMode::PUSH_MANUAL_LED) {
                            _set_switch_control_value(sp);
                        }                     
                    }
                }         
            }
            break;
        }

        default:
            break;
    }
}

//----------------------------------------------------------------------------
// _set_knob_control_position_from_preset
//----------------------------------------------------------------------------
void SfcManager::_set_knob_control_position_from_preset(const KnobParam *param)
{
    // Is this knob active?
    if (sfc::knob_is_active(param->control_num())) {        
        // Set the knob control haptic mode
        _set_knob_control_haptic_mode(param);
    }

    // Set the knob position from the param
    _set_knob_control_position(param);
}

//----------------------------------------------------------------------------
// _set_switch_control_value_from_preset
//----------------------------------------------------------------------------
void SfcManager::_set_switch_control_value_from_preset(const SwitchParam *param)
{
    // Save the switch logical state so that it can be toggled at the next 
    // switch press
    _switch_controls[param->control_num()].logical_state = (int)param->value();

    // Set the switch LED state
    sfc::set_switch_led_state(param->control_num(), param->value() == 0 ? false : true);
}

//----------------------------------------------------------------------------
// _process_sfc_param_changed
//----------------------------------------------------------------------------
void SfcManager::_process_sfc_param_changed(Param *param)
{
    // Get the param as a Surface Control param
    SfcControlParam *sfc_param = static_cast<SfcControlParam *>(param);
    
    // Parse the control type
    switch (sfc_param->control_type())
    {
        case sfc::ControlType::KNOB:
            // Process the knob control
            _set_knob_control_position(static_cast<KnobParam *>(sfc_param));
            break;

        case sfc::ControlType::SWITCH:
            // Process the switch control
            _set_switch_control_value(static_cast<SwitchParam *>(sfc_param));
            break;

        default:
            break;
    }
}

//----------------------------------------------------------------------------
// _process_param_changed_mapped_params
//----------------------------------------------------------------------------
void SfcManager::_process_param_changed_mapped_params(const Param *param, float diff, const Param *skip_param, bool displayed, bool force)
{
    // Get the mapped params and process them
    auto mapped_params = param->mapped_params(skip_param);
    for (Param *mp : mapped_params) {
        // Because this function is recursive, we need to skip the param that
        // caused any recursion, so it is not processed twice
        if (skip_param && (mp == skip_param)) {
            continue;
        }

        // If this a normal param (not a system function)
        if (mp->type() != ParamType::SYSTEM_FUNC) {
            // Get the current param value
            auto current_value = mp->value();

            // Are both parameters linked to each other?
            if (param->is_linked_param() && mp->is_linked_param()) {
                // Is the linked functionality enabled?
                // If not, ignore this mapping
                if (param->is_linked_param_enabled() || mp->is_linked_param_enabled()) {
                    // Linked parameters are linked by the change (difference) in one of the values
                    // This change is relected in the other linked param
                    mp->set_value(mp->value() + diff);
                }
                else {
                    // Ignore this mapping
                    continue;
                }
            }
            else {
                // Update the mapped parameter value
                mp->set_value_from_param(*static_cast<const Param *>(param));
            }

            // Create the mapped param change event if it has changed - or forced
            if ((current_value != mp->value()) || force) {
                // If this control is a knob, set that knob position
                if (mp->module() == MoniqueModule::SFC_CONTROL &&
                    static_cast<SfcControlParam *>(mp)->control_type() == sfc::ControlType::KNOB) {
                    _set_knob_control_position(static_cast<KnobParam *>(mp), false);                        
                }

                // Send the param changed event
                auto mapped_param_change = ParamChange(mp, module());
                if (displayed)
                    mapped_param_change.display = false;
                else
                    displayed = mapped_param_change.display;
                _event_router->post_param_changed_event(new ParamChangedEvent(mapped_param_change));
            }
        }
        else
        {
            auto sfc_param = static_cast<const SfcControlParam *>(param);
            auto value = sfc_param->value();

            // Send the System Function event
            auto sys_func = SystemFunc(static_cast<const SystemFuncParam *>(mp)->system_func_type(), 
                                       value,
                                       module());
            sys_func.linked_param = static_cast<const SystemFuncParam *>(mp)->linked_param();
            //sys_func.type = sfc_param->system_func_type();
            if (sys_func.type == SystemFuncType::MULTIFN_SWITCH) {
                // Set the multi-function index
                sys_func.num = static_cast<const SwitchParam *>(param)->multifn_switch_index();
            }
            _event_router->post_system_func_event(new SystemFuncEvent(sys_func));           
        }

        // We need to recurse each mapped param and process it
        // Note: We don't recurse system function params as they are a system action to be performed
        if (mp->type() != ParamType::SYSTEM_FUNC)
            _process_param_changed_mapped_params(mp, diff, param, displayed, force);
    }
}

//----------------------------------------------------------------------------
// _send_knob_control_param_change_events
//----------------------------------------------------------------------------
void SfcManager::_send_knob_control_param_change_events(const Param *param, float diff)
{
    // Send the mapped params changed events
    _process_param_changed_mapped_params(param, diff, nullptr, false);
}

//----------------------------------------------------------------------------
// _send_control_param_change_events
//----------------------------------------------------------------------------
void SfcManager::_send_control_param_change_events(const Param *param, bool force)
{
    // Send the mapped params changed events
    _process_param_changed_mapped_params(param, 0.0, nullptr, false, force);
}

//----------------------------------------------------------------------------
// _set_knob_control_position
//----------------------------------------------------------------------------
void SfcManager::_set_knob_control_position(const KnobParam *param, bool robust)
{    
    // No point doing anything if the Surface Control isn't initialised
    if (_sfc_control_init)
    {
        // The knob number is stored in the param ID
        uint num = param->param_id();

        // Knob 0 is a special case and we *never* go to position on this knob
        // (it is a relative position knob)
        if (num)
        {
            // Is this knob active?
            if (sfc::knob_is_active(num))
            {
                // Get the hardware position
                uint32_t pos = param->hw_value();

                // Set the target knob position in the hardware
                int res = sfc::set_knob_position(num, pos, robust);
                if (res < 0)
                {
                    // Show the error
                    //DEBUG_BASEMGR_MSG("Could not set the knob(" << num << ") position: " << res);
                }
            }
        }
    }
}

//----------------------------------------------------------------------------
// _set_switch_control_value
//----------------------------------------------------------------------------
void SfcManager::_set_switch_control_value(const SwitchParam *param)
{
    // No point doing anything if the Surface Control isn't initialised
    if (_sfc_control_init) {    
        // Get the switch param
        if (param) {
            // The switch number is stored in the param ID
            uint num = param->param_id();
            uint value = (int)param->value();

            // Save the switch logical state so that it can be updated at the next 
            // switch press
            _switch_controls[num].logical_state = value;

            // Set the switch LED state
            _set_switch_led_state(param);
        }
    }
}

//----------------------------------------------------------------------------
// _set_knob_control_haptic_mode
//----------------------------------------------------------------------------
void SfcManager::_set_knob_control_haptic_mode(const KnobParam *param)
{
    // Set the knob haptic mode
    int res = sfc::set_knob_haptic_mode(param->param_id(), param->haptic_mode());
    if (res < 0)
    {
        // Error setting the knob mode
        MSG("ERROR: Could not set a surface control knob haptic mode: " << res);
    }
}

//----------------------------------------------------------------------------
// _process_knob_control
//----------------------------------------------------------------------------
void SfcManager::_process_knob_control(uint num, const sfc::KnobState &knob_state, bool morphing)
{
    // Get the knob control param
    auto *param = static_cast<KnobParam *>(utils::get_param(KnobParam::ParamPath(num).c_str()));
    if (param && (param->module() == MoniqueModule::SFC_CONTROL)) {
        KnobControl &knob_control =  _knob_controls[num];

        // If morphing
        if (morphing) {
            // If this param is NOT morphable, then process the knob normally
            if (!param->morphable()) {
                // Process the knob normally
                _process_physical_knob(param, knob_control, knob_state);
            }

            // Norph the knob - note we need to do this even if the current param is NOT
            // morphable, so that any *other* params mapped to this knob in other states
            // are morphed if required
            _morph_control(sfc::ControlType::KNOB, num);
        }
        else {
            // Process the knob normally
            _process_physical_knob(param, knob_control, knob_state);
        }
    }
}

//----------------------------------------------------------------------------
// _process_switch_control
//----------------------------------------------------------------------------
void SfcManager::_process_switch_control(uint num, const bool *physical_states, bool morphing)
{
    // Get the switch control param
    auto *param = static_cast<SwitchParam *>(utils::get_param(SwitchParam::ParamPath(num).c_str()));
    if (param) {
        // If morphing and morphable
        if (morphing && param->morphable()) {
            // Norph the switch
            _morph_control(sfc::ControlType::SWITCH, num);
        }
        else {
            // Process the switch normally
            _process_physical_switch(param, physical_states);

            // Update the switch physical state for the next poll processing
            _switch_controls[num].physical_state = physical_states[num];                    
        }
    }
}

//----------------------------------------------------------------------------
// _process_physical_knob
//----------------------------------------------------------------------------
void SfcManager::_process_physical_knob(KnobParam *param, KnobControl &knob_control, const sfc::KnobState &knob_state)
{   
    // Process the knob
    // Check if this knob is not moving to target
    if ((knob_state.state & sfc::KnobState::STATE_MOVING_TO_TARGET) == 0) {
        // Has the knob just finished moving to target?
        if (knob_control.moving_to_target) {
            // Skip the processing of this knob for n polls, to allow the motor to settle
            knob_control.poll_skip_count--;

            // Is this the last poll to skip?
            if (knob_control.poll_skip_count == 0) {
                // We can assume the motor has settled into its target position
                // Reset the delta value to zero, and ensure that for subsequent movement checks, the 
                // large threshold is used
                // Note: The large threshold is only used for the first movement after moving to position via
                // an external update
                knob_control.position_delta = 0;
                knob_control.use_large_movement_threshold = true;
                knob_control.moving_to_large_threshold = false;
                knob_control.moving_to_target = false;                    
            }
        }
        else {
            // Knob not moving to target
            // Adjust the position delta
            if (knob_state.position > knob_control.position)
            {
                // Knob position is increasing
                knob_control.position_delta += (knob_state.position - knob_control.position);
            }
            else
            {
                // Knob position is decreasing or not moving
                knob_control.position_delta -= (knob_control.position - knob_state.position);
            }

            // Drift detection
            // Increment the timer and check if we are outside the threshold
            knob_control.polls_since_last_threshold_hit++;
            bool threshold_reached = param->hw_delta_outside_target_threshold(knob_control.position_delta,knob_control.use_large_movement_threshold);

            // If we are beyond the threshold but the timer is expired, then reset the delta
            if((knob_control.polls_since_last_threshold_hit > POLL_THRESH_COUNT) && threshold_reached)
            {
                knob_control.polls_since_last_threshold_hit = 0;
                knob_control.position_delta = 0;
            }
            // If the threshold is reached but the timer hasn't expired then reset the timer
            else if(threshold_reached)
            {
                knob_control.polls_since_last_threshold_hit = 0;
            }

            // Has the knob delta changed so that it is now outside the target threshold?
            if (param->hw_delta_outside_target_threshold(knob_control.position_delta, 
                                                            knob_control.use_large_movement_threshold))
            {
                // Reset the delta value to zero, and ensure that for subsequent movement checks, the 
                // large threshold is not used
                // Note: The large threshold is only used for the first movement after moving to position via
                // an external update
                knob_control.position_delta = 0;
                knob_control.use_large_movement_threshold = false;
                knob_control.small_movement_threshold_count = 0;

                // Update the knob parameter
                auto prev_val = param->value();
                param->set_value_from_hw(knob_state.position);

                // If not in maintenance mode
                if (!utils::maintenance_mode()) {
                    // Send the knob control param change events
                    _send_knob_control_param_change_events(param, (param->value() - prev_val));
                }                                 
            }
            else {
                // If we get here there was either no movement or delta movement below the threshold
                // If NOT using the large threshold
                if (!knob_control.use_large_movement_threshold) {
                    // Increment the small movement threshold count
                    knob_control.small_movement_threshold_count++;

                    // If there have been N successive checks of this knob with no movement, reset to
                    // a large threshold
                    if (knob_control.small_movement_threshold_count >= SMALL_MOVEMENT_THRESHOLD_MAX_COUNT) {
                        knob_control.use_large_movement_threshold = true;
                        knob_control.moving_to_large_threshold = false;
                    }
                }
                else {
                    // We are using the large threshold, has the position exceeded the small threshold?
                    if (param->hw_delta_outside_target_threshold(knob_control.position_delta, false)) {                        
                        if (!knob_control.moving_to_large_threshold) {
                            // Get the start time the knob started moving to the threshold
                            knob_control.large_movement_time_start = std::chrono::steady_clock::now();
                            knob_control.moving_to_large_threshold = true;
                        }
                    }
                }
            }
        }
    }
    else
    {
        // Knob is moving to target - indicate this in the knob control, and set the poll skip
        // count once the knob has reached the target
        knob_control.moving_to_target = true;
        knob_control.poll_skip_count = KNOB_MOVED_TO_TARGET_POLL_SKIP_COUNT;

        // This is to make sure we start in the threshold exeeded state on a knob move
        knob_control.polls_since_last_threshold_hit = POLL_THRESH_COUNT +1;
    }
}

//----------------------------------------------------------------------------
// _process_physical_switch
//----------------------------------------------------------------------------
void SfcManager::_process_physical_switch(SwitchParam *param, const bool *physical_states)
{
    // Send the param change if either not in maintenance mode, or this is one of the soft buttons
    SwitchControl& switch_control = _switch_controls[param->control_num()];
    bool physical_state = physical_states[switch_control.num];
    bool send_param_change = !utils::maintenance_mode() || switch_control.num < 2;

    // Get the switch haptic mode
    auto haptic_mode = param->haptic_mode();

    // If in maintenance mode force the haptic mode to PUSH
    if (utils::maintenance_mode()) {        
        haptic_mode.switch_mode = sfc::SwitchMode::PUSH;
    }

    // If a toggle switch, and the switch been pressed *and* the physical state changed
    if ((haptic_mode.switch_mode == sfc::SwitchMode::TOGGLE) || 
        (haptic_mode.switch_mode == sfc::SwitchMode::TOGGLE_LED_PULSE)) {
        // Is this a grouped switch?
        if (param->grouped_control()) {
            // Get all grouped params - note up to three are currently supported
            auto params = utils::get_grouped_params(param->group_name());
            if ((params.size() == 2) || (params.size() == 3)) {
                SwitchParam *&param1 = param;
                uint param1_num = param->control_num();
                SwitchParam *param2 = nullptr;
                uint param2_num = -1;
                SwitchParam *param3 = nullptr;
                uint param3_num = -1;             
                bool set_switch1_state = false;
                bool set_switch2_state = false;
                bool set_switch3_state = false;
                bool switch1_state = false;
                bool switch2_state = false;
                bool switch3_state = false;

                // Find the second switch
                for (SfcControlParam *p : params) {
                    // If this is not the switch being processed
                    if ((uint)p->param_id() != param1_num) {
                        param2 = static_cast<SwitchParam *>(p);
                        param2_num = p->param_id();
                        break;
                    }                       
                }

                // If there are three switches, find the third
                if (params.size() == 3) {
                    for (SfcControlParam *p : params) {
                        // If this is not the switch being processed
                        if (((uint)p->param_id() != param1_num) && ((uint)p->param_id() != param2_num)) {
                            param3 = static_cast<SwitchParam *>(p);
                            param3_num = p->param_id();
                            break;
                        }
                    }
                }

                // Check all params are valid for the grouped setup - don't process if not
                if ((param2 == nullptr) || ((params.size() == 3) && (param3 == nullptr))) {
                    return;
                }

                // If the switches are all not yet processed
                if (!_switch_controls[param1_num].processed && !_switch_controls[param2_num].processed &&
                    ((params.size() == 2) || !_switch_controls[param3_num].processed)) {
                    // Are all switches 1 and 2 pressed down and a group param specified?
                    // Note: 3 switches pressed down at once is not supported
                    if (param1->group_param() && (param3 == nullptr) &&
                        (physical_states[param1_num] && !_switch_controls[param1_num].physical_state) && 
                        (physical_states[param2_num] && !_switch_controls[param2_num].physical_state)) {
                        // Check if either need their logical state updated
                        set_switch1_state = !_switch_controls[param1_num].logical_state;
                        set_switch2_state = !_switch_controls[param2_num].logical_state;
                        switch1_state = true;
                        switch2_state = true;
                    }
                    // Has the physical state of switch 1 changed?
                    else if (physical_states[param1_num] && !_switch_controls[param1_num].physical_state) {
                        // Switch 1 is being pressed
                        // Is this switch logically OFF
                        if (!_switch_controls[param1_num].logical_state) {
                            // Turn this switch ON, and the others OFF
                            set_switch1_state = true;
                            set_switch2_state = true;
                            set_switch3_state = true;
                            switch1_state = true;
                            switch2_state = false;
                            switch3_state = false;
                        }
                        else {
                            // If switch 2/3 is also ON, switch them OFF
                            switch1_state = true;
                            switch2_state = false;
                            switch3_state = false;                  
                            if (_switch_controls[param2_num].logical_state) {
                                // Turn switch 2 OFF
                                set_switch1_state = true;
                                set_switch2_state = true;
                            }
                            if (param3 && (_switch_controls[param3_num].logical_state)) {
                                // Turn switch 3 OFF
                                set_switch1_state = true;
                                set_switch3_state = true;
                            }
                        }
                    }
                    // Has the physical state of switch 2 changed?
                    else if (physical_states[param2_num] && !_switch_controls[param2_num].physical_state) {
                        // Switch 2 is being pressed
                        // Is this switch logically OFF
                        if (!_switch_controls[param2_num].logical_state) {
                            // Turn this switch ON, and the others OFF
                            set_switch1_state = true;
                            set_switch2_state = true;
                            set_switch3_state = true;                       
                            switch1_state = false;
                            switch2_state = true;
                            switch3_state = false;
                        }
                        else {
                            // If switch 1/3 is also ON, switch them OFF
                            switch1_state = false;
                            switch2_state = true;
                            switch3_state = false;                              
                            if (_switch_controls[param1_num].logical_state) {
                                // Turn switch 1 OFF
                                set_switch1_state = true;
                                set_switch2_state = true;
                            }
                            if (param3 && (_switch_controls[param3_num].logical_state)) {
                                // Turn switch 3 OFF
                                set_switch2_state = true;
                                set_switch3_state = true;
                            }                                
                        } 
                    }
                    // Are there 3 switches and has the physical state of switch 3 changed?
                    else if (param3 && (physical_states[param3_num] && !_switch_controls[param3_num].physical_state)) {
                        // Switch 2 is being pressed
                        // Is this switch logically OFF
                        if (!_switch_controls[param3_num].logical_state) {
                            // Turn this switch ON, and the others OFF
                            set_switch1_state = true;
                            set_switch2_state = true;
                            set_switch3_state = true;                       
                            switch1_state = false;
                            switch2_state = false;
                            switch3_state = true;
                        }
                        else {
                            // If switch 1/2 is also ON, switch them OFF
                            switch1_state = false;
                            switch2_state = false;
                            switch3_state = true;                              
                            if (_switch_controls[param1_num].logical_state) {
                                // Turn switch 1 OFF
                                set_switch1_state = true;
                                set_switch2_state = true;
                            }
                            if (_switch_controls[param2_num].logical_state) {
                                // Turn switch 2 OFF
                                set_switch2_state = true;
                                set_switch3_state = true;
                            }                                
                        }
                    }                            

                    // The grouped switches have been processed
                    // Do we need to change the state of the switches?
                    if (set_switch1_state) {
                        // Set the switch state
                        _switch_controls[param1_num].logical_state = switch1_state;
                        param1->set_value(switch1_state);
                        _set_switch_led_state(param1);
                    }
                    if (set_switch2_state) {
                        // Set the switch state
                        _switch_controls[param2_num].logical_state = switch2_state;
                        param2->set_value(switch2_state);
                        _set_switch_led_state(param2);                                              
                    }
                    if (param3 && set_switch3_state) {
                        // Set the switch state
                        _switch_controls[param3_num].logical_state = switch2_state;
                        param3->set_value(switch3_state);
                        _set_switch_led_state(param3);                                                
                    }

                    // Send the control param change events if needed
                    if (send_param_change) {
                        // If switches 1/2 have been set (this can only happen if a group param is also specified and the group
                        // contains only two switches)
                        if ((param3 == nullptr) && (set_switch1_state || set_switch2_state) && switch1_state && switch2_state) {                       
                            // If the group param is a system function (only one supported ATM)
                            if (param1->group_param() && (param1->group_param()->type() == ParamType::SYSTEM_FUNC)) {
                                // Send the system func event
                                auto sys_func = SystemFunc(static_cast<const SystemFuncParam *>(param1->group_param())->system_func_type(), 
                                                            param1->value(), module());
                                sys_func.linked_param = static_cast<const SystemFuncParam *>(param1->group_param())->linked_param();                           
                                _event_router->post_system_func_event(new SystemFuncEvent(sys_func)); 
                            }
                        }
                        // Send the switch 1 param change if set
                        else if (switch1_state) {
                            _send_control_param_change_events(param1);
                        }
                        // Send the switch 2 param change if set
                        else if (switch2_state) {
                            _send_control_param_change_events(param2);
                        }
                        // Send the switch 3 param change if set
                        else if (param3 && set_switch3_state && switch3_state) {
                            _send_control_param_change_events(param3);
                        }                            
                    }

                    // Indicate these switches have been processed
                    _switch_controls[param1_num].processed = true;
                    _switch_controls[param2_num].processed = true;
                    if (param3) {
                        _switch_controls[param3_num].processed = true;
                    }
                }
                return;
            }
        }

        // Not a grouped switch
        // Has the physical state of this switch changed?
        if (physical_state && !switch_control.physical_state) {
            // If this is a multi-function switch
            if (param->multifn_switch()) {        
                // Will the new switch logical state be OFF?
                if (switch_control.logical_state == ON) {
                    // Can't turn this switch off in position mode
                    // Send the control param change events and return with no further processing
                    if (send_param_change)
                        _send_control_param_change_events(param);                                
                    return;
                }
            }

            // This is a new press of the switch, toggle the logical switch state
            switch_control.logical_state = !switch_control.logical_state;

            // Set the switch LED state
            sfc::set_switch_led_state(switch_control.num, switch_control.logical_state == 0 ? false : true);

            // Update the switch parameter value
            param->set_value(switch_control.logical_state);

            // Send the control param change events
            if (send_param_change)
                _send_control_param_change_events(param);

            // Is this a multi-function switch - not in Sequencer Record mode, as these switches
            // are processed manually
            if (param->multifn_switch() && (utils::multifn_switches_state() != utils::MultifnSwitchesState::SEQ_REC)) {
                // Parse all multi-function switches
                auto params = utils::get_multifn_switch_params();
                for (SwitchParam *sp : params) {
                    if ((sp->param_id() != param->param_id()) && (_switch_controls[sp->param_id()].logical_state)) {
                        _switch_controls[sp->param_id()].logical_state = OFF;

                        // Set the switch LED state
                        sfc::set_switch_led_state(_switch_controls[sp->param_id()].num, false);

                        // Update the switch parameter value
                        sp->set_value(OFF);
                    }
                }
            }

            // If this is a toggle LED pulse switch
            if (haptic_mode.switch_mode == sfc::SwitchMode::TOGGLE_LED_PULSE) {
                // If the switch logical state is ON
                if (switch_control.logical_state) {
                    // If the timer has not been specfied
                    if (switch_control.led_pulse_timer == nullptr) {
                        // Start the switch LED timer task
                        switch_control.led_state = true;
                        switch_control.led_pulse_timer = new Timer(TimerType::PERIODIC);
                        switch_control.led_pulse_timer->start(SWITCH_TOGGLE_LED_DUTY, 
                            std::bind(&SfcManager::_switch_led_pulse_timer_callback, 
                                this, &switch_control));                   
                    }
                }
                else {
                    // If the switch logical state is OFF
                    // Stop and delete the timer
                    if (switch_control.led_pulse_timer) {
                        switch_control.led_pulse_timer->stop();
                        delete switch_control.led_pulse_timer;
                        switch_control.led_pulse_timer = nullptr;

                        // Make sure the LED is OFF
                        sfc::set_switch_led_state(switch_control.num, false);
                    }
                }
            }
        }                        
    }
    // Is the switch haptic mode toggle-on-release?
    else if (haptic_mode.switch_mode == sfc::SwitchMode::TOGGLE_RELEASE)
    {
        // If the switch has been pressed
        if (physical_state && !switch_control.physical_state) {
            // The switch has been pressed, capture the push time start
            switch_control.push_time_start = std::chrono::steady_clock::now();

            // Send the control param change events
            if (send_param_change)
                _send_control_param_change_events(param);                
        }
        // If the switch has been released
        else if (!physical_state && switch_control.physical_state) {
            // If the switch has been released, check if the time is within the toggle-release threshold
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - 
                                                                                switch_control.push_time_start).count();
            if (diff < SWITCH_TOGGLE_RELEASE_THRESHOLD) {
                // Within the toggle-releass threshold - toggle the switch logical state
                switch_control.logical_state = !switch_control.logical_state;

                // Set the switch LED state
                sfc::set_switch_led_state(switch_control.num, switch_control.logical_state == 0 ? false : true);

                // Update the switch parameter value
                param->set_value(switch_control.logical_state);

                // Send the control param change events
                if (send_param_change)
                    _send_control_param_change_events(param);                    
            }
        }
    }
    // Is the switch haptic mode toggle-hold?
    else if (haptic_mode.switch_mode == sfc::SwitchMode::TOGGLE_HOLD)
    {
        // If the switch has been pressed
        if (physical_state && !switch_control.physical_state) {
            // The switch has been pressed, capture the push time start
            switch_control.push_time_start = std::chrono::steady_clock::now();
            switch_control.push_time_processed = false;

            // This is a new press of the switch, toggle the logical switch state
            switch_control.logical_state = !switch_control.logical_state;

            // Set the switch LED state
            sfc::set_switch_led_state(switch_control.num, switch_control.logical_state == 0 ? false : true);

            // Update the switch parameter value
            param->set_value(switch_control.logical_state);

            // Send the control param change events
            if (send_param_change)
                _send_control_param_change_events(param);                
        }
        // If the switch is held down
        else if (physical_state && switch_control.physical_state && !switch_control.push_time_processed) {
            // If the switch is held down, check if the time is outside the toggle-hold threshold
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - 
                                                                                switch_control.push_time_start).count();
            if (diff > SWITCH_TOGGLE_HOLD_THRESHOLD) {
                // Outside the toggle-hold threshold
                // Send the control param change events again
                if (send_param_change) {
                    _send_control_param_change_events(param, true);
                }

                // Indicate we have processed this push time
                switch_control.push_time_processed = true;                
            }
        }
    }
    // Is the switch haptic mode toggle-hold-inverted?
    else if (haptic_mode.switch_mode == sfc::SwitchMode::TOGGLE_HOLD_INVERTED)
    {
        // If the switch has been pressed
        if (physical_state && !switch_control.physical_state) {
            // The switch has been pressed, capture the push time start
            switch_control.push_time_start = std::chrono::steady_clock::now();
            switch_control.push_time_processed = false;
        }
        // If the switch is held down
        else if (physical_state && switch_control.physical_state && !switch_control.push_time_processed) {
            // If the switch is held down, check if the time is outside the toggle-hold threshold
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - 
                                                                                switch_control.push_time_start).count();
            if (diff > SWITCH_TOGGLE_HOLD_THRESHOLD) {
                // Outside the toggle-hold threshold
                // Send the control param change events again
                if (send_param_change) {
                    _send_control_param_change_events(param, true);
                }

                // Indicate we have processed this push time
                switch_control.push_time_processed = true;          
            }
        }
        // If the switch has been released (and a HOLD event not occurred)
        else if (!physical_state && switch_control.physical_state && !switch_control.push_time_processed) {
            // In this mode this is a new press of the switch, toggle the logical switch state
            switch_control.logical_state = !switch_control.logical_state;
            // Set the switch LED state
            sfc::set_switch_led_state(switch_control.num, switch_control.logical_state == 0 ? false : true);

            // Update the switch parameter value
            param->set_value(switch_control.logical_state);

            // Send the control param change events
            if (send_param_change)
                _send_control_param_change_events(param);                
        }        
    }    
    else if (haptic_mode.switch_mode == sfc::SwitchMode::TOGGLE_TRI_STATE) {
        // Has the physical state of this switch changed?
        if (physical_state && !switch_control.physical_state) {
            // This is a new press of the switch, update the logical switch state
            switch_control.logical_state++;
            if (switch_control.logical_state > ON_TRI) {
                switch_control.logical_state = OFF;
            }

            // Update the switch parameter value
            param->set_value(switch_control.logical_state);

            // Set the switch LED state
            _set_switch_led_state(param);                

            // Send the control param change events
            if (send_param_change)
                _send_control_param_change_events(param);
        }                  
    }
    else
    {
        // For all other modes (except for PUSH MANUAL LED), if the Toggle LED timer is active, stop and delete it
        // This might happen if the haptic mode for the switch is changed
        if ((haptic_mode.switch_mode != sfc::SwitchMode::PUSH_MANUAL_LED) && switch_control.led_pulse_timer) {
            switch_control.led_pulse_timer->stop();
            delete switch_control.led_pulse_timer;
            switch_control.led_pulse_timer = nullptr;

            // Make sure the LED is OFF
            sfc::set_switch_led_state(switch_control.num, false);                
        }

        // We need to check for the case where this is a grouped switch
        // We allow the processing of two switches pressed down at once for this type of switch
        if (param->grouped_control() && param->group_param()) {
            // Get all grouped params - note only two are currently supported for push
            auto params = utils::get_grouped_params(param->group_name());
            if (params.size() == 2) {
                uint param1_num = param->control_num();
                int param2_num = -1;

                // Find the second switch
                for (SfcControlParam *p : params) {
                    // If this is not the switch being processed
                    if ((uint)p->param_id() != param1_num) {
                        param2_num = p->param_id();
                        break;
                    }                       
                }

                // Check all params are valid for the grouped setup - don't process if not
                if (param2_num == -1) {
                    return;
                }

                // Make sure param 1 and 2 are in sequential order for processing
                if ((uint)param2_num < param1_num) {
                    auto param_1_num_tmp = param1_num;
                    param1_num = param2_num;
                    param2_num = param_1_num_tmp;
                }

                // If we are not processing the switches as a grouped push, and switch 1 has not already
                // been processed
                if (!_switch_controls[param1_num].grouped_push && !_switch_controls[param1_num].processed) {
                    // If both switches are currently pressed down
                    if (physical_states[param1_num] && physical_states[param2_num]) {                
                        // Both switches have been pressed, capture the push time start
                        _switch_controls[param1_num].push_time_start = std::chrono::steady_clock::now();
                        _switch_controls[param1_num].push_time_processed = false;
                        _switch_controls[param1_num].grouped_push = true;
                        _switch_controls[param1_num].processed = true;
                        
                        // Don't process the switches
                        send_param_change = false;                       
                    }
                }
                else {
                    // If they are being held down
                    if (_switch_controls[param1_num].physical_state && _switch_controls[param2_num].physical_state) {
                        // If the held down processing has not yet been performed
                        if (!_switch_controls[param1_num].push_time_processed) {
                            // If the switches are held down, check if the time is outside the grouped push threshold
                            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - 
                                                                                                _switch_controls[param1_num].push_time_start).count();
                            if (diff > SWITCH_GROUPED_PUSH_HOLD_THRESHOLD) {
                                // Outside the grouped push threshold
                                // Send the control param change event
                                if (send_param_change) {
                                    // If the group param is a system function (only one supported ATM)
                                    if (param->group_param()->type() == ParamType::SYSTEM_FUNC) {
                                        // Send the system func event
                                        auto sys_func = SystemFunc(static_cast<const SystemFuncParam *>(param->group_param())->system_func_type(), 
                                                                param->value(), module());
                                        sys_func.linked_param = static_cast<const SystemFuncParam *>(param->group_param())->linked_param();                           
                                        _event_router->post_system_func_event(new SystemFuncEvent(sys_func));
                                    }
                                    else {
                                        auto mapped_param_change = ParamChange(param->group_param(), module());
                                        _event_router->post_param_changed_event(new ParamChangedEvent(mapped_param_change));

                                        // KINDA HACKY but will do for now
                                        // Is the group param the All Notes Off param?
                                        if (param->group_param() == utils::get_param(MoniqueModule::DAW, Monique::gen_param_id(Monique::GlobalParams::ALL_NOTES_OFF))) {
                                            // Yep - also do a screen capture
                                            _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::SCREEN_CAPTURE_JPG, MoniqueModule::SFC_CONTROL)));  
                                        }                                                                      
                                    }
                                }

                                // Indicate we have processed this push time                                            
                                _switch_controls[param1_num].push_time_processed = true;
                            } 
                        }                                
                    }                        
                    // If there has been a grouped push, don't clear this state until both switches have been released                                    
                    else if (!_switch_controls[param1_num].processed && (!physical_states[param1_num] && !physical_states[param2_num])) {
                        // Clear the grouped push state
                        _switch_controls[param1_num].grouped_push = false;
                    }
                    _switch_controls[param1_num].processed = true;
                    send_param_change = false;
                }
            }
        }

        // If a push button and the physical state has changed
        if (((haptic_mode.switch_mode == sfc::SwitchMode::PUSH) || (haptic_mode.switch_mode == sfc::SwitchMode::PUSH_MANUAL_LED) || 
                (haptic_mode.switch_mode == sfc::SwitchMode::PUSH_NO_LED)) &&
            (physical_state != switch_control.physical_state))
        {
            // Don't process if the physical state and logical state are the same
            if (switch_control.logical_state != physical_state) {      
                // Set the switch logical state (same as physical state)
                switch_control.logical_state = physical_state;

                // Set the switch LED state for PUSH mode
                if (haptic_mode.switch_mode == sfc::SwitchMode::PUSH) {
                    sfc::set_switch_led_state(switch_control.num, switch_control.logical_state == 0 ? false : true);
                }

                // Update the switch parameter value
                param->set_value(switch_control.logical_state);

                // Send the control param change events if needed
                if (send_param_change)
                    _send_control_param_change_events(param);
            }
        }
        // If a latch-push button and the physical state has changed
        else if ((haptic_mode.switch_mode == sfc::SwitchMode::LATCH_PUSH) && (physical_state != switch_control.physical_state))
        {
            // If the Toggle LED timer is active, stop and delete it
            if (switch_control.led_pulse_timer) {
                switch_control.led_pulse_timer->stop();
                delete switch_control.led_pulse_timer;
                switch_control.led_pulse_timer = nullptr;                 
            }
            
            // If the switch has been pushed
            if (physical_state) {
                // If the current logical switch state is OFF
                if (switch_control.logical_state == 0) {
                    // Set the logical state to ON
                    switch_control.push_time_start = std::chrono::steady_clock::now();
                    switch_control.logical_state = 1.0;
                }
                else {
                    // Set the logical state to OFF
                    switch_control.logical_state = 0.0;                    
                }
            }
            else {
                // If the current logical switch state is ON
                if (switch_control.logical_state == 1.0) {
                    // If the switch has been released, check if the time is within the latch threshold
                    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - 
                                                                                        switch_control.push_time_start).count();
                    if (diff < SWITCH_PUSH_LATCH_THRESHOLD) {
                        return;
                    }

                    // Greater than the latch threshold, so don't latch the switch
                    switch_control.logical_state = 0.0;
                }
                else {
                    return;
                }             
            }

            // Set the switch LED state
            sfc::set_switch_led_state(switch_control.num, switch_control.logical_state == 0 ? false : true);

            // Update the switch parameter value
            param->set_value(switch_control.logical_state);

            // Send the control param change events if needed
            if (send_param_change)
                _send_control_param_change_events(param);  
        }
    }
}

//----------------------------------------------------------------------------
// _set_switch_led_state
//----------------------------------------------------------------------------
void SfcManager::_set_switch_led_state(const SwitchParam *param)
{
    SwitchControl& switch_control = _switch_controls[param->param_id()];

    // Is this a tri-state toggle switch?
    if (param->haptic_mode().switch_mode == sfc::SwitchMode::TOGGLE_TRI_STATE) {
        // Parse the new switch value
        switch ((uint)param->value()) {
            case OFF:
            case ON:
                // Stop and delete the timer
                if (switch_control.led_pulse_timer) {
                    switch_control.led_pulse_timer->stop();
                    delete switch_control.led_pulse_timer;
                    switch_control.led_pulse_timer = nullptr;
                }

                // Set the switch LED state appropriately
                sfc::set_switch_led_state(switch_control.num, param->value() == OFF ? false : true);
                break;

            case ON_TRI:
                // If the timer has not been specfied
                if (switch_control.led_pulse_timer == nullptr) {
                    // Start the switch LED timer task
                    switch_control.led_state = true;
                    switch_control.led_pulse_timer = new Timer(TimerType::PERIODIC);
                    switch_control.led_pulse_timer->start(SWITCH_TOGGLE_LED_DUTY, 
                        std::bind(&SfcManager::_switch_led_pulse_timer_callback, 
                            this, &switch_control));                      
                }
                break;

            default:
                break;
        }
    }
    else {
        // If this is a toggle LED pulse switch
        if (param->haptic_mode().switch_mode == sfc::SwitchMode::TOGGLE_LED_PULSE) {
            // If the switch logical state is ON
            if (param->value()) {
                // If the timer has not been specfied
                if (switch_control.led_pulse_timer == nullptr) {
                    // Start the switch LED timer task
                    switch_control.led_state = true;
                    switch_control.led_pulse_timer = new Timer(TimerType::PERIODIC);
                    switch_control.led_pulse_timer->start(SWITCH_TOGGLE_LED_DUTY, 
                        std::bind(&SfcManager::_switch_led_pulse_timer_callback, 
                            this, &switch_control));

                    // Make sure the LED is ON
                    sfc::set_switch_led_state(switch_control.num, true);                                               
                }
            }
            else {
                // If the switch logical state is OFF
                // Stop and delete the timer
                if (switch_control.led_pulse_timer) {
                    switch_control.led_pulse_timer->stop();
                    delete switch_control.led_pulse_timer;
                    switch_control.led_pulse_timer = nullptr;

                    // Make sure the LED is OFF
                    sfc::set_switch_led_state(switch_control.num, false);
                }
            }
        }
        else {
            // Stop and delete the timer - if we are switching the LED off
            if (switch_control.led_pulse_timer && (param->value() == OFF)) {               
                switch_control.led_pulse_timer->stop();
                delete switch_control.led_pulse_timer;
                switch_control.led_pulse_timer = nullptr;
            }
        
            // Set the switch LED state appropriately
            sfc::set_switch_led_state(switch_control.num, param->value() == OFF ? false : true);
        }
    }
}

//----------------------------------------------------------------------------
// _set_switch_led_state
//----------------------------------------------------------------------------
void SfcManager::_set_switch_led_state(const SwitchParam *param, SwitchValue led_state)
{
    SwitchControl& switch_control = _switch_controls[param->param_id()];

    // Parse the new switch LED state
    switch (led_state) {
        case OFF:
        case ON:
            // Stop and delete the timer
            if (switch_control.led_pulse_timer) {
                switch_control.led_pulse_timer->stop();
                delete switch_control.led_pulse_timer;
                switch_control.led_pulse_timer = nullptr;
            }

            // Set the switch LED state appropriately
            sfc::set_switch_led_state(switch_control.num, led_state == OFF ? false : true);
            break;

        case ON_TRI:
            // If the timer has not been specfied
            if (switch_control.led_pulse_timer == nullptr) {
                // Start the switch LED timer task
                switch_control.led_state = true;
                switch_control.led_pulse_timer = new Timer(TimerType::PERIODIC);
                switch_control.led_pulse_timer->start(SWITCH_TOGGLE_LED_DUTY, 
                    std::bind(&SfcManager::_switch_led_pulse_timer_callback, 
                        this, &switch_control));                 
            }
            break;

        default:
            break;
    }
}

//----------------------------------------------------------------------------
// _commit_led_control_states
//----------------------------------------------------------------------------
void SfcManager::_commit_led_control_states()
{
    // Only process if the Surface Control is initialised
    if (_sfc_control_init)
    {
        // Commit all LED states in the hardware
        int res = sfc::commit_led_states();
        if (res < 0)
        {
            // Show the error
            //DEBUG_BASEMGR_MSG("Could not commit the LED states: " << res);
        }
    }
}

//----------------------------------------------------------------------------
// _morph_control
//----------------------------------------------------------------------------
void SfcManager::_morph_control(sfc::ControlType type, uint num)
{
    std::string control_path;

    // Get the Surface Control path
    if (type == sfc::ControlType::KNOB)
        control_path = KnobParam::ParamPath(num);
    else if (type == sfc::ControlType::SWITCH)
        control_path = SwitchParam::ParamPath(num);
    else
    {
        // Can only morph knobs and switches
        return;
    }

    // Get the control param
    auto control_param = static_cast<SfcControlParam *>(utils::get_param(control_path));

    // We need too check all control states, including controls not currently shown 
    // on the front panel (e.g. LFO 2/3 if showing LFO 1)
    for (uint i=0; i<control_param->num_control_states(); i++) {
        // If the param in this control state is morphable
        if (control_param->morphable(i)) {
            // Get the mapped params for this control state
            auto mapped_params = control_param->mapped_params(i);
            for (Param *mp : mapped_params) {
                // Only process mapped DAW params, and only the first one mapped
                // Do NOT process mod matrix params
                if ((mp->module() == MoniqueModule::DAW) && !mp->mod_matrix_param()) {
                    // If the control value has changed
                    if ((control_param->value(i) != mp->value()) || _preset_reloaded) {
                        // Set the control value
                        control_param->set_value_from_param(i, *mp);

                        // If this is the current control state, also set the knob position
                        if (control_param->is_current_control_state(i)) {
                            _process_sfc_param_changed(control_param);
                        }
                    }
                    break;
                }
            }
        }
    }
}

//----------------------------------------------------------------------------
// _switch_led_pulse_timer_callback
//----------------------------------------------------------------------------
void SfcManager::_switch_led_pulse_timer_callback(SwitchControl *switch_control)
{
    // Toggle the switch LED state
    switch_control->led_state = !switch_control->led_state;
    sfc::set_switch_led_state(switch_control->num, switch_control->led_state);
}

//----------------------------------------------------------------------------
// _register_params
//----------------------------------------------------------------------------
void SfcManager::_register_params()
{
	// Register the surface params
    // Register the Knob controls
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {   
        // Register the knob controls
	    utils::register_param(std::move(KnobParam::CreateParam(i)));
    }

    // Register the Switch controls
    for (uint i=0; i<NUM_PHYSICAL_SWITCHES; i++)
    {
        // Register the switch control
	    utils::register_param(std::move(SwitchParam::CreateParam(i)));
    }
}

//----------------------------------------------------------------------------
// _process_sfc_control
//----------------------------------------------------------------------------
static void *_process_sfc_control(void* data)
{
    auto sfc_manager = static_cast<SfcManager*>(data);
    sfc_manager->process_sfc_control();

    // To suppress warnings
    return nullptr;
}
