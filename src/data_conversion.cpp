/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  data_conversion.cpp
 * @brief Data conversion implementation.
 *-----------------------------------------------------------------------------
 */

#include "data_conversion.h"
#include "monique_synth_parameters.h"
#include "arp_manager.h"
#include "seq_manager.h"
#include "pedals_manager.h"

namespace dataconv
{
    constexpr float LOG2_20 = std::log2f(20);
    constexpr float LOG2_12 = std::log2f(12);

    // Convert TO a normalised float
    float to_normalised_float(MoniqueModule module, int param_id, float value) {
        // Conversion based on the module
        switch (module) {
            case MoniqueModule::DAW:
                // Call the MONIQUE synth params conversion function
                return Monique::to_normalised_float(param_id, value);

            case MoniqueModule::SYSTEM: {
                switch (param_id) {
                    case SystemParamId::TEMPO_BPM_PARAM_ID: {
                        // Convert the tempo to normalised float
                        auto tempo = std::clamp(value, common::MIN_TEMPO_BPM, common::MAX_TEMPO_BPM);
                        return -0.1059 + (0.005330 * tempo) - (0.00000167 * tempo * tempo) - (0.00000000557 * tempo * tempo * tempo);
                    }

                    case SystemParamId::SEQ_ARP_MIDI_CHANNEL_PARAM_ID:
                    case SystemParamId::KBD_MIDI_CHANNEL_PARAM_ID: {
                        // Convert the MIDI channel to normalised float
                        auto val = std::clamp(value, 0.0f, 16.0f);
                        return val / 17;
                    }

                    case SystemParamId::SUSTAIN_POLARITY_PARAM_ID: {
                        // Convert the Sustain Polarity to a normalised float
                        auto val = std::clamp(value, static_cast<float>(SustainPolarity::POSITIVE), static_cast<float>(SustainPolarity::NEGATIVE));
                        return val / SustainPolarity::NUM_SUSTAIN_POLARITIES;
                    }

                    case SystemParamId::MIDI_ECHO_FILTER_PARAM_ID: {
                        // Convert the MIDI echo filter to a normalised float
                        auto val = std::clamp(value, static_cast<float>(MidiEchoFilter::NO_FILTER), static_cast<float>(MidiEchoFilter::FILTER_ALL));
                        return val / MidiEchoFilter::NUM_ECHO_FILTERS;                        
                    }

                    case SystemParamId::AT_SENSITIVITY_PARAM_ID: {
                        // Convert the AT Sensitivity to a normalised float
                        auto val = std::clamp(value, 0.0f, 15.0f);
                        return val / 16.0f;
                    }

                    default:
                        break;
                }
                break;
            }

            case MoniqueModule::SEQ: {
                switch(param_id) {
                    case SeqParamId::MODE_PARAM_ID: {
                        // Convert the SEQ mode to a normalised float
                        auto val = std::clamp(value, static_cast<float>(SeqMode::STEP), static_cast<float>(SeqMode::PHRASE_LOOPER));
                        return val / (float)SeqMode::NUM_MODES;                        
                    }
            
                    case SeqParamId::NUM_STEPS_PARAM_ID: {
                        // Convert the SEQ number of steps to a normalised float
                        auto val = std::clamp(value, 1.0f, (float)STEP_SEQ_MAX_STEPS);
                        return (val - 1.0f) / STEP_SEQ_MAX_STEPS;                        
                    }

                    case SeqParamId::TEMPO_NOTE_VALUE_PARAM_ID: {
                        // Convert the Tempo Note value to a normalised float
                        auto val = std::clamp(value, static_cast<float>(common::TempoNoteValue::QUARTER), static_cast<float>(common::TempoNoteValue::THIRTYSECOND_TRIPLETS));
                        return val / common::TempoNoteValue::NUM_TEMPO_NOTE_VALUES;
                    }

                    case SeqParamId::PHRASE_QUANTISATION_PARAM_ID: {
                        // Convert the Phrase Quantisation value to a normalised float
                        auto val = std::clamp(value, static_cast<float>(PhraseQuantisation::NONE), static_cast<float>(PhraseQuantisation::THIRTYSECOND_TRIPLETS));
                        return val / PhraseQuantisation::NUM_PHRASE_QUANTISATION_VALUES;
                    }

                    case SeqParamId::PHRASE_BEATS_PER_BAR_PARAM_ID: {
                        // Convert the Phrase Beats per Bar to a normalised float
                        auto val = std::clamp(value, static_cast<float>(PhraseBeatsPerBar::NONE), static_cast<float>(PhraseBeatsPerBar::FIVE));
                        return val / (float)PhraseBeatsPerBar::NUM_BEATS_PER_BAR_VALUES;                         
                    }

                    default:
                        break;
                }
                break;
            }

            case MoniqueModule::ARP: {
                switch(param_id) {
                    case ArpParamId::ARP_DIR_MODE_PARAM_ID: {
                        // Convert the DIR mode to a normalised float
                        auto val = std::clamp(value, static_cast<float>(ArpDirMode::UP), static_cast<float>(ArpDirMode::ASSIGNED));
                        return val / ArpDirMode::NUM_DIR_MODES;
                    }

                    case ArpParamId::ARP_TEMPO_NOTE_VALUE_PARAM_ID: {
                        // Convert the Tempo Note value to a normalised float
                        auto val = std::clamp(value, static_cast<float>(common::TempoNoteValue::QUARTER), static_cast<float>(common::TempoNoteValue::THIRTYSECOND_TRIPLETS));
                        return val / common::TempoNoteValue::NUM_TEMPO_NOTE_VALUES;
                    }                    

                    default:
                        break;
                }
                break;
            }

            default:
                break;
        }
        return value;
    }

