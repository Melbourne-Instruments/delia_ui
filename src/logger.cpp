/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  logger.cpp
 * @brief Logger implementation.
 *-----------------------------------------------------------------------------
 */
#include "ui_common.h"
#include "logger.h"
#include "version.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/async.h"

// Constants
constexpr char LOGGER_NAME[]       = "DELIA";
constexpr char LOGGER_FILENAME[]   = "/udata/delia/delia.log";
constexpr int LOGGER_MAX_FILE_SIZE = 1048576 * 10;          // 10MB

// Static variables
std::shared_ptr<spdlog::logger> Logger::_logger{nullptr};

//----------------------------------------------------------------------------
// Start
//----------------------------------------------------------------------------
void Logger::Start() 
{
    // Setup the spd file logger
    spdlog::set_pattern("[%Y-%m-%d %T.%e] [%l] %v");
    spdlog::set_level(spdlog::level::info);
    _logger = spdlog::rotating_logger_mt<spdlog::async_factory>(LOGGER_NAME,
                                                                LOGGER_FILENAME,
                                                                LOGGER_MAX_FILE_SIZE,
                                                                1);

    // Show the start log header
    _logger->flush_on(spdlog::level::err);
    _logger->info("---------------------");
    _logger->info("DELIA UI Log: Started");
    _logger->info("---------------------");    
#ifdef MONIQUE_UI_BETA_RELEASE    
    _logger->info("Version: {}.{}.{}-beta ({})", MONIQUE_UI_MAJOR_VERSION, MONIQUE_UI_MINOR_VERSION, MONIQUE_UI_PATCH_VERSION, MONIQUE_UI_GIT_COMMIT_HASH);
#else
    _logger->info("Version: {}.{}.{} ({})", MONIQUE_UI_MAJOR_VERSION, MONIQUE_UI_MINOR_VERSION, MONIQUE_UI_PATCH_VERSION, MONIQUE_UI_GIT_COMMIT_HASH);
#endif
}

//----------------------------------------------------------------------------
// Stop
//----------------------------------------------------------------------------
void Logger::Stop() 
{
    // Show the stop log header
    _logger->info("---------------------");
    _logger->info("DELIA UI Log: Stopped");
    _logger->info("---------------------");
    _logger->flush();
}

//----------------------------------------------------------------------------
// Flush
//----------------------------------------------------------------------------
void Logger::Flush() 
{
    // Flush the log
    _logger->flush();
}

//----------------------------------------------------------------------------
// module_name
//----------------------------------------------------------------------------
std::string Logger::module_name(MoniqueModule module)
{
    // Parse the module
    switch(module)
    {
        case MoniqueModule::ARP:
            return "[arp]";

        case MoniqueModule::DAW:
            return "[daw]";

        case MoniqueModule::FILE_MANAGER:
            return "[fmg]";

        case MoniqueModule::MIDI_DEVICE:
            return "[midi]";
        
        case MoniqueModule::SEQ:
            return "[seq]";

        case MoniqueModule::SFC_CONTROL:
            return "[sfc]";

        case MoniqueModule::PEDALS:
            return "[pdl]";

        case MoniqueModule::SOFTWARE:
            return "[sw]";       

        case MoniqueModule::SYSTEM:
        default:
            return "[sys]";          
    }
}
