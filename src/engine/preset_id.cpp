/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  preset_id.cpp
 * @brief Preset ID implementation.
 *-----------------------------------------------------------------------------
 */
#include <filesystem>
#include <dirent.h>
#include <cstring>
#include <regex>
#include "preset_id.h"

//----------------------------------------------------------------------------
// DefaultPresetName
//----------------------------------------------------------------------------
std::string PresetId::DefaultPresetName(uint index)
{
    char name[sizeof("000_BASIC_PRESET.json")];

    // Note - assume the passed index is valid
    std::sprintf(name, "%03d_BASIC_PRESET", index); 
    return name;
}

//----------------------------------------------------------------------------
// PresetId
//----------------------------------------------------------------------------
PresetId::PresetId()
{
    // Initialise member data
    _bank_folder = "";
    _preset_name = "";
}

//----------------------------------------------------------------------------
// PresetId
//----------------------------------------------------------------------------
PresetId::PresetId(std::string id)
{
    // Initialise member data
    set_id(id);
}

//----------------------------------------------------------------------------
// is_valid
//----------------------------------------------------------------------------
bool PresetId::is_valid(bool check_file_exists) const {
    return _bank_folder.size() > 0 && _preset_name.size() > 0 && (!check_file_exists || std::filesystem::exists(path()));
}

//----------------------------------------------------------------------------
// id
//----------------------------------------------------------------------------
std::string PresetId::id() const {
    return _bank_folder + "/" + _preset_name;
}

//----------------------------------------------------------------------------
// path
//----------------------------------------------------------------------------
std::string PresetId::path() const {
    return MONIQUE_PRESET_FILE_PATH(_bank_folder + "/" + _preset_name + ".json");
}

//----------------------------------------------------------------------------
// bank_folder
//----------------------------------------------------------------------------
std::string PresetId::bank_folder() const {
    return _bank_folder;
}

//----------------------------------------------------------------------------
// preset_name
//----------------------------------------------------------------------------
std::string PresetId::preset_name() const {
    return _preset_name;
}

//----------------------------------------------------------------------------
// preset_display_name
//----------------------------------------------------------------------------
std::string PresetId::preset_display_name() const {
    // Return the formatted preset name
    auto name = std::string(_preset_name);
    uint index = (name[0] == '0') ? 1 : 0;                        
    name = name.substr(index, (name.size() - index));
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);
    return std::regex_replace(name, std::regex{"_"}, " ");
}

//----------------------------------------------------------------------------
// preset_display_name_short
//----------------------------------------------------------------------------
std::string PresetId::preset_display_name_short() const {
    // Return the formatted preset name (short, no index)
    auto name = _preset_name.substr((sizeof("000_") - 1), (_preset_name.size() - (sizeof("000_") - 1)));
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);
    return std::regex_replace(name, std::regex{"_"}, " ");
}

//----------------------------------------------------------------------------
// preset_edit_name
//----------------------------------------------------------------------------
std::string PresetId::preset_edit_name() const
{
    // Return the edit name
    std::string name = _preset_name.substr(4, _preset_name.size() - 4);
    return std::regex_replace(name, std::regex{"_"}, " ");
}

//----------------------------------------------------------------------------
// operator==
//----------------------------------------------------------------------------
bool PresetId::operator==(const PresetId& rhs) const { 
    return (_bank_folder == rhs._bank_folder) && (_preset_name == rhs._preset_name);
}

//----------------------------------------------------------------------------
// next_preset_id
//----------------------------------------------------------------------------
PresetId PresetId::next_preset_id() const
{
    PresetId preset_id;

    // Parse the bank folder for the presets available
    auto filenames = _parse_bank_folder();
    for (auto itr = filenames.begin(); itr != filenames.end(); ++itr) {
        // Is this the current preset AND there is a preset following?
        if ((itr->second == _preset_name) && (std::next(itr) != filenames.end())) {
            // Next preset found, return its ID
            preset_id.set_id(_bank_folder, std::next(itr)->second);
            break;
        }
    }
    return preset_id;
}

//----------------------------------------------------------------------------
// prev_preset_id
//----------------------------------------------------------------------------
PresetId PresetId::prev_preset_id() const
{
    PresetId preset_id;

    // Parse the bank folder for the presets available
    auto filenames = _parse_bank_folder();
    for (auto itr = filenames.begin(); itr != filenames.end(); ++itr) {
        // Is this the current preset AND there is a previous preset?
        if ((itr->second == _preset_name) && (itr != filenames.begin())) {
            // Previous preset found, return its ID
            preset_id.set_id(_bank_folder, std::prev(itr)->second);
            break;
        }
    }
    return preset_id;
}

