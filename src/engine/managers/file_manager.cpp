/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  file_manager.cpp
 * @brief File Manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <fstream>
#include <ctime>
#include <filesystem>
#include <dirent.h>
#include <sys/stat.h>
#include <regex>
#include "file_manager.h"
#include "midi_device_manager.h"
#include "sfc.h"
#include "utils.h"
#include "logger.h"

// Preset versions - update these whenever the presets change
constexpr char PRESET_VERSION[] = "0.1.0";

// General module constants
constexpr char MANAGER_NAME[]                           = "FileManager";
constexpr char CONFIG_FILE[]                            = "config.json";
constexpr char HAPTIC_MODES_FILE[]                      = "haptic_modes.json";
constexpr char PARAM_BLACKLIST_FILE[]                   = "param_blacklist.json";
constexpr char PARAM_ATTRIBUTES_FILE[]                  = "param_attributes.json";
constexpr char PARAM_LISTS_FILE[]                       = "param_lists.json";
constexpr char PARAM_MAP_FILE[]                         = "param_map.json";
constexpr char SYSTEM_COLOURS_FILE[]                    = "system_colours.json";
constexpr char GLOBAL_PARAMS_FILE[]                     = "global_params.json";
constexpr char BASIC_PRESET_FILE[]                      = "BASIC_PRESET.json";
constexpr char CURRENT_PRESET_A_FILE[]                  = "current_preset_a.json";
constexpr char CURRENT_PRESET_B_FILE[]                  = "current_preset_b.json";
constexpr char PREV_PRESET_FILE[]                       = "prev_preset.json";
constexpr char BASIC_PRESET_L1_PATCH_NAME[]             = "INIT L1";
constexpr uint SAVE_CONFIG_FILE_IDLE_INTERVAL_US        = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds(2)).count();
constexpr uint SAVE_GLOBAL_PARAMS_FILE_IDLE_INTERVAL_MS = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds(2)).count();
constexpr uint SAVE_PRESET_FILE_IDLE_INTERVAL_MS        = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds(2)).count();
constexpr uint DEFAULT_DEMO_MODE_TIMEOUT                = std::chrono::seconds(300).count();

// Private static data
rapidjson::Document _basic_preset_json_data;

// Private static functions
bool _open_preset_file(std::string file_path, rapidjson::Document &json_data); 
bool _open_json_file(std::string file_path, const char *schema, rapidjson::Document &json_data, bool create=true, std::string def_contents="[]");

