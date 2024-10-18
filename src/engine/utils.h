/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  utils.h
 * @brief Utility functions.
 *-----------------------------------------------------------------------------
 */
#ifndef _UTILS_H
#define _UTILS_H

#include <memory>
#include <vector>
#include <mutex>
#include <map>
#include <pthread.h>
#include "monique_synth_parameters.h"
#include "system_config.h"
#include "param.h"
#include "system_func.h"
#include "layer_info.h"
#include "sfc.h"
#include "event_router.h"

namespace utils
{
    enum ParamRef
    {
        SELECTED_LAYER,
        LAYER_1_NUM_VOICES,
        LAYER_2_NUM_VOICES,
        TWELVE_NOTE_MODE,
        STATE,
        MIDI_CHANNEL_FILTER,
        MIDI_LOW_NOTE_FILTER,
        MIDI_HIGH_NOTE_FILTER,
        OCTAVE_OFFSET,
        UNISON_VOICES,
        WT_SELECT,
        OSC_1_FINE_TUNE,
        OSC_1_SHAPE,
        OSC_1_LEVEL,
        OSC_2_FINE_TUNE,
        OSC_2_SHAPE,
        OSC_2_LEVEL,
        OSC_3_FINE_TUNE,
        OSC_3_POS,
        OSC_3_LEVEL,
        OSC_4_NOISE_MODE,
        LFO_1_SHAPE,
        LFO_2_SHAPE,
        LFO_3_SHAPE,
        LFO_1_SLEW,
        LFO_2_SLEW,
        LFO_3_SLEW,        
        LFO_1_TEMPO_SYNC,
        LFO_2_TEMPO_SYNC,
        LFO_3_TEMPO_SYNC,
        VCF_ENV_ATTACK,
        VCA_ENV_ATTACK,
        AUX_ENV_ATTACK,
        VCF_EG_RESET,
        VCA_EG_RESET,
        AUX_EG_RESET,
        VCF_HP_CUTOFF,
        VCF_HP_RESONANCE,
        VCF_LP_RESONANCE,
        VCF_LP_SLOPE,
        FX_1_TYPE,
        FX_2_TYPE,
        FX_MACRO_SELECT,
        FX_MACRO_LEVEL,
        GLIDE_MODE,
        MORPH_VALUE,
        MORPH_MODE,
        MIDI_PITCH_BEND,
        MIDI_MODWHEEL,
        MIDI_AFTERTOUCH,
        MIDI_SUSTAIN,
        MIDI_EXPRESSION,
        MIDI_MOD_SRC_1_SEL,
        MIDI_MOD_SRC_2_SEL,
        MIDI_CC_1_MOD_SOURCE,
        MIDI_CC_2_MOD_SOURCE,
        METRONOME_TRIGGER       
    };

    enum class OscState
    {
        OSC_1_OSC_2_STATE,
        OSC_3_OSC_4_STATE
    };

    enum class OscTuneState
    {
        OSC_TUNE_FINE,
        OSC_TUNE_COARSE
    };

    enum class LfoState
    {
        LFO_1_STATE,
        LFO_2_STATE,
        LFO_3_STATE
    };

    enum class LfoRateState
    {
        LFO_NORMAL_RATE,
        LFO_SYNC_RATE
    };    

    enum class ResState
    {
        RES_LP_STATE,
        RES_HP_STATE
    };

    enum class EgState
    {
        VCF_EG_STATE,
        VCA_EG_STATE,
        AUX_EG_STATE
    };

    enum class FxState
    {
        FX_SEND_STATE,
        FX_PARAM_STATE
    };

    enum class TempoGlideState
    {
        TEMPO_STATE,
        GLIDE_STATE
    };

    enum class MultifnSwitchesState
    {
        DEFAULT,
        SEQ_REC,
        MOD_MATRIX
    };

    // Xenomai real-time utilities
    void init_xenomai();
    int create_rt_task(pthread_t *rt_thread, void *(*start_routine)(void *), void *arg, int sched_policy);
    void stop_rt_task(pthread_t *rt_thread);
    void rt_task_nanosleep(struct timespec *time);

    // System utilities
    SystemConfig *system_config();
    void generate_session_uuid();
    std::string get_session_uuid();
    void set_maintenance_mode(bool on);
    bool maintenance_mode();
    void set_demo_mode(bool on);
    bool demo_mode();
    
    // Preset/Layer utilities
    bool preset_modified();
    void set_preset_modified(bool modified);
    LayerInfo &get_current_layer_info();
    LayerInfo &get_layer_info(LayerId id);
    void set_current_layer(LayerId id);
    bool is_current_layer(LayerId id);
    
