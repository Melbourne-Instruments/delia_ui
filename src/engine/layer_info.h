/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  layer_info.h
 * @brief Layer Info class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _LAYER_INFO_H
#define _LAYER_INFO_H

#include <condition_variable>
#include "alsa/asoundlib.h"
#include "param.h"
#include "ui_common.h"

// LayerInfo class
class LayerInfo
{
public:    
    // Constructor
    LayerInfo(LayerId layer_id);

    // Destructor
    virtual ~LayerInfo();

    // Public functions
    LayerId layer_id() const;
    LayerState layer_state() const;
    std::string patch_name() const;
    bool disabled() const;
    uint num_voices() const;
    uint midi_channel_filter() const;
    float morph_value() const;
    void set_layer_state(LayerState state);
    void set_patch_name(const std::string& name);
    void set_num_voices(uint num_voices);
    void set_midi_channel_filter(uint filter);
    void set_morph_value(float morph_value);
    bool check_midi_channel_filter(unsigned char channel);  
    void reset();

private:
    // Private variables
    const LayerId _layer_type;
    LayerState _layer_state;
    std::string _patch_name;
    uint _num_voices;
    uint _midi_channel_filter;
    float _morph_value;
};

#endif  // _LAYER_INFO_H
