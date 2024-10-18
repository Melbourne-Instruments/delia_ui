/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  sfc_manager.h
 * @brief Surface Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _SFC_MANAGER
#define _SFC_MANAGER

#include <iostream>
#include "base_manager.h"
#include "event.h"
#include "sfc.h"
#include "daw_manager.h"
#include <iostream>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <vector>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include "param.h"
#include "timer.h"

// Knob control
struct KnobControl
{
    // Public variables
    uint num;
    uint16_t position;
    int16_t position_delta;
    bool use_large_movement_threshold;
    uint poll_skip_count;
    uint small_movement_threshold_count;
    uint polls_since_last_threshold_hit;
    bool moving_to_target;
    std::chrono::_V2::steady_clock::time_point large_movement_time_start;
    bool moving_to_large_threshold;
};

// Switch control
struct SwitchControl
{
    // Public variables
    uint num;
    uint logical_state;
    bool physical_state;
    std::chrono::_V2::steady_clock::time_point push_time_start;
    Timer *led_pulse_timer;
    bool led_state;
    bool latched;
    bool push_time_processed;
    bool grouped_push;
    bool processed;
};

// Surface Manager class
class SfcManager: public BaseManager
{
public:
    // Constructor
    SfcManager(EventRouter *event_router);

    // Destructor
    ~SfcManager();

    // Public functions
    bool start();
    void stop();
    void process();
    void process_event(const BaseEvent *event);
    void process_sfc_control();

private:
    // Private variables
    EventListener *_param_changed_listener;
    EventListener *_sfc_func_listener;
    EventListener *_reload_presets_listener;
    SwitchControl _switch_controls[NUM_PHYSICAL_SWITCHES];
#ifndef NO_XENOMAI
    pthread_t _sfc_control_thread;
#else
    std::thread* _sfc_control_thread;
#endif
    std::mutex _sfc_mutex;
    KnobControl _knob_controls[NUM_PHYSICAL_KNOBS];
    std::atomic<bool> _exit_sfc_control_thread;
    bool _sfc_control_init;
    bool _preset_reloaded;

    // Private functions
    void _process_param_changed_event(const ParamChange &param_change);
    void _process_reload_presets(bool from_layer_toggle);
    void _process_sfc_func(const SfcFunc &sfc_func);
    void _set_knob_control_position_from_preset(const KnobParam *param);
    void _set_switch_control_value_from_preset(const SwitchParam *param);
    void _process_sfc_param_changed(Param *param);
    void _process_param_changed_mapped_params(const Param *param, float diff, const Param *skip_param, bool displayed, bool force=false);
    void _send_knob_control_param_change_events(const Param *param, float diff);
    void _send_control_param_change_events(const Param *param, bool force=false);
    void _set_knob_control_position(const KnobParam *param, bool robust=true);
    void _set_switch_control_value(const SwitchParam *param);
    void _set_knob_control_haptic_mode(const KnobParam *param);
    void _process_knob_control(uint num, const sfc::KnobState &knob_state, bool morphing);
    void _process_switch_control(uint num, const bool *physical_states, bool morphing);
    void _process_physical_knob(KnobParam *param, KnobControl &knob_control, const sfc::KnobState &knob_state);
    void _process_physical_switch(SwitchParam *param, const bool *physical_states);
    void _morph_control(sfc::ControlType type, uint num);
    void _set_switch_led_state(const SwitchParam *param);
    void _set_switch_led_state(const SwitchParam *param, SwitchValue led_state);
    void _commit_led_control_states();
    void _switch_led_pulse_timer_callback(SwitchControl *switch_control);
    void _register_params();
};

#endif  //_SFC_MANAGER
