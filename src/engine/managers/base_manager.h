/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  base_manager.h
 * @brief Base Manager (virtual) class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _BASE_MANAGER_H
#define _BASE_MANAGER_H

#include "ui_common.h"
#include "event.h"
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

// Debug message MACRO
#define BASEMGR_MSG(str)        MSG(this->name() << ": " << str)
#define DEBUG_BASEMGR_MSG(str)  DEBUG_MSG(this->name() << ": " << str)

struct BaseManagerMsg;
class EventRouter;

class BaseManager
{
public:
    // Constructor
    BaseManager(MoniqueModule module, const char* thread_name, EventRouter *event_router);

    // Destructor
    virtual ~BaseManager();

    // Called once to create the thread
    // @return TRUE if thread is created. FALSE otherwise.
    virtual bool start();

    // Called once a program exit to exit the thread
    virtual void stop();

    // Get the module of this manager
    MoniqueModule module() const;

    // Get the Manager name
    const char *name() const;

    // Add a message to thread queue.
    void post_msg(const BaseEvent *event);

    // Entry point for the thread
    virtual void process();
    virtual void process_event(const BaseEvent *event);

    // Function to process MIDI event direct
    virtual void process_midi_event_direct(const snd_seq_event_t *event);

protected:
    EventRouter *_event_router;
    
private:
    std::thread* _mgr_thread;
    std::deque<BaseManagerMsg*> _msg_queue;
    std::mutex _mutex;
    std::condition_variable _cv;
    std::atomic<bool> _running{false};
    const char *_THREAD_NAME;
    MoniqueModule _module;
};

#endif  // _BASE_MANAGER_H
