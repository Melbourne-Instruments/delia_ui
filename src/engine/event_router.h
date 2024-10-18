/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  event_router.h
 * @brief Event Router definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _EVENT_ROUTER_H
#define _EVENT_ROUTER_H

#include "event.h"
#include "base_manager.h"

// Event Listener class
class EventListener
{
public:
	// Constructor/Destructor
	EventListener(MoniqueModule module_filter, EventType event_type, BaseManager *mgr)
	{
		_module_filter = module_filter;
		_event_type = event_type;
		_mgr = mgr;
	}
	~EventListener() {};

	// Public functions
	MoniqueModule module_filter() { return _module_filter; }
	EventType event_type() { return _event_type; }
	BaseManager *mgr() { return _mgr; }

private:
	// Private variables
	MoniqueModule _module_filter;
	EventType _event_type;
	BaseManager *_mgr;
};

// Event Router class
class EventRouter
{
public:
	// Constructor/Destructor
	EventRouter();
	~EventRouter();

	// Public functions
	void register_event_listener(EventListener *listener);
	void post_midi_event(const MidiEvent *event);
	void post_param_changed_event(const ParamChangedEvent *event);
	void post_system_func_event(const SystemFuncEvent *event);
	void post_reload_presets_event(const ReloadPresetsEvent *event);
	void post_sfc_func_event(const SfcFuncEvent *event);

private:
	// Private variables
	std::vector<EventListener *> _midi_event_listeners;
	std::vector<EventListener *> _param_changed_event_listeners;
	std::vector<EventListener *> _system_func_event_listeners;
	std::vector<EventListener *> _reload_presets_event_listeners;
	std::vector<EventListener *> _sfc_func_event_listeners;

	// Private functions
	inline bool _check_event_module_filter(EventListener *el, const BaseEvent *event);
};

#endif  // _EVENT_ROUTER_H
