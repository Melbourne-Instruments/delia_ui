/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  utils.cpp
 * @brief Utility functions implementation.
 *-----------------------------------------------------------------------------
 */

#include <iostream>
#include <random>
#include <sstream>
#include <stdint.h>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <condition_variable>
#include <utility>
#include <fstream>
#include <atomic>
#include <regex>
#include <assert.h>
#include <sys/sysinfo.h>
#include <dirent.h>
#ifndef NO_XENOMAI
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <xenomai/init.h>
#endif
#include "system_func.h"
#include "utils.h"

// MACROs
#define MONIQUE_RT_TASK_PRIORITY    45
#define RT_TASK_CREATE_DELAY        10000
#define MAKE_UI_STATE(prefix)       (std::string(prefix) + UI_STATE_SUFFIX)

// Constants
constexpr char TEMPO_BPM_PARAM_NAME[]              = "tempo_bpm";
constexpr char TEMPO_BPM_DISPLAY_NAME[]            = "Tempo BPM";
constexpr char TEMPO_BPM_FINE_PARAM_NAME[]         = "tempo_bpm_fine";
constexpr char TEMPO_BPM_FINE_DISPLAY_NAME[]       = "Tempo BPM Fine";
constexpr char MIDI_CLK_IN_PARAM_NAME[]            = "midi_clk_in";
constexpr char MIDI_CLK_IN_DISPLAY_NAME[]          = "MIDI Clock In";
constexpr char MIDI_ECHO_FILTER_PARAM_NAME[]       = "midi_echo_filter";
constexpr char MIDI_ECHO_FILTER_DISPLAY_NAME[]     = "MIDI Echo Filter";
constexpr char KBD_MIDI_CHANNEL_PARAM_NAME[]       = "kbd_midi_channel";
constexpr char WT_NAME_PARAM_NAME[]                = "wt_name";
constexpr char WT_NAME_DISPLAY_NAME[]              = "WT Name";
constexpr char SYSTEM_COLOUR_PARAM_NAME[]          = "system_colour";
constexpr char SYSTEM_COLOUR_DISPLAY_NAME[]        = "System Colour";
constexpr char SEQ_ARP_MIDI_CHANNEL_PARAM_NAME[]   = "seq_arp_midi_channel";
constexpr char SEQ_ARP_MIDI_CHANNEL_DISPLAY_NAME[] = "Seq/Arp MIDI Channel";
constexpr char PATCH_NAME_PARAM_NAME[]             = "patch_name";
constexpr char PATCH_NAME_DISPLAY_NAME[]           = "Patch Name";
constexpr char SUSTAIN_POLARITY_PARAM_NAME[]       = "sustain_polarity";
constexpr char SUSTAIN_POLARITY_DISPLAY_NAME[]     = "Sustain Polarity";
constexpr char CUTOFF_LINK_PARAM_NAME[]            = "cutoff_link";
constexpr char CUTOFF_LINK_DISPLAY_NAME[]          = "Cutoff Link";
constexpr char AT_SENSITIVITY_PARAM_NAME[]         = "at_sensitivity";
constexpr char AT_SENSITIVITY_DISPLAY_NAME[]       = "AT Sensitivity";
constexpr char UI_STATE_SUFFIX[]                   = "state";
constexpr char DEFAULT_UI_STATE[]                  = "default";
constexpr char OSC_1_OSC_2_UI_STATE[]              = "osc_1_osc_2_";
constexpr char OSC_3_OSC_4_UI_STATE[]              = "osc_3_osc_4_";
constexpr char OSC_1_OSC_2_TUNE_FINE_UI_STATE[]    = "osc_1_osc_2_tune_fine_";
constexpr char OSC_1_OSC_2_TUNE_COARSE_UI_STATE[]  = "osc_1_osc_2_tune_coarse_";
constexpr char OSC_3_TUNE_FINE_UI_STATE[]          = "osc_3_tune_fine_";
constexpr char OSC_3_TUNE_COARSE_UI_STATE[]        = "osc_3_tune_coarse_";
constexpr char LFO_1_UI_STATE[]                    = "lfo_1_";
constexpr char LFO_1_SYNC_RATE_UI_STATE[]          = "lfo_1_sync_rate_";
constexpr char LFO_2_UI_STATE[]                    = "lfo_2_";
constexpr char LFO_2_SYNC_RATE_UI_STATE[]          = "lfo_2_sync_rate_";
constexpr char LFO_3_UI_STATE[]                    = "lfo_3_";
constexpr char LFO_3_SYNC_RATE_UI_STATE[]          = "lfo_3_sync_rate_";
constexpr char RES_LP_UI_STATE[]                   = "res_lp_";
constexpr char RES_HP_UI_STATE[]                   = "res_hp_";
constexpr char VCF_UI_STATE[]                      = "vcf_eg_";
constexpr char VCA_UI_STATE[]                      = "vca_eg_";
constexpr char AUX_UI_STATE[]                      = "aux_eg_";
constexpr char FX_SEND_STATE[]                     = "fx_send_";
constexpr char FX_PARAM_STATE[]                    = "fx_param_";
constexpr char TEMPO_UI_STATE[]                    = "tempo_";
constexpr char GLIDE_UI_STATE[]                    = "glide_";
constexpr char MULTIFN_SWITCHES_MOD_STATE[]        = "multifn_";
constexpr char MULTIFN_SWITCHES_SEQ_STATE[]        = "multifn_seq_";
constexpr char SEQ_CHUNK_PARAM_RESET_VALUE[]       = "00000000FFFFFF00000000FFFFFF00000000FFFFFF00000000FFFFFF00000000FFFFFF00000000FFFFFF00000000FFFFFF00000000FFFFFF00000000FFFFFF00000000FFFFFF";

