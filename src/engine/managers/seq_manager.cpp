/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  seq_manager.h
 * @brief Sequencer Manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <unistd.h>
#include "seq_manager.h"
#include "daw_manager.h"
#include "utils.h"

// Constants
constexpr char MODE_PARAM_NAME[]                   = "mode";
constexpr char MODE_DISPLAY_NAME[]                 = "Mode";
constexpr char REC_PARAM_NAME[]                    = "rec";
constexpr char REC_DISPLAY_NAME[]                  = "Record";
constexpr char RUN_PARAM_NAME[]                    = "run";
constexpr char RUN_DISPLAY_NAME[]                  = "Run";
constexpr char NUM_STEPS_PARAM_NAME[]              = "num_steps";
constexpr char NUM_STEPS_DISPLAY_NAME[]            = "Num Steps";
constexpr char PHRASE_BEATS_PER_BAR_PARAM_NAME[]   = "phrase_beats_per_bar";
constexpr char PHRASE_BEATS_PER_BAR_DISPLAY_NAME[] = "Beats Per Bar";
constexpr char PHRASE_QUANTISATION_PARAM_NAME[]    = "phrase_quantisation";
constexpr char PHRASE_QUANTISATION_DISPLAY_NAME[]  = "Quantisation";
constexpr char STEP_PARAM_NAME[]                   = "step_";
constexpr char STEP_DISPLAY_NAME[]                 = "Step ";
constexpr char CHUNK_PARAM_NAME[]                  = "chunk_";
constexpr char CHUNK_NAME[]                        = "Chunk ";
constexpr unsigned char TIE_START_STEP             = 0x80;
constexpr unsigned char TIE_STEP                   = 0x40;
constexpr unsigned char TIE_END_STEP               = 0x20;
constexpr unsigned char REST_STEP                  = 0x10;
constexpr unsigned char STEP_ATTR_MASK             = (TIE_START_STEP+TIE_STEP+TIE_END_STEP+REST_STEP);
constexpr uint PHRASE_LOOPER_MAX_EVENTS            = 2000;
constexpr uint CHUNK_MAX_NOTES                     = 10;
constexpr uint MAX_CHUNKS                          = ((PHRASE_LOOPER_MAX_EVENTS) / CHUNK_MAX_NOTES) + 1;
constexpr unsigned char MAX_MIDI_NOTE              = 108;
constexpr uint MIDI_MAX_NOTE                       = 127;
constexpr uint END_OF_PHRASE_INDICATOR             = 0xFE;

//----------------------------------------------------------------------------
// SeqManager
//----------------------------------------------------------------------------
SeqManager::SeqManager(EventRouter *event_router) : 
    BaseManager(MoniqueModule::SEQ, "SeqManager", event_router)
{
    // Initialise class data
    _param_changed_listener = 0;
    _reload_presets_listener = 0;
    _sys_func_listener = 0;
    _seq_mode = SeqMode::STEP;
    _prev_seq_mode = SeqMode::STEP;
    _tempo_event_thread = 0;
    _save_phrase_seq_thread = 0;
    _fsm_running = true;
    _reset_fsm = false;
    _rec = false;
    _run = false;
    _prev_rec = _rec;
    _prev_run = _run;
    _phrase_qnt = PhraseQuantisation::NONE;
    _step_seq_state = StepSeqState::IDLE;
    _looper_seq_state = LooperSeqState::IDLE;
    _tempo_note_value = common::TempoNoteValue::QUARTER;
    _tempo_pulse_count = utils::tempo_pulse_count(_tempo_note_value);
    _note_duration_pulse_count = _tempo_pulse_count >> 1;
    _phrase_qnt_pulse_count = _tempo_pulse_count * PPQN_CLOCK_PULSES_PER_MIDI_CLOCK;
    _num_steps = 0;
    _num_active_steps = 0;
    _num_selected_steps = 0;
    _step = 0;
    _tie_start_step = -1;
    _tie_end_step = -1;
    _base_note = 0xFF;
    _preset_base_note = 0xFF;
    _step_note_index = 0;
    _reset_seq_steps();
    _phrase_seq_events_itr = _phrase_seq_events.begin();
    _ticks = 0;
    _end_of_phrase_ticks = 0;
    _clock_pulse_count = 0;
    _overdub = false;
    _overdub_started = false;
    _phrase_metronome_accent_pulse_count = 0;
    _phrase_metronome_accent_pulses = 0;
    _midi_clk_in_param = nullptr;
    _save_phrase_seq = false;
    _round_to_bar = false;
    _round_bar_count = 4;
    
    // Register the Sequencer params
    _register_params();

    // Get the number of steps param - we do this for efficiency 
    _num_steps_param = utils::get_param(MoniqueModule::SEQ, SeqParamId::NUM_STEPS_PARAM_ID);    
}

