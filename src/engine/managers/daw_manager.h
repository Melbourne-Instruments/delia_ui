/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  daw_manager.h
 * @brief DAW Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _DAW_MANAGER_H
#define _DAW_MANAGER_H

#include "base_manager.h"
#include "event.h"
#include "param.h"
#include "event_router.h"
#include "sushi_client.h"

// Sushi version
struct SushiVersion
{
    std::string version;
    std::string commit_hash;
};

// DAW Manager class
class DawManager: public BaseManager
{
public:
    // Constructor
    DawManager(EventRouter *event_router);

    // Destructor
    ~DawManager();

    // Public functions
    void process();
    void process_event(const BaseEvent *event);
    void process_midi_event_direct(const snd_seq_event_t *event);
    bool get_layer_patch_state_params();
    void set_global_params(std::vector<Param *> &params);
    void set_preset_common_params(std::vector<Param *> &params);
    void set_layer_params(std::vector<Param *> &params);
    void set_layer_patch_state_params(LayerId id, LayerState state);
    void set_param(const Param *param);
    SushiVersion get_sushi_version();

private:
    // Private variables
    EventListener *_param_changed_listener;
    std::shared_ptr<sushi_controller::SushiController> _sushi_controller;
    int _main_track_id;
    SushiVersion _sushi_verson;

    // Private functions
    void _process_param_changed_event(const ParamChange &data);
    void _param_update_notification(int processor_id, int parameter_id, float value);
    void _send_param(uint layer_id_mask, const Param *param);
    void _register_params();
    std::unique_ptr<Param> _cast_sushi_param(int processor_id, const sushi_controller::ParameterInfo &sushi_param, std::string path_prefix, LayerId& layer_id);
    bool _param_has_suffix(std::string& param_name, std::string suffix);
};

#endif  // _DAW_MANAGER_H
