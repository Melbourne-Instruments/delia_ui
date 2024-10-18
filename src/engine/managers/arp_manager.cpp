/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  arp_manager.h
 * @brief Arpeggiator Manager class implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <unistd.h>
#include "arp_manager.h"
#include "daw_manager.h"
#include "utils.h"

// Constants
constexpr char DIR_MODE_PARAM_NAME[]   = "dirmode";
constexpr char DIR_MODE_DISPLAY_NAME[] = "Direction Mode";
constexpr char RUN_PARAM_NAME[]        = "run";
constexpr char RUN_DISPLAY_NAME[]      = "Run";
constexpr uint HOLD_TIMEOUT            = 1000;
constexpr uint MAX_ARP_NOTES		   = 100;

//----------------------------------------------------------------------------
// ArpManager
//----------------------------------------------------------------------------
ArpManager::ArpManager(EventRouter *event_router) : 
    BaseManager(MoniqueModule::ARP, "ArpManager", event_router)             
{
    // Initialise class data
    _param_changed_listener = 0;
    _reload_presets_listener = 0;
    _tempo_event_thread = 0;
    _fsm_running = true;
    _tempo_pulse_count = utils::tempo_pulse_count(common::TempoNoteValue::QUARTER);
    _note_duration_pulse_count = _tempo_pulse_count >> 1;
    _pulse_count = 0;
    _enable = false;
    _hold = false;
    _dir_mode = ArpDirMode::UP;
    _updown_dir_mode = ArpDirMode::UP;
    _started = false;
    _prev_enable = false;
    _prev_started = false;
    _arp_state = ArpState::DISABLED;
    _arp_notes.reserve(MAX_ARP_NOTES);
    _step = 0;
    _hold_timeout = true;
    _midi_clk_in = false;

    // Register the Arp params
    _register_params();

    // Seed the random number generator
    std::srand(std::time(nullptr));    
}

