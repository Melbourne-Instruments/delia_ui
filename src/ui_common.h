/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  common.h
 * @brief Common definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _UI_COMMON_H
#define _UI_COMMON_H

#include <sys/types.h>
#include <string>
#include <chrono>
#include <iostream>
#include "common.h"

// MACRO to show a string on the console
#define MSG(str) do { std::cout << str << std::endl; } while( false )

// MACRO to show a debug string on the console
#ifndef NDEBUG
#define DEBUG_MSG(str) MSG(str)
#else
#define DEBUG_MSG(str) do { } while ( false )
#endif

// Constants
constexpr uint NUM_KEYS                                = 49;
constexpr uint NUM_PHYSICAL_SWITCHES                   = 45;
constexpr uint NUM_PHYSICAL_KNOBS                      = 21;
constexpr uint NUM_MOD_SRC_SWITCHES                    = 20;
constexpr uint DEFAULT_BANK_NUM                        = 1;
constexpr uint DEFAULT_MOD_SRC_NUM                     = 1;
constexpr uint NUM_BANKS                               = 127;
constexpr uint NUM_BANK_PRESET_FILES                   = 127;
constexpr uint NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT = 24;
constexpr uint PPQN                                    = 96;
constexpr uint PPQN_CLOCK_PULSES_PER_MIDI_CLOCK        = (PPQN / NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT);
constexpr uint MIDI_CC_MIN_VALUE                       = 0;
constexpr uint MIDI_CC_MAX_VALUE                       = 127;
constexpr int MIDI_PITCH_BEND_MAX_VALUE                = 8191;
constexpr int MIDI_PITCH_BEND_MID_VALUE                = 0;
constexpr int MIDI_PITCH_BEND_MIN_VALUE                = -8192;
constexpr uint MIDI_CHANPRESS_MIN_VALUE                = 0;
constexpr uint MIDI_CHANPRESS_MAX_VALUE                = 127;
constexpr int AT_SENSITIVITY_MIN                       = 92;
constexpr int AT_SENSITIVITY_MAX                       = 32;
constexpr int AT_SENSITIVITY_MID                       = AT_SENSITIVITY_MIN - ((AT_SENSITIVITY_MIN - AT_SENSITIVITY_MAX) / 2);
constexpr char DEFAULT_SYSTEM_COLOUR[]                 = "FFFFFF";
constexpr char ENABLE_PARAM_NAME[]                     = "enable";
constexpr char ENABLE_DISPLAY_NAME[]                   = "Enable";
constexpr char TEMPO_NOTE_VALUE_PARAM_NAME[]           = "tempo_note_value";
constexpr char TEMPO_NOTE_VALUE_DISPLAY_NAME[]         = "Tempo Note Value";
constexpr char HOLD_PARAM_NAME[]                       = "hold";
constexpr char HOLD_DISPLAY_NAME[]                     = "Hold";    
constexpr int US_PER_MINUTE                            = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::minutes(1)).count();
constexpr uint32_t KNOB_HW_VALUE_MAX_VALUE             = 32767;               // 2^15            
constexpr float FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR  = 32767.0f;            // 2^15 - 1
constexpr float KNOB_HW_VALUE_TO_FLOAT_SCALING_FACTOR  = 3.051850948e-05f;    // 1.0 / (2^15 - 1)

// MOIQUE module
enum class MoniqueModule
{
    SYSTEM,
    DAW,
    MIDI_DEVICE,
    SEQ,
    ARP,
    FILE_MANAGER,
    SFC_CONTROL,
    PEDALS,
    GUI,
    SOFTWARE
};

// Layer type
enum LayerId
{
    D0 = 1,
    D1 = 2
};

// Layer State
enum class LayerState
{
    STATE_A,
    STATE_B
};

// System Param IDs
enum SystemParamId
{
    TEMPO_BPM_PARAM_ID,
    MIDI_CLK_IN_PARAM_ID,
    MIDI_ECHO_FILTER_PARAM_ID,
    KBD_MIDI_CHANNEL_PARAM_ID,
    WT_NAME_PARAM_ID,
    SYSTEM_COLOUR_PARAM_ID,
    SEQ_ARP_MIDI_CHANNEL_PARAM_ID,
    PATCH_NAME_PARAM_ID,
    SUSTAIN_POLARITY_PARAM_ID,
    CUTOFF_LINK_PARAM_ID,
    AT_SENSITIVITY_PARAM_ID
};

// MIDI Echo Filter
enum MidiEchoFilter
{
    NO_FILTER,
    ECHO_FILTER,
    FILTER_ALL,
    NUM_ECHO_FILTERS
};

// Sustain Polarity
enum SustainPolarity
{
    POSITIVE,
    NEGATIVE,
    NUM_SUSTAIN_POLARITIES
};

// Options for running calibration script
enum class CalMode
{
    MIX_VCA,
    FILTER
};

// System Colour
struct SystemColour
{
    std::string name;
    std::string colour;
};

#endif  // _UI_COMMON_H
