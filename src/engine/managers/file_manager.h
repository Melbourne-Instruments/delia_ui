/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  file_manager.h
 * @brief File Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _FILE_MANAGER_H
#define _FILE_MANAGER_H

#define RAPIDJSON_HAS_STDSTRING 1
#include "rapidjson/document.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/filewritestream.h"
#include "base_manager.h"
#include "daw_manager.h"
#include "event.h"
#include "event_router.h"
#include "param.h"
#include "system_func.h"
#include "timer.h"

// Current Preset Save State
enum class CurrentPresetSaveState
{
    A,
    B
};

// Preset document
struct PresetDoc
{
    rapidjson::Value *preset_common_params_json_data;
    rapidjson::Value *d0_layer_json_data;
    rapidjson::Value *d0_layer_params_json_data;
    rapidjson::Value *d0_patch_json_data;
    rapidjson::Value *d1_layer_json_data;
    rapidjson::Value *d1_layer_params_json_data;
    rapidjson::Value *d1_patch_json_data;

    PresetDoc() {
        preset_common_params_json_data = nullptr;
        d0_layer_json_data = nullptr;
        d0_layer_params_json_data = nullptr;
        d0_patch_json_data = nullptr;
        d1_layer_json_data = nullptr;
        d1_layer_params_json_data = nullptr;
        d1_patch_json_data = nullptr;        
    } 
};

// File Manager class
class FileManager : public BaseManager
{
public:
    // Helper functions
    static bool PresetIsMultiTimbral(PresetId preset_id);
    static std::string PresetLayerName(PresetId preset_id, LayerId layer_id);

    // Constructor
    FileManager(EventRouter *event_router);

    // Destructor
    ~FileManager();

    // Functions
    bool start();
    void stop();
    void process();
    void process_event(const BaseEvent *event);

private:
    // Private variables
    DawManager *_daw_manager;
    EventListener *_param_changed_listener;
    EventListener *_system_func_listener;
    rapidjson::Document _config_json_data;
    rapidjson::Document _param_map_json_data;
    rapidjson::Document _global_params_json_data;
    rapidjson::Document _preset_json_data;
    PresetDoc _preset_doc;
    PresetDoc _basic_preset_doc;
    std::mutex _preset_mutex;
    Timer *_save_config_file_timer;
    Timer *_save_global_params_file_timer;
    Timer *_save_preset_file_timer;
    CurrentPresetSaveState _current_preset_save_state;

    // Private functions 
    void _process_param_changed_event(const ParamChange &param_change);
    void _process_system_func_event(const SystemFunc &system_func);
    void _reset_layers();
    void _setup_layer(LayerId layer_id, std::vector<Param *> &params, bool inc_layer_params=true);
    bool _open_config_file();
    void _open_and_parse_param_blacklist_file();
    bool _open_param_map_file();
    bool _open_and_parse_param_attributes_file();
    bool _open_and_parse_param_lists_file();
    void _open_and_parse_system_colours_file();
    bool _open_and_parse_haptic_modes_file();
    bool _open_and_parse_global_params_file();
    bool _open_and_check_startup_preset_file();
    bool _open_and_check_preset_file(std::string file_path);
    bool _check_preset(rapidjson::Document& json_data, PresetDoc& preset_doc);
    void _parse_config();
    void _parse_param_map();
    void _parse_preset();
    void _parse_preset_common_params(std::vector<Param *> &params);
    void _parse_preset_layer(std::vector<Param *> &params, bool inc_layer_params);
    void _parse_layer_params(std::vector<Param *> &params);
    void _parse_layer_patch_params(std::vector<Param *> &params);
    void _parse_patch_common_params(std::vector<Param *> &params);
    void _parse_patch_state_params(std::vector<Param *> &params, LayerState state);
    void _process_layer_mapped_params(const Param *param, const Param *skip_param);
    void _save_config_file();
    void _save_global_params_file();
    void _save_current_preset_file();
    void _save_preset_file();
    void _save_preset_file(std::string file_path);    
    void _save_json_file(std::string file_path, const rapidjson::Document &json_data);
    void _start_save_config_file_timer();
    void _start_save_global_params_file_timer();
    void _start_save_preset_file_timer();
    void _update_patch_state_params(LayerState state);
    rapidjson::Value& _get_layer_params_json_data(LayerId layer_id, PresetDoc& preset_doc);
    rapidjson::Value& _get_patch_common_json_data(LayerId layer_id, PresetDoc& preset_doc);
    rapidjson::Value& _get_patch_state_json_data(LayerId layer_id, LayerState layer_state, const Param *param, PresetDoc& preset_doc);
    rapidjson::Value& _get_patch_state_a_json_data(LayerId layer_id, PresetDoc& preset_doc);
    rapidjson::Value *_get_patch_state_a_json_data(LayerId layer_id, rapidjson::Document &json_data);
    rapidjson::Value& _get_patch_state_b_json_data(LayerId layer_id, PresetDoc& preset_doc);
    rapidjson::Value* _get_layer_json_data(LayerId layer_id, rapidjson::Document &json_data);
    rapidjson::Value::ValueIterator _find_global_param(std::string path);
    rapidjson::Value::ValueIterator _find_preset_param(const Param *param, PresetDoc& preset_doc);
    rapidjson::Value::ValueIterator _find_preset_param(LayerId layer_id, const Param *param, PresetDoc& preset_doc);
    void _calc_and_set_layer_voices();
    void _handle_preset_special_case_params(SystemFuncType event);
    void _check_if_morphing();
    void _check_layer_1_patch_name(const PresetId& preset_id);
    void _set_param_from_basic_preset(Param *param);
};

#endif // _FILE_MANAGER_H
