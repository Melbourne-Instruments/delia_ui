/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  midi_device_manager.cpp
 * @brief MIDI Device Manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <cmath>
#include <sys/ioctl.h>
#include <termios.h>
#include <dirent.h>
#include <sys/stat.h>
#include "midi_device_manager.h"
#include "daw_manager.h"
#include "seq_manager.h"
#include "arp_manager.h"
#include "data_conversion.h"
#include "logger.h"
#include "utils.h"

// Constants
#define KBD_SERIAL_MIDI_AMA_PORT_NUM         "1"
#define KBD_SERIAL_MIDI_DEV_NAME             "/dev/ttyAMA" KBD_SERIAL_MIDI_AMA_PORT_NUM
#define EXT_SERIAL_MIDI_AMA_PORT_NUM         "2"
#define EXT_SERIAL_MIDI_DEV_NAME             "/dev/ttyAMA" EXT_SERIAL_MIDI_AMA_PORT_NUM
constexpr char CC_PARAM_NAME[]               = "cc/0/";
constexpr char PITCH_BEND_PARAM_NAME[]       = "pitch_bend/0";
constexpr char CHANPRESS_PARAM_NAME[]        = "chanpress/0";
constexpr auto MIDI_POLL_TIMEOUT_MS          = 200;
constexpr int SERIAL_MIDI_ENCODING_BUF_SIZE  = 300;
constexpr uint MIDI_DEVICE_POLL_SLEEP_US     = 1*1000*1000;
constexpr uint SYSTEM_CLIENT_ID              = 0;
constexpr uint MIDI_THROUGH_CLIENT_ID        = 14;
constexpr char SUSHI_CLIENT_NAME[]           = "Sushi";
constexpr char GADGET_CLIENT_NAME[]          = "f_midi";
constexpr int MIDI_CC_ECHO_TIMEOUT           = 300;
constexpr uint MAX_DECODED_MIDI_EVENT_SIZE   = 12;
constexpr uint MIDI_EVENT_QUEUE_POLL_SEC     = 0;
constexpr uint MIDI_EVENT_QUEUE_POLL_NSEC    = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(20)).count();
constexpr uint MIDI_EVENT_QUEUE_RESERVE_SIZE = 200;
constexpr uint MAX_TEMPO_DURATION            = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(60000/5)).count();
constexpr uint KBD_BASE_NOTE                 = 36;

// Static functions
static void *_process_midi_devices(void* data);
static void *_process_midi_event(void* data);
static void *_process_midi_queue_event(void* data);

//----------------------------------------------------------------------------
// IsMidiPitchBendParamPath
//----------------------------------------------------------------------------
bool MidiDeviceManager::IsMidiCcParamPath(std::string param_name)
{
    // Check if this param is a MIDI CC param
    auto path = Param::ParamPath(MoniqueModule::MIDI_DEVICE, CC_PARAM_NAME);
    if (param_name.compare(0, path.length(), path) == 0) {
        return true;
    }
    return false;
}

//----------------------------------------------------------------------------
// IsMidiPitchBendParamPath
//----------------------------------------------------------------------------
bool MidiDeviceManager::IsMidiPitchBendParamPath(std::string param_name)
{
    // Check if this param is a MIDI Pitch Bend param
    auto path = Param::ParamPath(MoniqueModule::MIDI_DEVICE, PITCH_BEND_PARAM_NAME);
    if (param_name.compare(0, path.length(), path) == 0) {
        return true;
    }
    return false;
}

//----------------------------------------------------------------------------
// IsMidiChanpressParamPath
//----------------------------------------------------------------------------
bool MidiDeviceManager::IsMidiChanpressParamPath(std::string param_name)
{
    // Check if this param is a MIDI Chanpress param
    auto path = Param::ParamPath(MoniqueModule::MIDI_DEVICE, CHANPRESS_PARAM_NAME);
    if (param_name.compare(0, path.length(), path) == 0) {
        return true;
    }
    return false;
}

//----------------------------------------------------------------------------
// MidiDeviceManager
//----------------------------------------------------------------------------
MidiDeviceManager::MidiDeviceManager(EventRouter *event_router) : 
    BaseManager(MoniqueModule::MIDI_DEVICE, "MidiDeviceManager", event_router)
{
    // Initialise class data
    _sfc_param_changed_listener = nullptr;
    _gui_param_changed_listener = nullptr;
    _reload_presets_listener = nullptr;
    _seq_handle = 0;
    _seq_client = -1;
    _seq_port = -1;
    _kbd_serial_snd_midi_event = 0;
    _kbd_serial_midi_port_handle = 0;
    _ext_serial_snd_midi_event = 0;
    _ext_serial_midi_port_handle = 0;
    _midi_devices_thread = 0;
    _run_midi_devices_thread = true;    
    _midi_event_thread = 0;
    _run_midi_event_thread = true;
    _midi_event_queue_thread = 0;
    _run_midi_event_queue_thread = true;    
    _bank_select_index = -1;
    _midi_clk_timer = new Timer(TimerType::PERIODIC);
    _midi_clock_count = 0;
    _tempo_filter_state = 0;
    _tempo_param = nullptr;
    _seq_arp_midi_channel_param = nullptr;
    _midi_clk_in_param = nullptr;
    _midi_echo_filter_param = nullptr;
    _pitch_bend_param = nullptr;
    _chanpress_param = nullptr;
    _midi_event_queue_a.reserve(MIDI_EVENT_QUEUE_RESERVE_SIZE);
    _midi_event_queue_b.reserve(MIDI_EVENT_QUEUE_RESERVE_SIZE);
    _push_midi_event_queue = &_midi_event_queue_a;
    _pop_midi_event_queue = &_midi_event_queue_b;    
    _start_time = std::chrono::high_resolution_clock::now();
    _kdb_midi_channel = 0;
    _midi_clk_count = 0;

    // Initialise the state of each key
    for (uint i=0; i<NUM_KEYS; i++) {
        _keyboard_status[i].playing_note = false;
    }
}

//----------------------------------------------------------------------------
// ~MidiDeviceManager
//----------------------------------------------------------------------------
MidiDeviceManager::~MidiDeviceManager()
{
    // Stop the MIDI clock timer task
    if (_midi_clk_timer) {
        _midi_clk_timer->stop();
        delete _midi_clk_timer;
        _midi_clk_timer = 0;
    }

    // Delete the listeners
    if (_sfc_param_changed_listener)
        delete _sfc_param_changed_listener;    
    if (_gui_param_changed_listener)
        delete _gui_param_changed_listener;    
    if (_reload_presets_listener)
        delete _reload_presets_listener;    
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool MidiDeviceManager::start()
{
    // Open the sequencer MIDI interface
    _open_seq_midi();
    if (!_seq_handle) {
        // Error opening the sequencer MIDI interface, show an error
        DEBUG_BASEMGR_MSG("Open sequencer MIDI interface failed");    
    }

    // Open the KBD serial MIDI interface
    _open_kbd_serial_midi();
    if (!_kbd_serial_midi_port_handle) {
        // Error opening the KBD serial MIDI interface, show an error
        DEBUG_BASEMGR_MSG("Open KBD serial MIDI interface failed");    
    }

    // Open the external serial MIDI interface
    _open_ext_serial_midi();
    if (!_ext_serial_midi_port_handle) {
        // Error opening the external serial MIDI interface, show an error
        DEBUG_BASEMGR_MSG("Open external serial MIDI interface failed");    
    }

    // If no MIDI interface can be opened, return false
    if (!_seq_handle && !_kbd_serial_midi_port_handle && !_ext_serial_midi_port_handle)
        return false;

    // Get the various params used in MIDI processing
    // We do this here for efficiency - we only retrieve them once
    // Set these params *before* starting the MIDI threads
    _tempo_param = utils::get_tempo_param();
    _seq_arp_midi_channel_param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::SEQ_ARP_MIDI_CHANNEL_PARAM_ID);
    _midi_clk_in_param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::MIDI_CLK_IN_PARAM_ID);
    _midi_echo_filter_param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::MIDI_ECHO_FILTER_PARAM_ID);
    _pitch_bend_param = utils::get_param(Param::ParamPath(this, PITCH_BEND_PARAM_NAME).c_str());
    _chanpress_param = utils::get_param(Param::ParamPath(this, CHANPRESS_PARAM_NAME).c_str());
    _midi_mod_src_1_sel = utils::get_param(utils::ParamRef::MIDI_MOD_SRC_1_SEL);
    _midi_mod_src_2_sel = utils::get_param(utils::ParamRef::MIDI_MOD_SRC_2_SEL);
    _midi_cc_1_mod_source = utils::get_param(utils::ParamRef::MIDI_CC_1_MOD_SOURCE);
    _midi_cc_2_mod_source = utils::get_param(utils::ParamRef::MIDI_CC_2_MOD_SOURCE);

    // Get the KBD MIDI channel
    auto param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::KBD_MIDI_CHANNEL_PARAM_ID);
    if (param) {
        _kdb_midi_channel = param->hr_value();
    }

    // Create a normal thread to poll for MIDI devices
    if (_seq_handle)
        _midi_devices_thread = new std::thread(_process_midi_devices, this);

    // Create a normal thread to listen for MIDI events
    _midi_event_thread = new std::thread(_process_midi_event, this);

    // Create a normal thread to listen for MIDI queue events
    _midi_event_queue_thread = new std::thread(_process_midi_queue_event, this);

    // Start the MIDI clock timer thread
    _midi_clk_timer->start((US_PER_MINUTE / (_tempo_param->hr_value() * PPQN)), std::bind(&MidiDeviceManager::_midi_clk_timer_callback, this));

    // Call the base manager
    return BaseManager::start();		
}