//----------------------------------------------------------------------------
// FileManager
//----------------------------------------------------------------------------
bool FileManager::PresetIsMultiTimbral(PresetId preset_id)
{
    bool ret = true;
    rapidjson::Document json_doc;

    // Open the preset file
    if (::_open_preset_file(preset_id.path(), json_doc)) {
        // Iterate through the common preset params
        for (rapidjson::Value::ValueIterator itr1 = json_doc["params"].Begin(); itr1 != json_doc["params"].End(); ++itr1) {
            // Is this the number of voices for Layer 2?
            if (itr1->GetObject()["path"].GetString() == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES)->path()) {
                // Check if the number of voices is zero (disabled layer)
                ret = itr1->GetObject()["value"].GetFloat() > 0;
                break;
            }
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// FileManager
//----------------------------------------------------------------------------
std::string FileManager::PresetLayerName(PresetId preset_id, LayerId layer_id)
{
    std::string name = "UNKNOWN";
    rapidjson::Document json_doc;

    // Open the preset file
    if (::_open_preset_file(preset_id.path(), json_doc)) {
        // Iterate through the layers
        for (rapidjson::Value::ValueIterator itr = json_doc["layers"].Begin(); itr != json_doc["layers"].End(); ++itr) {
            // Get the layer ID and check if it is the Layer we have requested
            auto id = itr->GetObject()["layer_id"].GetString();
            if ((std::strcmp(id,"d0") == 0) && (layer_id == LayerId::D0)) {
                // Return the patch name
                name = itr->GetObject()["patch"]["name"].GetString();
                break;
            }
            else if ((std::strcmp(id,"d1") == 0) && (layer_id == LayerId::D1)) {
                // Return the State A data
                name = itr->GetObject()["patch"]["name"].GetString();
                break;
            }
        }
    }
    return name;
}

//----------------------------------------------------------------------------
// FileManager
//----------------------------------------------------------------------------
FileManager::FileManager(EventRouter *event_router) : 
    BaseManager(MoniqueModule::FILE_MANAGER, MANAGER_NAME, event_router)
{
    // Initialise class data
    _param_changed_listener = nullptr;
    _system_func_listener = nullptr;
    _daw_manager = 0;
    _save_config_file_timer = new Timer(TimerType::ONE_SHOT);
    _save_global_params_file_timer = new Timer(TimerType::ONE_SHOT);
    _save_preset_file_timer = new Timer(TimerType::ONE_SHOT);

    // Open the param blacklist file and parse it
    _open_and_parse_param_blacklist_file();
}

//----------------------------------------------------------------------------
// ~FileManager
//----------------------------------------------------------------------------
FileManager::~FileManager()
{
    // Delete the listeners
    if (_param_changed_listener)
        delete _param_changed_listener;
    if (_system_func_listener)
        delete _system_func_listener;

    // Delete any specified timers
    if (_save_config_file_timer)
        delete _save_config_file_timer;             
    if (_save_global_params_file_timer)
        delete _save_global_params_file_timer;             
    if (_save_preset_file_timer)
        delete _save_preset_file_timer;             
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool FileManager::start()
{
    // Catch all exceptions
    try
    {
        // Get the DAW manager
        _daw_manager = static_cast<DawManager *>(utils::get_manager(MoniqueModule::DAW));
        
        // Open the config file and parse it
        if (!_open_config_file()) {
            // This is a critical error
            MSG("An error occurred opening the config file: " << MONIQUE_UDATA_FILE_PATH(CONFIG_FILE));
            MONIQUE_LOG_CRITICAL(module(), "An error occurred opening the config file: {}", MONIQUE_UDATA_FILE_PATH(CONFIG_FILE));
            return false;
        }
        _parse_config();

        // Open the system colours file and parse it
        _open_and_parse_system_colours_file();

        // Open the param map file and parse it
        if (!_open_param_map_file()) {
            // This is a critical error
            MSG("An error occurred opening the param map file: " << MONIQUE_ROOT_FILE_PATH(PARAM_MAP_FILE));
            MONIQUE_LOG_CRITICAL(module(), "An error occurred opening the param map file: {}", MONIQUE_ROOT_FILE_PATH(PARAM_MAP_FILE));
            return false;
        }
        _parse_param_map();

        // Open and parse the param attributes
        if (!_open_and_parse_param_attributes_file()) {
            // Log the error
            MSG("An error occurred opening param attributes file: " << MONIQUE_ROOT_FILE_PATH(PARAM_ATTRIBUTES_FILE));
            MONIQUE_LOG_ERROR(module(), "An error occurred opening param attributes file: {}", MONIQUE_ROOT_FILE_PATH(PARAM_ATTRIBUTES_FILE));          
        }        

        // Open and parse the param lists
        if (!_open_and_parse_param_lists_file()) {
            // Log the error
            MSG("An error occurred opening param lists file: " << MONIQUE_ROOT_FILE_PATH(PARAM_LISTS_FILE));
            MONIQUE_LOG_ERROR(module(), "An error occurred opening param lists file: {}", MONIQUE_ROOT_FILE_PATH(PARAM_LISTS_FILE));          
        }

        // Open and parse the global params
        if (!_open_and_parse_global_params_file()) {
            // Log the error
            MSG("An error occurred opening global params file: " << MONIQUE_UDATA_FILE_PATH(GLOBAL_PARAMS_FILE));
            MONIQUE_LOG_ERROR(module(), "An error occurred opening global params file: {}", MONIQUE_UDATA_FILE_PATH(GLOBAL_PARAMS_FILE));              
        }

        // Open and check the BASIC preset file
        if (!::_open_preset_file(MONIQUE_ROOT_FILE_PATH(BASIC_PRESET_FILE), ::_basic_preset_json_data) ||
            !_check_preset(::_basic_preset_json_data, _basic_preset_doc)) {
            // This is a critical error
            MSG("An error occurred opening BASIC preset file: " << MONIQUE_ROOT_FILE_PATH(BASIC_PRESET_FILE));
            MONIQUE_LOG_CRITICAL(module(), "An error occurred opening BASIC preset file: {}", MONIQUE_ROOT_FILE_PATH(BASIC_PRESET_FILE)); 
            return false;       
        }

        // Open the startup preset file to setup each layer
        if (!_open_and_check_startup_preset_file()) {
            // This is a critical error - note the function call above logs any errors
            return false;
        }

        // Parse the preset and check the Layer 1 patch name is correct
        _parse_preset();
        _check_layer_1_patch_name(utils::system_config()->preset_id());
        
        // Check if we are currently morphing
        _check_if_morphing();

        // Open the haptic modes file and parse it
        if (!_open_and_parse_haptic_modes_file())
        {
            // Log the error
            MSG("An error occurred opening haptic modes file: " << MONIQUE_ROOT_FILE_PATH(HAPTIC_MODES_FILE));
            MONIQUE_LOG_ERROR(module(), "An error occurred opening haptic modes file: {}", MONIQUE_ROOT_FILE_PATH(HAPTIC_MODES_FILE));          
        }        
    }
    catch(const std::exception& e)
    {
        // This is a critical error
        MSG("An exception occurred during the startup of the File Manager: " << e.what());
        MONIQUE_LOG_CRITICAL(module(), "An exception occurred during the startup of the File Manager: {}", e.what());
        return false;
    }
    
    // All OK, call the base manager
    return BaseManager::start();
}

//----------------------------------------------------------------------------
// stop
//----------------------------------------------------------------------------
void FileManager::stop()
{
    // Stop the various timers
    _save_config_file_timer->stop();
    _save_global_params_file_timer->stop();
    _save_preset_file_timer->stop();

    // Call the base manager function
    BaseManager::stop();
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void FileManager::process()
{
    // Listen for param change events from the various modules
    _param_changed_listener = new EventListener(MoniqueModule::SYSTEM, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_param_changed_listener);
    _system_func_listener = new EventListener(MoniqueModule::SYSTEM, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_system_func_listener);

    // Call the base manager to start processing events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void FileManager::process_event(const BaseEvent *event)
{
    // Process the event depending on the type
    switch (event->type())
    {
        case EventType::PARAM_CHANGED:
        {
            // Process the param changed event
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            break;
        }

        case EventType::SYSTEM_FUNC:
            // Process the system function event
            _process_system_func_event(static_cast<const SystemFuncEvent *>(event)->system_func());
            break;

        default:
            // Event unknown, we can ignore it
            break;
    }
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void FileManager::_process_param_changed_event(const ParamChange &param_change)
{
    // If the param has been specified
    if (param_change.param) {
        // If this is a preset param (and NOT from the pedals manager)
        if (param_change.param->preset() && (param_change.from_module != MoniqueModule::PEDALS)) {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_preset_mutex);

            // Is this not a global param
            if (param_change.param->type() != ParamType::GLOBAL) {
                // Find the param in the JSON patch data
                auto itr = _find_preset_param(param_change.param, _preset_doc);

                // If it wasn't found and this is a mod matrix param
                if (!itr && param_change.param->mod_matrix_param()) {
                    // The Mod Matrix param is not specified in the preset file
                    // Note the FX params are preset common params - however most are patch state params, so check them first
                    if (param_change.param->type() == ParamType::PATCH_STATE) {
                        // We need to add it to the preset as a state param
                        rapidjson::Value obj;
                        rapidjson::Value& json_data = _get_patch_state_json_data(utils::get_current_layer_info().layer_id(), 
                                                                                 utils::get_current_layer_info().layer_state(), 
                                                                                 param_change.param,
                                                                                 _preset_doc);
                        obj.SetObject();
                        obj.AddMember("path", std::string(param_change.param->path()), _preset_json_data.GetAllocator());
                        (param_change.param->data_type() == ParamDataType::FLOAT) ?
                            obj.AddMember("value", param_change.param->hr_value(), _preset_json_data.GetAllocator()) :
                            obj.AddMember("str_value", param_change.param->str_value(), _preset_json_data.GetAllocator());
                        json_data.PushBack(obj, _preset_json_data.GetAllocator());
                        itr = json_data.End() - 1;
                    }
                    else if (param_change.param->type() == ParamType::PRESET_COMMON) {
                        // We need to add it to the preset as a preset common param
                        rapidjson::Value obj;               
                        obj.SetObject();
                        obj.AddMember("path", std::string(param_change.param->path()), _preset_json_data.GetAllocator());
                        (param_change.param->data_type() == ParamDataType::FLOAT) ?
                            obj.AddMember("value", param_change.param->hr_value(), _preset_json_data.GetAllocator()) :
                            obj.AddMember("str_value", param_change.param->str_value(), _preset_json_data.GetAllocator());
                        _preset_doc.preset_common_params_json_data->PushBack(obj, _preset_json_data.GetAllocator());
                        itr = _preset_doc.preset_common_params_json_data->End() - 1;                          
                    }
                }

                // Is this a Sequencer chunk param?
                if ((param_change.param->type() == ParamType::PRESET_COMMON) && param_change.param->seq_chunk_param()) {
                    // If it was not found and not in a reset state, add it to the preset
                    if (!itr && !param_change.param->seq_chunk_param_is_reset()) {
                        // Add it
                        rapidjson::Value obj;               
                        obj.SetObject();
                        obj.AddMember("path", std::string(param_change.param->path()), _preset_json_data.GetAllocator());
                        obj.AddMember("str_value", param_change.param->str_value(), _preset_json_data.GetAllocator());
                        _preset_doc.preset_common_params_json_data->PushBack(obj, _preset_json_data.GetAllocator());
                        itr = _preset_doc.preset_common_params_json_data->End() - 1;   
                    }
                    // If it was found and is in a reset state, remove it from the preset
                    else if (itr && param_change.param->seq_chunk_param_is_reset()) {
                        // Remove it                   
                        _preset_doc.preset_common_params_json_data->Erase(itr);
                        itr = nullptr;

                        // Indicate the preset has been modified
                        utils::set_preset_modified(true);

                        // Restart the save setuo config timer to save the file if no param has changed
                        // for a time interval
                        _start_save_preset_file_timer();                           
                    }
                }
                if (itr)
                {
                    // Update the param value in the preset data
                    (param_change.param->data_type() == ParamDataType::FLOAT) ?
                        itr->GetObject()["value"].SetFloat(param_change.param->hr_value()) :
                        itr->GetObject()["str_value"].SetString(param_change.param->str_value(), _preset_json_data.GetAllocator());

                    // Indicate the preset has been modified
                    utils::set_preset_modified(true);

                    // Restart the save setuo config timer to save the file if no param has changed
                    // for a time interval
                    _start_save_preset_file_timer();                    
                }
            }
            else {
                // Find the param in the JSON global params data
                auto itr = _find_global_param(param_change.param->path());
                if (itr) {
                    // Update the param value in the global params data
                    (param_change.param->data_type() == ParamDataType::FLOAT) ?
                        itr->GetObject()["value"].SetFloat(param_change.param->hr_value()) :
                        itr->GetObject()["str_value"].SetString(param_change.param->str_value(), _global_params_json_data.GetAllocator());

                    // Restart the save global params timer to save the file if no param has changed
                    // for a time interval
                    _start_save_global_params_file_timer();
                }                  
            }
        }
    }
}

//----------------------------------------------------------------------------
// _process_system_func_event
//----------------------------------------------------------------------------
void FileManager::_process_system_func_event(const SystemFunc &system_func)
{
    // Process the event if it is a file manager system function
    switch (system_func.type) {
        case SystemFuncType::LOAD_LAYER_1: 
        case SystemFuncType::LOAD_LAYER_2: {
            // Get the preset mutex
            std::lock_guard<std::mutex> guard(_preset_mutex);

            // Lock and reset morphing
            utils::morph_lock();
            utils::reset_morph_state();

            // Select the specified Layer
            auto layer_id = system_func.type == SystemFuncType::LOAD_LAYER_1 ? LayerId::D0 : LayerId::D1;

            // Set the new layer
            utils::set_current_layer(layer_id);

            // Parse the Layer patch params for this Layer
            auto params = utils::get_preset_params();
            for (Param *p : params) {
                _process_layer_mapped_params(p, nullptr);
            }
            MSG("Switched to Layer: " << ((layer_id == LayerId::D0) ? "D0" : "D1") << " Layer");

            // Calculate and set the Layer voices - we need to do this in case the Layer data
            // has voice allocation settings that are incorrect or invalid
            _calc_and_set_layer_voices();

            // Set the selected layer
            auto param = utils::get_param(utils::ParamRef::SELECTED_LAYER);
            param->set_value(layer_id == LayerId::D0 ? 0 : 1);
            _daw_manager->set_param(param);

            // Handle any special case params
            _handle_preset_special_case_params(system_func.type);

            // Check if we are currently morphing
            _check_if_morphing();

            // Send an event to get the managers to reload their presets
            auto event = new ReloadPresetsEvent(module());
            event->set_from_layer_toggle();
            _event_router->post_reload_presets_event(event);

            // Unlock the morph
            utils::morph_unlock();            
            break;
        }

        case SystemFuncType::TOGGLE_PATCH_STATE:
        {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_preset_mutex);

            // Has the patch state actually changed?
            auto new_state = system_func.value == 0 ? LayerState::STATE_A: LayerState::STATE_B;
            if (new_state != utils::get_current_layer_info().layer_state()) {
                // Lock and reset morphing
                utils::morph_lock();
                utils::reset_morph_state();

                // Set the new patch state
                utils::get_current_layer_info().set_layer_state(new_state);

                // Parse the Layer state patch params for this Layer
                auto params = utils::get_preset_params();
                _parse_patch_state_params(params, new_state);
                MSG("Selected Layer patch State " << ((new_state == LayerState::STATE_A) ? "A" : "B"));  

                // Set the layer state
                auto param = utils::get_param(utils::ParamRef::STATE);
                param->set_value(new_state == LayerState::STATE_A ? 0 : 1);
                _daw_manager->set_param(param);

                // Handle any special case params
                _handle_preset_special_case_params(system_func.type);

                // Set the Morph value and knob param for STATE A/B
                float morph_value = (new_state == LayerState::STATE_A) ? 0.0 : 1.0;
                if (utils::get_morph_value_param()) {
                    // Set the Morph params
                    utils::get_morph_value_param()->set_value(morph_value);

                    // Send the morph value param to the daw - wait 5ms before processing so that
                    // any state change can be fully processed by at least one buffer first
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    _daw_manager->set_param(utils::get_morph_value_param());

                    // Update the morph knob              
                    if (utils::get_morph_knob_param()) {
                        // Make sure the Morph Knonb is in the default state BEFORE setting the value
                        auto p = utils::get_morph_knob_param();
                        p->set_control_state("default");
                        p->set_value(morph_value);
                    }

                    // We also save the value in the preset - *always* save this as zero, so if Delia is
                    // restarted (in State A) the morph value is correct
                    auto itr = _find_preset_param(utils::get_morph_value_param(), _preset_doc);
                    if (itr) {
                        itr->GetObject()["value"].SetFloat(0.0f);
                        _start_save_preset_file_timer();
                    }   
                }
                utils::get_current_layer_info().set_morph_value(morph_value);
            
                // Send an event to get the managers to reload their presets
                auto event = new ReloadPresetsEvent(module());
                event->set_from_ab_toggle();
                _event_router->post_reload_presets_event(event);

                // Unlock the morph
                utils::morph_unlock();
            }
            break;
        }

        case SystemFuncType::SET_MOD_SRC_NUM: {
            // Save the new modulation source number
            auto num = system_func.num + 1;
            utils::system_config()->set_mod_src_num(num);
            _config_json_data["mod_src_num"].SetUint(num);
            _start_save_config_file_timer();
            break;              
        }

        case SystemFuncType::LOAD_PRESET: {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_preset_mutex);
            const PresetId& preset_id = system_func.preset_id;

            // Try to open the preset file
            if (_open_and_check_preset_file(preset_id.path())) {
                // Lock and reset morphing
                utils::morph_lock();
                utils::reset_morph_state();         
                
                // Before we load the preset, make a backup of the current preset state,
                // so it can be restored if need be
                auto prev_preset_id = utils::system_config()->preset_id();
                std::filesystem::copy_file(_current_preset_save_state == CurrentPresetSaveState::A ?
                                                MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_B_FILE) :
                                                MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_A_FILE),
                                           MONIQUE_UDATA_FILE_PATH(PREV_PRESET_FILE),
                                           std::filesystem::copy_options::overwrite_existing);

                // Parse the preset and check the Layer 1 patch name is correct
                _parse_preset();
                _check_layer_1_patch_name(preset_id);

                // Check if we are currently morphing
                _check_if_morphing();

                // Overwrite the current preset file
                // Note: Save twice so that both current preset files are the same and represet the
                // opened preset
                _current_preset_save_state = CurrentPresetSaveState::B;
                _save_current_preset_file();
                _save_current_preset_file();
                MSG("Loaded Preset: " << preset_id.path());

                // Save the Preset ID and previous Preset ID
                utils::system_config()->set_preset_id(preset_id);
                utils::system_config()->set_prev_preset_id(prev_preset_id);
                utils::set_preset_modified(false);
                _config_json_data["preset_id"].SetString(preset_id.id(), _config_json_data.GetAllocator());
                _config_json_data["prev_preset_id"].SetString(prev_preset_id.id(), _config_json_data.GetAllocator());
                _start_save_config_file_timer();

                // Send an event to get the managers to re-load their presets
                _event_router->post_reload_presets_event(new ReloadPresetsEvent(module()));

                // Unlock the morph
                utils::morph_unlock();             
            }
            break;
        }

        case SystemFuncType::LOAD_PRESET_LAYER: {
            rapidjson::Document from_preset_json_doc;
            const PresetId& preset_id = system_func.preset_id;

            // Get the preset mutex
            std::lock_guard<std::mutex> guard(_preset_mutex);
            
            // Try to open the preset file
            if (::_open_preset_file(preset_id.path(), from_preset_json_doc)) {
                // File opened successfully - lock and reset morphing
                utils::morph_lock();
                utils::reset_morph_state();

                // Copy the source Layer json data into the destination Layer json data
                // Note we need to also make sure the Layer ID is correct in the Layer json data
                auto dst_layer_json_data = _get_layer_json_data(system_func.dst_layer_id, _preset_json_data);
                dst_layer_json_data->CopyFrom(*_get_layer_json_data(system_func.src_layer_id, from_preset_json_doc), _preset_json_data.GetAllocator());
                if (dst_layer_json_data->GetObject().HasMember("layer_id") && dst_layer_json_data->GetObject()["layer_id"].IsString()) {
                    (system_func.dst_layer_id == LayerId::D0) ?
                        dst_layer_json_data->GetObject()["layer_id"].SetString("d0", _preset_json_data.GetAllocator()) :
                        dst_layer_json_data->GetObject()["layer_id"].SetString("d1", _preset_json_data.GetAllocator());
                }                
                (void)_check_preset(_preset_json_data, _preset_doc);

                // Setup the loaded layer
                // NOTE: We do NOT include the Layer params for processing, only the patch common and state params
                // are loaded
                auto params = utils::get_preset_params();
                _setup_layer(system_func.dst_layer_id, params, false);

                // If loading into Layer 2, we also need to make sure that at least one voice is assigned
                if ((system_func.dst_layer_id == LayerId::D1) && (utils::get_layer_info(LayerId::D1).num_voices() == 0)) {
                    auto param1 = utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES);
                    auto param2 = utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES);
                    if (param1 && param2) {
                        // Set the Layer 1 voices to (max - 1), and the Layer 2 voices to 1
                        param1->set_hr_value(common::NUM_VOICES - 1);
                        utils::get_layer_info(LayerId::D0).set_num_voices(common::NUM_VOICES - 1);
                        _daw_manager->set_param(param1);                       
                        param2->set_hr_value(1);
                        utils::get_layer_info(LayerId::D1).set_num_voices(1);
                        _daw_manager->set_param(param2);

                        // We also save the values in the preset
                        auto itr = _find_preset_param(param1, _preset_doc);
                        if (itr) {
                            itr->GetObject()["value"].SetFloat(param1->hr_value());
                        }
                        itr = _find_preset_param(param2, _preset_doc);
                        if (itr) {
                            itr->GetObject()["value"].SetFloat(param2->hr_value());
                        }                                               
                    }
                }

                // Calculate and set the Layer voices - we need to do this in case the Layer data
                // has voice allocation settings that are incorrect or invalid
                _calc_and_set_layer_voices();

                // Make sure the Layer 1 patch name is correct
                _check_layer_1_patch_name(preset_id);

                // Indicate the Layer is loaded
                utils::set_preset_modified(true);
                _current_preset_save_state = CurrentPresetSaveState::B;
                _save_current_preset_file();
                _save_current_preset_file();
                MSG("Loaded into Layer " << ((system_func.dst_layer_id == LayerId::D0) ? "D0" : "D1"));

                // Did we load into the current Layer?
                if (system_func.dst_layer_id == utils::get_current_layer_info().layer_id()) {
                    // Handle any special case params
                    _handle_preset_special_case_params(system_func.type);
                }

                // Check if we are currently morphing
                _check_if_morphing();

                // Send an event to get the managers to re-load their presets
                auto event = new ReloadPresetsEvent(module());
                event->set_from_layer_toggle();
                _event_router->post_reload_presets_event(event);

                // Unlock the morph
                utils::morph_unlock();
            }
            else {
                // Could not open the from patch file
                DEBUG_BASEMGR_MSG("An error occurred opening the patch file: " << preset_id.path());
            }
        }
        break;


        case SystemFuncType::LOAD_PRESET_SOUND: {
            rapidjson::Document src_preset_json_doc;
            const PresetId& preset_id = system_func.preset_id;

            // Get the preset mutex
            std::lock_guard<std::mutex> guard(_preset_mutex);
            
            // Try to open the preset file
            if (::_open_preset_file(preset_id.path(), src_preset_json_doc)) {
                // File opened successfully - lock and reset morphing
                utils::morph_lock();
                utils::reset_morph_state();

                // Get the source and destination json data
                auto src_layer_json_data = _get_patch_state_a_json_data(system_func.src_layer_id, src_preset_json_doc);
                rapidjson::Value& dst_layer_json_data = _get_patch_state_b_json_data(system_func.dst_layer_id, _preset_doc);
                if (src_layer_json_data) {
                    // Copy the source Layer json data into the destination Layer json data
                    dst_layer_json_data.CopyFrom(*src_layer_json_data, _preset_json_data.GetAllocator());                
                    (void)_check_preset(_preset_json_data, _preset_doc);

                    // Select the specified Layer
                    utils::set_current_layer(system_func.dst_layer_id);

                    // Set the Layer state
                    utils::get_current_layer_info().set_layer_state(system_func.dst_layer_state);

                    // Parse the Layer state patch params for this Layer
                    auto params = utils::get_preset_params();
                    _parse_patch_state_params(params, system_func.dst_layer_state);
                    _daw_manager->set_layer_patch_state_params(system_func.dst_layer_id, system_func.dst_layer_state);
                    utils::set_preset_modified(true);
                    _start_save_preset_file_timer();                    
                    MSG("Loaded into Layer " << ((system_func.dst_layer_id == LayerId::D0) ? "D0" : "D1") << " State " << ((system_func.dst_layer_state == LayerState::STATE_A) ? "A" : "B"));

                    // Set the selected layer
                    auto param = utils::get_param(utils::ParamRef::SELECTED_LAYER);
                    param->set_value(system_func.dst_layer_id == LayerId::D0 ? 0 : 1);
                    _daw_manager->set_param(param);     

                    // Set the layer state
                    param = utils::get_param(utils::ParamRef::STATE);
                    param->set_value(system_func.dst_layer_state == LayerState::STATE_A ? 0 : 1);
                    _daw_manager->set_param(param);

                    // Handle any special case params
                    _handle_preset_special_case_params(system_func.type);                 

                    // Set the Morph value and knob param for STATE A/B
                    float morph_value = (system_func.dst_layer_state == LayerState::STATE_A) ? 0.0 : 1.0;
                    if (utils::get_morph_value_param()) {
                        // Set the Morph params
                        utils::get_morph_value_param()->set_value(morph_value);

                        // Send the morph value param to the daw - wait 5ms before processing so that
                        // any state change can be fully processed by at least one buffer first
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        _daw_manager->set_param(utils::get_morph_value_param());

                        // Update the morph knob              
                        if (utils::get_morph_knob_param()) {
                            utils::get_morph_knob_param()->set_value(morph_value);
                        }

                        // We also save the value in the preset - *always* save this as zero, so if Delia is
                        // restarted (in State A) the morph value is correct
                        auto itr = _find_preset_param(utils::get_morph_value_param(), _preset_doc);
                        if (itr) {
                            itr->GetObject()["value"].SetFloat(0.0f);
                            _start_save_preset_file_timer();
                        }   
                    }
                    utils::get_current_layer_info().set_morph_value(morph_value);
            
                    // Send an event to get the managers to reload their presets
                    auto event = new ReloadPresetsEvent(module());
                    event->set_from_ab_toggle();
                    _event_router->post_reload_presets_event(event);                  
                }

                // Unlock the morph
                utils::morph_unlock();
            }
            else {
                // Could not open the from patch file
                DEBUG_BASEMGR_MSG("An error occurred opening the patch file: " << preset_id.path());
            }
            break;
        }

        case SystemFuncType::RESTORE_PREV_PRESET: {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_preset_mutex);
            const PresetId& preset_id = utils::system_config()->prev_preset_id();

            // Try to open the previous preset file
            if (preset_id.is_valid() && 
                std::filesystem::exists(MONIQUE_UDATA_FILE_PATH(PREV_PRESET_FILE)) &&
                _open_and_check_preset_file(MONIQUE_UDATA_FILE_PATH(PREV_PRESET_FILE))) {
                // Lock and reset morphing
                utils::morph_lock();
                utils::reset_morph_state();

                // Parse the preset and check the Layer 1 patch name is correct
                _parse_preset();
                _check_layer_1_patch_name(preset_id);

                // Check if we are currently morphing
                _check_if_morphing();

                // Overwrite the current preset file
                // Note: Save twice so that both current preset files are the same and represet the
                // opened preset
                _current_preset_save_state = CurrentPresetSaveState::B;
                _save_current_preset_file();
                _save_current_preset_file();
                MSG("Restore Previous Preset: " << preset_id.path());

                // Save the Preset ID and clear the previous Preset ID
                utils::system_config()->set_preset_id(preset_id);
                utils::system_config()->set_prev_preset_id(PresetId());
                utils::set_preset_modified(false);
                _config_json_data["preset_id"].SetString(utils::system_config()->preset_id().id(), _config_json_data.GetAllocator());
                _config_json_data["prev_preset_id"].SetString(utils::system_config()->prev_preset_id().id(), _config_json_data.GetAllocator());
                _start_save_config_file_timer();

                // Send an event to get the managers to re-load their presets
                _event_router->post_reload_presets_event(new ReloadPresetsEvent(module()));

                // Unlock the morph
                utils::morph_unlock();
            }            
            break;
        }

        case SystemFuncType::SAVE_PRESET: {
            // Get the preset mutex
            std::lock_guard<std::mutex> guard(_preset_mutex);
            const PresetId& preset_id = system_func.preset_id;

            // Save the preset file
            utils::morph_lock();
            _save_preset_file(preset_id.path());
            utils::morph_unlock();
            MSG("Saved Preset file: " << preset_id.path());

            // Save the Preset ID if it has changed
            if (utils::system_config()->preset_id() != preset_id) {
                utils::system_config()->set_preset_id(preset_id);
                utils::set_preset_modified(false);
                _config_json_data["preset_id"].SetString(preset_id.id(), _config_json_data.GetAllocator());
                _start_save_config_file_timer();
            }
            break;                        
        }

        case SystemFuncType::INIT_PRESET: {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_preset_mutex);
            const PresetId& preset_id = utils::system_config()->preset_id();

            // Lock and reset morphing
            utils::morph_lock();
            utils::reset_morph_state();

            // Before initialising the preset, get the patch names so that we can reinstate them after the init
            std::string l1_patch_name = "";
            std::string l2_patch_name = "";
            if (_preset_doc.d0_patch_json_data->HasMember("name") && (*_preset_doc.d0_patch_json_data)["name"].IsString()) {
                l1_patch_name = (*_preset_doc.d0_patch_json_data)["name"].GetString();
            }
            if (_preset_doc.d1_patch_json_data->HasMember("name") && (*_preset_doc.d0_patch_json_data)["name"].IsString()) {
                l2_patch_name = (*_preset_doc.d1_patch_json_data)["name"].GetString();
            }

            // Copy the BASIC preset data to the current document
            // Note we can safely assume it checks ok, but still need to run the check to setup the data pointers
            _preset_json_data.CopyFrom(::_basic_preset_json_data, _preset_json_data.GetAllocator());
            (void)_check_preset(_preset_json_data, _preset_doc);

            // Reinstate the patch names where defined
            if (!l1_patch_name.empty() && _preset_doc.d0_patch_json_data->HasMember("name") && (*_preset_doc.d0_patch_json_data)["name"].IsString()) {
                (*_preset_doc.d0_patch_json_data)["name"].SetString(l1_patch_name, _preset_json_data.GetAllocator());
                utils::get_layer_info(LayerId::D0).set_patch_name(l1_patch_name);
            }
            if (!l2_patch_name.empty() && _preset_doc.d1_patch_json_data->HasMember("name") && (*_preset_doc.d1_patch_json_data)["name"].IsString()) {
                (*_preset_doc.d1_patch_json_data)["name"].SetString(l2_patch_name, _preset_json_data.GetAllocator());
                utils::get_layer_info(LayerId::D1).set_patch_name(l2_patch_name);
            }

            // Parse the preset
            _parse_preset();          

            // Check if we are currently morphing
            _check_if_morphing();

            // Overwrite the current preset file
            // Note: Save twice so that both current preset files are the same and represet the
            // opened preset
            _current_preset_save_state = CurrentPresetSaveState::B;
            _save_current_preset_file();
            _save_current_preset_file();
            utils::set_preset_modified(true);
            MSG("INIT Preset: " << preset_id.path());

            // Send an event to get the managers to re-load their presets
            _event_router->post_reload_presets_event(new ReloadPresetsEvent(module()));

            // Unlock the morph
            utils::morph_unlock();
            break;
        }

        case SystemFuncType::STORE_MORPH_TO_PRESET_SOUND: {
            // Get the preset mutex
            std::lock_guard<std::mutex> guard(_preset_mutex);
            bool switch_states = utils::get_current_layer_info().layer_state() != system_func.dst_layer_state;
            
            // File opened successfully - lock and reset morphing
            utils::morph_lock();
            utils::reset_morph_state();

            // Update the patch state params
            _update_patch_state_params(system_func.dst_layer_state);

            // If we are saving to a different state than the current, we need to
            // switch to that state 
            if (switch_states) {
                // Set the new patch state
                utils::get_current_layer_info().set_layer_state(system_func.dst_layer_state);

                // Set the layer state
                auto param = utils::get_param(utils::ParamRef::STATE);
                param->set_value(system_func.dst_layer_state == LayerState::STATE_A ? 0 : 1);
                _daw_manager->set_param(param);

                // Handle any special case params
                _handle_preset_special_case_params(system_func.type);
            }

            // Set the Morph value and knob param for STATE A/B
            float morph_value = (utils::get_current_layer_info().layer_state() == LayerState::STATE_A) ? 0.0 : 1.0;
            if (utils::get_morph_value_param()) {
                // Set the Morph param
                utils::get_morph_value_param()->set_value(morph_value);

                // Send the morph value param to the daw - wait 5ms before processing so that
                // any state change can be fully processed by at least one buffer first
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                _daw_manager->set_param(utils::get_morph_value_param());

                // Update the morph knob              
                if (utils::get_morph_knob_param()) {
                    // Note: Update the Morph knob param and send a param change so that its position
                    // is updated
                    utils::get_morph_knob_param()->set_value(morph_value);
                    auto param_change = ParamChange(utils::get_morph_knob_param(), module());       
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                }

                // We also save the value in the preset - *always* save this as zero, so if Delia is
                // restarted (in State A) the morph value is correct
                auto itr = _find_preset_param(utils::get_morph_value_param(), _preset_doc);
                if (itr) {
                    itr->GetObject()["value"].SetFloat(0.0f);
                }   
            }
            utils::get_current_layer_info().set_morph_value(morph_value);

            // Update the state params in the DAW
            _daw_manager->set_layer_patch_state_params(utils::get_current_layer_info().layer_id(), system_func.dst_layer_state);

            // Indicate the preset has been modified and save it
            utils::set_preset_modified(true);
            MSG("Saved Morph to: Sound  " << ((system_func.dst_layer_state == LayerState::STATE_A) ? "A" : "B"));
            _start_save_preset_file_timer();

            // If we updated the other state, then get the managers to reload the reset current state
            if (switch_states) {
                // Send an event to get the managers to reload their presets - note indicate it is from a layer toggle,
                // even though it really isn't, we just want the managers to process the reset state
                auto event = new ReloadPresetsEvent(module());
                event->set_from_ab_toggle();
                _event_router->post_reload_presets_event(event);
            }

            // Unlock the morph
            utils::morph_unlock();
        }
        break;


        case SystemFuncType::BANK_RENAMED: {
            // Get the preset mutex
            std::lock_guard<std::mutex> guard(_preset_mutex);
            const std::string& org_folder = system_func.str_value;
            const std::string& new_folder = system_func.str_value_2;

            // Has the current preset or previous bank been renamed?
            if (utils::system_config()->preset_id().bank_folder() == org_folder) {
                // Yes, update it in the system config preset ID and config file
                auto preset_id = PresetId();
                preset_id.set_id(new_folder, utils::system_config()->preset_id().preset_name());
                utils::system_config()->set_preset_id(preset_id);
                _config_json_data["preset_id"].SetString(preset_id.id(), _config_json_data.GetAllocator());
                _start_save_config_file_timer();                
            }
            if (utils::system_config()->prev_preset_id().bank_folder() == org_folder) {
                // Yes, update it in the system config previous preset ID and config file
                auto prev_preset_id = PresetId();
                prev_preset_id.set_id(new_folder, utils::system_config()->prev_preset_id().preset_name());
                utils::system_config()->set_prev_preset_id(prev_preset_id);
                _config_json_data["prev_preset_id"].SetString(prev_preset_id.id(), _config_json_data.GetAllocator());
                _start_save_config_file_timer();                
            }            
            break;                        
        }

        case SystemFuncType::PATCH_RENAMED: {
            // Get the preset mutex
            std::lock_guard<std::mutex> guard(_preset_mutex);
            const std::string& patch_name = system_func.str_value;

            // Update the patch name in the preset
            rapidjson::Value& json_data = (utils::get_current_layer_info().layer_id() == LayerId::D0) ? *_preset_doc.d0_patch_json_data : *_preset_doc.d1_patch_json_data;
            if (json_data.HasMember("name") && json_data["name"].IsString()) {
                json_data["name"].SetString(patch_name, _preset_json_data.GetAllocator());
                utils::set_preset_modified(true);
                _start_save_preset_file_timer();                
            }
            break;                        
        }

        case SystemFuncType::SAVE_DEMO_MODE: {
            // Save the demo mode state
            _config_json_data["demo_mode"].SetBool(utils::system_config()->get_demo_mode());
            _start_save_config_file_timer();              
        }
        break;   

        case SystemFuncType::SET_SYSTEM_COLOUR: {
            // Save the new system colour
            utils::system_config()->set_system_colour(system_func.str_value);
            _config_json_data["system_colour"].SetString(system_func.str_value, _config_json_data.GetAllocator());
            _start_save_config_file_timer();           
            break;
        }        

        default:
            break;    
    }
}

