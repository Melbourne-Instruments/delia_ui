/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  base_manager.cpp
 * @brief Base Manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include "base_manager.h"
#include "utils.h"

// Base Message Type
enum class BaseMsgType
{
    POST_EVENT,
    EXIT_THREAD
};

// Base message
struct BaseManagerMsg
{
    BaseManagerMsg(BaseMsgType a, const BaseEvent *b) 
    {
        base_msg_type = a;
        event = b;
    }
    BaseMsgType base_msg_type;
    const BaseEvent *event;
};

//----------------------------------------------------------------------------
// BaseManager
//----------------------------------------------------------------------------
BaseManager::BaseManager(MoniqueModule module, const char* thread_name, EventRouter *event_router) : _mgr_thread(0), _THREAD_NAME(thread_name)
{
    // Initialise private data
    _module = module;
    _event_router = event_router;
    _mgr_thread = 0;
}

//----------------------------------------------------------------------------
// ~BaseManager
//----------------------------------------------------------------------------
BaseManager::~BaseManager()
{
    // Make sure any threads are tidied up
    stop();
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool BaseManager::start()
{
    // If the thread has not been created
    if (!_mgr_thread)
    {
        // Create a non real-time thread
        _mgr_thread = new std::thread(&BaseManager::process, this);
    }
    return true;
}

//----------------------------------------------------------------------------
// module
//----------------------------------------------------------------------------
MoniqueModule BaseManager::module() const
{
    return _module;
}

//----------------------------------------------------------------------------
// name
//----------------------------------------------------------------------------
const char *BaseManager::name() const
{
    return _THREAD_NAME;
}

//----------------------------------------------------------------------------
// ExitThread
//----------------------------------------------------------------------------
void BaseManager::stop()
{
    if (!_mgr_thread)
        return;

    // Create a new BaseManagerMsg
    BaseManagerMsg* msg = new BaseManagerMsg(BaseMsgType::EXIT_THREAD, nullptr);

    // Put exit thread message into the queue
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _msg_queue.push_back(msg);
        _cv.notify_one();
    }
    if (_mgr_thread->joinable())
        _mgr_thread->join();
    delete _mgr_thread;
    _mgr_thread = 0;
}

//----------------------------------------------------------------------------
// PostMsg
//----------------------------------------------------------------------------
void BaseManager::post_msg(const BaseEvent *event)
{
    BaseManagerMsg* new_msg = new BaseManagerMsg(BaseMsgType::POST_EVENT, event);
    bool push_msg = true;

    // Firstly get the last event in the queue (if any) and check if it is
    // the same as this event
    // If this is the case then for some events we do not push this event
    // and instead overwrite the last to avoid spamming the event queue
    std::unique_lock<std::mutex> lk(_mutex);
    if (!_msg_queue.empty())
    {
        // Get the last message and check if it is the same
        auto msg = _msg_queue.back();
        if (msg->event->type() == event->type())
        {
            // For some events overwrite the last event
            switch (event->type())
            {
                case EventType::PARAM_CHANGED:
                {
                    // We don't want to spam the queue with lots of param change messages from the
                    // same param
                    // Reverse iterate the message queue checking for existing param change messages
                    auto new_pc = static_cast<const ParamChangedEvent *>(event)->param_change();
                    for (auto itr=_msg_queue.rbegin(); itr != _msg_queue.rend(); ++itr) {
                        // If this is a param change evert
                        if ((*itr)->event->type() == EventType::PARAM_CHANGED) {
                            auto cur_pc = static_cast<const ParamChangedEvent *>((*itr)->event)->param_change();

                            // If this param change matches, overwrite it and don't add a new one
                            if (cur_pc.param == new_pc.param)
                            {
                                // Overwrite this event
                                *itr = new_msg;
                                push_msg = false;
                                break;
                            }
                        }
                    }
                    break;
                }

                case EventType::SYSTEM_FUNC:
                {
                    // Nothing to do for now
                    break;
                }			

                case EventType::RELOAD_PRESETS:
                    // Just overwrite the last event
                    _msg_queue.back() = new_msg;
                    push_msg = false;
                    return;

                default:
                    break;
            }
        }
    }

    // Add the message if needed and notify the worker thread
    if (push_msg)
        _msg_queue.push_back(new_msg);
    _cv.notify_one();
}

//----------------------------------------------------------------------------
// Process
//----------------------------------------------------------------------------
void BaseManager::process()
{
    while (1)
    {
        BaseManagerMsg* msg = 0;
        {
            // Wait for a message to be added to the queue
            std::unique_lock<std::mutex> lk(_mutex);
            while (_msg_queue.empty())
                _cv.wait(lk);

            if (_msg_queue.empty())
                continue;

            msg = _msg_queue.front();
            _msg_queue.pop_front();
        }

        // Parse the Base Message Type
        switch (msg->base_msg_type)
        {
            case BaseMsgType::POST_EVENT:
            {
                // Process the event
                process_event(msg->event);

                // Delete dynamic data passed through message queue
                if (msg->event)
                    delete msg->event;
                delete msg;
                break;
            }

            case BaseMsgType::EXIT_THREAD:
            {
                if (msg->event)
                    delete msg->event;
                delete msg;
                std::unique_lock<std::mutex> lk(_mutex);
                while (!_msg_queue.empty())
                {
                    msg = _msg_queue.front();
                    _msg_queue.pop_front();
                    if (msg->event)
                        delete msg->event;					
                    delete msg;
                }
                return;
            }

            default:
                DEBUG_BASEMGR_MSG("Unknown message");
                //ASSERT();
        }
    }
}

//----------------------------------------------------------------------------
// ProcessEvent
//----------------------------------------------------------------------------
void BaseManager::process_event([[maybe_unused]] const BaseEvent *event)
{
    // Virtual so always overriden
}

//----------------------------------------------------------------------------
// process_midi_event_direct
//----------------------------------------------------------------------------
void BaseManager::process_midi_event_direct([[maybe_unused]] const snd_seq_event_t *event)
{
    // Overriden as necessary
}
