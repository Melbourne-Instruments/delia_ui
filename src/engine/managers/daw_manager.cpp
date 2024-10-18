/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  daw_manager.h
 * @brief DAW Manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <unistd.h>
#include <regex>
#include "daw_manager.h"
#include "sushi_client.h"
#include "utils.h"
#include "logger.h"

// Constants
constexpr char MANAGER_NAME[]               = "DawManager";
constexpr uint REGISTER_PARAMS_RETRY_COUNT  = 50;
constexpr char MAIN_TRACK_NAME[]            = "main";
constexpr char VST_PLUGIN_NAME[]            = "delia";
constexpr char D0_LAYER_PARAM_SUFFIX[]      = ":D0"; 
constexpr char D1_LAYER_PARAM_SUFFIX[]      = ":D1"; 
constexpr char GLOBAL_PARAM_SUFFIX[]        = ":G";
constexpr char PRESET_COMMON_PARAM_SUFFIX[] = ":P";
constexpr char LAYER_PARAM_SUFFIX[]         = ":L";
constexpr char PATCH_COMMON_PARAM_SUFFIX[]  = ":C";
constexpr char PATCH_STATE_A_PARAM_SUFFIX[] = ":A";
constexpr char PATCH_STATE_B_PARAM_SUFFIX[] = ":B";
constexpr char MOD_CONSTANT_PARAM_PREFX[]   = "Mod_Constant:";
constexpr char MOD_MATRIX_PARAM_PREFIX[]    = "Mod_";

//----------------------------------------------------------------------------
// DawManager
//----------------------------------------------------------------------------
DawManager::DawManager(EventRouter *event_router) : 
    BaseManager(MoniqueModule::DAW, MANAGER_NAME, event_router)
{
    std::vector<std::pair<int,int>> param_blocklist;

    // Initialise class data
    _sushi_controller = sushi_controller::CreateSushiController();
    _param_changed_listener = 0;
    _main_track_id = -1;

    // Register the DAW params
    _register_params();

    // Retrieve the Sushi build info
    auto build_info = _sushi_controller->system_controller()->get_build_info();
    if (build_info.first == sushi_controller::ControlStatus::OK) {
        _sushi_verson.version = build_info.second.version;
        _sushi_verson.commit_hash = build_info.second.commit_hash;
    }
    else {
        _sushi_verson.version = "Unknown";
    }    

    // Register the param change notification listener
    _sushi_controller->notification_controller()->subscribe_to_parameter_updates(
        std::bind(&DawManager::_param_update_notification,
        this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3), param_blocklist);
}