// Private variables
SystemConfig _system_config;
bool _maintenance_mode = false;
bool _demo_mode = false;
std::string _session_uuid;
bool _preset_modified;
LayerInfo _d0_layer(LayerId::D0);
LayerInfo _d1_layer(LayerId::D1);
std::atomic<LayerId> _current_layer_id { LayerId::D0 };
std::mutex _params_mutex;
std::vector<std::unique_ptr<Param>> _global_params;
std::vector<std::unique_ptr<Param>> _layer_params;
std::vector<std::string> _params_blacklist;
std::vector<sfc::HapticMode> _haptic_modes;
KnobParam *_data_knob_param = nullptr;
Param *_wt_filename_param = nullptr;
Param *_morph_value_param = nullptr;
KnobParam *_morph_knob_param = nullptr;
Param *_morph_mode_param = nullptr;
bool _morph_enabled = false;
bool _prev_morph_enabled = false;
std::mutex _morph_mutex;
utils::OscState _osc_state = utils::OscState::OSC_1_OSC_2_STATE;
utils::OscTuneState _osc_tune_state = utils::OscTuneState::OSC_TUNE_FINE;
utils::LfoState _lfo_state = utils::LfoState::LFO_1_STATE;
utils::LfoRateState _lfo_rate_state = utils::LfoRateState::LFO_NORMAL_RATE;
utils::ResState _res_state = utils::ResState::RES_LP_STATE;
utils::EgState _eg_state = utils::EgState::VCA_EG_STATE;
utils::FxState _fx_state = utils::FxState::FX_SEND_STATE;
utils::TempoGlideState _tempo_glide_state = utils::TempoGlideState::TEMPO_STATE;
utils::MultifnSwitchesState _multifn_switches_state = utils::MultifnSwitchesState::DEFAULT;
Monique::ModMatrixSrc _mod_state = Monique::ModMatrixSrc::KEY_PITCH;
BaseManager *_daw_mgr;
BaseManager *_midi_device_mgr;
BaseManager *_arp_mgr;
BaseManager *_seq_mgr;
BaseManager *_sw_mgr;
bool _seq_signalled = false;
std::mutex _seq_mutex;
std::condition_variable _seq_cv;
bool _arp_signalled = false;
std::mutex _arp_mutex;
std::condition_variable _arp_cv;
const char *_mod_state_prefix[] = {
    "key_pitch_mod_",
    "key_vel_mod_",
    "aftertouch_mod_",
    "modwheel_mod_",
    "lfo_1_mod_",
    "lfo_2_mod_",
    "lfo_3_mod_",
    "eg_vcf_mod_",
    "eg_vca_mod_",
    "eg_aux_mod_",
    "osc_1_mod_",
    "osc_2_mod_",
    "osc_3_mod_",
    "panner_mod_",
    "expression_mod_",
    "cv_1_mod_",
    "cv_2_mod_",
    "cc_1_mod_",
    "cc_2_mod_",
    "offset_mod_"
};
const char *_param_refs[] = {
    "Selected_Layer",
    "Layer_1_Num_Voices",
    "Layer_2_Num_Voices",
    "12_Note_Mode",
    "State",
    "Midi_Channel_Filter",
    "Midi_Low_Note_Filter",
    "Midi_High_Note_Filter",
    "Octave_Offset",
    "Unison_Voices",
    "WT_Select",
    "OSC_1_Fine_Tune",
    "OSC_1_Shape",
    "OSC_1_Level",
    "OSC_2_Fine_Tune",
    "OSC_2_Shape",
    "OSC_2_Level",
    "OSC_3_Fine_Tune",
    "OSC_3_Pos",
    "OSC_3_Level",
    "OSC_4_Noise_Mode",
    "LFO_1_Shape",
    "LFO_2_Shape",
    "LFO_3_Shape",
    "LFO_1_Slew",
    "LFO_2_Slew",
    "LFO_3_Slew",    
    "LFO_1_Tempo_Sync",
    "LFO_2_Tempo_Sync",
    "LFO_3_Tempo_Sync",
    "VCF_Env_Attack",
    "VCA_Env_Attack",
    "AUX_Env_Attack",
    "VCF_EG_Reset",
    "VCA_EG_Reset",
    "AUX_EG_Reset",
    "Filter_HP_Cutoff",
    "Filter_HP_Resonance",
    "Filter_LP_Resonance",
    "Filter_LP_Mode",
    "FX_1_Type",
    "FX_2_Type",
    "FX_Macro_Select",
    "FX_Macro_Level",
    "Glide_Mode",
    "Morph_Value",
    "Morph_Mode",
    "MIDI_Pitch_Bend",
    "MIDI_Modwheel",
    "MIDI_Aftertouch",
    "Midi_Sustain",
    "Midi_Expression",
    "Midi_Mod_Src_1_Sel",
    "Midi_Mod_Src_2_Sel",
    "MIDI_CC_1_Mod_Source",
    "MIDI_CC_2_Mod_Source",
    "Metronome_Trigger"  
};

// Private functions
Param *_get_param(std::string path);
Param *_get_param(std::string path, bool preset_param);
Param *_get_param(std::string path, std::vector<std::unique_ptr<Param>>& params);

//----------------------------------------------------------------------------
// init_xenomai
//----------------------------------------------------------------------------
void utils::init_xenomai()
{
#ifndef NO_XENOMAI	
    // Fake command line arguments to pass to xenomai_init(). For some
    // obscure reasons, xenomai_init() crashes if argv is allocated here on
    // the stack, so we alloc it beforehand
    int argc = 2;
    auto argv = new char* [argc + 1];
    for (int i = 0; i < argc; i++) {
        argv[i] = new char[32];
    }
    argv[argc] = nullptr;
    std::strcpy(argv[0], "delia_app");

    // Add cpu affinity argument to xenomai init - setting it to just core 3
    std::strcpy(argv[1], "--cpu-affinity=3");

    // Initialise Xenomai
    xenomai_init(&argc, (char* const**) &argv);
#endif
}