//----------------------------------------------------------------------------
// ~SeqManager
//----------------------------------------------------------------------------
SeqManager::~SeqManager()
{
	// Stop the FSM
	{
		std::lock_guard<std::mutex> lk(utils::seq_mutex());
		_fsm_running = false;
	}
	utils::seq_signal_without_lock();
	
	// Stop the tempo event thread
	if (_tempo_event_thread) {
		// Wait for the tempo event thread to finish and delete it
		if (_tempo_event_thread->joinable())
			_tempo_event_thread->join();
		delete _tempo_event_thread;
		_tempo_event_thread = 0;
	}

    // Stop the save phrase sequence thread
    if (_save_phrase_seq_thread) {
		// Wait for the tempo event thread to finish and delete it
		if (_save_phrase_seq_thread->joinable())
			_save_phrase_seq_thread->join();
		delete _save_phrase_seq_thread;
		_save_phrase_seq_thread = 0;
	}
    
    // Clean up the event listeners
    if (_param_changed_listener)
        delete _param_changed_listener;
    if (_reload_presets_listener)
        delete _reload_presets_listener; 
    if (_sys_func_listener)
        delete _sys_func_listener; 
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool SeqManager::start()
{
    // Get the MIDI clock in param (before we process the presets)
    _midi_clk_in_param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::MIDI_CLK_IN_PARAM_ID);    

    // Before starting the Sequencer, process all the preset values
    _process_presets();

	// Start the tempo event thread
	_tempo_event_thread = new std::thread(&SeqManager::_process_tempo_event, this);

	// Start the save phrase sequence thread
	_save_phrase_seq_thread = new std::thread(&SeqManager::_process_save_phrase_seq_event, this);

    // Call the base manager
    return BaseManager::start();			
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void SeqManager::process()
{
    // Create and add the various event listeners
    _param_changed_listener = new EventListener(MoniqueModule::SYSTEM, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_param_changed_listener);	
    _reload_presets_listener = new EventListener(MoniqueModule::FILE_MANAGER, EventType::RELOAD_PRESETS, this);
    _event_router->register_event_listener(_reload_presets_listener);
    _sys_func_listener = new EventListener(MoniqueModule::SYSTEM, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_sys_func_listener);

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void SeqManager::process_event(const BaseEvent *event)
{
    // Process the event depending on the type
    switch (event->type()) {
        case EventType::PARAM_CHANGED: {
            // Process the Param Changed event
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            break;
        }

        case EventType::RELOAD_PRESETS: {
            // Don't process if this reload is simply from a layer or A/B toggle
            if (!static_cast<const ReloadPresetsEvent *>(event)->from_ab_toggle() && !static_cast<const ReloadPresetsEvent *>(event)->from_layer_toggle()) {
                // Process the presets
                _process_presets();
            }
            break;
        }
        
        case EventType::SYSTEM_FUNC: {
            // Process the System Function event
            _process_system_func_event(static_cast<const SystemFuncEvent *>(event)->system_func());        
            break;            
        }
        
        default:
            // Event unknown, we can ignore it
            break;
    }
}

//----------------------------------------------------------------------------
// process_midi_event_direct
//----------------------------------------------------------------------------
void SeqManager::process_midi_event_direct(const snd_seq_event_t *event)
{
    auto seq_event = *event;

    // Get the Sequencer mutex
    std::lock_guard<std::mutex> guard(utils::seq_mutex());

    // Update the variable used to set the midi channel, this is so we still set the 
    // MIDI output to be the last received channel, even when the voice 1 filter is set to omni
    _current_midi_channel = seq_event.data.note.channel;

    // Check the Sequencer Mode
    if (_seq_mode == SeqMode::STEP) {
        // Are we in programming mode?
        if (_rec) {
            // If there are steps to fill
            if ((_num_steps < STEP_SEQ_MAX_STEPS)) {
                // If this is a note on event, add it to the current step 
                if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (seq_event.data.note.velocity != 0)) {
                    // Make sure the max notes per step are not exceeded
                    if (_step_note_index < SeqStep::max_notes_per_step) {
                        // If this is the first note of the first step
                        if ((_num_steps == 0) && (_step_note_index == 0)) {
                            // Set the base note
                            _base_note = seq_event.data.note.note;
                        }

                        // Add the note to the step
                        DEBUG_BASEMGR_MSG("Added note: " << (int)seq_event.data.note.note << " to step " << _num_steps);
                        _seq_steps[_num_steps].notes[_step_note_index] = seq_event;
                        _seq_steps[_num_steps].notes[_step_note_index].data.note.note -= _base_note;
                        _step_note_index++;

                        // Add the note to the played notes vector
                        // Once this vector is empty, programming will move to the next step
                        _seq_played_notes.push_back(seq_event.data.note.note);
                    }
                }
                // If this is a note off event
                else if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF) || 
                        ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (seq_event.data.note.velocity == 0))) {
                    // Remove the note from the played notes
                    bool found = false;
                    for (auto it = _seq_played_notes.begin(); it != _seq_played_notes.end(); it++) {
                        // Erase the note if found
                        if (*it == seq_event.data.note.note) {
                            _seq_played_notes.erase(it);
                            found = true;
                            break;
                        }
                    }

                    // Are there any notes left? If none, move to the next step
                    if (found && _seq_played_notes.size() == 0) {
                        // Update the step
                        DEBUG_BASEMGR_MSG("Sequencer Step " << _num_steps << ": Programmed " << _step_note_index << " notes");
                        _seq_steps[_num_steps].num_notes = _step_note_index;

                        // Are we tying this step to other steps?
                        if ((_tie_start_step != -1) && (_tie_end_step != -1) && 
                            ((uint)_tie_start_step == _num_steps) && ((uint)_tie_end_step > (uint)_tie_start_step)) {
                            // Yes, indicate this is the first step in the tie
                            DEBUG_BASEMGR_MSG("Start Tie: " << _num_steps);
                            _seq_steps[_num_steps].step_type = StepType::START_TIE;
                            _set_seq_step_param(_num_steps);
                            _num_steps++;

                            // Now loop through and set any middle steps to continue the tie
                            for (; _num_steps<(uint)_tie_end_step; _num_steps++) {
                                _seq_steps[_num_steps].step_type = StepType::TIE;
                                _set_seq_step_param(_num_steps);
                            }

                            // Set the last step to indicate this is the last step in the tie
                            DEBUG_BASEMGR_MSG("End Tie: " << _num_steps);
                            _seq_steps[_num_steps].step_type = StepType::END_TIE;
                            _set_seq_step_param(_num_steps);
                        }
                        else {
                            // Not tying notes, just set this sequencer step
                            _set_seq_step_param(_num_steps);
                        }

                        // Increment for the next step
                        _num_steps++;
                        _step_note_index = 0;
                        if (_num_steps < STEP_SEQ_MAX_STEPS) {
                            // Set the next step switch
                            _set_multifn_switch(_num_steps, true);
                            _tie_start_step = -1;
                            _tie_end_step = -1;
                        }
                        else if (_num_steps == STEP_SEQ_MAX_STEPS) {
                            // Reset the multi-function keys
                            utils::reset_multifn_switches(_event_router);             
                        }

                        // Update the number of steps parameter
                        _num_steps_param->set_hr_value(_num_steps);
                        _num_selected_steps = _num_steps;
                    }            
                }
            }

            // Forward the MIDI event note
            _send_note(seq_event);
        }
        // Is the Sequencer on and has steps?
        else if (_run && _num_steps)
        {
            // If a note-on
            if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (seq_event.data.note.velocity != 0)) {
                // Add the note to the played notes
                _seq_played_notes.push_back(seq_event.data.note.note);

                // Set the new base note and start the sequence immediately if not playing
                _base_note = seq_event.data.note.note;

                //_note_off_received = false;
                if (!_hold && (_step_seq_state == StepSeqState::START_PLAYING)) {
                    // Reset the Seq FSM
                    // Note: Lock already aquired at the start of this function
                    _reset_fsm = true;
                    utils::seq_signal_without_lock();
                }
            }
            // If a note-off
            else if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF) || 
                    ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (seq_event.data.note.velocity == 0))) {
                // Remove the note from the played notes
                for (auto it = _seq_played_notes.begin(); it != _seq_played_notes.end(); it++) {
                    // Erase the note if found
                    if (*it == seq_event.data.note.note) {
                        _seq_played_notes.erase(it);
                        break;
                    }
                }

                // Are there any notes left? If so, set the new base note
                if (_seq_played_notes.size() > 0) {
                    // Set the new base note
                    _base_note = _seq_played_notes.back();			
                }
            }
        }
        else {
            // If a note-on
            if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (seq_event.data.note.velocity != 0)) {
                // Add the note to the idle sent notes
                _idle_sent_notes.push_back(seq_event);
            }
            // If a note-off
            else if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF) || 
                    ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (seq_event.data.note.velocity == 0))) {
                // Remove the note from the idle sent notes
                for (auto it = _idle_sent_notes.begin(); it != _idle_sent_notes.end(); it++) {
                    // Erase the note if found
                    if (((*it).data.note.note == seq_event.data.note.note) &&
                        ((*it).data.note.channel == seq_event.data.note.channel)) {
                        _idle_sent_notes.erase(it);
                        break;
                    }
                }
            }
                    
            // Forward the MIDI event note
            _send_note(seq_event);
        }
    }
    else {
        // Are we in programming mode?
        if (_rec) {
            // If this is a note event
            if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) || (seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF)) {  
                // Make sure we can add this event
                if (_phrase_seq_events.size() < PHRASE_LOOPER_MAX_EVENTS) {
                    // If we are creating the initial phrase AND this is the first note, reset the tick
                    // count for the start of the phrase
                    if (!_overdub && _phrase_seq_events.empty()) {
                        _ticks = 1;
                    }

                    // Note: If the ticks are zero this means a note has been received just as the state machine has reset the tick count
                    // for the next phrase loop - so set it to 1 in this instance
                    auto ticks = _ticks ? (uint32_t)_ticks : (uint32_t)1;

                    // Create the note event
                    auto ev = PhraseSeqEvent();
                    ev.note_event = *event;
                    ev.ticks = ticks;
                    ev.qnt_ticks = ticks;

                    // Add/insert the note event to the sequence
                    std::vector<PhraseSeqEvent>::iterator itr;
                    if (_overdub) { 
                        _phrase_seq_events_itr = _phrase_seq_events.insert(_phrase_seq_events_itr, ev);
                        itr = _phrase_seq_events_itr;
                        _overdub_started = true;
                    }
                    else {
                        _phrase_seq_events.push_back(ev);
                        itr = _phrase_seq_events.end() - 1;
                    }
                    DEBUG_MSG("Added note: " << (uint)event->data.note.note  << " : " << ev.ticks);  

                    // Quantise the note event
                    _quantise_note_event(itr);                  
                }
            }
        }

        // Forward the MIDI event note
        _send_note(seq_event);        
    }
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void SeqManager::_process_param_changed_event(const ParamChange &param_change)
{
    // If the parma is valid and for the Sequencer OR is the MIDI clock in param
    if (param_change.param && ((param_change.param->module() == module()) || (param_change.param == _midi_clk_in_param))) {
        // Get the mutex lock
        std::lock_guard<std::mutex> lk(utils::seq_mutex());        

        // Process the param value
        _process_param_value(*param_change.param, param_change.from_module);
    }
}

//----------------------------------------------------------------------------
// _process_presets
//----------------------------------------------------------------------------
void SeqManager::_process_presets()
{
    // Get the mutex lock
    std::unique_lock<std::mutex> lk(utils::seq_mutex());

    // The Record and Run modes in the Sequencer should be reset whenever the presets
    // are loaded
    auto *param = utils::get_param(module(), SeqParamId::RUN_PARAM_ID);
    if (param) {
        param->set_value(false);
    }
    param = utils::get_param(module(), SeqParamId::REC_PARAM_ID);
    if (param) {
        param->set_value(false);
    }
    _set_sys_func_switch(SystemFuncType::SEQ_REC, false);
    _set_sys_func_switch(SystemFuncType::SEQ_RUN, false);
    
    // Reset the sequencer settings
    _reset_seq();

    // Parse the Sequencer params
    for (const Param *p : utils::get_params(module())) {
        // Process the initial param value
        _process_param_value(*p, MoniqueModule::FILE_MANAGER);
    }

    // Load the Sequencer chunks
    _load_seq_chunks();

    // Quantise the sequence
    _quantise_seq();
}

//----------------------------------------------------------------------------
// _process_system_func_event
//----------------------------------------------------------------------------
void SeqManager::_process_system_func_event(const SystemFunc &system_func)
{
    // Get the Sequencer mutex
    std::lock_guard<std::mutex> guard(utils::seq_mutex());

    // Check the Sequencer mode
    if (_seq_mode == SeqMode::STEP) {
        // If we are programming and this is a multi-function switch
        if (_rec && (system_func.type == SystemFuncType::MULTIFN_SWITCH)) {
            // Only process if the multi-function switches are in Sequencer record state
            if (utils::multifn_switches_state() == utils::MultifnSwitchesState::SEQ_REC) {
                // If the step key is pressed then insert a rest at the current step, and move
                // to the next step
                if ((system_func.num == _num_steps) && system_func.value) {
                    // Add a rest by simply having no notes defined for this step
                    DEBUG_BASEMGR_MSG("Added rest to step " << _num_steps);
                    DEBUG_BASEMGR_MSG("Sequencer Step " << _num_steps << ": Programmed " << _step_note_index << " notes");

                    // If a tie is set, clear it
                    if ((_tie_start_step != -1) && (_tie_end_step != -1)) {
                        _tie_start_step = -1;
                        _tie_end_step = -1;
                    }

                    // Now add the rest
                    _seq_steps[_num_steps].step_type = StepType::REST;
                    _set_seq_step_param(_num_steps);
                    _num_steps++;
                    _step_note_index = 0;
                    if (_num_steps < STEP_SEQ_MAX_STEPS) {
                        // Set the next step switch
                        _set_multifn_switch(_num_steps, true);
                    }
                    else if (_num_steps == STEP_SEQ_MAX_STEPS) {
                        // Reset the multi-function keys
                        utils::reset_multifn_switches(_event_router);                      
                    }

                    // Update the number of steps parameter
                    _num_steps_param->set_hr_value(_num_steps);
                    _num_selected_steps = _num_steps;                                   
                }
                // If the key being pressed is after the current step then turn that key on to indicate a tie will now
                // be inserted
                else if ((system_func.num > _num_steps) && system_func.value) {
                    bool set_tie = true;

                    // If a tie is already set
                    if ((_tie_start_step != -1) && (_tie_end_step != -1)) {
                        // If this is the same tie end step, clear the tie
                        _set_multifn_switch(_num_steps, true);
                        if ((uint)_tie_end_step == system_func.num) {
                            _tie_start_step = -1;
                            _tie_end_step = -1;
                            set_tie = false;
                        }
                    }

                    // If we should set the tie
                    if (set_tie) {
                        // Indicate this step will now be tied
                        _tie_start_step = _num_steps;
                        _tie_end_step = system_func.num;
                        _set_multifn_switch(system_func.num, false);
                    }
                }
            }
        }      
    }
    else {
        // If this is the SEQ reset system function
        if (system_func.type == SystemFuncType::SEQ_RESET) {
            // Play any note-offs to finish the Sequencer
            _stop_seq();

            // Reset all Sequencer chunks
            for (uint i=0; i<MAX_CHUNKS; i++) {
                // Get the next chunk
                auto param = utils::get_param(MoniqueModule::SEQ, SeqParamId::CHUNK_PARAM_ID + i);
                if (param) {            
                    // If this chunk is already reset, we can assume all subsequent
                    // chunks are also reset
                    if (param->seq_chunk_param_is_reset()) {
                        break;
                    }

                    // Reset the chunk and send a param change to remove it from the preset
                    param->reset_seq_chunk_param();
                    auto param_change = ParamChange(param, module());
                    param_change.display = false;
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                }            
            }

            // Reset the Sequencer
            _run = false;
            _rec = false;
            _reset_seq_phrase_looper();
            _set_sys_func_switch(SystemFuncType::SEQ_REC, false);
            _set_sys_func_switch(SystemFuncType::SEQ_RUN, false);        
        }
    }
}

