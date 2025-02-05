/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  logger.h
 * @brief Logger class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include "spdlog/spdlog.h"

#define MONIQUE_LOG_INFO(module, msg, ...)      Logger::LogInfo(module, msg, ##__VA_ARGS__)
#define MONIQUE_LOG_WARNING(module, msg, ...)   Logger::LogWarning(module, msg, ##__VA_ARGS__)
#define MONIQUE_LOG_ERROR(module, msg, ...)     Logger::LogError(module, msg, ##__VA_ARGS__)
#define MONIQUE_LOG_CRITICAL(module, msg, ...)  Logger::LogCritical(module, msg, ##__VA_ARGS__)
#define MONIQUE_LOG_FLUSH()                     Logger::Flush()

// Logger
class Logger
{
public:
    // Public functions
    static void Start();
    static void Stop();
    static void Flush();
    template <typename ... Args>
    static void LogInfo(MoniqueModule module, const std::string& msg, Args... args)
    {
        _logger->info("{} " + msg, module_name(module), std::forward<Args>(args)...);
    }
    template <typename ... Args>
    static void LogWarning(MoniqueModule module, const std::string& msg, Args... args)
    {
        _logger->warn("{} " + msg, module_name(module), std::forward<Args>(args)...);
    }
    template <typename ... Args>
    static void LogError(MoniqueModule module, const std::string& msg, Args... args)
    {
        _logger->error("{} " + msg, module_name(module), std::forward<Args>(args)...);
    }
    template <typename ... Args>
    static void LogCritical(MoniqueModule module, const std::string& msg, Args... args)
    {
        _logger->critical("{} " + msg, module_name(module), std::forward<Args>(args)...);
    }

private:
    // Private variables
    static std::shared_ptr<spdlog::logger> _logger;

    // Private functions
    static std::string module_name(MoniqueModule module);
};

#endif // LOGGER_H