//----------------------------------------------------------------------------
// stop
//----------------------------------------------------------------------------
void MidiDeviceManager::stop()
{
    // Call the base manager
    BaseManager::stop();

    // MIDI devices task running?
    if (_midi_devices_thread != 0) {
        // Stop the MIDI devices task
        _run_midi_devices_thread = false;
		if (_midi_devices_thread->joinable())
			_midi_devices_thread->join(); 
        _midi_devices_thread = 0;       
    }

    // MIDI event queue task running?
    if (_midi_event_queue_thread != 0) {
        // Stop the MIDI queue event task
        _run_midi_event_queue_thread = false;
		if (_midi_event_queue_thread->joinable())
			_midi_event_queue_thread->join(); 
        _midi_event_queue_thread = 0;       
    }

    // MIDI event task running?
    if (_midi_event_thread != 0) {
        // Stop the MIDI event task
        _run_midi_event_thread = false;
		if (_midi_event_thread->joinable())
			_midi_event_thread->join(); 
        _midi_event_thread = 0;       
    }

    // Close the sequencer MIDI interface
    _close_seq_midi();

    // Close the KBD serial MIDI interface
    _close_kbd_serial_midi();

    // Close the external serial MIDI interface
    _close_ext_serial_midi();
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void MidiDeviceManager::process()
{
    // Create and add the listeners
    _sfc_param_changed_listener = new EventListener(MoniqueModule::SFC_CONTROL, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_sfc_param_changed_listener);
    _gui_param_changed_listener = new EventListener(MoniqueModule::GUI, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_gui_param_changed_listener);
    _reload_presets_listener = new EventListener(MoniqueModule::FILE_MANAGER, EventType::RELOAD_PRESETS, this);
    _event_router->register_event_listener(_reload_presets_listener);
    
    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void MidiDeviceManager::process_event(const BaseEvent * event)
{
    // Process the event depending on the type
    switch (event->type())
    {
        case EventType::PARAM_CHANGED:
            // Process the param changed event
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            break;

        case EventType::RELOAD_PRESETS: {
            // Don't process if this reload is simply from a layer or A/B toggle
            if (!static_cast<const ReloadPresetsEvent *>(event)->from_ab_toggle() && !static_cast<const ReloadPresetsEvent *>(event)->from_layer_toggle()) {
                // Process the presets
                _process_presets();
            }
            break;
        }

		default:
            // Event unknown, we can ignore it
            break;
	}
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_param_changed_event(const ParamChange &param_change)
{
    // If the param is valid
    if (param_change.param) {
        // Process the param value
        _process_param_value(param_change.param);
    }
}

//----------------------------------------------------------------------------
// _process_presets
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_presets()
{
    // Process the tempo param
    _process_param_value(_tempo_param);

    // Process the SYSTEM params
    for (const Param *p : utils::get_params(MoniqueModule::SYSTEM)) {
        // Process the param value
        _process_param_value(p);					
    }    
}

//----------------------------------------------------------------------------
// _process_param_value
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_param_value(const Param *param)
{
    // If this is a MIDI param change
    if (param->module() == MoniqueModule::MIDI_DEVICE) {
        // Process the MIDI param change
        _process_midi_param_changed(param);
    }
    // If this is a tempo BPM param change
    else if(param == _tempo_param) {
        // Update the MIDI clock timer
        _midi_clk_timer->change_interval((US_PER_MINUTE / (_tempo_param->hr_value() * PPQN)));
    }
    // Is this a system param change
    else if (param->module() == MoniqueModule::SYSTEM) {
        // KBD MIDI channel changed?
        if (param->param_id() == SystemParamId::KBD_MIDI_CHANNEL_PARAM_ID) {
            // Set the new KBD MIDI channel
            _kdb_midi_channel = param->hr_value();
        }
    }    
}

//----------------------------------------------------------------------------
// _process_midi_param_changed
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_midi_param_changed(const Param *param)
{
    // Get the MIDI sequencer mutex       
    std::lock_guard<std::mutex> lock(_seq_mutex);

    // Process the MIDI param changed event
    _process_midi_param_changed_locked(param);
}

//----------------------------------------------------------------------------
// _process_midi_param_changed_locked
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_midi_param_changed_locked(const Param *param)
{
    // Get the change state string
    int pos = param->path().find(CC_PARAM_NAME);
    if (pos != -1) {
        // Start position of the param number        
        pos += std::strlen(CC_PARAM_NAME);

        // Get the param number string and make sure its valid
        auto str = param->path().substr(pos, (param->path().size() - pos));
        if (str.size() > 0) {
            // Get the channel number to send the MIDI out on
            //auto channel = utils::get_current_layer_info().midi_channel_filter();
            uint channel = 1;
            if (channel) {
                channel--;
            }

            // Get the param number
            int cc_param = std::atoi(str.c_str());

            // Create the sound sequence controller event
            snd_seq_event_t ev;
            snd_seq_ev_clear(&ev);
            ev.type = SND_SEQ_EVENT_CONTROLLER;
            ev.data.control.channel = channel;
            ev.data.control.param = cc_param;
            ev.data.control.value = std::round(dataconv::midi_cc_from_normalised_float(param->value()));

            // If the MIDI echo filter is on, then we log the msg
            if (_get_midi_echo_filter() == MidiEchoFilter::ECHO_FILTER) { 
                // Log the MIDI CC message and elapsed time
                auto end = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - _start_time);
                _logged_sent_cc_msgs.push_back({ev, elapsed.count()});
            }

            // Send the event to all subscribers of this port
            snd_seq_ev_set_direct(&ev);
            snd_seq_ev_set_subs(&ev);
            snd_seq_event_output(_seq_handle, &ev);
            snd_seq_drain_output(_seq_handle);

            // Any received MIDI event should also be outout on the external serial MIDI interface
            uint8_t serial_buf[MAX_DECODED_MIDI_EVENT_SIZE];
            std::memset(serial_buf, 0, sizeof(serial_buf));

            // Decode the input MIDI event into external serial MIDI data
            int res = snd_midi_event_decode(_ext_serial_snd_midi_event, serial_buf, sizeof(serial_buf), &ev);
            if (res > 0) {
                // Write the bytes to the external serial MIDI port
                // Ignore the return value for now
                std::ignore = ::write(_ext_serial_midi_port_handle, &serial_buf, res);

                // Reset the event decoder
                // This needs to be done after each event is processed (not sure why, but no
                // big deal as it has no performance impact)
                snd_midi_event_reset_decode(_ext_serial_snd_midi_event);                            
            }              
        }
    }
}

//----------------------------------------------------------------------------
// process_midi_devices
//----------------------------------------------------------------------------
void MidiDeviceManager::process_midi_devices()
{
    snd_seq_client_info_t *cinfo;
    snd_seq_port_info_t *pinfo;

    // Do forever (until the thread is exited)
    while (_run_midi_devices_thread) {
        // Allocate and set the client/port info structures
        snd_seq_client_info_malloc(&cinfo);
        snd_seq_port_info_malloc(&pinfo);
        snd_seq_client_info_set_client(cinfo, -1);

        // Query and process all clients
        while (snd_seq_query_next_client(_seq_handle, cinfo) >= 0) {
            uint client_id = snd_seq_client_info_get_client(cinfo);

            // Always ignore the System, MIDI Through, Sushi and MONIQUE clients
            if ((client_id != SYSTEM_CLIENT_ID) && (client_id != MIDI_THROUGH_CLIENT_ID) &&
                (client_id != (uint)_seq_client) && 
                (std::strcmp(snd_seq_client_info_get_name(cinfo), SUSHI_CLIENT_NAME) != 0)) {
                // Got a client, query all ports
                snd_seq_port_info_set_client(pinfo, snd_seq_client_info_get_client(cinfo));
                snd_seq_port_info_set_port(pinfo, -1);                
                while (snd_seq_query_next_port(_seq_handle, pinfo) >= 0) {
                    snd_seq_addr_t src_addr;
                    snd_seq_addr_t dst_addr;

                    // Set the source and destination address
                    src_addr.client = snd_seq_client_info_get_client(cinfo);
                    src_addr.port = snd_seq_port_info_get_port(pinfo);                        
                    dst_addr.client = _seq_client;
                    dst_addr.port = _seq_port;                    
                
                    // Can this port support a write subscription?
                    uint seq_port_cap = snd_seq_port_info_get_capability(pinfo);
                    if (seq_port_cap & SND_SEQ_PORT_CAP_SUBS_WRITE) {
                        // We can and should be subscribed to this client
                        // Firstly check if we are already subscribed
                        bool subscribed = false;
                        snd_seq_query_subscribe_t *query;

                        // Setup the query
                        snd_seq_query_subscribe_malloc(&query);
                        snd_seq_query_subscribe_set_root(query, &dst_addr);
                        snd_seq_query_subscribe_set_type(query, SND_SEQ_QUERY_SUBS_WRITE);
                        snd_seq_query_subscribe_set_index(query, 0);

                        // Go through all subscribers
                        while (snd_seq_query_port_subscribers(_seq_handle, query) >= 0) {
                            // Check if we are already subscribed
                            auto addr = snd_seq_query_subscribe_get_addr(query);
                            if ((addr->client == src_addr.client) && (addr->port == src_addr.port)) {
                                // Already subscribed
                                subscribed = true;
                                break;
                            }
                            snd_seq_query_subscribe_set_index(query, snd_seq_query_subscribe_get_index(query) + 1);
                        }
                        snd_seq_query_subscribe_free(query);

                        // Do we need to subscribe to this port?
                        if (!subscribed) {
                            // Connect FROM this port
                            if (snd_seq_connect_from(_seq_handle, _seq_port, src_addr.client, src_addr.port) == 0) {
                                // Successful connection
                                MSG("Connected from MIDI device: " << snd_seq_client_info_get_name(cinfo));
                            }
                            else {
                                // Connection failed
                                //MSG("Connection from MIDI device FAILED: " << snd_seq_client_info_get_client(cinfo));
                            }
                        }                    
                    }

                    // Can this port support a read subscription AND is this the gadget port?
                    if ((seq_port_cap & SND_SEQ_PORT_CAP_SUBS_READ) &&
                        (std::strcmp(snd_seq_client_info_get_name(cinfo), GADGET_CLIENT_NAME) == 0)) {
                        // We can and should be subscribed to this client
                        // Firstly check if we are already subscribed
                        bool subscribed = false;
                        snd_seq_query_subscribe_t *query;

                        // Setup the query
                        snd_seq_query_subscribe_malloc(&query);
                        snd_seq_query_subscribe_set_root(query, &dst_addr);
                        snd_seq_query_subscribe_set_type(query, SND_SEQ_QUERY_SUBS_READ);
                        snd_seq_query_subscribe_set_index(query, 0);

                        // Go through all subscribers
                        while (snd_seq_query_port_subscribers(_seq_handle, query) >= 0) {
                            // Check if we are already subscribed
                            auto addr = snd_seq_query_subscribe_get_addr(query);
                            if ((addr->client == src_addr.client) && (addr->port == src_addr.port))
                            {
                                // Already subscribed
                                subscribed = true;
                                break;
                            }
                            snd_seq_query_subscribe_set_index(query, snd_seq_query_subscribe_get_index(query) + 1);
                        }
                        snd_seq_query_subscribe_free(query);

                        // Do we need to subscribe to this port?
                        if (!subscribed) {
                            // Connect TO this port
                            if (snd_seq_connect_to(_seq_handle, _seq_port, src_addr.client, src_addr.port) == 0) {
                                // Successful connection
                                MSG("Connected to MIDI device: " << snd_seq_client_info_get_name(cinfo));
                            }
                            else {
                                // Connection failed
                                //MSG("Connection to MIDI device FAILED: " << snd_seq_client_info_get_client(cinfo));
                            }
                        }                    
                    }                    
                }
            }
        }
        snd_seq_client_info_free(cinfo);
        snd_seq_port_info_free(pinfo);

        // Sleep before checking all connections again
        usleep(MIDI_DEVICE_POLL_SLEEP_US);
    }
}

//----------------------------------------------------------------------------
// process_midi_event
//----------------------------------------------------------------------------
void MidiDeviceManager::process_midi_event()
{
    int seq_npfd = 0;
    int kbd_serial_npfd = 0;
    int ext_serial_npfd = 0;
    int npfd = 0;
    std::vector<RxMidiEvent> all_midi_events;
    std::vector<snd_seq_event_t> ext_serial_midi_events;
    all_midi_events.reserve(MIDI_EVENT_QUEUE_RESERVE_SIZE);
    ext_serial_midi_events.reserve(MIDI_EVENT_QUEUE_RESERVE_SIZE);

    // Was the sequencer MIDI interface opened?
    if (_seq_handle) {
        // Get the number of sequencer MIDI poll descriptors, checking for POLLIN
        seq_npfd = snd_seq_poll_descriptors_count(_seq_handle, POLLIN);
    }

    // Was the KBD serial MIDI interface opened? We need to add a POLLIN
    // descriptor for that port if so
    if (_kbd_serial_midi_port_handle) {
        kbd_serial_npfd = 1;
    }

    // Was the external serial MIDI interface opened? We need to add a POLLIN
    // descriptor for that port if so
    if (_ext_serial_midi_port_handle) {
        ext_serial_npfd = 1;
    }

    // If there are actually any poll descriptors
    npfd = seq_npfd + kbd_serial_npfd + ext_serial_npfd;
    if (npfd) {
        auto pfd = std::make_unique<pollfd[]>(npfd);
        uint8_t serial_buf[SERIAL_MIDI_ENCODING_BUF_SIZE];

        // Are there any sequencer poll descriptors?
        if (seq_npfd) {
            // Get the sequencer poll descriptiors
            snd_seq_poll_descriptors(_seq_handle, pfd.get(), seq_npfd, POLLIN);
        }
        
        // Is there a KBD serial poll descriptor?
        if (kbd_serial_npfd) {
            // Add the KBD serial poll descriptor after the sequencer entries (if any)
            pfd[seq_npfd].fd = _kbd_serial_midi_port_handle;
            pfd[seq_npfd].events = POLLIN;
            pfd[seq_npfd].revents = 0;
        }

        // Is there an external serial poll descriptor?
        if (ext_serial_npfd) {
            // Add the external serial poll descriptor after the sequencer and KBD serial entries (if any)
            pfd[seq_npfd + kbd_serial_npfd].fd = _ext_serial_midi_port_handle;
            pfd[seq_npfd + kbd_serial_npfd].events = POLLIN;
            pfd[seq_npfd + kbd_serial_npfd].revents = 0;
        }

        // Do forever (until the thread is exited)
        while (_run_midi_event_thread) {
            // Wait for MIDI events, or a timeout
            if (poll(pfd.get(), npfd, MIDI_POLL_TIMEOUT_MS) > 0) {
                snd_seq_event_t* ev = nullptr;

                // Get the MIDI sequencer mutex
                std::lock_guard<std::mutex> lock(_seq_mutex);

                // Process MIDI events in priority order:
                // - KBD (including mod+pitch wheels)
                // - Sequencer (USB)
                // - Serial in

                // If there was a POLLIN event for the KBD MIDI
                if ((pfd[seq_npfd].revents & POLLIN) == POLLIN) {                
                    // Process all KBD serial MIDI events
                    // Firstly read the data available in the serial port
                    int res = ::read(_kbd_serial_midi_port_handle, &serial_buf, sizeof(serial_buf));
                    if (res > 0) {
                        snd_seq_event_t ev;
                        uint8_t *buf = serial_buf;
                        int buf_size = res;

                        // Process the received KBD serial MIDI data
                        while (buf_size > 0) {
                            // Encode bytes to a sequencer event
                            res = snd_midi_event_encode(_kbd_serial_snd_midi_event, buf, buf_size, &ev);
                            if (res <= 0) {
                                // No message to decode, stop processing the buffer
                                break;
                            }

                            // If the event type is NONE, it means more bytes are needed to
                            // process the message, which will be processed on the next poll
                            if (ev.type != SND_SEQ_EVENT_NONE) {
                                bool push_msg = true;

                                // Is this a high priority MIDI event?
                                // We process these immediately, and not via the MIDI event queue
                                if (_is_high_priority_midi_event(ev.type)) {
                                    // Process the high priority MIDI event
                                    // Note: This function will set and return the KBD MIDI channel and adjusted note 
                                    // value if needed
                                    _process_high_priority_midi_event(&ev, true);

                                    // Push the event to the all MIDI events queue (used by this function only)
                                    // Note: Don't push if the keyboard MIDI channel is set to 0 (local)
                                    if (_kdb_midi_channel) {
                                        all_midi_events.push_back(RxMidiEvent(MidiEventSource::INTERNAL, ev));
                                    }
                                }
                                else {
                                    // Process the MIDI event via the MIDI queue
                                    // Get the MIDI event queue mutex
                                    std::lock_guard<std::mutex> lock(_midi_event_queue_mutex);

                                    // Set the KBD MIDI channel
                                    ev.data.control.channel = _kdb_midi_channel ? (_kdb_midi_channel - 1) : 0;

                                    // Optimise MIDI messages in the push events queue 
                                    push_msg =  _optimise_midi_message(*_push_midi_event_queue, ev);
                                    if (push_msg) {
                                        // Push the event to the MIDI event queue
                                        _push_midi_event_queue->push_back(RxMidiEvent(MidiEventSource::INTERNAL, ev));
                                    }

                                    // Optimise MIDI messages in the all events queue
                                    // Note: Don't push if the keyboard MIDI channel is set to 0 (local)
                                    push_msg = _optimise_midi_message(all_midi_events, ev);
                                    if (push_msg && _kdb_midi_channel) {
                                        // Push the event to the all MIDI events queue (used by this function only)
                                        all_midi_events.push_back(RxMidiEvent(MidiEventSource::INTERNAL, ev));
                                    }
                                }
                            }

                            // If a buffer underflow occurs (should never happen)
                            if (res > buf_size) {
                                // Buffer underflow, stop processing the buffer
                                break;
                            }

                            // Set the offset for the next message, if any
                            buf += res;
                            buf_size -= res;
                        }
                    }
                }

                // Process all sequencer (USB) MIDI events (if any)
                while (snd_seq_event_input(_seq_handle, &ev) > 0) {
                    bool push_msg = true;

                    // Is this a high priority MIDI event?
                    // We process these immediately, and not via the MIDI event queue
                    if (_is_high_priority_midi_event(ev->type)) {
                        // Process the high priority MIDI event
                        _process_high_priority_midi_event(ev, false);

                        // Push the event to the all MIDI events queue (used by this function only)
                        all_midi_events.push_back(RxMidiEvent(MidiEventSource::EXT_USB, *ev));
                    }
                    else {
                        // Process the MIDI event via the MIDI queue
                        // Get the MIDI event queue mutex
                        std::lock_guard<std::mutex> lock(_midi_event_queue_mutex);

                        // Optimise MIDI messages in the push events queue
                        push_msg = _optimise_midi_message(*_push_midi_event_queue, *ev);
                        if (push_msg) {
                            // Push the event to the MIDI event queue
                            _push_midi_event_queue->push_back(RxMidiEvent(MidiEventSource::EXT_USB, *ev));

                        }
                        
                        // Optimise MIDI messages in the all events queue
                        push_msg = _optimise_midi_message(all_midi_events, *ev);
                        if(push_msg) {
                            // Push the event to the all MIDI events queue (used by this function only)
                            all_midi_events.push_back(RxMidiEvent(MidiEventSource::EXT_USB, *ev));
                        }
                    }
                }

                // If there was a POLLIN event for the external serial MIDI
                if ((pfd[seq_npfd + kbd_serial_npfd].revents & POLLIN) == POLLIN) {                
                    // Process all external serial MIDI events
                    // NOTE: The MIDI event queue is NOT used for external serial MIDI events as the performance
                    // is automatically rate-limited by the speed of the serial port
                    // Firstly read the data available in the serial port
                    int res = ::read(_ext_serial_midi_port_handle, &serial_buf, sizeof(serial_buf));
                    if (res > 0) {
                        snd_seq_event_t ev;
                        uint8_t *buf = serial_buf;
                        int buf_size = res;

                        // Process the received external serial MIDI data
                        while (buf_size > 0) {
                            // Encode bytes to a sequencer event
                            res = snd_midi_event_encode(_ext_serial_snd_midi_event, buf, buf_size, &ev);
                            if (res <= 0) {
                                // No message to decode, stop processing the buffer
                                break;
                            }

                            // If the event type is NONE, it means more bytes are needed to
                            // process the message, which will be processed on the next poll
                            if (ev.type != SND_SEQ_EVENT_NONE) {
                                bool push_msg;

                                // Optimise MIDI messages in the external serial events queue
                                push_msg = _optimise_midi_message(ext_serial_midi_events, ev);
                                if (push_msg) {
                                    // Push the event to process
                                    ext_serial_midi_events.push_back(ev);
                                }

                                // Optimise MIDI messages in the all events queue
                                push_msg = _optimise_midi_message(all_midi_events, ev);
                                if (push_msg) {
                                    // Push the event to the all MIDI events queue (used by this function only)
                                    all_midi_events.push_back(RxMidiEvent(MidiEventSource::EXT_SERIAL, ev));                                    
                                }
                            }

                            // If a buffer underflow occurs (should never happen)
                            if (res > buf_size) {
                                // Buffer underflow, stop processing the buffer
                                break;
                            }

                            // Set the offset for the next message, if any
                            buf += res;
                            buf_size -= res;
                        }
                    }

                    // Process the serial MIDI events received
                    for (snd_seq_event_t ev : ext_serial_midi_events) {
                        // Process the event as a high priority or normal event
                        _is_high_priority_midi_event(ev.type) ?
                            _process_high_priority_midi_event(&ev, false) :
                            _process_normal_midi_event(RxMidiEvent(MidiEventSource::EXT_SERIAL, ev));
                    }                  
                }

                // Were any MIDI events received and processed?
                if ((all_midi_events.size() > 0) && _ext_serial_midi_port_handle) {
                    // Currently all input MIDI events (from the KBD, sequencer, and serial interfaces) are also output
                    // on the external serial MIDI interface
                    // We also output these events on USB MIDI - except for those received via USB MIDI
                    std::memset(serial_buf, 0, sizeof(serial_buf));
                    for (RxMidiEvent rx_ev : all_midi_events) {
                        // Decode the input MIDI event into external serial MIDI data
                        int res = snd_midi_event_decode(_ext_serial_snd_midi_event, serial_buf, sizeof(serial_buf), &rx_ev.ev);
                        if (res > 0) {
                            // Write the bytes to the external serial MIDI port
                            // Ignore the return value for now
                            std::ignore = ::write(_ext_serial_midi_port_handle, &serial_buf, res);

                            // Reset the event decoder
                            // This needs to be done after each event is processed (not sure why, but no
                            // big deal as it has no performance impact)
                            snd_midi_event_reset_decode(_ext_serial_snd_midi_event);                            
                        }

                        // If this event was not received via USB MIDI
                        if (rx_ev.src != MidiEventSource::EXT_USB) {
                            // Send the event to all subscribers of this port
                            snd_seq_ev_set_direct(&rx_ev.ev);
                            snd_seq_ev_set_subs(&rx_ev.ev);
                            snd_seq_event_output(_seq_handle, &rx_ev.ev);
                            snd_seq_drain_output(_seq_handle);
                        }
                    }

                    // Make sure the all MIDI events are cleared for the next poll
                    all_midi_events.clear();
                    ext_serial_midi_events.clear();
                }
            }
        }
    }
    else
    {
        // No POLLIN descriptors
        DEBUG_BASEMGR_MSG("No MIDI file descriptors to poll");
    }
}

//----------------------------------------------------------------------------
// process_midi_event_queue
//----------------------------------------------------------------------------
void MidiDeviceManager::process_midi_event_queue()
{
    struct timespec poll_time;

    // Set the thread poll time (in nano-seconds)
    std::memset(&poll_time, 0, sizeof(poll_time));
    poll_time.tv_sec = MIDI_EVENT_QUEUE_POLL_SEC;
    poll_time.tv_nsec = MIDI_EVENT_QUEUE_POLL_NSEC;

    // Do forever (until the thread is exited)
    while (_run_midi_event_queue_thread) {
        // Sleep for the poll time
        ::nanosleep(&poll_time, NULL);

        // Swap the push/pop event queues
        {
            std::lock_guard<std::mutex> lock(_midi_event_queue_mutex);
            if (_pop_midi_event_queue == &_midi_event_queue_a) {
                _push_midi_event_queue = &_midi_event_queue_a;
                _pop_midi_event_queue = &_midi_event_queue_b;
            }
            else {
                _push_midi_event_queue = &_midi_event_queue_b;
                _pop_midi_event_queue = &_midi_event_queue_a;                
            }
        }
        {
            // Get the MIDI sequencer mutex
            std::lock_guard<std::mutex> lock(_seq_mutex);

            // Process each normal MIDI event in the pop queue, and then clear the queue
            for (auto itr=_pop_midi_event_queue->begin(); itr != _pop_midi_event_queue->end(); ++itr) {
                _process_normal_midi_event(*itr);
            }
            _pop_midi_event_queue->clear();
        }
    }
}

//----------------------------------------------------------------------------
// _process_high_priority_midi_event
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_high_priority_midi_event(snd_seq_event_t *seq_event, bool from_kbd)
{
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"    
    // Parse the event type
    switch (seq_event->type) {
        case SND_SEQ_EVENT_NOTEON: {
            // If the velocity is zero, make it a NOTE OFF
            if (seq_event->data.note.velocity == 0) {
                seq_event->type = SND_SEQ_EVENT_NOTEOFF;
            }
            
            // Fall-thru
        }

        case SND_SEQ_EVENT_NOTEOFF: {
            // If this note is from the KBD, perform additional processing
            if (from_kbd) {
                // Set the MIDI channel
                seq_event->data.note.channel = _kdb_midi_channel ? (_kdb_midi_channel - 1) : 0;

                // Process the key state - is this a NOTE ON?
                auto& key_status = _keyboard_status[seq_event->data.note.note - KBD_BASE_NOTE];
                if (seq_event->type == SND_SEQ_EVENT_NOTEON) {
                    // NOTE ON - if this key is currently NOT playing a note?
                    if (!key_status.playing_note) {
                        // Set the Key status
                        key_status.playing_note = true;
                    }
                    else {
                        // A duplicate NOTE ON has been received for a key that is already
                        // playing - this should never happen, throw this event away
                        MSG("Duplicate NOTE ON received: " << (int)seq_event->data.note.note);
                        return;
                    }
                }
                else {
                    // NOTE OFF - if this key is currently playing a note?
                    if (key_status.playing_note) {
                        // Set the Key status
                        key_status.playing_note = false;
                    }
                    else {
                        // A duplicate NOTE OFF has been received for a key that is already
                        // playing - this should never happen, throw this event away
                        MSG("Duplicate NOTE OFF received: " << (int)seq_event->data.note.note);
                        return;
                    }
                }
            }

            // Should we send this note to the SEQ/ARP or directly to the DAW?
            auto seq_arp_midi_channel = _seq_arp_midi_channel_param->hr_value();
            ((seq_arp_midi_channel == 0) || (from_kbd && (_kdb_midi_channel == 0)) || (seq_event->data.note.channel == (seq_arp_midi_channel - 1))) ?
                utils::get_manager(MoniqueModule::SEQ)->process_midi_event_direct(seq_event) :
                utils::get_manager(MoniqueModule::DAW)->process_midi_event_direct(seq_event);
            _event_router->post_midi_event(new MidiEvent(module(), *seq_event));
            break;
        }

        case SND_SEQ_EVENT_START: {
            // Only process if the MIDI Clock In is enabled (non-zero param value)
            if (_midi_clk_in_param->value()) {            
                // Save the MIDI start time
                _midi_clock_count = 0;
                _tempo_filter_state = 0;
                _midi_clock_start = std::chrono::high_resolution_clock::now();

                // Start running the sequencer
                _start_stop_seq_run(true);
            }
            break;
        }

        case SND_SEQ_EVENT_STOP: {
            // Only process if the MIDI Clock In is enabled (non-zero param value)
            if (_midi_clk_in_param->value()) {            
                // Reset the MIDI clock
                _midi_clock_count = 0;
                _tempo_filter_state = 0;

                // Stop running the sequencer
                _start_stop_seq_run(false);
            }
            break;
        }        

        case SND_SEQ_EVENT_CLOCK: {
            // Only process if the MIDI Clock In is enabled (non-zero param value)
            if (_midi_clk_in_param->value()) {
                // Signal the sequencer and arpeggiator
                utils::seq_signal();
                utils::arp_signal();

                // We need to also calculate the tempo and update the tempo param
                // Wait for 24 pulses = 1 beat, normally a quarter note
                // Waiting reduces the CPU load and helps with calculation stability                
                _midi_clock_count++;
                if (_midi_clock_count == NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT) {
                    // Get the time duration in uS
                    auto end = std::chrono::high_resolution_clock::now();
                    float duration = std::chrono::duration_cast<std::chrono::microseconds>(end -  _midi_clock_start).count();

                    // Only process durations that are vaguely sensible
                    if (duration < MAX_TEMPO_DURATION) {
                        // Filter the duration to try and smooth out the value
                        (_tempo_filter_state == 0) ?
                            _tempo_filter_state = duration :
                            _tempo_filter_state += (duration - _tempo_filter_state) * 0.2;

                        // Calculate the tempo
                        float tempo = (60000.f / (duration / 1000.f));
                        tempo = std::roundf(tempo);

                        // If the tempo has changed
                        if (_tempo_param && (_tempo_param->value() != tempo)) {
                            // Set the new tempo and send a param change
                            _tempo_param->set_hr_value(tempo);
                            auto param_change = ParamChange(_tempo_param, module());
                            param_change.display = false;
                            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

                            // We need to recurse each mapped param and process it
                            _process_param_changed_mapped_params(utils::get_current_layer_info().layer_id(), _tempo_param, 0.0f, _tempo_param);
                        }
                    }

                    // Reset the MIDI clock count
                    _midi_clock_start = end;
                    _midi_clock_count = 0;
                }               
            }
            break;
        }
    }
    #pragma GCC diagnostic pop    
}

//----------------------------------------------------------------------------
// _optimise_midi_message
//----------------------------------------------------------------------------
bool MidiDeviceManager::_optimise_midi_message(std::vector<RxMidiEvent>& midi_events, snd_seq_event_t& ev)
{
    bool push_msg = true;

    // Make sure the MIDI events are optimised - that is, if an event for this MIDI event already
    // exists, overwrite it rather than adding it to the queue
    // Do this only for pitchbend, chanpress and CC events
    for (auto itr=midi_events.rbegin(); itr != midi_events.rend(); ++itr) {
        auto oev = *itr;
        if (((ev.type == SND_SEQ_EVENT_PITCHBEND && oev.ev.type == SND_SEQ_EVENT_PITCHBEND) ||
            (ev.type == SND_SEQ_EVENT_CHANPRESS && oev.ev.type == SND_SEQ_EVENT_CHANPRESS)) &&
            (ev.data.control.channel == oev.ev.data.control.channel)) {
            // Overwrite this event
            (*itr).ev = ev;
            push_msg = false;
            break; 
        }
        if ((ev.type == SND_SEQ_EVENT_CONTROLLER && oev.ev.type == SND_SEQ_EVENT_CONTROLLER) &&
            (ev.data.control.param == oev.ev.data.control.param) &&
            (ev.data.control.channel == oev.ev.data.control.channel)) {
            // Overwrite this event
            (*itr).ev = ev;
            push_msg = false;
            break; 
        }
    }
    return push_msg;
}

//----------------------------------------------------------------------------
// _optimise_midi_message
//----------------------------------------------------------------------------
bool MidiDeviceManager::_optimise_midi_message(std::vector<snd_seq_event>& midi_events, snd_seq_event_t& ev)
{
    bool push_msg = true;

    // Make sure the MIDI events are optimised - that is, if an event for this MIDI event already
    // exists, overwrite it rather than adding it to the queue
    // Do this only for pitchbend, chanpress and CC events
    for (auto itr=midi_events.rbegin(); itr != midi_events.rend(); ++itr) {
        auto oev = *itr;
        if (((ev.type == SND_SEQ_EVENT_PITCHBEND && oev.type == SND_SEQ_EVENT_PITCHBEND) ||
            (ev.type == SND_SEQ_EVENT_CHANPRESS && oev.type == SND_SEQ_EVENT_CHANPRESS)) &&
            (ev.data.control.channel == oev.data.control.channel)) {
            // Overwrite this event
            *itr = ev;
            push_msg = false;
            break; 
        }
        if ((ev.type == SND_SEQ_EVENT_CONTROLLER && oev.type == SND_SEQ_EVENT_CONTROLLER) &&
            (ev.data.control.param == oev.data.control.param) &&
            (ev.data.control.channel == oev.data.control.channel)) {
            // Overwrite this event
            *itr = ev;
            push_msg = false;
            break; 
        }
    }
    return push_msg;
}

//----------------------------------------------------------------------------
// _process_normal_midi_event
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_normal_midi_event(const RxMidiEvent& event)
{
    auto &ev = event.ev;

    // Parse the event type
    switch (ev.type) {        
        case SND_SEQ_EVENT_CONTROLLER: {
            // If this is controller number 0
            if (ev.data.control.param == 0) {
                // Assume this is a bank select - check the value is within range
                if ((ev.data.control.value >= 0) && ((uint)ev.data.control.value < NUM_BANKS)) {
                    // Set the select bank index
                    _bank_select_index = ev.data.control.value + 1;
                }
            }
            else {
                // MIDI CC events can be mapped to another param
                // Firstly create the MIDI CC path to check against
                auto path = Param::ParamPath(this, CC_PARAM_NAME + std::to_string(ev.data.control.param));
                
                // Get the MIDI param
                auto param = utils::get_param(path.c_str());
                if (param) {
                    bool block = false;
                    auto mode = _get_midi_echo_filter();

                    // Run echo filtering if the MIDI echo filter is enabled 
                    if (mode == MidiEchoFilter::ECHO_FILTER) {
                        // Process the previous messages in the log, removing old messages and checking
                        // for duplicate messages
                        auto end = std::chrono::high_resolution_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - _start_time);
                        auto it = _logged_sent_cc_msgs.begin();
                        while(it != _logged_sent_cc_msgs.end()) {
                            // Remove this message if is is older than the timeout threshold
                            uint64_t delta = elapsed.count() - std::get<1>(*it);
                            if (delta > MIDI_CC_ECHO_TIMEOUT) {
                                // Remove the message
                                _logged_sent_cc_msgs.erase(it);
                            }
                            else {
                                // If the message is an echo of one we have sent, then block the msg
                                snd_seq_event &log_msg = std::get<0>(*it);
                                if ((log_msg.data.control.param == ev.data.control.param) &&
                                    (log_msg.data.control.value == ev.data.control.value) &&
                                    (log_msg.data.control.channel == ev.data.control.channel)) {
                                    block = true;
                                }                                 
                                it++;
                            }
                        }
                    }
                    // If the MIDI echo filter is filter all - all CC messages are blocked
                    else if (mode == MidiEchoFilter::FILTER_ALL) {
                        block = true;
                    }

                    // Dont send the MIDI CC message if its blocked
                    if (!block) {
                        // Get the CC value
                        float value = ev.data.control.value;

                        // Check if this is a MIDI all notes off message
                        if ((ev.data.control.param >= MIDI_CTL_ALL_NOTES_OFF) && (ev.data.control.param <= MIDI_CTL_MONO2)) {
                            // Set the param value to 1.0 as this CC message is translated into an All Notes Off param change
                            // sent to the DAW, where the value has to be > 0.5
                            value = 1.0;
                        }
                        else {
                            // Get the normalised CC value
                            value = dataconv::midi_cc_to_normalised_float(value);
                        }

                        // Set the new value - and get the previous in case we need to process the difference
                        auto prev_val = param->value();  
                        param->set_value(value);

                        // Get the layers to process for this event
                        uint layer_id_mask = (event.src == MidiEventSource::INTERNAL) && (_kdb_midi_channel == 0) ?
                                                (LayerId::D0 | LayerId::D1) :
                                                _get_layer_id_mask(ev.data.control.channel);
                        if (layer_id_mask) {
                            // Process the mapped params for this param change
                            _process_param_changed_mapped_params(layer_id_mask, param, (param->value() - prev_val), nullptr);
                        }
                    }
                }
            }

            // We also need to check if this CC is selected as a mod source
            if (_midi_mod_src_1_sel && _midi_cc_1_mod_source) {
                // Is this CC selected as CC 1 Mod source?
                if (_midi_mod_src_1_sel->hr_value() == ev.data.control.param) {
                    // Send the MIDI CC 1 Mod source param - normalise the CC value to a float
                    _midi_cc_1_mod_source->set_value(dataconv::midi_cc_to_normalised_float(ev.data.control.value));
                    static_cast<DawManager *>(utils::get_manager(MoniqueModule::DAW))->set_param(_midi_cc_1_mod_source);
                }
            }
            if (_midi_mod_src_2_sel && _midi_cc_2_mod_source) {
                // Is this CC selected as CC 2 Mod source?
                if (_midi_mod_src_2_sel->hr_value() == ev.data.control.param) {
                    // Send the MIDI CC 2 Mod source param - normalise the CC value to a float
                    _midi_cc_2_mod_source->set_value(dataconv::midi_cc_to_normalised_float(ev.data.control.value));
                    static_cast<DawManager *>(utils::get_manager(MoniqueModule::DAW))->set_param(_midi_cc_2_mod_source);
                }
            }            
            break;
        }

        case SND_SEQ_EVENT_PITCHBEND: {
            // MIDI Pitch Bend events can be mapped to another param
            if (_pitch_bend_param) {
                // Get the normalised Pitch Bend value
                float value = dataconv::pitch_bend_to_normalised_float(ev.data.control.value);
                _pitch_bend_param->set_value(value);

                // Get the layers to process for this event
                uint layer_id_mask = (event.src == MidiEventSource::INTERNAL) && (_kdb_midi_channel == 0) ?
                                        (LayerId::D0 | LayerId::D1) :
                                        _get_layer_id_mask(ev.data.control.channel);
                if (layer_id_mask) {
                    // Process the mapped params for this param change
                    _process_param_changed_mapped_params(layer_id_mask, _pitch_bend_param, 0.0f, nullptr);
                }
            }
            //DEBUG_MSG("Pitch Bend event on Channel " << (int)(ev.data.control.channel) << " val " << (int)(ev.data.control.value));
            break;
        }

        case SND_SEQ_EVENT_CHANPRESS: {
            // MIDI Chanpress events can be mapped to another param
            if (_chanpress_param) {
                // Get the Chanpress value, clip to min/max, and normalise it
                float value = ev.data.control.value;
                if (value < MIDI_CHANPRESS_MIN_VALUE)
                    value = MIDI_CHANPRESS_MIN_VALUE;
                else if (value > MIDI_CHANPRESS_MAX_VALUE)
                    value = MIDI_CHANPRESS_MAX_VALUE;
                value = (value - MIDI_CHANPRESS_MIN_VALUE) / (MIDI_CHANPRESS_MAX_VALUE - MIDI_CHANPRESS_MIN_VALUE);
                _chanpress_param->set_value(value);
                                
                // Get the layers to process for this event
                uint layer_id_mask = (event.src == MidiEventSource::INTERNAL) && (_kdb_midi_channel == 0) ?
                                        (LayerId::D0 | LayerId::D1) :
                                        _get_layer_id_mask(ev.data.control.channel);
                if (layer_id_mask) {                
                    // Process the mapped params for this param change
                    _process_param_changed_mapped_params(layer_id_mask, _chanpress_param, 0.0f, nullptr);
                }
            }
            //DEBUG_MSG("Chanpress event on Channel " << (int)(ev.data.control.channel) << " val " << (int)(ev.data.control.value));
            break;
        }

        case SND_SEQ_EVENT_KEYPRESS:
            // Send the MIDI event - channel filtering is done by the DAW
            utils::get_manager(MoniqueModule::DAW)->process_midi_event_direct(&ev);
            break;  

        case SND_SEQ_EVENT_PGMCHANGE: {
            // Preset select - check the value is within range
            if ((ev.data.control.value >= 0) && ((uint)ev.data.control.value < NUM_BANK_PRESET_FILES)) {
                uint preset_select_index = ev.data.control.value + 1;
                std::string bank_folder;

                // A preset has been selected, so load it
                // Firstly check if a bank has been selected - if not, then get the current
                // preset bank folder
                if (_bank_select_index >= 0) {
                    struct dirent **dirent = nullptr;
                    int num_files;

                    // Scan the presets folder for the bank folder name
                    num_files = ::scandir(common::MONIQUE_PRESETS_DIR, &dirent, 0, ::versionsort);
                    if (num_files > 0) {
                        // Process each directory in the folder
                        for (uint i=0; i<(uint)num_files; i++) {
                            // Is this a directory?
                            if (dirent[i]->d_type == DT_DIR) {
                                // Get the bank index from the folder name
                                // Note: If the folder name format is invalid, atoi will return 0 - which is ok
                                // as this is an invalid bank index                
                                uint index = std::atoi(dirent[i]->d_name);

                                // Are the first four characters the bank index?
                                if ((index > 0) && (dirent[i]->d_name[3] == '_') && (index == (uint)_bank_select_index)) {                                   
                                    // Set the bank folder
                                    bank_folder = dirent[i]->d_name;
                                    break;
                                }
                            }
                            
                        }
                        for (uint i=0; i<(uint)num_files; i++) {
                            ::free(dirent[i]);
                        }
                    }
                    _bank_select_index = -1;
                }
                else {
                    // Get the current preset bank folder
                    bank_folder = utils::system_config()->preset_id().bank_folder();
                }

                // Was a bank folder found?
                if (!bank_folder.empty()) {
                    std::string preset_name;
                    struct dirent **dirent = nullptr;
                    int num_files;

                    // Scan the bank folder for the preset name
                    num_files = ::scandir(MONIQUE_PRESET_FILE_PATH(bank_folder).c_str(), &dirent, 0, ::versionsort);
                    if (num_files > 0) {
                        // Process each file in the folder
                        for (uint i=0; i<(uint)num_files; i++) {
                            // Is this a normal file?
                            if (dirent[i]->d_type == DT_REG) {
                                // Get the preset index from the filename
                                // Note: If the filename format is invalid, atoi will return 0 - which is ok
                                // as this is an invalid preset index
                                uint index = std::atoi(dirent[i]->d_name);

                                // Are the first two characters the preset number?
                                if ((index > 0) && (dirent[i]->d_name[3] == '_') && (index == preset_select_index)) {                  
                                    // Set the preset name
                                    auto name = std::string(dirent[i]->d_name);
                                    preset_name = name.substr(0, (name.size() - (sizeof(".json") - 1)));
                                    break;
                                }
                            }
                        }
                        for (uint i=0; i<(uint)num_files; i++) {
                            ::free(dirent[i]);
                        }

                        // Was the preset found? If not just use the default preset
                        if (preset_name.empty()) {
                            preset_name = PresetId::DefaultPresetName(preset_select_index);
                        }

                        // Finally we can load the preset
                        auto preset_id = PresetId();
                        preset_id.set_id(bank_folder, preset_name);
                        _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::LOAD_PRESET, preset_id, MoniqueModule::MIDI_DEVICE)));                        
                    }                    
                }
                else {
                    // The bank folder index does not exist - show and log the error
                    MSG("The bank folder index does not exist: " << _bank_select_index);
                    MONIQUE_LOG_ERROR(module(), "The bank folder index does not exist: {}", _bank_select_index);
                }                
            }
            break;
        }
    }   
}

