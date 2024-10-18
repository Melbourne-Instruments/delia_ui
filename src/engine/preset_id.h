/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  preset_id.h
 * @brief Preset ID class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _PRESET_ID_H
#define _PRESET_ID_H

#include <map>
#include "ui_common.h"

// Preset ID class
class PresetId
{
public:
    // Helper functions
    static std::string DefaultPresetName(uint index);

    // Constructors
    PresetId();
    PresetId(std::string id);

    // Destructor
    virtual ~PresetId(){}

    // Public functions
    bool is_valid(bool check_file_exist=false) const;
    std::string id() const;
    std::string path() const;
    std::string bank_folder() const;
    std::string preset_name() const;
    std::string preset_display_name() const;
    std::string preset_display_name_short() const;
    std::string preset_edit_name() const;
    bool operator==(const PresetId& rhs) const;
    PresetId next_preset_id() const;
    PresetId prev_preset_id() const;    
    void set_id(std::string id);
    void set_id(std::string bank_folder, std::string preset_name);
    void set_fallback_id();

private:
    // Private variables
    std::string _bank_folder;
    std::string _preset_name;

    // Private functions
    std::map<uint, std::string> _parse_bank_folder() const;
};

#endif  // _PRESET_ID_H
