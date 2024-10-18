/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  event.h
 * @brief Event definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _EVENT_H
#define _EVENT_H

#include "alsa/asoundlib.h"
#include "layer_info.h"
#include "param.h"
#include "system_func.h"
#include "ui_common.h"

// Event Type
enum class EventType
{
	MIDI,
	PARAM_CHANGED,
	SYSTEM_FUNC,
	RELOAD_PRESETS,
	SFC_FUNC
};

// Surface Control Function types
enum class SfcFuncType
{
	SET_SWITCH_VALUE,
	SET_SWITCH_LED_STATE,
	RESET_MULTIFN_SWITCHES,
	SET_MULTIFN_SWITCH,	
	SET_CONTROL_HAPTIC_MODE,
	SET_CONTROLS_STATE
};

// Surface Control Function
class SfcFunc
{
public:
    // Public data
	MoniqueModule from_module;
    SfcFuncType type;
	SfcControlParam *param;
	uint switch_value;
	std::string haptic_mode;
	std::string controls_state;
	uint num;
	bool reset_associated_switches;

    // Constructor/Destructor
    SfcFunc() {}
	SfcFunc(SfcFuncType type, MoniqueModule from_module) {
		this->type = type;
		this->from_module = from_module;
		this->param = nullptr;
		this->switch_value = OFF;
		this->haptic_mode = "";
		this->controls_state = "";
		this->num = 0;
		this->reset_associated_switches = true;
	}
    ~SfcFunc() {}
};

// Base Event class (virtual)
class BaseEvent
{
public:
	// Constructor
	BaseEvent(MoniqueModule module, EventType type);

	// Destructor
	virtual ~BaseEvent() = 0;

	// Public functions
	MoniqueModule module() const;
	EventType type() const;

private:
	// Private data
    MoniqueModule _module;
    EventType _type;
};

// MIDI Event class
class MidiEvent : public BaseEvent
{
public:
	// Constructor/destructor
	MidiEvent(MoniqueModule module, const snd_seq_event_t &seq_event);
	MidiEvent(MoniqueModule module, const snd_seq_event_t &seq_event, uint layer_id_mask);
	~MidiEvent();

	// Public functions
    snd_seq_event_t seq_event() const;

private:
	// Private data
    snd_seq_event_t _seq_event;
	uint _layer_id_mask;
};

// Param Changed Event class
class ParamChangedEvent : public BaseEvent
{
public:
	// Constructor/destructor
	ParamChangedEvent(const ParamChange &param_change);
	~ParamChangedEvent();

	// Public functions
    ParamChange param_change() const;

private:
	// Private data
    ParamChange _param_change;
};

// System Function Event class
class SystemFuncEvent : public BaseEvent
{
public:
	// Constructor/destructor
	SystemFuncEvent(const SystemFunc &system_func);
	~SystemFuncEvent();

	// Public functions
    SystemFunc system_func() const;

private:
	// Private data
    SystemFunc _system_func;
};

// Re-load Presets Event class
class ReloadPresetsEvent : public BaseEvent
{
public:
	// Constructor/destructor
	ReloadPresetsEvent(MoniqueModule module);
	~ReloadPresetsEvent();

	// Public functions
	bool from_layer_toggle() const;
	bool from_ab_toggle() const;
	void set_from_layer_toggle();
	void set_from_ab_toggle();

private:
	// Private data
	bool _from_layer_toggle;
	bool _from_ab_toggle;
};

// Surface Control Function Event class
class SfcFuncEvent : public BaseEvent
{
public:
	// Constructor/destructor
	SfcFuncEvent(const SfcFunc &sfc_func);
	~SfcFuncEvent();

	// Public functions
    SfcFunc sfc_func() const;

private:
	// Private data
    SfcFunc _sfc_func;	
};

#endif  // _EVENT_H
