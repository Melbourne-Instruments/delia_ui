/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  arp_manager.h
 * @brief Arpeggiator Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _ARP_MANAGER_H
#define _ARP_MANAGER_H

#include <atomic>
#include <random>
#include <algorithm>
#include "base_manager.h"
#include "midi_device_manager.h"
#include "event.h"
#include "event_router.h"
#include "param.h"
#include "timer.h"
#include "utils.h"
#include "layer_info.h"

// Arpeggiator Param IDs
enum ArpParamId : int
{
    ARP_ENABLE_PARAM_ID,
    ARP_DIR_MODE_PARAM_ID,
    ARP_TEMPO_NOTE_VALUE_PARAM_ID,
    ARP_HOLD_PARAM_ID,
    ARP_RUN_PARAM_ID
};

// Arpeggiator Direction Mode
enum ArpDirMode : int
{
    UP = 0,
    DOWN,
    UPDOWN,
    RANDOM,
    ASSIGNED,
    NUM_DIR_MODES
};

// Arpeggiator State
enum class ArpState
{
    DISABLED,
    IDLE,
    PLAYING_NOTEON,
    PLAYING_NOTEOFF
};

// Arpeggiator Manager class
class ArpManager: public BaseManager
{
public:
    // Constructor
    ArpManager(EventRouter *event_router);

    // Destructor
    ~ArpManager();

    // Public functions
    bool start();
    void process();
    void process_event(const BaseEvent *event);
    void process_midi_event_direct(const snd_seq_event_t *event);

private:
    // Private variables
    EventListener *_param_changed_listener;
    EventListener *_reload_presets_listener;
    std::thread *_tempo_event_thread;
    bool _fsm_running;
    uint _tempo_pulse_count;
    uint _pulse_count;
    uint _note_duration_pulse_count;
    std::atomic<bool> _enable;
    std::atomic<bool> _hold;
    std::atomic<ArpDirMode> _dir_mode;
    ArpDirMode _updown_dir_mode;
    std::atomic<bool> _started;
    bool _prev_enable;
    bool _prev_started;
    ArpState _arp_state;
    std::vector<snd_seq_event_t> _arp_notes;
    std::vector<snd_seq_event_t> _held_notes;
    std::vector<uint> _arp_notes_shuffle;
    snd_seq_event_t _note_on;
    snd_seq_event_t _prev_note_on;
    uint _step;
    bool _hold_timeout;
    bool _hold_reset  = true;
    bool _midi_clk_in;

    // Private functions
    void _process_param_changed_event(const ParamChange &param_change);
    void _process_presets();
    void _process_param_value(const Param &param);
    void _process_tempo_event();
    void _process_fsm_async_event(bool reset);
    void _process_fsm();
    void _update_hold(bool hold);
    snd_seq_event_t _get_next_arp_note();
    void _add_arp_note(const snd_seq_event_t &note);
    bool _find_arp_note(const snd_seq_event_t &note);
    bool _remove_arp_note(const snd_seq_event_t &note);
    void _send_arp_note();
    void _stop_arp_note();
    void _stop_arp_notes();
    void _send_note(const snd_seq_event_t &note);
    void _register_params();
};

#endif  // _ARP_MANAGER_H