//----------------------------------------------------------------------------
// _reset_layers
//----------------------------------------------------------------------------
void FileManager::_reset_layers()
{
    // Set the selected layer to D0
    auto param = utils::get_param(utils::ParamRef::SELECTED_LAYER);
    param->set_value(0);
    _daw_manager->set_param(param);     

    // Set the layer state to State A
    param = utils::get_param(utils::ParamRef::STATE);
    param->set_value(0);
    _daw_manager->set_param(param);

    // Set the morph value to 0
    param = utils::get_morph_value_param();
    param->set_value(0);
    _daw_manager->set_param(param);    
}

//----------------------------------------------------------------------------
// _setup_layer
//----------------------------------------------------------------------------
void FileManager::_setup_layer(LayerId layer_id, std::vector<Param *> &params, bool inc_layer_params)
{
    // Select and reset the specified Layer
    utils::set_current_layer(layer_id);
    utils::get_current_layer_info().reset();

    // Parse the Layer
    _parse_preset_layer(params, inc_layer_params);

    // Save the layer morph position
    utils::get_current_layer_info().set_morph_value(utils::get_morph_value_param()->value());

    // Set the selected layer
    auto param = utils::get_param(utils::ParamRef::SELECTED_LAYER);
    param->set_value(layer_id == LayerId::D0 ? 0 : 1);
    _daw_manager->set_param(param);     

    // Set the layer state to State A
    param = utils::get_param(utils::ParamRef::STATE);
    param->set_value(0);
    _daw_manager->set_param(param);

    // Send the morph value param to the daw - wait 5ms before processing so that
    // any state change can be fully processed by at least one buffer first
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    _daw_manager->set_param(utils::get_morph_value_param());

    // Get the Layer MIDI Channel Filter and set in the layer info structure
    // We do this for convienence and performance
    param = utils::get_param(utils::ParamRef::MIDI_CHANNEL_FILTER);
    if (param) {
        // Set the MIDI channel filter
        utils::get_layer_info(layer_id).set_midi_channel_filter(param->position_value());
    }
    MSG("Setup " << ((layer_id == LayerId::D0) ? "D0" : "D1") << " Layer");

    // Small delay so that the params and morph can be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

//----------------------------------------------------------------------------
// _open_config_file
//----------------------------------------------------------------------------
bool FileManager::_open_config_file()
{
    const char *schema =
#include "../json_schemas/config_schema.json"
;
    
    // Open the config file
    bool ret = ::_open_json_file(MONIQUE_UDATA_FILE_PATH(CONFIG_FILE), schema, _config_json_data, "{}");
    if (ret)
    {
        // If the JSON data is empty (not an object), make it one!
        if (!_config_json_data.IsObject())
            _config_json_data.SetObject();
    }
    return ret;
}

//----------------------------------------------------------------------------
// _open_and_parse_param_blacklist_file
//----------------------------------------------------------------------------
void FileManager::_open_and_parse_param_blacklist_file()
{
    const char *schema =
#include "../json_schemas/param_blacklist_schema.json"
;
    rapidjson::Document json_data;
    
    // Open the param blacklist file (don't create it if it doesn't exist)
    if (::_open_json_file(MONIQUE_ROOT_FILE_PATH(PARAM_BLACKLIST_FILE), schema, json_data, false))
    {
        // If the JSON data is empty its an invalid file
        if (json_data.IsArray())
        {
            // Iterate through the blacklisted params
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
            {
                // Is the blacklist param entry valid?
                if (itr->GetObject().HasMember("param") && itr->GetObject()["param"].IsString())
                {
                    // Blacklist this param
                    utils::blacklist_param(itr->GetObject()["param"].GetString());
                }
            }
        }
    }
}

//----------------------------------------------------------------------------
// _open_param_map_file
//----------------------------------------------------------------------------
bool FileManager::_open_param_map_file()
{
    const char *schema =
#include "../json_schemas/param_map_schema.json"
;
    
    // Open the param map file
    bool ret = ::_open_json_file(MONIQUE_ROOT_FILE_PATH(PARAM_MAP_FILE), schema, _param_map_json_data);
    if (ret)
    {
        // If the JSON data is empty its an invalid file
        if (!_param_map_json_data.IsArray())
            ret = false;       
    }
    return ret;
}

//----------------------------------------------------------------------------
// _open_and_parse_param_attributes_file
//----------------------------------------------------------------------------
bool FileManager::_open_and_parse_param_attributes_file()
{
    const char *schema =
#include "../json_schemas/param_attributes_schema.json"
;
    rapidjson::Document json_data;

    // Open the param atttributes file
    bool ret = ::_open_json_file(MONIQUE_ROOT_FILE_PATH(PARAM_ATTRIBUTES_FILE), schema, json_data, false);
    if (ret)
    {
        // If the JSON data is empty its an invalid file
        if (!json_data.IsArray())
            return false;

        // Parse the param attributes file
        // If the JSON data is not an array don't parse it
        if (json_data.IsArray())
        {
            // Iterate through the params
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
            {
                // Is the param path present?
                if (itr->GetObject().HasMember("param_path") && itr->GetObject()["param_path"].IsString())
                {
                    // Get the param path and find the param(s)
                    auto param_path = itr->GetObject()["param_path"].GetString();
                    auto params = utils::get_params(param_path);
                    for (Param *p : params) {
                        // Check for the various attributes that can be specified
                        if (itr->GetObject().HasMember("ref") && itr->GetObject()["ref"].IsString()) {
                            // Get the reference for this param
                            p->set_ref(itr->GetObject()["ref"].GetString());

                            // Additional processing
                            // If this param is the morph value param, check the other mapped
                            // params and if a physical control, set it as the morph knob
                            // Note: Assumes there is only one control mapped to morph
                            if (utils::param_has_ref(p, utils::ParamRef::MORPH_VALUE)) {
                                // Set this as the morph value param
                                utils::set_morph_value_param(p);

                                // Get the mapped params and find the first Surface Control knob mapped
                                auto mapped_params = p->mapped_params(nullptr);
                                for (Param *mp : mapped_params) {
                                    // Get the mapped control, if it is a Surface Control knob save it as
                                    // the morph knob param
                                    if ((mp->module() == MoniqueModule::SFC_CONTROL) && (static_cast<SfcControlParam *>(mp)->control_type() == sfc::ControlType::KNOB)) {
                                        utils::set_morph_knob_param(static_cast<KnobParam *>(mp));
                                        break;
                                    }
                                }
                            }
                            // If this param is the morph mode param
                            else if (utils::param_has_ref(p, utils::ParamRef::MORPH_MODE)) {
                                // Set this as the morph mode param
                                utils::set_morph_mode_param(p);
                            }
                        }
                        if (itr->GetObject().HasMember("preset") && itr->GetObject()["preset"].IsBool()) {
                            // Set the preset attribute for the param
                            p->set_preset(itr->GetObject()["preset"].GetBool());                         
                        }
                        if (itr->GetObject().HasMember("save") && itr->GetObject()["save"].IsBool()) {
                            // Set the save attribute for the param
                            p->set_save(itr->GetObject()["save"].GetBool());                    
                        }                                                                                                              
                        if (itr->GetObject().HasMember("display_name") && itr->GetObject()["display_name"].IsString()) {
                            // Set the display name string -an empty string means the GUI will not display it
                            p->set_display_name(itr->GetObject()["display_name"].GetString());
                        }
                        if (p->data_type() == ParamDataType::FLOAT) {
                            if (itr->GetObject().HasMember("num_positions") && itr->GetObject()["num_positions"].IsUint()) {
                                // Set this as a position param
                                p->set_position_param(itr->GetObject()["num_positions"].GetUint());
                            }                       
                            if (itr->GetObject().HasMember("display_min_value") && itr->GetObject()["display_min_value"].IsInt()) {
                                // Set the display range min
                                p->set_display_min_value(itr->GetObject()["display_min_value"].GetInt());
                            }
                            if (itr->GetObject().HasMember("display_max_value") && itr->GetObject()["display_max_value"].IsInt()) {
                                // Set the display range max
                                p->set_display_max_value(itr->GetObject()["display_max_value"].GetInt());
                            }
                            if (itr->GetObject().HasMember("display_decimal_places") && itr->GetObject()["display_decimal_places"].IsInt()) {
                                // Set the display decimal places
                                p->set_display_decimal_places(itr->GetObject()["display_decimal_places"].GetInt());
                            }                            
                            if (itr->GetObject().HasMember("value_strings") && itr->GetObject()["value_strings"].IsArray()) {
                                // Parse each value string
                                auto value_strings = itr->GetObject()["value_strings"].GetArray();
                                for (auto& vs : value_strings) {
                                    // If a value string has been specified
                                    if (vs.HasMember("string") && vs["string"].IsString()) {
                                        // Add the Dvalue string
                                        p->add_value_string(vs["string"].GetString());
                                    }                                      
                                }
                            }
                            if (itr->GetObject().HasMember("value_tag") && itr->GetObject()["value_tag"].IsString()) {
                                // Add the Value Tag
                                p->add_value_tag(itr->GetObject()["value_tag"].GetString());
                            }                            
                            if (itr->GetObject().HasMember("value_tags") && itr->GetObject()["value_tags"].IsArray()) {
                                // Parse each Value Tag
                                auto value_tags = itr->GetObject()["value_tags"].GetArray();
                                for (auto& vt : value_tags) {
                                    // If a Value Tag has been specified
                                    if (vt.HasMember("string") && vt["string"].IsString()) {
                                        // Add the Value Tag
                                        p->add_value_tag(vt["string"].GetString());
                                    }                                      
                                }
                            }                                                       
                        }                  
                        if (itr->GetObject().HasMember("display_as_numeric") && itr->GetObject()["display_as_numeric"].IsBool()) {
                            // Indicate the param should be displayed as a numeric param, even if it has values as strings
                            p->set_display_as_numeric(itr->GetObject()["display_as_numeric"].GetBool());
                        } 
                        if (itr->GetObject().HasMember("param_list") && itr->GetObject()["param_list"].IsString()) {
                            // Set the param list name for this param
                            p->set_param_list_name(itr->GetObject()["param_list"].GetString());
                        }
                        if (itr->GetObject().HasMember("linked_param") && itr->GetObject()["linked_param"].IsString()) {
                            // If the param exists and this is a System Func param
                            auto param = utils::get_param(itr->GetObject()["linked_param"].GetString());
                            if (param && p->type() == ParamType::SYSTEM_FUNC) {
                                // Set the linked param
                                static_cast<SystemFuncParam *>(p)->set_linked_param(param);
                            }
                        }
                        if (itr->GetObject().HasMember("display_enum_list") && itr->GetObject()["display_enum_list"].IsBool()) {
                            // Indicate whether this param, if an enum param, should be shown as a list or not
                            p->set_display_enum_list(itr->GetObject()["display_enum_list"].GetBool());
                        }
                        if (itr->GetObject().HasMember("state_a_only_param") && itr->GetObject()["state_a_only_param"].IsBool()) {
                            // Is this a state param?
                            if (p->type() == ParamType::PATCH_STATE) {
                                // Indicate whether this param is a state A only param
                                static_cast<LayerStateParam *>(p)->set_state_a_only_param(itr->GetObject()["state_a_only_param"].GetBool());
                            }
                        }                                                
                    }
                }
            }
        }

        // Special processing for the FX Macro Select param
        auto param = utils::get_param(utils::ParamRef::FX_MACRO_SELECT);
        if (param) {
            // Populate this param with the FX Macro Params
            uint count = 0;
            for (uint pi : Monique::fx_macro_params()) {
                // Get the DAW param
                auto dp = utils::get_param(MoniqueModule::DAW, pi);
                if (dp) {
                    // Add the param name as a value string
                    param->add_value_string(dp->display_name());
                    count++;
                }
            }

            // Set the number of positions for this param
            param->set_position_param(count);
            param->set_display_enum_list(true);
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _open_and_parse_param_lists_file
//----------------------------------------------------------------------------
bool FileManager::_open_and_parse_param_lists_file()
{
    const char *schema =
#include "../json_schemas/param_lists_schema.json"
;
    constexpr char PARAM_LIST_TYPE_NORMAL[]        = "normal";
    constexpr char PARAM_LIST_TYPE_ADSR_ENVELOPE[] = "adsr_envelope";
    constexpr char PARAM_LIST_TYPE_VCF_CUTOFF[]    = "vcf_cutoff";
    rapidjson::Document json_data;

    // Open the param lists file
    bool ret = ::_open_json_file(MONIQUE_ROOT_FILE_PATH(PARAM_LISTS_FILE), schema, json_data, false);
    if (ret)
    {
        // If the JSON data is empty its an invalid file
        if (!json_data.IsArray())
            return false;

        // Parse the param lists file
        // If the JSON data is not an array don't parse it
        if (json_data.IsArray())
        {
            // Iterate through the params
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
            {
                std::string param_list_name;
                std::string param_list_display_name;
                auto param_list_type = ParamListType::NORMAL;
                auto param_list = std::vector<Param *>();
                auto ct_params_list = std::vector<ContextSpecificParams>();

                // Has the param list name been specified?
                if (itr->GetObject().HasMember("name") && itr->GetObject()["name"].IsString())
                {
                    // Get the param list name
                    param_list_name = itr->GetObject()["name"].GetString();
                    param_list_display_name = param_list_name;
                }
                if (itr->GetObject().HasMember("display_name") && itr->GetObject()["display_name"].IsString())
                {
                    // Get the param list display name
                    param_list_display_name = itr->GetObject()["display_name"].GetString();
                }

                // Has the param list type been specified?
                if (itr->GetObject().HasMember("type") && itr->GetObject()["type"].IsString()) {
                    // Check the display type string, and set it as an enum if valid
                    auto type = itr->GetObject()["type"].GetString();
                    if (std::strcmp(type, PARAM_LIST_TYPE_NORMAL) == 0) {
                        param_list_type = ParamListType::NORMAL;
                    }
                    else if (std::strcmp(type, PARAM_LIST_TYPE_ADSR_ENVELOPE) == 0) {
                        param_list_type = ParamListType::ADSR_ENVELOPE;
                    }
                    else if (std::strcmp(type, PARAM_LIST_TYPE_VCF_CUTOFF) == 0) {
                        param_list_type = ParamListType::VCF_CUTOFF;
                    }                    
                }

                // Get the param list
                if (itr->GetObject().HasMember("params") && itr->GetObject()["params"].IsArray())
                {
                    // Parse each param
                    auto params = itr->GetObject()["params"].GetArray();
                    for (auto& p : params)
                    {
                        // If a param path has been specified
                        if (p.HasMember("param") && p["param"].IsString())
                        {
                            // Get the param path and add the param to the list
                            auto param = utils::get_param(p["param"].GetString());
                            if (param) {
                                // If this param is also a separator
                                //if (p.HasMember("separator") && p["separator"].IsBool())
                                //{
                                //    // Indicate if this param is a separator
                                //    param->separator = p["separator"].GetBool();
                                //}

                                // Add the param to the list                       
                                param_list.push_back(param);
                            }
                        }
                    }
                }

                // Get the context specific params list
                if (itr->GetObject().HasMember("context_specific_params") && itr->GetObject()["context_specific_params"].IsArray())
                {
                    // Parse each context specific param list
                    auto ct_params_obj = itr->GetObject()["context_specific_params"].GetArray();
                    for (auto& ct_param_obj : ct_params_obj)
                    {
                        auto ct_params = ContextSpecificParams();

                        // Get the context param
                        if (ct_param_obj.HasMember("context_param") && ct_param_obj["context_param"].IsString())
                        {
                            // Get the context param
                            auto param = utils::get_param(ct_param_obj["context_param"].GetString());
                            if (param) {
                                ct_params.context_param = param;
                            }
                        }

                        // Get the context param value
                        if (ct_param_obj.HasMember("context_value") && ct_param_obj["context_value"].IsUint())
                        {
                            // Get the context value
                            ct_params.context_value = ct_param_obj["context_value"].GetUint();
                        }                        
                        else if (ct_param_obj.HasMember("context_value") && ct_param_obj["context_value"].IsFloat())
                        {
                            // Get the context value
                            ct_params.context_value = ct_param_obj["context_value"].GetFloat();
                        }

                        // Get the params list
                        if (ct_param_obj.HasMember("params") && ct_param_obj["params"].IsArray())
                        {
                            // Parse each param
                            auto params = ct_param_obj["params"].GetArray();
                            for (auto& p : params)
                            {
                                // If a param path has been specified
                                if (p.HasMember("param") && p["param"].IsString())
                                {
                                    // Get the param path and add the param to the list
                                    auto param = utils::get_param(p["param"].GetString());
                                    if (param) {
                                        // Add the param to the list                                         
                                        ct_params.param_list.push_back(param);
                                    }
                                }                                      
                            }
                        }

                        // Add to the list of context specific params
                        ct_params_list.push_back(ct_params);                                          
                    }
                }                

                // If both the name and param list have been specified
                if ((param_list_name.size() > 0) && ((param_list.size() > 0) || (ct_params_list.size() > 0))) {
                    // Find each global param that uses this list, and set in the param
                    auto params = utils::get_global_params();
                    for (auto p : params) {
                        if (p->param_list_name() == param_list_name) {
                            p->set_param_list_display_name(param_list_display_name);
                            p->set_param_list_type(param_list_type);
                            p->set_param_list(param_list);
                            p->set_context_specific_param_list(ct_params_list);
                        }
                    }

                    // Find each Layer param that uses this list, and set in the param
                    params = utils::get_layer_params();
                    for (auto p : params) {
                        if (p->param_list_name() == param_list_name) {
                            p->set_param_list_display_name(param_list_display_name);
                            p->set_param_list_type(param_list_type);
                            p->set_param_list(param_list);
                            p->set_context_specific_param_list(ct_params_list);
                        }
                    }
                }
            }
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _open_and_parse_system_colours_file
//----------------------------------------------------------------------------
void FileManager::_open_and_parse_system_colours_file()
{
    const char *schema =
#include "../json_schemas/system_colours_schema.json"
;
    rapidjson::Document json_data;
    
    // Open the system colours file (don't create it if it doesn't exist)
    if (::_open_json_file(MONIQUE_ROOT_FILE_PATH(SYSTEM_COLOURS_FILE), schema, json_data, false))
    {
        // If the JSON data is empty its an invalid file
        if (json_data.IsArray())
        {
            std::vector<std::string> system_colour_names;

            // Iterate through the system colours
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
            {
                // Is the blacklist param entry valid?
                if ((itr->GetObject().HasMember("name") && itr->GetObject()["name"].IsString()) &&
                    (itr->GetObject().HasMember("colour") && itr->GetObject()["colour"].IsString()))
                {
                    SystemColour system_colour;

                    // Get the system colour and add it
                    system_colour.name = itr->GetObject()["name"].GetString();
                    system_colour.colour = itr->GetObject()["colour"].GetString();
                    utils::system_config()->add_available_system_colour(system_colour);
                    system_colour_names.push_back(system_colour.name);
                }
            }

            // Is the current system colour any of these available colours?
            if (utils::system_config()->system_colour_is_custom()) {
                // It doesn't exist, so add it as a custom colour
                SystemColour system_colour;
                system_colour.name = "Custom Colour";
                system_colour.colour = utils::system_config()->get_system_colour();
                utils::system_config()->add_available_system_colour(system_colour);
                system_colour_names.push_back(system_colour.name);                
            }

            // Setup the system Colour param
            auto param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::SYSTEM_COLOUR_PARAM_ID);
            if (param) {
                // Add the display strings and select the current system colour
                for (std::string s : system_colour_names) {
                    param->add_value_string(s);
                }
                param->set_position_param(system_colour_names.size());
                param->set_value_from_position(utils::system_config()->get_system_colour_index(), true);
            }            
        }
    }
}

//----------------------------------------------------------------------------
// _open_and_parse_haptic_modes_file
//----------------------------------------------------------------------------
bool FileManager::_open_and_parse_haptic_modes_file()
{
    const char *schema =
#include "../json_schemas/haptic_modes_schema.json"
;
    rapidjson::Document json_data;

    // Open the haptic modes file
    bool ret = ::_open_json_file(MONIQUE_ROOT_FILE_PATH(HAPTIC_MODES_FILE), schema, json_data, false);
    if (ret)
    {
        // Initialise the haptic modes
        utils::init_haptic_modes();

        // If the JSON data is empty its an invalid file
        if (!json_data.IsObject())
            return false;

        // Get the default knob and switch haptic modes - these must always be present (schema checks this)
        auto default_knob_haptic_mode = json_data["default_knob_haptic_mode"].GetString();
        auto default_switch_haptic_mode = json_data["default_switch_haptic_mode"].GetString();

        // Have any haptic modes been specified?
        if (json_data.HasMember("haptic_modes") && json_data["haptic_modes"].IsArray())
        {
            // Parse each haptic mode
            auto haptic_modes = json_data["haptic_modes"].GetArray();
            for (auto& mode : haptic_modes)
            {
                // Is the haptic mode entry valid?
                if ((mode.HasMember("control_type") && mode["control_type"].IsString()) &&
                    (mode.HasMember("name") && mode["name"].IsString()))
                {
                    auto haptic_mode = sfc::HapticMode();
                    const char *control_type = mode["control_type"].GetString();
                    const char *name = mode["name"].GetString();

                    // Get the control type
                    auto type = sfc::control_type_from_string(control_type);
                    haptic_mode.type = type;
                    haptic_mode.name = name;

                    // Is this a knob?
                    if (type == sfc::ControlType::KNOB)
                    {
                        // Has the knob physical start pos been specified?
                        if (mode.HasMember("knob_start_pos") && mode["knob_start_pos"].IsUint())
                        {
                            // Get the knob physical start pos and check it is valid
                            auto start_pos = mode["knob_start_pos"].GetUint();
                            if (start_pos <= 360)
                            {
                                // Set the knob physical start pos in the control mode
                                haptic_mode.knob_start_pos = start_pos;
                                haptic_mode.knob_actual_start_pos = (float)start_pos;
                            }
                        }

                        // Has the knob physical width been specified?
                        if (mode.HasMember("knob_width") && mode["knob_width"].IsUint())
                        {
                            // Get the knob physical width and check it is valid
                            auto width = mode["knob_width"].GetUint();
                            if (width <= 360)
                            {
                                // Set the knob physical width in the control mode
                                haptic_mode.knob_width = width;
                                haptic_mode.knob_actual_width = (float)width;
                            }
                        }

                        // Has the knob actual start pos been specified?
                        if (mode.HasMember("knob_actual_start_pos") && mode["knob_actual_start_pos"].IsFloat())
                        {
                            // Get the knob actual start pos and check it is valid
                            auto start_pos = mode["knob_actual_start_pos"].GetFloat();
                            if (start_pos <= 360.0f)
                            {
                                // Set the knob actual start pos in the control mode
                                haptic_mode.knob_actual_start_pos = start_pos;
                            }
                        }

                        // Has the knob actual width been specified?
                        if (mode.HasMember("knob_actual_width") && mode["knob_actual_width"].IsFloat())
                        {
                            // Get the knob actual width and check it is valid
                            auto width = mode["knob_actual_width"].GetFloat();
                            if (width <= 360.0f)
                            {
                                // Set the knob actual width in the control mode
                                haptic_mode.knob_actual_width = width;
                            }
                        }

                        // Have the number of detents been specified?
                        if (mode.HasMember("knob_num_detents") && mode["knob_num_detents"].IsUint())
                        {
                            // Set the knob number of detents in the control mode
                            haptic_mode.knob_num_detents = mode["knob_num_detents"].GetUint();
                        }

                        // Has the friction been specified?
                        if (mode.HasMember("knob_friction") && mode["knob_friction"].IsUint())
                        {
                            // Set the knob friction in the control mode
                            haptic_mode.knob_friction = mode["knob_friction"].GetUint();
                        }

                        // Has the detent strength been specified?
                        if (mode.HasMember("knob_detent_strength") && mode["knob_detent_strength"].IsUint())
                        {
                            // Set the knob detent strength in the control mode
                            haptic_mode.knob_detent_strength = mode["knob_detent_strength"].GetUint();
                        }

                        // Have the indents been specified?
                        if (mode.HasMember("knob_indents") && mode["knob_indents"].IsArray())
                        {
                            // Parse each indent
                            auto indents_array = mode["knob_indents"].GetArray();
                            for (auto& indent : indents_array)
                            {
                                // Add the indent
                                if ((indent.HasMember("angle") && indent["angle"].IsUint()) && 
                                    (indent.HasMember("hw_active") && indent["hw_active"].IsBool()))
                                {
                                    // Check the angle is valid
                                    uint angle = indent["angle"].GetUint();
                                    if (angle <= 360)
                                    {
                                        // Convert the indent to a hardware value and push the indent
                                        std::pair<bool, uint> knob_indent;
                                        knob_indent.first = indent["hw_active"].GetBool();
                                        knob_indent.second = (angle / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
                                        haptic_mode.knob_indents.push_back(knob_indent);
                                    }
                                }
                            }
                        }
                    }
                    // Is this a switch?
                    else if (type == sfc::ControlType::SWITCH)
                    {
                        // Has the switch mode been specified?
                        if (mode.HasMember("switch_mode") && mode["switch_mode"].IsUint())
                        {
                            // Get the switch mode and set in the control
                            auto switch_mode = mode["switch_mode"].GetUint();
                            if (switch_mode <= uint(sfc::SwitchMode::PUSH_MANUAL_LED))
                            {
                                // Set the switch mode
                                haptic_mode.switch_mode = static_cast<sfc::SwitchMode>(switch_mode);
                            }
                        }
                    }

                    // Add the haptic mode
                    utils::add_haptic_mode(haptic_mode);
                }
            }
        }

        // Set the default knob and switch haptic modes
        if (!utils::set_default_haptic_mode(sfc::ControlType::KNOB, default_knob_haptic_mode))
        {
            // Default mode does not exist
            MSG("The default knob haptic mode " << default_knob_haptic_mode << " does not exist, created a default");
            MONIQUE_LOG_WARNING(module(), "The default knob haptic mode {} does not exist, created a default", default_knob_haptic_mode);          
        }
        if (!utils::set_default_haptic_mode(sfc::ControlType::SWITCH, default_switch_haptic_mode))
        {
            // Default mode does not exist
            MSG("The default switch haptic mode " << default_switch_haptic_mode << " does not exist, created a default");
            MONIQUE_LOG_WARNING(module(), "The default switch haptic mode {} does not exist, created a default", default_switch_haptic_mode);          
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _open_and_parse_global_params_file
//----------------------------------------------------------------------------
bool FileManager::_open_and_parse_global_params_file()
{
    const char *schema =
#include "../json_schemas/global_params_schema.json"
;

    // Open the global params file
    bool ret = ::_open_json_file(MONIQUE_UDATA_FILE_PATH(GLOBAL_PARAMS_FILE), schema, _global_params_json_data, true);
    if (ret) {
        // If the JSON data is not an array it is an invalid file
        if (!_global_params_json_data.IsArray())
            return false;

        // Get the global params and parse them
        bool save_file = false;
        auto params = utils::get_global_params();
        for (Param *p : params) {
            bool param_missed = true;

            // Skip any global params that are also not presets
            if (!p->preset())
                continue;

            // Check if there is an entry for this param
            for (rapidjson::Value::ValueIterator itr = _global_params_json_data.Begin(); itr != _global_params_json_data.End(); ++itr) {
                // Does this entry match the param?
                if (itr->GetObject()["path"].GetString() == p->path()) {
                    // Update the parameter value
                    (p->data_type() == ParamDataType::FLOAT) ?
                        p->set_hr_value(itr->GetObject()["value"].GetFloat()) :
                        p->set_str_value(itr->GetObject()["str_value"].GetString());
                    param_missed = false;
                    break;
                }
            }
            if (param_missed && p->save()) {
                // Param is not specified in the global params file
                // We need to add it to the file
                rapidjson::Value obj;
                obj.SetObject();
                obj.AddMember("path", std::string(p->path()), _global_params_json_data.GetAllocator());
                (p->data_type() == ParamDataType::FLOAT) ?
                    obj.AddMember("value", p->hr_value(), _global_params_json_data.GetAllocator()) :
                    obj.AddMember("str_value", p->str_value(), _global_params_json_data.GetAllocator());
                _global_params_json_data.PushBack(obj, _global_params_json_data.GetAllocator());
                save_file = true;
            }
        }

        // If we need to save the global params file, do so
        if (save_file) {
            _save_global_params_file();
        }

        // Send the global params to the DAW
        _daw_manager->set_global_params(params);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _open_and_check_startup_preset_file
//----------------------------------------------------------------------------
bool FileManager::_open_and_check_startup_preset_file()
{
    // First check if either the current preset a/b files exist
    // Note: The current preset save state is set to the alternate file so that if the current
    // preset file is sucessfully opened, on the next write the alternate file is written
    std::string cp_path = "";
    std::string cp_alt_path = "";
    CurrentPresetSaveState cp_save_state = CurrentPresetSaveState::B;
    CurrentPresetSaveState cp_alt_save_state = CurrentPresetSaveState::A;
    bool cp_open = false;
    bool cp_a_exists = std::filesystem::exists(MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_A_FILE));
    bool cp_b_exists = std::filesystem::exists(MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_B_FILE));

    // If both current preset a/b files exist, choose the one with the latest last write time
    if (cp_a_exists && cp_b_exists) {
        if (std::filesystem::last_write_time(MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_B_FILE)) <
            std::filesystem::last_write_time(MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_A_FILE))) {
            cp_path = MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_A_FILE);
            cp_alt_path = MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_B_FILE);
        }
        else {
            cp_path = MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_B_FILE);
            cp_alt_path = MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_A_FILE);
            cp_save_state = CurrentPresetSaveState::A;
            cp_alt_save_state = CurrentPresetSaveState::B;            
        }
    }
    else if (cp_a_exists) {
        cp_path = MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_A_FILE);
    }
    else if (cp_b_exists) {
        cp_path = MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_B_FILE);
        cp_save_state = CurrentPresetSaveState::A;
    }

    // If a current preset was found
    if (!cp_path.empty()) {
        // Open and check the current preset
        if (_open_and_check_preset_file(cp_path)) {
            // Current preset opened and checked
            MSG("Loaded current Preset: " << cp_path);
            _current_preset_save_state = cp_save_state;
            cp_open = true;
        }
        else {
            // Opening and checking the current preset failed - if the alternate current
            // preset file exists, attempt to open that
            if (!cp_alt_path.empty() && _open_and_check_preset_file(cp_alt_path)) {
                // Current preset opened and checked
                MSG("Loaded current Preset: " << cp_alt_path);
                _current_preset_save_state = cp_alt_save_state;
                cp_open = true;
            }
        }             
    }

    // Was the current preset successfully opened and checked?
    if (!cp_open) {
        // No - final attempt is to open the actual preset file, and save it as the
        // current preset file
        if (!_open_and_check_preset_file(utils::system_config()->preset_id().path())) {
            // This is a critical error - note the function call above logs any errors
            return false;
        }

        // Note: Save twice so that both current preset files are the same and represet the
        // opened preset
        _current_preset_save_state = CurrentPresetSaveState::B;
        _save_current_preset_file();
        _save_current_preset_file();
        MSG("Loaded Preset: " << utils::system_config()->preset_id().path());               
    }
    return true;  
}

//----------------------------------------------------------------------------
// _open_and_check_preset_file
//----------------------------------------------------------------------------
bool FileManager::_open_and_check_preset_file(std::string file_path)
{
    // Open the preset file
    if (::_open_preset_file(file_path, _preset_json_data)) {
        // Check the preset
        return _check_preset(_preset_json_data, _preset_doc);
    }
    return false;
}

//----------------------------------------------------------------------------
// _check_preset
//----------------------------------------------------------------------------
bool FileManager::_check_preset(rapidjson::Document& json_data, PresetDoc& preset_doc)
{
    // Get the preset common params
    if (json_data.HasMember("params") && json_data["params"].IsArray()) {
        // Save the iterator to the preset common JSON data
        preset_doc.preset_common_params_json_data = &json_data["params"];
    }           

    // Iterate through the layers
    for (rapidjson::Value::ValueIterator itr = json_data["layers"].Begin(); itr != json_data["layers"].End(); ++itr) {
        // Has the layer ID been specified?
        if (itr->GetObject().HasMember("layer_id") && itr->GetObject()["layer_id"].IsString()) {
            // Get the layer ID and make sure it is valid
            auto id = itr->GetObject()["layer_id"].GetString();
            if (std::strcmp(id,"d0") == 0) {
                // Digital 0 Layer
                preset_doc.d0_layer_json_data = itr;
                if (itr->GetObject().HasMember("params") && itr->GetObject()["params"].IsArray()) {
                    // Save the iterator to the params JSON data
                    preset_doc.d0_layer_params_json_data = &itr->GetObject()["params"];
                }
                if (itr->GetObject().HasMember("patch") && itr->GetObject()["patch"].IsObject()) {
                    // Save the iterator to the patch JSON data
                    preset_doc.d0_patch_json_data = &itr->GetObject()["patch"];

                    // Set the Layer patch name
                    std::string patch_name = "UNNAMED";
                    if (preset_doc.d0_patch_json_data->HasMember("name") && (*preset_doc.d0_patch_json_data)["name"].IsString()) {
                        patch_name = (*preset_doc.d0_patch_json_data)["name"].GetString();
                    }
                    utils::get_layer_info(LayerId::D0).set_patch_name(patch_name);
                }              
            }
            else if (std::strcmp(id,"d1") == 0) {
                // Digital 1 Layer
                preset_doc.d1_layer_json_data = itr;
                if (itr->GetObject().HasMember("params") && itr->GetObject()["params"].IsArray()) {
                    // Save the iterator to the params JSON data
                    preset_doc.d1_layer_params_json_data = &itr->GetObject()["params"];
                }
                if (itr->GetObject().HasMember("patch") && itr->GetObject()["patch"].IsObject()) {
                    // Save the iterator to the patch JSON data
                    preset_doc.d1_patch_json_data = &itr->GetObject()["patch"];

                    // Set the Layer patch name
                    std::string patch_name = "UNNAMED";
                    if (preset_doc.d1_patch_json_data->HasMember("name") && (*preset_doc.d1_patch_json_data)["name"].IsString()) {
                        patch_name = (*preset_doc.d1_patch_json_data)["name"].GetString();
                    }
                    utils::get_layer_info(LayerId::D1).set_patch_name(patch_name);                                       
                }
            }
            else {
                // This is a critical error
                MSG("A layer ID specified in the preset file is invalid");
                MONIQUE_LOG_CRITICAL(module(), "A layer ID specified in the preset file is invalid");                 
                return false;
            }
        }
        else {
            // This is a critical error
            MSG("The layer ID has not been specified for a layer");
            MONIQUE_LOG_CRITICAL(module(), "The layer ID has not been specified for a layer");                 
            return false;            
        }        

    }

    // Common data and both layers found ok?
    if (!preset_doc.preset_common_params_json_data || 
        !preset_doc.d0_layer_params_json_data || !preset_doc.d0_patch_json_data ||
        !preset_doc.d1_layer_params_json_data || !preset_doc.d1_patch_json_data) {
        // This is a critical error
        MSG("Not all required data is specified in the preset file");
        MONIQUE_LOG_CRITICAL(module(), "Not all required data is specified in the preset file");               
        return false;
    }

    // Preset OK
    return true;
}

//----------------------------------------------------------------------------
// _parse_config
//----------------------------------------------------------------------------
void FileManager::_parse_config()
{
    bool save_config_file = false;
    PresetId preset_id;
    PresetId prev_preset_id;

    // Is the preset ID present?
    if (_config_json_data.HasMember("preset_id") && _config_json_data["preset_id"].IsString()) {
        // Check the preset ID, if not valid set it to the fallback ID
        preset_id.set_id(_config_json_data["preset_id"].GetString());
        if (!preset_id.is_valid(true)) {
            preset_id.set_fallback_id();
        }
    }
    else {
        // Set the fallback preset ID
        preset_id.set_fallback_id();

        // Create the preset ID entry
        _config_json_data.AddMember("preset_id", preset_id.id(), _config_json_data.GetAllocator());
        save_config_file = true;
    }
    utils::system_config()->set_preset_id(preset_id);

    // Is the previous Preset ID present?
    if (_config_json_data.HasMember("prev_preset_id") && _config_json_data["prev_preset_id"].IsString()) {
        // Get the previous preset ID, if not valid just ignore it
        prev_preset_id.set_id(_config_json_data["prev_preset_id"].GetString());
    }
    else {
        // Create an empty entry - there is no previous preset ID
        _config_json_data.AddMember("prev_preset_id", prev_preset_id.id(), _config_json_data.GetAllocator());
        save_config_file = true;        
    }
    utils::system_config()->set_prev_preset_id(prev_preset_id);

    // Has the modulation source number been specified?
    if (_config_json_data.HasMember("mod_src_num") && _config_json_data["mod_src_num"].IsUint())
    {
        // Set the modulation source number
        utils::system_config()->set_mod_src_num(_config_json_data["mod_src_num"].GetUint());
    }
    else
    {
        // Create the modulation source number entry
        _config_json_data.AddMember("mod_src_num", DEFAULT_MOD_SRC_NUM, _config_json_data.GetAllocator());
        save_config_file = true;
    }

    // Has demo mode been specified?
    if (_config_json_data.HasMember("demo_mode") && _config_json_data["demo_mode"].IsBool())
    {
        // Set the demo mode
        utils::system_config()->set_demo_mode(_config_json_data["demo_mode"].GetBool());
    }
    else
    {
        // Create the demo mode
        _config_json_data.AddMember("demo_mode", false, _config_json_data.GetAllocator());
        utils::system_config()->set_demo_mode(false);
        save_config_file = true;
    }

    // Has the demo mode timeout been specified?
    if (_config_json_data.HasMember("demo_mode_timeout") && _config_json_data["demo_mode_timeout"].IsUint())
    {
        // Set the demo mode timeout threshold
        utils::system_config()->set_demo_mode_timeout(_config_json_data["demo_mode_timeout"].GetUint());
    }
    else
    {
        // Create the demo mode timeout threshold
        _config_json_data.AddMember("demo_mode_timeout", DEFAULT_DEMO_MODE_TIMEOUT, _config_json_data.GetAllocator());
        utils::system_config()->set_demo_mode_timeout(DEFAULT_DEMO_MODE_TIMEOUT);
        save_config_file = true;
    }

    // Has the system colour been specified?
    std::string system_colour;
    if (_config_json_data.HasMember("system_colour") && _config_json_data["system_colour"].IsString())
    {
        // Get the specified system colour
        system_colour = _config_json_data["system_colour"].GetString();
    }
    else {
        // Create the system colour
        system_colour = DEFAULT_SYSTEM_COLOUR;
        _config_json_data.AddMember("system_colour", DEFAULT_SYSTEM_COLOUR, _config_json_data.GetAllocator());
        save_config_file = true;
    }

    // Try and convert the system colour to an integer and set the system colour
    try {
        [[maybe_unused]] auto colour = std::stoi(system_colour, nullptr, 16);
    }
    catch (...) {
        system_colour = DEFAULT_SYSTEM_COLOUR;
    }
    utils::system_config()->set_system_colour(system_colour);

    // Does the config file need saving?
    if (save_config_file) {
        _save_config_file();
    }
}

//----------------------------------------------------------------------------
// _parse_param_map
//----------------------------------------------------------------------------
void FileManager::_parse_param_map()
{
    std::string last_multifn_switch_state = "";
    uint multifn_switch_index = 0;
    
    // Iterate through the param map
    for (rapidjson::Value::ValueIterator itr = _param_map_json_data.Begin(); itr != _param_map_json_data.End(); ++itr) {
        // Is the param mapping entry valid?
        if ((itr->GetObject().HasMember("param_1") && itr->GetObject()["param_1"].IsString()) &&
            (itr->GetObject().HasMember("param_2") && itr->GetObject()["param_2"].IsString())) {
            std::string param_1_path = itr->GetObject()["param_1"].GetString();
            std::string param_2_path = itr->GetObject()["param_2"].GetString();
            auto param_1 = utils::get_param(param_1_path);
            auto param_2 = utils::get_param(param_2_path);      

            // If the param 1 does not exist, check if it is a MIDI CC or pitchbend param
            if (!param_1) {
                // Is this a MIDI CC, Pitch Bend, or Chanpress param?
                if (MidiDeviceManager::IsMidiCcParamPath(param_1_path) ||
                    MidiDeviceManager::IsMidiPitchBendParamPath(param_1_path) ||
                    MidiDeviceManager::IsMidiChanpressParamPath(param_1_path)) {
                    // Create the source param (dummy param as it doesn't do anything, just allows
                    // the mapping)
                    auto param = DummyParam::CreateParam(MoniqueModule::MIDI_DEVICE, param_1_path);
                    utils::register_param(std::move(param));
                    param_1 = utils::get_param(param_1_path);                    
                }                
            }

            // If the param 2 does not exist, check to see if the mapping is to MIDI or
            // a state change
            if (!param_2) {
                // Is this a MIDI CC, Pitch Bend, or Chanpress param?
                if (MidiDeviceManager::IsMidiCcParamPath(param_2_path) ||
                    MidiDeviceManager::IsMidiPitchBendParamPath(param_2_path) ||
                    MidiDeviceManager::IsMidiChanpressParamPath(param_2_path)) {
                    // Create the source param (dummy param as it doesn't do anything, just allows
                    // the mapping)
                    auto param = DummyParam::CreateParam(MoniqueModule::MIDI_DEVICE, param_2_path);                 
                    utils::register_param(std::move(param));
                    param_2 = utils::get_param(param_2_path);                    
                }
            }

            // Do both the source and destination params exist AND they are float params?
            // Don't bother processing any further if they aren't
            if (param_1 && param_2 && (param_1->data_type() == ParamDataType::FLOAT) && (param_2->data_type() == ParamDataType::FLOAT)) {
                // Assume this mapping is for the default state
                std::string state = utils::default_ui_state();

                // Does this mapping have a state specified?
                if ((itr->GetObject().HasMember("ui_state") && itr->GetObject()["ui_state"].IsString())) {    
                    // Get the state
                    state = itr->GetObject()["ui_state"].GetString();
                }

                // Add the control state for this mapping if a Surface Control param
                // Is param 1 a Surface Control param?
                if (param_1->module() == MoniqueModule::SFC_CONTROL) {
                    // Add the control state                  
                    static_cast<SfcControlParam *>(param_1)->add_control_state(state);
                    static_cast<SfcControlParam *>(param_1)->set_control_state(state);
                }
                // Is param 2 a Surface Control param?
                if (param_2->module() == MoniqueModule::SFC_CONTROL) {
                    // Add the control state                  
                    static_cast<SfcControlParam *>(param_2)->add_control_state(state);
                    static_cast<SfcControlParam *>(param_2)->set_control_state(state);
                }

                // Does this param have a group?
                if ((itr->GetObject().HasMember("group") && itr->GetObject()["group"].IsString())) {
                    // Get the group name
                    auto group = itr->GetObject()["group"].GetString();

                    // Is param 1 a Surface Control param?
                    if (param_1->module() == MoniqueModule::SFC_CONTROL) {
                        // Set the group name                   
                        static_cast<SfcControlParam *>(param_1)->set_group_name(group);
                    }
                    // Is param 2 a Surface Control param?
                    if (param_2->module() == MoniqueModule::SFC_CONTROL) {
                        // Set the group name              
                        static_cast<SfcControlParam *>(param_2)->set_group_name(group);
                    }                    
                }

                // Does this param have a group param
                if ((itr->GetObject().HasMember("group_param") && itr->GetObject()["group_param"].IsString())) {
                    // Get the group param
                    auto group_param_path = itr->GetObject()["group_param"].GetString();
                    auto group_param = utils::get_param(group_param_path);
                    if (group_param) {
                        // Is param 1 a Surface Control param?
                        if (param_1->module() == MoniqueModule::SFC_CONTROL) {
                            // Set the group param                   
                            static_cast<SfcControlParam *>(param_1)->set_group_param(group_param);
                        }
                        // Is param 2 a Surface Control param?
                        if (param_2->module() == MoniqueModule::SFC_CONTROL) {
                            // Set the group param              
                            static_cast<SfcControlParam *>(param_2)->set_group_param(group_param);
                        }
                    }  
                }                

                // Is this the default group control?
                if ((itr->GetObject().HasMember("group_default") && itr->GetObject()["group_default"].IsBool())) {
                    // Get the setting
                    bool group_default = itr->GetObject()["group_default"].GetBool();

                    // If the default, set the Surface control param to be 1.0 (ON)
                    if (group_default) {
                        // Is param 1 a Surface Control param?
                        if (param_1->module() == MoniqueModule::SFC_CONTROL) {
                            // Set the param to ON                   
                            param_1->set_value(true);
                        }
                        // Is param 2 a Surface Control param?
                        if (param_2->module() == MoniqueModule::SFC_CONTROL) {
                            // Set the param to ON                   
                            param_2->set_value(true);
                        }
                    }
                } 

                // Does this mapping have a haptic mode specified?
                // The mode is applicable if the source or destination is a hardware param
                if ((itr->GetObject().HasMember("haptic_mode") && itr->GetObject()["haptic_mode"].IsString()))
                {
                    // Get the mode
                    auto mode = itr->GetObject()["haptic_mode"].GetString();

                    // Is param 1 a Surface Control param?
                    if (param_1->module() == MoniqueModule::SFC_CONTROL) {
                        // Set the mode                    
                        static_cast<SfcControlParam *>(param_1)->set_haptic_mode(mode);
                    }
                    // Is param 2 a Surface Control param?
                    if (param_2->module() == MoniqueModule::SFC_CONTROL) {
                        // Set the mode                           
                        static_cast<SfcControlParam *>(param_2)->set_haptic_mode(mode);
                    }                    
                }

                // Does this mapping have the morphable param specified?
                // The mode is applicable if the source or destination is a hardware param
                if ((itr->GetObject().HasMember("morphable") && itr->GetObject()["morphable"].IsBool())) {
                    // Get the setting
                    auto morphable = itr->GetObject()["morphable"].GetBool();

                    // Is param 1 a Surface Control param?
                    if (param_1->module() == MoniqueModule::SFC_CONTROL) {
                        // Set the morphable state              
                        static_cast<SfcControlParam *>(param_1)->set_morphable(morphable);
                    }
                    // Is param 2 a Surface Control param?
                    if (param_2->module() == MoniqueModule::SFC_CONTROL) {
                        // Set the morphable state                                               
                        static_cast<SfcControlParam *>(param_2)->set_morphable(morphable);
                    }                    
                }

                // Is this param mapping a linked params map?
                // This means that when one param changes, the other param changes by the delta in the first param value
                if ((itr->GetObject().HasMember("type") && itr->GetObject()["type"].IsString())) {
                    // Is this a linked params mapping type?
                    std::string type = itr->GetObject()["type"].GetString();
                    if (type == "linked_params") {
                        // We only allow DAW params to be linked
                        if ((param_1->module() == MoniqueModule::DAW) && (param_2->module() == MoniqueModule::DAW)) {
                            // Set the linked param - we also disable the linked param functionality by default by
                            // disabling it in param 1
                            // This assumes it is enabled via param 1   
                            param_1->set_as_linked_param();
                            param_1->enable_linked_param(false);
                            param_2->set_as_linked_param();
                            param_2->enable_linked_param(false);
                        }
                    }
                }

                // If either param is a is a multi-function switch system function,
                // indicate this in the other (switch) param
                if (param_1->path() == SystemFuncParam::ParamPath(SystemFuncType::MULTIFN_SWITCH)) {
                    // If this is a new multi-function switch state, reset the switch index
                    if (state != last_multifn_switch_state) {
                        multifn_switch_index = 0;
                    }
                    static_cast<SwitchParam *>(param_2)->set_as_multifn_switch(multifn_switch_index);
                    multifn_switch_index++;
                    last_multifn_switch_state = state;
                }
                else if (param_2->path() == SystemFuncParam::ParamPath(SystemFuncType::MULTIFN_SWITCH)) {
                    // If this is a new multi-function switch state, reset the switch index
                    if (state != last_multifn_switch_state) {
                        multifn_switch_index = 0;
                    }                    
                    static_cast<SwitchParam *>(param_1)->set_as_multifn_switch(multifn_switch_index);
                    multifn_switch_index++;
                    last_multifn_switch_state = state;              
                }

                // Subscribe the mapping
                // Note: If either param does not exist, this function does nothing
                param_1->add_mapped_param(param_2);
                param_2->add_mapped_param(param_1);

                // If any param is a Surface Control param, make sure the control
                // state is set back to default
                // Is param 1 a Surface Control param?
                if (param_1->module() == MoniqueModule::SFC_CONTROL) {
                    // Set the default control state                  
                    static_cast<SfcControlParam *>(param_1)->set_default_control_state();
                }
                // Is param 2 a Surface Control param?
                if (param_2->module() == MoniqueModule::SFC_CONTROL) {
                    // Set the default control state                  
                    static_cast<SfcControlParam *>(param_2)->set_default_control_state();
                }
            }
        }
    }

    // Additional processing
    // Get the mapped (knob) param to the data knob system function
    // Note: Assumes there is only one control mapped, and it is a knob param
    auto param = utils::get_sys_func_param(SystemFuncType::DATA_KNOB);
    if (param) {
        // Get the mapped params and check there is only one
        auto mapped_params = param->mapped_params(nullptr);
        if (mapped_params.size() == 1) {
            // Get the mapped control, if it is a Surface Control knob save it as
            // the data knob param
            auto mp = mapped_params.front();
            if ((mp->module() == MoniqueModule::SFC_CONTROL) && (static_cast<SfcControlParam *>(mp)->control_type() == sfc::ControlType::KNOB)) {
                utils::set_data_knob_param(static_cast<KnobParam *>(mp));
            }
        }
    }
}

//----------------------------------------------------------------------------
// _parse_preset
//----------------------------------------------------------------------------
void FileManager::_parse_preset()
{
    // Get the preset params for parsing
    auto params = utils::get_preset_params();

    // Reset the layers
    _reset_layers();

    // Parse the preset common params
    _parse_preset_common_params(params);

    // Send the preset common params to the DAW
    _daw_manager->set_preset_common_params(params);   

    // Setup each layer
    _setup_layer(LayerId::D1, params);
    _setup_layer(LayerId::D0, params);

    // Calculate and set the Layer voices - we need to do this in case the Layer data
    // has voice allocation settings that are incorrect or invalid
    _calc_and_set_layer_voices();

    // Handle any special case params
    _handle_preset_special_case_params(SystemFuncType::LOAD_PRESET);
}

//----------------------------------------------------------------------------
// _parse_preset_common_params
//----------------------------------------------------------------------------
void FileManager::_parse_preset_common_params(std::vector<Param *> &params)
{
    // Parse the preset common params
    for (Param *p : params) {
        // Is this a preset common param?
        if (p->type() == ParamType::PRESET_COMMON) {
            bool param_missed = true;

            // Check if there is a patch for this param
            for (rapidjson::Value::ValueIterator itr = _preset_doc.preset_common_params_json_data->Begin(); itr != _preset_doc.preset_common_params_json_data->End(); ++itr) {
                // Does this entry match the param?
                if (itr->GetObject()["path"].GetString() == p->path()) {
                    // Update the parameter value
                    (p->data_type() == ParamDataType::FLOAT) ?
                        p->set_hr_value(itr->GetObject()["value"].GetFloat()) :
                        p->set_str_value(itr->GetObject()["str_value"].GetString());
                    param_missed = false;
                    break;
                }
            }
            if (param_missed && p->save()) {
                // If this is a mod matrix param, just set the value to 0.0
                // Do not add it to the preset at this stage
                if (p->mod_matrix_param()) {
                    p->set_hr_value(0.0);
                }              
                // If this is a Sequencer chunk param, just reset the param
                // Do not add it to the preset at this stage
                else if (p->seq_chunk_param()) {
                    p->reset_seq_chunk_param();
                }
                else {
                    // Param is not specified in the preset file
                    // Firstly set the default value from the BASIC preset
                    _set_param_from_basic_preset(p);

                    // Add the missing param to the patch
                    rapidjson::Value obj;
                    obj.SetObject();
                    obj.AddMember("path", std::string(p->path()), _preset_json_data.GetAllocator());
                    (p->data_type() == ParamDataType::FLOAT) ?
                        obj.AddMember("value", p->hr_value(), _preset_json_data.GetAllocator()) :
                        obj.AddMember("str_value", p->str_value(), _preset_json_data.GetAllocator());
                    _preset_doc.preset_common_params_json_data->PushBack(obj, _preset_json_data.GetAllocator());
                }
            }

            // Process the mapped params
            _process_layer_mapped_params(p, nullptr);            
        }
    }
}

//----------------------------------------------------------------------------
// _parse_preset_layer
//----------------------------------------------------------------------------
void FileManager::_parse_preset_layer(std::vector<Param *> &params, bool inc_layer_params)
{
    // Parse the Layer params (if specified)
    if (inc_layer_params) {
        _parse_layer_params(params);
    }

    // Parse the Layer patch params
    _parse_layer_patch_params(params);

    // Send the preset layer params to the DAW
    _daw_manager->set_layer_params(params);
}

//----------------------------------------------------------------------------
// _parse_layer_params
//----------------------------------------------------------------------------
void FileManager::_parse_layer_params(std::vector<Param *> &params)
{
    // Parse the params
    for (Param *p : params) {
        // Is this a layer patch param?
        if (p->type() == ParamType::LAYER) {
            bool param_missed = true;

            // Check if there is a patch for this param
            auto json_data = &_get_layer_params_json_data(utils::get_current_layer_info().layer_id(), _preset_doc);
            for (rapidjson::Value::ValueIterator itr = json_data->Begin(); itr != json_data->End(); ++itr) {
                // Does this entry match the param?
                if (itr->GetObject()["path"].GetString() == p->path()) {
                    // Update the parameter value
                    p->set_hr_value(itr->GetObject()["value"].GetFloat());
                    param_missed = false;
                    break;
                }
            }
            if (param_missed && p->save()) {
                // Param is not specified in the patch file
                // Firstly set the default value from the BASIC preset
                _set_param_from_basic_preset(p);

                // Add the missing param to the patch
                rapidjson::Value obj;
                obj.SetObject();
                obj.AddMember("path", std::string(p->path()), _preset_json_data.GetAllocator());
                (p->data_type() == ParamDataType::FLOAT) ?
                    obj.AddMember("value", p->hr_value(), _preset_json_data.GetAllocator()) :
                    obj.AddMember("str_value", p->str_value(), _preset_json_data.GetAllocator());
                json_data->PushBack(obj, _preset_json_data.GetAllocator());
            }
        }
    }
}

//----------------------------------------------------------------------------
// _parse_layer_patch_params
//----------------------------------------------------------------------------
void FileManager::_parse_layer_patch_params(std::vector<Param *> &params)
{
    // Process the Layer patch common params
    _parse_patch_common_params(params);

    // Process the patch State B params
    _parse_patch_state_params(params, LayerState::STATE_B);

    // Process the Layer patch State A params - the default when loading a patch
    _parse_patch_state_params(params, LayerState::STATE_A);
}

//----------------------------------------------------------------------------
// _parse_patch_common_params
//----------------------------------------------------------------------------
void FileManager::_parse_patch_common_params(std::vector<Param *> &params)
{
    // Parse the patch params
    for (Param *p : params) {
        // Is this a common patch param?
        if (p->type() == ParamType::PATCH_COMMON) {
            bool param_missed = true;

            // Check if there is a patch for this param
            rapidjson::Value& json_data = _get_patch_common_json_data(utils::get_current_layer_info().layer_id(), _preset_doc);
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr) {
                // Does this entry match the param?
                if (itr->GetObject()["path"].GetString() == p->path()) {
                    // Update the parameter value
                    (p->data_type() == ParamDataType::FLOAT) ?
                        p->set_hr_value(itr->GetObject()["value"].GetFloat()) :
                        p->set_str_value(itr->GetObject()["str_value"].GetString());
                    param_missed = false;
                    break;
                }
            }
            if (param_missed && p->save()) {
                // Param is not specified in the patch file
                // Firstly set the default value from the BASIC preset
                _set_param_from_basic_preset(p);

                // Add the missing param to the patch
                rapidjson::Value obj;
                obj.SetObject();
                obj.AddMember("path", std::string(p->path()), _preset_json_data.GetAllocator());
                (p->data_type() == ParamDataType::FLOAT) ?
                    obj.AddMember("value", p->hr_value(), _preset_json_data.GetAllocator()) :
                    obj.AddMember("str_value", p->str_value(), _preset_json_data.GetAllocator());
                json_data.PushBack(obj, _preset_json_data.GetAllocator());
            }

            // Process the mapped params
            _process_layer_mapped_params(p, nullptr);
         
        }
    }
}

//----------------------------------------------------------------------------
// _parse_patch_state_params
//----------------------------------------------------------------------------
void FileManager::_parse_patch_state_params(std::vector<Param *> &params, LayerState state)
{
    // Set the Layer patch state
    utils::get_current_layer_info().set_layer_state(state);

    // Parse the patch params
    for (Param *p : params) {
        // Is this a state patch param?
        if (p->type() == ParamType::PATCH_STATE) {
            bool param_missed = true;

            // Check if there is a patch for this param
            rapidjson::Value& json_data = _get_patch_state_json_data(utils::get_current_layer_info().layer_id(), state, p, _preset_doc);
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr) {
                // Does this entry match the param?
                if (itr->GetObject()["path"].GetString() == p->path()) {
                    // Update the parameter value
                    (p->data_type() == ParamDataType::FLOAT) ?
                        p->set_hr_value(itr->GetObject()["value"].GetFloat()) :
                        p->set_str_value(itr->GetObject()["str_value"].GetString());
                    param_missed = false;

                    // Special case handling for wavetables
                    if ((p->module() == MoniqueModule::SYSTEM) && (p->param_id() == SystemParamId::WT_NAME_PARAM_ID) &&
                        (p->str_value().size() > 0)) {
                        struct dirent **dirent = nullptr;
                        int num_files;
                        int file_pos = 0;
                        uint wt_file_count = 0;
                        
                        // Scan the MONIQUE wavetable folder
                        num_files = ::scandir(common::MONIQUE_WT_DIR, &dirent, 0, ::versionsort);
                        if (num_files > 0) {
                            // Process each file in the folder
                            for (uint i=0; i<(uint)num_files; i++) {
                                // If we've not found the max number of wavetables yet and this a normal file
                                if ((wt_file_count < common::MAX_NUM_WAVETABLE_FILES) && (dirent[i]->d_type == DT_REG)) {
                                    // If it has a WAV file extension
                                    auto name = std::string(dirent[i]->d_name);
                                    if (name.substr((name.size() - (sizeof(".wav") - 1))) == ".wav") {
                                        // Is this the specified wavetable?
                                        if (name.substr(0, (name.size() - (sizeof(".wav") - 1))) == p->str_value()) {
                                            file_pos = wt_file_count;
                                        }
                                        wt_file_count++;
                                    }
                                }
                                ::free(dirent[i]);
                            }

                            // Get the WT Select param
                            auto param = utils::get_param(utils::ParamRef::WT_SELECT);
                            if (param) {  
                                // Set the position value
                                param->set_position_param(wt_file_count);
                                param->set_value_from_position(file_pos);
                            }                                
                        }
                        if (dirent) {
                            ::free(dirent);
                        }
                    }
                    break;
                }
            }
            if (param_missed && p->save()) {
                // If this is a mod matrix param, just set the value to 0.0
                // Do not add it to the patch at this stage
                if (p->mod_matrix_param()) {
                    p->set_hr_value(0.0);
                }
                else {
                    // Param is not specified in the patch file
                    // Firstly set the default value from the BASIC preset
                    _set_param_from_basic_preset(p);

                    // Add the missing param to the patch
                    rapidjson::Value obj;
                    obj.SetObject();
                    obj.AddMember("path", std::string(p->path()), _preset_json_data.GetAllocator());
                    (p->data_type() == ParamDataType::FLOAT) ?
                        obj.AddMember("value", p->hr_value(), _preset_json_data.GetAllocator()) :
                        obj.AddMember("str_value", p->str_value(), _preset_json_data.GetAllocator());
                    json_data.PushBack(obj, _preset_json_data.GetAllocator());
                }
            }

            // Process the mapped params
            _process_layer_mapped_params(p, nullptr);
        }
    }
}

