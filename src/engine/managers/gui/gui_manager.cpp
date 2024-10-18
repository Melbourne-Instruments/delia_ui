/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  gui_manager.h
 * @brief GUI Manager class implementation (main functions).
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <cstdlib>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include <regex>
#include <algorithm>
#include <filesystem>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/reboot.h>
#include "gui_manager.h"
#include "seq_manager.h"
#include "arp_manager.h"
#include "file_manager.h"
#include "utils.h"
#include "logger.h"
#include "version.h"
#include "data_conversion.h"

// Constants
constexpr char GUI_MSG_QUEUE_NAME[]            = "/delia_msg_queue";
constexpr uint GUI_MSG_QUEUE_SIZE              = 50;
constexpr uint GUI_PARAM_CHANGE_SEND_POLL_TIME = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(17)).count();
constexpr uint PARAM_CHANGED_SHOWN_THRESHOLD   = std::chrono::milliseconds(50).count();
constexpr uint MAX_MOD_MATRIX_SRC              = 20;
constexpr uint TEMPO_BPM_FINE_NUM_ADJ_POS      = (10*2) + 1;

// Private variables
const char *_knob_names[] = {
    "DATA",
    "TEMPO/GLIDE",
    "OSC 1/3 TUNE/TUNE",
    "OSC 1/3 SHAPE/POS",
    "OSC 1/3 LEVEL",
    "FILTER HP CUTOFF",
    "FILTER LP CUTOFF",
    "EG LEVEL",
    "OSC 2/4 TUNE/TYPE",
    "OSC 2/4 SHAPE/TONE",
    "OSC 2/4 LEVEL",
    "FILTER DRIVE",
    "FILTER RESONANCE",
    "EG ATTACK",
    "EG RELEASE",
    "EG SUSTAIN",
    "EG RELEASE",
    "LFO RATE",
    "LFO GAIN",
    "EFFECTS SEND/PARAM",
    "MORPH POSITION"
};

//----------------------------------------------------------------------------
// GuiManager
//----------------------------------------------------------------------------
GuiManager::GuiManager(EventRouter *event_router) : 
    BaseManager(MoniqueModule::GUI, "GuiManager", event_router)
{
    mq_attr attr;

    // Open the GUI Message Queue
    std::memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = GUI_MSG_QUEUE_SIZE;
    attr.mq_msgsize = sizeof(GuiMsg);
    _gui_mq_desc = ::mq_open(GUI_MSG_QUEUE_NAME, (O_CREAT|O_WRONLY|O_NONBLOCK),
                             (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH),
                             &attr);
    if (_gui_mq_desc == (mqd_t)-1)
    {
        // Error opening the GUI Message Queue
        MSG("ERROR: Could not open the GUI Message Queue: " << errno);
    }

    // Initialise class data
    _sw_manager = nullptr;
    _param_changed_listener = 0;
    _system_func_listener = 0;
    _reload_presets_listener = 0;
    _midi_event_listener = 0;
    _gui_param_change_send_timer = new Timer(TimerType::PERIODIC);
    _param_change_timer = new Timer(TimerType::ONE_SHOT);
    _demo_mode_timer = new Timer(TimerType::ONE_SHOT);
    _gui_state = GuiState::INVALID;
    _msd_event_thread = nullptr;
    _run_msd_event_thread = false;
    _selected_bank_index = -1;
    _selected_preset_index = -1;
    _loaded_preset_index = -1;
    _selected_patch_src_index = 0;
    _selected_patch_dst_index = 0;
    _show_undo_last_load = false;
    _selected_preset_id.set_id("", "");
    _show_param_list = false;
    _param_shown = 0;
    _param_shown_root = 0;
    _param_shown_index = -1;
    _new_param_list = false;
    _editing_param = false;
    _param_change_available = false;
    _num_list_items = 0;
    _list_items.clear();
    _new_mod_matrix_param_list = false;
    _selected_mod_matrix_src_index = -1;
    _system_menu_state = SystemMenuState::SHOW_OPTIONS;
    _selected_system_menu_item = 0;
    _scope_mode = SoundScopeMode::SCOPE_MODE_OSC;
    _show_scope = true;
    _manage_preset_state = ManagePresetState::LOAD;
    _select_preset_state = SelectPresetState::SELECT_PRESET;
    _save_preset_state = SavePresetState::SELECT_PRESET;
    _edit_name_state = EditNameState::NONE;
    _save_edit_name = "";
    _selected_char_index = 0;
    _selected_list_char = 0;
    _reload_presets_from_select_preset_load = 0;
    _calibrate_state = CalibrateState::CALIBRATE_STARTED;
    _progress_state = ProgressState::NOT_STARTED;
    _renaming_patch = false;
    _soft_button_1_is_latched_edit = false;
    _show_patch_names = false;
    _showing_additional_mod_dst_params = false;
    _bank_management_state = BankManagmentState::SHOW_LIST;
    _import_bank_state = ImportBankState::NONE;
    _export_bank_state = ExportBankState::NONE;
    _clear_bank_state = ClearBankState::NONE;
    _selected_bank_management_item = 0;
    _selected_bank_archive = 0;
    _selected_bank_archive_name = "";
    _selected_bank_dest = 0;
    _selected_bank_dest_name = "";
    _wt_management_state = WtManagmentState::SHOW_LIST;
    _showing_wt_prune_confirm_screen = false;
    _selected_wt_management_item = 0;
    _backup_state = BackupState::BACKUP_STARTED;
    _show_ext_system_menu = false;
    _ext_sys_menu_just_shown = false;
    _wheels_calibrate_state = WheelsCalibrateState::WHEELS_CALIBRATE_NOT_STARTED;
    _running_background_test = false;
    _wheel_check_val_ok = false;
    _layer_2_voices_changed = false;
    _qa_check_ok = false;
    _mix_vca_cal_status_ok = false;
    _filter_cal_status_ok = false;
    _wheels_cal_status_ok = false;
    _run_diag_script_state = RunDiagScriptState::NONE;
    _selected_diag_script = 0;
}

//----------------------------------------------------------------------------
// ~GuiManager
//----------------------------------------------------------------------------
GuiManager::~GuiManager()
{
    // Stop the GUI param change send timer task
    if (_gui_param_change_send_timer) {
        _gui_param_change_send_timer->stop();
        delete _gui_param_change_send_timer;
        _gui_param_change_send_timer = 0;
    }

    // Stop the param change timer task
    if (_param_change_timer) {
        _stop_param_change_timer();
        delete _param_change_timer;
        _param_change_timer = 0;
    }

    // Stop the store demo mode timer task
    if (_demo_mode_timer) {
        _demo_mode_timer->stop();
        delete _demo_mode_timer;
        _demo_mode_timer = 0;
    }

    // MSD event task running?
    if (_msd_event_thread != 0)
    {
        // Stop the MSD event task
        _run_msd_event_thread = false;
		if (_msd_event_thread->joinable())
			_msd_event_thread->join(); 
        _msd_event_thread = 0;       
    }

    // Is the GUI Message Queue open?
    if (_gui_mq_desc != (mqd_t)-1)
    {
        // Close the GUI Message Queue - don't unlink, this is done by the GUI app
        ::mq_close(_gui_mq_desc);
        _gui_mq_desc = (mqd_t)-1;
    }

    // Clean up the event listeners
    if (_param_changed_listener)
        delete _param_changed_listener;      
    if (_system_func_listener)
        delete _system_func_listener;         
    if (_reload_presets_listener)
        delete _reload_presets_listener;      
    if (_midi_event_listener)
        delete _midi_event_listener;      
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool GuiManager::start()
{
    // Get the Software Manager
    _sw_manager = static_cast<SwManager *>(utils::get_manager(MoniqueModule::SOFTWARE));

	// Start the GUI param change  send timer periodic thread
	_gui_param_change_send_timer->start(GUI_PARAM_CHANGE_SEND_POLL_TIME, std::bind(&GuiManager::_gui_param_change_send_callback, this));

    // Process the presets (pass NULL as the event)
    _process_reload_presets(nullptr);

    // Get the Mod Matrix param source and destination names
    uint index = 0;
    auto params = utils::get_mod_matrix_params();
    for (LayerStateParam *p : params) {
        // Is this source name already in the list of names?
        if (std::find(_mod_matrix_src_names.begin(), _mod_matrix_src_names.end(), p->mod_src_name()) == _mod_matrix_src_names.end()) {
            if (index < MAX_MOD_MATRIX_SRC) {
                // Add the Mod Matrix source name
                _mod_matrix_src_names.push_back(p->mod_src_name());
                index++;
            }
        }

        // Is this destination name already in the list of names?
        if (std::find(_mod_matrix_dst_names.begin(), _mod_matrix_dst_names.end(), p->mod_dst_name()) == _mod_matrix_dst_names.end()) {
            // Add the Mod Matrix destination name
            _mod_matrix_dst_names.push_back(p->mod_dst_name());
        }
    }

    // All ok, call the base manager
    return BaseManager::start();
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void GuiManager::process()
{
    // Create and add the various event listeners
    _param_changed_listener = new EventListener(MoniqueModule::SYSTEM, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_param_changed_listener);
    _system_func_listener = new EventListener(MoniqueModule::SYSTEM, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_system_func_listener);
    _reload_presets_listener = new EventListener(MoniqueModule::FILE_MANAGER, EventType::RELOAD_PRESETS, this);
    _event_router->register_event_listener(_reload_presets_listener);
    _midi_event_listener = new EventListener(MoniqueModule::MIDI_DEVICE, EventType::MIDI, this);
    _event_router->register_event_listener(_midi_event_listener);

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void GuiManager::process_event(const BaseEvent *event)
{
    // Process the event depending on the type
    switch (event->type())
    {
        case EventType::PARAM_CHANGED:
        {
            // Process the Param Changed event - and re-start the demo mode timer
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            _start_demo_mode_timer(); 
            break;
        }

        case EventType::SYSTEM_FUNC:
        {
            // Process the System Function event - and re-start the demo mode timer
            _process_system_func_event(static_cast<const SystemFuncEvent *>(event)->system_func());
            _start_demo_mode_timer();
            break;            
        }

        case EventType::RELOAD_PRESETS:
            // Process reloading of the presets
            // Note re-starting the demo mode is done within this function
            _process_reload_presets(static_cast<const ReloadPresetsEvent *>(event));
            _start_demo_mode_timer();
            break;

        case EventType::MIDI:
            // Process the MIDI event - and re-start the demo mode timer
            _process_midi_event(static_cast<const MidiEvent *>(event)->seq_event());
            _start_demo_mode_timer();            
            break;

        default:
            // Event unknown, we can ignore it
            break;
    }
}

//----------------------------------------------------------------------------
// process_msd_event
//----------------------------------------------------------------------------
void GuiManager::process_msd_event()
{
    bool msd_mounted = _sw_manager->msd_mounted();

    // Do until exited
    while (_run_msd_event_thread) {
        // Sleep for a second
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Get the MSD status - has it changed?
        bool current_msd_mounted = _sw_manager->msd_mounted();
        if (msd_mounted != current_msd_mounted) {
            // Refresh the System Menu if being shown
            if ((_gui_state == GuiState::SYSTEM_MENU) && (_system_menu_state == SystemMenuState::SHOW_OPTIONS)) {
                _show_system_menu_screen();
            }
            else if ((_gui_state == GuiState::BANK_MANAGMENT) && (_bank_management_state == BankManagmentState::SHOW_LIST)) {
                _show_bank_management_screen();
            }
            else if ((_gui_state == GuiState::WAVETABLE_MANAGEMENT) && (_wt_management_state == WtManagmentState::SHOW_LIST)) {
                _show_wt_management_screen();
            }
            msd_mounted = current_msd_mounted;
        }
    }
}

//----------------------------------------------------------------------------
// show_msg_box
//----------------------------------------------------------------------------
void GuiManager::show_msg_box(const char *line1, const char *line2)
{
    // Show the message box
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, line1);
    std::strcpy(msg.msg_box.line_2, line2);
    _post_gui_msg(msg);    
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void GuiManager::_process_param_changed_event(const ParamChange &data)
{
    // Check for the special case of Tempo BPM - this is shown in the status bar
    if (data.param == utils::get_tempo_param()) {
        // Update the Tempo Status
        _set_tempo_status(data.param->display_string().second);
    }

    // Check for the special case of Pitch Bend/Mod Wheelk - this is shown as part of the
    // calibration process/QA status
    if ((_gui_state == GuiState::WHEELS_CALIBRATE) || (_gui_state == GuiState::QA_STATUS)) {
        // Is this a pitch bend param change?
        if (data.param == utils::get_param(utils::ParamRef::MIDI_PITCH_BEND)) {
            // Are we showing the QA status?
            if (_gui_state == GuiState::QA_STATUS) {
                // Update the QA status screen
                auto mw_param = utils::get_param(utils::ParamRef::MIDI_MODWHEEL); 
                auto at_param = utils::get_param(utils::ParamRef::MIDI_AFTERTOUCH);              
                _show_calibration_status_screen(false,
                                                dataconv::pitch_bend_from_normalised_float(data.param->value()),
                                                dataconv::midi_cc_from_normalised_float(mw_param->value()),
                                                dataconv::aftertouch_from_normalised_float(at_param->value()));
            }
            else {
                // Parse the calibration state
                switch (_wheels_calibrate_state) {
                    case WheelsCalibrateState::PITCH_BEND_WHEEL_TOP_CHECK:
                        // Show the value
                        _show_pitch_bend_wheel_top_check_screen(dataconv::pitch_bend_from_normalised_float(data.param->value()));
                        return;

                    case WheelsCalibrateState::PITCH_BEND_WHEEL_MID_CHECK:
                        // Show the value
                        _show_pitch_bend_wheel_mid_check_screen(dataconv::pitch_bend_from_normalised_float(data.param->value()));
                        return;
                    
                    case WheelsCalibrateState::PITCH_BEND_WHEEL_BOTTOM_CHECK:
                        // Show the value
                        _show_pitch_bend_wheel_bottom_check_screen(dataconv::pitch_bend_from_normalised_float(data.param->value()));
                        return;

                    default:
                        break;
                }
            }
        }
        // Is this a mod wheel param change?
        else if (data.param == utils::get_param(utils::ParamRef::MIDI_MODWHEEL)) {
            // Are we showing the QA status?
            if (_gui_state == GuiState::QA_STATUS) {
                // Update the QA status screen - mod wheel value
                auto pb_param = utils::get_param(utils::ParamRef::MIDI_PITCH_BEND);
                auto at_param = utils::get_param(utils::ParamRef::MIDI_AFTERTOUCH);
                _show_calibration_status_screen(false, 
                                                dataconv::pitch_bend_from_normalised_float(pb_param->value()),
                                                dataconv::midi_cc_from_normalised_float(data.param->value()),
                                                dataconv::aftertouch_from_normalised_float(at_param->value()));
            }
            else {            
                // Parse the calibration state
                switch (_wheels_calibrate_state) {
                    case WheelsCalibrateState::MOD_WHEEL_TOP_CHECK:
                        // Show the value
                        _show_mod_wheel_top_check_screen(dataconv::midi_cc_from_normalised_float(data.param->value()));
                        return;

                    case WheelsCalibrateState::MOD_WHEEL_BOTTOM_CHECK:
                        // Show the value
                        _show_mod_wheel_bottom_check_screen(dataconv::midi_cc_from_normalised_float(data.param->value()));
                        return;

                    default:
                        break;
                }
            }
        }
        // Is this an aftertouch param change?
        else if (data.param == utils::get_param(utils::ParamRef::MIDI_AFTERTOUCH)) {
            // Are we showing the QA status?
            if (_gui_state == GuiState::QA_STATUS) {
                // Update the QA status screen - aftertouch value
                auto pb_param = utils::get_param(utils::ParamRef::MIDI_PITCH_BEND);
                auto mw_param = utils::get_param(utils::ParamRef::MIDI_MODWHEEL);
                _show_calibration_status_screen(false, 
                                                dataconv::pitch_bend_from_normalised_float(pb_param->value()),
                                                dataconv::midi_cc_from_normalised_float(mw_param->value()),
                                                dataconv::aftertouch_from_normalised_float(data.param->value()));
            }
        }                
    }

    // Check if we should display this param change event
    if (data.display && (_gui_state <= GuiState::MOD_MATRIX)) {
        // Get the GUI mutex
        std::lock_guard<std::mutex> guard(_gui_mutex);

        // Get the changed param
        auto param = utils::get_param(data.param->path().c_str());
        if (param) {
            // If we are currently showing a param and the param in this change event is different
            if (_param_shown && (_param_shown != param)) {
                // Check for how long we have shown the current param
                // If less than a specific threshold, then don't show this param change event
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - 
                                                                                  _param_shown_start_time).count();
                if (diff < PARAM_CHANGED_SHOWN_THRESHOLD) {
                    // Don't show this param change
                    return;
                }
            }

            // Check we can show this param (don't process surface control params)
            if ((std::strlen(param->display_name()) > 0) && (param->param_list().size() > 0) &&
                (param->module() != MoniqueModule::SFC_CONTROL)) {
                // A trivial param is a position param with just two values (i.e. a switch)
                bool trivial_param = param->num_positions() == 2;

                //  If we are editing the currently shown param AND it is shown as an enum list
                if ((_param_shown == param) && _editing_param && _show_param_as_enum_list(param)) {
                    // Update the enum param and return
                    _show_enum_param();
                    return;
                }

                // Check if this param is part of the root param list
                int index = _get_root_param_list_index(param);

                // If this param is not the currently shown param
                if (_param_shown != param) {
                    // If we are showing a param shortcut, then don't show
                    // this param update
                    if (_showing_param_shortcut) {
                        return;
                    }

                    // Exit edit mode if we are currently editing - but ONLY if:
                    // - We are showing a param list and this param is from the same root
                    //   param list OR
                    // - We are not showing a param list and this param is a not a trivial param
                    if (_editing_param && 
                        ((_show_param_list && (index >= 0)) ||
                         (!_show_param_list && !trivial_param))) {
                        _editing_param = false;
                        _new_param_list = true;
                        _showing_param_shortcut = false;
                        _reset_param_shortcut_switches();                        
                        _config_soft_button_1(true, true);
                    }
                    // If we are in the Mod Matrix GUI state
                    else if (_gui_state == GuiState::MOD_MATRIX) {
                        // Is this param in the current Mod Matrix list?
                        // If not it probably means the additional Mod Matrix list
                        // is being shown
                        if (index == -1) {
                            // If this param is not in the main param list, don't show it
                            if (_is_main_mod_matrix_param(param)) {
                                // Show the main mod matrix param list
                                _showing_additional_mod_dst_params = false;
                                _show_mod_matrix_src_screen();
                            }
                            return;
                        }

                        // Make sure the list is shown in case this param is
                        // not displayed yet
                        _new_param_list = true;
                    }
                }

                // Is the param in the current root param list?
                if (index >= 0) {
                    // If we are not showing a param list, and this is a trivial param OR
                    // the Tempo BPM param, do not show
                    if (!_show_param_list && (trivial_param || (param == utils::get_tempo_param()))) {
                        return;
                    }

                    // If we are editing a param and it is the param shown, exit edit mode
                    if ((_param_shown == param) && _editing_param) {
                        // Exit edit mode
                        _editing_param = false;                     
                        _config_soft_button_1(true, true);
                        _config_data_knob(-1, -1);                        
                    }

                    // Param was found, so set it as the param to show
                    // Note: The shown index is calculated for Mod Matrix processing, so don't
                    // update it here if in the Mod Matrix state
                    _param_shown = param;
                    if (_gui_state != GuiState::MOD_MATRIX) {
                        _param_shown_index = index;
                    }
                    else {
                        _new_param_list = true;
                    }
                }
                else {
                    // Param was not found - if we are already showing (a different) param list,
                    // OR it is a trivial or Tempo BPM param, do not show
                    if (_show_param_list || (trivial_param || (param == utils::get_tempo_param()))) {
                        return;
                    }

                    // Param not found, which means this param now becomes
                    // the new root param                    
                    _param_shown_root = param;
                    _new_param_list = true;
                    if (!_new_param_list) {
                        _new_param_list = true;
                    //    _reset_param_shortcut_switches();
                    }
                    _show_param_list = false;
                    _show_patch_names = false;
                    //_set_sys_func_switch(SystemFuncType::SEQ_SETTINGS, false);

                    // Find the param in the current root param list
                    index = _get_root_param_list_index(param);
                    if (index >= 0) {
                        _param_shown = param;
                        _param_shown_index = index;
                    }
                }
                if (index >= 0) {
                    // Start the param change timer
                    if (!_editing_param) {
                        _start_param_change_timer();
                    }
                    
                    // Show this param change
                    _param_shown_start_time = std::chrono::steady_clock::now();
                    _param_change_available = true;
                }
            }
        }
    }
    // TODO
    // Is this the All Notes Off param for Layer 1?
    //else if ((data.path == utils::get_param_from_ref(utils::ParamRef::ALL_NOTES_OFF)->get_path()) &&
    //         ((data.layers_mask & LayerInfo::GetLayerMaskBit(0)) == LayerInfo::GetLayerMaskBit(0))) {
    //    // Stop the Sequencer if it is running
    //    _start_stop_seq_run(false);
    //}
}

//----------------------------------------------------------------------------
// _process_reload_presets
//----------------------------------------------------------------------------
void GuiManager::_process_reload_presets(const ReloadPresetsEvent *event)
{
    // Always show the Tempo Status in the status bar
    auto param = utils::get_tempo_param();
    if (param) {
        _set_tempo_status(param->display_string().second);
    }

    // If the event is valid
    if (event) {
        // If not loaded via the select setup load screen
        if (_reload_presets_from_select_preset_load == 0) {
            // Has this reload occurred due to a Layer or A/B toggle?
            if (event->from_layer_toggle() || event->from_ab_toggle()) {
                // If this is due to a Layer 2 number of voices change, don't set
                // the Layer select switches, leave them showing Layer 2
                if (!_layer_2_voices_changed) {
                    // Make sure the Layer select switches are in the correct state
                    if (utils::is_current_layer(LayerId::D0)) {
                        _set_sys_func_switch(SystemFuncType::SELECT_LAYER_1, ON);
                        _set_sys_func_switch(SystemFuncType::SELECT_LAYER_2, OFF);
                    }
                    else {
                        _set_sys_func_switch(SystemFuncType::SELECT_LAYER_1, OFF);
                        _set_sys_func_switch(SystemFuncType::SELECT_LAYER_2, ON);                        
                    }
                }

                // Make sure the A/B switch is in the correct state
                _set_sys_func_switch(SystemFuncType::TOGGLE_PATCH_STATE, 
                        ((utils::get_current_layer_info().layer_state() == LayerState::STATE_A) ? false : true));

                // Make sure the KBD Octave Offset inc/dec buttons are in the correct state
                auto octave_offset_param = utils::get_param(utils::ParamRef::OCTAVE_OFFSET);
                if (octave_offset_param) {
                    // Set the LED states
                    _set_kbd_octave_led_states(octave_offset_param->hr_value());
                }                        
                
                // If this is from a Layer toggle
                if (event->from_layer_toggle()) {
                    // Update the Layer status
                    _set_layer_status();

                    // If not due to a Layer 2 number of voices change
                    if (!_layer_2_voices_changed) {
                        // Are we currently showing the PATCH or MULTI params?
                        if (_param_shown_root && 
                            (((_param_shown_root->module() == MoniqueModule::SYSTEM) && (_param_shown_root->param_id() == SystemParamId::PATCH_NAME_PARAM_ID)) ||
                            (_param_shown_root == utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES)) ||
                            (_param_shown_root == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES)))) {
                            // Swap the param shown root if we are showing L1/L2 voices
                            if (_param_shown_root == utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES)) {
                                _param_shown_root = utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES);
                                if (_param_shown == utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES)) {
                                    _param_shown = utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES);
                                }
                            }
                            else if (_param_shown_root == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES)) {
                                _param_shown_root = utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES);
                                if (_param_shown == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES)) {
                                    _param_shown = utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES);
                                }                            
                            }
                            
                            // Update the system menu shown - make sure editing is also off
                            _new_param_list = true;
                            _editing_param = false;
                            _config_soft_button_1(true, true);
                            _update_param_list();
                            return;
                        }
                    }
                    else {
                        // The Layer 2 number of voices has changed, we need to make sure
                        // Layer 2 is still shown, even if Layer 1 is loaded due to the number of voices
                        // being changed to zero
                        auto param = utils::get_param(utils::ParamRef::SELECTED_LAYER);
                        param->set_value(1);

                        // Update the system menu shown - this will show the full menu if the number
                        // of voices is > 0, or just the nhmber of voices param if = 0
                        _new_param_list = true;
                        _update_param_list();
                        _layer_2_voices_changed = false;
                        return;
                    }
                }

                // Reset the UI state and show the home screen
                _config_sys_func_switches(true);
                _gui_state = GuiState::INVALID;
                _reset_gui_state_and_show_home_screen();            
            }
        }
        else {
            // Always reset the UI states
            utils::set_osc_state(utils::OscState::OSC_1_OSC_2_STATE);
            utils::set_osc_tune_state(utils::OscTuneState::OSC_TUNE_FINE);
            utils::set_lfo_state(utils::LfoState::LFO_1_STATE);
            utils::set_res_state(utils::ResState::RES_LP_STATE);
            utils::set_eg_state(utils::EgState::VCA_EG_STATE);
            utils::set_fx_state(utils::FxState::FX_SEND_STATE);
            utils::set_tempo_glide_state(utils::TempoGlideState::TEMPO_STATE);

            // If we are not in the LOAD state
            if (!((_gui_state == GuiState::MANAGE_PRESET) && (_manage_preset_state == ManagePresetState::LOAD))) {
                // Show the default screen
                _gui_state = GuiState::INVALID;
                _reset_gui_state_and_show_home_screen();
            }
            else {               
                // Update the Layer status
                _set_layer_status();

                // Update the various control states
                utils::set_controls_state(utils::default_ui_state(), _event_router);
                utils::set_controls_state(utils::osc_ui_state(), _event_router);
                utils::set_controls_state(utils::lfo_ui_state(), _event_router);
                utils::set_controls_state(utils::res_ui_state(), _event_router);
                utils::set_controls_state(utils::eg_ui_state(), _event_router);
                utils::set_controls_state(utils::fx_ui_state(), _event_router);
                utils::set_controls_state(utils::osc_tune_ui_state(), _event_router);
                utils::set_controls_state(utils::lfo_rate_ui_state(), _event_router);
                utils::set_controls_state(utils::tempo_glide_ui_state(), _event_router);
            }

            // Configure the switches state
            _config_sys_func_switches(true);
            _set_sys_func_switch(SystemFuncType::OSC_1_OSC_2_SELECT, ON);
            _set_sys_func_switch(SystemFuncType::OSC_3_OSC_4_SELECT, OFF);
            _set_sys_func_switch(SystemFuncType::VCF_EG_SELECT, OFF);
            _set_sys_func_switch(SystemFuncType::VCA_EG_SELECT, ON);
            _set_sys_func_switch(SystemFuncType::AUX_EG_SELECT, OFF);
            _set_sys_func_switch(SystemFuncType::LFO_1_SELECT, ON);
            _set_sys_func_switch(SystemFuncType::LFO_2_SELECT, OFF);
            _set_sys_func_switch(SystemFuncType::LFO_3_SELECT, OFF);
            _set_sys_func_switch(SystemFuncType::FX_MACRO, OFF);
            _set_sys_func_switch(SystemFuncType::TOGGLE_PATCH_STATE, OFF);
            auto octave_offset_param = utils::get_param(utils::ParamRef::OCTAVE_OFFSET);
            if (octave_offset_param) {
                // Set the LED states
                _set_kbd_octave_led_states(octave_offset_param->hr_value());
            }            
            _set_sys_func_switch(SystemFuncType::SELECT_LAYER_1, ON);
            _set_sys_func_switch(SystemFuncType::SELECT_LAYER_2, OFF);
            _reload_presets_from_select_preset_load--;
        }
    }
}

