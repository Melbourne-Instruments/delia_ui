/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  pedals_manager.h
 * @brief Pedals Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _PEDALS_MANAGER_H
#define _PEDALS_MANAGER_H

#include <thread>
#include "base_manager.h"
#include "event_router.h"
#include "event.h"
#include "timer.h"

// Pedals Manager class
class PedalsManager: public BaseManager
{
public:
    // Constructor
    PedalsManager(EventRouter *event_router);

    // Destructor
    ~PedalsManager();

    // Public functions
    bool start();
    void stop();
    void process();
    void process_event(const BaseEvent *event);
    void process_digtial_pedal_event();
    void process_analog_pedal_event();

private:
    // Private variables
    EventListener *_gui_param_changed_listener;
    SustainPolarity _sustain_polarity;
    int _gpio_chip_handle;
    std::thread *_digital_pedal_thread;
    std::thread *_analog_pedal_thread;
    bool _run_digtial_pedal_thread;
    bool _run_analog_pedal_thread;
    float _sustain;
    Timer *_send_sustain_timer;
    uint _kdb_midi_channel;

    // Private functions
    void _process_param_changed_event(const ParamChange &param_change);
    void _send_sustain();
    uint _get_layer_id_mask(unsigned char channel);
};

#endif  // _PEDALS_MANAGER_H