//----------------------------------------------------------------------------
// _process_param_value
//----------------------------------------------------------------------------
void SeqManager::_process_param_value(const Param& param, MoniqueModule from_module)
{
    // Note: Lock the SEQ mutex *before* calling this function

    // Is this the MIDU clock in param?
    if (&param == _midi_clk_in_param) {
        // Adjust the step sequencer tempo and note durations
        _tempo_pulse_count = utils::tempo_pulse_count(_tempo_note_value);
        if (param.value() == 0) {
            _tempo_pulse_count *= PPQN_CLOCK_PULSES_PER_MIDI_CLOCK;
        }
        _note_duration_pulse_count = _tempo_pulse_count >> 1;
        return;       
    }

    // Process the sequencer param value based on the param ID
    switch (param.param_id()) {
        case SeqParamId::MODE_PARAM_ID: {
            // Update the Sequencer mode
            _prev_seq_mode = _seq_mode;
            _seq_mode = static_cast<SeqMode>(param.hr_value());

            // If the Sequencer is recording or running, stop it
            if (_rec) {
                _rec = false;
                _set_sys_func_switch(SystemFuncType::SEQ_REC, false);
                utils::reset_multifn_switches(_event_router);
            }
            if (_run) {
                _run = false;
                _set_sys_func_switch(SystemFuncType::SEQ_RUN, false);
            }

            // Signal the FSM - we have to run the FSM *once* in the previous mode
            // to stop and clean up the Sequencer state
            // Note: Lock already aquired at the start of this function
            _reset_fsm = true;
            utils::seq_signal_without_lock();            
            break;
        }

        case SeqParamId::REC_PARAM_ID: {
            // Update the program value
            // This will be processed during the next processing loop of the FSM
            _rec = (param.value() == 0) ? false : true;

            // Check the Sequencer Mode
            if (_seq_mode == SeqMode::STEP) {
                // Clear the sequence step parameters if we are starting programming
                if (_rec) {
                    for (uint i=0; i<STEP_SEQ_MAX_STEPS; i++) {
                        auto param = utils::get_param(MoniqueModule::SEQ, SeqParamId::STEP_1_ID + i);
                        if (param) {
                            param->set_str_value("00FFFFFFFFFFFFFFFFFFFFFFFF");
                            auto param_change = ParamChange(param, module());
                            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                        }                                    
                    }
                    utils::config_multifn_switches(0, utils::MultifnSwitchesState::SEQ_REC, _event_router);

                    // If we are in RUN mode - stop
                    if (_run) {
                        _run = false;
                        _set_sys_func_switch(SystemFuncType::SEQ_RUN, false);
                    }                  
                }
                else {
                    // Have any steps been programmed?
                    if (_num_steps) {
                        // Send the number of steps parameter change
                        auto param_change = ParamChange(_num_steps_param, module());
                        param_change.display = false;
                        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                    }

                    // Reset the multi-function keys
                    utils::reset_multifn_switches(_event_router);
                }

                // Signal the FSM
                // Note: Lock already aquired at the start of this function
                _reset_fsm = true;
                utils::seq_signal_without_lock();
            }
            else {
                // If the sequencer has notes programmed
                if (!_phrase_seq_events.empty()) {
                    // If we have stopped recording
                    if (!_rec) {
                        // If we were not overdubbing, save the initial phrase
                        if (!_overdub) {
                            // Check for any held notes
                            _check_for_phrase_held_notes();
                            
                            // Append the end of phrase event
                            _append_end_phrase_event();

                            // Sort the phrase sequence
                            _sort_phrase_seq();
                        }

                        // Set a flag to save the phrase sequence, done asynchronously                      
                        _save_phrase_seq = true;
                    }

                    // Run the Sequencer
                    _run = true;
                    _set_sys_func_switch(SystemFuncType::SEQ_RUN, true);
                }
            }
            break;
        }

        case SeqParamId::RUN_PARAM_ID: {
            // Update the enable value
            // This will be processed during the next processing loop of the FSM
            _run = (param.value() == 0) ? false : true;

            // Check the Sequencer Mode
            if (_seq_mode == SeqMode::STEP) {
                // If steps have been programmed
                if (_num_steps) {
                    // If we are enabling, signal the FSM immediately and make sure the RUN button is on
                    if (_run) {
                        // Make sure the RUN button is on                     
                        _set_sys_func_switch(SystemFuncType::SEQ_RUN, true);

                        // If we were recording - stop
                        if (_rec) {
                            _rec = false;
                            _set_sys_func_switch(SystemFuncType::SEQ_REC, false);
                            utils::reset_multifn_switches(_event_router);
                        }                   
                    }
                    else {
                        // If we receive this RUN stop param change from MIDI, reset the RUN button and multi-function keys
                        if (from_module == MoniqueModule::MIDI_DEVICE) {
                            _set_sys_func_switch(SystemFuncType::SEQ_RUN, false);
                            utils::reset_multifn_switches(_event_router);
                        }
                    }
                }
                else {
                    // No steps programmed, so don't allow the step sequencer to enter run mode
                    _run = false;
                    _set_sys_func_switch(SystemFuncType::SEQ_RUN, false);
                }

                // Signal the FSM
                // Note: Lock already aquired at the start of this function
                _reset_fsm = true;
                utils::seq_signal_without_lock();                 
            }
            else {
                // If the sequencer has notes programmed
                if (!_phrase_seq_events.empty()) {         
                    // If we're starting RUN and recording, and not overdubbing, we need to save the 
                    // initial Sequence
                    if (_run && _rec && !_overdub) {
                        // Check for any held notes
                        _check_for_phrase_held_notes();
                        
                        // Append the end of phrase event
                        _append_end_phrase_event();

                        // Sort the phrase sequence
                        _sort_phrase_seq();

                        // Set a flag to save the phrase sequence, done asynchronously                       
                        _save_phrase_seq = true;                   
                    }

                    // If we're stoping RUN and recording, then update the phrase stop record
                    if (!_run && _rec) {    
                        // Check for any held notes
                        _check_for_phrase_held_notes();
                          
                        // Set a flag to save the phrase sequence, done asynchronously                       
                        _save_phrase_seq = true; 

                        // Stop recording                                       
                        _rec = false;
                        _set_sys_func_switch(SystemFuncType::SEQ_REC, false);
                    }
                }
                else {
                    // No sequence, so nothing to run
                    _run = false;
                }

                // Make sure the RUN button is in the correct state
                _set_sys_func_switch(SystemFuncType::SEQ_RUN, _run);
            }        
            break;					
        }

        case SeqParamId::NUM_STEPS_PARAM_ID: {
            // Set the number of steps to play
            // Note 1: If set to more steps than are available, it will be limited
            // to the actual steps available
            // Note 2: Min steps is 1
            auto num_selected_steps = param.hr_value();
            if (num_selected_steps > _num_steps) {
                // Set the number of active steps
                _num_active_steps = _num_steps;
            }
            else {
                // Set the number of active steps
                _num_active_steps = num_selected_steps;
            }
            _num_selected_steps = num_selected_steps;
            break;				
        }

        case SeqParamId::TEMPO_NOTE_VALUE_PARAM_ID: {
            // Set the tempo pulse count from the updated note value
            _tempo_note_value = utils::tempo_note_value(param.hr_value());
            _tempo_pulse_count = utils::tempo_pulse_count(_tempo_note_value);
            if (_midi_clk_in_param->value() == 0) {
                _tempo_pulse_count *= PPQN_CLOCK_PULSES_PER_MIDI_CLOCK;
            }
            _note_duration_pulse_count = _tempo_pulse_count >> 1;
            break;                  
        }

        case SeqParamId::HOLD_PARAM_ID: {
            // Update the hold parameter
            _hold = (param.value() == 0) ? false : true;
            break;		
        }

        case SeqParamId::PHRASE_QUANTISATION_PARAM_ID: {
            // Get the phrase quantisation value
            _phrase_qnt = static_cast<PhraseQuantisation>(param.hr_value());
            _phrase_qnt == PhraseQuantisation::NONE ?
                _phrase_qnt_pulse_count = utils::tempo_pulse_count(utils::tempo_note_value(common::TempoNoteValue::QUARTER)) * PPQN_CLOCK_PULSES_PER_MIDI_CLOCK :
                _phrase_qnt_pulse_count = utils::tempo_pulse_count(utils::tempo_note_value(param.hr_value() - 1)) * PPQN_CLOCK_PULSES_PER_MIDI_CLOCK;

            // If the phrase looper has events
            if (_phrase_seq_events.size() > 0) {
                // Quantise the current sequence
                _quantise_seq();
            }        
            break;		
        }

        case SeqParamId::PHRASE_BEATS_PER_BAR_PARAM_ID: {
            auto beats_per_bar = static_cast<PhraseBeatsPerBar>(param.hr_value());

            // Based on the beats per bar, set the metronome accent pulse count, and 
            // round to bar count
            switch (beats_per_bar) {
                case PhraseBeatsPerBar::FOUR:
                    _phrase_metronome_accent_pulse_count = 3;
                    _round_bar_count = 4;
                    _round_to_bar = true;
                    break;

                case PhraseBeatsPerBar::THREE:
                    _phrase_metronome_accent_pulse_count = 2;
                    _round_bar_count = 3;
                    _round_to_bar = true;
                    break;

                case PhraseBeatsPerBar::TWO:
                    _phrase_metronome_accent_pulse_count = 1;
                    _round_bar_count = 2;
                    _round_to_bar = true;
                    break;

                case PhraseBeatsPerBar::FIVE:
                    _phrase_metronome_accent_pulse_count = 4;
                    _round_bar_count = 5;
                    _round_to_bar = true;
                    break;

                case PhraseBeatsPerBar::NONE:
                default:
                    _phrase_metronome_accent_pulse_count = 0;
                    _round_bar_count = 4;
                    _round_to_bar = false;
                    break;
            }

            // Reset the pulses count
            _phrase_metronome_accent_pulses = 0;
            break;              
        }

        case SeqParamId::STEP_1_ID...STEP_LAST_ID: {
            // Processing for sequencer steps
            auto step_index = param.param_id() - SeqParamId::STEP_1_ID;

            // Reset the number of notes for this step
            _seq_steps[step_index].num_notes = -1;

            // Must be a string param
            if ((param.data_type() == ParamDataType::STRING) && (param.str_value().size() == (2 + (SeqStep::max_notes_per_step * 2)))) {
                auto notes = param.str_value();
                bool valid_step = true;

                // Get the note attributes
                auto str_attr = notes.substr(0, 2);
                auto attr = std::stoi(str_attr, nullptr, 16);

                // Is this a normal or start tie note
                if ((attr == 0) || (attr == TIE_START_STEP)) {
                    // Set the note type
                    _seq_steps[step_index].step_type = ((attr == TIE_START_STEP) ? StepType::START_TIE : StepType::NORMAL);

                    // Process the notes for this step
                    for (uint i=0; i<SeqStep::max_notes_per_step; i++) {
                        auto str_note = notes.substr((2 + (i*2)), 2);
                        auto note = std::stoi(str_note, nullptr, 16);

                        // If this step has no note, stop processing notes for this step
                        if (note == 0xFF) {
                            if (i == 0) {
                                valid_step = false;
                            }
                            break;
                        }

                        // If this is the first note of step 1, set the base note
                        if ((step_index == 0) && (i == 0)) {
                            _preset_base_note = note;
                        }

                        // Convert the key to a char and insert into the step sequence
                        snd_seq_event_t ev;
                        snd_seq_ev_clear(&ev);
                        snd_seq_ev_set_noteon(&ev, 0, (note - _preset_base_note), 127);
                        _seq_steps[step_index].notes[i] = ev;
                        _seq_steps[step_index].num_notes = i + 1;
                    }
                }
                // Is this a tie note
                else if (attr == TIE_STEP) {
                    _seq_steps[step_index].step_type = StepType::TIE;
                }
                // Is this an end tie note
                else if (attr == TIE_END_STEP) {
                    _seq_steps[step_index].step_type = StepType::END_TIE;
                }
                // Is this a rest note
                else if (attr == REST_STEP) {
                    _seq_steps[step_index].step_type = StepType::REST;
                }

                // If this step is valid, update the number of steps (and base note if not set)
                if (valid_step) {
                    _num_steps = step_index + 1;
                    if ((step_index == 0) && (_base_note == 0xFF)) {
                        _base_note = _preset_base_note;
                    }
                }
            }
            break;
        }

        default:
            // Note sequencer chunks are loaded separately
            break;               
    }
}