//----------------------------------------------------------------------------
// _process_midi_event
//----------------------------------------------------------------------------
void GuiManager::_process_midi_event(const snd_seq_event_t &seq_event)
{
    // If this is a note ON
    if (seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) {
        // Are we editing the Layer MIDI low note filter?
        if ((_param_shown == utils::get_param(utils::ParamRef::MIDI_LOW_NOTE_FILTER)) && _editing_param) {
            // Update the low note from the keypress
            _param_shown->set_value_from_position(seq_event.data.note.note);

            // Post a param change message
            auto param_change = ParamChange(_param_shown, module());
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

            // Update the value shown
            _show_param_update(false);
        }
        // Are we editing a Layer MIDI high note filter?
        else if ((_param_shown == utils::get_param(utils::ParamRef::MIDI_HIGH_NOTE_FILTER)) && _editing_param) {
            // Update the high note from the keypress
            _param_shown->set_value_from_position(seq_event.data.note.note);

            // Post a param change message
            auto param_change = ParamChange(_param_shown, module());
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

            // Update the value shown       
            _show_param_update(false);
        }        
    }
}

//----------------------------------------------------------------------------
// _process_system_func_event
//----------------------------------------------------------------------------
void GuiManager::_process_system_func_event(const SystemFunc &system_func)
{
    // Parse the system function
    switch (system_func.type) {
        case SystemFuncType::INIT_PRESET: {
            // Make sure the INIT PRESET is handled as a full preset load
            _reload_presets_from_select_preset_load++;
            break;
        }

        case SystemFuncType::MOD_MATRIX: {
            // Process the MOD system function
            _process_mod(system_func);
            break;
        }

        case SystemFuncType::LOAD:
        case SystemFuncType::SAVE: {
            // Process the load/save system function
            _process_load_save(system_func.value, (system_func.type == SystemFuncType::LOAD ? false : true));
            break;
        }

        case SystemFuncType::OSC_1_OSC_2_SELECT: {
            // Process the OSC 1/2 Select system function
            _process_osc_select(system_func, utils::OscState::OSC_1_OSC_2_STATE);
            break;
        }

        case SystemFuncType::OSC_3_OSC_4_SELECT: {
            // Process the OSC 3/4 Select system function
            _process_osc_select(system_func, utils::OscState::OSC_3_OSC_4_STATE);
            break;
        }        

        case SystemFuncType::OSC_COARSE: {
            // Process the OSC Coarse system function
            _process_osc_coarse(system_func.value ? utils::OscTuneState::OSC_TUNE_COARSE : utils::OscTuneState::OSC_TUNE_FINE);
            break;
        }

        case SystemFuncType::LFO_1_SELECT: {
            // Process the LFO 1 Select
            _process_lfo_select(system_func, utils::LfoState::LFO_1_STATE);
            break;
        }

        case SystemFuncType::LFO_2_SELECT: {
            // Process the LFO 2 Select 
            _process_lfo_select(system_func, utils::LfoState::LFO_2_STATE);
            break;
        }

        case SystemFuncType::LFO_3_SELECT: {
            // Process the LFO 3 Select 
            _process_lfo_select(system_func, utils::LfoState::LFO_3_STATE);
            break;
        }

        case SystemFuncType::LFO_SHAPE: {
            // Process the LFO Shape
            _process_lfo_shape(system_func);
            break;
        }

        case SystemFuncType::VCF_LP_CUTOFF_MODE: {
            // Process the VCF LP Cutoff mode
            _process_vcf_lp_cutoff_mode(system_func);
            break;
        }

        case SystemFuncType::VCF_CUTOFF_LINK: {
            // Process the VCF Cutoff link
            _process_vcf_cutoff_link(system_func);
            break;
        }

        case SystemFuncType::VCF_RES_SELECT: {
            // Process the Resonance LP/HP select
            _process_res_select(system_func.value ? utils::ResState::RES_LP_STATE : utils::ResState::RES_HP_STATE);
            break;
        }

        case SystemFuncType::VCF_EG_SELECT: {
            // Process the VCF EG Select 
            _process_eg_select(system_func, utils::EgState::VCF_EG_STATE);
            break;
        }

        case SystemFuncType::VCA_EG_SELECT: {
            // Process the VCA EG Select 
            _process_eg_select(system_func, utils::EgState::VCA_EG_STATE);
            break;
        }

        case SystemFuncType::AUX_EG_SELECT: {
            // Process the AUX EG Select 
            _process_eg_select(system_func, utils::EgState::AUX_EG_STATE);
            break;
        }

        case SystemFuncType::FX_MACRO: {
            // Process the FX Macro
            _process_fx_select(system_func.value ? utils::FxState::FX_PARAM_STATE : utils::FxState::FX_SEND_STATE);
            break;
        }

        case SystemFuncType::FX_PARAM: {
            // Process the FX Param
            _process_fx_param(system_func.value);
            break;
        }

        case SystemFuncType::OCTAVE_DEC: {
            // Process the KBD octave decrement
            _process_octave_dec(system_func.value);
            break;
        }

        case SystemFuncType::OCTAVE_INC: {
            // Process the KBD octave increment
            _process_octave_inc(system_func.value);
            break;
        }

        case SystemFuncType::SEQ_REC: {
            // Process the Sequencer record function
            _process_seq_rec(system_func.value);
            break;          
        }

        case SystemFuncType::SEQ_RUN: {
            // Process the Sequencer run function
            _process_seq_run(system_func.value);
            break;          
        }        

        case SystemFuncType::PRESET_INC: {
            // Process the preset increment functionality
            _process_preset_inc(system_func.value);
            break;
        }

        case SystemFuncType::PRESET_DEC: {
            // Process the preset decrement functionality
            _process_preset_dec(system_func.value);
            break;
        }

        case SystemFuncType::SELECT_LAYER_1: {
            // Process the select Layer 1 functionality
            _process_select_layer_1();
            break;
        }

        case SystemFuncType::SELECT_LAYER_2: {
            // Process the select Layer 2 functionality
            _process_select_layer_2();
            break;
        }

        case SystemFuncType::BANK: {
            // Process the BANK functionality
            _process_select_bank();
            break;
        }

        case SystemFuncType::OSC_MENU: {
            // Process the OSC Menu functionality
            _process_osc_menu(system_func); 
            break;
        }

        case SystemFuncType::LFO_MENU: {
            // Process the LFO Menu functionality
            _process_lfo_menu(system_func); 
            break;
        }

        case SystemFuncType::MULTI_MENU:
            // Process the MULTI Menu functionality
            _process_multi_menu(system_func);
            break;

        case SystemFuncType::WAVE_MENU:
            // Process the WAVE Menu functionality
            _process_wave_menu(system_func);
            break;

        case SystemFuncType::SEQ_MENU:
        case SystemFuncType::PATCH_MENU:
        case SystemFuncType::ARP_MENU:
        case SystemFuncType::VCF_MENU:
        case SystemFuncType::FX1_MENU:
        case SystemFuncType::FX2_MENU: {
            // Process the standard Menu functionality
            _process_system_func_param(system_func, system_func.linked_param, false, true); 
            break;
        }

        case SystemFuncType::ENV_MENU: {
            // Process the ENV Menu functionality
            _process_env_menu(system_func); 
            break;
        }

        case SystemFuncType::TEMPO_SELECT: {
            // Process the Tempo select functionality
            _process_tempo_select(system_func);
            break;
        }

        case SystemFuncType::GLIDE_SELECT: {
            // Process the Glide select functionality
            _process_glide_select(system_func);
            break;
        }

        case SystemFuncType::TOGGLE_PATCH_STATE: {
            // Process the toggle patch state functionality
            _process_toggle_patch_state(system_func);
            break;
        }

        case SystemFuncType::MULTIFN_SWITCH:
        {
            // Process depending on the current state
            switch (_gui_state) {
                case GuiState::MOD_MATRIX:
                    // Process the keypress for Mod Matrix
                    _process_mod_matrix_multifn_switch(system_func.num);
                    break;

                default:
                    // No processing
                    break;
            }
            break;
        }

        case SystemFuncType::SOFT_BUTTON_1: {
            // Process soft button 1
            _process_soft_button_1(system_func.value);
            break;            
        }

        case SystemFuncType::SOFT_BUTTON_2: {
            // Process soft button 2
            _process_soft_button_2(system_func.value);
            break;
        }

        case SystemFuncType::DATA_KNOB: {
            auto knob_param = utils::get_data_knob_param();
            if (knob_param) {
                switch (_gui_state) {
                    case GuiState::MANAGE_PRESET:
                        // Process the data knob for select bank/setup
                        if (_select_preset_state == SelectPresetState::SELECT_BANK) {
                            _process_select_bank_data_knob(*knob_param);
                        }
                        else if (_select_preset_state == SelectPresetState::SELECT_PRESET) {
                            _process_select_preset_data_knob(*knob_param);
                        }
                        else {
                            _manage_preset_state == ManagePresetState::LOAD_INTO_SELECT_SRC ?
                                _process_select_patch_src_data_knob(*knob_param) :
                                _process_select_patch_dst_data_knob(*knob_param);
                        }
                        break;

                    case GuiState::SHOW_PARAM:
                    case GuiState::MOD_MATRIX:
                        // Process the data knob when showing a param
                        _process_show_param_data_knob(*knob_param);
                        break;

                    case GuiState::SYSTEM_MENU:
                        // Process the data knob for the System Menu
                        _process_system_menu_data_knob(*knob_param);
                        break;

                    case GuiState::BANK_MANAGMENT:
                        // Process the data knob for bank managenebt
                        _process_bank_management_data_knob(*knob_param);
                        break;

                    case GuiState::WAVETABLE_MANAGEMENT:
                        // Process the data knob for wavetable management
                        _process_wt_management_data_knob(*knob_param);
                        break;                        

                    case GuiState::RUN_DIAG_SCRIPT:
                        // Process the data knob for running a diag script
                        _process_run_diag_script_data_knob(*knob_param);
                        break;

                    default:
                        break;
                }
            }
            break;
        }

        case SystemFuncType::START_SW_UPDATE: {
            // Show the starting software update screen
            _show_start_sw_update_screen(system_func.str_value);
            break;       
        }        

        case SystemFuncType::FINISH_SW_UPDATE: {
            // Show the finishing software update screen
            _show_finish_sw_update_screen(system_func.str_value, system_func.result);
            break;                       
        }

        case SystemFuncType::SFC_INIT: {
            // The surface has been initialised - clear the boot warning screen
            // Wait an additional 100ms to let the Surface module settle down before
            // clearing the boot screen and continuing
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto msg = GuiMsg(GuiMsgType::CLEAR_BOOT_WARNING_SCREEN);
            _post_gui_msg(msg);

            // If not in maintenance mode
            if (!utils::maintenance_mode()) {
                // Check if any of the motors failed to start
                uint num_failed = 0;
                int mc_1_failed_num = -1;
                int mc_2_failed_num = -1;
                for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++) {
                    if (!sfc::knob_is_active(i)) {
                        num_failed++;
                        if (mc_1_failed_num == -1) {
                            mc_1_failed_num = i;
                        }
                        else if (mc_2_failed_num == -1) {
                            mc_2_failed_num = i;
                        }
                    }
                } 

                // Show a pop-up if any knob motor failed to start
                if (num_failed) {
                    // Reset the GUI state
                    _reset_gui_state();                    
                    _gui_state = GuiState::MOTOR_STARTUP_FAILED;                  

                    // Show the message box
                    auto msg = GuiMsg();
                    msg.type = GuiMsgType::SHOW_MSG_BOX;
                    msg.msg_box.show = true;
                    msg.msg_box.show_hourglass = false;
                    std::string str1 = "Motor Startup FAILED (" + std::to_string(num_failed) + "):";
                    std::strcpy(msg.msg_box.line_1, str1.c_str());
                    std::strcpy(msg.msg_box.line_2, _knob_names[mc_1_failed_num]);
                    mc_2_failed_num != -1 ?
                        std::strcpy(msg.msg_box.line_3, _knob_names[mc_2_failed_num]) :
                        std::strcpy(msg.msg_box.line_3, "");
                    _post_gui_msg(msg);
                    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
                    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "OK");
                    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "----");
                    _post_gui_msg(msg);
                }
                else {
                    // Reset the GUI state and show the home screen
                    _gui_state = GuiState::INVALID;
                    _reset_gui_state_and_show_home_screen();
                }          
            }

            // Make sure the KBD Octave Offset inc/dec buttons are in the correct state
            auto octave_offset_param = utils::get_param(utils::ParamRef::OCTAVE_OFFSET);
            if (octave_offset_param) {
                // Set the LED states
                _set_kbd_octave_led_states(octave_offset_param->hr_value());
            }            
            break;                  
        }

        case SystemFuncType::SCREEN_CAPTURE_JPG: {
            // Only process the button press on RELEASE
            if (!system_func.value)  {            
                // Send a screen capture message
                auto msg = GuiMsg(GuiMsgType::SCREEN_CAPTURE);
                _post_gui_msg(msg);
            }
            break;
        }

        default:
            break;        
    }
}