    // Param utilities
    std::vector<Param *> get_global_params();
    std::vector<Param *> get_layer_params();
    std::vector<Param *> get_preset_params();
    std::vector<Param *> get_daw_params();
    std::vector<LayerStateParam *> get_mod_matrix_params();
    std::vector<SwitchParam *> get_multifn_switch_params();
    std::vector<Param *> get_params(MoniqueModule module);
    std::vector<Param *> get_params(const std::string param_path_regex);
    std::vector<SfcControlParam *> get_params_with_state(const std::string state);
    std::vector<SfcControlParam *> get_grouped_params(const std::string group_name);
    Param *get_param(std::string path);
    Param *get_param(MoniqueModule module, int param_id);
    Param *get_param(ParamRef ref);
    SystemFuncParam *get_sys_func_param(SystemFuncType sys_func_type);
    KnobParam *get_data_knob_param();
    Param *get_wt_filename_param();
    void set_data_knob_param(KnobParam *param);
    bool param_has_ref(const Param *param, ParamRef ref);
    void blacklist_param(std::string path);
    bool param_is_blacklisted(std::string path);
    void register_param(std::unique_ptr<Param> param);
    void register_system_params();

    // UI States
    std::string default_ui_state();
    OscState osc_state();
    OscTuneState osc_tune_state();
    std::string osc_ui_state();
    std::string osc_tune_ui_state();
    void set_osc_state(OscState state);
    void set_osc_tune_state(OscTuneState state);
    LfoState lfo_state();
    LfoRateState lfo_rate_state();
    std::string lfo_ui_state();
    std::string lfo_rate_ui_state();
    void set_lfo_state(LfoState state);
    void set_lfo_rate_state(LfoRateState state);
    ResState res_state();
    std::string res_ui_state();
    void set_res_state(ResState state);
    EgState eg_state();
    std::string eg_ui_state();
    void set_eg_state(EgState state);
    FxState fx_state();
    std::string fx_ui_state();
    void set_fx_state(FxState state);    
    TempoGlideState tempo_glide_state();   
    std::string tempo_glide_ui_state();
    void set_tempo_glide_state(TempoGlideState state);
    Monique::ModMatrixSrc mod_state();
    std::string mod_ui_state();
    std::string mod_osc_ui_state();
    std::string mod_lfo_ui_state();
    std::string mod_res_ui_state();
    std::string mod_eg_ui_state();
    std::string mod_fx_ui_state();
    void set_mod_state(Monique::ModMatrixSrc state);
    MultifnSwitchesState multifn_switches_state();
    std::string multifn_switches_ui_state();
    void set_multifn_switches_state(MultifnSwitchesState state);
    void set_controls_state(std::string state, EventRouter *event_router);

    // Morph param utilities
    Param *get_morph_value_param();
    KnobParam *get_morph_knob_param();
    Monique::MorphMode morph_mode();
    void set_morph_value_param(Param *param);
    void set_morph_knob_param(KnobParam *param);
    void set_morph_mode_param(Param *param);
    void morph_lock();
    void morph_unlock();
    bool get_morph_state();
    void set_morph_state(bool enabled);
    bool get_prev_morph_state();
    void set_prev_morph_state();
    void reset_morph_state();

    // Haptic utilities
    void init_haptic_modes();
    void add_haptic_mode(const sfc::HapticMode& haptic_mode);
    bool set_default_haptic_mode(sfc::ControlType control_type,std::string haptic_mode_name);
    const sfc::HapticMode& get_haptic_mode(sfc::ControlType control_type, std::string haptic_mode_name);

    // Seq/Arp utilties
    std::mutex& seq_mutex();
    void seq_signal();
    void seq_signal_without_lock();
    void seq_wait(std::unique_lock<std::mutex>& lk);
    const char *seq_chunk_param_reset_value();
    std::mutex& arp_mutex();
    void arp_signal();
    void arp_signal_without_lock();
    void arp_wait(std::unique_lock<std::mutex>& lk);
    Param *get_tempo_param();
    uint tempo_pulse_count(common::TempoNoteValue note_value);
    common::TempoNoteValue tempo_note_value(int value);

    // Multi-function switch utilties
    void reset_multifn_switches(EventRouter *event_router);
    void config_multifn_switches(int selected, utils::MultifnSwitchesState state, EventRouter *event_router);
    void select_multifn_switch(uint index, EventRouter *event_router, bool reset_other_switches=true);    

    // Manager utilties
    void register_manager(MoniqueModule module, BaseManager *mgr);
    BaseManager *get_manager(MoniqueModule module);

    // KDB Octave inc/dec utilities
    std::pair<SwitchParam *, SwitchParam *> kbd_octave_mapped_params();
    std::pair<SwitchValue, SwitchValue> kbd_octave_led_states(int octave_offset);

    // Aftertouch utilities
    void set_at_sensitivity(const Param *at_sensitivity_param);
}

#endif  // _UTILS_H