//----------------------------------------------------------------------------
// ~ArpManager
//----------------------------------------------------------------------------
ArpManager::~ArpManager()
{
	// Stop the FSM
	{
		std::lock_guard<std::mutex> lk(utils::arp_mutex());
		_fsm_running = false;
	}
	utils::arp_signal_without_lock();
	
	// Stop the tempo event thread
	if (_tempo_event_thread)
	{
		// Wait for the tempo event thread to finish and delete it
		if (_tempo_event_thread->joinable())
			_tempo_event_thread->join();
		delete _tempo_event_thread;
		_tempo_event_thread = 0;
	}

    // Stop the Arpegiator Hold timer task

    // Clean up the event listeners
    if (_param_changed_listener)
        delete _param_changed_listener;
    if (_reload_presets_listener)
        delete _reload_presets_listener;
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool ArpManager::start()
{
    // Before starting the Arpeggiator, process all the preset values
    _process_presets();

	// Start the tempo event thread
	_tempo_event_thread = new std::thread(&ArpManager::_process_tempo_event, this);

    // Call the base manager
    return BaseManager::start();	
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void ArpManager::process()
{
    // Create and add the various event listeners
    _param_changed_listener = new EventListener(MoniqueModule::SYSTEM, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_param_changed_listener);	
    _reload_presets_listener = new EventListener(MoniqueModule::FILE_MANAGER, EventType::RELOAD_PRESETS, this);
    _event_router->register_event_listener(_reload_presets_listener);

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void ArpManager::process_event(const BaseEvent *event)
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

        default:
            // Event unknown, we can ignore it
            break;
    }
}

//----------------------------------------------------------------------------
// process_midi_event_direct
//----------------------------------------------------------------------------
void ArpManager::process_midi_event_direct(const snd_seq_event_t *event)
{
    auto data = *event;

    // Get the Arpeggiator mutex
    std::unique_lock<std::mutex> lock(utils::arp_mutex());

    // If the msg is a key pressure event, then just forward it
    if (data.type == snd_seq_event_type::SND_SEQ_EVENT_KEYPRESS) {
       _send_note(data); 
       return;
    }

    // Is the Arpeggiator enabled?
    if (!_enable)
    {
        // Arpeggiator is bypassed, just forward the note to the DAW if this is a note-on
        // or note-off
        if ((data.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF) ||
            (data.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON))
        {
            // Send the note to the DAW
            _send_note(data);

            // Also add/remove the notes from the arps array, this means that if notes are held before turing on the arp, it will play them
            if ((data.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (data.data.note.velocity != 0))
            {
                _add_arp_note(data);
            }
            else
            {
                _remove_arp_note(data);
            }
        }
    }
    else
    {
        // The Arpeggiator is enabled
        // Is this a note-off?
        if ((data.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF) ||
            (data.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON && data.data.note.velocity == 0))
        {
           //erase notes that arn't held anymore. then reevalute hold reset.  
            for (auto i = _held_notes.begin(); i < _held_notes.end(); i++)
            {
                // Does this note match the passed note?
                if (data.data.note.note == i->data.note.note)
                {
                    // Matches - remove it
                    _held_notes.erase(i);

                }
            }
            _hold_reset = _held_notes.size() ==0;

            // Are we in hold mode? If so, don't remove the notes from the Arpeggiator notes array
            if (!_hold)
            {
                // Try to remove the note from the array of Arpeggiator notes
                if (!_remove_arp_note(data))
                {
                    // If it can't be removed (isn't known) then just forward to the DAW
                    // We do this as it could be a note-off from a keypress before the Arpeggiator
                    // was enabled
                    _send_note(data);
                }

                // If there are no more notes left to play
                if (_arp_notes.size() == 0) {
                    // Process the FSM immediately (don't reset the pulse count)
                    _process_fsm_async_event(false);
                }                
            }
            else
            {
                // In hold mode - does the note exist in the array of Arpeggiator notes?
                if (!_find_arp_note(data))
                {
                    // If the note isn't known, then just forward to the DAW
                    // We do this as it could be a note-off from a keypress before the Arpeggiator
                    // was enabled
                    _send_note(data);
                }
            }
        }
        // Is this a note-on?
        else if (data.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON)
        {
            _held_notes.push_back(data);
            // Are we in hold mode?
            if (_hold)
            {
                //have all the held notes been released
                if (_hold_reset)
                {
                    // Clear the array of Arpeggiator notes
                    _arp_notes.clear();
                }
            }

            // Add it to the array of Arpeggiator notes
            _add_arp_note(data);

            // If the Arpeggiator state is idle, wake it up immediately to start processing the note
            if (_arp_state == ArpState::IDLE)
            {
                // Process the FSM immediately (and reset the pulse count)
                _process_fsm_async_event(true);
            }

            // Are we in hold mode?
            if (_hold)
            {
                // We need to re-start the hold timer
                // However, we don't want to do this with the mutex held for performance reasons
                lock.unlock();

                // Stop the hold timer (if not already stopped), and re-start it
                _hold_timeout = false;
            }
            //if there is a note on now, then we wont reset hold anymore
            _hold_reset = _held_notes.size() ==0;
        }
    }
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void ArpManager::_process_param_changed_event(const ParamChange &data)
{
    // If the param is valid
    if (data.param) {
        // Get the mutex lock
        std::lock_guard<std::mutex> lk(utils::arp_mutex());        

        // Process the param value
        _process_param_value(*static_cast<const Param *>(data.param));
    }
}

//----------------------------------------------------------------------------
// _process_presets
//----------------------------------------------------------------------------
void ArpManager::_process_presets()
{
    // Get the mutex lock
    std::lock_guard<std::mutex> lk(utils::arp_mutex());

    // Make sure the enable and hold params are cleared before processing, and
    // that any played notes are stopped
    _enable = false;
    _prev_enable = false;
    _hold = false;
    _arp_state = ArpState::DISABLED;
    _stop_arp_notes(); 
    _arp_notes.clear();
    _held_notes.clear();

    // Parse the common MIDI clock in param
    auto param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::MIDI_CLK_IN_PARAM_ID);
    if (param){
        _process_param_value(*param);					
    }

    // Parse the Arpeggiator params
    for (const Param *p : utils::get_params(MoniqueModule::ARP)) {
        // Process the param value
        _process_param_value(*p);					
    }

    // Process the FSM to ensure it is in the correct state now that all preset params
    // have been set
    _process_fsm_async_event(true);
}

//----------------------------------------------------------------------------
// _process_param_value
//----------------------------------------------------------------------------
void ArpManager::_process_param_value(const Param &param)
{
    // Note: Lock the ARP mutex *before* calling this function

    // If the MIDI clock in param has changed
    if ((param.module() == MoniqueModule::SYSTEM) && (param.param_id() == SystemParamId::MIDI_CLK_IN_PARAM_ID)) {
        // Update the MIDI clock in state
        _midi_clk_in = param.value() ? true : false;
    }
    // Is this an Arpeggiator param?
    else if (param.module() == module()) {
        // Process the arpeggiator param value based on the param ID
        switch (param.param_id()) {
            case ArpParamId::ARP_ENABLE_PARAM_ID: {
                // Update the enable value
                // This will be processed during the next processing loop of the FSM
                _enable = (param.value() == 0) ? false : true;
                break;
            }
            
            case ArpParamId::ARP_DIR_MODE_PARAM_ID: {
                // Update the direction mode if changed            
                auto mode = static_cast<ArpDirMode>(param.hr_value());
                if (mode != _dir_mode) {
                    _dir_mode = mode;
                }
                break;
            }

            case ArpParamId::ARP_TEMPO_NOTE_VALUE_PARAM_ID: {       
                // Set the tempo pulse count from the updated note value
                _tempo_pulse_count = utils::tempo_pulse_count(utils::tempo_note_value(param.hr_value()));
                _note_duration_pulse_count = _tempo_pulse_count >> 1;
                break;               
            }

            case ArpParamId::ARP_HOLD_PARAM_ID: {
                // Update the hold parameter
                _update_hold(param.value() != 0);
                break;            
            }

            case ArpParamId::ARP_RUN_PARAM_ID: {
                // We need to handle the RUN param, which is sent whenever a MIDI SEQ EVENT START/STOP
                // is sent, by reseting the FSM so that the ARP is in sync with the following 
                // MIDI clock pulses
                _started = (param.value() == 0) ? false : true;
                _prev_started = !_started;

                // Process the FSM (and reset the pulse count)
                _process_fsm_async_event(true);
                break;
            }            

            default:
                break;
        }
    }
}

//----------------------------------------------------------------------------
// _process_tempo_event
//----------------------------------------------------------------------------
void ArpManager::_process_tempo_event()
{
	// Do forever until stopped
	while (true) {
        // Get the mutex lock
        std::unique_lock<std::mutex> lk(utils::arp_mutex());

        // Wait for a tempoo event to be signalled
        utils::arp_wait(lk);

        // If the FSM is running
        if (_fsm_running) {
            // Has the FSM been reset or the required number of tempo pulses reached?
            if (++_pulse_count >= _note_duration_pulse_count) {
                // Process the FSN
                _process_fsm();

                // Set the next note duration pulse count to check
                _note_duration_pulse_count = _tempo_pulse_count - _note_duration_pulse_count;
                _pulse_count = 0;
            }
        }
        else {
            // Quit the tempo event thread
            break;
        }        
    }
}

//----------------------------------------------------------------------------
// _process_fsm_async_event
//----------------------------------------------------------------------------
void ArpManager::_process_fsm_async_event(bool reset)
{
    // Note: Lock the ARP mutex *before* calling this function

    // If the FSM is running
    if (_fsm_running) {
        // Process the FSM immediately
        _process_fsm();

        // Should we also reset the pulse count?
        if (reset) {
            // Set the next note duration pulse count to check
            _note_duration_pulse_count = _tempo_pulse_count - _note_duration_pulse_count;
            _pulse_count = 0;
        }
    }
}

//----------------------------------------------------------------------------
// _process_fsm
//----------------------------------------------------------------------------
void ArpManager::_process_fsm()
{
    ArpState prev_arp_state = _arp_state;

    // Has the enable state changed?
    if (_prev_enable != _enable)
    {
        // Are we now enabled?
        if (_enable && !_midi_clk_in) {
            // If there are any notes already played, play each note OFF and then we can start
            // the ARP cleanly
            _stop_arp_notes();
            _arp_state = ArpState::IDLE;
            _started = true;
            _prev_started = true;
        }
        else {
            _arp_state = ArpState::DISABLED;
            _prev_started = false;
        }
        _prev_enable = _enable;
    }
    // Has the started state changed?
    else if (_enable && (_prev_started != _started)) {
        // Has the ARP been re-started?
        if (_started) {
            // If there are any notes already played, play each note OFF and then we can start
            // the ARP cleanly
            _stop_arp_notes();           
            _arp_state = ArpState::IDLE;
        }
        else {
            _arp_state = ArpState::DISABLED;           
        }
        _prev_started = _started;
    }

    // Run the state machine
    switch (_arp_state)
    {
    case ArpState::DISABLED:
        // If not already disabled
        if (prev_arp_state != ArpState::DISABLED)
        {
            // The Arpeggiator has been disabled
            // Were we just playing a note-on?
            if (prev_arp_state == ArpState::PLAYING_NOTEON)
            {
                // Play the note-off to finish the Arpeggiator sequence
                _stop_arp_note();
            }

            // Clear the Arpeggiator array of notes
            _arp_notes.clear();
            _held_notes.clear();
        }
        break;

    case ArpState::IDLE:
        // In this state, the Arpeggiator is enabled, and waiting for the first note to be
        // received
        // Are there any notes to play?
        if (_arp_notes.size() > 0)
        {
            // Reset the Up/Down Direction Mode direction
            _updown_dir_mode = ArpDirMode::UP;

            // Get the next Arpeggiator note to play and play it
            _send_arp_note();

            // Change the state to indicate we are now playing a note-on
            _arp_state = ArpState::PLAYING_NOTEON;
        }
        break;

    case ArpState::PLAYING_NOTEON:
        // In this state, we have just played a note-on, and now need to
        // play a note-off
        _stop_arp_note();

        // If there are notes to play
        if (_arp_notes.size() > 0){
            // Change the state to indicate we are now playing a note-on
            _arp_state = ArpState::PLAYING_NOTEOFF;
        }
        else {
            // No more notes to play, return to the idle state
            _arp_state = ArpState::IDLE;
        }
        break;

    case ArpState::PLAYING_NOTEOFF:
        // In this state, we have just played a note-off, and now need to
        // play the next note
        // First check there are notes to play
        if (_arp_notes.size() > 0)
        {
            // Get the next Arpeggiator note to play and play it
            _send_arp_note();

            // Change the state to indicate we are now playing a note-on
            _arp_state = ArpState::PLAYING_NOTEON;
        }
        else
        {
            // No more notes to play, return to the idle state
            _arp_state = ArpState::IDLE;
        }
        break;
    }
}

//----------------------------------------------------------------------------
// _update_hold
//----------------------------------------------------------------------------
void ArpManager::_update_hold(bool hold)
{
    // Update the hold parameter
    bool prev_hold = _hold;
    _hold = hold;

    // If we are coming out of hold mode, clear all Arpeggiator (un-held) notes in the array
    if (prev_hold && !_hold) {
        // Check all current ARP notes
        for (auto itr1 = _arp_notes.begin(); itr1 < _arp_notes.end();) {
            // Is this a held note?
            bool keep_note = false;
            for (auto itr2 = _held_notes.begin(); itr2 < _held_notes.end(); itr2++) {
                // Does this note match?
                if (itr1->data.note.note == itr2->data.note.note) {
                    // Matches - the note is held so keep this note
                    keep_note = true;
                    break;
                }
            }

            // Should we keep this note?
            keep_note ? ++itr1 : _arp_notes.erase(itr1);
        }
    }
    else
        _hold_timeout = false;
}

//----------------------------------------------------------------------------
// _get_next_arp_note
//----------------------------------------------------------------------------
snd_seq_event_t ArpManager::_get_next_arp_note()
{
    ArpDirMode dir_mode = _dir_mode;

    // If there is only one note in the notes array, just return that
    if (_arp_notes.size() == 1)
    {
        return _arp_notes.at(0);
    }

    // More than one note - parse the direction mode
    // Up/Down Direction Mode?
    if (dir_mode == ArpDirMode::UPDOWN)
    {
        // Check the current Up/Down direction
        if (_updown_dir_mode == ArpDirMode::UP)
        {
            // Process in UP mode
            dir_mode = ArpDirMode::UP;
        }
        else
        {
            // Process in DOWN mode
            dir_mode = ArpDirMode::DOWN;
        }
    }

    // Assigned Direction Mode?
    if (dir_mode == ArpDirMode::ASSIGNED)
    {
        // This mode gets the notes in the order they were added into the list
        // Increment the step for the next note, and check for wrap-around
        _step++;
        if (_step >= _arp_notes.size())
        {
            // Wrap-around the step to the start of the notes array
            _step = 0;
        }

        // Return the next note to play
        return _arp_notes.at(_step);
    }

    // Up Direction Mode?
    if (dir_mode == ArpDirMode::UP)
    {
        // This mode gets the notes in sequence from pitch low to high
        snd_seq_event_t note_out;
        bool note_out_set = false;
        snd_seq_event_t lowest_note = _arp_notes.at(0);

        // We need to search for the next highest pitch note to play, if any
        // If there is no next highest pitch note, we return to the lowest
        // pitch note to play
        for (snd_seq_event_t note : _arp_notes)
        {
            // Is this note higher in pitch than the previously played note?
            if (note.data.note.note > _note_on.data.note.note)
            {
                // Has either a note out candidate not been set, or if set is this
                // note less than the current candidate for note out?
                if (!note_out_set || (note.data.note.note < note_out.data.note.note))  
                {
                    // This note is a candidate for the next note to play
                    note_out = note;
                    note_out_set = true;
                }
            }

            // We also need to save the lowest pitch note so that we can send this if there
            // is no higher pitch note to send
            if (note.data.note.note < lowest_note.data.note.note)
            {
                // This note is a candidate for the lowest pitch note
                lowest_note = note;
            }
        }

        // Was the next highest pitch note found? If so, return it
        if (note_out_set)
        {
            // Save the previous note for Up/Down Direction processing
            _prev_note_on = _note_on;
            return note_out;
        }

        // The next highest pitch note was not found
        // If we are really in UP Direction Mode, return the lowest note to wrap-around
        if (_dir_mode == ArpDirMode::UP)
            return lowest_note;

        // If we get here we must be in Up/Down Direction Mode, currently going UP, and
        // the highest pitch note has been played
        // In this case, we play the *previous note*, which will be the next lowest note,
        // and switch the Up/Down Mode to DOWN
        _updown_dir_mode = ArpDirMode::DOWN;
        note_out = _prev_note_on;
        _prev_note_on = _note_on;
        return note_out;
    }
    
    // Down Direction Mode?
    if (dir_mode == ArpDirMode::DOWN)
    {
        // This mode gets the notes in sequence from pitch high to low
        snd_seq_event_t note_out;
        bool note_out_set = false;
        snd_seq_event_t highest_note = _arp_notes.at(0);

        // We need to search for the next lowest pitch note to play, if any
        // If there is no next lowest pitch note, we return to the highest
        // pitch note to play
        for (snd_seq_event_t note : _arp_notes)
        {
            // Is this note lower in pitch than the previously played note? 
            if (note.data.note.note < _note_on.data.note.note)
            {
                // Has either a note out candidate not been set, or if set is this
                // note greater than the current candidate for note out?
                if (!note_out_set || (note.data.note.note > note_out.data.note.note))  
                {
                    // This note is a candidate for the next note to play
                    note_out = note;
                    note_out_set = true;
                }
            }

            // We also need to save the highest pitch note so that we can send this if there
            // is no lower pitch note to send
            if (note.data.note.note > highest_note.data.note.note)
            {
                // This note is a candidate for the highest pitch note
                highest_note = note;
            }
        }

        // Was the next lowest pitch note found? If so, return it
        if (note_out_set)
        {
            // Save the previous note for Up/Down Direction processing
            _prev_note_on = _note_on;
            return note_out;
        }
        
        // The next lowest pitch note was not found
        // If we are really in DOWN Direction Mode, return the highest note to wrap-around
        if (_dir_mode == ArpDirMode::DOWN)
            return highest_note;

        // If we get here we must be in Up/Down Direction Mode, currently going DOWN, and
        // the lowest pitch note has been played
        // In this case, we play the *previous note*, which will be the next highest note,
        // and switch the Up/Down Mode to UP
        _updown_dir_mode = ArpDirMode::UP;
        note_out = _prev_note_on;
        _prev_note_on = _note_on;
        return note_out;
    }

    // If we get here, the Direction Mode must be random
    _step++;

    // If we have gone though all the notes, then reshuffle the array and start from note 0
    if (_step >= _arp_notes.size())
    {
        _arp_notes_shuffle.clear();
        _arp_notes_shuffle.reserve(_arp_notes.size());
        for(uint i = 0; i < _arp_notes.size(); i++)
        {
            _arp_notes_shuffle.push_back(i);
        }
        std::shuffle(_arp_notes_shuffle.begin(), _arp_notes_shuffle.end(), std::default_random_engine(std::rand()));
        _step = 0;
    }
    
    // If the size of the arp notes array has changed, then we have to also change the shuffle array, could consider 
    // nicer behaviour here, rather than just reshuffling the whole array
    if(_arp_notes.size() != _arp_notes_shuffle.size())
    {
        _arp_notes_shuffle.clear();
        for(uint i = 0; i < _arp_notes.size(); i++)
        {
            _arp_notes_shuffle.push_back(i);
        }
        std::shuffle(_arp_notes_shuffle.begin(), _arp_notes_shuffle.end(), std::default_random_engine(std::rand()));
        _step = 0;
    }
    return _arp_notes.at(_arp_notes_shuffle.at(_step));
}

//----------------------------------------------------------------------------
// _add_arp_note
//----------------------------------------------------------------------------
void ArpManager::_add_arp_note(const snd_seq_event_t &note)
{
    // Make sure the capacity is not exceeded
    if (_arp_notes.size() < MAX_ARP_NOTES)
    {
        // Add the note
        _arp_notes.push_back(note);
    }
}

//----------------------------------------------------------------------------
// _find_arp_note
//----------------------------------------------------------------------------
bool ArpManager::_find_arp_note(const snd_seq_event_t &note)
{
    // Find the note in the Arpeggiator notes array list which matches the one to remove
    for (auto i = _arp_notes.begin(); i < _arp_notes.end(); i++)
    {
        // Does this note match the passed note?
        if (note.data.note.note == i->data.note.note)
        {
            // Note found
            return true;
        }
    }
    return false;
}

//----------------------------------------------------------------------------
// _remove_arp_note
//----------------------------------------------------------------------------
bool ArpManager::_remove_arp_note(const snd_seq_event_t &note)
{
    // Find the note in the Arpeggiator notes array list which matches the one to remove

    for (auto i = _arp_notes.begin(); i < _arp_notes.end(); i++)
    {
        // Does this note match the passed note?
        if (note.data.note.note == i->data.note.note)
        {
            // Matches - remove it
            _arp_notes.erase(i);
            return true;

        }
    }
    return false;
}

//----------------------------------------------------------------------------
// _send_arp_note
//----------------------------------------------------------------------------
void ArpManager::_send_arp_note()
{
    // Get the next Arpeggiator note to play and send it
    _note_on = _get_next_arp_note();
    _send_note(_note_on);
}

//----------------------------------------------------------------------------
// _stop_arp_note
//----------------------------------------------------------------------------
void ArpManager::_stop_arp_note()
{
    // Get the last note that was sent out and send the note again, except as a note-off
    // with 0 velocity
    snd_seq_event_t note_off = _note_on;
    note_off.type = snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF;
    note_off.data.note.velocity = 0;
    _send_note(note_off);
}

//----------------------------------------------------------------------------
// _stop_arp_notes
//----------------------------------------------------------------------------
void ArpManager::_stop_arp_notes()
{
    // If there are played arp notes
    if (_arp_notes.size() > 0) {
        // Play each note-off
        for (uint i=0; i<_arp_notes.size(); i++) {
            auto note_off = _arp_notes[i];
            note_off.type = snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF;
            note_off.data.note.velocity = 0;
            _send_note(note_off);
        }
    }
}

//----------------------------------------------------------------------------
// _send_note
//----------------------------------------------------------------------------
void ArpManager::_send_note(const snd_seq_event_t &note)
{
    //if (note.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON)
    //    DEBUG_BASEMGR_MSG("Send note: " << (int)note.data.note.note << ": ON");
    //else
    //    DEBUG_BASEMGR_MSG("Send note: " << (int)note.data.note.note << ": OFF");

    // Send the note directly to the DAW
    utils::get_manager(MoniqueModule::DAW)->process_midi_event_direct(&note);
}

//----------------------------------------------------------------------------
// _register_params
//----------------------------------------------------------------------------
void ArpManager::_register_params()
{
    // Register the Enable param
    auto param1 = Param::CreateParam(this, ArpParamId::ARP_ENABLE_PARAM_ID, ENABLE_PARAM_NAME, ENABLE_DISPLAY_NAME);
    param1->set_type(ParamType::PRESET_COMMON);
    param1->set_hr_value(_enable);
    utils::register_param(std::move(param1));

    // Register the Direction Mode param
    auto param2 = Param::CreateParam(this, ArpParamId::ARP_DIR_MODE_PARAM_ID, DIR_MODE_PARAM_NAME, DIR_MODE_DISPLAY_NAME);
    param2->set_type(ParamType::PRESET_COMMON);
    param2->set_hr_value(_dir_mode);
    utils::register_param(std::move(param2));

	// Register the Tempo Note Value param
	auto param3 = Param::CreateParam(this, ArpParamId::ARP_TEMPO_NOTE_VALUE_PARAM_ID, TEMPO_NOTE_VALUE_PARAM_NAME, TEMPO_NOTE_VALUE_DISPLAY_NAME);
    param3->set_type(ParamType::PRESET_COMMON);
    param3->set_hr_value(common::TempoNoteValue::QUARTER);
    utils::register_param(std::move(param3));

	// Register the Hold param
	auto param4 = Param::CreateParam(this, ArpParamId::ARP_HOLD_PARAM_ID, HOLD_PARAM_NAME, HOLD_DISPLAY_NAME);
    param4->set_type(ParamType::PRESET_COMMON);
    param4->set_hr_value(_hold);
    utils::register_param(std::move(param4));

    // Register the Run param
    auto param5 = Param::CreateParam(this, ArpParamId::ARP_RUN_PARAM_ID, RUN_PARAM_NAME, RUN_DISPLAY_NAME);
    param5->set_type(ParamType::PRESET_COMMON);
    param5->set_hr_value(false);
    param5->set_preset(false);
    utils::register_param(std::move(param5));    
}