//----------------------------------------------------------------------------
// _process_mod
//----------------------------------------------------------------------------
void GuiManager::_process_mod(const SystemFunc& sys_func)
{
    // Are we entering the Mod Matrix state?
    if (sys_func.value) {
        // Stop the param change timer
        _stop_param_change_timer();

        // Reset the GUI state and set the state to Mod Matrix
        _reset_gui_state();
        _gui_state = GuiState::MOD_MATRIX;

        // Get the current (saved) mod matrix source aindex
        _selected_mod_matrix_src_index = utils::system_config()->get_mod_src_num() - 1;

        // We need to convert the index into a Mod Matrix source
        Monique::ModMatrixSrc state = Monique::ModMatrixSrc::KEY_PITCH;
        if (_selected_mod_matrix_src_index < static_cast<int>(Monique::ModMatrixSrc::CONSTANT)) {
            state = static_cast<Monique::ModMatrixSrc>(_selected_mod_matrix_src_index);
        }

        // Set the Mod Matrix state
        _set_mod_matrix_state(state);

        // Make sure the other system function switches are reset and disable the
        // param shortcut switches (except for the sequencer run)
        _reset_sys_func_switches(SystemFuncType::MOD_MATRIX);
        _config_sys_func_switches(false);
        utils::set_osc_tune_state(utils::OscTuneState::OSC_TUNE_FINE);

        // Show the intial Mod Matrix screen
        _showing_additional_mod_dst_params = false;
        _show_mod_matrix_src_screen();

        // Configure the multi-function switches
        utils::config_multifn_switches(_selected_mod_matrix_src_index, utils::MultifnSwitchesState::MOD_MATRIX, _event_router);

        // Get the Cutoff Link param
        auto param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::CUTOFF_LINK_PARAM_ID);
        if (param) {
            // Set the linked param state
            auto lp = utils::get_param(_mod_matrix_param_path(_selected_mod_matrix_src_index, (uint)Monique::ModMatrixDst::FILTER_CUTOFF_HP));
            lp->enable_linked_param(param->value() ? true : false);
        }
    }
    else {
        // We need to make sure the LFO rate is in the correct state
        // Get the relevant LFO Tempo Sync param
        Param *param;
        if (utils::lfo_state() == utils::LfoState::LFO_1_STATE) {
            param = utils::get_param(utils::ParamRef::LFO_1_TEMPO_SYNC);
        }
        else if (utils::lfo_state() == utils::LfoState::LFO_2_STATE) {
            param = utils::get_param(utils::ParamRef::LFO_2_TEMPO_SYNC);
        }
        else {
            param = utils::get_param(utils::ParamRef::LFO_3_TEMPO_SYNC);
        }
        bool lfo_tempo_sync = param->value();

        // Set the LFO rate controls state
        utils::set_lfo_rate_state(lfo_tempo_sync ? utils::LfoRateState::LFO_SYNC_RATE : utils::LfoRateState::LFO_NORMAL_RATE);

        // Reset the GUI state and show the home screen
        _reset_gui_state_and_show_home_screen();
    }
}

//----------------------------------------------------------------------------
// _process_load_save
//----------------------------------------------------------------------------
void GuiManager::_process_load_save(bool selected, bool save)
{
    // Stop the param change timer
    _stop_param_change_timer(); 
    
    // Selected functionality?
    if (selected) {
        // Show the select Preset load/save screen
        _reset_gui_state();
        _selected_preset_id = utils::system_config()->preset_id();
        _show_select_preset_load_save_screen(save);

        // Make sure the other system function switches are reset
        _config_sys_func_switches(true);
        _reset_sys_func_switches(save ? SystemFuncType::SAVE : SystemFuncType::LOAD);

        // Reset the multi-function switches
        utils::reset_multifn_switches(_event_router);
    }
    else {
        // Reset the GUI state and show the home screen
        _reset_gui_state_and_show_home_screen();
    }
}

//----------------------------------------------------------------------------
// _process_osc_select
//----------------------------------------------------------------------------
void GuiManager::_process_osc_select(const SystemFunc &sys_func, utils::OscState state)
{
    utils::OscState prev_state = utils::osc_state();

    // If we are not already in that OSC state
    if (utils::osc_state() != state) {
        // Set the new OSC state
        utils::set_osc_state(state);

        // Set the controls state for OSC 1/2 or OSC 3/4
        // Note: The OSC Tune control state is set separately
        utils::set_controls_state(utils::osc_ui_state(), _event_router);
        if (_gui_state == GuiState::MOD_MATRIX) {
            utils::set_controls_state(utils::osc_tune_ui_state(), _event_router);
            utils::set_controls_state(utils::mod_osc_ui_state(), _event_router);
        }
        else if (utils::osc_state() != prev_state) {
            // Set the Oscillator Tune controls state
            utils::set_controls_state(utils::osc_tune_ui_state(), _event_router);
        }              
    }

    // Can we also show the OSC tune state, and potentially the OSC params list?
    if (_gui_state < GuiState::MOD_MATRIX) {
        // Are we showing a params list?
        if (_show_param_list && _param_shown_root) {
            // If we are in the OSC 1/2 state
            Param *param = nullptr;
            if (state == utils::OscState::OSC_1_OSC_2_STATE) {
                // Are we showing the OSC 1 list?
                if (_param_shown_root == utils::get_param(utils::ParamRef::OSC_1_FINE_TUNE)) {
                    // Toggle and show the OSC 2 list
                    param = utils::get_param(utils::ParamRef::OSC_2_FINE_TUNE);
                    _set_sys_func_switch_led_state(SystemFuncType::OSC_1_OSC_2_SELECT, SwitchValue::ON_TRI);
                }
                // Are we showing the OSC 2/3/4 List?
                else if (_param_shown_root == utils::get_param(utils::ParamRef::OSC_2_FINE_TUNE) ||
                         _param_shown_root == utils::get_param(utils::ParamRef::OSC_3_FINE_TUNE) ||
                         _param_shown_root == utils::get_param(utils::ParamRef::OSC_4_NOISE_MODE)) {
                    // Toggle and show the OSC 1 list
                    param = utils::get_param(utils::ParamRef::OSC_1_FINE_TUNE);
                    if (_param_shown_root == utils::get_param(utils::ParamRef::OSC_2_FINE_TUNE)) {
                        _set_sys_func_switch_led_state(SystemFuncType::OSC_1_OSC_2_SELECT, SwitchValue::ON);
                    }
                }

                // Process the system function if possible
                if (param) {
                    _process_system_func_param(sys_func, param, false, true);
                }
            }
            else {
                // Are we showing the OSC 3 list?
                if (_param_shown_root == utils::get_param(utils::ParamRef::OSC_3_FINE_TUNE)) {
                    // Toggle and show the OSC 4 list
                    param = utils::get_param(utils::ParamRef::OSC_4_NOISE_MODE);
                    _set_sys_func_switch_led_state(SystemFuncType::OSC_3_OSC_4_SELECT, SwitchValue::ON_TRI);
                }
                // Are we showing the OSC 1/2/4 List?
                else if (_param_shown_root == utils::get_param(utils::ParamRef::OSC_1_FINE_TUNE) ||
                         _param_shown_root == utils::get_param(utils::ParamRef::OSC_2_FINE_TUNE) ||
                         _param_shown_root == utils::get_param(utils::ParamRef::OSC_4_NOISE_MODE)) {
                    // Toggle and show the OSC 3 list
                    param = utils::get_param(utils::ParamRef::OSC_3_FINE_TUNE);
                    if (_param_shown_root == utils::get_param(utils::ParamRef::OSC_4_NOISE_MODE)) {
                        _set_sys_func_switch_led_state(SystemFuncType::OSC_3_OSC_4_SELECT, SwitchValue::ON);
                    }                    
                }

                // Process the system function if possible
                if (param) {
                    _process_system_func_param(sys_func, param, false, true);
                }                
            }
        }
        else {
            // Did the OSC state change?
            if (utils::osc_state() != prev_state) {            
                // Reset the GUI state and show the home screen
                _stop_param_change_timer();
                _reset_gui_state_and_show_home_screen();
            }       
        }
    }
}

//----------------------------------------------------------------------------
// _process_osc_coarse
//----------------------------------------------------------------------------
void GuiManager::_process_osc_coarse(utils::OscTuneState state)
{
    // Can we process Coarse?
    if (_gui_state != GuiState::MOD_MATRIX) {
        // If we are not already in that OSC tune state
        if (utils::osc_tune_state() != state) {                    
            // Set the new OSC Tune state
            utils::set_osc_tune_state(state);

            // Set the Oscillator Tune controls state
            utils::set_controls_state(utils::osc_tune_ui_state(), _event_router);

            // // Reset the GUI state and show the home screen if possible
            if ((_gui_state < GuiState::SYSTEM_MENU) && (!_show_param_list || !_param_shown_root)) {
                _stop_param_change_timer();
                _reset_gui_state_and_show_home_screen();
            }
        }
    }
}

//----------------------------------------------------------------------------
// _process_lfo_select
//----------------------------------------------------------------------------
void GuiManager::_process_lfo_select(const SystemFunc &sys_func, utils::LfoState state)
{
    utils::LfoState prev_state = utils::lfo_state();

    // If we are not already in that LFO state
    if (utils::lfo_state() != state) {
        // Set the new LFO state
        utils::set_lfo_state(state);

        // Set the LFO controls state
        utils::set_controls_state(utils::lfo_ui_state(), _event_router);
        if (_gui_state == GuiState::MOD_MATRIX) {
            utils::set_controls_state(utils::mod_lfo_ui_state(), _event_router);
        }
        else if (utils::lfo_state() != prev_state) {
            // Get the relevant LFO Tempo sync param
            Param *param;
            if (utils::lfo_state() == utils::LfoState::LFO_1_STATE) {
                param = utils::get_param(utils::ParamRef::LFO_1_TEMPO_SYNC);
            }
            else if (utils::lfo_state() == utils::LfoState::LFO_2_STATE) {
                param = utils::get_param(utils::ParamRef::LFO_2_TEMPO_SYNC);
            }
            else {
                param = utils::get_param(utils::ParamRef::LFO_3_TEMPO_SYNC);
            }
            bool lfo_tempo_sync = param->value();

            // Set the new LFO rate controls state
            utils::set_lfo_rate_state(lfo_tempo_sync ? utils::LfoRateState::LFO_SYNC_RATE : utils::LfoRateState::LFO_NORMAL_RATE);
            utils::set_controls_state(utils::lfo_rate_ui_state(), _event_router);
        }         

        // Are we not in the Mod Matrix state?
        if (_gui_state < GuiState::MOD_MATRIX) {
            // Are we showing a params list?
            if (_show_param_list && _param_shown_root) {        
                // Are we currently showing LFO params?
                if ((_param_shown_root == utils::get_param(utils::ParamRef::LFO_1_SLEW)) ||
                    (_param_shown_root == utils::get_param(utils::ParamRef::LFO_2_SLEW)) ||
                    (_param_shown_root == utils::get_param(utils::ParamRef::LFO_3_SLEW))) {
                    // We need to get the linked param to show
                    Param *param;
                    if (state == utils::LfoState::LFO_1_STATE) {
                        param = utils::get_param(utils::ParamRef::LFO_1_SLEW);
                    }
                    else if (state == utils::LfoState::LFO_2_STATE) {
                        param = utils::get_param(utils::ParamRef::LFO_2_SLEW);
                    }
                    else {
                        param = utils::get_param(utils::ParamRef::LFO_3_SLEW);
                    }
                    
                    // Process the system function
                    _process_system_func_param(sys_func, param); 
                }
            }
            // If we are just showing the LFO Shape
            else if (!_show_param_list &&
                    ((_param_shown_root == utils::get_param(utils::ParamRef::LFO_1_SHAPE)) ||
                    (_param_shown_root == utils::get_param(utils::ParamRef::LFO_2_SHAPE)) ||
                    (_param_shown_root == utils::get_param(utils::ParamRef::LFO_3_SHAPE)))) {
                // Show the LFO shape
                auto sf = sys_func;
                sf.type = SystemFuncType::LFO_SHAPE;
                _process_lfo_shape(sf);
            }
            else {
                // Reset the GUI state and show the home screen
                _stop_param_change_timer();
                _reset_gui_state_and_show_home_screen();              
            }
        }
    }
}

//----------------------------------------------------------------------------
// _process_lfo_shape
//----------------------------------------------------------------------------
void GuiManager::_process_lfo_shape(const SystemFunc& sys_func)
{
    // Can we show this system function related param?
    if (_gui_state != GuiState::MOD_MATRIX) {
        Param *param;

        // Get the LFO shape param to show
        if (utils::lfo_state() == utils::LfoState::LFO_1_STATE) {
            param = utils::get_param(utils::ParamRef::LFO_1_SHAPE);
        }
        else if (utils::lfo_state() == utils::LfoState::LFO_2_STATE) {
            param = utils::get_param(utils::ParamRef::LFO_2_SHAPE);
        }
        else {
            param = utils::get_param(utils::ParamRef::LFO_3_SHAPE);
        }
        
        // Selected functionality and a linked param has been specified?
        if (sys_func.value && param) {
            // Stop the param change timer
            _stop_param_change_timer();

            // This param will become the root param - get the index
            // of this param in the param list
            auto index = _get_param_list_index(param, param);
            if (index >= 0) {
                // Reset the GUI state                  
                _reset_gui_state();
                _reset_sys_func_switches(sys_func.type);
                _config_soft_button_1(false, true);

                // Setup the param shown settings
                _param_shown_root = param;
                _param_shown = param;
                _param_shown_index = index;
                _show_param_list = false;
                _gui_state = GuiState::SHOW_PARAM;

                // Indicate we are editing
                _editing_param = true;
                _showing_param_shortcut = true;               

                // Show the param as an enum list                   
                _show_enum_param();
            }
        }
        else {
            // Reset the GUI state and show the home screen
            _reset_gui_state_and_show_home_screen();
        }
    }
}

//----------------------------------------------------------------------------
// _process_vcf_lp_cutoff_mode
//----------------------------------------------------------------------------
void GuiManager::_process_vcf_lp_cutoff_mode(const SystemFunc& sys_func)
{
    // If the linked param has been specified
    if (sys_func.linked_param) {
        // Update the linked param
        sys_func.linked_param->set_value(sys_func.value);
        auto param_change = ParamChange(sys_func.linked_param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

        // Can we show the popup?
        if (_gui_state <= GuiState::MOD_MATRIX) {
            // Are we in the home/param state?
            if (_gui_state < GuiState::MOD_MATRIX) {
                // Are we showing the VCF param list?
                if (_show_param_list && (_param_shown_root == utils::get_param(utils::ParamRef::VCF_HP_CUTOFF))) {
                    //  If we are editing the currently shown param AND it is shown as an enum list
                    if ((_param_shown == sys_func.linked_param) && _editing_param && _show_param_as_enum_list(sys_func.linked_param)) {
                        // Update the enum param and return
                        _show_enum_param();
                    }
                    else {
                        // Update the list and select the param
                        auto index = _get_param_list_index(_param_shown_root, sys_func.linked_param);
                        if (index >= 0) {
                            // Show the param
                            _param_shown = sys_func.linked_param;
                            _param_shown_index = index;
                            _editing_param = false;
                            _new_param_list = false;
                            _config_soft_button_1(true, true);
                            _show_param();
                        }
                    }
                }
                else {
                    // Show a popup info screen
                    _show_msg_popup("LP SLOPE:", (sys_func.value ? "24dB" : "12dB"));
                }
            }
            else {
                // Show a popup info screen
                _show_msg_popup("LP SLOPE:", (sys_func.value ? "24dB" : "12dB"));
            }
        }
    }    
}

//----------------------------------------------------------------------------
// _process_vcf_cutoff_link
//----------------------------------------------------------------------------
void GuiManager::_process_vcf_cutoff_link(const SystemFunc& sys_func)
{
    // If the linked param has been specified
    if (sys_func.linked_param) {
        // Enable/disable the processing of this param linked param - for the normal param and current Mod Matrix param (if in
        // the Mod Matrix state)
        sys_func.linked_param->enable_linked_param(sys_func.value ? true : false);
        if (_gui_state == GuiState::MOD_MATRIX) {
            auto lp = utils::get_param(_mod_matrix_param_path(_selected_mod_matrix_src_index, (uint)Monique::ModMatrixDst::FILTER_CUTOFF_HP));
            lp->enable_linked_param(sys_func.value ? true : false);
        }

        // Set the saved Cuttoff Link param
        auto param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::CUTOFF_LINK_PARAM_ID);
        if (param) {
            // Update the value and post a param change message
            param->set_value(sys_func.value ? 1.0f : 0.0f);
            auto param_change = ParamChange(param, module());
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
        }
    }
}

