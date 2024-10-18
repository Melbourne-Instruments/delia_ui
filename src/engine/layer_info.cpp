/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  layer_info.cpp
 * @brief Layer Info implementation.
 *-----------------------------------------------------------------------------
 */
#include "layer_info.h"

//----------------------------------------------------------------------------
// LayerInfo
//----------------------------------------------------------------------------
LayerInfo::LayerInfo(LayerId layer_id) : _layer_type(layer_id)
{
    // Initialise member data
    _layer_state = LayerState::STATE_A;
    _patch_name = "";
    _midi_channel_filter = 0;
}

//----------------------------------------------------------------------------
// ~LayerInfo
//----------------------------------------------------------------------------
LayerInfo::~LayerInfo()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// layer_id
//----------------------------------------------------------------------------
LayerId LayerInfo::layer_id() const
{ 
    // Return the layer type
    return _layer_type; 
}

//----------------------------------------------------------------------------
// layer_state
//----------------------------------------------------------------------------
LayerState LayerInfo::layer_state() const
{
    // Return the layer state
    return _layer_state;
}

//----------------------------------------------------------------------------
// patch_name
//----------------------------------------------------------------------------
std::string LayerInfo::patch_name() const
{
    // Return the patch name
    return _patch_name;
}

//----------------------------------------------------------------------------
// disabled
//----------------------------------------------------------------------------
bool LayerInfo::disabled() const
{
    // Return if this layer is disabled or not
    return _layer_type == LayerId::D1 && (_num_voices == 0);
}

//----------------------------------------------------------------------------
// num_voices
//----------------------------------------------------------------------------
uint LayerInfo::num_voices() const
{
    // Return the layer number of voices
    return _num_voices;
}

//----------------------------------------------------------------------------
// midi_channel_filter
//----------------------------------------------------------------------------
uint LayerInfo::midi_channel_filter() const
{
    // Return the layer MIDI channel filter
    return _midi_channel_filter;
}

//----------------------------------------------------------------------------
// morph_value
//----------------------------------------------------------------------------
float LayerInfo::morph_value() const
{
    // Return the morph value
    return _morph_value;
}

//----------------------------------------------------------------------------
// set_layer_state
//----------------------------------------------------------------------------
void LayerInfo::set_layer_state(LayerState state)
{
    // Set the layer State
    _layer_state = state;
}

//----------------------------------------------------------------------------
// set_patch_names
//----------------------------------------------------------------------------
void LayerInfo::set_patch_name(const std::string& name)
{
    // Set the patch name
    _patch_name = name;
}

//----------------------------------------------------------------------------
// set_num_voices
//----------------------------------------------------------------------------
void LayerInfo::set_num_voices(uint num_voices)
{
    // Set the layer number of voices
    _num_voices = num_voices;
}

//----------------------------------------------------------------------------
// set_midi_channel_filter
//----------------------------------------------------------------------------
void LayerInfo::set_midi_channel_filter(uint filter)
{
    // Set the layer MIDI channel filter
    _midi_channel_filter = filter;
}

//----------------------------------------------------------------------------
// set_morph_value
//----------------------------------------------------------------------------
void LayerInfo::set_morph_value(float morph_value)
{
    // Set the morph value
    _morph_value = morph_value;
}

//----------------------------------------------------------------------------
// check_midi_channel_filter
//----------------------------------------------------------------------------
bool LayerInfo::check_midi_channel_filter(unsigned char channel)
{
    return (_midi_channel_filter == 0) || (channel == (_midi_channel_filter - 1));
}

//----------------------------------------------------------------------------
// reset
//----------------------------------------------------------------------------
void LayerInfo::reset()
{
    // Reset the Layer
    _layer_state = LayerState::STATE_A;
    _morph_value = 0.0; 
}