//----------------------------------------------------------------------------
// _process_layer_mapped_params
//----------------------------------------------------------------------------
void FileManager::_process_layer_mapped_params(const Param *param, const Param *skip_param)
{
    // Only process float params
    if (param->data_type() == ParamDataType::FLOAT) {
        // Get the mapped params
        for (Param *mp : param->mapped_params(skip_param))
        {
            // Because this function is recursive, we need to skip the param that
            // caused any recursion, so it is not processed twice
            // Also skip if the params are linked to one-another, linked params are OFF by default
            if ((skip_param && (mp == skip_param)) ||
                (param->is_linked_param() && mp->is_linked_param())) {
                continue;
            }

            // Set the mapped param?
            // Don't process system function or MIDI params
            if ((mp->type() != ParamType::SYSTEM_FUNC) || (mp->module() != MoniqueModule::MIDI_DEVICE)) {
                // Set the source param value from this param
                mp->set_value_from_param(*param);
            }

            // We need to recurse each mapped param and process it
            // Note: We don't recurse system function params as they are a system action to be performed
            if (mp->type() != ParamType::SYSTEM_FUNC)
                _process_layer_mapped_params(mp, param);        
        }
    }   
}

//----------------------------------------------------------------------------
// _save_config_file
//----------------------------------------------------------------------------
void FileManager::_save_config_file()
{
    DEBUG_BASEMGR_MSG("_save_config_file");

    // Save the config file
    _save_json_file(MONIQUE_UDATA_FILE_PATH(CONFIG_FILE), _config_json_data);
}