//----------------------------------------------------------------------------
// _process_res_select
//----------------------------------------------------------------------------
void GuiManager::_process_res_select(utils::ResState state)
{
    // Set the new Resonance state
    utils::set_res_state(state);

    // Set the Resonance controls state
    utils::set_controls_state(_gui_state == GuiState::MOD_MATRIX ? utils::mod_res_ui_state() : utils::res_ui_state(), _event_router);

    // Can we show the popup?
    if (_gui_state <= GuiState::MOD_MATRIX) {      
        // Are we in the home/param state?
        if (_gui_state < GuiState::MOD_MATRIX) {
            // Are we showing the VCF param list?
            if (_show_param_list && (_param_shown_root == utils::get_param(utils::ParamRef::VCF_HP_CUTOFF))) {
                // Update the list and select the resonance
                auto param = state == utils::ResState::RES_HP_STATE ?
                                            utils::get_param(utils::ParamRef::VCF_HP_RESONANCE) :
                                            utils::get_param(utils::ParamRef::VCF_LP_RESONANCE);
                auto index = _get_param_list_index(_param_shown_root, param);
                if (index >= 0) {
                    // Show the param
                    _param_shown = param;
                    _param_shown_index = index;
                    _editing_param = false;
                    _new_param_list = false;
                    _config_soft_button_1(true, true);
                    _show_param();
                }
            }
            else {             
                // Show a popup info screen
                _show_msg_popup("RESONANCE:", (state == utils::ResState::RES_LP_STATE ? "LP" : "HP")); 
            }               
        }
        else {
            // Show a popup info screen
            _show_msg_popup("RESONANCE:", (state == utils::ResState::RES_LP_STATE ? "LP" : "HP"));
        }        
    }
}

//----------------------------------------------------------------------------
// _process_eg_select
//----------------------------------------------------------------------------
void GuiManager::_process_eg_select(const SystemFunc& sys_func, utils::EgState state)
{
    // If we are not already in that EG state
    if (utils::eg_state() != state) {    
        // Set the new EG state
        utils::set_eg_state(state);
        
        // Set the EG controls state
        utils::set_controls_state(utils::eg_ui_state(), _event_router);
        if (_gui_state == GuiState::MOD_MATRIX) {
            utils::set_controls_state(utils::mod_eg_ui_state(), _event_router);
        }

        // Are we not in the Mod Matrix state?
        if (_gui_state < GuiState::MOD_MATRIX) {
            // Are we showing a params list?
            if (_show_param_list && _param_shown_root) {        
                // Are we currently showing ENV params?
                if ((_param_shown_root == utils::get_param(utils::ParamRef::VCF_EG_RESET)) ||
                    (_param_shown_root == utils::get_param(utils::ParamRef::VCA_EG_RESET)) ||
                    (_param_shown_root == utils::get_param(utils::ParamRef::AUX_EG_RESET))) {
                    // We need to get the linked param to show
                    Param *param;
                    if (utils::eg_state() == utils::EgState::VCF_EG_STATE) {
                        param = utils::get_param(utils::ParamRef::VCF_EG_RESET);
                    }
                    else if (utils::eg_state() == utils::EgState::VCA_EG_STATE) {
                        param = utils::get_param(utils::ParamRef::VCA_EG_RESET);
                    }
                    else {
                        param = utils::get_param(utils::ParamRef::AUX_EG_RESET);
                    }
                    
                    // Process the system function
                    _process_system_func_param(sys_func, param, false, true); 
                }
            }
            // If not showing a param short-cut
            else if (!_showing_param_shortcut) {
                // Show the attack param with the visualiser
                Param *param;
                if (utils::eg_state() == utils::EgState::VCF_EG_STATE) {
                    param = utils::get_param(utils::ParamRef::VCF_ENV_ATTACK);
                }
                else if (utils::eg_state() == utils::EgState::VCA_EG_STATE) {
                    param = utils::get_param(utils::ParamRef::VCA_ENV_ATTACK);
                }
                else {
                    param = utils::get_param(utils::ParamRef::AUX_ENV_ATTACK);
                }

                auto index = _get_param_list_index(param, param);
                if (index >= 0) {
                    // Reset the GUI state                  
                    _reset_gui_state();

                    // Setup the param shown settings
                    _param_shown_root =  param->param_list().front();
                    _param_shown = param;
                    _param_shown_index = index;
                    _show_param_list = false;
                    _editing_param = false;
                    _show_param();
                    _config_soft_button_1(false, true);
                    _start_param_change_timer();  
                }        
            } 
        }
    }
}

//----------------------------------------------------------------------------
// _process_fx_select
//----------------------------------------------------------------------------
void GuiManager::_process_fx_select(utils::FxState state)
{
    // Set the FX state
    utils::set_fx_state(state);
    
    // Set the FX controls state
    (_gui_state == GuiState::MOD_MATRIX) ?
        utils::set_controls_state(utils::mod_fx_ui_state(), _event_router) :
        utils::set_controls_state(utils::fx_ui_state(), _event_router);
}

//----------------------------------------------------------------------------
// _process_fx_param
//----------------------------------------------------------------------------
void GuiManager::_process_fx_param(float value)
{
    // Get the FX Macro Value
    auto param = utils::get_param(utils::ParamRef::FX_MACRO_LEVEL);
    if (param) {
        // Set the value and send it
        param->set_value(value);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

        // We also need to get the actual param we have modified
        auto msp = utils::get_param(utils::ParamRef::FX_MACRO_SELECT);
        if (msp) {
            // Get the param, update it and send it
            auto fp = utils::get_param(MoniqueModule::DAW, Monique::fx_macro_params().at(msp->hr_value()));
            fp->set_value(value);
            auto param_change = ParamChange(fp, module());
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));            

            // Can we show this param?
            if (_gui_state != GuiState::MOD_MATRIX) {
                // Is the param already shown?
                if (_param_shown != fp) {
                    // Are we showing an FX param list?
                    if (_show_param_list && ((_param_shown_root == utils::get_param(utils::ParamRef::FX_1_TYPE)) ||
                                             (_param_shown_root == utils::get_param(utils::ParamRef::FX_2_TYPE)))) {
                        // Is this param in the FX list?
                        auto index = _get_param_list_index(_param_shown_root, fp);
                        if (index >= 0) {
                            // Show the param
                            _param_shown = fp;
                            _param_shown_index = index;
                            _editing_param = false;
                            _new_param_list = false;
                            _show_param();
                        }
                    }
                    else {
                        // Reset the GUI state
                        _reset_gui_state();

                        // Show the param
                        _param_shown_root = fp;
                        _param_shown = fp;
                        _param_shown_index = 0;
                        _show_param_list = false;
                        _gui_state = GuiState::SHOW_PARAM_SHORT;
                        _editing_param = false;
                        _config_soft_button_1(false, true);
                        _config_data_knob(-1, -1);                                                  
                        _show_param();
                        _start_param_change_timer();
                    }
                }
                else {
                    // If we are editing, exit edit mode
                    if (_editing_param) {
                        _editing_param = false;
                        _config_soft_button_1(true, true);
                        _config_data_knob(_params_list.size());                        
                    }

                    // Update the param value
                    _show_param_update(false);
                    _start_param_change_timer();                                                            
                }
            }
        }
    }
}

//----------------------------------------------------------------------------
// _process_system_func_param
//----------------------------------------------------------------------------
void GuiManager::_process_system_func_param(const SystemFunc &data, Param *linked_param, bool show_patch_names, bool force_show_list)
{
    // Can we show this system function related param?
    if (_gui_state != GuiState::MOD_MATRIX) {
        // Selected functionality and a linked param has been specified?
        if (data.value && linked_param) {
            // Stop the param change timer
            _stop_param_change_timer();

            // This param will become the root param - get the index
            // of this param in the param list
            auto index = _get_param_list_index(linked_param, linked_param);
            if (index >= 0) {
                // Reset the GUI state                  
                _reset_gui_state();
                _reset_sys_func_switches(data.type);

                // Setup the param shown settings
                _param_shown_root =  linked_param->param_list().front();
                _param_shown = linked_param;
                _param_shown_index = index;
                _show_param_list = true;
                _show_patch_names = show_patch_names;
                _gui_state = GuiState::SHOW_PARAM;

                // Should we show this param as an enum list?
                if (!force_show_list && _show_param_as_enum_list(linked_param)) {
                    // Indicate we are editing
                    _editing_param = true;
                    //_showing_param_shortcut = true;               

                    // Show the param as an enum list                   
                    _show_enum_param();
                }
                else {
                    // Show the param normally
                    _editing_param = false;
                    _show_scope = true;
                    _config_soft_button_1(data.type == SystemFuncType::PATCH_MENU ? false : true, true);
                    _show_param();
                }
            }
        }
        else
        {
            // Reset the GUI state and show the home screen
            _reset_gui_state_and_show_home_screen();
        }
    }
}

//----------------------------------------------------------------------------
// _process_octave_dec
//----------------------------------------------------------------------------
void GuiManager::_process_octave_dec(bool selected)
{
    // Only process the button press on RELEASE
    if (!selected) {
        // Get the current KBD octave offset param
        auto param = utils::get_param(utils::ParamRef::OCTAVE_OFFSET);
        if (param) {
            int octave_offset = param->hr_value();
            if (octave_offset > Monique::OCTAVE_OFFSET_MIN) {
                // Decrement the octave offset
                octave_offset--;
                param->set_hr_value(octave_offset);

                // Manually set the LED states for the KBD Octave inc/dec buttons
                _set_kbd_octave_led_states(octave_offset);            

                // Post a param change message
                auto param_change = ParamChange(param, module());
                _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

                // If we are showing the KDB octave offset param, update it
                if (_param_shown == param) {
                    _show_param_update(false);
                }
            }
        }
    }   
}

//----------------------------------------------------------------------------
// _process_octave_inc
//----------------------------------------------------------------------------
void GuiManager::_process_octave_inc(bool selected)
{
    // Only process the button press on RELEASE
    if (!selected) {
        // Get the current KBD octave offset
        auto param = utils::get_param(utils::ParamRef::OCTAVE_OFFSET);
        if (param) {
            int octave_offset = param->hr_value();
            if (octave_offset < Monique::OCTAVE_OFFSET_MAX) {
                // Increment the octave offset
                octave_offset++;
                param->set_hr_value(octave_offset);

                // Manually set the LED states for the KBD Octave inc/dec buttons
                _set_kbd_octave_led_states(octave_offset);                  

                // Post a param change message
                auto param_change = ParamChange(param, module());
                _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

                // If we are showing the KDB octave offset param, update it
                if (_param_shown == param) {
                    _show_param_update(false);
                }                
            }
        }   
    }    
}

//----------------------------------------------------------------------------
// _process_seq_rec
//----------------------------------------------------------------------------
void GuiManager::_process_seq_rec(bool selected)
{
    // Start/stop the sequencer recording
    _start_stop_seq_rec(selected);
}

//----------------------------------------------------------------------------
// _process_seq_run
//----------------------------------------------------------------------------
void GuiManager::_process_seq_run(bool selected)
{
    // Start/stop the sequencer running
    _start_stop_seq_run(selected);
}

//----------------------------------------------------------------------------
// _process_preset_inc
//----------------------------------------------------------------------------
void GuiManager::_process_preset_inc(bool selected)
{
    // Only process the button press on RELEASE and if we are in the LOAD state
    if (!selected && (_gui_state == GuiState::MANAGE_PRESET) && (_manage_preset_state == ManagePresetState::LOAD) && (_select_preset_state == SelectPresetState::SELECT_PRESET)) {
        // Get the next preset ID, if any
        PresetId preset_id;
        preset_id.set_id(_selected_preset_id.bank_folder(), _list_item_from_index(_selected_preset_index).second);
        auto next_preset_id = preset_id.next_preset_id();
        if (next_preset_id.is_valid()) {
            // Load the specified preset immediately
            _reload_presets_from_select_preset_load++;
            _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::LOAD_PRESET, next_preset_id, MoniqueModule::GUI)));         

            // Set the new selected preset index
            _loaded_preset_index++;
            _selected_preset_index = _loaded_preset_index;

            // Update the selected list item
            _post_update_selected_list_item(_selected_preset_index);
        }
    }    
}

//----------------------------------------------------------------------------
// _process_preset_dec
//----------------------------------------------------------------------------
void GuiManager::_process_preset_dec(bool selected)
{
    // Only process the button press on RELEASE and if we are in the LOAD state
    if (!selected && (_gui_state == GuiState::MANAGE_PRESET) && (_manage_preset_state == ManagePresetState::LOAD) && (_select_preset_state == SelectPresetState::SELECT_PRESET)) {
        // Get the previous preset ID, if any
        PresetId preset_id;
        preset_id.set_id(_selected_preset_id.bank_folder(), _list_item_from_index(_selected_preset_index).second);
        auto prev_preset_id = preset_id.prev_preset_id();
        if (prev_preset_id.is_valid()) {
            // Load the specified preset immediately
            _reload_presets_from_select_preset_load++;
            _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::LOAD_PRESET, prev_preset_id, MoniqueModule::GUI)));

            // Set the new selected preset index
            _loaded_preset_index--;
            _selected_preset_index = _loaded_preset_index;

            // Update the selected list item
            _post_update_selected_list_item(_selected_preset_index);                   
        }    
    }    
}

//----------------------------------------------------------------------------
// _process_select_layer_1
//----------------------------------------------------------------------------
void GuiManager::_process_select_layer_1()
{
    // Are we currently showing the MULTI param list?
    if (_param_shown_root && 
        ((_param_shown_root == utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES)) ||
         (_param_shown_root == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES)))) {
        // If we are currently showing Layer 2
        if (_param_shown_root == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES)) {
            // Load layer 1
            _select_layer_name(LayerId::D0);
            _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::LOAD_LAYER_1, MoniqueModule::GUI)));
        }
    }
    // If Layer 1 is not currently selected
    else if (utils::get_current_layer_info().layer_id() != LayerId::D0) {
        // Load layer 1
        _select_layer_name(LayerId::D0);
        _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::LOAD_LAYER_1, MoniqueModule::GUI)));
    }
}

//----------------------------------------------------------------------------
// _process_select_layer_2
//----------------------------------------------------------------------------
void GuiManager::_process_select_layer_2()
{
    // Are we currently showing the MULTI param list?
    if (_param_shown_root && 
        ((_param_shown_root == utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES)) ||
         (_param_shown_root == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES)))) {
        // If we are currently showing Layer 1
        if (_param_shown_root == utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES)) {
            // If Layer 2 has voices
            if (utils::get_layer_info(LayerId::D1).num_voices() > 0) {
                // Load layer 2
                _select_layer_name(LayerId::D1);
                _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::LOAD_LAYER_2, MoniqueModule::GUI)));
            }
            else {
                // Select the Layer name, but don't change the actual Layer - leave Layer 1 loaded
                _select_layer_name(LayerId::D1);

                // Swap the param shown root to Layer 2
                _param_shown_root = utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES);
                _param_shown = utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES);
            
                // Select Layer 2 - this just makes sure we can edit the number of voices param, but leaves the loaded
                // Layer 1 as-is
                auto param = utils::get_param(utils::ParamRef::SELECTED_LAYER);
                param->set_value(1);
            
                // Update the system menu shown - make sure editing is also off
                _new_param_list = true;
                _editing_param = false;
                _config_soft_button_1(true, true);
                _update_param_list();
            }
        }
    }
    else {
        // If Layer 2 has no voices assigned
        if (utils::get_layer_info(LayerId::D1).num_voices() == 0) {
            // Indicate that this Layer is disabled
            _show_msg_popup("Layer 2:", "DISABLED (0 Voices)");

            // Switch the buttons back to Layer 1
            _set_sys_func_switch(SystemFuncType::SELECT_LAYER_1, ON);
            _set_sys_func_switch(SystemFuncType::SELECT_LAYER_2, OFF);            
        }
        // If Layer 2 is not currently selected
        else if (utils::get_current_layer_info().layer_id() != LayerId::D1) {            
            // Load layer 2
            _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::LOAD_LAYER_2, MoniqueModule::GUI)));
        }
    }
}

//----------------------------------------------------------------------------
// _process_select_bank
//----------------------------------------------------------------------------
void GuiManager::_process_select_bank()
{
    // If we are managing presets
    if (_gui_state == GuiState::MANAGE_PRESET) {
        // If we are showing presets
        if (_select_preset_state != SelectPresetState::SELECT_BANK) {
            // Make sure the BANK button is ON
            _set_sys_func_switch(SystemFuncType::BANK, ON);

            // Show the select bank screen
            _show_select_bank_screen();
        }
        else {
            // Make sure any Edit Name state is reset - we could be renaming a bank
            _edit_name_state = EditNameState::NONE;
            _edit_name.clear();

            // Show the LOAD/SAVE screen
            _show_select_preset_load_save_screen(_manage_preset_state == ManagePresetState::SAVE ? true : false);
        }
    }
    else {
        // Enter manage preset LOAD state and show the select bank screen
        _reset_gui_state();
        _selected_preset_id = utils::system_config()->preset_id();
        _gui_state = GuiState::MANAGE_PRESET;
        _show_select_bank_screen();

        // Make sure the other system function switches are reset
        _config_sys_func_switches(true);
        _reset_sys_func_switches(SystemFuncType::BANK);
        _set_sys_func_switch(SystemFuncType::LOAD, ON);

        // Reset the multi-function switches
        utils::reset_multifn_switches(_event_router);         
    }
}

//----------------------------------------------------------------------------
// _process_multi_menu
//----------------------------------------------------------------------------
void GuiManager::_process_multi_menu(const SystemFunc& sys_func)
{
    // We need to get the linked param to show
    Param *param;
    if (utils::is_current_layer(LayerId::D0)) {
        _select_layer_name(LayerId::D0);
        param = utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES);
    }
    else {
        _select_layer_name(LayerId::D1);
        param = utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES);
    }
    
    // Process the system function
    _process_system_func_param(sys_func, param, true, true);    
}