//----------------------------------------------------------------------------
// _process_param_changed_mapped_params
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_param_changed_mapped_params(uint layer_id_mask, const Param *param, float diff, const Param *skip_param)
{
    bool current_layer_id_in_mask = utils::get_current_layer_info().layer_id() & layer_id_mask;

    // Get the mapped params
    auto mapped_params = param->mapped_params(skip_param);
    for (Param *mp : mapped_params) {
        // Because this function is recursive, we need to skip the param that
        // caused any recursion, so it is not processed twice
        if (skip_param && (mp == skip_param))
            continue;

        // Is this a system function? Only process if the current layer is also being processed
        if ((mp->type() == ParamType::SYSTEM_FUNC) && current_layer_id_in_mask) {
            const SfcControlParam *sfc_param = nullptr; 

            // Retrieve the mapped parameters for this system function and parse them
            // to find the first physical control mapped to that function (if any)
            auto sf_mapped_params = mp->mapped_params(nullptr);
            for (Param *sf_mp : sf_mapped_params) {
                // If this is mapped to a physical control, set that control and exit
                if (sf_mp->module() == MoniqueModule::SFC_CONTROL) {
                    sfc_param = static_cast<const SfcControlParam *>(sf_mp);
                    break;
                }
            }

            // Is there a physical control associated with this system function?
            if (sfc_param) {
                // Send the System Function event
                auto sys_func = SystemFunc(static_cast<const SystemFuncParam *>(mp)->system_func_type(), 
                                           param->value(),
                                           module());
                sys_func.linked_param = static_cast<const SystemFuncParam *>(mp)->linked_param();
                //sys_func.type = sfc_param->system_func_type();
                if (sys_func.type == SystemFuncType::MULTIFN_SWITCH) {
                    // Set the multi-function index
                    sys_func.num = static_cast<const SwitchParam *>(param)->multifn_switch_index();
                }
                _event_router->post_system_func_event(new SystemFuncEvent(sys_func));
            }
            else {
                // Create the system function event
                auto sys_func = SystemFunc(static_cast<const SystemFuncParam *>(mp)->system_func_type(), param->value(), module());
                sys_func.linked_param = static_cast<const SystemFuncParam *>(mp)->linked_param();

                // Send the system function event
                _event_router->post_system_func_event(new SystemFuncEvent(sys_func));
            }

            // Note: We don't recurse system function params as they are a system action to be performed
        }
        // If a MIDI param, handle it here
        else if (mp->module() == MoniqueModule::MIDI_DEVICE) {
            // Process the MIDI param change directly
            mp->set_value(param->value());
            _process_midi_param_changed_locked(mp);
        }
        // If a Surface param, handle it here
        else if (mp->module() == MoniqueModule::SFC_CONTROL) {
            // Update the mapped param value to a normalised float
            static_cast<SfcControlParam *>(mp)->set_value_from_param(*param);

            // Send a param changed event - never show param changes on the GUI that come via a CC message
            auto param_change = ParamChange(mp, module());
            param_change.display = false;    
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
        }        
        // Global params cannot be mapped to CCs - only process preset (layer+patch) params
        else if (mp->type() != ParamType::GLOBAL) {
            if (mp->type() == ParamType::PRESET_COMMON) {
                mp->set_value(param->value());
            }
            else {
                // Are both parameters linked to each other?
                if (param->is_linked_param() && mp->is_linked_param()) {
                    // Is the linked functionality enabled?
                    // If not, ignore this mapping
                    if (param->is_linked_param_enabled() || mp->is_linked_param_enabled()) {
                        // Linked parameters are linked by the change (difference) in one of the values
                        // This change is reflected in the other linked param
                        auto value = mp->value() + diff;

                        // Update the params for each Layer as needed
                        if (layer_id_mask & LayerId::D0) {
                            // Update the mapped param value to a normalised float
                            static_cast<LayerParam *>(mp)->set_value(LayerId::D0, value);
                        }
                        if (layer_id_mask & LayerId::D1) {
                            // Update the mapped param value to a normalised float
                            static_cast<LayerParam *>(mp)->set_value(LayerId::D1, value);
                        }
                    }
                    else {
                        // Ignore this mapping
                        continue;
                    }
                }
                else {
                    // Update the params for each Layer as needed
                    if (layer_id_mask & LayerId::D0) {
                        // Update the mapped param value to a normalised float
                        static_cast<LayerParam *>(mp)->set_value_from_param(LayerId::D0, *param);
                    }
                    if (layer_id_mask & LayerId::D1) {
                        // Update the mapped param value to a normalised float
                        static_cast<LayerParam *>(mp)->set_value_from_param(LayerId::D1, *param);
                    }
                }
            }

            // Send a param changed event - never show param changes on the GUI that come via a CC message
            auto param_change = ParamChange(mp, module());
            param_change.layer_id_mask = layer_id_mask;
            param_change.display = false;    
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
        }

        // We need to recurse each mapped param and process it
        _process_param_changed_mapped_params(layer_id_mask, mp, diff, param);
    }
}

