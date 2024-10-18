/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  sfc.h
 * @brief Surface driver class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _SFC_H
#define _SFC_H

#include <mutex>
#include <vector>
#include <fstream>
#include "ui_common.h"

namespace sfc
{
    // Control types
    enum class ControlType
    {
        KNOB,
        SWITCH,
        UNKNOWN
    };

    // Switch modes
    enum class SwitchMode
    {
        TOGGLE,
        TOGGLE_LED_PULSE,
        TOGGLE_RELEASE,
        TOGGLE_HOLD,
        TOGGLE_HOLD_INVERTED,
        TOGGLE_TRI_STATE,
        PUSH,
        LATCH_PUSH,
        PUSH_MANUAL_LED,
        PUSH_NO_LED
    };

    // Knob state
    struct KnobState
    {
        // Constants
        static const uint STATE_MOVING_TO_TARGET = 0x02;
        static const uint STATE_TAP_DETECTED = 0x01;
        
        // State and position
        uint16_t state;
        uint16_t position;
    };

    // Surface Control mode
    struct HapticMode
    {
        ControlType type;
        std::string name;
        bool default_mode;

        // Knob related params
        int knob_start_pos;
        uint knob_width;
        float knob_actual_start_pos;
        float knob_actual_width;    
        uint knob_num_detents;
        uint knob_friction;
        uint knob_detent_strength;
        std::vector<std::pair<bool,uint>> knob_indents;

        // Switch related params
        SwitchMode switch_mode;

        // Constructor
        HapticMode()
        {
            type = ControlType::UNKNOWN;
            name = "";
            default_mode = false;
            knob_start_pos = -1;
            knob_width = 360;
            knob_actual_start_pos = -1.0f;
            knob_actual_width = 360.0f;        
            knob_num_detents = 0;
            knob_friction = 0;
            knob_detent_strength = 0;
            knob_indents.clear();
            switch_mode = SwitchMode::PUSH;
        }

        // Public functions
        bool knob_haptics_on() const
        {
            // Indicate whether this mode has haptics switched on for a knob
            return ((knob_width < 360) ||
                    knob_friction ||
                    knob_num_detents || 
                    (knob_indents.size() > 0));
        }
    };

    // Public functions
    int init();
    void reinit();
    void deinit();
    void lock();
    void unlock();
    bool knob_is_active(uint num);
    int request_knob_states();
    int read_knob_states(KnobState *states);
    int read_switch_states(bool *states);
    int set_knob_haptic_mode(unsigned int num, const HapticMode& haptic_mode);
    int set_knob_position(unsigned int num, uint16_t position, bool robust=true);
    int set_switch_led_state(unsigned int num, bool led_on);
    void set_all_switch_led_states(bool leds_on);
    int commit_led_states();
    int set_at_scaling(uint8_t scaling);
    int latch_pitch_wheel_min();
    int latch_pitch_wheel_max();
    int latch_pitch_wheel_mid_1();
    int latch_pitch_wheel_mid_2();    
    int latch_mod_wheel_min();
    int latch_mod_wheel_max();
    int save_pitch_mod_wheel_config();
    ControlType control_type_from_string(const char *type);
}

#endif  // _SFC_H