//----------------------------------------------------------------------------
// _process_wave_menu
//----------------------------------------------------------------------------
void GuiManager::_process_wave_menu(const SystemFunc& sys_func)
{
    // Can we show this system function related param?
    if (_gui_state != GuiState::MOD_MATRIX) {
        // Selected functionality and the param has been specified?
        if (sys_func.value && sys_func.linked_param) {
            // Process this linked param
            _stop_param_change_timer();

            // This param will become the root param - get the index
            // of this param in the param list
            auto index = _get_param_list_index(sys_func.linked_param, sys_func.linked_param);
            if (index >= 0) {
                // Reset the GUI state
                _reset_gui_state();
                _reset_sys_func_switches(SystemFuncType::WAVE_MENU);
                _config_soft_button_1(false, true);

                // Setup the param shown settings
                _param_shown_root = sys_func.linked_param;
                _param_shown = sys_func.linked_param;
                _param_shown_index = index;
                _show_param_list = false;
                _showing_param_shortcut = true;
                _gui_state = GuiState::SHOW_PARAM;

                // Indicate we are editing and show this param as a WT file browser
                _editing_param = true;
                _show_wt_file_browser_param();                      
            }
        }
        else {
            // Reset the GUI state and show the home screen
            _reset_gui_state_and_show_home_screen();
        }
    }
}


//----------------------------------------------------------------------------
// _process_osc_menu
//----------------------------------------------------------------------------
void GuiManager::_process_osc_menu(const SystemFunc& sys_func)
{
    // We need to get the linked param to show
    Param *param;
    if (utils::osc_state() == utils::OscState::OSC_1_OSC_2_STATE) {
        param = utils::get_param(utils::ParamRef::OSC_1_FINE_TUNE);
    }
    else {
        param = utils::get_param(utils::ParamRef::OSC_3_FINE_TUNE);
    }
    
    // Process the system function
    _process_system_func_param(sys_func, param);    
}

//----------------------------------------------------------------------------
// _process_lfo_menu
//----------------------------------------------------------------------------
void GuiManager::_process_lfo_menu(const SystemFunc& sys_func)
{
    // We need to get the linked param to show
    Param *param;
    if (utils::lfo_state() == utils::LfoState::LFO_1_STATE) {
        param = utils::get_param(utils::ParamRef::LFO_1_SLEW);
    }
    else if (utils::lfo_state() == utils::LfoState::LFO_2_STATE) {
        param = utils::get_param(utils::ParamRef::LFO_2_SLEW);
    }
    else {
        param = utils::get_param(utils::ParamRef::LFO_3_SLEW);
    }
    
    // Process the system function
    _process_system_func_param(sys_func, param);    
}

//----------------------------------------------------------------------------
// _process_env_menu
//----------------------------------------------------------------------------
void GuiManager::_process_env_menu(const SystemFunc& sys_func)
{
    // We need to get the linked param to show
    Param *param;
    if (utils::eg_state() == utils::EgState::VCF_EG_STATE) {
        param = utils::get_param(utils::ParamRef::VCF_EG_RESET);
    }
    else if (utils::eg_state() == utils::EgState::VCA_EG_STATE) {
        param = utils::get_param(utils::ParamRef::VCA_EG_RESET);
    }
    else {
        param = utils::get_param(utils::ParamRef::AUX_EG_RESET);
    }
    
    // Process the system function
    _process_system_func_param(sys_func, param, false, true);    
}

//----------------------------------------------------------------------------
// _process_tempo_select
//----------------------------------------------------------------------------
void GuiManager::_process_tempo_select(const SystemFunc& sys_func)
{
    // If we are not in the Tempo state
    if (utils::tempo_glide_state() != utils::TempoGlideState::TEMPO_STATE) {
        // Set the new Tempo state
        utils::set_tempo_glide_state(utils::TempoGlideState::TEMPO_STATE);

        // Set the controls state Tempo
        utils::set_controls_state(utils::tempo_glide_ui_state(), _event_router);
    }

    // If we are currently showing the Tempo param list
    if (_show_param_list && (_param_shown_root == utils::get_tempo_param())) {
        // Reset the GUI state
        _reset_gui_state_and_show_home_screen();
    }
    else {
        // Process the system function
        _process_system_func_param(sys_func, utils::get_tempo_param());
    }   
}

//----------------------------------------------------------------------------
// _process_glide_select
//----------------------------------------------------------------------------
void GuiManager::_process_glide_select(const SystemFunc& sys_func)
{
    // If we are not in the Glide state
    if (utils::tempo_glide_state() != utils::TempoGlideState::GLIDE_STATE) {
        // Set the new Glide state
        utils::set_tempo_glide_state(utils::TempoGlideState::GLIDE_STATE);

        // Set the controls state Glide
        utils::set_controls_state(utils::tempo_glide_ui_state(), _event_router);
    }

    // If we are currently showing the Glide param list
    if (_show_param_list && (_param_shown_root == utils::get_param(utils::ParamRef::GLIDE_MODE))) {
        // Reset the GUI state
        _reset_gui_state_and_show_home_screen();
    }
    else {
        // Process the system function
        _process_system_func_param(sys_func, utils::get_param(utils::ParamRef::GLIDE_MODE), false, true);
    }   
}

//----------------------------------------------------------------------------
// _process_toggle_patch_state
//----------------------------------------------------------------------------
void GuiManager::_process_toggle_patch_state(const SystemFunc& sys_func)
{
    // Can we show the popup or menu?
    if (_gui_state <= GuiState::MOD_MATRIX) {
        // Show a popup info screen
        _stop_param_change_timer();
        _show_msg_popup("SELECTED", sys_func.value ? "SOUND B" : "SOUND A");
    }  
}

//----------------------------------------------------------------------------
// _process_mod_matrix_multifn_switch
//----------------------------------------------------------------------------
void GuiManager::_process_mod_matrix_multifn_switch(uint switch_index)
{
    // Has the index changed?
    if (switch_index != (uint)_selected_mod_matrix_src_index) {
        Monique::ModMatrixSrc state = Monique::ModMatrixSrc::KEY_PITCH;

        // We need to convert the index into a Mod Matrix source
        if (switch_index < static_cast<uint>(Monique::ModMatrixSrc::CONSTANT)) {
            state = static_cast<Monique::ModMatrixSrc>(switch_index);
        }

        // Set the Mod Matrix state
        _set_mod_matrix_state(state);

        // Update the selected mod matrix index and show the intial screen
        _selected_mod_matrix_src_index = switch_index;
        _showing_additional_mod_dst_params = false;
        _show_mod_matrix_src_screen();

        // Get the Cutoff Link param
        auto param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::CUTOFF_LINK_PARAM_ID);
        if (param) {
            // Set the linked param state
            auto lp = utils::get_param(_mod_matrix_param_path(_selected_mod_matrix_src_index, (uint)Monique::ModMatrixDst::FILTER_CUTOFF_HP));
            lp->enable_linked_param(param->value() ? true : false);
        }

        // Send an event to indicate the mod matrix index/num has changed
        _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::SET_MOD_SRC_NUM, (uint)_selected_mod_matrix_src_index, module())));
    }   
}

//----------------------------------------------------------------------------
// _process_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_process_soft_button_1(bool selected)
{
    // Update the soft button state
    _post_soft_button_state_update(selected, SoftButtonId::BUTTON_1);

    // Parse the GUI state
    switch (_gui_state) {
        case GuiState::HOME_SCREEN:
        case GuiState::SHOW_PARAM_SHORT: {
            // Set the extended system menu indicator
            _show_ext_system_menu = selected;

            // Only process the button press on RELEASE
            if (!selected) {
                // Update the Scope mode
                if (_scope_mode == SoundScopeMode::SCOPE_MODE_OFF) {
                    _scope_mode = SoundScopeMode::SCOPE_MODE_OSC;
                }
                else if (_scope_mode == SoundScopeMode::SCOPE_MODE_OSC) {
                    _scope_mode = SoundScopeMode::SCOPE_MODE_XY;
                }
                else {
                    _scope_mode = SoundScopeMode::SCOPE_MODE_OFF;
                }

                // Update the home screen
                _stop_param_change_timer();
                _show_home_screen(utils::system_config()->preset_id().preset_display_name());
            }
            break;
        }

        case GuiState::MOD_MATRIX:
        case GuiState::SHOW_PARAM: {
            // Is there anything to edit?
            if (_params_list.size() > 0) {
                // Process the button press while showing a param list
                _process_show_param_soft_button_1(selected);
            }            
            break;              
        }

        case GuiState::SHOW_MORPH_PARAM: {
            // Only process the button press on RELEASE
            if (!selected) {            
                // Store the morph position to the patch state A
                _save_morph_to_layer_state(LayerState::STATE_A);
            }            
            break;
        }

        case GuiState::MANAGE_PRESET: {
            // Process the soft button when managing preset
            _process_manage_preset_soft_button_1(selected);
            break;
        }

        case GuiState::SYSTEM_MENU: {
            if (!selected && !_ext_sys_menu_just_shown) {
                // Process the System Menu button press
                _process_system_menu_soft_button_1();
            }
            _ext_sys_menu_just_shown = false;
            break;
        }

        case GuiState::BANK_MANAGMENT: {
            if (!selected) {
                // Process the button press for Bank management
                _process_bank_management_soft_button_1();
            }
            break;
        }

        case GuiState::WAVETABLE_MANAGEMENT: {
            if (!selected) {
                // Process the Wavetable Management button press
                _process_wt_management_soft_button_1();
            }
            break;          
        }

        case GuiState::BACKUP: {
            // If the backup is complete
            if (!selected && (_backup_state == BackupState::BACKUP_FINISHED)) {
                // Process the Backup button press
                _process_backup_soft_button_1();                    
            }
            break;
        }        

        case GuiState::RUN_DIAG_SCRIPT: {
            // If the script has not yet been run
            if (!selected) {
                // Parse the run diag script state
                if (_run_diag_script_state == RunDiagScriptState::SELECT_DIAG_SCRIPT) {
                    // Confirm the run diag script
                    _show_run_diag_script_confirm_screen();
                }
                else if (_run_diag_script_state == RunDiagScriptState::CONFIRM_DIAG_SCRIPT) {
                    // Run the diag script
                    _show_run_diag_script_screen();
                }
            }
            break;
        }

        case GuiState::QA_STATUS:
            if (!selected) {
                // Go back to the System Menu
                _hide_msg_box();               
                _show_system_menu_screen();
            }
            break;

        case GuiState::CALIBRATE:
            // If the calibrate is complete
            if (!selected && (_calibrate_state == CalibrateState::CALIBRATE_FINISHED)) {
                // Go back to the System Menu
                _hide_msg_box();               
                _show_system_menu_screen();

                // If we are running a background test
                if (_running_background_test) {
                    // End the test
                    _sw_manager->end_background_test();
                    _running_background_test = false;
                }
            }
            break;

        case GuiState::WHEELS_CALIBRATE:
            // Only process the button press on RELEASE
            if (!selected) {         
                // Process the wheels calibration
                _process_wheels_calibration_soft_button_1();
            }
            break;

        case GuiState::MOTOR_STARTUP_FAILED:
            // Reset the GUI state and show the home screen
            if (!selected) {
                _gui_state = GuiState::INVALID;
                _reset_gui_state_and_show_home_screen();
            }
            break;

        default:
            break;
    }
}

//----------------------------------------------------------------------------
// _process_show_param_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_process_show_param_soft_button_1(bool selected)
{
    // If the param shown is valid
    if (_param_shown) {
        // Parse the edit (bank) name state
        switch (_edit_name_state) {
            case EditNameState::NONE: {
                if ((_param_shown->module() == MoniqueModule::SYSTEM) && 
                    (_param_shown->param_id() == SystemParamId::PATCH_NAME_PARAM_ID)) {
                    if (!selected) {
                        // Enter the rename patch state
                        _edit_name = utils::get_current_layer_info().patch_name();
                        _editing_param = true;
                        _renaming_patch = true;
                        _show_edit_name_select_char_screen();
                        _config_soft_button_1(false); 
                    }              
                }
                // If the param shown is valid
                else if (_param_shown) {            
                    // If the button is RELEASED
                    if (!selected) {
                        // If this is a system param
                        if (_param_shown->type() == ParamType::SYSTEM_FUNC) {
                            // Are we reseting the Sequencer?
                            if (_param_shown == utils::get_sys_func_param(SystemFuncType::SEQ_RESET)) {
                                // Have we shown the reset Sequencer screen?
                                if (!_showing_reset_conf_screen) {
                                    // Show a confirmation screen to reset the sequencer
                                    _show_reset_seq_screen();
                                }
                                else {
                                    // Send the system func event to reset the Sequencer
                                    auto sys_func = SystemFunc(static_cast<const SystemFuncParam *>(_param_shown)->system_func_type(), 
                                                            _param_shown->value(), module());                         
                                    _event_router->post_system_func_event(new SystemFuncEvent(sys_func));

                                    // Show a confirmation popup
                                    _show_msg_popup("SEQUENCER", "Reset");

                                    // Hide the reset Sequencer screen
                                    _hide_reset_seq_screen();                                                             
                                }
                            }
                            // Are we resetting the global params?
                            else if (_param_shown == utils::get_sys_func_param(SystemFuncType::RESET_GLOBAL_SETTINGS)) {
                                // Have we shown the reset to factory confirmation screen?
                                if (!_showing_reset_conf_screen) {
                                    // Show a confirmation screen to reset to factory
                                    _show_reset_to_factory_screen();
                                }
                                else {                                
                                    // Reset the global params
                                    _reset_global_params();

                                    // Show a confirmation popup
                                    _show_msg_popup("RESET TO FACTORY DEFAULTS", "Done");

                                    // Hide the reset to factory screen and go back to the home screen
                                    _hide_reset_to_factory_screen();
                                    _stop_param_change_timer();          
                                    _reset_gui_state_and_show_home_screen();
                                }
                            }
                            else {
                                // Send the system func event
                                auto sys_func = SystemFunc(static_cast<const SystemFuncParam *>(_param_shown)->system_func_type(), 
                                                        _param_shown->value(), module());                         
                                _event_router->post_system_func_event(new SystemFuncEvent(sys_func));
                            }                
                        }
                        else {
                            // Special handling if the param is FX 1/2 Type
                            if ((_param_shown == utils::get_param(utils::ParamRef::FX_1_TYPE)) || (_param_shown == utils::get_param(utils::ParamRef::FX_2_TYPE))) {
                                // Handle the FX Type edit exit
                                _handle_fx_type_edit_exit();

                                // Make sure the FX list is updated once editing of the slot is finished
                                _new_param_list = true;
                            }
                            // If we just changed the Sequencer Mode
                            else if (_param_shown == utils::get_param(MoniqueModule::SEQ, SeqParamId::MODE_PARAM_ID)) {
                                // Make sure the Seq list is updated
                                _new_param_list = true;                            
                            }
                            // Special handling if the param is LFO Tempo Sync
                            else if ((_param_shown == utils::get_param(utils::ParamRef::LFO_1_TEMPO_SYNC)) ||
                                     (_param_shown == utils::get_param(utils::ParamRef::LFO_2_TEMPO_SYNC)) ||
                                     (_param_shown == utils::get_param(utils::ParamRef::LFO_3_TEMPO_SYNC))) {
                                // Set the new controls state
                                utils::set_lfo_rate_state(_param_shown->value() ? utils::LfoRateState::LFO_SYNC_RATE : utils::LfoRateState::LFO_NORMAL_RATE);
                                utils::set_controls_state(utils::lfo_rate_ui_state(), _event_router);

                                // Make sure the LFO list is updated
                                _new_param_list = true;                                
                            }

                            // Reset the data knob haptic mode
                            _reset_param_shortcut_switches();
                            _show_param();
                            _show_param_list ?
                                _config_data_knob(_params_list.size()) :
                                _config_data_knob(-1, -1);
                            _editing_param = false;
                            _showing_param_shortcut = false;
                            _start_param_change_timer();
                        }
                    }
                    // If the button is currently pressed (held down)
                    else {
                        // Ignore system params, they are processed when the button is released
                        if (_param_shown->type() != ParamType::SYSTEM_FUNC) {           
                            // Stop the param change timer
                            _stop_param_change_timer();   
        
                            // Indicate we are now editing
                            _editing_param = true;

                            // Is this a WT file browser param?
                            if (_show_param_as_wt_file_browser(_param_shown)) {
                                // Show the param as a WT file browser
                                _showing_param_shortcut = true;
                                _show_wt_file_browser_param();
                            }
                            // Should we show this param as an enum list?
                            else if (_show_param_as_enum_list(_param_shown)) {
                                // If this is the LFO shape, make sure it is shown as a shortcut - meaning
                                // other param changes will not exit the edit screen
                                if ((_param_shown == utils::get_param(utils::ParamRef::LFO_1_SHAPE)) ||
                                    (_param_shown == utils::get_param(utils::ParamRef::LFO_2_SHAPE)) ||
                                    (_param_shown == utils::get_param(utils::ParamRef::LFO_3_SHAPE))) {
                                    _showing_param_shortcut = true;
                                }
                                 
                                // Show the enum param
                                _show_enum_param();   
                            }
                            else {
                                // If this is an enum param (not shown as a list)
                                if (_param_shown->num_positions() > 0) {
                                    // Setup the UI for this enum param
                                    _config_data_knob(_param_shown->num_positions());
                                }
                                else {
                                    // Setup the UI for this normal param
                                    _config_data_knob(-1, _param_shown->value());
                                }

                                // Are are we in Mod Matrix mode? If so change soft button 2 to RESET
                                if (_gui_state == GuiState::MOD_MATRIX) {
                                    auto msg = GuiMsg();
                                    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
                                    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "EDIT");
                                    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "RESET");
                                    _post_gui_msg(msg);                                    
                                }
                            }
                        }                      
                    }
                }    
                break;
            }

            default: {
                // Process the EDIT button in rename state
                _process_rename_edit_button(selected);
                break;
            }
        }
    }
}