//----------------------------------------------------------------------------
// _save_global_params_file
//----------------------------------------------------------------------------
void FileManager::_save_global_params_file()
{
    DEBUG_BASEMGR_MSG("_save_global_params_file");

    // Save the global params file
    _save_json_file(MONIQUE_UDATA_FILE_PATH(GLOBAL_PARAMS_FILE), _global_params_json_data);
}

//----------------------------------------------------------------------------
// _save_current_preset_file
//----------------------------------------------------------------------------
void FileManager::_save_current_preset_file()
{
    // Save the current preset file
    _save_preset_file(_current_preset_save_state == CurrentPresetSaveState::A ?
        MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_A_FILE) :
        MONIQUE_UDATA_FILE_PATH(CURRENT_PRESET_B_FILE));

    // Toggle the current preset save state so that it writes to the alternate 
    // file each save
    _current_preset_save_state == CurrentPresetSaveState::A ?
        _current_preset_save_state = CurrentPresetSaveState::B :
        _current_preset_save_state = CurrentPresetSaveState::A;
}

//----------------------------------------------------------------------------
// _save_preset_file
//----------------------------------------------------------------------------
void FileManager::_save_preset_file()
{
    DEBUG_BASEMGR_MSG("_save_preset_file");
    uint revision = 1;

    // When we save the actual preset file, make sure the version and
    // revision are updated

    // Does the version property exist?
    if (!_preset_json_data.HasMember("version"))
    {
        // No - add the version property
        _preset_json_data.AddMember("version", PRESET_VERSION, _preset_json_data.GetAllocator());
    }
    else
    {
        // Set the current version
        if (_preset_json_data.GetObject()["version"].IsString()) {
            _preset_json_data.GetObject()["version"].SetString(PRESET_VERSION);
        }
    }    

    // Does the revision property exist?
    if (!_preset_json_data.HasMember("revision"))
    {
        // No - add the revision property
        _preset_json_data.AddMember("revision", revision, _preset_json_data.GetAllocator());
    }
    else
    {
        // Get the current revision and increment it
        if (_preset_json_data.GetObject()["revision"].IsUint()) {
            revision = _preset_json_data.GetObject()["revision"].GetUint();
            _preset_json_data.GetObject()["revision"].SetUint(revision + 1);
        }
    }

    // Save the specified preset file
    _save_preset_file(utils::system_config()->preset_id().path());
}

