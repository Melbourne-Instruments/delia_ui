/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  main.cpp
 * @brief Main entry point to the DELIA UI.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <condition_variable>
#include "event_router.h"
#include "seq_manager.h"
#include "arp_manager.h"
#include "file_manager.h"
#include "gui_manager.h"
#include "daw_manager.h"
#include "midi_device_manager.h"
#include "pedals_manager.h"
#include "sfc_manager.h"
#include "sw_manager.h"
#include "ui_common.h"
#include "layer_info.h"
#include "utils.h"
#include "logger.h"
#include "ain.h"
#include "version.h"

// Constants
constexpr char PID_FILENAME[] = "/var/run/delia_ui.pid";

// Global variables
bool exit_flag = false;
bool exit_condition() {return exit_flag;}
std::condition_variable exit_notifier;

// Local functions
void _print_delia_ui_info();
bool _check_pid();
void _sigint_handler([[maybe_unused]] int sig);

//----------------------------------------------------------------------------
// main
//----------------------------------------------------------------------------
int main(void)
{
    // Setup the exit signal handler (e.g. ctrl-c, kill)
    signal(SIGINT, _sigint_handler);
    signal(SIGTERM, _sigint_handler);

    // Ignore broken pipe signals, handle in the app instead
    signal(SIGPIPE, SIG_IGN);

    // Show the app info
    _print_delia_ui_info();

    // Check the PID and don't run the app if its already running
    if (_check_pid()) {
        // Generate the session UUID
        // This just needs to be done once on startup
        utils::generate_session_uuid();
        
        // Start the logger
        Logger::Start();

        // Initialise the analog input driver - done here as multiple managers
        // use this driver
        ain::init();

        // Create the Event Router
        auto event_router = std::make_unique<EventRouter>();

        // Create the manager threads
        // Note 1: During creation, any associated params are registered
        // Note 2: The file manager must be created first so that it can create the param
        // blacklist (if any)
        auto file_manager = std::make_unique<FileManager>(event_router.get());
        auto sfc_control_manager = std::make_unique<SfcManager>(event_router.get());
        auto gui_manager = std::make_unique<GuiManager>(event_router.get());       
        auto daw_manager = std::make_unique<DawManager>(event_router.get());
        auto midi_device_manager = std::make_unique<MidiDeviceManager>(event_router.get());
        auto seq_manager = std::make_unique<SeqManager>(event_router.get());
        auto arp_manager = std::make_unique<ArpManager>(event_router.get());
        auto pedals_manager = std::make_unique<PedalsManager>(event_router.get());
        auto sw_manager = std::make_unique<SwManager>(event_router.get());
        
        // Register the DAW, MIDI, SEQ, ARP, and SOFTWARE managers
        // We do this so that they can be used for direct access when needed
        utils::register_manager(MoniqueModule::DAW, daw_manager.get());
        utils::register_manager(MoniqueModule::MIDI_DEVICE, midi_device_manager.get());
        utils::register_manager(MoniqueModule::SEQ, seq_manager.get());
        utils::register_manager(MoniqueModule::ARP, arp_manager.get());
        utils::register_manager(MoniqueModule::SOFTWARE, sw_manager.get());
        
        // Register the System params
        utils::register_system_params();
        
        // Register the System Function params
        SystemFunc::RegisterParams();  

        // Start the managers
        // Note: The file manager must be started first so that it can initialise the
        // params from the preset files, and map from the map file
        if (file_manager->start()) {
            // Start the GUI manager, this is always used no matter the mode
            gui_manager->start();
            
            // Start the software manager - if this module finds a software
            // update, it will put the app into maintenance mode
            sw_manager->start();
            if (!utils::maintenance_mode()) {
                // Initialise the other managers
                midi_device_manager->start();
                daw_manager->start();
                seq_manager->start();           
                arp_manager->start();
                pedals_manager->start();
                sfc_control_manager->start();
                        
                // Wait forever for an exit signal
                std::mutex m;
                std::unique_lock<std::mutex> lock(m);
                exit_notifier.wait(lock, exit_condition);

                // Clean up the managers
                sfc_control_manager->stop();
                pedals_manager->stop();
                arp_manager->stop();
                seq_manager->stop();
                daw_manager->stop();
                midi_device_manager->stop();
                gui_manager->stop();
                file_manager->stop();
            }
            else {
                // Maintence mode - a software update is available and in progress
                // Start the minimum managers to process the software update
                sfc_control_manager->start();

                // Wait forever for an exit signal
                std::mutex m;
                std::unique_lock<std::mutex> lock(m);
                exit_notifier.wait(lock, exit_condition);

                // Clean up the managers
                file_manager->stop();
                gui_manager->stop();
                sfc_control_manager->stop();
                sw_manager->stop(); 
            }
        }
        else {
            // The file manager is a critical component to start the UI
            MSG("\nDELIA UI could not be started");
        }

        // De-initialise the analog input driver
        ain::deinit();

        // Stop the logger
        Logger::Stop();
    }

    // DELIA UI has exited
    MSG("DELIA UI exited");
    return 0;
}

//----------------------------------------------------------------------------
// _print_delia_ui_info
//----------------------------------------------------------------------------
void _print_delia_ui_info()
{
    MSG("DELIA UI - Copyright (c) 2023-2024 Melbourne Instruments, Australia");
#ifdef MONIQUE_UI_BETA_RELEASE    
    MSG("Version " << MONIQUE_UI_MAJOR_VERSION << 
        "." << MONIQUE_UI_MINOR_VERSION << 
        "." << MONIQUE_UI_PATCH_VERSION <<
        "-beta " <<
        "(" << MONIQUE_UI_GIT_COMMIT_HASH << ")");    
#else
    MSG("Version " << MONIQUE_UI_MAJOR_VERSION << 
        "." << MONIQUE_UI_MINOR_VERSION << 
        "." << MONIQUE_UI_PATCH_VERSION << 
        " (" << MONIQUE_UI_GIT_COMMIT_HASH << ")");
#endif
    MSG("");
}

//----------------------------------------------------------------------------
// _check_pid
//----------------------------------------------------------------------------
bool _check_pid()
{
    FILE *fp;

    // Get the program PID
    auto pid = ::getpid();

    // Try to open the PID file
    fp = std::fopen(PID_FILENAME, "r");
    if (fp != nullptr) {
        // The file exits, read the specified PID
        pid_t current_pid;
        if (std::fread(&current_pid, sizeof(pid), 1, fp) == 1) {
            // Check if a program is running with that PID
            if (::kill(current_pid, 0) == 0) {
                // The MONIQUE UI app is already running
                MSG("App is already running");
                return false;
            }
        }
        std::fclose(fp);

        // Open the PID file for writing
        fp = fopen(PID_FILENAME, "w");
    }
    else {
        // File doesn't exist, create it
        fp = fopen(PID_FILENAME, "a");
    }

    // File open to write the PID ok?
    if (fp != nullptr) {
        // Write the PID
        std::fwrite(&pid, sizeof(pid), 1, fp);
        std::fclose(fp);
    }
    return true;
}

//----------------------------------------------------------------------------
// _sigint_handler
//----------------------------------------------------------------------------
void _sigint_handler([[maybe_unused]] int sig)
{
    // Signal to exit the app
    exit_flag = true;
    exit_notifier.notify_one();
}
