/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  pedals_manager.cpp
 * @brief Pedals Manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <unistd.h>
#include <cstring>
 #include <sys/ioctl.h>
#include <linux/gpio.h>
#include "pedals_manager.h"
#include "arp_manager.h"
#include "ain.h"
#include "utils.h"
#include "logger.h"

// Constants
constexpr char GPIO_DEV_NAME[]           = "/dev/gpiochip0";
constexpr uint SUSTAIN_PEDAL_GPIO        = 26;
constexpr auto GPIO_POLL_TIMEOUT_MS      = 500;
constexpr auto ANALOG_PEDAL_POLL_TIME_MS = std::chrono::milliseconds(30);
constexpr uint SEND_SUSTAIN_DEBOUNCE_MS  = std::chrono::milliseconds(10).count();

// Static functions
static void *_process_digtial_pedal_event(void* data);
static void *_process_analog_pedal_event(void* data);

//----------------------------------------------------------------------------
// PedalsManager
//----------------------------------------------------------------------------
PedalsManager::PedalsManager(EventRouter *event_router) : 
    BaseManager(MoniqueModule::PEDALS, "PedalsManager", event_router)
{
    // Initialise class data
    _gui_param_changed_listener = nullptr;
    _sustain_polarity = SustainPolarity::POSITIVE;
    _gpio_chip_handle = -1;
    _digital_pedal_thread = nullptr;
    _analog_pedal_thread = nullptr;
    _run_digtial_pedal_thread = true;
    _run_analog_pedal_thread = true;
    _sustain = 0.0;
    _send_sustain_timer = new Timer(TimerType::ONE_SHOT);
}

//----------------------------------------------------------------------------
// ~PedalsManager
//----------------------------------------------------------------------------
PedalsManager::~PedalsManager()
{
    // Delete the listeners
    if (_gui_param_changed_listener)
        delete _gui_param_changed_listener;

    // Delete any specified timers
    if (_send_sustain_timer)
        delete _send_sustain_timer;         
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool PedalsManager::start()
{
    // Get the Sustain Polarity (global) param
    auto param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::SUSTAIN_POLARITY_PARAM_ID);
    if (param) {
        _sustain_polarity = static_cast<SustainPolarity>(param->hr_value());
    }

    // Get the KBD MIDI channel (global) param
    param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::KBD_MIDI_CHANNEL_PARAM_ID);
    if (param) {
        _kdb_midi_channel = param->hr_value();
    }

    // Open the GPIO chip (read-only)
    int handle = ::open(GPIO_DEV_NAME, O_RDONLY);
    if (handle > 0) {
        struct gpiohandle_request gpio_req;
        struct gpiohandle_data gpio_data;
        
        // GPIO chip successfully opened
        _gpio_chip_handle = handle;

        // Read the state of the Sustain pedal and set the relevant param
        gpio_req.lineoffsets[0] = SUSTAIN_PEDAL_GPIO;
        gpio_req.flags = GPIOHANDLE_REQUEST_INPUT;
        gpio_req.lines = 1;
        auto res = ::ioctl(_gpio_chip_handle, GPIO_GET_LINEHANDLE_IOCTL, &gpio_req);
        if (res == 0) {
            // Now read the GPIO line value
            res = ::ioctl(gpio_req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &gpio_data);
            if (res == 0) {
                // Convert the digital signal into a Sustain value
                _sustain = (_sustain_polarity == SustainPolarity::POSITIVE ?
                                (gpio_data.values[0] ? 0.0 : 1.0) :
                                (gpio_data.values[0] ? 1.0 : 0.0));

                // Send the Sustain param
                _send_sustain();
            }
            ::close(gpio_req.fd);
        }
    }
    else {
        // Could not open the GPIO chip, show an error
        MSG("ERROR: Could not open the GPIO chip for the Digital pedal");
        MONIQUE_LOG_CRITICAL(module(), "Could not open the GPIO chip for the Digital pedal: {}", handle);
    }

    // Create a normal thread to listen for digital (Sustain) pedal events
    _digital_pedal_thread = new std::thread(_process_digtial_pedal_event, this);    

    // Create a normal thread to listen for analog (Expression) pedal events
    _analog_pedal_thread = new std::thread(_process_analog_pedal_event, this);    

    // Call the base manager
    return BaseManager::start();		
}