//----------------------------------------------------------------------------
// _process_manage_preset_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_process_manage_preset_soft_button_1(bool selected)
{
    // If we are not showing a bank
    if (_select_preset_state != SelectPresetState::SELECT_BANK) {
        // Parse the manage preset state
        switch(_manage_preset_state) {
            case ManagePresetState::LOAD: {
                // Process when released AND if the index is valid (not the INIT PATCH)
                if (!selected && _selected_preset_index) {
                    // Check if this is a single or multi-timbral preset
                    PresetId preset_id;
                    preset_id.set_id(_selected_preset_id.bank_folder(), _list_item_from_index(_selected_preset_index).second);
                    if (FileManager::PresetIsMultiTimbral(preset_id)) {
                        // Multi - show the select patch screen
                        _show_preset_load_option_select_patch_screen();
                    }
                    else {
                        // Single - show the select destination screen, always load from Layer D0
                        _selected_patch_src_index = 0;
                        _show_undo_last_load = true;
                        _show_preset_load_option_select_dest_screen();                    
                    }                
                }
                break;
            }

            case ManagePresetState::LOAD_INTO_SELECT_SRC: {
                if (!selected) {
                    _show_select_preset_load_save_screen(false);
                }
                break;
            }

            case ManagePresetState::LOAD_INTO_SELECT_DST: {
                if (!selected) {
                    // Check if this is a single or multi-timbral preset
                    PresetId preset_id;
                    preset_id.set_id(_selected_preset_id.bank_folder(), _list_item_from_index(_selected_preset_index).second);
                    if (FileManager::PresetIsMultiTimbral(preset_id)) {
                        // Multi - show the select patch screen
                        _show_preset_load_option_select_patch_screen();
                    }
                    else {
                        // Single - go back to the load screen
                        _show_select_preset_load_save_screen(false);                    
                    }                
                }              
                break;
            }

            case ManagePresetState::SAVE: {
                _process_save_preset_soft_button_1(selected);
                break;
            }

            case ManagePresetState::STORE_MORPH: {
                if (!selected) {
                    // Reset the GUI state and show the home screen
                    _hide_msg_box();
                    _stop_param_change_timer();          
                    _reset_gui_state_and_show_home_screen();
                }
                break;
            }

            default:
                break;
        }
    }
    else {
        // Parse the edit (bank) name state
        switch (_edit_name_state) {
            case EditNameState::NONE: {
                // Only process the button press on RELEASE
                if (!selected) {                   
                    // Enter the rename bank state
                    _edit_name = _get_edit_name_from_index(_selected_bank_index);
                    _renaming_patch = false;
                    _show_edit_name_select_char_screen();
                    _config_soft_button_1(false);
                }
                break;
            }

            default: {
                // Process the EDIT button in rename state
                _process_rename_edit_button(selected);
                break;
            }
        }       
    }
}

//----------------------------------------------------------------------------
// _process_bank_management_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_process_bank_management_soft_button_1()
{
    // If we are showing a list
    if (_bank_management_state == BankManagmentState::SHOW_LIST) {
        bool set_state = true;

        // Set the new Bank Management state
        auto state = static_cast<BankManagmentState>(_selected_bank_management_item + (uint)BankManagmentState::IMPORT);
        if (((state == BankManagmentState::IMPORT) && !_sw_manager->bank_archive_present()) ||
            ((state == BankManagmentState::EXPORT) && !_sw_manager->msd_mounted())) {
            set_state = false;
        }
        if (set_state) {
            _bank_management_state = state;
        } 
    }
    switch (_bank_management_state) {
        case BankManagmentState::IMPORT: {
            // Parse the import state
            switch (_import_bank_state) {
                case ImportBankState::NONE: {
                    // If the import archive is present
                    if (_sw_manager->bank_archive_present()) {
                        // Show the bank import select bank archive screen
                        _show_select_bank_archive_screen();
                    }
                    break;
                }

                case ImportBankState::SELECT_ARCHIVE:
                    // Archive selected, show the select destination bank screen
                    _show_select_dest_bank_screen();
                    break;

                case ImportBankState::SELECT_DEST:
                    // Destination bank selected, show the import method screen
                    _show_bank_import_method_screen();
                    break;

                case ImportBankState::IMPORT_METHOD:
                    // If the export has finished
                    if (_progress_state == ProgressState::FINISHED) {
                        // Unmount the MSD and go back to the Bank Management Menu
                        _sw_manager->umount_msd();
                        _import_bank_state = ImportBankState::NONE;
                        _selected_bank_archive = 0;
                        _hide_msg_box();
                        _show_bank_management_screen();                        
                    }
                    else if (_progress_state == ProgressState::NOT_STARTED) {
                        // Show the bank import screen - merge
                        _show_import_bank_screen(true);
                    }
                    break;

                default:
                    // No action
                    break;
            }
            break;             
        }

        case BankManagmentState::EXPORT: {
            // Parse the export state
            switch (_export_bank_state) {
                case ExportBankState::NONE: {
                    // If the MSD is mounted
                    if (_sw_manager->msd_mounted()) {
                        // Archive selected, show the select bank screen
                        _show_select_dest_bank_screen();
                    }
                    break;
                }

                case ExportBankState::SELECT_BANK:
                    // If the export has finished
                    if (_progress_state == ProgressState::FINISHED) {
                        // Unmount the MSD and go back to the Bank Management Menu
                        _sw_manager->umount_msd();
                        _export_bank_state = ExportBankState::NONE;
                        _selected_bank_dest = 0;
                        _hide_msg_box();
                        _show_bank_management_screen();
                    }
                    else {
                        // Show the bank export scteen
                        _show_export_bank_screen();
                    }
                    break;

                default:
                    // No action
                    break;
            }          
            break;             
        }

        case BankManagmentState::ADD:
            // Show the add bank screen
            if (_progress_state == ProgressState::NOT_STARTED) {
                _show_add_bank_screen();
            }   
            break;

        case BankManagmentState::CLEAR:
            // Parse the clear state
            switch (_clear_bank_state) {
                case ClearBankState::NONE: {
                    // Show the select bank screen
                    _show_select_dest_bank_screen();
                    break;
                }

                case ClearBankState::SELECT_BANK:
                    // Show the clear bank confirm screen
                    _show_clear_bank_confirm_screen();
                    break;

                case ClearBankState::CONFIRM:
                    // Show the clean bank screen
                    _show_clear_bank_screen();
                    break;

                default:
                    // No action
                    break;
            }          
            break;

        default:
            // No action
            break;
    }
}

//----------------------------------------------------------------------------
// _process_save_preset_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_process_save_preset_soft_button_1(bool selected)
{
    // If we're selecting a preset to save
    if (!selected && (_save_preset_state == SavePresetState::SELECT_PRESET)) {
        // Show the select Bank screen and make sure the BANK LED is on
        _show_select_bank_screen();
        _set_sys_func_switch(SystemFuncType::BANK, ON);
    }
    // If we have selected a preset to save
    else if (_save_preset_state == SavePresetState::SAVE_PRESET) {
        // Process the EDIT button in rename state
        _process_rename_edit_button(selected);
    }
}

//----------------------------------------------------------------------------
// _process_system_menu_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_process_system_menu_soft_button_1()
{
    // Parse the system menu state
    switch (_system_menu_state) {
        case SystemMenuState::SHOW_OPTIONS: {
            auto selected_sys_menu_item = _selected_system_menu_item;

            // Adjust the selected item for the extended or standard system menu
            if (!_show_ext_system_menu) {
                selected_sys_menu_item += SystemMenuOption::GLOBAL_SETTINGS;
            }

            // Action the selected item
            switch (selected_sys_menu_item) {
                case SystemMenuOption::CALIBRATION_STATUS: {
                    // Show the Calibration status screen
                    auto pb_param = utils::get_param(utils::ParamRef::MIDI_PITCH_BEND);
                    auto mw_param = utils::get_param(utils::ParamRef::MIDI_MODWHEEL);
                    auto at_param = utils::get_param(utils::ParamRef::MIDI_AFTERTOUCH);
                    _show_calibration_status_screen(true, 
                                                    dataconv::pitch_bend_from_normalised_float(pb_param->value()),
                                                    dataconv::midi_cc_from_normalised_float(mw_param->value()),
                                                    dataconv::aftertouch_from_normalised_float(at_param->value()));
                    break;
                }
                
                case SystemMenuOption::MIX_VCA_CALIBRATION:
                    // Show the Mix VCA calibrate status screen
                    _show_calibrate_screen(CalMode::MIX_VCA);
                    break;

                case SystemMenuOption::FILTER_CALIBRATION:
                    // Show the Filter calibrate screen
                    _show_calibrate_screen(CalMode::FILTER);
                    break;

                case SystemMenuOption::WHEELS_CALIBRATION:
                    // Process the wheels calibration
                    _wheels_calibrate_state = WheelsCalibrateState::WHEELS_CALIBRATE_NOT_STARTED;
                    _process_wheels_calibration_soft_button_1();
                    break;

                case SystemMenuOption::FACTORY_SOAK_TEST:
                    _show_factory_soak_test_screen();
                    break;

                case SystemMenuOption::MOTOR_TEST:
                    // Show the Motor/LED test screen
                    _show_motor_test_screen();
                    break;

                case SystemMenuOption::RUN_DIAG_SCRIPT:
                    // If the MSD is mounted and contains (at least one) diagnostic script
                    if (_sw_manager->diag_script_present()) {
                        // Show the system menu select diagnostic script screen
                        _show_select_diag_script_screen();
                    }
                    break;                

                case SystemMenuOption::GLOBAL_SETTINGS:
                    // Show the global settings
                    _show_global_settings_screen();
                    break;

                case SystemMenuOption::BANK_MANAGMENT:
                    // Show the Bank Management options
                    _selected_bank_management_item = 0;
                    _selected_bank_archive = 0;
                    _selected_bank_dest = 0;
                    _import_bank_state = ImportBankState::NONE;
                    _export_bank_state = ExportBankState::NONE;
                    _clear_bank_state = ClearBankState::NONE;
                    _show_bank_management_screen();
                    break;

                case SystemMenuOption::WAVETABLE_MANAGEMENT:
                    // Show the Wavetable management screen
                    _show_wt_management_screen();
                    break;

                case SystemMenuOption::BACKUP: {
                    // If an MSD is mounted
                    if (_sw_manager->msd_mounted()) {
                        // Show the system menu backup screen
                        _show_backup_screen();      
                    }
                    break;
                }

                case SystemMenuOption::RESTORE_BACKUP: {
                    // If the MSD is mounted and contains at least one restore backup archive
                    if (_sw_manager->restore_backup_archives_present()) {
                        // Show the system menu restore screen
                        _show_restore_screen();                                        
                    }
                    break;                  
                }

                case SystemMenuOption::STORE_DEMO_MODE: {
                    // Toggle store demo-mode ON/OFF
                    utils::system_config()->set_demo_mode(!utils::system_config()->get_demo_mode());
                    _show_system_menu_screen();
                    utils::system_config()->get_demo_mode() ?
                        _start_demo_mode_timer() :
                        _stop_demo_mode_timer();
                    _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::SAVE_DEMO_MODE, MoniqueModule::GUI)));                    
                    break;
                }

                case SystemMenuOption::ABOUT:
                    // Show the about screen
                    _show_about_screen();
                    break;                

                default:
                    // No processing
                    break;
            }
            break;
        }

        default:
            // No action
            break;
    }
}

//----------------------------------------------------------------------------
// _process_wt_management_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_process_wt_management_soft_button_1()
{
    // Process the selected wavetable management action
    uint selected_item = _selected_wt_management_item + (uint)WtManagmentState::IMPORT;
    switch (selected_item) {
        case (uint)WtManagmentState::IMPORT: {
            // If the import archive is present
            if ((_progress_state == ProgressState::NOT_STARTED) && _sw_manager->wt_archive_present()) {
                // Show the wavetable import screen
                _show_wt_import_screen();
            }
            else if (_progress_state == ProgressState::FINISHED) {
                // Unmount the MSD
                _sw_manager->umount_msd();
                _hide_msg_box();
                _show_wt_management_screen();
            }
            break;             
        }

        case (uint)WtManagmentState::EXPORT: {
            // If the MSD is mounted
            if ((_progress_state == ProgressState::NOT_STARTED) && _sw_manager->msd_mounted()) {
                // Show the wavetable export screen
                _show_wt_export_screen();
            }
            else if (_progress_state == ProgressState::FINISHED) {
                // Unmount the MSD
                _sw_manager->umount_msd();
                _hide_msg_box();
                _show_wt_management_screen();
            }            
            break;             
        }

        case (uint)WtManagmentState::PRUNE:
            // Show the wavetable prune screen
            if (_progress_state == ProgressState::NOT_STARTED) {
                (_showing_wt_prune_confirm_screen) ?
                    _show_wt_prune_screen() :
                    _show_wt_prune_confirm_screen();
            }
            break;

        default:
            // No action
            break;
    }
}

//----------------------------------------------------------------------------
// _process_backup_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_process_backup_soft_button_1()
{
    // Unmount the USB drive
    _sw_manager->umount_msd();

    // Return to the system menu screen
    _hide_msg_box();
    _show_system_menu_screen();
}

//----------------------------------------------------------------------------
// _process_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_soft_button_2(bool selected)
{
    // Update the soft button state
    _post_soft_button_state_update(selected, SoftButtonId::BUTTON_2);

    // Parse the GUI state
    switch (_gui_state) {
        case GuiState::HOME_SCREEN:
        case GuiState::SHOW_PARAM_SHORT: {
            // Only process the button press on RELEASE
            if (!selected) {                
                // Show the System Menu
                _ext_sys_menu_just_shown = _show_ext_system_menu;
                _stop_param_change_timer();
                _show_system_menu_screen();
            }
            break;
        }

        case GuiState::SHOW_PARAM: {
            // Process the soft button when showing param
            _process_param_update_soft_button_2(selected);
            break;              
        }

        case GuiState::SHOW_MORPH_PARAM: {
            // Only process the button press on RELEASE
            if (!selected) {            
                // Store the morph position to the patch state B
                _save_morph_to_layer_state(LayerState::STATE_B);
            }            
            break;
        }

        case GuiState::MOD_MATRIX: {
            // Only process the button press on RELEASE
            if (!selected) {
                // If we are currently editing a param then this button acts as RESET
                if (_editing_param && _param_shown && (_params_list.size() > 0)) {
                    // Reset the mod matrix entry
                    _param_shown->set_hr_value(0);
                    auto param_change = ParamChange(_param_shown, module());
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));                    

                    // If this is a linked param AND linking is ON, we also need to
                    // reset the other linked param
                    auto mapped_params = _param_shown->mapped_params(nullptr);
                    for (Param *mp : mapped_params) {
                        // If this a normal param (not a system function)
                        if (mp->type() != ParamType::SYSTEM_FUNC) {
                            // Are both parameters linked to each other?
                            if (_param_shown->is_linked_param() && mp->is_linked_param()) {
                                // Is the linked functionality enabled?
                                // If not, ignore this mapping
                                if (_param_shown->is_linked_param_enabled() || mp->is_linked_param_enabled()) {
                                    // Reset the linked param
                                    mp->set_hr_value(0);
                                    auto param_change = ParamChange(mp, module());
                                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                                }
                            }
                        }
                    }

                    // Process the mapped params
                    _process_param_changed_mapped_params(_param_shown, 0.0f, nullptr);

                    // Update the list and exit editing
                    _editing_param = false;
                    _config_soft_button_1(true, true);
                    _show_param();
                }                    
                else {
                    // Toggle the main/more list
                    _showing_additional_mod_dst_params = !_showing_additional_mod_dst_params;
                    _show_mod_matrix_src_screen();
                }
            }
            break;
        }

        case GuiState::MANAGE_PRESET: {
            // Only process the button press on RELEASE
            if (!selected) {          
                // Selecting a Bank?
                if (_select_preset_state == SelectPresetState::SELECT_BANK) {
                    // Process the Select Bank enter switch press
                    _process_select_bank_soft_button_2();
                }
                else {
                    // Process the select Setup Load/Save
                    _process_manage_preset_soft_button_2();
                }
            }
            break;
        }

        case GuiState::SYSTEM_MENU: {
            // Only process the button press on RELEASE
            if (!selected) {
                // Are we showing a system menu option?
                if (_system_menu_state == SystemMenuState::OPTION_ACTIONED) {
                    // Show the system menu
                    _show_system_menu_screen();
                }
                else {
                    // Reset the GUI state and show the home screen
                    _reset_gui_state_and_show_home_screen();
                }
            }
            break;
        }

        case GuiState::BANK_MANAGMENT: {
            // Only process the button press on RELEASE
            if (!selected) {     
                // Process the Bank Management button press
                _process_bank_management_soft_button_2();
            }
            break;
        }            

        case GuiState::WAVETABLE_MANAGEMENT:
            // Only process the button press on RELEASE
            if (!selected) {
                // If showing the management list
                if (_wt_management_state == WtManagmentState::SHOW_LIST) {
                    // Reset the GUI state and show the home screen           
                    _reset_gui_state_and_show_home_screen();
                }
                // If the wavetable operation is complete or we are showing the wavetable
                // prune confirm screen
                else if ((_progress_state == ProgressState::FINISHED) || (_showing_wt_prune_confirm_screen)) {
                    // Go back to the Wavetable Management Menu
                    _hide_msg_box();  
                    _show_wt_management_screen();
                }
            }
            break;   

        case GuiState::BACKUP:
            // If the backup is complete
            if (!selected && (_backup_state == BackupState::BACKUP_FINISHED)) {
                // Go back to the System Menu
                _hide_msg_box();               
                _show_system_menu_screen();
            }
            break;

        case GuiState::RUN_DIAG_SCRIPT:
            if (!selected) {
                // If showing the list of diag scripts
                if (_run_diag_script_state == RunDiagScriptState::SELECT_DIAG_SCRIPT) {
                    // Reset the GUI state and show the home screen           
                    _reset_gui_state_and_show_home_screen();                    
                }
                else {
                    // Go back to the Select Diag script screen
                    _hide_msg_box();               
                    _show_select_diag_script_screen();
                }
            }
            break;

        case GuiState::WHEELS_CALIBRATE: {
            if (!selected) {
                // Exit if in any of the check states
                switch (_wheels_calibrate_state) {
                    case WheelsCalibrateState::PITCH_BEND_WHEEL_TOP_CHECK:
                    case WheelsCalibrateState::PITCH_BEND_WHEEL_MID_CHECK:
                    case WheelsCalibrateState::PITCH_BEND_WHEEL_BOTTOM_CHECK:
                    case WheelsCalibrateState::MOD_WHEEL_TOP_CHECK:
                    case WheelsCalibrateState::MOD_WHEEL_BOTTOM_CHECK:
                        // Go back to the System Menu
                        _hide_msg_box();        
                        _show_system_menu_screen();
                        break;

                    default:
                        break;                   
                }
            }
            break;
        }    

        default:
            break;
    }
}

