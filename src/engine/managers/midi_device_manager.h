/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  midi_device_manager.h
 * @brief MIDI Device Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _MIDI_DEVICE_MANAGER_H
#define _MIDI_DEVICE_MANAGER_H

#include <thread>
#include <algorithm>
#include <tuple>
#include "base_manager.h"
#include "event_router.h"
#include "event.h"
#include "timer.h"
#include "layer_info.h"

class DawManager;

// Key Status
struct KeyStatus
{
    bool playing_note; 
};

// MIDI event source
enum MidiEventSource
{
    INTERNAL,
    EXT_USB,
    EXT_SERIAL
};

// Received MIDI Event
struct RxMidiEvent
{
    MidiEventSource src;
    snd_seq_event_t ev;

    RxMidiEvent(MidiEventSource src, snd_seq_event_t ev) {
        this->src = src;
        this->ev = ev;
    }
};

// MIDI Device Manager class
class MidiDeviceManager: public BaseManager
{
public:
    // Helper functions
    static bool IsMidiCcParamPath(std::string param_name);
    static bool IsMidiPitchBendParamPath(std::string param_name);
    static bool IsMidiChanpressParamPath(std::string param_name);

    // Constructor
    MidiDeviceManager(EventRouter *event_router);

    // Destructor
    ~MidiDeviceManager();

    // Public functions
    bool start();
    void stop();
    void process();
    void process_event(const BaseEvent *event);
    void process_midi_devices();
    void process_midi_event();
    void process_midi_event_queue();

private:
    // Private variables
    EventListener *_sfc_param_changed_listener;
    EventListener *_gui_param_changed_listener;
    EventListener *_reload_presets_listener;
    std::mutex _seq_mutex;
    snd_seq_t *_seq_handle;
    int _seq_client;
    int _seq_port;
    snd_midi_event_t *_kbd_serial_snd_midi_event;
    int _kbd_serial_midi_port_handle;    
    snd_midi_event_t *_ext_serial_snd_midi_event;
    int _ext_serial_midi_port_handle;
    std::thread *_midi_devices_thread;
    bool _run_midi_devices_thread;    
    std::thread *_midi_event_thread;
    bool _run_midi_event_thread;
    std::thread *_midi_event_queue_thread;
    bool _run_midi_event_queue_thread;    
    int _bank_select_index;
    Timer *_midi_clk_timer;
    uint _midi_clock_count;
    std::chrono::system_clock::time_point _midi_clock_start;
    float _tempo_filter_state;
    Param *_tempo_param;
    Param *_seq_arp_midi_channel_param;
    Param *_midi_clk_in_param;
    Param *_midi_echo_filter_param;
    Param *_pitch_bend_param;
    Param *_chanpress_param;
    Param *_midi_mod_src_1_sel;
    Param *_midi_mod_src_2_sel;
    Param *_midi_cc_1_mod_source;
    Param *_midi_cc_2_mod_source;
    std::vector<std::tuple<snd_seq_event_t, uint64_t>> _logged_sent_cc_msgs;
    std::chrono::_V2::system_clock::time_point _start_time;
    std::mutex _midi_event_queue_mutex;
    std::vector<RxMidiEvent> _midi_event_queue_a;
    std::vector<RxMidiEvent> _midi_event_queue_b;
    std::vector<RxMidiEvent> *_push_midi_event_queue;
    std::vector<RxMidiEvent> *_pop_midi_event_queue;
    std::array<KeyStatus, NUM_KEYS> _keyboard_status;
    uint _kdb_midi_channel;
    uint _midi_clk_count;

    // Private functions
    void _process_param_changed_event(const ParamChange &param_change);
    void _process_presets();
    void _process_param_value(const Param *param);
    void _process_midi_param_changed(const Param *param);
    void _process_midi_param_changed_locked(const Param *param);
    void _process_high_priority_midi_event(snd_seq_event_t *seq_event, bool from_kbd);
    void _process_normal_midi_event(const RxMidiEvent& event);
    void _process_param_changed_mapped_params(uint layer_id_mask, const Param *param, float diff, const Param *skip_param);
    void _start_stop_seq_run(bool start);
    void _open_seq_midi();
    void _close_seq_midi();
    void _open_kbd_serial_midi();
    bool _optimise_midi_message(std::vector<RxMidiEvent>& midi_events, snd_seq_event_t& ev);
    bool _optimise_midi_message(std::vector<snd_seq_event>& midi_events, snd_seq_event_t& ev);
    void _close_kbd_serial_midi();
    void _open_ext_serial_midi();
    void _close_ext_serial_midi();    
    void _midi_clk_timer_callback();
    bool _is_high_priority_midi_event(snd_seq_event_type_t type);
    uint _get_layer_id_mask(unsigned char channel);
    inline MidiEchoFilter _get_midi_echo_filter();
};

#endif  // _MIDI_DEVICE_MANAGER_H