//----------------------------------------------------------------------------
// _process_tempo_event
//----------------------------------------------------------------------------
void SeqManager::_process_tempo_event()
{
    uint pulse_count = 0;

	// Do forever until stopped
	while (true) {
        // Get the mutex lock
        std::unique_lock<std::mutex> lk(utils::seq_mutex());

        // Wait for a tempoo event to be signalled
        utils::seq_wait(lk);

        // If the FSM is running
        if (_fsm_running) {
            // Process depending on the Sequencer mode
            if (_seq_mode == SeqMode::STEP) {
                // Has the FSM been reset or the required number of tempo pulses reached?
                if (_reset_fsm || (++pulse_count >= _note_duration_pulse_count)) {
                    // Process the FSM
                    bool start_playing = _process_fsm();

                    // Set the next note duration pulse count to check
                    // Note: Wait for one MIDI clock before when we start playing - we do this to ensure
                    // the clock is running before sending note-on events                
                    _note_duration_pulse_count = _tempo_pulse_count - _note_duration_pulse_count;
                    pulse_count = start_playing ? (_note_duration_pulse_count - 1) : 0;
                    _reset_fsm = false;
                }
            }
            else {
                // If the MIDI clock source is MIDI clock in
                if (_midi_clk_in_param->value()) {
                    // MIDI clock in
                    // Increment the clock pulse count and ticks - the ticks are scaled to
                    // the internal PPQN of 96
                    _clock_pulse_count += PPQN_CLOCK_PULSES_PER_MIDI_CLOCK;
                    _ticks += PPQN_CLOCK_PULSES_PER_MIDI_CLOCK;
                }
                else {
                    // Internal MIDI clock
                    // Have clock pulse ticks been received? If so increment the count
                    _clock_pulse_count++;
                    _ticks++;
                }

                // Process the FSM
                (void)_process_fsm();

                // Have we counted a beat?
                if (_clock_pulse_count >= PPQN) {
                    // Send the metronome trigger param to the DAW and reset the clock pulse count
                    // Note: Only do this for the initial sequence and not during overdub
                    if (_rec && !_overdub) {
                        // Get the Metronome param and set the value for the tone type
                        auto param = utils::get_param(utils::ParamRef::METRONOME_TRIGGER);
                        auto val = ((_phrase_metronome_accent_pulse_count == 0) || (_phrase_metronome_accent_pulses)) ? 0.0 : 1.0;
                        param->set_value(val);
                        static_cast<DawManager *>(utils::get_manager(MoniqueModule::DAW))->set_param(param);

                        // Reset the Metronome pulse count if needed
                        _phrase_metronome_accent_pulses < _phrase_metronome_accent_pulse_count ?
                            _phrase_metronome_accent_pulses++ :
                            _phrase_metronome_accent_pulses = 0;
                    }
                    _clock_pulse_count = 0;
                }
            }
        }
        else {
            // Quit the tempo event thread
            break;
        }        
    }
}

//----------------------------------------------------------------------------
// _process_save_phrase_seq_event
//----------------------------------------------------------------------------
void SeqManager::_process_save_phrase_seq_event()
{
	// Do forever until stopped
	while (_fsm_running) {
        // Save the phrase sequence?
        if (_save_phrase_seq) {
            // Take a copy of the sequence so we can ensure the sort order is correct
            // and hold the mutex for as short a time as possible
            std::vector<PhraseSeqEvent> seq;
            {
                std::unique_lock<std::mutex> lk(utils::seq_mutex());

                // Take a copy of the current sequence
                seq = _phrase_seq_events;
            }

            // Sort the sequence as non-quantised
            std::sort(seq.begin(), seq.end(), [ ]( const PhraseSeqEvent& a, const PhraseSeqEvent& b) {return a.ticks < b.ticks;});

            // Save this Sequence in chunks
            _save_seq_chunks(seq);
            _save_phrase_seq = false;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1)); 
    }
}