//----------------------------------------------------------------------------
// ~DawManager
//----------------------------------------------------------------------------
DawManager::~DawManager()
{
    // Clean up the event listeners   
    if (_param_changed_listener)
        delete _param_changed_listener;
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void DawManager::process()
{
    // Create and add the various event listeners
    _param_changed_listener = new EventListener(MoniqueModule::SYSTEM, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_param_changed_listener);
    
    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void DawManager::process_event(const BaseEvent *event)
{
    // Process the event depending on the type
    switch (event->type()) {
        case EventType::PARAM_CHANGED: {
            // Process the Param Changed event
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            break;
        }

        default:
            // Event unknown, we can ignore it
            break;
    }
}

//----------------------------------------------------------------------------
// process_midi_event
//----------------------------------------------------------------------------
void DawManager::process_midi_event_direct(const snd_seq_event_t *event)
{
    auto data = *event;

    // If the main track ID was found
    if (_main_track_id != -1) {
        // Parse the MIDI message type
        switch (data.type) {
            case SND_SEQ_EVENT_NOTEOFF: {
                // Send the NOTE OFF message to Sushi
                // Normalise midi velocity by dividing by max midi velocity
                _sushi_controller->keyboard_controller()->send_note_off(_main_track_id, data.data.note.channel, data.data.note.note, ((float)data.data.note.velocity)/127.0);
                //DEBUG_BASEMGR_MSG("Send note: " << (int)data.data.note.note << ": OFF");
                break;
            }

            case SND_SEQ_EVENT_NOTEON: {
                // Send the NOTE ON message to Sushi
                // Normalise midi velocity by dividing by max midi velocity
                _sushi_controller->keyboard_controller()->send_note_on(_main_track_id, data.data.note.channel, data.data.note.note, ((float)data.data.note.velocity)/127.0);
                //DEBUG_BASEMGR_MSG("Send note: " << (int)data.data.note.note << ": ON");
                break;
            }

            case SND_SEQ_EVENT_KEYPRESS: {
                // Send the key pressure event message to Sushi
                // Normalise midi velocity by dividing by max midi velocity                
                _sushi_controller->keyboard_controller()->send_note_aftertouch(_main_track_id,data.data.note.channel, data.data.note.note,((float)data.data.note.velocity)/127.0);
                break;
            }

            default:
                // Unknown message type ignore it
                break;
        }
    }
}

//----------------------------------------------------------------------------
// get_layer_patch_state_params
//----------------------------------------------------------------------------
bool DawManager::get_layer_patch_state_params()
{
    bool ret = false;

    // Get the Morph Value param - this is used to get the processor ID used to retrieve the
    // state param values
    auto param = utils::get_morph_value_param();
    if (param) {
        // Get the patch params
        auto patch_params = _sushi_controller->parameter_controller()->get_parameter_values(param->processor_id(),
                                                        (utils::get_current_layer_info().layer_id() == LayerId::D0 ? 0 : 1),
                                                        (utils::get_current_layer_info().layer_state() == LayerState::STATE_A ? 0 : 1));
        auto itr = patch_params.second.begin();

        // Parse the available DAW params
        auto params = utils::get_params(MoniqueModule::DAW);
        for (Param *p : params) {
            // Is this a state param?
            if ((p->type() == ParamType::PATCH_STATE)) {
                // Ignore state A only params, make sure the param ID matches
                if ((p->param_id() == itr->parameter_id) && !static_cast<LayerStateParam *>(p)->state_a_only_param()) {
                    // If the value has changed
                    if (p->value() != itr->value) {
                        // Yes, update the param value
                        p->set_value(itr->value);
                        ret = true;
                    }
                }
                itr++;
            }
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// set_global_params
//----------------------------------------------------------------------------
void DawManager::set_global_params(std::vector<Param *> &params)
{
    std::vector<sushi_controller::ParameterValue> param_values;

    // Parse the global params
    for (Param *p : params) {
        // If this is a global DAW preset param
        if ((p->type() == ParamType::GLOBAL) && (p->module() == MoniqueModule::DAW) && p->preset()) {
            // Create the param value to send
            auto param_value = sushi_controller::ParameterValue();
            param_value.processor_id = p->processor_id();
            param_value.parameter_id = p->param_id();
            param_value.value = p->value();
            param_values.push_back(param_value);     
        }
    }

    // Send the values to Sushi
    _sushi_controller->parameter_controller()->set_parameter_values(param_values);    
}

//----------------------------------------------------------------------------
// set_layer_params
//----------------------------------------------------------------------------
void DawManager::set_preset_common_params(std::vector<Param *> &params)
{
    std::vector<sushi_controller::ParameterValue> param_values;

    // Parse the preset params
    for (Param *p : params) {
        // Skip param if not a DAW param or preset
        if ((p->module() != MoniqueModule::DAW) || !p->preset()) {
            continue;
        }

        // If this is a preset common param
        if ((p->type() == ParamType::PRESET_COMMON)) {
            // Create the param value to send
            auto param_value = sushi_controller::ParameterValue();
            param_value.processor_id = p->processor_id();
            param_value.parameter_id = p->param_id();
            param_value.value = p->value();
            param_values.push_back(param_value);
        }     
    }

    // Send the values to Sushi
    _sushi_controller->parameter_controller()->set_parameter_values(param_values);

    // We also need to set the tempo in Sushi
    auto param = utils::get_tempo_param();
    if (param) {
        // Set the tempo in Sushi
        _sushi_controller->transport_controller()->set_tempo(param->hr_value());
    }
}

//----------------------------------------------------------------------------
// set_layer_params
//----------------------------------------------------------------------------
void DawManager::set_layer_params(std::vector<Param *> &params)
{
    std::vector<sushi_controller::ParameterValue> param_values;

    // Parse the preset params
    for (Param *p : params) {
        // Skip param if a global or preset common or not a DAW param or preset, or if this is the Morph Value param
        if ((p->type() == ParamType::GLOBAL) || (p->type() == ParamType::PRESET_COMMON) || (p->module() != MoniqueModule::DAW) || !p->preset() || 
            (p->param_id() == utils::get_morph_value_param()->param_id())) {
            continue;
        }

        // If this is not Layer state param
        auto param_value = sushi_controller::ParameterValue();
        if (p->type() != ParamType::PATCH_STATE) {
            // Create the param value to send
            param_value.processor_id = p->processor_id();
            param_value.parameter_id = p->param_id();
            param_value.value = p->value();
            param_values.push_back(param_value);
        }
        else {
            // Create the STATE A param to send
            param_value.processor_id = p->processor_id();
            param_value.parameter_id = static_cast<LayerStateParam *>(p)->param_id(LayerState::STATE_A);
            param_value.value = static_cast<LayerStateParam *>(p)->value(LayerState::STATE_A);
            param_values.push_back(param_value);

            // Create the STATE B param to send
            param_value.processor_id = p->processor_id();
            param_value.parameter_id = static_cast<LayerStateParam *>(p)->param_id(LayerState::STATE_B);
            param_value.value = static_cast<LayerStateParam *>(p)->value(LayerState::STATE_B);
            param_values.push_back(param_value);                     
        }

        // Batch the updates into max 100 params at a time, with a 1.2ms delay
        if (param_values.size() == 100) {
            // Send the values to Sushi
            _sushi_controller->parameter_controller()->set_parameter_values(param_values);
            std::this_thread::sleep_for(std::chrono::microseconds(1200));
            param_values.clear();
        }
    }

    // Send the last batch, if any
    if (param_values.size() > 0) {
        // Send the values to Sushi
        _sushi_controller->parameter_controller()->set_parameter_values(param_values);
        std::this_thread::sleep_for(std::chrono::microseconds(1200));
    }    
}

//----------------------------------------------------------------------------
// set_layer_patch_state_params
//----------------------------------------------------------------------------
void DawManager::set_layer_patch_state_params(LayerId id, LayerState state)
{
    std::vector<sushi_controller::ParameterValue> param_values;

    // Parse the available DAW params
    auto params = utils::get_params(MoniqueModule::DAW);
    for (Param *p : params) {
        // If this is a preset state param
        if ((p->type() == ParamType::PATCH_STATE) && p->preset()) {
            // Create the State param to send
            auto param_value = sushi_controller::ParameterValue();
            param_value.processor_id = p->processor_id();
            param_value.parameter_id = static_cast<LayerStateParam *>(p)->param_id(id, state);
            param_value.value = static_cast<LayerStateParam *>(p)->value(id, state);
            param_values.push_back(param_value);
        }
    }

    // Send the values to Sushi
    _sushi_controller->parameter_controller()->set_parameter_values(param_values);    
}

//----------------------------------------------------------------------------
// set_param
//----------------------------------------------------------------------------
void DawManager::set_param(const Param *param)
{
    // If this is a DAW param
    if (param->module() == MoniqueModule::DAW) {
        // Send the param to Sushi
        _send_param(utils::get_current_layer_info().layer_id(), param);
    }  
}

//----------------------------------------------------------------------------
// get_sushi_version
//----------------------------------------------------------------------------
SushiVersion DawManager::get_sushi_version()
{
    return _sushi_verson;
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void DawManager::_process_param_changed_event(const ParamChange &data)
{
    // Get the param and make sure it exists
    if (data.param) {
        // If this is a DAW param
        if (data.param->module() == MoniqueModule::DAW) {
            // Send the param to Sushi
            _send_param(data.layer_id_mask, data.param);
        }
        // Not a DAW param, is it however the Tempo BPM param (special case for Sushi)
        else if (data.param == utils::get_tempo_param()) {
            _sushi_controller->transport_controller()->set_tempo(data.param->hr_value());
        }
    }
}

//----------------------------------------------------------------------------
// _param_update_notification
//----------------------------------------------------------------------------
void DawManager::_param_update_notification(int processor_id, int parameter_id, float value)
{
    // Find the param to update
    auto params = utils::get_daw_params();
    for (Param *p : params) {
        // Match?
        if ((p->param_id() == parameter_id) && (p->processor_id() == processor_id)) {
            // If this is the Morphing param, we use this to enable/disable morphing for the system
            if (p->path() == "/daw/delia/Morphing") {
                // Enable/disable morphing
                utils::set_morph_state((value == 1.0) ? true : false);
            }
            break;
        }
    }
}

//----------------------------------------------------------------------------
// _send_param
//----------------------------------------------------------------------------
void DawManager::_send_param(uint layer_id_mask, const Param *param)
{
    // If this is a state param
    // Note: Assumes that the passed param is a DAW param, and the caller checks
    if ((param->type() == ParamType::GLOBAL) || (param->type() == ParamType::PRESET_COMMON)) {
        // Send the param change to Sushi
        _sushi_controller->parameter_controller()->set_parameter_value(param->processor_id(), 
                                                                       param->param_id(),
                                                                       param->value());
    }
    else {
        // Send the param change to Sushi for the required layers
        if (layer_id_mask & LayerId::D0) {
            _sushi_controller->parameter_controller()->set_parameter_value(param->processor_id(), 
                                                                        static_cast<const LayerParam *>(param)->param_id(LayerId::D0),
                                                                        static_cast<const LayerParam *>(param)->value(LayerId::D0));
        }
        if (layer_id_mask & LayerId::D1) {
            _sushi_controller->parameter_controller()->set_parameter_value(param->processor_id(), 
                                                                        static_cast<const LayerParam *>(param)->param_id(LayerId::D1),
                                                                        static_cast<const LayerParam *>(param)->value(LayerId::D1));
        }
    }
}

//----------------------------------------------------------------------------
// _register_params
//----------------------------------------------------------------------------
void DawManager::_register_params()
{
    uint num_tracks = 0;
    uint retry_count = REGISTER_PARAMS_RETRY_COUNT;
    std::pair<sushi_controller::ControlStatus, std::vector<sushi_controller::TrackInfo>> tracks;
    std::pair<sushi_controller::ControlStatus, std::vector<sushi_controller::ParameterInfo>> track_params;
    std::pair<sushi_controller::ControlStatus, std::vector<sushi_controller::ProcessorInfo>> track_processors;
    std::pair<sushi_controller::ControlStatus, std::vector<sushi_controller::ParameterInfo>> proc_params;

    // Retry until we get tracks/params from Sushi
    MSG("Registering DAW params from Sushi...");
    while (retry_count--)
    {
        // Get a list of tracks in Sushi
        tracks = _sushi_controller->audio_graph_controller()->get_all_tracks();

        // Was the track data received ok?
        if (tracks.first != sushi_controller::ControlStatus::OK)
        {
            // No - try again after waiting 100ms
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // One or more tracks reported by Sushi - process each track
        MSG("Connected to the Sushi Controller, retry attempts: " << (REGISTER_PARAMS_RETRY_COUNT-retry_count));
        num_tracks = tracks.second.size();
        for (sushi_controller::TrackInfo ti : tracks.second)
        {
            // Get the track processors (pligins)
            // Note: We don't process the track params as these are not used by the UI
            track_processors = _sushi_controller->audio_graph_controller()->get_track_processors(ti.id);

            // Parse each processor on the track
            for (sushi_controller::ProcessorInfo pi : track_processors.second)
            {
                // Get the processor params
                proc_params = _sushi_controller->parameter_controller()->get_processor_parameters(pi.id);

                // We need to check that this plug-in is supported
                if (pi.name != VST_PLUGIN_NAME) {
                    // Don't process this plugin as we don't know what it is
                    continue;
                }

                // Get each parameter on the processor
                LayerId layer_id;
                for (sushi_controller::ParameterInfo param_info : proc_params.second)
                {
                    // Use a helper fn to fill out fields in our parameter struct. use track path to complete path
                    auto daw_param = DawManager::_cast_sushi_param(pi.id, param_info, pi.name + "/", layer_id);
                    if (daw_param) {
                        // Register the param
                        //MSG(daw_param->path() << ":" << daw_param->param_id() << ":" << (int)daw_param->hr_value());
                        utils::register_param(std::move(daw_param));
                    }
                }

                // Save the track ID if this is the main track
                if (ti.name == MAIN_TRACK_NAME) {
                    _main_track_id = ti.id;
                }                
            }
        }

        // Sushi tracks processed, so we can break from the retry loop
        break;        
    }
    
    // Show a warning if we couldn't communicate with Sushi
    if (tracks.first != sushi_controller::ControlStatus::OK)
    {
        MSG("WARNING: Could not retrieve track data from Sushi, check if it is running");
        MONIQUE_LOG_INFO(MoniqueModule::DAW, "Could not retrieve track data from Sushi, check if it is running");
    }
    // Show a warning if the main track was not found
    else if (_main_track_id == -1)
    {
        MSG("WARNING: Main track not found, check the Sushi configiration");
        MONIQUE_LOG_INFO(MoniqueModule::DAW, "WARNING: Main track not found, check the Sushi configiration");
    }        
    // Show a warning if no tracks were processed
    else if (num_tracks == 0)
    {
        MSG("WARNING: Sushi did not report any tracks, no DAW params registered");
        MONIQUE_LOG_INFO(MoniqueModule::DAW, "Sushi did not report any tracks, no DAW params registered");
    }
}

//----------------------------------------------------------------------------
// _cast_sushi_param
//----------------------------------------------------------------------------
std::unique_ptr<Param> DawManager::_cast_sushi_param(int processor_id, const sushi_controller::ParameterInfo &sushi_param, std::string path_prefix, LayerId& layer_id)
{
    // Get the param default value from the Sushi plugin
    auto value = _sushi_controller->parameter_controller()->get_parameter_value(processor_id, sushi_param.id);
    if (value.first == sushi_controller::ControlStatus::OK) {
        std::unique_ptr<Param> daw_param = nullptr;
        std::string param_name = sushi_param.name;
        ParamType param_type;
        LayerState param_state = LayerState::STATE_A;

        // Check if this is a Global param
        // Note: _param_has_suffix strips the suffix if there is a match
        if (_param_has_suffix(param_name, GLOBAL_PARAM_SUFFIX)) {
            // This is a Global param
            param_type = ParamType::GLOBAL;
        }
        else if (_param_has_suffix(param_name, PRESET_COMMON_PARAM_SUFFIX)) {
            // This is a Preset Common param
            param_type = ParamType::PRESET_COMMON;
        }        
        else {
            // Check if this is a Layer param
            if (_param_has_suffix(param_name, LAYER_PARAM_SUFFIX)) {
                // This is a Layer param
                param_type = ParamType::LAYER;
            }
            else {
                // Check if this is a common Patch param
                if (_param_has_suffix(param_name, PATCH_COMMON_PARAM_SUFFIX)) {
                    // This is a common Patch param
                    param_type = ParamType::PATCH_COMMON;
                }
                else {
                    // Check if this is a State A Patch param
                    if (_param_has_suffix(param_name, PATCH_STATE_A_PARAM_SUFFIX)) {
                        // This is a State A param
                        param_type = ParamType::PATCH_STATE;
                        param_state = LayerState::STATE_A;
                    }
                    else {
                        // Check if this is a State B Patch param
                        if (_param_has_suffix(param_name, PATCH_STATE_B_PARAM_SUFFIX)) {
                            // This is a State B param
                            param_type = ParamType::PATCH_STATE;
                            param_state = LayerState::STATE_B;
                        }
                        else {
                            // Unknown param type, ignore this param
                            return nullptr;
                        }
                    }
                }
            }

            // All Layer/patch params must have the layer ID appended
            // Check if this is for the D0 or D1 Layer
            if (_param_has_suffix(param_name, D0_LAYER_PARAM_SUFFIX)) {
                layer_id = LayerId::D0;
            }
            else if (_param_has_suffix(param_name, D1_LAYER_PARAM_SUFFIX)) {
                layer_id = LayerId::D1;
            }
            else {
                // Unknown Layer ID, ignore this param
                return nullptr;  
            }
        }

        // Append the path prefix to the param name, and replace any spaces with underscores so that
        // it can be used in the path
        auto display_name = param_name;
        param_name = std::regex_replace(path_prefix + param_name, std::regex{" "}, "_");

        // Check if this param already exists
        // This will happen for Layer params
        Param *param = utils::get_param(Param::ParamPath(this, param_name));
        if (param == nullptr) {
            // Create the param
            if ((param_type == ParamType::GLOBAL) || (param_type == ParamType::PRESET_COMMON)) {
                // Create a global/preset common param
                daw_param = Param::CreateParam(this, sushi_param.id, param_name, display_name);
                param = daw_param.get();
                param->set_type(param_type);
                param->set_value(value.second);              
            }
            else if (param_type != ParamType::PATCH_STATE) {
                // Create a normal layer param
                daw_param = LayerParam::CreateParam(this, param_name, display_name);
                param = daw_param.get();
                param->set_type(param_type);
                param->set_value(value.second);
                static_cast<LayerParam *>(param)->set_param_id(layer_id, sushi_param.id);
            }
            else {
                // Create a state layer param
                daw_param = LayerStateParam::CreateParam(this, param_name, display_name);
                param = daw_param.get();
                param->set_type(param_type);
                static_cast<LayerStateParam *>(param)->set_param_id(layer_id, param_state, sushi_param.id);
                static_cast<LayerStateParam *>(param)->set_state_value(layer_id, param_state, value.second);
            }

            // If the param is not blacklisted
            if (!utils::param_is_blacklisted(param->path())) {
                // Set the processor ID
                param->set_processor_id(processor_id);

                // Get the Mod Constant and Mod Matrix param prefixes
                auto mod_constant_param_prefix = Param::ParamPath(MoniqueModule::DAW, path_prefix + MOD_CONSTANT_PARAM_PREFX);
                auto mod_matrix_param_prefix = Param::ParamPath(MoniqueModule::DAW, path_prefix + MOD_MATRIX_PARAM_PREFIX);

                // Check if this is a Mod Matrix param (ignore Mod Constant as these are not actual mod matrix params)
                if ((param->path().substr(0, mod_constant_param_prefix.size()) != mod_constant_param_prefix) &&
                    (param->path().substr(0, mod_matrix_param_prefix.size()) == mod_matrix_param_prefix)) {
                    // Find the modulation src/dst delimiter
                    auto src_dst_str = param->path().substr(mod_matrix_param_prefix.size(),
                                                            (param->path().size() - mod_matrix_param_prefix.size()));
                    int pos = src_dst_str.find(':');
                    if (pos != -1) {
                        // Get the modulation source name
                        auto src_name = src_dst_str.substr(0, pos);

                        // If the position for the destination is valid
                        if (((uint)pos + 1) < src_dst_str.size()) {
                            // Get the modulation destination name
                            auto dst_name = src_dst_str.substr((pos + 1), (src_dst_str.size() - pos));

                            // Indicate this param is a valid modulation matrix entry
                            param->set_as_mod_matrix_param(std::regex_replace(src_name, std::regex{"_"}, " "), std::regex_replace(dst_name, std::regex{"_"}, " "));      
                        }
                    }
                }
                return daw_param;
            }

            // If we get here the param is blacklisted
            MONIQUE_LOG_INFO(MoniqueModule::DAW, "DAW param blacklisted: {}", param->path());            
        }
        else {
            // The parameter has already been registered - is it a normal Layer param?
            if (((param_type == ParamType::LAYER) && (param->type() == ParamType::LAYER)) ||
                ((param_type == ParamType::PATCH_COMMON) && (param->type() == ParamType::PATCH_COMMON))) {
                // Set the param ID and value
                static_cast<LayerParam *>(param)->set_param_id(layer_id, sushi_param.id);
                param->set_value(value.second);
            }
            // Is is a Layer state param?
            else if ((param_type == ParamType::PATCH_STATE) && (param->type() == ParamType::PATCH_STATE)) {
                // Set the param ID and value
                static_cast<LayerStateParam *>(param)->set_param_id(layer_id, param_state, sushi_param.id);
                static_cast<LayerStateParam *>(param)->set_state_value(layer_id, param_state, value.second);
            }        
        }
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// _param_has_suffix
//----------------------------------------------------------------------------
bool DawManager::_param_has_suffix(std::string& param_name, std::string suffix)
{
    // Does the param have the specified suffix?
    if (param_name.substr((param_name.size() - suffix.size()), suffix.size()) == suffix) {
        // Strip the suffix and return
        param_name = param_name.substr(0, (param_name.size() - suffix.size()));
        return true;
    }
    return false;
}
