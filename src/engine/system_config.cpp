/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  system_config.cpp
 * @brief System Config implementation.
 *-----------------------------------------------------------------------------
 */

#include "system_config.h"
#include "utils.h"


//----------------------------------------------------------------------------
// SystemConfig
//----------------------------------------------------------------------------
SystemConfig::SystemConfig()
{
    // Initialise class data
    _mod_src_num = DEFAULT_MOD_SRC_NUM;
    _demo_mode = false;
}

//----------------------------------------------------------------------------
// ~SystemConfig
//----------------------------------------------------------------------------
SystemConfig::~SystemConfig()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// preset_id
//----------------------------------------------------------------------------
const PresetId& SystemConfig::preset_id()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the Preset ID
    return _preset_id;
}

//----------------------------------------------------------------------------
// prev_preset_id
//----------------------------------------------------------------------------
const PresetId& SystemConfig::prev_preset_id()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the previous Preset ID
    return _prev_preset_id;
}

//----------------------------------------------------------------------------
// set_preset_id
//----------------------------------------------------------------------------
void SystemConfig::set_preset_id(PresetId id)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the Preset ID
    _preset_id = id;
}

//----------------------------------------------------------------------------
// set_preset_id
//----------------------------------------------------------------------------
void SystemConfig::set_prev_preset_id(PresetId id)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the previous Preset ID
    _prev_preset_id = id;
}

//----------------------------------------------------------------------------
// set_mod_src_num
//----------------------------------------------------------------------------
void SystemConfig::set_mod_src_num(uint num)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the modulation source number
    _mod_src_num = num;
}

//----------------------------------------------------------------------------
// get_mod_src_num
//----------------------------------------------------------------------------
uint SystemConfig::get_mod_src_num()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the modulation source number
    if (_mod_src_num > 0)
        return _mod_src_num;
    return DEFAULT_MOD_SRC_NUM;
}

//----------------------------------------------------------------------------
// set_demo_mode
//----------------------------------------------------------------------------
void SystemConfig::set_demo_mode(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Enable/disable demo mode
    _demo_mode = enabled;
}

//----------------------------------------------------------------------------
// get_demo_mode
//----------------------------------------------------------------------------
bool SystemConfig::get_demo_mode()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Return if demo mode is enabled/disabled
    return _demo_mode;
}

//----------------------------------------------------------------------------
// set_demo_mode_timeout
//----------------------------------------------------------------------------
void SystemConfig::set_demo_mode_timeout(uint timeout)
{
    // Set the demo mode timeout
    _demo_mode_timeout = timeout;
}

//----------------------------------------------------------------------------
// get_demo_mode_timeout
//----------------------------------------------------------------------------
uint SystemConfig::get_demo_mode_timeout()
{
    // Return the demo mode timeout
    return _demo_mode_timeout;
}

//----------------------------------------------------------------------------
// set_system_colour
//----------------------------------------------------------------------------
void SystemConfig::set_system_colour(std::string colour)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the system colour
    _system_colour = colour;
}

//----------------------------------------------------------------------------
// get_system_colour
//----------------------------------------------------------------------------
std::string SystemConfig::get_system_colour()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the system colour
    return _system_colour;
}

//----------------------------------------------------------------------------
// get_system_colour
//----------------------------------------------------------------------------
std::string SystemConfig::get_system_colour(std::string name)
{
    // Find the system colour
    for (SystemColour& sc : _system_colours) {
        if (sc.name == name) {
            // Return the system colour
            return sc.colour;
        }
    }
    return "";
}

//----------------------------------------------------------------------------
// get_system_colour_index
//----------------------------------------------------------------------------
uint SystemConfig::get_system_colour_index()
{
    // Find the system colour
    uint index = 0;
    for (SystemColour& sc : _system_colours) {
        if (sc.colour == _system_colour) {
            // Return the system colour index
            return index;
        }
        index++;
    }
    return 0;
}

//----------------------------------------------------------------------------
// add_available_system_colour
//----------------------------------------------------------------------------
void SystemConfig::add_available_system_colour(SystemColour system_colour)
{
    // See if the system colour already exists
    for (SystemColour& sc : _system_colours) {
        if (sc.name == system_colour.name) {
            // Already exists, so don't set
            return;
        }
    }

    // Add the system colour
    _system_colours.push_back(system_colour);
}

//----------------------------------------------------------------------------
// system_colour_is_custom
//----------------------------------------------------------------------------
bool SystemConfig::system_colour_is_custom()
{
    // Find the system colour
    for (SystemColour& sc : _system_colours) {
        if (sc.colour == _system_colour) {
            // Found
            return false;
        }
    }
    return true;
}