//----------------------------------------------------------------------------
// _process_fsm
//----------------------------------------------------------------------------
bool SeqManager::_process_fsm()
{
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"     
    SeqMode& seq_mode = _prev_seq_mode == _seq_mode ? _seq_mode : _prev_seq_mode;

    // Processing of the FSM depends on the Sequencer mode
    if (seq_mode == SeqMode::STEP) {
        StepSeqState prev_seq_state = _step_seq_state;

        // Has the program state changed?
        if (_prev_rec != _rec) {
            // Entering programming mode?
            if (_rec) {
                // Clear any idle sent notes, and enter programming
                _clear_idle_sent_notes();
                _step_seq_state = StepSeqState::PROGRAMMING;
            }
            else
            {
                // Entering enable mode and have sequencer steps been programmed?
                if (_run && _num_steps) {
                    if (_num_selected_steps > _num_steps) {
                        _num_active_steps = _num_steps;
                    }
                    else {
                        _num_active_steps = _num_selected_steps;
                    }
                    _seq_played_notes.clear();
                    _step_seq_state = StepSeqState::START_PLAYING;

                    // Return TRUE to indicate we are starting playing
                    _prev_rec = _run;
                    _prev_seq_mode = _seq_mode;
                    return true;
                }
                else
                    _step_seq_state = StepSeqState::IDLE;
            }
            _prev_rec = _run;
        }
        // Has the enable mode changed?
        else if (_prev_run != _run) {   
            // Entering enable mode and have sequencer steps been programmed?
            if (_run && _num_steps) {
                // If enabling (with steps programmed), clear any idle sent notes
                if (_run) {
                    _clear_idle_sent_notes();
                }

                if (_num_selected_steps > _num_steps) {
                    _num_active_steps = _num_steps;
                }
                else {
                    _num_active_steps = _num_selected_steps;
                }
                _seq_played_notes.clear();
                _step_seq_state = StepSeqState::START_PLAYING;

                // Return TRUE to indicate we are starting playing
                _prev_run = _run;
                _prev_seq_mode = _seq_mode;
                return true;     
            }
            else
                _step_seq_state = StepSeqState::IDLE;
            _prev_run = _run;
        }

        // Run the state machine
        switch (_step_seq_state)
        {
        case StepSeqState::IDLE:
            // In this state, the sequencer is idle and waiting to either be played or
            // programmed
            // If not already idle
            if (prev_seq_state != StepSeqState::IDLE) {
                // The Sequencer has just been made idle
                // Play any note-offs to finish the Sequencer
                _stop_seq();
            }
            break;

        case StepSeqState::PROGRAMMING:
            // In this mode we are waiting for keys to be pressed for each step
            // If not already in programming mode
            if (prev_seq_state != StepSeqState::PROGRAMMING) {
                // We are entering programming mode
                // Play any note-offs to finish the Sequencer
                _stop_seq();

                // Reset the sequencer step count
                _num_steps = 0;
                _step_note_index = 0;
                _base_note = 0xFF;
                _tie_start_step = -1;
                _tie_end_step = -1;
                _reset_seq_steps();
                _seq_played_notes.clear();
                _set_multifn_switch(_num_steps, true);
            }
            break;

        case StepSeqState::START_PLAYING:
            // Wait for a base note to be set
            if (_hold || (_seq_played_notes.size() > 0)) {
                // In this mode we are starting to play the sequence
                // Reset the step sequence and current note
                _step = 0;

                // Get the next Sequencer note to play and play it
                _send_step_seq_note();

                // Change the state to indicate we are now playing a note-on
                _step_seq_state = StepSeqState::PLAYING_NOTEON;
            }
            break;        

        case StepSeqState::PLAYING_NOTEON:
            // In this state, we have just played a note-on, and now need to
            // play a note-off
            _stop_step_seq_note();

            // If we are not in hold mode, just play the sequence once
            if ((!_hold) && (_step == _num_active_steps) && (_seq_played_notes.size() == 0)) {
                // Enter the playing last note-off state
                _step_seq_state = StepSeqState::PLAYING_LAST_NOTEOFF;
            }
            else {
                // Change the state to indicate we are now playing a note-off
                _step_seq_state = StepSeqState::PLAYING_NOTEOFF;
            }
            break;

        case StepSeqState::PLAYING_NOTEOFF:
            // In this state, we have just played a note-off, and now need to
            // play the next note
            _send_step_seq_note();

            // Change the state to indicate we are now playing a note-on
            _step_seq_state = StepSeqState::PLAYING_NOTEON;
            break;

        case StepSeqState::PLAYING_LAST_NOTEOFF:
            // After this state, return to the start playing state
            _step_seq_state = StepSeqState::START_PLAYING;
            break;
        }
    }
    else {
        LooperSeqState prev_seq_state = _looper_seq_state;

        // Has the REC state changed?
        if (_prev_rec != _rec) {
            // Entering REC mode?
            if (_rec) {
                // Should we also start playing the current sequence?
                if (_run) {
                    // If already playing just leave the state as-is
                    if (_looper_seq_state == LooperSeqState::IDLE) {
                        _looper_seq_state = LooperSeqState::START_PLAYING;
                    }
                    _overdub = true;
                    _overdub_started = false;
                }
                else {
                    // Starting a new sequence so reset the clock pulse count
                    _clock_pulse_count = (PPQN - 1);
                    _phrase_metronome_accent_pulses = 0;
                }
                _phrase_seq_note_on_events.clear();
            }
            else {
                // Leaving REC mode - should we also start playing the 
                // current sequence?
                if (_run) {
                    // If already playing just leave the state as-is
                    if (_looper_seq_state == LooperSeqState::IDLE) {
                        _looper_seq_state = LooperSeqState::START_PLAYING;
                    }                    
                }
                else {
                    _looper_seq_state = LooperSeqState::IDLE;
                }                    
            }
        }
        // Has the RUN mode changed?
        else if (_prev_run != _run) {
            // Start or stop playing the sequence
            _run && _phrase_seq_events.size() ?
                _looper_seq_state = LooperSeqState::START_PLAYING :
                _looper_seq_state = LooperSeqState::IDLE;
            
            // If we're also recording AND there are notes, enter overdub mode
            if (_rec && (_phrase_seq_events.size() > 0)) {
                _overdub = true;
                _overdub_started = false;
                _phrase_seq_note_on_events.clear();
            }
        }
        _prev_rec = _rec;
        _prev_run = _run;

        // Run the state machine
        switch (_looper_seq_state)
        {
        case LooperSeqState::IDLE:
            // In this state, the sequencer is idle and waiting to either be played or
            // programmed
            // If not already idle
            if (prev_seq_state != LooperSeqState::IDLE) {
                // The Sequencer has just been made idle
                // Play any note-offs to finish the Sequencer
                _stop_seq();
            }
            break;

        case LooperSeqState::START_PLAYING:
            // Start playing the notes in the sequencer from the
            // beginning of the existing sequence
            _phrase_seq_events_itr = _phrase_seq_events.begin();
            _ticks = 1;

            // Change the state to indicate we are now playing a sequence
            _looper_seq_state = LooperSeqState::PLAYING;

            // Fall-thru to PLAYING to play all the notes at tick 1

        case LooperSeqState::PLAYING:
            // Play all notes that are less than the current ticks count
            while (true) {
                // If we have reached the end of the phrase
                if (_phrase_seq_events_itr == _phrase_seq_events.end()) {
                    // Re-start the sequence on the next tick
                    _phrase_seq_events_itr = _phrase_seq_events.begin();
                    _ticks = 0;

                    // If overdub was performed during this phrase loop AND we are quantising, we need to sort the notes for
                    // the newly added overdub notes
                    if (_overdub_started && (_phrase_qnt != PhraseQuantisation::NONE)) {
                        std::sort(_phrase_seq_events.begin(), _phrase_seq_events.end(), [ ]( const PhraseSeqEvent& a, const PhraseSeqEvent& b) {return a.qnt_ticks < b.qnt_ticks;});
                        _overdub_started = false;
                    }
                    break;
                }

                // Should we use the quantised tick events?
                uint32_t ticks = _phrase_qnt == PhraseQuantisation::NONE ? _phrase_seq_events_itr->ticks : _phrase_seq_events_itr->qnt_ticks;

                // Check if this event should be processed
                if (ticks <= _ticks) {
                    // Play this note and move to the next event to check
                    if (_phrase_seq_events_itr->type == PhraseSeqEventType::NOTE) {
                        // Play the note
                        _send_seq_note(_phrase_seq_events_itr->note_event);
                    }
                    _phrase_seq_events_itr++;
                }
                else {
                    // All notes less than the current ticks count have been played
                    break;
                }                
            }
            break;
        }
    }
    _prev_seq_mode = _seq_mode;
    return false;
#pragma GCC diagnostic pop     
}

//----------------------------------------------------------------------------
// _clear_idle_sent_notes
//----------------------------------------------------------------------------
void SeqManager::_clear_idle_sent_notes()
{
    // If there are any sent notes in the idle state, play each note OFF
    if (_idle_sent_notes.size() > 0) {
        // Play each note-off
        for (uint i=0; i<_idle_sent_notes.size(); i++) {
            auto note_off = _idle_sent_notes[i];
            note_off.type = snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF;
            note_off.data.note.velocity = 0;
            _send_note(note_off);
        }
        _idle_sent_notes.clear();
    }
}

//----------------------------------------------------------------------------
// _send_step_seq_note
//----------------------------------------------------------------------------
void SeqManager::_send_step_seq_note()
{
  // Wrap the step if it has reached the maximum
    if (_step >= _num_active_steps) {
        _step = 0;

        // If the step counter has wrapped, then send note offs for all held notes so that ties arn't left hanging
        if (_sent_notes.size() > 0) {
            // Play each note-off
            for (uint i = 0; i < _sent_notes.size(); i++) {
                auto note_off = _sent_notes[i];
                note_off.type = snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF;
                note_off.data.note.velocity = 0;
                note_off.data.note.channel = _current_midi_channel;
                _send_note(note_off);
            }
            _sent_notes.clear();
        }
  }

    // If the tie state is normal or start tie, then play the notes ON
    // normally
    if (_seq_steps[_step].step_type <= StepType::START_TIE) {
        // Get the next Sequencer notes to play and send them
        _sent_notes.clear();
        if (_seq_steps[_step].num_notes > 0) {
            // Play each note-on
            for (uint i=0; i<(uint)_seq_steps[_step].num_notes; i++) {
                auto note_on = _seq_steps[_step].notes[i];
                note_on.data.note.note += _base_note;
                if (note_on.data.note.note > MAX_MIDI_NOTE)
                    note_on.data.note.note = MAX_MIDI_NOTE;
                note_on.data.note.velocity = 127;      
                note_on.data.note.channel = _current_midi_channel;
                _send_note(note_on);
                _sent_notes.push_back(note_on);
            }
        }
    }
}

