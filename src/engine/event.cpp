/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  event.cpp
 * @brief Event implementation.
 *-----------------------------------------------------------------------------
 */

#include "event.h"

//----------------------------------------------------------------------------
// BaseEvent
//----------------------------------------------------------------------------
BaseEvent::BaseEvent(MoniqueModule module, EventType type)
{
    // Initialise class data
    _module = module;
    _type = type;
}

//----------------------------------------------------------------------------
// ~BaseEvent
//----------------------------------------------------------------------------
BaseEvent::~BaseEvent()
{
    // Nothing specific to do - note vitual function
}

//----------------------------------------------------------------------------
// module
//----------------------------------------------------------------------------
MoniqueModule BaseEvent::module() const
{ 
    // Return the module
    return _module; 
}

//----------------------------------------------------------------------------
// type
//----------------------------------------------------------------------------
EventType BaseEvent::type() const
{
    // Return the type
    return _type; 
}

//----------------------------------------------------------------------------
// MidiEvent
//----------------------------------------------------------------------------
MidiEvent::MidiEvent(MoniqueModule module, const snd_seq_event_t &seq_event) :
    BaseEvent(module, EventType::MIDI)
{
    // Initialise class data
    _seq_event = seq_event;
    _layer_id_mask = LayerId::D0 | LayerId::D1;
}

//----------------------------------------------------------------------------
// MidiEvent
//----------------------------------------------------------------------------
MidiEvent::MidiEvent(MoniqueModule module, const snd_seq_event_t &seq_event, uint layer_id_mask) :
    BaseEvent(module, EventType::MIDI)
{
    // Initialise class data
    _seq_event = seq_event;
    _layer_id_mask = layer_id_mask;
}

//----------------------------------------------------------------------------
// ~MidiEvent
//----------------------------------------------------------------------------
MidiEvent::~MidiEvent()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// seq_event
//----------------------------------------------------------------------------
snd_seq_event_t MidiEvent::seq_event() const
{
    // Return the sequencer event
    return _seq_event;
}

//----------------------------------------------------------------------------
// ParamChangedEvent
//----------------------------------------------------------------------------
ParamChangedEvent::ParamChangedEvent(const ParamChange &param_change) :
    BaseEvent(param_change.from_module, EventType::PARAM_CHANGED)
{
    // Initialise class data
    _param_change = param_change;
}

//----------------------------------------------------------------------------
// ~ParamChangedEvent
//----------------------------------------------------------------------------
ParamChangedEvent::~ParamChangedEvent()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// param_change
//----------------------------------------------------------------------------
ParamChange ParamChangedEvent::param_change() const
{
    // Return the param change event
    return _param_change;
}

//----------------------------------------------------------------------------
// SystemFuncEvent
//----------------------------------------------------------------------------
SystemFuncEvent::SystemFuncEvent(const SystemFunc &system_func) :
    BaseEvent(system_func.from_module, EventType::SYSTEM_FUNC)
{
    // Initialise class data
    _system_func = system_func;
}

//----------------------------------------------------------------------------
// ~SystemFuncEvent
//----------------------------------------------------------------------------
SystemFuncEvent::~SystemFuncEvent()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// system_func
//----------------------------------------------------------------------------
SystemFunc SystemFuncEvent::system_func() const
{
    // Return the ssytem function event
    return _system_func;
}

//----------------------------------------------------------------------------
// ReloadPresetsEvent
//----------------------------------------------------------------------------
ReloadPresetsEvent::ReloadPresetsEvent(MoniqueModule module) :
    BaseEvent(module, EventType::RELOAD_PRESETS)
{
    //  Initialise class data
    _from_layer_toggle = false;
    _from_ab_toggle = false;
}

//----------------------------------------------------------------------------
// ~ReloadPresetsEvent
//----------------------------------------------------------------------------
ReloadPresetsEvent::~ReloadPresetsEvent()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// from_layer_toggle
//----------------------------------------------------------------------------
bool ReloadPresetsEvent::from_layer_toggle() const
{
    // Return if this reload was via a Layer toggle
    return _from_layer_toggle;
}

//----------------------------------------------------------------------------
// from_ab_toggle
//----------------------------------------------------------------------------
bool ReloadPresetsEvent::from_ab_toggle() const
{
    // Return if this reload was via an A/B toggle
    return _from_ab_toggle;
}

//----------------------------------------------------------------------------
// set_from_layer_toggle
//----------------------------------------------------------------------------
void ReloadPresetsEvent::set_from_layer_toggle()
{
    // Indicate this reload is from a layer toggle
    _from_layer_toggle = true;
}

//----------------------------------------------------------------------------
// set_from_ab_toggle
//----------------------------------------------------------------------------
void ReloadPresetsEvent::set_from_ab_toggle()
{
    // Indicate this reload is from an A/B toggle
    _from_ab_toggle = true;
}

//----------------------------------------------------------------------------
// SfcFuncEvent
//----------------------------------------------------------------------------
SfcFuncEvent::SfcFuncEvent(const SfcFunc &sfc_func) :
    BaseEvent(sfc_func.from_module, EventType::SFC_FUNC)
{
    // Initialise class data
    _sfc_func = sfc_func;
}

//----------------------------------------------------------------------------
// ~SfcFuncEvent
//----------------------------------------------------------------------------
SfcFuncEvent::~SfcFuncEvent()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// sfc_func
//----------------------------------------------------------------------------
SfcFunc SfcFuncEvent::sfc_func() const
{
    // Return the Surface Control function
    return _sfc_func;
}