    // Special cases - Pitch Bend, Aftertouch, and MIDI CC
    float pitch_bend_to_normalised_float(float value) {
        auto val = std::clamp(value, (float)MIDI_PITCH_BEND_MIN_VALUE, (float)MIDI_PITCH_BEND_MAX_VALUE);
        return (val - MIDI_PITCH_BEND_MIN_VALUE) / (MIDI_PITCH_BEND_MAX_VALUE - MIDI_PITCH_BEND_MIN_VALUE);        
    }

    float aftertouch_to_normalised_float(float value) {
        // Note: Chanpress == Aftertouch
        auto val = std::clamp(value, (float)MIDI_CHANPRESS_MIN_VALUE, (float)MIDI_CHANPRESS_MAX_VALUE);
        return (val - MIDI_CHANPRESS_MIN_VALUE) / (MIDI_CHANPRESS_MAX_VALUE - MIDI_CHANPRESS_MIN_VALUE);        
    }    

    float midi_cc_to_normalised_float(float value) {
        auto val = std::clamp(value, (float)MIDI_CC_MIN_VALUE, (float)MIDI_CC_MAX_VALUE);
        return (val - MIDI_CC_MIN_VALUE) / (MIDI_CC_MAX_VALUE - MIDI_CC_MIN_VALUE);       
    }