//----------------------------------------------------------------------------
// _save_preset_file
//----------------------------------------------------------------------------
void FileManager::_save_preset_file(std::string file_path)
{
    DEBUG_BASEMGR_MSG("_save_preset_file: " << file_path);

    // Save the specified preset file
    _save_json_file(file_path, _preset_json_data);
}

//----------------------------------------------------------------------------
// _save_json_file
//----------------------------------------------------------------------------
void FileManager::_save_json_file(std::string file_path, const rapidjson::Document &json_data)
{
    char write_buffer[131072];

    // Open the file for writing
    FILE *fp = ::fopen(file_path.c_str(), "w");
    if (fp == nullptr)
    {
        MSG("An error occurred (" << errno << ") writing the file: " << file_path);
        MONIQUE_LOG_ERROR(module(), "An error occurred ({}) writing the file: {}", errno, file_path);
        return;
    }

    // Write the JSON data to the file
    // Note: If the data is larger than the write buffer then the Accept
    // function writes it in chunks
    rapidjson::FileWriteStream os(fp, write_buffer, sizeof(write_buffer));
    rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
    (void)json_data.Accept(writer);
    fclose(fp);
    sync();
}

//----------------------------------------------------------------------------
// _start_save_config_file_timer
//----------------------------------------------------------------------------
void FileManager::_start_save_config_file_timer()
{
    // Stop and start the save config file timer
    _save_config_file_timer->stop();
    _save_config_file_timer->start(SAVE_CONFIG_FILE_IDLE_INTERVAL_US, std::bind(&FileManager::_save_config_file, this));
}