//----------------------------------------------------------------------------
// _stop_step_seq_note
//----------------------------------------------------------------------------
void SeqManager::_stop_step_seq_note()
{
    // If the tie state is either none or end tie, play all the recently played 
    // notes, except as a note-off with 0 velocity
    if ((_seq_steps[_step].step_type == StepType::NORMAL) || 
        (_seq_steps[_step].step_type == StepType::END_TIE)) {
        // Send the note OFF for each note
        _stop_seq();
    }
    _step++;    
}

//----------------------------------------------------------------------------
// _send_seq_note
//----------------------------------------------------------------------------
void SeqManager::_send_seq_note(const snd_seq_event_t &ev)
{
    // If this is a NOTE ON
    if (ev.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) {
        // Add to the played notes queue
        _seq_played_notes.push_back(ev.data.note.note);
    }
    else {
        // If it is in the played notes array, remove it
        for (auto it = _seq_played_notes.begin(); it != _seq_played_notes.end(); it++) {
            // Erase the note if found
            if (*it == ev.data.note.note) {
                _seq_played_notes.erase(it);
                break;
            }
        }        
    }

    // Send the note
    _send_note(ev);
}

//----------------------------------------------------------------------------
// _stop_seq
//----------------------------------------------------------------------------
void SeqManager::_stop_seq()
{
    SeqMode& seq_mode = _prev_seq_mode == _seq_mode ? _seq_mode : _prev_seq_mode;

    // Check the Sequencer mode
    if (seq_mode == SeqMode::STEP) {
        // If there are any sent notes, play each note OFF
        if (_sent_notes.size() > 0) {
            // Play each note-off
            for (uint i=0; i<_sent_notes.size(); i++) {
                auto note_off = _sent_notes[i];
                note_off.type = snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF;
                note_off.data.note.velocity = 0;
                note_off.data.note.channel = _current_midi_channel;
                _send_note(note_off);
            }
            _sent_notes.clear();
        }        
    }
    else {
        // If there are any sequencer played notes, play each note OFF
        if (_seq_played_notes.size() > 0) {
            // Play each note-off
            for (uint i=0; i<_seq_played_notes.size(); i++) {
                snd_seq_event_t note_off;
                note_off.type = snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF;
                note_off.data.note.note = _seq_played_notes[i];
                note_off.data.note.velocity = 0;
                note_off.data.note.channel = _current_midi_channel;
                _send_note(note_off);
            }
            _seq_played_notes.clear();
        }
    }
}

//----------------------------------------------------------------------------
// _send_note
//----------------------------------------------------------------------------
void SeqManager::_send_note(const snd_seq_event_t &note)
{
    //if (note.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON)
    //    DEBUG_BASEMGR_MSG("Send note: " << (int)note.data.note.note << ": ON");
    //else
    //    DEBUG_BASEMGR_MSG("Send note: " << (int)note.data.note.note << ": OFF");

    // Send the note directly to the Arpeggiator
    utils::get_manager(MoniqueModule::ARP)->process_midi_event_direct(&note);
}

//----------------------------------------------------------------------------
// _set_sys_func_switch
//----------------------------------------------------------------------------
void SeqManager::_set_sys_func_switch(SystemFuncType system_func_type, bool set)
{
    // Get the switch associated with the system function and set ut
    auto param = utils::get_param(SystemFuncParam::ParamPath(system_func_type).c_str());
    if (param) {
        auto mapped_params = param->mapped_params(nullptr);
        for (Param *mp : mapped_params) {
            auto sfc_func = SfcFunc(SfcFuncType::SET_SWITCH_VALUE, MoniqueModule::SEQ);
            sfc_func.param = static_cast<SfcControlParam *>(utils::get_param(mp->path()));
            sfc_func.switch_value = set;
            _event_router->post_sfc_func_event(new SfcFuncEvent(sfc_func));
        }
    }
}

//----------------------------------------------------------------------------
// _select_multifn_switch
//----------------------------------------------------------------------------
void SeqManager::_set_multifn_switch(uint index, bool reset_other_switches)
{
    // If we're recording and the multi-function switches are set to SEQ REC state
    if (_rec && utils::multifn_switches_state() == utils::MultifnSwitchesState::SEQ_REC) {
        // If the index is within range
        if (index < STEP_SEQ_MAX_STEPS) {
            // Select the specified multi-function key
            utils::select_multifn_switch(index, _event_router, reset_other_switches);
        }
    }
}

//----------------------------------------------------------------------------
// _register_params
//----------------------------------------------------------------------------
void SeqManager::_register_params()
{
    // Register the Mode param
    auto param1 = Param::CreateParam(this, MODE_PARAM_ID, MODE_PARAM_NAME, MODE_DISPLAY_NAME);
    param1->set_type(ParamType::PRESET_COMMON);
    param1->set_hr_value((uint)SeqMode::STEP);
    utils::register_param(std::move(param1));
    
    // Register the Record param
    auto param2 = Param::CreateParam(this, REC_PARAM_ID, REC_PARAM_NAME, REC_DISPLAY_NAME);
    param2->set_type(ParamType::PRESET_COMMON);
    param2->set_preset(false);
    param2->set_hr_value(_rec);
    utils::register_param(std::move(param2));

    // Register the Run param
    auto param3 = Param::CreateParam(this, RUN_PARAM_ID, RUN_PARAM_NAME, RUN_DISPLAY_NAME);
    param3->set_type(ParamType::PRESET_COMMON);
    param3->set_preset(false);
    param3->set_hr_value(_run);
    utils::register_param(std::move(param3));

    // Register the Number of Steps param
    auto param4 = Param::CreateParam(this, NUM_STEPS_PARAM_ID, NUM_STEPS_PARAM_NAME, NUM_STEPS_DISPLAY_NAME);
    param4->set_type(ParamType::PRESET_COMMON);
    param4->set_hr_value(1.0f);
    utils::register_param(std::move(param4));

    // Register the Tempo Note Value param
    auto param5 = Param::CreateParam(this, TEMPO_NOTE_VALUE_PARAM_ID, TEMPO_NOTE_VALUE_PARAM_NAME, TEMPO_NOTE_VALUE_DISPLAY_NAME);
    param5->set_type(ParamType::PRESET_COMMON);
    param5->set_hr_value(1.0f);
    utils::register_param(std::move(param5));

	// Register the Hold param
    auto param6 = Param::CreateParam(this, HOLD_PARAM_ID, HOLD_PARAM_NAME, HOLD_DISPLAY_NAME);
    param6->set_type(ParamType::PRESET_COMMON);
    param6->set_hr_value(true);
    utils::register_param(std::move(param6));

    // Register the Quantise Notes param
    auto param7 = Param::CreateParam(this, PHRASE_QUANTISATION_PARAM_ID, PHRASE_QUANTISATION_PARAM_NAME, PHRASE_QUANTISATION_DISPLAY_NAME);
    param7->set_type(ParamType::PRESET_COMMON);
    param7->set_hr_value((uint)PhraseQuantisation::NONE);
    utils::register_param(std::move(param7));

    // Register the Phrase Looper Beats per Bar param
    auto param8 = Param::CreateParam(this, PHRASE_BEATS_PER_BAR_PARAM_ID, PHRASE_BEATS_PER_BAR_PARAM_NAME, PHRASE_BEATS_PER_BAR_DISPLAY_NAME);
    param8->set_type(ParamType::PRESET_COMMON);
    param8->set_hr_value((uint)PhraseBeatsPerBar::FOUR);
    utils::register_param(std::move(param8));

    // Register the step notes param
    for (uint i=0; i<STEP_SEQ_MAX_STEPS; i++) {
        auto param = Param::CreateParam(this, SeqParamId::STEP_1_ID + i,
                                        STEP_PARAM_NAME + std::to_string(i + 1), 
                                        STEP_DISPLAY_NAME + std::to_string(i + 1),
                                        ParamDataType::STRING);
        param->set_type(ParamType::PRESET_COMMON);
        param->set_str_value("00FFFFFFFFFFFFFFFFFFFFFFFF");
        utils::register_param(std::move(param));
    } 

    // Register the looper chunk params
    for (uint i=0; i<MAX_CHUNKS; i++) {
        auto param = Param::CreateParam(this, SeqParamId::CHUNK_PARAM_ID + i,
                                        CHUNK_PARAM_NAME + std::to_string(i + 1), 
                                        CHUNK_NAME + std::to_string(i + 1),
                                        ParamDataType::STRING);
        param->set_type(ParamType::PRESET_COMMON);
        param->set_as_seq_chunk_param();
        param->reset_seq_chunk_param();
        utils::register_param(std::move(param));
    }    
}

//----------------------------------------------------------------------------
// _reset_seq
//----------------------------------------------------------------------------
void SeqManager::_reset_seq()
{
    // Reset the step sequencer settings
    _num_steps = 0;
    _step_note_index = 0;
    _base_note = 0xFF;
    _tie_start_step = -1;
    _tie_end_step = -1;
    _reset_seq_steps();

    // Reset the phrase looper settings
    _reset_seq_phrase_looper();
}

//----------------------------------------------------------------------------
// _reset_seq_steps
//----------------------------------------------------------------------------
void SeqManager::_reset_seq_steps()
{
    // Reset the sequence steps
    for (uint i=0; i<STEP_SEQ_MAX_STEPS; i++) {
        _seq_steps.at(i).num_notes = -1;
        _seq_steps.at(i).step_type = StepType::NORMAL;
    }
}

