/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  seq_manager.h
 * @brief Sequencer Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _SEQ_MANAGER_H
#define _SEQ_MANAGER_H

#include <atomic>
#include "base_manager.h"
#include "midi_device_manager.h"
#include "event.h"
#include "event_router.h"
#include "utils.h"
#include "layer_info.h"
#include "param.h"

// Constants
constexpr uint STEP_SEQ_MAX_STEPS = 16;

// Sequencer Param IDs
enum SeqParamId : int
{
    MODE_PARAM_ID,
    REC_PARAM_ID,
    RUN_PARAM_ID,
    NUM_STEPS_PARAM_ID,
    TEMPO_NOTE_VALUE_PARAM_ID,
    HOLD_PARAM_ID,
    PHRASE_BEATS_PER_BAR_PARAM_ID,
    PHRASE_QUANTISATION_PARAM_ID,
    STEP_1_ID,
    STEP_LAST_ID = (STEP_1_ID + (STEP_SEQ_MAX_STEPS - 1)),
    CHUNK_PARAM_ID
};

// Sequencer Mode
enum class SeqMode
{
    STEP,
    PHRASE_LOOPER,
    NUM_MODES
};

// Step Sequencer State
enum class StepSeqState
{
    IDLE,
    PROGRAMMING,
    START_PLAYING,
    PLAYING_NOTEON,
    PLAYING_NOTEOFF,
    PLAYING_LAST_NOTEOFF
};

// Looper Sequencer State
enum class LooperSeqState
{
    IDLE,
    START_PLAYING,
    PLAYING
};

// Step Type
enum class StepType
{
    NORMAL,
    START_TIE,
    TIE,
    END_TIE,
    REST
};

// Phrase Sequencer Event Type
enum class PhraseSeqEventType
{
    NOTE,
    END_OF_PHRASE
};

// Phrase Beats per Bar
enum class PhraseBeatsPerBar
{
    NONE,
    FOUR,
    THREE,
    TWO,
    FIVE,
    NUM_BEATS_PER_BAR_VALUES
};

// Phrase Quantisation
enum PhraseQuantisation {
    NONE,
    QUARTER,
    EIGHTH,
    SIXTEENTH,
    THIRTYSECOND,
    QUARTER_TRIPLETS,
    EIGHTH_TRIPLETS,
    SIXTEENTH_TRIPLETS,
    THIRTYSECOND_TRIPLETS,
    NUM_PHRASE_QUANTISATION_VALUES
};

// Sequencer Step
struct SeqStep
{
    static const uint max_notes_per_step = 12;
    StepType step_type;
    std::array<snd_seq_event_t, max_notes_per_step> notes;
    int num_notes;
};

// Phrase Sequencer Event
struct PhraseSeqEvent
{
    PhraseSeqEventType type;
    snd_seq_event_t note_event;
    uint32_t ticks;
    uint32_t qnt_ticks;
};

// Sequencer Manager class
class SeqManager : public BaseManager
{
public:
    // Constructor
    SeqManager(EventRouter *event_router);

    // Destructor
    ~SeqManager();

    // Public functions
    bool start();
    void process();
    void process_event(const BaseEvent *event);
    void process_midi_event_direct(const snd_seq_event_t *event);

private:
    // Private variables
    EventListener *_param_changed_listener;
    EventListener *_reload_presets_listener;
    EventListener *_sys_func_listener;
    SeqMode _seq_mode;
    SeqMode _prev_seq_mode;
    std::thread *_tempo_event_thread;
    std::thread *_save_phrase_seq_thread;
    bool _fsm_running;
    bool _reset_fsm;
    std::mutex _seq_mutex;
    std::atomic<bool> _run;
    std::atomic<bool> _rec;
    std::atomic<bool> _hold;
    bool _prev_run;
    bool _prev_rec;
    PhraseQuantisation _phrase_qnt;
    StepSeqState _step_seq_state;
    LooperSeqState _looper_seq_state;
    std::array<SeqStep, STEP_SEQ_MAX_STEPS> _seq_steps;
    uint _tempo_pulse_count;
    uint _note_duration_pulse_count;
    uint _phrase_metronome_accent_pulse_count;
    uint _phrase_metronome_accent_pulses;
    uint _phrase_qnt_pulse_count;
    uint _num_steps;
    uint _num_active_steps;
    uint _num_selected_steps;
    uint _step;
    int _tie_start_step;
    int _tie_end_step;
    snd_seq_event_t _note_on;
    unsigned char _base_note;
    unsigned char _preset_base_note;
    uint _step_note_index;
    std::vector<snd_seq_event_t> _sent_notes;
    std::vector<snd_seq_event_t> _idle_sent_notes;       
    Param *_num_steps_param;
    std::vector<PhraseSeqEvent> _phrase_seq_events;
    std::vector<PhraseSeqEvent>::iterator _phrase_seq_events_itr;
    std::vector<PhraseSeqEvent> _phrase_seq_note_on_events;
    std::atomic<uint32_t> _ticks;
    uint32_t _end_of_phrase_ticks;
    uint _clock_pulse_count;
    bool _overdub;
    bool _overdub_started;
    std::vector<unsigned char> _seq_played_notes;
    unsigned char _current_midi_channel = 0;
    Param *_midi_clk_in_param;
    bool _save_phrase_seq;
    bool _round_to_bar;
    uint _round_bar_count;
    common::TempoNoteValue _tempo_note_value;

    // Private functions
    void _process_param_changed_event(const ParamChange &param_change);
    void _process_presets();
    void _process_system_func_event(const SystemFunc &data);
    void _process_param_value(const Param& param, MoniqueModule from_module);
    void _register_params();
    void _process_tempo_event();
    void _process_save_phrase_seq_event();
    bool _process_fsm();
    void _send_seq_note(const snd_seq_event_t &ev);
    void _stop_seq();
    void _clear_idle_sent_notes();    
    void _send_note(const snd_seq_event_t &note);
    void _set_sys_func_switch(SystemFuncType system_func_type, bool set);
    void _set_multifn_switch(uint index, bool reset_other_switches);
    void _send_step_seq_note();
    void _stop_step_seq_note();
    void _send_seq_note(PhraseSeqEvent& note);
    void _reset_seq();
    void _reset_seq_steps();
    void _reset_seq_phrase_looper();
    void _set_seq_step_param(uint step);
    void _check_for_phrase_held_notes();
    void _append_end_phrase_event();
    void _sort_phrase_seq();
    void _load_seq_chunks();
    void _save_seq_chunks(const std::vector<PhraseSeqEvent>& seq);
    void _quantise_seq();
    void _quantise_note_event(std::vector<PhraseSeqEvent>::iterator note_event_itr);
    void _quantise_note_on_event(std::vector<PhraseSeqEvent>::iterator note_on_event_itr);
    void _quantise_note_off_event(std::vector<PhraseSeqEvent>::iterator note_on_event_itr, std::vector<PhraseSeqEvent>::iterator note_off_event_itr);
    inline uint32_t _quantise_ticks(uint32_t ticks); 
    inline uint32_t _quantise_ticks(uint32_t ticks, uint phrase_note_pulse_count, bool round_up=false); 
};

#endif // _SEQ_MANAGER_H