    // Convert FROM a normalised float
    float from_normalised_float(MoniqueModule module, int param_id, float value) {
        // Clamp the passed normalised float to min/max
        value = std::clamp(value, 0.0f, 1.0f);
   
        // Conversion based on the module
        switch (module) {
            case MoniqueModule::DAW:
                // Call the MONIQUE synth params conversion function
                return Monique::from_normalised_float(param_id, value);

            case MoniqueModule::SYSTEM: {
                switch (param_id) {
                    case SystemParamId::TEMPO_BPM_PARAM_ID: {
                        // Convert the normalised float to a value between the min and max bpm
                        // Quantise the value to 0.1 if less than 100 BPM, and 0.5 if above
                        auto val = std::pow(1.9, 3 + (3.106 * value)) + (176.5 * value) + 13.16;
                        return val >= 100.0f ?
                            std::round(val * 2.0f) / 2.0f :
                            std::round(val * 10.0f) / 10.0f;
                    }

                    case SystemParamId::SEQ_ARP_MIDI_CHANNEL_PARAM_ID:
                    case SystemParamId::KBD_MIDI_CHANNEL_PARAM_ID: {
                        // Convert the normalised float to a MIDI channel value
                        auto val = value * 17;
                        return std::clamp(val, 0.0f, 16.0f); 
                    }

                    case SystemParamId::SUSTAIN_POLARITY_PARAM_ID: {
                        // Convert the normalised float to a Sustain Polarity
                        auto val = value * SustainPolarity::NUM_SUSTAIN_POLARITIES;
                        return std::clamp(val, static_cast<float>(SustainPolarity::POSITIVE), static_cast<float>(SustainPolarity::NEGATIVE));                   
                    }

                    case SystemParamId::MIDI_ECHO_FILTER_PARAM_ID: {
                        // Convert the normalised float to a MIDI Echo Filter value
                        auto val = value * MidiEchoFilter::NUM_ECHO_FILTERS;
                        return std::clamp(val, static_cast<float>(MidiEchoFilter::NO_FILTER), static_cast<float>(MidiEchoFilter::FILTER_ALL));                   
                    }

                    case SystemParamId::AT_SENSITIVITY_PARAM_ID: {
                        // Convert the normalised float to an AT Sensitivity value
                        auto val = value * 16.0f;
                        return std::clamp(val, 0.0f, 15.0f);                   
                    }

                    default:
                        break;
                }
                break;
            }

            case MoniqueModule::SEQ: {
                switch (param_id) {
                    case SeqParamId::MODE_PARAM_ID: {
                        // Convert the normalised float to a Seq mode
                        auto val = value * (float)SeqMode::NUM_MODES;
                        return std::clamp(val, static_cast<float>(SeqMode::STEP), static_cast<float>(SeqMode::PHRASE_LOOPER));                   
                    }

                    case SeqParamId::NUM_STEPS_PARAM_ID: {
                        // Convert the normalised float to Sequencer number of steps
                        auto val = value * STEP_SEQ_MAX_STEPS + 1.0f;
                        return std::clamp(val, 1.0f, (float)STEP_SEQ_MAX_STEPS);                   
                    }

                    case SeqParamId::TEMPO_NOTE_VALUE_PARAM_ID: {
                        // Convert the normalised float to a Tempo note value
                        auto val = value * common::TempoNoteValue::NUM_TEMPO_NOTE_VALUES;
                        return std::clamp(val, static_cast<float>(common::TempoNoteValue::QUARTER), static_cast<float>(common::TempoNoteValue::THIRTYSECOND_TRIPLETS));                   
                    }

                    case SeqParamId::PHRASE_QUANTISATION_PARAM_ID: {
                        // Convert the normalised float to a Phrase Quantiastion value
                        auto val = value * PhraseQuantisation::NUM_PHRASE_QUANTISATION_VALUES;
                        return std::clamp(val, static_cast<float>(PhraseQuantisation::NONE), static_cast<float>(PhraseQuantisation::THIRTYSECOND_TRIPLETS));                   
                    }

                    case SeqParamId::PHRASE_BEATS_PER_BAR_PARAM_ID: {
                        // Convert the normalised float to a Phrase Beats per Bar value
                        auto val = value * (float)PhraseBeatsPerBar::NUM_BEATS_PER_BAR_VALUES;
                        return std::clamp(val, static_cast<float>(PhraseBeatsPerBar::NONE), static_cast<float>(PhraseBeatsPerBar::FIVE));                         
                    }                                  
                }
                break;
            }

            case MoniqueModule::ARP: {
                switch (param_id) {
                    case ArpParamId::ARP_DIR_MODE_PARAM_ID: {
                        // Convert the normalised float to a DIR mode
                        auto val = value * ArpDirMode::NUM_DIR_MODES;
                        return std::clamp(val, static_cast<float>(ArpDirMode::UP), static_cast<float>(ArpDirMode::ASSIGNED));                   
                    }

                    case ArpParamId::ARP_TEMPO_NOTE_VALUE_PARAM_ID: {
                        // Convert the normalised float to a Tempo note value
                        auto val = value * common::TempoNoteValue::NUM_TEMPO_NOTE_VALUES;
                        return std::clamp(val, static_cast<float>(common::TempoNoteValue::QUARTER), static_cast<float>(common::TempoNoteValue::THIRTYSECOND_TRIPLETS));                   
                    }                    
                }
                break;
            }
        
            default:
                break;
        }
        return value; 
    }

    // Special cases - Pitch Bend, Aftertouch, and MIDI CC
    float pitch_bend_from_normalised_float(float value) {
        auto val = (value * (MIDI_PITCH_BEND_MAX_VALUE - MIDI_PITCH_BEND_MIN_VALUE)) + MIDI_PITCH_BEND_MIN_VALUE;
        return std::clamp(val, (float)MIDI_PITCH_BEND_MIN_VALUE, (float)MIDI_PITCH_BEND_MAX_VALUE);     
    }

    float aftertouch_from_normalised_float(float value) {
        // Note: Chanpress == Aftertouch
        auto val = (value * (MIDI_CHANPRESS_MAX_VALUE - MIDI_CHANPRESS_MIN_VALUE)) + MIDI_CHANPRESS_MIN_VALUE;
        return std::clamp(val, (float)MIDI_CHANPRESS_MIN_VALUE, (float)MIDI_CHANPRESS_MAX_VALUE);     
    }

    float midi_cc_from_normalised_float(float value) {
        auto val = (value * (MIDI_CC_MAX_VALUE - MIDI_CC_MIN_VALUE)) + MIDI_CC_MIN_VALUE;
        return std::clamp(val, (float)MIDI_CC_MIN_VALUE, (float)MIDI_CC_MAX_VALUE);   
    }    
};