//----------------------------------------------------------------------------
// _start_save_global_params_file_timer
//----------------------------------------------------------------------------
void FileManager::_start_save_global_params_file_timer()
{
    // Stop and start the save globals file timer
    _save_global_params_file_timer->stop();
    _save_global_params_file_timer->start(SAVE_GLOBAL_PARAMS_FILE_IDLE_INTERVAL_MS, std::bind(&FileManager::_save_global_params_file, this));
}

//----------------------------------------------------------------------------
// _start_save_preset_file_timer
//----------------------------------------------------------------------------
void FileManager::_start_save_preset_file_timer()
{
    // Stop and start the save preset file timer
    _save_preset_file_timer->stop();
    _save_preset_file_timer->start(SAVE_PRESET_FILE_IDLE_INTERVAL_MS, std::bind(&FileManager::_save_current_preset_file, this));
}

//----------------------------------------------------------------------------
// _find_global_param
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_find_global_param(std::string path)
{
    // Search the global params json data
    for (rapidjson::Value::ValueIterator itr = _global_params_json_data.Begin(); itr != _global_params_json_data.End(); ++itr) {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == path) {
            return itr;
        }
    }                                 
    return nullptr;
}

//----------------------------------------------------------------------------
// _find_preset_param
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_find_preset_param(const Param *param, PresetDoc& preset_doc)
{
    // Search the preset with the current Layer ID
    return _find_preset_param(utils::get_current_layer_info().layer_id(), param, preset_doc);
}

//----------------------------------------------------------------------------
// _find_preset_param
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_find_preset_param(LayerId layer_id, const Param *param, PresetDoc& preset_doc)
{
    // Search the preset common json data
    for (rapidjson::Value::ValueIterator itr = preset_doc.preset_common_params_json_data->Begin(); itr != preset_doc.preset_common_params_json_data->End(); ++itr) {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == param->path()) {
            return itr;
        }
    }
    
    // Search the preset Layer json data
    rapidjson::Value& layer_json_data = _get_layer_params_json_data(layer_id, preset_doc);
    for (rapidjson::Value::ValueIterator itr = layer_json_data.Begin(); itr != layer_json_data.End(); ++itr) {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == param->path()) {
            return itr;
        }
    }
    
    // Search the preset Common json data
    rapidjson::Value& common_json_data = _get_patch_common_json_data(layer_id, preset_doc);
    for (rapidjson::Value::ValueIterator itr = common_json_data.Begin(); itr != common_json_data.End(); ++itr) {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == param->path()) {
            return itr;
        }
    }

    // Search the state preset data
    rapidjson::Value& state_json_data = _get_patch_state_json_data(layer_id, utils::get_current_layer_info().layer_state(), param, preset_doc);
    for (rapidjson::Value::ValueIterator itr = state_json_data.Begin(); itr != state_json_data.End(); ++itr) {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == param->path()) {
            return itr;
        }
    }                                 
    return nullptr;
}

//----------------------------------------------------------------------------
// _update_patch_state_params
//----------------------------------------------------------------------------
void FileManager::_update_patch_state_params(LayerState state)
{
    // Parse the preset params
    auto params = utils::get_preset_params();
    for (Param *p : params) {        
        // Is this a state param?
        if (p->type() == ParamType::PATCH_STATE) {
            bool param_missed = true;

            // Check if there is a patch for this param
            rapidjson::Value& json_data = _get_patch_state_json_data(utils::get_current_layer_info().layer_id(), state, p, _preset_doc);
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr) {
                // Does this entry match the param?
                if (itr->GetObject()["path"].GetString() == p->path()) {
                    // Update the parameter value in the JSON
                    (p->data_type() == ParamDataType::FLOAT) ?
                        itr->GetObject()["value"].SetFloat(p->hr_value()) :
                        itr->GetObject()["str_value"].SetString(p->str_value(), _preset_json_data.GetAllocator());

                    // If we are updating a param not in the current state, make sure the param is also updated
                    if (utils::get_current_layer_info().layer_state() != state) {
                        // Update the param in the other state
                        static_cast<LayerStateParam *>(p)->set_state_value(utils::get_current_layer_info().layer_id(), state, p->value());
                    }
                    param_missed = false;
                    break;
                }
            }
            if (param_missed && p->mod_matrix_param() && (p->hr_value() != 0.0f) && p->save()) {
                // Add the missing Mod Matrix param to the patch
                rapidjson::Value obj;
                obj.SetObject();
                obj.AddMember("path", std::string(p->path()), _preset_json_data.GetAllocator());
                (p->data_type() == ParamDataType::FLOAT) ?
                    obj.AddMember("value", p->hr_value(), _preset_json_data.GetAllocator()) :
                    obj.AddMember("str_value", p->str_value(), _preset_json_data.GetAllocator());
                json_data.PushBack(obj, _preset_json_data.GetAllocator());

                // If we are updating a param not in the current state, make sure the param is also updated
                if (utils::get_current_layer_info().layer_state() != state) {
                    // Update the param in the other state
                    static_cast<LayerStateParam *>(p)->set_state_value(utils::get_current_layer_info().layer_id(), state, p->value());
                }                
            }
        }
    }
}

//----------------------------------------------------------------------------
// _get_layer_params_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_layer_params_json_data(LayerId layer_id, PresetDoc& preset_doc)
{
    // Return the Layer params data
    // Note: It is checked earlier if this element exists and is an array
    auto json_data = (layer_id == LayerId::D0) ? preset_doc.d0_layer_params_json_data : preset_doc.d1_layer_params_json_data;
    assert(json_data);
    return json_data->GetArray();
}

//----------------------------------------------------------------------------
// _get_patch_common_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_patch_common_json_data(LayerId layer_id, PresetDoc& preset_doc)
{
    // Return the Layer patch common params data
    // Note: It is checked earlier if this element exists and is an array
    auto json_data = (layer_id == LayerId::D0) ? preset_doc.d0_patch_json_data : preset_doc.d1_patch_json_data;
    assert(json_data);
    return (*json_data)["common"].GetArray();
}

//----------------------------------------------------------------------------
// _get_patch_state_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_patch_state_json_data(LayerId layer_id, LayerState layer_state, const Param *param, PresetDoc& preset_doc)
{
    // Check if this is a state A only param
    bool state_a_only_param = (param->type() == ParamType::PATCH_STATE) && static_cast<const LayerStateParam *>(param)->state_a_only_param();
    return (layer_state == LayerState::STATE_A) || state_a_only_param ? 
                _get_patch_state_a_json_data(layer_id, preset_doc) :
                _get_patch_state_b_json_data(layer_id, preset_doc);     
}