//----------------------------------------------------------------------------
// _process_select_bank_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_select_bank_soft_button_2()
{
    // Parse the edit name state
    switch (_edit_name_state) {
        case EditNameState::NONE: {
            std::string new_bank = "";

            // Get the selected bank
            auto selected_bank = _list_item_from_index(_selected_bank_index);

            // Did the selected bank change?
            if (selected_bank.second != _selected_preset_id.bank_folder()) {
                new_bank = selected_bank.second;
            }

            // Turn the BANK button OFF
            _set_sys_func_switch(SystemFuncType::BANK, OFF);

            // Show the LOAD/SAVE screen with the selected bank
            _show_select_preset_load_save_screen(_manage_preset_state == ManagePresetState::SAVE ? true : false, new_bank);      
            break;
        }

        default: {
            // Was the bank name changed?
            if (_edit_name.size() > 0) {
                // Right trim all characters after the cursor
                _edit_name = _edit_name.substr(0, _selected_char_index + 1);

                // Also right trim any whitespace
                auto end = _edit_name.find_last_not_of(" ");
                if (end != std::string::npos) {
                    _edit_name = _edit_name.substr(0, end + 1);
                }

                // Append the prefix to the edited name
                auto num = _list_item_from_index(_selected_bank_index).first;
                _edit_name = _list_items[num].substr(0,4) + _edit_name;

                // Has the name actually changed?
                if (std::strcmp(_edit_name.c_str(), _list_items[num].c_str()) != 0)
                {
                    std::string org_folder_name;
                    std::string org_folder;
                    std::string new_folder_name;
                    std::string new_folder;

                    // Create the original and new bank folder names (full path)
                    org_folder_name = std::regex_replace(_list_items[num], std::regex{" "}, "_");
                    org_folder = common::MONIQUE_PRESETS_DIR + org_folder_name;
                    new_folder_name = std::regex_replace(_edit_name, std::regex{" "}, "_");
                    new_folder = common::MONIQUE_PRESETS_DIR + new_folder_name;
                    
                    // Rename it....
                    int ret = ::rename(org_folder.c_str(), new_folder.c_str());
                    if (ret != 0) {
                        MSG("Error renaming bank: " << org_folder << " to: " << new_folder);
                        MONIQUE_LOG_ERROR(module(), "Error renaming Bank folder: {} to {}", org_folder, new_folder);
                    }

                    // Update the preset ID
                    _selected_preset_id.set_id(new_folder_name, _selected_preset_id.preset_name());

                    // Send a system function to indicate a bank has been renamed
                    auto system_func = SystemFunc(SystemFuncType::BANK_RENAMED, MoniqueModule::GUI);
                    system_func.str_value = org_folder_name;
                    system_func.str_value_2 = new_folder_name;
                    _event_router->post_system_func_event(new SystemFuncEvent(system_func));                    
                }
            }

            // Reset the edit name state, and show the Select Bank screen
            _edit_name_state = EditNameState::NONE;
            _edit_name.clear();
            _show_select_bank_screen();
            break;
        }
    }
}

//----------------------------------------------------------------------------
// _process_manage_preset_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_manage_preset_soft_button_2()
{
    // Parse the manage preset state
    switch(_manage_preset_state) {
        case ManagePresetState::LOAD: {
            _process_select_setup_load_soft_button_2();
            break;
        }

        case ManagePresetState::LOAD_INTO_SELECT_SRC: {
            // Show the select destination screen, or undo the last preset load
            if (_selected_patch_src_index < 2) {
                _show_undo_last_load = false;
                _show_preset_load_option_select_dest_screen();
            }
            else {
                _undo_last_preset_load();
            }
            break;
        }

        case ManagePresetState::LOAD_INTO_SELECT_DST: {
            // Load the preset option, or undo the last preset load
            !_show_undo_last_load || (_selected_patch_dst_index < (_num_list_items - 1)) ?
                _process_select_option_soft_button_2() :
                _undo_last_preset_load();
            break;
        }

        case ManagePresetState::SAVE: {
            _process_select_setup_save_soft_button_2();
            break;
        }

        case ManagePresetState::STORE_MORPH: {
            // Store the morph position to the current patch state A/B
            _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::STORE_MORPH_TO_PRESET_SOUND, MoniqueModule::GUI)));

            // Reset the GUI state and show the home screen
            _hide_msg_box();
            _stop_param_change_timer();          
            _reset_gui_state_and_show_home_screen();            
            break;
        }
    } 
}

//----------------------------------------------------------------------------
// _process_select_setup_load_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_select_setup_load_soft_button_2()
{
    // Parse the edit name state
    switch (_edit_name_state) {
        case EditNameState::NONE: {
            // Preset index zero is a special case used to initialise the preset
            if (_selected_preset_index) {
                // Load the specified setup immediately - get the preset number to load
                PresetId preset_id;
                preset_id.set_id(_selected_preset_id.bank_folder(), _list_item_from_index(_selected_preset_index).second);
                _loaded_preset_index = _selected_preset_index;
                _reload_presets_from_select_preset_load++;
                _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::LOAD_PRESET, preset_id, MoniqueModule::GUI)));
            }
            else {
                // The user has selected to initialise the preset
                // Load the init patch, overwriting the current preset values
                _reload_presets_from_select_preset_load++;
                _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::INIT_PRESET, MoniqueModule::GUI)));                
            }
            break;
        }

        default:
            // Process the edit patch name exit
            //_process_edit_patch_name_exit();
            break;
    }        
}

//----------------------------------------------------------------------------
// _process_select_setup_save_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_select_setup_save_soft_button_2()
{
    // Preset selected for saving?
    if (_save_preset_state == SavePresetState::SELECT_PRESET) {
        // Show the edit name screen to allow a rename
        _edit_name =_save_edit_name;
        _renaming_patch = false;
        _show_edit_name_select_char_screen();
        _config_soft_button_1(false);
        _save_preset_state = SavePresetState::SAVE_PRESET;
    }
    else {
        PresetId preset_id = utils::system_config()->preset_id();

        // Was the preset name changed?
        if (_edit_name.size() > 0) {
            auto org_preset_id = preset_id;

            // Right trim all characters after the cursor
            _edit_name = _edit_name.substr(0, _selected_char_index + 1);

            // Also right trim any whitespace
            auto end = _edit_name.find_last_not_of(" ");
            if (end != std::string::npos) {
                _edit_name = _edit_name.substr(0, end + 1);
            }

            // Append the prefix to the edited name
            auto num = _list_item_from_index(_selected_preset_index).first;
            _edit_name = _list_items[num].substr(0,4) + _edit_name;

            // Strip out any spaces from the edit name, and update the Preset ID
            _edit_name = std::regex_replace(_edit_name, std::regex{" "}, "_");
            preset_id.set_id(_selected_preset_id.bank_folder(), _edit_name);

            // Are we saving in the current bank folder
            auto new_filename = preset_id.path();
            if (preset_id.bank_folder() == org_preset_id.bank_folder()) {
                // Has the preset index changed?
                if (preset_id.preset_name().substr(0,3) != org_preset_id.preset_name().substr(0,3)) {
                    // We need to rename the existing file to the new name
                    auto ex_preset_id = PresetId();
                    ex_preset_id.set_id(preset_id.bank_folder(), _list_items[num]);
                    auto ex_filename = ex_preset_id.path();
                    if (std::filesystem::exists(ex_filename)) {
                        // Rename it....
                        int ret = ::rename(ex_filename.c_str(), new_filename.c_str());
                        if (ret == 0) {
                            _list_items[num] = _edit_name;
                        }
                        else {
                            MSG("Error renaming Preset File: " << ex_filename << " to: " << new_filename);
                            MONIQUE_LOG_ERROR(module(), "Error renaming Preset file: {} to {}", ex_filename, new_filename);
                        }
                    }                    
                }
                // Has the preset name changed?
                else if (preset_id.preset_name() != org_preset_id.preset_name()) {
                    // We need to rename the org file to the new name
                    auto org_filename = org_preset_id.path();
                    if (std::filesystem::exists(org_filename)) {
                        // Rename it....
                        int ret = ::rename(org_filename.c_str(), new_filename.c_str());
                        if (ret == 0) {
                            _list_items[num] = _edit_name;
                        }
                        else {
                            MSG("Error renaming Preset File: " << org_filename << " to: " << new_filename);
                            MONIQUE_LOG_ERROR(module(), "Error renaming Preset file: {} to {}", org_filename, new_filename);
                        }
                    }
                }
            }
            // Saving to a different bank
            else {
                // We need to rename the existing selected file to the new name
                auto ex_preset_id = PresetId();
                ex_preset_id.set_id(preset_id.bank_folder(), _list_items[num]);
                auto ex_filename = ex_preset_id.path();
                if (std::filesystem::exists(ex_filename)) {
                    // Rename it....
                    int ret = ::rename(ex_filename.c_str(), new_filename.c_str());
                    if (ret == 0) {
                        _list_items[num] = _edit_name;
                    }
                    else {
                        MSG("Error renaming Preset File: " << ex_filename << " to: " << new_filename);
                        MONIQUE_LOG_ERROR(module(), "Error renaming Preset file: {} to {}", ex_filename, new_filename);
                    }
                }
            }
        }

        // Reset the edit name state, and show the select patch load/save screen
        _edit_name_state = EditNameState::NONE;
        _edit_name.clear();

        // Save the preset         
        _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::SAVE_PRESET, preset_id, MoniqueModule::GUI)));

        // Show a confirmations creen
        _show_msg_popup(preset_id.preset_display_name().c_str(), "PRESET SAVED");

        // Show the default screen
        utils::set_preset_modified(false);
        _reset_gui_state_and_show_home_screen(preset_id.preset_display_name());
    }   
}

//----------------------------------------------------------------------------
// _process_select_option_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_select_option_soft_button_2()
{
    // Get the patch source data
    PresetId src_preset_id;
    src_preset_id.set_id(_selected_preset_id.bank_folder(), _list_item_from_index(_selected_preset_index).second);   
    auto src_layer_id = _selected_patch_src_index ? LayerId::D1 : LayerId::D0;

    // Get the patch dest data
    switch (_selected_patch_dst_index) {
        case 0:
        case 1: {
            // Load the specified dst Layer into the src Layer
            auto sys_func = SystemFunc(SystemFuncType::LOAD_PRESET_LAYER, MoniqueModule::GUI);
            sys_func.preset_id = src_preset_id;
            sys_func.src_layer_id = src_layer_id;
            sys_func.dst_layer_id = _selected_patch_dst_index == 0 ? LayerId::D0 : LayerId::D1;      
            _event_router->post_system_func_event(new SystemFuncEvent(sys_func));
            break;
        }

        case 2:
        case 3: {
            // Load the specified dst Layer Sound A into the src Layer Sound B
            auto sys_func = SystemFunc(SystemFuncType::LOAD_PRESET_SOUND, MoniqueModule::GUI);
            sys_func.preset_id = src_preset_id;
            sys_func.src_layer_id = src_layer_id;
            sys_func.dst_layer_id = _selected_patch_dst_index == 2 ? LayerId::D0 : LayerId::D1;;
            sys_func.dst_layer_state = LayerState::STATE_B;      
            _event_router->post_system_func_event(new SystemFuncEvent(sys_func));
            break;
        }

        default:
            break;                                
    }
}

//----------------------------------------------------------------------------
// _process_param_update_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_param_update_soft_button_2(bool selected)
{
    // Parse the edit name state
    switch (_edit_name_state) {
        case EditNameState::NONE: {
            // Only process when released
            if (!selected) {
                // If this is the Sequencer reset param AND the reset sequencer confirmation
                // screen is shown
                if ((_param_shown == utils::get_sys_func_param(SystemFuncType::SEQ_RESET)) && _showing_reset_conf_screen) {
                    // Hide the reset Sequencer screen
                    _hide_reset_seq_screen();
                }
                // If this is the Reset Global Settings param and the reset confirmation screen is shown
                else if ((_param_shown == utils::get_sys_func_param(SystemFuncType::RESET_GLOBAL_SETTINGS)) && _showing_reset_conf_screen) {
                    // Hide the reset to factory screen
                    _hide_reset_to_factory_screen();
                }                
                // If we are not showing the param as an edited enum list
                else if (!_show_param_list || !_editing_param || !_show_param_as_enum_list(_param_shown)) {
                    // Reset the GUI state and show the home screen
                    //_config_sys_func_switches(true);
                    _stop_param_change_timer();
                    _reset_gui_state_and_show_home_screen();            
                }
            }
            break;
        }

        default: {
            // Was the patch name changed
            if (!selected && (_edit_name.size() > 0)) {
                // Right trim all characters after the cursor
                _edit_name = _edit_name.substr(0, _selected_char_index + 1);

                // Also right trim any whitespace
                auto end = _edit_name.find_last_not_of(" ");
                if (end != std::string::npos) {
                    _edit_name = _edit_name.substr(0, end + 1);
                }

                // Has the name actually changed?
                if (std::strcmp(_edit_name.c_str(), utils::get_current_layer_info().patch_name().c_str()) != 0) {
                    // Update it
                    utils::get_current_layer_info().set_patch_name(_edit_name);

                    // Send a system function to indicate a patch has been renamed
                    auto system_func = SystemFunc(SystemFuncType::PATCH_RENAMED, MoniqueModule::GUI);
                    system_func.str_value = _edit_name;
                    _event_router->post_system_func_event(new SystemFuncEvent(system_func));
                }

                // Reset the edit name state, and show the Select Bank screen
                _reset_param_shortcut_switches();
                _show_param();
                _config_data_knob(_params_list.size());
                _config_soft_button_1(false);
                _editing_param = false;            
                _edit_name_state = EditNameState::NONE;
                _edit_name.clear();
            }
            break;
        }
    }
}

//----------------------------------------------------------------------------
// _process_wheels_calibration_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_process_wheels_calibration_soft_button_1()
{
    // Parse the wheels caibrate state
    switch (_wheels_calibrate_state) {
        case WheelsCalibrateState::WHEELS_CALIBRATE_NOT_STARTED: {
            // Calibration was started, reset the status in the calibration file to indicate failed,
            // it's changed to OK at the end of the process
            std::ofstream file(MONIQUE_CALIBRATION_FILE(WHEELS_CAL_STATUS_FILENAME));
            file << "1" << std::endl;
            file.close();

            // Show the Pitch Bend top calibrate screen
            _show_pitch_bend_wheel_top_calibration_screen();
            _gui_state = GuiState::WHEELS_CALIBRATE;
            break;
        }

        case WheelsCalibrateState::PITCH_BEND_WHEEL_TOP_CALIBRATE:
            // Latch the max position
            sfc::latch_pitch_wheel_max();

            // Show the Pitch Bend mid calibrate screen (1)
            _show_pitch_bend_wheel_mid_calibration_screen(true);
            break;
        
        case WheelsCalibrateState::PITCH_BEND_WHEEL_MID_CALIBRATE_1:
            // Latch the mid position (1)
            sfc::latch_pitch_wheel_mid_1();

            // Show the Pitch Bend bottom calibrate screen
            _show_pitch_bend_wheel_bottom_calibration_screen();
            break;            

        case WheelsCalibrateState::PITCH_BEND_WHEEL_BOTTOM_CALIBRATE:
            // Latch the min position
            sfc::latch_pitch_wheel_min();

            // Show the Pitch Bend mid calibrate screen (2)
            _show_pitch_bend_wheel_mid_calibration_screen(false);
            break;

        case WheelsCalibrateState::PITCH_BEND_WHEEL_MID_CALIBRATE_2:
            // Latch the min position (2)
            sfc::latch_pitch_wheel_mid_2();

            // Show the Mod wheel top calibrate screen
            _show_mod_wheel_top_calibration_screen();
            break;

        case WheelsCalibrateState::MOD_WHEEL_TOP_CALIBRATE:
            // Latch the top position
            sfc::latch_mod_wheel_max();

            // Show the Mod wheel bottom calibrate screen
            _show_mod_wheel_bottom_calibration_screen();
            break;
    
        case WheelsCalibrateState::MOD_WHEEL_BOTTOM_CALIBRATE: {
            // Latch the bottom position, and save the Pitch/Mod wheel config
            sfc::latch_mod_wheel_min();
            sfc::save_pitch_mod_wheel_config();

            // Show the Pitch Bend wheel top check screen
            auto param = utils::get_param(utils::ParamRef::MIDI_PITCH_BEND);
            _show_pitch_bend_wheel_top_check_screen(param ? dataconv::pitch_bend_from_normalised_float(param->value()) : 0);
            break;
        }

        case WheelsCalibrateState::PITCH_BEND_WHEEL_TOP_CHECK: {
            // Show the Pitch Bend mid check screen IF the Pitch Bend top check passed
            if (_wheel_check_val_ok) {
                auto param = utils::get_param(utils::ParamRef::MIDI_PITCH_BEND);
                _show_pitch_bend_wheel_mid_check_screen(param ? dataconv::pitch_bend_from_normalised_float(param->value()) : 0);
            }
            break;
        }

        case WheelsCalibrateState::PITCH_BEND_WHEEL_MID_CHECK: {
            // Show the Pitch Bend bottom calibrate screen IF the Pitch Bend mid check passed
            if (_wheel_check_val_ok) {
                auto param = utils::get_param(utils::ParamRef::MIDI_PITCH_BEND);
                _show_pitch_bend_wheel_bottom_check_screen(param ? dataconv::pitch_bend_from_normalised_float(param->value()) : 0);
            }
            break;
        }

        case WheelsCalibrateState::PITCH_BEND_WHEEL_BOTTOM_CHECK: {
            // Show the Mod wheel top check screen IF the Pitch Bend bottom check passed
            if (_wheel_check_val_ok) {
                auto param = utils::get_param(utils::ParamRef::MIDI_MODWHEEL);
                _show_mod_wheel_top_check_screen(param ? dataconv::midi_cc_from_normalised_float(param->value()) : 0);
            }
            break;
        }

        case WheelsCalibrateState::MOD_WHEEL_TOP_CHECK: {
            // Show the Mod wheel bottom check screen IF the Modwheel top check passed
            if (_wheel_check_val_ok) {
                auto param = utils::get_param(utils::ParamRef::MIDI_MODWHEEL);
                _show_mod_wheel_bottom_check_screen(param ? dataconv::midi_cc_from_normalised_float(param->value()) : 0);
            }
            break;
        }

        case WheelsCalibrateState::MOD_WHEEL_BOTTOM_CHECK: {
            // If the Modwheel bottom check passed
            if (_wheel_check_val_ok) {            
                // Go back to the System Menu
                _hide_msg_box();               
                _show_system_menu_screen();

                // Show a popup to indicate calibration is done
                _show_msg_popup("PITCH/MOD WHEEL", "Calibration: COMPLETE");
                MONIQUE_LOG_INFO(module(), "PITCH/MOD WHEEL Calibration: COMPLETE");
                _wheels_calibrate_state = WheelsCalibrateState::WHEELS_CALIBRATE_NOT_STARTED;

                // Calibration was successful, so indicate this in the status file
                std::ofstream file(MONIQUE_CALIBRATION_FILE(WHEELS_CAL_STATUS_FILENAME));
                file << "0" << std::endl;
                file.close();
            }         
            break;          
        }

        default:
            break;
    }
}