//----------------------------------------------------------------------------
// _reset_seq_phrase_looper
//----------------------------------------------------------------------------
void SeqManager::_reset_seq_phrase_looper()
{
    // Reset the phrase looper settings
    _phrase_seq_events.clear();
    _ticks = 0;
    _end_of_phrase_ticks = 0;
    _clock_pulse_count = 0;
    _overdub = false;
    _overdub_started = false;
    _seq_played_notes.clear();
    _phrase_seq_note_on_events.clear();
}

//----------------------------------------------------------------------------
// _set_seq_step_param
//----------------------------------------------------------------------------
void SeqManager::_set_seq_step_param(uint step)
{
    // Get the sequence param
    auto param = utils::get_param(Param::ParamPath(this, STEP_PARAM_NAME + std::to_string(step + 1)));
    if (param) {
        std::string step_notes = "00FFFFFFFFFFFFFFFFFFFFFFFF";
        unsigned char attr = 0;
        char str_note[3];

        // If this is normal notes or the start of a tie
        if (_seq_steps[_num_steps].step_type <= StepType::START_TIE) {
            // Set the step attributes
            attr = ((_seq_steps[_num_steps].step_type == StepType::START_TIE) ? TIE_START_STEP : 0);
            std::sprintf(str_note, "%02X", (char)attr);
            step_notes.replace(0, 2, str_note);             

            // Process each note
            for (uint i=0; i<_step_note_index; i++) {
                unsigned char note = _seq_steps[_num_steps].notes[i].data.note.note;
                note += _base_note;
                if (note > MAX_MIDI_NOTE) {
                    note = MAX_MIDI_NOTE;
                }
                std::sprintf(str_note, "%02X", (char)note);
                step_notes.replace((2 + (i*2)), 2, str_note);                
            }
        }
        else {
            // If this is a tie step
            if (_seq_steps[_num_steps].step_type == StepType::TIE) {
                attr = TIE_STEP;
            }
            // If this is an end tie step
            else if (_seq_steps[_num_steps].step_type == StepType::END_TIE) {
                attr = TIE_END_STEP;
            }
            // If this is a rest ntoe
            else if (_seq_steps[_num_steps].step_type == StepType::REST) {
                attr = REST_STEP;
            }

            // Set the step attributes
            std::sprintf(str_note, "%02X", (char)attr);
            step_notes.replace(0, 2, str_note);  
        }
        param->set_str_value(step_notes);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
    }
}

//----------------------------------------------------------------------------
// _check_for_phrase_held_notes
//----------------------------------------------------------------------------
void SeqManager::_check_for_phrase_held_notes()
{
    // If there are any held notes when we stop recording, add NOTE OFF
    // events
    if (!_phrase_seq_note_on_events.empty()) {
        for (auto it = _phrase_seq_note_on_events.begin(); it != _phrase_seq_note_on_events.end(); ++it) {
            // Add a NOTE OFF event for this note
            auto ev = PhraseSeqEvent();
            ev.type = PhraseSeqEventType::NOTE;
            ev.note_event = (*it).note_event;
            ev.note_event.type = snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF;
            ev.note_event.data.note.velocity = 0;
            ev.ticks = _ticks;
            ev.qnt_ticks = _ticks;
            _phrase_seq_events.push_back(ev);
            _quantise_note_off_event(it, _phrase_seq_events.end() - 1);
            DEBUG_MSG("Added held NOTE OFF: " << (uint)ev.note_event.data.note.note  << " : " << ev.ticks);     
        }
        _phrase_seq_note_on_events.clear();
    }
}

//----------------------------------------------------------------------------
// _append_end_phrase_event
//----------------------------------------------------------------------------
void SeqManager::_append_end_phrase_event()
{
    uint32_t ticks = 0;

    // Are we rounding to a bar?
    if (!_round_to_bar) {
        uint min_ticks = 0;

        // From the end of the phrase, find the last NOTE ON event, and round up to the nearest beat
        // This sets the MINIMUM bar length of the phrase
        for (auto itr=_phrase_seq_events.rbegin(); itr != _phrase_seq_events.rend(); ++itr) {
            // If this is a NOTE ON event
            if ((itr->type == PhraseSeqEventType::NOTE) && (itr->note_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON)) {
                // Calculate the minimum phrase length - rounded UP to the next beat
                min_ticks = _quantise_ticks(itr->ticks, PPQN, true) - 1;
                break;
            }
        }

        // Now calculate the actual end of the phrase - either the minimum phrase length, or the
        // actual phrase length (when recording was stopped) rounded up/down
        uint actual_ticks = _quantise_ticks(_ticks, PPQN) - 1;
        ticks = actual_ticks < min_ticks ? min_ticks : actual_ticks;

        // Make sure any NOTE OFF events at the end of the phrase are truncated if needed
        for (auto itr=_phrase_seq_events.rbegin(); itr != _phrase_seq_events.rend(); ++itr) {
            // If this is a NOTE event
            if (itr->type == PhraseSeqEventType::NOTE) {
                // If this is a NOTE ON, stop searching for NOTE OFF events
                if (itr->note_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) {
                    break;
                }

                // If this NOTE OFF exceeds the current end of phrase ticks, truncate it
                if (itr->ticks > ticks) {
                    itr->ticks = ticks;
                }
                else {
                    // NOTE OFF is within the phrase, stop searching
                    break;
                }
            }
        }
    }
    else {
        // Get the last event in the phrase, assumed to be a NOTE OFF    
        auto itr = _phrase_seq_events.end() - 1;

        // Calculate the ticks in a bar
        uint bar_ticks = _round_bar_count * PPQN;

        // Round up to the end of the next bar
        uint bars = itr->ticks / bar_ticks;
        if (itr->ticks % bar_ticks) {
            // From the end of the phrase, find the last NOTE ON event
            // This sets the MINIMUM bar length of the phrase
            for (auto itr=_phrase_seq_events.rbegin(); itr != _phrase_seq_events.rend(); ++itr) {
                // If this is a NOTE ON event
                if ((itr->type == PhraseSeqEventType::NOTE) && (itr->note_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON)) {
                    // Calculate the minimum phrase length
                    uint bars = itr->ticks / bar_ticks;
                    if (itr->ticks % bar_ticks) {
                        bars++;
                    }
                    ticks = (bars * bar_ticks);
                    break;
                }
            }

            // Now calculate the mid-point of the next bar if we rounded up
            auto rounded_bar_half_ticks = ticks + (bar_ticks / 2);

            // The last event is assumed to be a NOTE OFF event
            // Check if this exceeds the mid point of the next bar if we rounded up
            if (itr->ticks > rounded_bar_half_ticks) {
                // It exceeds the mid-point so we need to round up the bar based on this NOTE OFF event
                // Calculate the new phrase length
                uint bars = itr->ticks / bar_ticks;
                if (itr->ticks % bar_ticks) {
                    bars++;
                }
                ticks = (bars * bar_ticks);
            }
            else {
                // It does not exceed the mid point of the next bar, so use the minimum phrase length
                // as the actual phrase length
                // Because of this we need to make sure any NOTE OFF events at the end of the phrase are
                // truncated to the minimum phrase length
                for (auto itr=_phrase_seq_events.rbegin(); itr != _phrase_seq_events.rend(); ++itr) {
                    // If this is a NOTE event
                    if (itr->type == PhraseSeqEventType::NOTE) {
                        // If this is a NOTE ON, stop searching for NOTE OFF events
                        if (itr->note_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) {
                            break;
                        }

                        // If this NOTE OFF exceeds the current end of phrase ticks, truncate it
                        if (itr->ticks > ticks) {
                            itr->ticks = ticks;
                        }
                        else {
                            // NOTE OFF is within the phrase, stop searching
                            break;
                        }
                    }
                }
            }
        }
        else {
            // The last NOTE OFF is exactly at the end of a bar, so no rounding checks are needed
            ticks = (bars * bar_ticks);
        }
    }

    // Append the end of phrase event
    auto ev = PhraseSeqEvent();
    ev.type = PhraseSeqEventType::END_OF_PHRASE;
    ev.ticks = ticks;
    ev.qnt_ticks = ticks;
    _phrase_seq_events.push_back(ev);
    _end_of_phrase_ticks = ticks;

    // Re-quantise the initial phrase
    // We need to do this to make sure any quantised notes that exceed
    // the end of phrase are handled correctly, by wrapping around to the
    // start of the sequence and honouring the note duration
    _quantise_seq();
}

//----------------------------------------------------------------------------
// _sort_phrase_seq
//----------------------------------------------------------------------------
void SeqManager::_sort_phrase_seq()
{
    // Sort the sequence depending on whether quantisation is enabled or not
    _phrase_qnt == PhraseQuantisation::NONE ?
        std::sort(_phrase_seq_events.begin(), _phrase_seq_events.end(), [ ]( const PhraseSeqEvent& a, const PhraseSeqEvent& b) {return a.ticks < b.ticks;}) :
        std::sort(_phrase_seq_events.begin(), _phrase_seq_events.end(), [ ]( const PhraseSeqEvent& a, const PhraseSeqEvent& b) {return a.qnt_ticks < b.qnt_ticks;});
}