//----------------------------------------------------------------------------
// stop
//----------------------------------------------------------------------------
void PedalsManager::stop()
{
    // Call the base manager
    BaseManager::stop();

    // Stop the various timers
    _send_sustain_timer->stop();

    // Digtial pedal event task running?
    if (_digital_pedal_thread != 0) {
        // Stop the digital pedal event task
        _run_digtial_pedal_thread = false;
		if (_digital_pedal_thread->joinable())
			_digital_pedal_thread->join(); 
        _digital_pedal_thread = 0;       
    }

    // Analog pedal event task running?
    if (_analog_pedal_thread != 0) {
        // Stop the analog pedal event task
        _run_analog_pedal_thread = false;
		if (_analog_pedal_thread->joinable())
			_analog_pedal_thread->join(); 
        _analog_pedal_thread = 0;       
    }

    // GPIO chip open?
    if (_gpio_chip_handle != -1) {
        // Close the GPIO chip
        auto res = ::close(_gpio_chip_handle);
        if (res < 0) {
            // Error closing the GPIO chip for digital pedal, show the error
            DEBUG_BASEMGR_MSG("Could not close the GPIO chip for the Digtial pedal, close has failed: " << res);
        }        
    }
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void PedalsManager::process()
{
    // Create and add the listeners
    _gui_param_changed_listener = new EventListener(MoniqueModule::GUI, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_gui_param_changed_listener);

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void PedalsManager::process_event(const BaseEvent * event)
{
    // Process the event depending on the type
    switch (event->type())
    {
        case EventType::PARAM_CHANGED:
            // Process the param changed event
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            break;

		default:
            // Event unknown, we can ignore it
            break;
	}
}

//----------------------------------------------------------------------------
// process_digtial_pedal_event
//----------------------------------------------------------------------------
void PedalsManager::process_digtial_pedal_event()
{
    struct pollfd pfd;
    struct gpioevent_request sustain_event_req;
    struct gpioevent_data event_data;
    int ret;

    // Request an event for the Sustain pedal (falling and rising edge)
    sustain_event_req.lineoffset = SUSTAIN_PEDAL_GPIO;
    sustain_event_req.eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;
    ret = ::ioctl(_gpio_chip_handle, GPIO_GET_LINEEVENT_IOCTL, &sustain_event_req);
    if (ret == 0) {
        // Set the GPIO poll descriptor
        pfd.fd = sustain_event_req.fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        // Do forever (until the thread is exited)
        while (_run_digtial_pedal_thread) {
            // Wait for a GPIO event, or a timeout
            if (poll(&pfd, 1, GPIO_POLL_TIMEOUT_MS) > 0) {
                // GPIO event occurred on the Sustain pedal?
                if ((pfd.revents & POLLIN) == POLLIN) {
                    // Read the event
                    auto ret = ::read(sustain_event_req.fd, &event_data, sizeof(event_data));
                    if (ret == sizeof(event_data)) {
                        // Get the value as a rising edge (0) or falling edge (1)
                        float value = event_data.id == GPIOEVENT_EVENT_RISING_EDGE ? 0.0 : 1.0;

                        // Get the Sustain value
                        _sustain = (_sustain_polarity == SustainPolarity::POSITIVE ? 
                                        value :
                                        (value == 0.0 ? 1.0 : 0.0));

                        // Stop and start the send sustain timer
                        _send_sustain_timer->stop();
                        _send_sustain_timer->start(SEND_SUSTAIN_DEBOUNCE_MS, std::bind(&PedalsManager::_send_sustain, this));
                    }
                }               
            }
        }
    }
    else {
        // Event request failed
        DEBUG_BASEMGR_MSG("Could not request the GPIO event: " << errno);
    }
}

//----------------------------------------------------------------------------
// process_analog_pedal_event
//----------------------------------------------------------------------------
void PedalsManager::process_analog_pedal_event()
{
    float value;

    // Do forever (until the thread is exited)
    while (_run_analog_pedal_thread) {
        // Wait the poll time
        std::this_thread::sleep_for(ANALOG_PEDAL_POLL_TIME_MS);

        // Read the value from AIN1
        if (ain::read_ain1(value)) {
            // Get the Expression param and check if the value has changed
            auto param = get_param(utils::ParamRef::MIDI_EXPRESSION);
            if (param && (param->value() != value)) {
                // Set the Expression param - assume for both layers
                static_cast<LayerParam *>(param)->set_value(LayerId::D0, value);
                static_cast<LayerParam *>(param)->set_value(LayerId::D1, value);

                // Get the layers to process for this event
                uint layer_id_mask = (_kdb_midi_channel == 0) ? (LayerId::D0 | LayerId::D1) : _get_layer_id_mask(_kdb_midi_channel - 1);
                if (layer_id_mask) {
                    // Send the param changed even        
                    auto param_change = ParamChange(param, module());
                    param_change.layer_id_mask = layer_id_mask;
                    param_change.display = false;
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                }
            }
        }    
    }
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void PedalsManager::_process_param_changed_event(const ParamChange &param_change)
{
    // If the param is valid
    if (param_change.param) {
        // Is this the Sustain Polarity param?
        if (param_change.param->module() == MoniqueModule::SYSTEM) {
            if (param_change.param == utils::get_param(MoniqueModule::SYSTEM, SystemParamId::SUSTAIN_POLARITY_PARAM_ID)) {
                // Update the Sustain Polarity
                _sustain_polarity = static_cast<SustainPolarity>(param_change.param->hr_value());

                // Update the Sustain value and send it
                _sustain = _sustain ? 0.0f : 1.0f;
                _send_sustain();
            }
            // KBD MIDI channel changed?
            else if (param_change.param->param_id() == SystemParamId::KBD_MIDI_CHANNEL_PARAM_ID) {
                // Set the new KBD MIDI channel
                _kdb_midi_channel = param_change.param->hr_value();
            }            
        }
    }
}

//----------------------------------------------------------------------------
// _send_sustain
//----------------------------------------------------------------------------
void PedalsManager::_send_sustain()
{
    // Get the Sustain param and ARP enable/hold params
    auto sustain_param = utils::get_param(utils::ParamRef::MIDI_SUSTAIN);
    auto arp_enable_param = utils::get_param(MoniqueModule::ARP, ArpParamId::ARP_ENABLE_PARAM_ID);
    auto arp_hold_param = utils::get_param(MoniqueModule::ARP, ArpParamId::ARP_HOLD_PARAM_ID);
    if (sustain_param && arp_enable_param && arp_hold_param) {
        // If the ARP is ON then the sustain is used to turn HOLD on and off
        if (arp_enable_param->hr_value()) {
            // Turn the ARP hold on/off
            arp_hold_param->set_hr_value(_sustain ? 1.0 : 0.0);
            auto param_change = ParamChange(arp_hold_param, module());
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
        }
        else {
            // Set the Sustain param - assume for both layers
            static_cast<LayerParam *>(sustain_param)->set_value(LayerId::D0, _sustain);
            static_cast<LayerParam *>(sustain_param)->set_value(LayerId::D1, _sustain);
        
            // Get the layers to process for this event
            uint layer_id_mask = (_kdb_midi_channel == 0) ? (LayerId::D0 | LayerId::D1) : _get_layer_id_mask(_kdb_midi_channel - 1);
            if (layer_id_mask) {
                // Send the param changed event
                auto param_change = ParamChange(sustain_param, module());
                param_change.layer_id_mask = layer_id_mask;
                param_change.display = false;
                _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
            }
        }
    }
}

//----------------------------------------------------------------------------
// _get_layer_id_mask
//----------------------------------------------------------------------------
uint PedalsManager::_get_layer_id_mask(unsigned char channel)
{
    uint layer_id_mask = 0;

    // Check the MIDI filter for each Layer and set the Layer mask
    if (utils::get_layer_info(LayerId::D0).check_midi_channel_filter(channel)) {
        layer_id_mask |= LayerId::D0;
    }
    if (utils::get_layer_info(LayerId::D1).check_midi_channel_filter(channel)) {
        layer_id_mask |= LayerId::D1;
    }
    return layer_id_mask;
}

//----------------------------------------------------------------------------
// _process_digtial_pedal_event
//----------------------------------------------------------------------------
static void *_process_digtial_pedal_event(void* data)
{
    auto mgr = static_cast<PedalsManager *>(data);
    mgr->process_digtial_pedal_event();

    // To suppress warnings
    return nullptr;
}

//----------------------------------------------------------------------------
// _process_analog_pedal_event
//----------------------------------------------------------------------------
static void *_process_analog_pedal_event(void* data)
{
    auto mgr = static_cast<PedalsManager *>(data);
    mgr->process_analog_pedal_event();

    // To suppress warnings
    return nullptr;
}