//----------------------------------------------------------------------------
// set_id
//----------------------------------------------------------------------------
void PresetId::set_id(std::string id)
{
    // Initialise member data
    auto pos = id.find("/");
    if ((pos != 0) && (pos < (id.size() - 1))) {
        _bank_folder = id.substr(0, pos);
        _preset_name = id.substr((pos + 1), (id.size() - 1));
    } 
}

//----------------------------------------------------------------------------
// set_id
//----------------------------------------------------------------------------
void PresetId::set_id(std::string bank_folder, std::string preset_name)
{
    // Initialise member data
    if ((bank_folder.size() > 0) && (preset_name.size() > 0)) {
        _bank_folder = bank_folder;
        _preset_name = preset_name;
    }
}

//----------------------------------------------------------------------------
// set_fallback_id
//----------------------------------------------------------------------------
void PresetId::set_fallback_id()
{
    struct dirent **dirent = nullptr;
    int num_entries;
    std::string bank_folder;

    // Scan the PRESETS bank folder
    num_entries = ::scandir(common::MONIQUE_PRESETS_DIR, &dirent, 0, ::versionsort);
    if (num_entries > 0) {
        // Get the first folder
        for (uint i=0; i<(uint)num_entries; i++) {
            // Is this a folder?
            if ((dirent[i]->d_type == DT_DIR) && (std::strcmp(dirent[i]->d_name, ".") != 0) && (std::strcmp(dirent[i]->d_name, "..") != 0)) {
                bank_folder = dirent[i]->d_name;
                break;
            }
        }
    }

    // Bank folder found?
    if (!bank_folder.empty()) {
        // Reset the preset name
        _preset_name = "";

        // Scan the bank folder
        num_entries = ::scandir(MONIQUE_PRESET_FILE_PATH(bank_folder).c_str(), &dirent, 0, ::versionsort);
        if (num_entries > 0) {
            // Get the first preset file
            for (uint i=0; i<(uint)num_entries; i++) {
                // Is this a file?
                if (dirent[i]->d_type == DT_REG) {
                    // If it has a JSON file extension
                    auto name = std::string(dirent[i]->d_name);
                    if (name.substr((name.size() - (sizeof(".json") - 1))) == ".json") {
                        _bank_folder = bank_folder;
                        _preset_name = name.substr(0, (name.size() - (sizeof(".json") - 1)));
                        break;
                    }
                }
            }
        }

        // Preset not found?
        if (_preset_name.empty()) {
            // Default if no preset exists in the bank
            _bank_folder = bank_folder;
            _preset_name = PresetId::DefaultPresetName(1);      
        }        
    }
    if (dirent) {
        ::free(dirent);
    }
}

//----------------------------------------------------------------------------
// _parse_bank_folder
//----------------------------------------------------------------------------
std::map<uint, std::string> PresetId::_parse_bank_folder() const
{
    std::map<uint, std::string> filenames;

    // If the current preset is valid
    //if (is_valid()) {
        struct dirent **dirent = nullptr;

        // Scan the bank folder
        int num_files = ::scandir(MONIQUE_PRESET_FILE_PATH(_bank_folder).c_str(), &dirent, 0, ::versionsort);
        if (num_files > 0) {
            // Process each file in the folder - looking for the current preset
            for (uint i=0; i<(uint)num_files; i++) {
                // Is this a normal file?
                if (dirent[i]->d_type == DT_REG) {
                    // Get the preset index from the filename
                    // Note: If the filename format is invalid, atoi will return 0 - which is ok
                    // as this is an invalid preset index
                    uint index = std::atoi(dirent[i]->d_name);

                    // Are the first two characters the preset number?
                    if ((index > 0) && (dirent[i]->d_name[3] == '_')){
                        // Has this preset already been found?
                        // We ignore any duplicated presets with the same index
                        if (filenames[index].empty()) {                    
                            // Add the preset name
                            auto name = std::string(dirent[i]->d_name);
                            if (name.substr((name.size() - (sizeof(".json") - 1))) == ".json") {
                                filenames[index] = name.substr(0, (name.size() - (sizeof(".json") - 1)));
                            }                            
                            
                        }
                    }
                }
                ::free(dirent[i]);
            }
        }

        // We now need to make sure that the maximum number of presets is always shown
        // Any missing setups are shown as BASIC (the default) in the list
        for (uint i=1; i<=NUM_BANK_PRESET_FILES; i++) {
            // Does this preset exist?
            if (filenames[i].empty()) {
                // Set the default preset filename            
                filenames[i] = PresetId::DefaultPresetName(i);
            }
        }        
    //}
    return filenames;
}