//----------------------------------------------------------------------------
// _load_seq_chunks
//----------------------------------------------------------------------------
void SeqManager::_load_seq_chunks()
{
    // Parse each Sequencer chunk until an undefined note is found
    for (uint i=0; i<MAX_CHUNKS; i++) {
        // Get the Sequencer Chunk param
        auto param = utils::get_param(MoniqueModule::SEQ, SeqParamId::CHUNK_PARAM_ID + i);
        if (param) {
            // Parse all notes in the chunk
            auto chunk = param->str_value();
            for (uint j=0; j<CHUNK_MAX_NOTES; j++) {
                // Extract the ticks and channel
                // Check if the channel is set to the end of phrase indicator                
                auto ticks_str = chunk.substr((j * 7 * 2), 8);
                auto channel_str = chunk.substr(((j * 7 * 2) + 8), 2);
                auto ticks = std::stoi(ticks_str, nullptr, 16);
                auto channel = std::stoi(channel_str, nullptr, 16);
                if (channel == END_OF_PHRASE_INDICATOR) {
                    // Add the end of phrase event, and stop processing chunks
                    auto ev = PhraseSeqEvent();
                    ev.type = PhraseSeqEventType::END_OF_PHRASE;
                    ev.ticks = ticks;
                    ev.qnt_ticks = ticks;
                    _phrase_seq_events.push_back(ev);
                    _end_of_phrase_ticks = ticks;
                    return;                   
                }
                else {
                    // Extract the note and velocity
                    auto note_str = chunk.substr(((j * 7 * 2) + 10), 2);
                    auto vel_str = chunk.substr(((j * 7 * 2) + 12), 2);
                    auto note = std::stoi(note_str, nullptr, 16);
                    auto vel = std::stoi(vel_str, nullptr, 16);

                    // If this is a valid note definition
                    if ((uint)note <= MIDI_MAX_NOTE) {
                        // Create the sequencer note event
                        snd_seq_event_t note_event;
                        note_event.type = vel ? snd_seq_event_type::SND_SEQ_EVENT_NOTEON : snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF;
                        note_event.data.note.channel = channel;
                        note_event.data.note.note = note;
                        note_event.data.note.velocity = vel;

                        // Add it to the Sequencer events
                        auto ev = PhraseSeqEvent();
                        ev.type = PhraseSeqEventType::NOTE;
                        ev.note_event = note_event;
                        ev.ticks = ticks;
                        ev.qnt_ticks = ticks;
                        _phrase_seq_events.push_back(ev);
                        //DEBUG_MSG("Loaded note: " << (uint)note_event.data.note.note  << " : " << ticks);  
                    }
                    else {
                        // Note is invalid, stop processing chunks
                        return;
                    }
                }
            }
        }
    }  
}

//----------------------------------------------------------------------------
// _save_seq_chunks
//----------------------------------------------------------------------------
void SeqManager::_save_seq_chunks(const std::vector<PhraseSeqEvent>& seq)
{
    std::string chunk_notes = utils::seq_chunk_param_reset_value();
    char str_event[(7 * 2) + 1];
    uint chunk_index = 0;
    uint chunk_note_index = 0;

    // Get the first chunk param
    auto param = utils::get_param(MoniqueModule::SEQ, SeqParamId::CHUNK_PARAM_ID + chunk_index);
    if (param) {
        // Iterate all events
        for (auto itr=seq.begin(); itr!=seq.end(); ++itr) {
            // Is this an end of phrase event?
            if (itr->type == PhraseSeqEventType::END_OF_PHRASE) {
                // Create the event definition as a string
                std::sprintf(str_event, "%08X%02XFFFF", itr->ticks, END_OF_PHRASE_INDICATOR);

                // Add it to the current chunk
                chunk_notes.replace((chunk_note_index * 7 * 2), (7 * 2), str_event);

                // Finished processing chunks, send a param change
                param->set_str_value(chunk_notes);
                auto param_change = ParamChange(param, module());
                param_change.display = false;
                _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                return;                
            }
            else {
                // Create the note definition as a string
                std::sprintf(str_event, "%08X%02X%02X%02X", itr->ticks, itr->note_event.data.note.channel, itr->note_event.data.note.note, itr->note_event.data.note.velocity);

                // Add it to the current chunk
                chunk_notes.replace((chunk_note_index * 7 * 2), (7 * 2), str_event);
                chunk_note_index++;

                // Is the chunk full?
                if (chunk_note_index >= CHUNK_MAX_NOTES) {
                    // The chunk is full, send a param change
                    param->set_str_value(chunk_notes);
                    auto param_change = ParamChange(param, module());
                    param_change.display = false;
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

                    // Increment the chunk index and check if there are chunks free to process
                    chunk_index++;
                    if (chunk_index >= MAX_CHUNKS) {
                        // No more chunks to process so stop processing Sequencer notes
                        break;
                    }

                    // Get the next chunk to process
                    param = utils::get_param(MoniqueModule::SEQ, SeqParamId::CHUNK_PARAM_ID + chunk_index);
                    if (!param) {
                        // Chunk invalid, should never happen
                        return;
                    }

                    // Reset the chunk note index
                    chunk_note_index = 0;
                    chunk_notes = utils::seq_chunk_param_reset_value();
                }
            }
        }

        // Processed all Sequencer notes - is there a partially processed chunk to send?
        if (chunk_note_index) {
            // Send a param change for the final chunk
            param->set_str_value(chunk_notes);
            auto param_change = ParamChange(param, module());
            param_change.display = false;
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));            
        }
    }
}    

//----------------------------------------------------------------------------
// _quantise_seq
//----------------------------------------------------------------------------
void SeqManager::_quantise_seq()
{
    // Before we quantise the sequence, make sure it is in a non-quantised order
    std::sort(_phrase_seq_events.begin(), _phrase_seq_events.end(), [ ]( const PhraseSeqEvent& a, const PhraseSeqEvent& b) {return a.ticks < b.ticks;});
    
    // Go through the entire phrase sequence, quantising each note to the current note value
    _phrase_seq_note_on_events.clear();
     for (auto itr=_phrase_seq_events.begin(); itr!=_phrase_seq_events.end(); ++itr) {
        // Quantise the note event
        _quantise_note_event(itr);
     }

    // Now re-sort the sequence, if needed
    if (_phrase_qnt != PhraseQuantisation::NONE) {
        _sort_phrase_seq();
    }

     // Make sure the NOTE ON events vector is cleared
     _phrase_seq_note_on_events.clear(); 
}

//----------------------------------------------------------------------------
// _quantise_note_event
//----------------------------------------------------------------------------
void SeqManager::_quantise_note_event(std::vector<PhraseSeqEvent>::iterator itr)
{
    // Is this a NOTE ON event?
    if (itr->note_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) {
        // Quantise the NOTE ON event
        _quantise_note_on_event(itr);

        // Add to the NOTE ON events vector
        _phrase_seq_note_on_events.push_back(*itr);
    }
    else {
        // Find the equivalent event in the NOTE ON events vector
        for (auto itr2 = _phrase_seq_note_on_events.begin(); itr2 != _phrase_seq_note_on_events.end(); itr2++) {
            // Does this note match the NOTE OFF?
            if ((itr2->note_event.data.note.note == itr->note_event.data.note.note) &&
                (itr2->note_event.data.note.channel == itr->note_event.data.note.channel)) {
                // Quantise the NOTE OFF event
                _quantise_note_off_event(itr2, itr);

                // Remove the from the NOTE ON events
                _phrase_seq_note_on_events.erase(itr2);
                break;
            }
        }                        
    }
}

//----------------------------------------------------------------------------
// _quantise_note_on_event
//----------------------------------------------------------------------------
void SeqManager::_quantise_note_on_event(std::vector<PhraseSeqEvent>::iterator note_on_event_itr)
{
    // Calculate the quantised ticks
    note_on_event_itr->qnt_ticks = _quantise_ticks(note_on_event_itr->ticks);

    // If a phrase has been defined, and the quantised ticks have moved past the end
    // of the phrase, wrap it around to the start
    if (_end_of_phrase_ticks && (note_on_event_itr->qnt_ticks >= _end_of_phrase_ticks)) {
        note_on_event_itr->qnt_ticks = 1;
    }
}

//----------------------------------------------------------------------------
// _quantise_note_off_event
//----------------------------------------------------------------------------
void SeqManager::_quantise_note_off_event(std::vector<PhraseSeqEvent>::iterator note_on_event_itr, std::vector<PhraseSeqEvent>::iterator note_off_event_itr)
{
    // Calculate the quantised note off ticks - firstly calculate the note duration
    uint32_t note_dur;
    if (note_on_event_itr->ticks <= note_off_event_itr->ticks) {
        note_dur = note_off_event_itr->ticks - note_on_event_itr->ticks;
    }
    else {
        note_dur = (_end_of_phrase_ticks - note_on_event_itr->ticks) + note_off_event_itr->ticks - 1;
    }

    // Calculate the quantised note off ticks, wrap around if the phrase exists and
    // it exceeds the phrase duration
    uint32_t qnt_ticks = note_on_event_itr->qnt_ticks + note_dur;
    if (_end_of_phrase_ticks && (qnt_ticks > _end_of_phrase_ticks)) {
        qnt_ticks = note_dur - (_end_of_phrase_ticks - note_on_event_itr->qnt_ticks) + 1;
    }
    note_off_event_itr->qnt_ticks = qnt_ticks;
}

//----------------------------------------------------------------------------
// _quantise_ticks
//----------------------------------------------------------------------------
inline uint32_t SeqManager::_quantise_ticks(uint32_t ticks)
{
    return _quantise_ticks(ticks, _phrase_qnt_pulse_count);
}

//----------------------------------------------------------------------------
// _quantise_ticks
//----------------------------------------------------------------------------
inline uint32_t SeqManager::_quantise_ticks(uint32_t ticks, uint phrase_note_pulse_count, bool round_up)
{
    auto rem = (ticks % phrase_note_pulse_count);
    return (float)rem < (phrase_note_pulse_count / 2.f) && !round_up ?
                ticks - rem + 1:
                ticks + (phrase_note_pulse_count - rem) + 1;
}