//----------------------------------------------------------------------------
// _process_bank_management_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_bank_management_soft_button_2()
{
    // The action here depends on the current Bank Management state
    switch (_bank_management_state) 
    {
        case BankManagmentState::SHOW_LIST:
            // Reset the GUI state and show the home screen
            _stop_param_change_timer();          
            _reset_gui_state_and_show_home_screen();
            break;

        case BankManagmentState::IMPORT: {
            // Parse the import state
            switch (_import_bank_state) {
                case ImportBankState::SELECT_ARCHIVE: 
                    // Go back to the Bank Management Menu
                    _import_bank_state = ImportBankState::NONE;
                    _selected_bank_archive = 0;
                    _show_bank_management_screen();
                    break;

                case ImportBankState::SELECT_DEST:
                    // Go back to the select archive state
                    _selected_bank_dest = 0;
                    _progress_state = ProgressState::NOT_STARTED;
                    _hide_msg_box();
                    _show_select_bank_archive_screen();
                    break;

                case ImportBankState::IMPORT_METHOD:
                    if (_progress_state == ProgressState::NOT_STARTED) {
                        // Show the bank import screen - overwrite
                        _show_import_bank_screen(false);                        
                    }
                    else {
                        // Go back to the select archive state
                        _selected_bank_dest = 0;
                        _progress_state = ProgressState::NOT_STARTED;
                        _hide_msg_box();
                        _show_select_bank_archive_screen();
                    }
                    break;

                default:
                    // No action
                    break;
            }
            break;      
        }

        case BankManagmentState::EXPORT: {
            // Parse the export state
            switch (_export_bank_state) {
                case ExportBankState::SELECT_BANK:
                    // If the export has finished
                    if (_progress_state == ProgressState::FINISHED) {
                        // Show the select destination bank screen
                        _progress_state = ProgressState::NOT_STARTED;
                        _hide_msg_box();
                        _show_select_dest_bank_screen();
                    }
                    else {
                        // Go back to the Bank Management Menu
                        _export_bank_state = ExportBankState::NONE;
                        _selected_bank_dest = 0;
                        _hide_msg_box();
                        _show_bank_management_screen();
                    }
                    break;

                default:
                    // No action
                    break;
            }
            break;
        }

        case BankManagmentState::ADD:
            // Go back to the Bank Management Menu
            _hide_msg_box();
            _show_bank_management_screen();
            break;

        case BankManagmentState::CLEAR:
        {
            // Parse the clear bank state
            switch (_clear_bank_state) {
                case ClearBankState::SELECT_BANK: 
                    // Go back to the Bank Management Menu
                    _clear_bank_state = ClearBankState::NONE;
                    _selected_bank_dest = 0;
                    _hide_msg_box();
                    _show_bank_management_screen();
                    break;

                case ClearBankState::CONFIRM:
                    if (_progress_state == ProgressState::NOT_STARTED) {
                        // Go back to the select bank screen
                        _hide_msg_box();
                        _show_select_dest_bank_screen();
                    }
                    else {
                        // Go back to the Bank Management Menu
                        _clear_bank_state = ClearBankState::NONE;
                        _selected_bank_dest = 0;
                        _hide_msg_box();
                        _show_bank_management_screen();                            
                    }
                    break;

                default:
                    // No action
                    break;
            }
            break;
        }

        default:
            // No action
            break;
    }
}

//----------------------------------------------------------------------------
// _process_select_bank_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_select_bank_data_knob(KnobParam &data_knob)
{
    // Parse the edit name state
    switch (_edit_name_state) {
        case EditNameState::NONE: {
            // Get the knob position value
            auto value = data_knob.position_value(_selected_bank_index);
            if (value != (uint)_selected_bank_index) 
            {
                // Set the new selected bank index
                _selected_bank_index = value;

                // Update the selected list item
                _post_update_selected_list_item(_selected_bank_index);
            }
            break;
        }

        case EditNameState::SELECT_CHAR: {
            // Process the selected char
            _process_rename_select_char(data_knob);
            break;         
        }

        case EditNameState::CHANGE_CHAR: {
            // Process the changed char
            _process_rename_change_char(data_knob);
            break;
        }   
    }
}

//----------------------------------------------------------------------------
// _process_select_preset_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_select_preset_data_knob(KnobParam &data_knob)
{
    // Parse the edit name state
    switch (_edit_name_state) {
        case EditNameState::NONE: {
            // Has the selection changed?
            auto value = data_knob.position_value(_selected_preset_index);
            if (value != (uint)_selected_preset_index) {
                // Set the new selected preset index
                _selected_preset_index = value;

                // Update the selected list item
                _post_update_selected_list_item(_selected_preset_index);

                // Update the soft buttons if in the LOAD state
                if (_manage_preset_state == ManagePresetState::LOAD) {
                    auto msg = GuiMsg();
                    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
                    _selected_preset_index ?
                        _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "OPTIONS") :
                        _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "----");
                    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "LOAD");
                    _post_gui_msg(msg);
                }                                 
            }
            break;
        }

        case EditNameState::SELECT_CHAR: {
            // Process the selected char
            _process_rename_select_char(data_knob);
            break;         
        }

        case EditNameState::CHANGE_CHAR: {
            // Process the changed char
            _process_rename_change_char(data_knob);
            break;
        }
    }        
}

//----------------------------------------------------------------------------
// _process_select_patch_src_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_select_patch_src_data_knob(KnobParam &data_knob)
{
    // Has the selection changed?
    auto value = data_knob.position_value(_selected_patch_src_index);
    if (value != (uint)_selected_patch_src_index) {
        // Set the new selected patch source index
        _selected_patch_src_index = value;

        // Update the selected list item
        _post_update_selected_list_item(_selected_patch_src_index);
    }        
}

//----------------------------------------------------------------------------
// _process_select_patch_dst_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_select_patch_dst_data_knob(KnobParam &data_knob)
{
    // Has the selection changed?
    auto value = data_knob.position_value(_selected_patch_dst_index);
    if (value != (uint)_selected_patch_dst_index) {
        // Set the new selected patch dst index
        _selected_patch_dst_index = value;

        // Update the selected list item
        _post_update_selected_list_item(_selected_patch_dst_index);
    }        
}

//----------------------------------------------------------------------------
// _process_show_param_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_show_param_data_knob(KnobParam &data_knob)
{
    // Are we editing a param?
    if (!_editing_param) {
        // If we are showing the a reset confirmation screen, don't process
        // the data knob
        if (!_showing_reset_conf_screen) {
            // Get the knob position value
            auto value = data_knob.position_value(_param_shown_index);
            if (value != (uint)_param_shown_index) {
                // Set the new selected param
                _param_shown_index = value;

                // Get the list param to show
                if ((uint)_param_shown_index < _params_list.size()) {
                    auto param = _params_list[_param_shown_index];
                    if (param) {
                        // Show the param
                        _param_shown = param;                       
                        (_gui_state == GuiState::MOD_MATRIX) ?
                            _show_param() :
                            _show_param_update(true);
                    }
                }
            }
        }
    }
    else {
        // Parse the edit name state
        switch (_edit_name_state) {
            case EditNameState::NONE: {
                // If the shown param is valid
                if (_param_shown) {
                    // If we are showing the edited param as an enum list
                    if (_show_param_as_enum_list(_param_shown)) {
                        uint num_pos = _param_shown->num_positions();
                        uint pos_value = _param_shown->position_value();

                        // If the number of positions valid (should always be)
                        if (num_pos > 0) {
                            // Get the knob position value and only process if its changed
                            auto value = data_knob.position_value(pos_value);
                            if (value != pos_value) {
                                // Is this the special case of the System Colour param?
                                if ((_param_shown->module() == MoniqueModule::SYSTEM) && (_param_shown->param_id() == SystemParamId::SYSTEM_COLOUR_PARAM_ID)) {
                                    // Get the system colour from the display string selected
                                    auto system_colour = utils::system_config()->get_system_colour(_param_shown->position_string(value));
                                    if (system_colour.size() > 0) {
                                        // Send a set system colour GUI message
                                        auto msg = GuiMsg();
                                        msg.type = GuiMsgType::SET_SYSTEM_COLOUR;
                                        _strcpy_to_gui_msg(msg.system_colour.colour, system_colour.c_str());
                                        _post_gui_msg(msg);

                                        // Show the list update
                                        _show_enum_param_update(value);                                

                                        // Post a system function to indicate it has changed
                                        auto sys_func = SystemFunc(SystemFuncType::SET_SYSTEM_COLOUR, module());
                                        sys_func.str_value = system_colour;
                                        _event_router->post_system_func_event(new SystemFuncEvent(sys_func));                                
                                    }
                                }
                                // Is this the special case of FX Macro Select param?
                                else if (_param_shown == utils::get_param(utils::ParamRef::FX_MACRO_SELECT)) {
                                    // Update the enum param
                                    _show_enum_param_update(value);
                                    
                                    // Post a param change message
                                    auto param_change = ParamChange(_param_shown, module());
                                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                                    _process_param_changed_mapped_params(_param_shown, 0.0f, nullptr); 
                                    
                                    // Get the FX param mapped to the Macro and update the macro level param
                                    auto fp = utils::get_param(MoniqueModule::DAW, Monique::fx_macro_params().at(_param_shown->hr_value()));
                                    auto mlp = utils::get_param(utils::ParamRef::FX_MACRO_LEVEL);
                                    if (fp && mlp) {
                                        // Update the Macro send level param
                                        mlp->set_value(fp->value());

                                        // We need to update the (surface) param mapped to the FX Param
                                        auto sfp = utils::get_sys_func_param(SystemFuncType::FX_PARAM);
                                        if (sfp) {
                                            // Get the mapped param and check there is only one mapped, which must be
                                            // a surface control
                                            auto mp = sfp->mapped_params(nullptr);
                                            if ((mp.size() == 1) && (mp[0]->module() == MoniqueModule::SFC_CONTROL)) {
                                                // Update the surface control - in the FX param state
                                                auto fxs = utils::fx_state();
                                                utils::set_fx_state(utils::FxState::FX_PARAM_STATE);
                                                static_cast<SfcControlParam *>(mp[0])->set_control_state(utils::fx_ui_state());
                                                mp[0]->set_value(fp->value());
                                                utils::set_fx_state(fxs);
                                                static_cast<SfcControlParam *>(mp[0])->set_control_state(utils::fx_ui_state());

                                                // If we are in the FX param state, send a param change to move the knob position
                                                if (utils::fx_state() == utils::FxState::FX_PARAM_STATE) {
                                                    auto param_change = ParamChange(mp[0], module());
                                                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                                                }
                                            }
                                        }
                                    }                              
                                }
                                else {
                                    // Update the enum param
                                    _show_enum_param_update(value);

                                    // If we are edititing the FX Type, then don't post the param update, do this on exiting EDIT instead
                                    // We have to do this as it causes the VST to freak out otherwise
                                    if ((_param_shown != utils::get_param(utils::ParamRef::FX_1_TYPE)) && (_param_shown != utils::get_param(utils::ParamRef::FX_2_TYPE))) {
                                        // Post a param change message
                                        auto param_change = ParamChange(_param_shown, module());
                                        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                                        _process_param_changed_mapped_params(_param_shown, 0.0f, nullptr);

                                        // Are we editing the 12 note mode param?
                                        if (_param_shown == utils::get_param(utils::ParamRef::TWELVE_NOTE_MODE)) {
                                            // Yes - always update the status to indicate whether 12 note mode is active or not
                                            _set_layer_status();
                                        }                                        
                                    }
                                }                       
                            }
                        }                        
                    }
                    // If we are showing the edited param as an enum (but not as an enum list)
                    else if (_param_shown->num_positions() > 0) {
                        // Get the knob position value and only process if its changed
                        uint pos_value = _param_shown->position_value();
                        auto value = data_knob.position_value(pos_value);
                        if (value != pos_value) {
                            // Set the new param value and show it
                            _param_shown->set_value_from_position(value);
                            _show_param_update(false);

                            // Is this the special case of the Octave Offset param?
                            if (_param_shown == utils::get_param(utils::ParamRef::OCTAVE_OFFSET)) {
                                // Manually set the LED states for the KBD Octave inc/dec buttons
                                _set_kbd_octave_led_states(_param_shown->hr_value());
                            }
                            // Is this the special case of AT sensistivity?
                            else if ((_param_shown->module() == MoniqueModule::SYSTEM) && (_param_shown->param_id() == SystemParamId::AT_SENSITIVITY_PARAM_ID)) {
                                // Set the Aftertouch sensitivity
                                utils::set_at_sensitivity(_param_shown);
                            }

                            // Send the param change
                            auto param_change = ParamChange(_param_shown, module());
                            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                            _process_param_changed_mapped_params(_param_shown, 0.0f, nullptr);                  
                        }
                    }
                    else {
                        // Set the new param value and show it
                        auto prev_val = _param_shown->value();                      
                        _param_shown->set_value_from_param(data_knob);
                        
                        // Send the param change
                        auto param_change = ParamChange(_param_shown, module());
                        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                        _process_param_changed_mapped_params(_param_shown, (_param_shown->value() - prev_val), nullptr);

                        // Update thre param value
                        if (_gui_state == GuiState::MOD_MATRIX) {
                            _new_param_list = true;
                        }
                        _show_param_update(true);

                        // Is this the Tempo param? If so also update the tempo in the status bar
                        if (_param_shown == utils::get_tempo_param()) {
                            _set_tempo_status(_param_shown->display_string().second);
                        }
                        else {
                            // Special case handling it we are editing the FX MACRO param
                            // Get the FX MACRO select param so we can check if we are editing it
                            auto msp = utils::get_param(utils::ParamRef::FX_MACRO_SELECT);
                            if (msp) {
                                // Are we editing the FX MACRO param?
                                if (_param_shown == utils::get_param(MoniqueModule::DAW, Monique::fx_macro_params().at(msp->hr_value()))) {
                                    // Yes - we need to update the FX MACRO param and send that too
                                    auto mlp = utils::get_param(utils::ParamRef::FX_MACRO_LEVEL);
                                    if (mlp) {
                                        // Set the value and send it
                                        mlp->set_value(_param_shown->value());
                                        auto param_change = ParamChange(mlp, module());
                                        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

                                        // Is the FX MACRO param selected?
                                        if (utils::fx_state() == utils::FxState::FX_PARAM_STATE) {
                                            // We need to update the (surface) param mapped to the FX Param
                                            auto sfp = utils::get_sys_func_param(SystemFuncType::FX_PARAM);
                                            if (sfp) {
                                                // Get the mapped param and check there is only one mapped, which must be
                                                // a surface control
                                                auto mp = sfp->mapped_params(nullptr);
                                                if ((mp.size() == 1) && (mp[0]->module() == MoniqueModule::SFC_CONTROL)) {
                                                    // Update the surface control
                                                    mp[0]->set_value(_param_shown->value());
                                                    auto param_change = ParamChange(mp[0], module());
                                                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                                                }
                                            }
                                        }
                                    }                                
                                }
                            }          
                        }              
                    }
                }
                break;
            }

            case EditNameState::SELECT_CHAR: {
                // Process the selected char
                _process_rename_select_char(data_knob);
                break;         
            }

            case EditNameState::CHANGE_CHAR: {
                // Process the changed char
                _process_rename_change_char(data_knob);
                break;
            }
        }
    }
}

//----------------------------------------------------------------------------
// _process_system_menu_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_system_menu_data_knob(KnobParam &data_knob)
{
    // If showing the system menu
    if (_system_menu_state == SystemMenuState::SHOW_OPTIONS) {
        // Get the data knob position
        auto value = data_knob.position_value(_selected_system_menu_item);
        if (value != (uint)_selected_system_menu_item) {
            // Set the new menu item
            _selected_system_menu_item = value;

            // Update the selected list item
            _post_update_selected_list_item(_selected_system_menu_item);
        }
    }  
}

//----------------------------------------------------------------------------
// _process_bank_management_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_bank_management_data_knob(KnobParam &data_knob)
{
    // Parse the Bank Management state
    switch (_bank_management_state) {
        case BankManagmentState::SHOW_LIST: {
            // Get the data knob position
            auto value = data_knob.position_value(_selected_bank_management_item);
            if (value != (uint)_selected_bank_management_item) {
                // Set the new menu item
                _selected_bank_management_item = value;

                // Update the selected list item
                _post_update_selected_list_item(_selected_bank_management_item);
            }
            break;
        }

        case BankManagmentState::IMPORT: {
            // Parse the import state
            switch (_import_bank_state) {
                case ImportBankState::SELECT_ARCHIVE: {
                    // Get the data knob position
                    auto value = data_knob.position_value(_selected_bank_archive);
                    if (value != (uint)_selected_bank_archive) {
                        // Set the new menu item
                        _selected_bank_archive = value;
                        _selected_bank_archive_name = _list_items[_selected_bank_archive];

                        // Update the selected list item
                        _post_update_selected_list_item(_selected_bank_archive);
                    }
                    break;                    
                }

                case ImportBankState::SELECT_DEST: {
                    // Get the data knob position
                    auto value = data_knob.position_value(_selected_bank_dest);
                    if (value != (uint)_selected_bank_dest) {
                        // Set the new menu item
                        _selected_bank_dest = value;
                        _selected_bank_dest_name = _list_item_from_index(_selected_bank_dest).second;

                        // Update the selected list item
                        _post_update_selected_list_item(_selected_bank_dest);
                    }
                    break;                    
                }

                default:
                    // No action
                    break;
            }
            break;
        }

        case BankManagmentState::EXPORT: {
            // Parse the export state
            switch (_export_bank_state) {
                case ExportBankState::SELECT_BANK: {
                    // Get the data knob position
                    auto value = data_knob.position_value(_selected_bank_dest);
                    if (value != (uint)_selected_bank_dest) {
                        // Set the new menu item
                        _selected_bank_dest = value;
                        _selected_bank_dest_name = _list_item_from_index(_selected_bank_dest).second;

                        // Update the selected list item
                        _post_update_selected_list_item(_selected_bank_dest);
                    }
                    break;                    
                }

                default:
                    // No action
                    break;
            }
            break;
        }

        case BankManagmentState::CLEAR: {
            // Parse the clear state
            switch (_clear_bank_state) {
                case ClearBankState::SELECT_BANK: {
                    // Get the data knob position
                    auto value = data_knob.position_value(_selected_bank_dest);
                    if (value != (uint)_selected_bank_dest) {
                        // Set the new menu item
                        _selected_bank_dest = value;
                        _selected_bank_dest_name = _list_item_from_index(_selected_bank_dest).second;

                        // Update the selected list item
                        _post_update_selected_list_item(_selected_bank_dest);
                    }
                    break;                    
                }

                default:
                    // No action
                    break;
            }
            break;
        }

        default:
            // No action
            break;
    }
}

//----------------------------------------------------------------------------
// _process_wt_management_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_wt_management_data_knob(KnobParam &data_knob)
{
    // Get the data knob position
    auto value = data_knob.position_value(_selected_wt_management_item);
    if (value != (uint)_selected_wt_management_item) {
        // Set the new menu item
        _selected_wt_management_item = value;

        // Update the selected list item
        _post_update_selected_list_item(_selected_wt_management_item);
    }
}

//----------------------------------------------------------------------------
// _process_run_diag_script_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_run_diag_script_data_knob(KnobParam &data_knob)
{
    // Only process when selecting a script
    if (_run_diag_script_state == RunDiagScriptState::SELECT_DIAG_SCRIPT) {
        // Get the data knob position
        auto value = data_knob.position_value(_selected_diag_script);
        if (value != (uint)_selected_diag_script) {
            // Set the new menu item
            _selected_diag_script = value;

            // Update the selected list item
            _post_update_selected_list_item(_selected_diag_script);
        }
    }
}

//----------------------------------------------------------------------------
// _process_system_menu
//----------------------------------------------------------------------------
void GuiManager::_process_system_menu(bool selected)
{
    // Should we show the SYSTEM menu?
    if (selected) {
        // Reset the GUI state and show the system screen
        _reset_gui_state();
        _show_system_menu_screen();      
    }
    else {
        // Reset the GUI state and show the home screen
        _reset_gui_state_and_show_home_screen();
    }    
}