//----------------------------------------------------------------------------
// _start_stop_seq_run
//----------------------------------------------------------------------------
void MidiDeviceManager::_start_stop_seq_run(bool start)
{
    // Get the sequencer run paeram
    auto seq_param = utils::get_param(MoniqueModule::SEQ, SeqParamId::RUN_PARAM_ID);
    auto arp_param = utils::get_param(MoniqueModule::ARP, ArpParamId::ARP_RUN_PARAM_ID);
    if (seq_param) {
        // Start/stop sequencer running
        seq_param->set_value(start ? 1.0 : 0.0);
        auto param_change = ParamChange(seq_param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
    }
    if (arp_param) {
        // Start/stop arpeggiator running
        arp_param->set_value(start ? 1.0 : 0.0);
        auto param_change = ParamChange(arp_param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
    }     
}

//----------------------------------------------------------------------------
// _open_seq_midi
//----------------------------------------------------------------------------
void MidiDeviceManager::_open_seq_midi()
{
    // Open the ALSA Sequencer
    if (snd_seq_open(&_seq_handle, "hw", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        DEBUG_BASEMGR_MSG("Open ALSA Sequencer failed");
        _seq_handle = 0;
        return;
    }

    // Set the client name
    if (snd_seq_set_client_name(_seq_handle, "Monique_App:") < 0) {
        DEBUG_BASEMGR_MSG("Set sequencer client name failed");
        snd_seq_close(_seq_handle); 
        snd_config_update_free_global();
        _seq_handle = 0;      
        return;
    }

    // Create a simple port
    _seq_port = snd_seq_create_simple_port(_seq_handle, "Monique_App:",
                                           (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE |
                                            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ), 
                                           SND_SEQ_PORT_TYPE_APPLICATION);
    if (_seq_port < 0) {
        DEBUG_BASEMGR_MSG("Create ALSA simple port failed");
        snd_seq_close(_seq_handle); 
        snd_config_update_free_global();        
        _seq_handle = 0;
        _seq_port = -1;      
        return;
    }

    // Get the sequencer client ID
    snd_seq_client_info_t *cinfo;
    snd_seq_client_info_alloca(&cinfo);
    if (snd_seq_get_client_info(_seq_handle, cinfo) < 0) {
        DEBUG_BASEMGR_MSG("Could not retrieve sequencer client ID");
        snd_seq_close(_seq_handle);
        snd_config_update_free_global();        
        _seq_handle = 0;
        _seq_port = -1;      
        return;
    }
    _seq_client = snd_seq_client_info_get_client(cinfo);

    // Put the sequencer in non-blocking mode
    // Note this just applies to retrieving events, not the poll function
    if (snd_seq_nonblock(_seq_handle, SND_SEQ_NONBLOCK) < 0) {
        DEBUG_BASEMGR_MSG("Put sequencer in non-blocking mode failed");
        snd_seq_close(_seq_handle); 
        snd_config_update_free_global();        
        _seq_handle = 0;
        _seq_client = -1;
        _seq_port = -1;   
        return;     
    }
}

//----------------------------------------------------------------------------
// _open_kbd_serial_midi
//----------------------------------------------------------------------------
void MidiDeviceManager::_open_kbd_serial_midi()
{
    // Open the KBD serial MIDI device (non-blocking)
    int ret1 = ::open(KBD_SERIAL_MIDI_DEV_NAME, O_RDWR|O_NONBLOCK);
    if (ret1 != -1) {
        termios tty;

        // Setup the serial port - firstly get the current settings
        ::tcgetattr(ret1, &tty);

        // Set Control Modes
        tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
        tty.c_cflag |= (CS8 | CREAD | CLOCAL);

        // Set Local Modes
        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);

        // Set Input Modes
        tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

        // Set the input baud rate to 1MB
        ::cfsetispeed(&tty, B1000000);

        // Update the settings
        ::tcsetattr(ret1, TCSANOW, &tty);

        // Port opened OK, create the KBD serial MIDI event
        int ret2 = snd_midi_event_new(SERIAL_MIDI_ENCODING_BUF_SIZE, &_kbd_serial_snd_midi_event);
        if (ret2 == 0) {
            // KBD serial MIDI event created ok, set the KBD serial MIDI port handle
            _kbd_serial_midi_port_handle = ret1;
        }
        else {
            // Create KBD serial MIDI event failed
            DEBUG_BASEMGR_MSG("Create KBD serial MIDI event failed: " << ret2);
            ::close(ret1);
        }
    }
    else {
        // An error occurred opening the KBD serial MIDI device
        DEBUG_BASEMGR_MSG("Open KBD serial MIDI device " << KBD_SERIAL_MIDI_DEV_NAME << " failed: " << ret1);
    }
}

//----------------------------------------------------------------------------
// _open_ext_serial_midi
//----------------------------------------------------------------------------
void MidiDeviceManager::_open_ext_serial_midi()
{
    // Open the external serial MIDI device (non-blocking)
    int ret1 = ::open(EXT_SERIAL_MIDI_DEV_NAME, O_RDWR|O_NONBLOCK);
    if (ret1 != -1) {
        termios tty;

        // Setup the serial port - firstly get the current settings
        ::tcgetattr(ret1, &tty);

        // Set Control Modes
        tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
        tty.c_cflag |= (CS8 | CREAD | CLOCAL);

        // Set Local Modes
        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);

        // Set Input Modes
        tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

        // Set Output Modes
        tty.c_oflag &= ~(OPOST | ONLCR);

        // Set the baud rate to 31250 (note setting to 38400 here sets that baud
        // as we have a specific clock rate for the UART)
        ::cfsetispeed(&tty, B38400);
        ::cfsetospeed(&tty, B38400);

        // Update the settings
        ::tcsetattr(ret1, TCSANOW, &tty);

        // Port opened OK, create the external serial MIDI event
        int ret2 = snd_midi_event_new(SERIAL_MIDI_ENCODING_BUF_SIZE, &_ext_serial_snd_midi_event);
        if (ret2 == 0) {
            // External serial MIDI event created ok, set the external serial MIDI port handle
            _ext_serial_midi_port_handle = ret1;
        }
        else {
            // Create external serial MIDI event failed
            DEBUG_BASEMGR_MSG("Create external serial MIDI event failed: " << ret2);
            ::close(ret1);
        }
    }
    else {
        // An error occurred opening the external serial MIDI device
        DEBUG_BASEMGR_MSG("Open external serial MIDI device " << EXT_SERIAL_MIDI_DEV_NAME << " failed: " << ret1);
    }
}

//----------------------------------------------------------------------------
// _close_seq_midi
//----------------------------------------------------------------------------
void MidiDeviceManager::_close_seq_midi()
{
    // ALSA sequencer opened?
    if (_seq_handle) {
        // Close the  ALSA sequencer and free any allocated memory
        snd_seq_close(_seq_handle); 
        snd_config_update_free_global();
        _seq_handle = 0;
    }
}

//----------------------------------------------------------------------------
// _close_kbd_serial_midi
//----------------------------------------------------------------------------
void MidiDeviceManager::_close_kbd_serial_midi()
{
    // KBD serial MIDI port opened?
    if (_kbd_serial_midi_port_handle) {
        // Close it
        ::close(_kbd_serial_midi_port_handle);
        _kbd_serial_midi_port_handle = 0;

        // Free the MIDI event
        snd_midi_event_free(_kbd_serial_snd_midi_event);
        _kbd_serial_snd_midi_event = 0;
    }
}

//----------------------------------------------------------------------------
// _close_ext_serial_midi
//----------------------------------------------------------------------------
void MidiDeviceManager::_close_ext_serial_midi()
{
    // External serial MIDI port opened?
    if (_ext_serial_midi_port_handle) {
        // Close it
        ::close(_ext_serial_midi_port_handle);
        _ext_serial_midi_port_handle = 0;

        // Free the MIDI event
        snd_midi_event_free(_ext_serial_snd_midi_event);
        _ext_serial_snd_midi_event = 0;
    }
}

//----------------------------------------------------------------------------
// _midi_clk_timer_callback
//----------------------------------------------------------------------------
void MidiDeviceManager::_midi_clk_timer_callback()
{
    // If not the MIDI clock source
    if (_midi_clk_in_param->value() == 0) {
        // Just signal the Arpeggiator at the normal MIDI clock rate, the PPQN is overkill
        _midi_clk_count++;
        if (_midi_clk_count >= PPQN_CLOCK_PULSES_PER_MIDI_CLOCK) {
            //Signal the Arpeggiator
            utils::arp_signal();
            _midi_clk_count = 0;
        }

        // Always Signal the Sequencer
        utils::seq_signal();
    }
}

//----------------------------------------------------------------------------
// _is_high_priority_midi_event
//----------------------------------------------------------------------------
bool MidiDeviceManager::_is_high_priority_midi_event(snd_seq_event_type_t type)
{
    // Return if this is a high priority MIDI event or not
    return (type == SND_SEQ_EVENT_NOTEON) || (type == SND_SEQ_EVENT_NOTEOFF) ||
           (type == SND_SEQ_EVENT_START) || (type == SND_SEQ_EVENT_STOP) ||
           (type == SND_SEQ_EVENT_CLOCK);
}

//----------------------------------------------------------------------------
// _get_layer_id_mask
//----------------------------------------------------------------------------
uint MidiDeviceManager::_get_layer_id_mask(unsigned char channel)
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
// _get_midi_echo_filter
//----------------------------------------------------------------------------
inline MidiEchoFilter MidiDeviceManager::_get_midi_echo_filter()
{
    // Convert and return the mode
    int mode = _midi_echo_filter_param->hr_value();
    if (mode > MidiEchoFilter::FILTER_ALL)
        mode = MidiEchoFilter::FILTER_ALL;
    return MidiEchoFilter(mode);
}

//----------------------------------------------------------------------------
// _process_midi_devices
//----------------------------------------------------------------------------
static void *_process_midi_devices(void* data)
{
    auto midi_manager = static_cast<MidiDeviceManager*>(data);
    midi_manager->process_midi_devices();

    // To suppress warnings
    return nullptr;
}

//----------------------------------------------------------------------------
// _process_midi_event
//----------------------------------------------------------------------------
static void *_process_midi_event(void* data)
{
    auto midi_manager = static_cast<MidiDeviceManager*>(data);
    midi_manager->process_midi_event();

    // To suppress warnings
    return nullptr;
}

//----------------------------------------------------------------------------
// _process_midi_queue_event
//----------------------------------------------------------------------------
static void *_process_midi_queue_event(void* data)
{
    auto midi_manager = static_cast<MidiDeviceManager*>(data);
    midi_manager->process_midi_event_queue();

    // To suppress warnings
    return nullptr;
}