//----------------------------------------------------------------------------
// _get_patch_state_a_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_patch_state_a_json_data(LayerId layer_id, PresetDoc& preset_doc)
{
    // Return the patch State A data
    // Note: It is checked earlier if this element exists and is an array
    auto json_data = (layer_id == LayerId::D0) ? preset_doc.d0_patch_json_data : preset_doc.d1_patch_json_data;
    assert(json_data);
    return (*json_data)["state_a"].GetArray();
}

//----------------------------------------------------------------------------
// _get_patch_state_a_json_data
//----------------------------------------------------------------------------
rapidjson::Value *FileManager::_get_patch_state_a_json_data(LayerId layer_id, rapidjson::Document &json_data)
{
    rapidjson::Value *ret = nullptr;

    // Iterate through the layers
    for (rapidjson::Value::ValueIterator itr = json_data["layers"].Begin(); itr != json_data["layers"].End(); ++itr) {
        // Get the layer ID and check if it is the Layer we have requested
        auto id = itr->GetObject()["layer_id"].GetString();
        if ((std::strcmp(id,"d0") == 0) && (layer_id == LayerId::D0)) {
            // Return the State A data
            ret = &itr->GetObject()["patch"]["state_a"];
        }
        else if ((std::strcmp(id,"d1") == 0) && (layer_id == LayerId::D1)) {
            // Return the State A data
            ret = &itr->GetObject()["patch"]["state_a"];
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _get_patch_state_b_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_patch_state_b_json_data(LayerId layer_id, PresetDoc& preset_doc)
{
    // Return the patch State B data
    // Note: It is checked earlier if this element exists and is an array
    auto json_data = (layer_id == LayerId::D0) ? preset_doc.d0_patch_json_data : preset_doc.d1_patch_json_data;
    assert(json_data);
    return (*json_data)["state_b"].GetArray();
}

//----------------------------------------------------------------------------
// _get_layer_json_data
//----------------------------------------------------------------------------
rapidjson::Value* FileManager::_get_layer_json_data(LayerId layer_id, rapidjson::Document &json_data)
{
    rapidjson::Value *ret = nullptr;

    // Iterate through the layers
    for (rapidjson::Value::ValueIterator itr = json_data["layers"].Begin(); itr != json_data["layers"].End(); ++itr) {
        // Get the layer ID and check if it is the Layer we have requested
        auto id = itr->GetObject()["layer_id"].GetString();
        if ((std::strcmp(id,"d0") == 0) && (layer_id == LayerId::D0)) {
            // Return the Layer 1 data
            ret = itr;
            break;
        }
        else if ((std::strcmp(id,"d1") == 0) && (layer_id == LayerId::D1)) {
            // Return the Layer 2 data
            ret =  itr;
            break;
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _calc_and_set_layer_voices
//----------------------------------------------------------------------------
void FileManager::_calc_and_set_layer_voices()
{
    // Get the Layer 1 and Layer 2 number of voices allocated
    auto l1_num_voices_param = static_cast<LayerParam *>(utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES));
    auto l2_num_voices_param = static_cast<LayerParam *>(utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES));
    if (l1_num_voices_param && l2_num_voices_param) {
        uint layer_n_voices = 0;
        uint leftover_voices = 0;

        // Firstly get the D0 layer number of voices
        auto obj = _find_preset_param(LayerId::D0, l1_num_voices_param, _preset_doc);

        // Check the number of voices is within range (1-6)
        layer_n_voices = l1_num_voices_param->hr_value();
        if (layer_n_voices == 0) {
            layer_n_voices = 1;
            l1_num_voices_param->set_hr_value(layer_n_voices);
            if (obj) {
                obj->GetObject()["value"].SetFloat((float)layer_n_voices);
            }                
        }  
        if (layer_n_voices > common::NUM_VOICES) {
            layer_n_voices = common::NUM_VOICES;
            l1_num_voices_param->set_hr_value(layer_n_voices);
            if (obj) {
                obj->GetObject()["value"].SetFloat((float)layer_n_voices);
            }
        }
        utils::get_layer_info(LayerId::D0).set_num_voices(layer_n_voices);        
        MSG("Layer D0 voices: " << layer_n_voices);

        // Any unallocated voices are automatically allocated to Layer D1
        leftover_voices = common::NUM_VOICES - layer_n_voices;

        // Get the D1 layer number of voices
        obj = _find_preset_param(LayerId::D1, l2_num_voices_param, _preset_doc);

        // Check the number of voices is as expected
        layer_n_voices = l2_num_voices_param->hr_value();
        if (layer_n_voices != leftover_voices) {
            layer_n_voices = leftover_voices;
            l2_num_voices_param->set_hr_value(layer_n_voices);
            if (obj) {
                obj->GetObject()["value"].SetFloat((float)layer_n_voices);
            }                
        }
        utils::get_layer_info(LayerId::D1).set_num_voices(layer_n_voices); 
        MSG("Layer D1 voices: " << layer_n_voices);
    }
}

//----------------------------------------------------------------------------
// _handle_preset_special_case_params
//----------------------------------------------------------------------------
void FileManager::_handle_preset_special_case_params(SystemFuncType event)
{
    // Handle special case params:
    // - LFO N Tempo Sync
    // - VCF Cutoff Link
    // - FX Macro Level
    // - RES HP/LP select
    // - VCF LP Cutoff Mode
    
    // If this is a Preset Load or Layer Load event
    if ((event == SystemFuncType::LOAD_PRESET) || (event == SystemFuncType::LOAD_PRESET_LAYER) ||
        (event == SystemFuncType::LOAD_LAYER_1) || (event == SystemFuncType::LOAD_LAYER_2)) {
        // Get the relevant LFO Tempo Sync param - so we can make sure the LFO rate
        // is in the correct state for this preset
        Param *param;
        if (utils::lfo_state() == utils::LfoState::LFO_1_STATE) {
            param = utils::get_param(utils::ParamRef::LFO_1_TEMPO_SYNC);
        }
        else if (utils::lfo_state() == utils::LfoState::LFO_2_STATE) {
            param = utils::get_param(utils::ParamRef::LFO_2_TEMPO_SYNC);
        }
        else {
            param = utils::get_param(utils::ParamRef::LFO_3_TEMPO_SYNC);
        }
        if (param) {
            // Set the LFO rate state
            utils::set_lfo_rate_state(param->value() ? utils::LfoRateState::LFO_SYNC_RATE : utils::LfoRateState::LFO_NORMAL_RATE);
        }

        // We need to make sure the VCF Cutoff Link switch has the correct state
        auto sfp = utils::get_sys_func_param(SystemFuncType::VCF_CUTOFF_LINK);
        auto clp = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::CUTOFF_LINK_PARAM_ID);
        if (sfp && clp) {
            auto lp = sfp->linked_param();

            // If the linked param has been specified
            if (lp) {        
                // Set the sys func param
                sfp->set_value(clp->value());

                // Set the mapped param - should be just one, and mapped to a SFC control
                auto mp = sfp->mapped_params(nullptr);
                if ((mp.size() == 1) && (mp[0]->module() == MoniqueModule::SFC_CONTROL)) {
                    mp[0]->set_value(clp->value());
                }

                // Enable/disable the standard linked param
                lp->enable_linked_param(clp->value() ? true : false);
            }
        }

        // If this is just a Preset Load event
        if (event == SystemFuncType::LOAD_PRESET) {
            // The RES HP/LP must also be set to LP (switch ON)
            sfp = utils::get_sys_func_param(SystemFuncType::VCF_RES_SELECT);
            if (sfp) {
                // Set the sys func param
                sfp->set_value(1.0);

                // Get the mapped param- should be just one, and mapped to a SFC control
                auto mp = sfp->mapped_params(nullptr);
                if ((mp.size() == 1) && (mp[0]->module() == MoniqueModule::SFC_CONTROL)) {
                    mp[0]->set_value(1.0);
                }
            }

            // Get the FX Macro Select param
            auto msp = utils::get_param(utils::ParamRef::FX_MACRO_SELECT);
            if (msp) {
                // Get the FX Macro and FX Macro Level params
                auto fmp = utils::get_param(MoniqueModule::DAW, Monique::fx_macro_params().at(msp->hr_value()));
                auto fmlp = utils::get_param(utils::ParamRef::FX_MACRO_LEVEL);
                if (fmp && fmlp) {   
                    // Set the FX Macro Level param to the FX Macro param value
                    fmlp->set_value(fmp->value());

                    // We need to update the (surface) param mapped to the FX Param
                    auto sfp = utils::get_sys_func_param(SystemFuncType::FX_PARAM);
                    if (sfp) {
                        // Get the mapped param and check there is only one mapped, which must be
                        // a surface control
                        auto mp = sfp->mapped_params(nullptr);
                        if ((mp.size() == 1) && (mp[0]->module() == MoniqueModule::SFC_CONTROL)) {
                            // Update the surface control - in the FX param state
                            auto fxs = utils::fx_state();
                            utils::set_fx_state(utils::FxState::FX_PARAM_STATE);
                            static_cast<SfcControlParam *>(mp[0])->set_control_state(utils::fx_ui_state());
                            mp[0]->set_value(fmp->value());
                            utils::set_fx_state(fxs);
                            static_cast<SfcControlParam *>(mp[0])->set_control_state(utils::fx_ui_state());
                        }
                    }                    
                }
            }  
        }
    }

    // We need to make sure the VCF LP Cutoff Mode switch has the correct state - do this for all events
    auto sfp = utils::get_sys_func_param(SystemFuncType::VCF_LP_CUTOFF_MODE);
    if (sfp) {
        auto lp = sfp->linked_param();

        // If the linked param has been specified
        if (lp) {        
            // Set the sys func param
            sfp->set_value(lp->value());

            // Set the mapped param - should be just one, and mapped to a SFC control
            auto mp = sfp->mapped_params(nullptr);
            if ((mp.size() == 1) && (mp[0]->module() == MoniqueModule::SFC_CONTROL)) {
                mp[0]->set_value(lp->value());
            }
        }
    }
}

//----------------------------------------------------------------------------
// _check_if_morphing
//----------------------------------------------------------------------------
void FileManager::_check_if_morphing()
{
    // Are we morphing on the current layer?
    if ((((utils::get_current_layer_info().layer_state() == LayerState::STATE_A) &&
          (utils::get_current_layer_info().morph_value() > 0.0f)) ||
         ((utils::get_current_layer_info().layer_state() == LayerState::STATE_B) &&
          (utils::get_current_layer_info().morph_value() < 1.0f))) &&
        (utils::morph_mode() == Monique::MorphMode::DANCE)) {
        // Enable morphing
        utils::set_morph_state(true);           
    }
}

//----------------------------------------------------------------------------
// _check_layer_1_patch_name
//----------------------------------------------------------------------------
void FileManager::_check_layer_1_patch_name(const PresetId& preset_id)
{
    // If Layer 2 is disabled (0 voices) AND the patch name for Layer 1
    // is the INIT patch name, change the Layer 1 patch name to be the
    // preset name
    if ((utils::get_layer_info(LayerId::D1).num_voices() == 0) && (utils::get_layer_info(LayerId::D0).patch_name() == BASIC_PRESET_L1_PATCH_NAME)) {
        // Set the Layer 1 preset name
        utils::get_layer_info(LayerId::D0).set_patch_name(preset_id.preset_display_name_short());
        rapidjson::Value& json_data = *_preset_doc.d0_patch_json_data;
        if (json_data.HasMember("name") && json_data["name"].IsString()) {
            json_data["name"].SetString(preset_id.preset_display_name_short(), _preset_json_data.GetAllocator());
            _start_save_preset_file_timer();                
        }
    }    
}

//----------------------------------------------------------------------------
// _set_param_from_basic_preset
//----------------------------------------------------------------------------
void FileManager::_set_param_from_basic_preset(Param *param)
{
    // Gset the default value from the BASIC preset, and if found set the param
    auto itr = _find_preset_param(param, _basic_preset_doc);
    if (itr) {
        // Update the param value
        (param->data_type() == ParamDataType::FLOAT) ?
            param->set_hr_value(itr->GetObject()["value"].GetFloat()) :
            param->set_str_value(itr->GetObject()["str_value"].GetString());
    }
}

//----------------------------------------------------------------------------
// _open_preset_file
// Note: Private functions
//----------------------------------------------------------------------------
bool _open_preset_file(std::string file_path, rapidjson::Document &json_data)
{
    const char *schema =
#include "../json_schemas/preset_schema.json"
;

    // Open the preset file
    bool ret = ::_open_json_file(file_path, schema, json_data, false);
    if (!ret)
    {
        // The preset could not be opened - most likely as it doesn't exist
        MSG("The preset file could not be opened: " << file_path);
        MONIQUE_LOG_INFO(MoniqueModule::FILE_MANAGER, "The preset file could not be opened: {}", file_path);

        // If we are trying to open the BASIC preset file, then just return with false (this is
        // a critical error)
        if (file_path == MONIQUE_ROOT_FILE_PATH(BASIC_PRESET_FILE)) {
            return false;
        }

        // Use the BASIC preset settings
        MSG("Using BASIC preset settings");
        MONIQUE_LOG_INFO(MoniqueModule::FILE_MANAGER, "Using BASIC preset settings");
        json_data.CopyFrom(::_basic_preset_json_data, json_data.GetAllocator());
    }

    // If the JSON data is empty its an invalid file
    if (!json_data.IsObject()) {
        // This is a critical error
        MSG("The preset file is an empty file");
        MONIQUE_LOG_CRITICAL(MoniqueModule::FILE_MANAGER, "The preset file is an empty file");        
        return false;
    }

    // Does any preset data exist - the schema checks this but do it here anyway
    // We can assume the schema checks that the rest of the document is formatted correctly
    if (!json_data.HasMember("layers") || !json_data["layers"].IsArray()) {
        // This is a critical error
        MSG("The preset file format is invalid");
        MONIQUE_LOG_CRITICAL(MoniqueModule::FILE_MANAGER, "The preset file format is invalid");         
        return false;
    }
    return true;
}

//----------------------------------------------------------------------------
// _open_json_file
// Note: Private functions
//----------------------------------------------------------------------------
bool _open_json_file(std::string file_path, const char *schema, rapidjson::Document &json_data, bool create, std::string def_contents)
{
    rapidjson::Document schema_data;  
    
    // Open the JSON file
    std::fstream json_file;
    json_data.SetNull();
    json_data.GetAllocator().Clear();    
    json_file.open(file_path, std::fstream::in);
    if (!json_file.good())
    {
        // Couldn't open the file, should we create it?
        if (!create)
        {
            DEBUG_MSG("JSON file open error: " << file_path);
            return false;
        }            
            
        // Try and create the file
        json_file.open(file_path, std::fstream::out);
        if (!json_file.good())
        {
            DEBUG_MSG("JSON file create error: " << file_path);
            return false;
        }

        // File is now created, but empty
        // We can close it and clear the JSON data structure
        json_data.Parse(def_contents);
        json_file.close();
        return true;
    }

    // The file is open, check if it is empty
    std::string json_file_contents((std::istreambuf_iterator<char>(json_file)), std::istreambuf_iterator<char>());
    if (json_file_contents.empty())
    {
        // Its empty, so just clear the JSON data and return
        json_data.Parse(def_contents);
        json_file.close();
        return true;
    }

    // Get the JSON file contents and ensure there are no JSON errors
    json_data.Parse(json_file_contents.c_str());
    if (json_data.HasParseError())
    {
        DEBUG_MSG("JSON file read error: " << file_path);
        json_data.Parse(def_contents);
        json_file.close();
        return false;
    }

    // Get the JSON schema and ensure there are no schema errors
    schema_data.Parse(schema);
    if (schema_data.HasParseError())
    {
        DEBUG_MSG("JSON schema error: " << file_path);
        json_data.Parse(def_contents);
        json_file.close();
        return false;
    }

    // Now validate the JSON data against the passed schema
    rapidjson::SchemaDocument schema_document(schema_data);
    rapidjson::SchemaValidator schema_validator(schema_document);
    if (!json_data.Accept(schema_validator))
    {
        DEBUG_MSG("Schema validation failed: " << file_path);
        json_data.Parse(def_contents);
        json_file.close();
        return false;
    }

    // JSON file OK, JSON data read OK
    json_file.close();
    return true;
}
