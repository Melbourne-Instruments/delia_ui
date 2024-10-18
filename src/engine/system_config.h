/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  system_config.h
 * @brief System Config definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _SYSTEM_CONFIG_H
#define _SYSTEM_CONFIG_H

#include <mutex>
#include "param.h"
#include "ui_common.h"

// System Config struct
class SystemConfig
{
public:
    // Constructor    
    SystemConfig();

    // Destructor
    ~SystemConfig();

    // Public functions
    const PresetId& preset_id();
    const PresetId& prev_preset_id();
    void set_preset_id(PresetId id);
    void set_prev_preset_id(PresetId id);
    uint get_mod_src_num();
    void set_mod_src_num(uint num);
    bool get_demo_mode();
    uint get_demo_mode_timeout();
    void set_demo_mode(bool enable);
    void set_demo_mode_timeout(uint timeout);    
    std::string get_system_colour();
    std::string get_system_colour(std::string name);
    uint get_system_colour_index();
    void set_system_colour(std::string colour);
    void add_available_system_colour(SystemColour colour);
    bool system_colour_is_custom();

private:
    // Private variables
    std::mutex _mutex;
    PresetId _preset_id;
    PresetId _prev_preset_id;
    bool _preset_modified;
    uint _mod_src_num;
    bool _demo_mode;
    uint _demo_mode_timeout;
    std::string _system_colour;
    std::vector<SystemColour> _system_colours;
};

#endif  // _SYSTEM_CONFIG_H