//----------------------------------------------------------------------------
// create_rt_task
// Note: The priority off all real-time tasks created here is arbitrarily set
// at 45, which is half the Raspa real-time task priority
//------------------------------------:----------------------------------------
int utils::create_rt_task(pthread_t *rt_thread, void *(*start_routine)(void *), void *arg, int sched_policy)
{
#ifndef NO_XENOMAI    
    struct sched_param rt_params = {.sched_priority = MONIQUE_RT_TASK_PRIORITY};
    pthread_attr_t task_attributes;

    // Initialise the RT task
    __cobalt_pthread_attr_init(&task_attributes);
    pthread_attr_setdetachstate(&task_attributes, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setinheritsched(&task_attributes, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&task_attributes, sched_policy);

    // Create the RT thread
    pthread_attr_setschedparam(&task_attributes, &rt_params);    
    int res = __cobalt_pthread_create(rt_thread, &task_attributes, start_routine, arg);
    usleep(RT_TASK_CREATE_DELAY);
    return res;
#else
    (void)rt_thread;
    (void)start_routine;
    (void)arg;
    (void)sched_policy;
    return 0;
#endif
}

//----------------------------------------------------------------------------
// stop_rt_task
//----------------------------------------------------------------------------
void utils::stop_rt_task(pthread_t *rt_thread)
{
#ifndef NO_XENOMAI
    // Note: Need to signal the task to end, TBD
    __cobalt_pthread_join(*rt_thread, nullptr);
    *rt_thread = 0;
#else
    (void)rt_thread;
#endif
}

//----------------------------------------------------------------------------
// rt_task_nanosleep
//----------------------------------------------------------------------------
void utils::rt_task_nanosleep(struct timespec *time)
{
#ifndef NO_XENOMAI    
    // Perform the RT sleep
    __cobalt_clock_nanosleep(CLOCK_REALTIME, 0, time, NULL);
#else
    (void)time;
#endif
}

//----------------------------------------------------------------------------
// system_config
//----------------------------------------------------------------------------
SystemConfig *utils::system_config()
{
    // Return a pointer to the system config object
    return &_system_config;
}

//----------------------------------------------------------------------------
// generate_session_uuid
//----------------------------------------------------------------------------
void utils::generate_session_uuid()
{
    static std::random_device              rd;
    static std::mt19937                    gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    int i;
    ss << std::hex;
    for (i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4";
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 12; i++) {
        ss << dis(gen);
    }
    _session_uuid = ss.str();
}

//----------------------------------------------------------------------------
// generate_session_uuid
//----------------------------------------------------------------------------
std::string utils::get_session_uuid()
{
    // Return the generated session UUID
    return _session_uuid;
}

//----------------------------------------------------------------------------
// set_maintenance_mode
//----------------------------------------------------------------------------
void utils::set_maintenance_mode(bool on)
{
    _maintenance_mode = on;
}

//----------------------------------------------------------------------------
// maintenance_mode
//----------------------------------------------------------------------------
bool utils::maintenance_mode()
{
    return _maintenance_mode;
}

//----------------------------------------------------------------------------
// set_demo_mode
//----------------------------------------------------------------------------
void utils::set_demo_mode(bool on)
{
    _demo_mode = on;
}

//----------------------------------------------------------------------------
// demo_mode
//----------------------------------------------------------------------------
bool utils::demo_mode()
{
    return _demo_mode;
}

//----------------------------------------------------------------------------
// preset_modified
//----------------------------------------------------------------------------
bool utils::preset_modified()
{
    // Return if the preset is modified or not
    return _preset_modified;
}

//----------------------------------------------------------------------------
// set_demo_mode_timeout
//----------------------------------------------------------------------------
void utils::set_preset_modified(bool modified)
{
    // Set the preset modified state
    _preset_modified = modified;
}

//----------------------------------------------------------------------------
// get_current_layer_info
//----------------------------------------------------------------------------
LayerInfo &utils::get_current_layer_info()
{
    // Return the current Layer info
    return _current_layer_id == LayerId::D0 ? _d0_layer : _d1_layer;
}

//----------------------------------------------------------------------------
// get_layer_info
//----------------------------------------------------------------------------
LayerInfo &utils::get_layer_info(LayerId id)
{
    // Return the specified Layer info
    return id == LayerId::D0 ? _d0_layer : _d1_layer;
}

//----------------------------------------------------------------------------
// set_current_layer
//----------------------------------------------------------------------------
void utils::set_current_layer(LayerId id)
{
    // Set the current Layer
    _current_layer_id = id;
}

//----------------------------------------------------------------------------
// is_current_layer
//----------------------------------------------------------------------------
bool utils::is_current_layer(LayerId id)
{
    // Check if the passed Layer type is the current layer
    return id == _current_layer_id;
}

//----------------------------------------------------------------------------
// get_global_params
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_global_params()
{
    std::vector<Param *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Parse the global params
    for (const std::unique_ptr<Param> &p : _global_params) {
        // Global param?
        if (p->type() == ParamType::GLOBAL) {
            // Yes, add it
            params.push_back(p.get());
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_daw_params
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_daw_params()
{
    std::vector<Param *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Parse the global params
    for (const std::unique_ptr<Param> &p : _global_params) {
        // Daw param?
        if (p->module() == MoniqueModule::DAW) {
            // Yes, add it
            params.push_back(p.get());
        }
    }

    // Parse the Layer params
    for (const std::unique_ptr<Param> &p : _layer_params) {
        // Daw param?
        if (p->module() == MoniqueModule::DAW) {
            // Yes, add it
            params.push_back(p.get());
        }
    }     
    return params;
}

//----------------------------------------------------------------------------
// get_layer_params
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_layer_params()
{
    std::vector<Param *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Return the Layer params
    for (const std::unique_ptr<Param> &p : _layer_params) {
        params.push_back(p.get());
    }   
    return params;
}

//----------------------------------------------------------------------------
// get_mod_matrix_params
//----------------------------------------------------------------------------
std::vector<LayerStateParam *> utils::get_mod_matrix_params()
{
    std::vector<LayerStateParam *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Parse the Layer params
    for (const std::unique_ptr<Param> &p : _layer_params) {
        // If this is a state param?
        if (p->type() == ParamType::PATCH_STATE) {
            LayerStateParam *sp = static_cast<LayerStateParam *>(p.get());

            // Is this a Mod Matrix param?
            if (sp->mod_matrix_param()) {
                // Yes, add it
                params.push_back(sp);
            }
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_preset_params
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_preset_params()
{
    std::vector<Param *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Parse the global params
    for (const std::unique_ptr<Param> &p : _global_params) {
        // Preset param?
        if (p->preset()) {
            // Yes, add it
            params.push_back(p.get());
        }
    }

    // Parse the Layer params
    for (const std::unique_ptr<Param> &p : _layer_params) {
        // Preset param?
        if (p->preset()) {
            // Yes, add it
            params.push_back(p.get());
        }
    }   
    return params;
}

//----------------------------------------------------------------------------
// get_multifn_switch_params
//----------------------------------------------------------------------------
std::vector<SwitchParam *> utils::get_multifn_switch_params()
{
    std::vector<SwitchParam *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Parse the global params
    for (std::unique_ptr<Param> &p : _global_params) {
        // Is this a multi-function switch?
        if ((p->module() == MoniqueModule::SFC_CONTROL) &&
            (static_cast<SfcControlParam *>(p.get())->control_type() == sfc::ControlType::SWITCH)) {
            auto *sp = static_cast<SwitchParam *>(p.get());
            if (sp->multifn_switch()) {
                // Push the param
                params.push_back(sp);
            }
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_params
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_params(MoniqueModule module)
{
    std::vector<Param *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Parse the global params
    for (const std::unique_ptr<Param> &p : _global_params) {
        // Specified module?
        if (p->module() == module) {
            // Yes, add it
            params.push_back(p.get());
        }
    }

    // Parse the Layer params
    for (const std::unique_ptr<Param> &p : _layer_params) {
        // Specified module?
        if (p->module() == module) {
            // Yes, add it
            params.push_back(p.get());
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_params
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_params(const std::string param_path_regex)
{
    std::vector<Param *> params;
    std::cmatch m;
    const std::regex base_regex(param_path_regex);

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Parse the global params
    for (const std::unique_ptr<Param> &p : _global_params) {
        // Does the param path match (regex)
        if (std::regex_match(p->path().c_str(), m, base_regex))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }

    // Parse the Layer params
    for (const std::unique_ptr<Param> &p : _layer_params) {
        // Does the param path match (regex)
        if (std::regex_match(p->path().c_str(), m, base_regex))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }     
    return params;
}

//----------------------------------------------------------------------------
// get_params_with_state
//----------------------------------------------------------------------------
std::vector<SfcControlParam *> utils::get_params_with_state(const std::string state)
{
    std::vector<SfcControlParam *> params;

    // Was a state specified?
    if (state.size() > 0) {
        // Get the params mutex
        std::lock_guard<std::mutex> lock(_params_mutex);

        // Parse the global params
        for (const std::unique_ptr<Param> &p : _global_params) {
            // If this is a Surface Control param
            if (p->module() == MoniqueModule::SFC_CONTROL) {
                SfcControlParam *sp = static_cast<SfcControlParam *>(p.get());

                // Does the param have this state?
                if (sp->has_control_state(state)) {
                    // Yes, add it
                    params.push_back(sp);
                }
            }
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_grouped_params
//----------------------------------------------------------------------------
std::vector<SfcControlParam *> utils::get_grouped_params(const std::string group_name)
{
    std::vector<SfcControlParam *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Parse the global params
    for (const std::unique_ptr<Param> &p : _global_params) {
        // If this is a Surface Control param
        if (p->module() == MoniqueModule::SFC_CONTROL) {
            SfcControlParam *sp = static_cast<SfcControlParam *>(p.get());

            // Does the group name match?
            if (sp->group_name() == group_name) {
                // Yes, add it
                params.push_back(sp);
            }
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_param
//----------------------------------------------------------------------------
Param *utils::get_param(std::string path)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Get the param
    return _get_param(path);
}

//----------------------------------------------------------------------------
// get_param
//----------------------------------------------------------------------------
Param *utils::get_param(MoniqueModule module, int param_id)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Parse the global params
    for (const std::unique_ptr<Param> &p : _global_params) {
        // Does the parameter module and ID and passed state match?
        if ((p->module() == module) && (p->param_id() == param_id)) {             
            // Param found, return it
            return (p.get());
        }
    }

    // Parse the Layer params
    for (const std::unique_ptr<Param> &p : _layer_params) {
        // Does the parameter module and ID and passed state match?
        if ((p->module() == module) && (p->param_id() == param_id)) { 
            // Param found, return it
            return (p.get());
        }
    }    
    return nullptr;    
}

//----------------------------------------------------------------------------
// get_param
//----------------------------------------------------------------------------
Param *utils::get_param(ParamRef ref)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Parse the global params
    for (const std::unique_ptr<Param> &p : _global_params) {
        // Does the reference match?
        if (p->ref() == _param_refs[ref]) {
            // Param found, return it
            return (p.get());
        }
    }

    // Parse the Layer params
    for (const std::unique_ptr<Param> &p : _layer_params) {
        // Does the reference match?
        if (p->ref() == _param_refs[ref]) {
            // Param found, return it
            return (p.get());
        }
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// get_sys_func_param
//----------------------------------------------------------------------------
SystemFuncParam *utils::get_sys_func_param(SystemFuncType sys_func_type)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Parse the global params
    for (const std::unique_ptr<Param> &p : _global_params) {
        // Does the parameter type and system function match?
        if ((p->type() == ParamType::SYSTEM_FUNC) && 
            (static_cast<const SystemFuncParam *>(p.get())->system_func_type() == sys_func_type)) {
            // Param found, return it
            return (static_cast<SystemFuncParam *>(p.get()));
        }
    }
    return nullptr;    
}

//----------------------------------------------------------------------------
// get_data_knob_param
//----------------------------------------------------------------------------
KnobParam *utils::get_data_knob_param()
{
    return _data_knob_param;
}

//----------------------------------------------------------------------------
// get_wt_filename_param
//----------------------------------------------------------------------------
Param *utils::get_wt_filename_param()
{
    return _wt_filename_param;
}

//----------------------------------------------------------------------------
// get_morph_value_param
//----------------------------------------------------------------------------
Param *utils::get_morph_value_param()
{
    return _morph_value_param;
}

//----------------------------------------------------------------------------
// get_morph_knob_param
//----------------------------------------------------------------------------
KnobParam *utils::get_morph_knob_param()
{
    return _morph_knob_param;
}

//----------------------------------------------------------------------------
// morph_mode
//----------------------------------------------------------------------------
Monique::MorphMode utils::morph_mode()
{
    // Return the current morph mode
    Monique::MorphMode mode = Monique::MorphMode::DANCE;
    if (_morph_mode_param) {
        mode = static_cast<Monique::MorphMode>(_morph_mode_param->hr_value());
    }
    return mode;
}

//----------------------------------------------------------------------------
// set_data_knob_param
//----------------------------------------------------------------------------
void utils::set_data_knob_param(KnobParam *param)
{
    // Set the data knob param if not already set
    if (!_data_knob_param)
        _data_knob_param = param;
}

//----------------------------------------------------------------------------
// set_morph_value_param
//----------------------------------------------------------------------------
void utils::set_morph_value_param(Param *param)
{
    // Set the morph value param if not already set
    if (!_morph_value_param)
        _morph_value_param = param;
}

//----------------------------------------------------------------------------
// set_morph_knob_param
//----------------------------------------------------------------------------
void utils::set_morph_knob_param(KnobParam *param)
{
    // Set the morph knob param if not already set
    if (!_morph_knob_param)
        _morph_knob_param = param;
}

//----------------------------------------------------------------------------
// set_morph_mode_param
//----------------------------------------------------------------------------
void utils::set_morph_mode_param(Param *param)
{
    // Set the morph mode param if not already set - this is for efficiency when
    // retreiving the morph mode
    if (!_morph_mode_param)
        _morph_mode_param = param;
}

//----------------------------------------------------------------------------
// param_has_ref
//----------------------------------------------------------------------------
bool utils::param_has_ref(const Param *param, ParamRef ref)
{
    return param->ref() == _param_refs[ref];
}

//----------------------------------------------------------------------------
// blacklist_param
//----------------------------------------------------------------------------
void utils::blacklist_param(std::string path)
{
    // Is this param already blacklisted?
    if (!param_is_blacklisted(path))
    {
        // Add the blacklisted param
        _params_blacklist.push_back(path);
    }
}

//----------------------------------------------------------------------------
// param_is_blacklisted
//----------------------------------------------------------------------------
bool utils::param_is_blacklisted(std::string path)
{
    // Parse all blacklisted params to see if it already exists
    for (const std::string &p : _params_blacklist)
    {
        // Is this param already blacklisted?
        if (p == path)
        {
            // Blacklisted param
            return true;
        }
    }
    return false;
}

//----------------------------------------------------------------------------
// register_param
//----------------------------------------------------------------------------
void utils::register_param(std::unique_ptr<Param> param)
{
    // Is this a global or system func param?
    if ((param->type() == ParamType::GLOBAL) || (param->type() == ParamType::SYSTEM_FUNC)) {
        // Check if this param already exists
        if (_get_param(param->path(), _global_params) == nullptr) {
            // Add the param
            _global_params.push_back(std::move(param));           
        }
    }
    else {
        // Check if this param already exists
        if (_get_param(param->path(), _layer_params) == nullptr) {
            // Add the param
            _layer_params.push_back(std::move(param));
        }
    }
}

//----------------------------------------------------------------------------
// register_system_params
//----------------------------------------------------------------------------
void utils::register_system_params()
{
	// Register the Tempo BMP param
	auto param1 = Param::CreateParam(SystemParamId::TEMPO_BPM_PARAM_ID, TEMPO_BPM_PARAM_NAME, TEMPO_BPM_DISPLAY_NAME);
    param1->set_type(ParamType::PRESET_COMMON);
    param1->set_hr_value(common::DEFAULT_TEMPO_BPM);
    param1->set_display_hr_value(true);
    utils::register_param(std::move(param1));

	// Register the MIDI Clock In param (global)
	auto param3 = Param::CreateParam(SystemParamId::MIDI_CLK_IN_PARAM_ID, MIDI_CLK_IN_PARAM_NAME, MIDI_CLK_IN_DISPLAY_NAME);
	param3->set_hr_value(false);
	utils::register_param(std::move(param3));
    
	// Register the MIDI Echo Filter param (global)
	auto param4 = Param::CreateParam(SystemParamId::MIDI_ECHO_FILTER_PARAM_ID, MIDI_ECHO_FILTER_PARAM_NAME, MIDI_ECHO_FILTER_PARAM_NAME);
	param4->set_hr_value((float)MidiEchoFilter::ECHO_FILTER);
	utils::register_param(std::move(param4)); 

	// Register the KBD MIDI Channel param (global)
	auto param5 = Param::CreateParam(SystemParamId::KBD_MIDI_CHANNEL_PARAM_ID, KBD_MIDI_CHANNEL_PARAM_NAME, KBD_MIDI_CHANNEL_PARAM_NAME);
	param5->set_hr_value(0);
	utils::register_param(std::move(param5)); 

	// Register the Wavetable Name param
	auto param6 = LayerStateParam::CreateParam(SystemParamId::WT_NAME_PARAM_ID, WT_NAME_PARAM_NAME, WT_NAME_DISPLAY_NAME, ParamDataType::STRING);
    param6->set_str_state_value(LayerId::D0, LayerState::STATE_A, "");
    param6->set_str_state_value(LayerId::D0, LayerState::STATE_B, "");
    param6->set_str_state_value(LayerId::D1, LayerState::STATE_A, "");
    param6->set_str_state_value(LayerId::D1, LayerState::STATE_B, "");
    _wt_filename_param = param6.get();
	utils::register_param(std::move(param6));

	// Register the System Colour param
	auto param7 = Param::CreateParam(SystemParamId::SYSTEM_COLOUR_PARAM_ID, 
                                     SYSTEM_COLOUR_PARAM_NAME, 
                                     SYSTEM_COLOUR_DISPLAY_NAME,
                                     ParamDataType::STRING);
    param7->set_preset(false);
	param7->set_str_value(DEFAULT_SYSTEM_COLOUR);
	utils::register_param(std::move(param7));

	// Register the SEQ/ARP MIDI Channel param
	auto param8 = Param::CreateParam(SystemParamId::SEQ_ARP_MIDI_CHANNEL_PARAM_ID, SEQ_ARP_MIDI_CHANNEL_PARAM_NAME, SEQ_ARP_MIDI_CHANNEL_DISPLAY_NAME);
    param8->set_type(ParamType::PRESET_COMMON);
    param8->set_hr_value(0);
	utils::register_param(std::move(param8));

	// Register the Patch Name param
	auto param9 = Param::CreateParam(SystemParamId::PATCH_NAME_PARAM_ID, PATCH_NAME_PARAM_NAME, PATCH_NAME_DISPLAY_NAME, ParamDataType::STRING);
    param9->set_preset(false);
    param9->set_str_value("");
	utils::register_param(std::move(param9));

    // Register the Sustain Polarity param
    auto param10 = Param::CreateParam(SystemParamId::SUSTAIN_POLARITY_PARAM_ID, SUSTAIN_POLARITY_PARAM_NAME, SUSTAIN_POLARITY_DISPLAY_NAME);
    param10->set_type(ParamType::GLOBAL);
    param10->set_hr_value(SustainPolarity::POSITIVE);
    utils::register_param(std::move(param10));

    // Register the Cutoff Link param
    auto param11 = LayerParam::CreateParam(SystemParamId::CUTOFF_LINK_PARAM_ID, CUTOFF_LINK_PARAM_NAME, CUTOFF_LINK_DISPLAY_NAME);
    param11->set_type(ParamType::LAYER);
    param11->set_hr_value(LayerId::D0, 0.0);
    param11->set_hr_value(LayerId::D1, 0.0);
    utils::register_param(std::move(param11));

    // Register the AT Sensitivity param
    auto param12 = Param::CreateParam(SystemParamId::AT_SENSITIVITY_PARAM_ID, AT_SENSITIVITY_PARAM_NAME, AT_SENSITIVITY_DISPLAY_NAME);
    param12->set_type(ParamType::GLOBAL);
    param12->set_hr_value(5.0);
    utils::register_param(std::move(param12));          
}

//----------------------------------------------------------------------------
// default_ui_state
//----------------------------------------------------------------------------
std::string utils::default_ui_state()
{
    // Return the default UI state
    return DEFAULT_UI_STATE;
}

//----------------------------------------------------------------------------
// osc_state
//----------------------------------------------------------------------------
utils::OscState utils::osc_state()
{
    // Return the current Oscillators state
    return _osc_state;
}

//----------------------------------------------------------------------------
// osc_tune_state
//----------------------------------------------------------------------------
utils::OscTuneState utils::osc_tune_state()
{
    // Return the current Oscillators Tune state
    return _osc_tune_state;
}

//----------------------------------------------------------------------------
// osc_ui_state
//----------------------------------------------------------------------------
std::string utils::osc_ui_state()
{
    // Return the Oscillators UI state
    return (_osc_state == OscState::OSC_1_OSC_2_STATE) ?
                MAKE_UI_STATE(OSC_1_OSC_2_UI_STATE) :
                MAKE_UI_STATE(OSC_3_OSC_4_UI_STATE);
}

//----------------------------------------------------------------------------
// osc_tune_state
//----------------------------------------------------------------------------
std::string utils::osc_tune_ui_state()
{
    // Return the Oscillators Tune UI state
    if (_osc_tune_state == OscTuneState::OSC_TUNE_FINE) {
        return (_osc_state == OscState::OSC_1_OSC_2_STATE) ?
                    MAKE_UI_STATE(OSC_1_OSC_2_TUNE_FINE_UI_STATE) :
                    MAKE_UI_STATE(OSC_3_TUNE_FINE_UI_STATE);
    }
    return (_osc_state == OscState::OSC_1_OSC_2_STATE) ?
                MAKE_UI_STATE(OSC_1_OSC_2_TUNE_COARSE_UI_STATE) :
                MAKE_UI_STATE(OSC_3_TUNE_COARSE_UI_STATE);
}

//----------------------------------------------------------------------------
// set_osc_state
//----------------------------------------------------------------------------
void utils::set_osc_state(utils::OscState state)
{
    // Set the Oscillators state
    _osc_state = state;
}

//----------------------------------------------------------------------------
// set_osc_tune_state
//----------------------------------------------------------------------------
void utils::set_osc_tune_state(utils::OscTuneState state)
{
    // Set the Oscillators Tune state
    _osc_tune_state = state;
}

//----------------------------------------------------------------------------
// lfo_state
//----------------------------------------------------------------------------
utils::LfoState utils::lfo_state()
{
    // Return the current LFO state
    return _lfo_state;
}

//----------------------------------------------------------------------------
// lfo_rate_state
//----------------------------------------------------------------------------
utils::LfoRateState utils::lfo_rate_state()
{
    // Return the current LFO Rate state
    return _lfo_rate_state;
}

//----------------------------------------------------------------------------
// lfo_ui_state
//----------------------------------------------------------------------------
std::string utils::lfo_ui_state()
{
    // Return the LFO UI state
    if (_lfo_state == LfoState::LFO_1_STATE) {
        return MAKE_UI_STATE(LFO_1_UI_STATE);
    }
    else if (_lfo_state == LfoState::LFO_2_STATE) {
        return MAKE_UI_STATE(LFO_2_UI_STATE);
    }
    return MAKE_UI_STATE(LFO_3_UI_STATE);
}

//----------------------------------------------------------------------------
// lfo_rate_ui_state
//----------------------------------------------------------------------------
std::string utils::lfo_rate_ui_state()
{
    // Return the LFO Rate UI state
    if (_lfo_rate_state == LfoRateState::LFO_NORMAL_RATE) {
        if (_lfo_state == LfoState::LFO_1_STATE) {
            return MAKE_UI_STATE(LFO_1_UI_STATE);
        }
        else if (_lfo_state == LfoState::LFO_2_STATE) {
            return MAKE_UI_STATE(LFO_2_UI_STATE);
        }
        return MAKE_UI_STATE(LFO_3_UI_STATE);
    }
    else {
        if (_lfo_state == LfoState::LFO_1_STATE) {
            return MAKE_UI_STATE(LFO_1_SYNC_RATE_UI_STATE);
        }
        else if (_lfo_state == LfoState::LFO_2_STATE) {
            return MAKE_UI_STATE(LFO_2_SYNC_RATE_UI_STATE);
        }
        return MAKE_UI_STATE(LFO_3_SYNC_RATE_UI_STATE);
    }
}

//----------------------------------------------------------------------------
// set_lfo_state
//----------------------------------------------------------------------------
void utils::set_lfo_state(utils::LfoState state)
{
    // Set the LFO state
    _lfo_state = state;
}

//----------------------------------------------------------------------------
// set_lfo_rate_state
//----------------------------------------------------------------------------
void utils::set_lfo_rate_state(utils::LfoRateState state)
{
    // Set the LFO Rate state
    _lfo_rate_state = state;
}

//----------------------------------------------------------------------------
// res_state
//----------------------------------------------------------------------------
utils::ResState utils::res_state()
{
    // Return the current Resonance state
    return _res_state;
}

//----------------------------------------------------------------------------
// res_ui_state
//----------------------------------------------------------------------------
std::string utils::res_ui_state()
{
    // Return the Resonance UI state
    return (_res_state == ResState::RES_LP_STATE) ?
                MAKE_UI_STATE(RES_LP_UI_STATE) :
                MAKE_UI_STATE(RES_HP_UI_STATE);
}

//----------------------------------------------------------------------------
// set_res_state
//----------------------------------------------------------------------------
void utils::set_res_state(utils::ResState state)
{
    // Set the Resonance state
    _res_state = state;
}

//----------------------------------------------------------------------------
// eg_state
//----------------------------------------------------------------------------
utils::EgState utils::eg_state()
{
    // Return the current EG state
    return _eg_state;
}

//----------------------------------------------------------------------------
// eg_ui_state
//----------------------------------------------------------------------------
std::string utils::eg_ui_state()
{
    // Return the EG UI state
    if (_eg_state == EgState::VCF_EG_STATE) {
        return MAKE_UI_STATE(VCF_UI_STATE);
    }
    else if (_eg_state == EgState::VCA_EG_STATE) {
        return MAKE_UI_STATE(VCA_UI_STATE);
    }
    return MAKE_UI_STATE(AUX_UI_STATE);
}

//----------------------------------------------------------------------------
// set_eg_state
//----------------------------------------------------------------------------
void utils::set_eg_state(utils::EgState state)
{
    // Set the EG state
    _eg_state = state;
}

//----------------------------------------------------------------------------
// fx_state
//----------------------------------------------------------------------------
utils::FxState utils::fx_state()
{
    // Return the current FX state
    return _fx_state;
}

//----------------------------------------------------------------------------
// fx_ui_state
//----------------------------------------------------------------------------
std::string utils::fx_ui_state()
{
    // Return the FX UI state
    if (_fx_state == FxState::FX_SEND_STATE) {
        return MAKE_UI_STATE(FX_SEND_STATE);
    }
    return MAKE_UI_STATE(FX_PARAM_STATE);
}

//----------------------------------------------------------------------------
// set_fx_state
//----------------------------------------------------------------------------
void utils::set_fx_state(utils::FxState state)
{
    // Set the FX state
    _fx_state = state;
}

//----------------------------------------------------------------------------
// tempo_glide_state
//----------------------------------------------------------------------------
utils::TempoGlideState utils::tempo_glide_state()
{
    // Return the current Tempo/Glide state
    return _tempo_glide_state;
}

//----------------------------------------------------------------------------
// tempo_glide_ui_state
//----------------------------------------------------------------------------
std::string utils::tempo_glide_ui_state()
{
    // Return the Tempo/Glide UI state
    if (_tempo_glide_state == TempoGlideState::TEMPO_STATE) {
        return MAKE_UI_STATE(TEMPO_UI_STATE);
    }
    return MAKE_UI_STATE(GLIDE_UI_STATE);
}

//----------------------------------------------------------------------------
// set_tempo_glide_state
//----------------------------------------------------------------------------
void utils::set_tempo_glide_state(utils::TempoGlideState state)
{
    // Set the Tempo/Glide state
    _tempo_glide_state = state;
}

//----------------------------------------------------------------------------
// mod_state
//----------------------------------------------------------------------------
Monique::ModMatrixSrc utils::mod_state()
{
    // Return the Mod state
    return _mod_state;
}

//----------------------------------------------------------------------------
// mod_ui_state
//----------------------------------------------------------------------------
std::string utils::mod_ui_state()
{
    // Return the Mod UI state
    return MAKE_UI_STATE(_mod_state_prefix[(uint)_mod_state]);
}

//----------------------------------------------------------------------------
// mod_osc_ui_state
//----------------------------------------------------------------------------
std::string utils::mod_osc_ui_state()
{
    // Assemble and return the mod matrix state name
    return _mod_state_prefix[(uint)_mod_state] + osc_ui_state();
}

//----------------------------------------------------------------------------
// mod_lfo_ui_state
//----------------------------------------------------------------------------
std::string utils::mod_lfo_ui_state()
{
    // Assemble and return the mod matrix state name
    return _mod_state_prefix[(uint)_mod_state] + lfo_ui_state();    
}

//----------------------------------------------------------------------------
// mod_res_ui_state
//----------------------------------------------------------------------------
std::string utils::mod_res_ui_state()
{
    // Assemble and return the mod matrix state name
    return _mod_state_prefix[(uint)_mod_state] + res_ui_state();    
}

//----------------------------------------------------------------------------
// mod_eg_ui_state
//----------------------------------------------------------------------------
std::string utils::mod_eg_ui_state()
{
    // Assemble and return the mod matrix state name
    return _mod_state_prefix[(uint)_mod_state] + eg_ui_state();    
}

//----------------------------------------------------------------------------
// mod_fx_ui_state
//----------------------------------------------------------------------------
std::string utils::mod_fx_ui_state()
{
    // Assemble and return the mod matrix state name
    return _mod_state_prefix[(uint)_mod_state] + fx_ui_state();    
}

//----------------------------------------------------------------------------
// set_mod_state
//----------------------------------------------------------------------------
void utils::set_mod_state(Monique::ModMatrixSrc state)
{
    // Set the Mod state
    _mod_state = state;
}

//----------------------------------------------------------------------------
// multifn_switches_state
//----------------------------------------------------------------------------
utils::MultifnSwitchesState utils::multifn_switches_state()
{
    // Return the multi-function switches state
    return _multifn_switches_state;
}

//----------------------------------------------------------------------------
// multifn_switches_ui_state
//----------------------------------------------------------------------------
std::string utils::multifn_switches_ui_state()
{
    // Return the multi-function switches UI state
    if (_multifn_switches_state == MultifnSwitchesState::MOD_MATRIX) {
        return MAKE_UI_STATE(MULTIFN_SWITCHES_MOD_STATE);
    }
    else if (_multifn_switches_state == MultifnSwitchesState::SEQ_REC) {
        return MAKE_UI_STATE(MULTIFN_SWITCHES_SEQ_STATE);
    }
    return DEFAULT_UI_STATE;
}

//----------------------------------------------------------------------------
// set_multifn_switches_state
//----------------------------------------------------------------------------
void utils::set_multifn_switches_state(MultifnSwitchesState state)
{
    // Set the multi-function switches state
    _multifn_switches_state = state;
}

//----------------------------------------------------------------------------
// utils::set_controls_state
//----------------------------------------------------------------------------
void utils::set_controls_state(std::string state, EventRouter *event_router)
{
    auto sfc_func = SfcFunc(SfcFuncType::SET_CONTROLS_STATE, MoniqueModule::GUI);
    sfc_func.controls_state = state;
    event_router->post_sfc_func_event(new SfcFuncEvent(sfc_func));
}

//----------------------------------------------------------------------------
// morph_lock
//----------------------------------------------------------------------------
void utils::morph_lock()
{
    // Lock the morph mutex
    _morph_mutex.lock();
}

//----------------------------------------------------------------------------
// morph_unlock
//----------------------------------------------------------------------------
void utils::morph_unlock()
{
    // Unlock the morph mutex
    _morph_mutex.unlock();
}

//----------------------------------------------------------------------------
// get_morph_state
//----------------------------------------------------------------------------
bool utils::get_morph_state()
{
    return _morph_enabled;
}

//----------------------------------------------------------------------------
// set_morph_state
//----------------------------------------------------------------------------
void utils::set_morph_state(bool enabled)
{
    _morph_enabled = enabled;
}

//----------------------------------------------------------------------------
// get_prev_morph_state
//----------------------------------------------------------------------------
bool utils::get_prev_morph_state()
{
    return _prev_morph_enabled;
}

//----------------------------------------------------------------------------
// set_prev_morph_state
//----------------------------------------------------------------------------
void utils::set_prev_morph_state()
{
    _prev_morph_enabled = _morph_enabled;
}

//----------------------------------------------------------------------------
// reset_morph_state
//----------------------------------------------------------------------------
void utils::reset_morph_state()
{
    // Reset the morph state
    _morph_enabled = false;
    _prev_morph_enabled = false;
}

//----------------------------------------------------------------------------
// init_haptic_modes
//----------------------------------------------------------------------------
void utils::init_haptic_modes()
{
    // Nothing specific to do here (yet)
}

//----------------------------------------------------------------------------
// add_haptic_mode
//----------------------------------------------------------------------------
void utils::add_haptic_mode(const sfc::HapticMode& haptic_mode)
{
    // Make sure the mode does not already exist
    for (const sfc::HapticMode &hm : _haptic_modes)
    {
        // Mode already specified?
        if ((hm.name == haptic_mode.name) && (hm.type == haptic_mode.type))
        {
            // Return without adding the passed mode
            return;
        }
    }

    // Add the mode to the hapticl modes vector
    _haptic_modes.push_back(haptic_mode);  
}

//----------------------------------------------------------------------------
// set_default_haptic_mode
//----------------------------------------------------------------------------
bool utils::set_default_haptic_mode(sfc::ControlType control_type,std::string haptic_mode_name)
{
    // Parse the haptic modes
    for (sfc::HapticMode &hm : _haptic_modes)
    {
        // Mode found?
        if ((hm.name == haptic_mode_name) && (hm.type == control_type))
        {
            // Found, indicate this is the default mode and return true
            hm.default_mode = true;
            return true;
        }
    }

    // The default haptic mode was not found
    // In this instance, add a default mode so that MONIQUE can continue
    // operating, but with a warning
    auto haptic_mode = sfc::HapticMode();
    haptic_mode.type = control_type;
    haptic_mode.name = haptic_mode_name;
    haptic_mode.default_mode = true;
    _haptic_modes.push_back(haptic_mode);
    return false;
}

//----------------------------------------------------------------------------
// get_haptic_mode
//----------------------------------------------------------------------------
const sfc::HapticMode& utils::get_haptic_mode(sfc::ControlType control_type, std::string haptic_mode_name)
{
    const sfc::HapticMode *default_mode = nullptr;

    // Parse the haptic modes
    for (const sfc::HapticMode &hm : _haptic_modes)
    {
        // Mode found?
        if ((hm.name == haptic_mode_name) && (hm.type == control_type))
        {
            // Return the mode
            return hm;
        }

        // Is this the default haptic mode? Save this in case we need
        // to return it if the mode cannot be found
        if (hm.default_mode && (hm.type == control_type))
            default_mode = &hm;
    }

    // The mode could not be found, return the default mode
    // Note: A default haptic mode *always* exists
    return *default_mode;
}

//----------------------------------------------------------------------------
// seq_mutex
//----------------------------------------------------------------------------
std::mutex& utils::seq_mutex()
{
    // Return the mutex
    return _seq_mutex;
}

//----------------------------------------------------------------------------
// seq_signal
//----------------------------------------------------------------------------
void utils::seq_signal()
{
    // Signal the Sequencer
    {
        std::lock_guard lk(_seq_mutex);
        _seq_signalled = true;
    }    
    _seq_cv.notify_all();
}

//----------------------------------------------------------------------------
// seq_signal_without_lock
//----------------------------------------------------------------------------
void utils::seq_signal_without_lock()
{
    // Signal the Sequencer without a lock - assumes the lock has
    // already been aquired
    _seq_signalled = true;  
    _seq_cv.notify_all();
}

//----------------------------------------------------------------------------
// seq_wait
//----------------------------------------------------------------------------
void utils::seq_wait(std::unique_lock<std::mutex>& lk)
{
    // Wait for the Sequencer to be signalled
    _seq_cv.wait(lk, []{return _seq_signalled;});
    _seq_signalled = false;
}

//----------------------------------------------------------------------------
// seq_chunk_param_reset_value
//----------------------------------------------------------------------------
const char *utils::seq_chunk_param_reset_value()
{
    return SEQ_CHUNK_PARAM_RESET_VALUE;
}

//----------------------------------------------------------------------------
// arp_mutex
//----------------------------------------------------------------------------
std::mutex& utils::arp_mutex()
{
    // Return the mutex
    return _arp_mutex;
}

//----------------------------------------------------------------------------
// arp_signal
//----------------------------------------------------------------------------
void utils::arp_signal()
{
    // Signal the Arpeggiator with a lock
    {
        std::lock_guard lk(_arp_mutex);
        _arp_signalled = true;
    }    
    _arp_cv.notify_all();
}

//----------------------------------------------------------------------------
// arp_signal_without_lock
//----------------------------------------------------------------------------
void utils::arp_signal_without_lock()
{
    // Signal the Arpeggiator without a lock - assumes the lock has
    // already been aquired
    _arp_signalled = true;  
    _arp_cv.notify_all();
}

//----------------------------------------------------------------------------
// arp_wait
//----------------------------------------------------------------------------
void utils::arp_wait(std::unique_lock<std::mutex>& lk)
{
    // Wait for the Arpeggiator to be signalled
    _arp_cv.wait(lk, []{return _arp_signalled;});
    _arp_signalled = false;
}

//----------------------------------------------------------------------------
// get_tempo_param
//----------------------------------------------------------------------------
Param *utils::get_tempo_param()
{
    // Return the tempo param
    return get_param(MoniqueModule::SYSTEM, SystemParamId::TEMPO_BPM_PARAM_ID);
}

//----------------------------------------------------------------------------
// tempo_pulse_count
//----------------------------------------------------------------------------
uint utils::tempo_pulse_count(common::TempoNoteValue note_value) {
    // Parse the note value
    switch (note_value)
    {
        case common::TempoNoteValue::QUARTER:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 1;

        case common::TempoNoteValue::EIGHTH:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 2;

        case common::TempoNoteValue::SIXTEENTH:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 4;

        case common::TempoNoteValue::THIRTYSECOND:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 8;

        case common::TempoNoteValue::QUARTER_TRIPLETS:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 1.5;

        case common::TempoNoteValue::EIGHTH_TRIPLETS:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 3;

        case common::TempoNoteValue::SIXTEENTH_TRIPLETS:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 6;

        case common::TempoNoteValue::THIRTYSECOND_TRIPLETS:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 12;

        default:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 1;
    }
}

//----------------------------------------------------------------------------
// tempo_note_value
//----------------------------------------------------------------------------
common::TempoNoteValue utils::tempo_note_value(int value)
{
    // Make sure the value is valid
    if (value > common::TempoNoteValue::THIRTYSECOND_TRIPLETS)
        value = common::TempoNoteValue::THIRTYSECOND_TRIPLETS;
    return common::TempoNoteValue(value);
}

//----------------------------------------------------------------------------
// _reset_multifn_switches
//----------------------------------------------------------------------------
void utils::reset_multifn_switches(EventRouter *event_router)
{
    // Reset the multi-function switches to their default state
    utils::set_multifn_switches_state(MultifnSwitchesState::DEFAULT);
    set_controls_state(utils::multifn_switches_ui_state(), event_router);
}

//----------------------------------------------------------------------------
// _config_multifn_switches
//----------------------------------------------------------------------------
void utils::config_multifn_switches(int selected, utils::MultifnSwitchesState state, EventRouter *event_router)
{
    // Set the multi-function switches to the specified state, and reset all switches
    utils::set_multifn_switches_state(state);
    set_controls_state(utils::multifn_switches_ui_state(), event_router);
    auto sfc_func = SfcFunc(SfcFuncType::RESET_MULTIFN_SWITCHES, MoniqueModule::SYSTEM);
    event_router->post_sfc_func_event(new SfcFuncEvent(sfc_func));
    if (selected != -1) {
        select_multifn_switch(selected, event_router);
    }
}

//----------------------------------------------------------------------------
// _select_multifn_switch
//----------------------------------------------------------------------------
void utils::select_multifn_switch(uint index, EventRouter *event_router, bool reset_other_switches)
{
    // If the index is within range
    if (index < NUM_MOD_SRC_SWITCHES) {
        // Select the specified multi-function key
        auto sfc_func = SfcFunc(SfcFuncType::SET_MULTIFN_SWITCH, MoniqueModule::SYSTEM);
        sfc_func.num = index;
        sfc_func.reset_associated_switches = reset_other_switches;
        event_router->post_sfc_func_event(new SfcFuncEvent(sfc_func));
    }
    else {
        // Clear all the multi-function keys
        auto sfc_func = SfcFunc(SfcFuncType::RESET_MULTIFN_SWITCHES, MoniqueModule::SYSTEM);
        event_router->post_sfc_func_event(new SfcFuncEvent(sfc_func));         
    }    
}

//----------------------------------------------------------------------------
// register_manager
//----------------------------------------------------------------------------
void utils::register_manager(MoniqueModule module, BaseManager *mgr)
{
    // Parse the MONIQUE module
    switch (module) {
        case MoniqueModule::DAW:
            _daw_mgr = mgr;
            break;

        case MoniqueModule::MIDI_DEVICE:
            _midi_device_mgr = mgr;
            break;

        case MoniqueModule::SEQ:
            _seq_mgr = mgr;
            break;

        case MoniqueModule::ARP:
            _arp_mgr = mgr;
            break;

        case MoniqueModule::SOFTWARE:
            _sw_mgr = mgr;
            break;

        default:
            break;
    }
}

//----------------------------------------------------------------------------
// get_manager
//----------------------------------------------------------------------------
BaseManager *utils::get_manager(MoniqueModule module)
{
    // Parse the MONIQUE module
    switch (module) {
        case MoniqueModule::DAW:
            return _daw_mgr;

        case MoniqueModule::MIDI_DEVICE:
            return _midi_device_mgr;

        case MoniqueModule::SEQ:
            return _seq_mgr;

        case MoniqueModule::ARP:
            return _arp_mgr;

        case MoniqueModule::SOFTWARE:
            return _sw_mgr;

        default:
            return nullptr;
    }
}


//----------------------------------------------------------------------------
// kbd_octave_mapped_params
//----------------------------------------------------------------------------
std::pair<SwitchParam *, SwitchParam *> utils::kbd_octave_mapped_params()
{
    std::pair<SwitchParam *, SwitchParam *> params(nullptr, nullptr);

    // Get the Octave dec and inc mapped params
    auto dec_param = get_sys_func_param(SystemFuncType::OCTAVE_DEC);
    auto inc_param = get_sys_func_param(SystemFuncType::OCTAVE_INC);
    if (dec_param && inc_param && (dec_param->mapped_params(nullptr).size() == 1) && (inc_param->mapped_params(nullptr).size() == 1)) {
        // Check the mapped params are switches
        auto dec_mp = dec_param->mapped_params(nullptr).front();
        auto inc_mp = inc_param->mapped_params(nullptr).front();
        if ((dec_mp->module() == MoniqueModule::SFC_CONTROL) && (inc_mp->module() == MoniqueModule::SFC_CONTROL) &&
            (static_cast<SfcControlParam *>(dec_mp)->control_type() == sfc::ControlType::SWITCH) &&
            (static_cast<SfcControlParam *>(inc_mp)->control_type() == sfc::ControlType::SWITCH)) {
            // Get the (single) mapped param for each
            params.first = static_cast<SwitchParam *>(dec_mp);
            params.second = static_cast<SwitchParam *>(inc_mp);
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// kbd_octave_led_states
//----------------------------------------------------------------------------
std::pair<SwitchValue, SwitchValue> utils::kbd_octave_led_states(int octave_offset)
{
    std::pair<SwitchValue, SwitchValue> led_states(SwitchValue::OFF, SwitchValue::OFF);

    // Work out the switch LED states
    switch (octave_offset) {
        case Monique::OCTAVE_OFFSET_MIN:
            led_states.first = SwitchValue::ON_TRI;
            break;

        case Monique::OCTAVE_OFFSET_MIN + 1:
            led_states.first = SwitchValue::ON;
            break;

        case Monique::OCTAVE_OFFSET_MIN + 3:
            led_states.second = SwitchValue::ON;
            break;

        case Monique::OCTAVE_OFFSET_MIN + 4:
            led_states.second = SwitchValue::ON_TRI;
            break;

        default:
            // Both OFF
            break;
    }
    return led_states;
}

//----------------------------------------------------------------------------
// set_at_sensitivity
//----------------------------------------------------------------------------
void utils::set_at_sensitivity(const Param *at_sensitivity_param)
{
    // The mid-point for AT sensitivity is at 1.0, or enum value 5
    // Use linear interpolation to calculate the value before or after this mid-point
    uint at_scaling;
    if (at_sensitivity_param->position_value() < 6) {
        // Interpolate between the min and mid values
        auto inc = std::abs(AT_SENSITIVITY_MIN - AT_SENSITIVITY_MID) / 5;
        at_scaling = AT_SENSITIVITY_MIN - (inc * at_sensitivity_param->position_value());
    }
    else {
        // Interpolate between the mid and max values
        auto inc = std::abs(AT_SENSITIVITY_MID - AT_SENSITIVITY_MAX) / 9;
        at_scaling = AT_SENSITIVITY_MID - (inc * (at_sensitivity_param->position_value() - 5));                                    
    }

    // Set the AT scaling
    sfc::set_at_scaling(at_scaling);
}

//----------------------------------------------------------------------------
// _get_param
// Note: Private function
//----------------------------------------------------------------------------
Param *_get_param(std::string path)
{
    // Parse the global params
    for (const std::unique_ptr<Param> &p : _global_params) {
        // Does the parameter path match?
        if (p->cmp_path(path)) {
            // Param found, return it
            return (p.get());
        }
    }

    // Parse the Layer params
    for (const std::unique_ptr<Param> &p : _layer_params) {
        // Does the parameter path match?
        if (p->cmp_path(path)) {
            // Param found, return it
            return (p.get());
        }
    }    
    return nullptr;  
}

//----------------------------------------------------------------------------
// _get_param
// Note: Private function
//----------------------------------------------------------------------------
Param *_get_param(std::string path, std::vector<std::unique_ptr<Param>>& params)
{
    // Search the passed params
    for (std::unique_ptr<Param> &p : params)
    {
        // Does the parameter path match?
        if (p->cmp_path(path)) {
            return p.get();
        }
    }
    return nullptr;
}
