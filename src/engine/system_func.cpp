/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  system_func.cpp
 * @brief System Func implementation.
 *-----------------------------------------------------------------------------
 */

#include <iostream>
#include "system_func.h"
#include "param.h"
#include "utils.h"
#include "ui_common.h"


// Static data
// System Func type names
const char *SystemFunc::_type_names[] = {
    "sfc_init",
    "soft_button_1",
    "soft_button_2",
    "data_knob",
    "mod_matrix",
    "load",
    "load_preset",
    "load_preset_layer",
    "load_preset_sound",
    "restore_prev_preset",
    "save",
    "save_preset",
    "store_morph_to_preset_sound",
    "bank_renamed",
    "patch_renamed",
    "toggle_patch_state",
    "osc_1_osc_2_select",
    "osc_3_osc_4_select",
    "osc_coarse",
    "lfo_1_select",
    "lfo_2_select",
    "lfo_3_select",
    "lfo_shape",
    "vcf_lp_cutoff_mode",
    "vcf_cutoff_link",
    "vcf_res_select",
    "vcf_eg_select",
    "vca_eg_select",
    "aux_eg_select",
    "fx_macro",
    "fx_param",
    "octave_dec",
    "octave_inc",
    "seq_menu",
    "seq_rec",
    "seq_run",
    "seq_reset",
    "preset_inc",
    "preset_dec",
    "bank",
    "select_layer_1",
    "select_layer_2",
    "load_layer_1",
    "load_layer_2",
    "patch_menu",
    "multi_menu",
    "wave_menu",
    "arp_menu",
    "osc_menu",
    "lfo_menu",
    "vcf_menu",
    "env_menu",
    "fx1_menu",
    "fx2_menu",
    "tempo_select",
    "glide_select",
    "multifn_switch",
    "set_mod_src_num",
    "start_sw_update",
    "finish_sw_update",
    "set_system_colour",
    "init_preset",
    "save_demo_mode",
    "reset_global_settings",
    "screen_capture",
    "nul"
};

//----------------------------------------------------------------------------
// RegisterParams
//----------------------------------------------------------------------------
void SystemFunc::RegisterParams()
{
    // Register each system func param
    auto param = SystemFuncParam::CreateParam(SystemFuncType::SFC_INIT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SOFT_BUTTON_1);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SOFT_BUTTON_2);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::DATA_KNOB);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::MOD_MATRIX);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LOAD);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LOAD_PRESET);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LOAD_PRESET_LAYER);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LOAD_PRESET_SOUND);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::RESTORE_PREV_PRESET);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SAVE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SAVE_PRESET);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::STORE_MORPH_TO_PRESET_SOUND);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::BANK_RENAMED);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::PATCH_RENAMED);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::TOGGLE_PATCH_STATE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::OSC_1_OSC_2_SELECT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::OSC_3_OSC_4_SELECT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::OSC_COARSE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LFO_1_SELECT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LFO_2_SELECT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LFO_3_SELECT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LFO_SHAPE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::VCF_LP_CUTOFF_MODE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::VCF_CUTOFF_LINK);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::VCF_RES_SELECT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::VCF_EG_SELECT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::VCA_EG_SELECT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::AUX_EG_SELECT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::FX_MACRO);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::FX_PARAM);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::OCTAVE_DEC);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::OCTAVE_INC);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SEQ_MENU);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SEQ_REC);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SEQ_RUN);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SEQ_RESET);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::PRESET_INC);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::PRESET_DEC);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::BANK);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SELECT_LAYER_1);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SELECT_LAYER_2);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LOAD_LAYER_1);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LOAD_LAYER_2);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::PATCH_MENU);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::MULTI_MENU);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::WAVE_MENU);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::ARP_MENU);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::OSC_MENU);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LFO_MENU);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::VCF_MENU);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::ENV_MENU);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::FX1_MENU);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::FX2_MENU);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::TEMPO_SELECT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::GLIDE_SELECT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::MULTIFN_SWITCH);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::START_SW_UPDATE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::FINISH_SW_UPDATE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SET_SYSTEM_COLOUR);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::INIT_PRESET);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SAVE_DEMO_MODE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::RESET_GLOBAL_SETTINGS);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SCREEN_CAPTURE_JPG);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::NUL);
    utils::register_param(std::move(param));
}

//----------------------------------------------------------------------------
// TypeName
//----------------------------------------------------------------------------
const std::string SystemFunc::TypeName(SystemFuncType type)
{
    // Return the type name
    return _type_names[static_cast<int>(type)];
}

//----------------------------------------------------------------------------
// SystemFunc
//----------------------------------------------------------------------------
SystemFunc::SystemFunc()
{
    // Initialise class data
    linked_param = nullptr;
    result = false;
}

//----------------------------------------------------------------------------
// SystemFunc
//----------------------------------------------------------------------------
SystemFunc::SystemFunc(SystemFuncType type, MoniqueModule from_module)
{
    // Initialise class data
    this->type = type;
    this->value = 0;
    this->linked_param = nullptr;
    this->from_module = from_module;
}

//----------------------------------------------------------------------------
// SystemFunc
//----------------------------------------------------------------------------
SystemFunc::SystemFunc(SystemFuncType type, float value, MoniqueModule from_module)
{
    // Initialise class data
    this->type = type;
    this->value = value;
    this->linked_param = nullptr;
    this->from_module = from_module;
}

//----------------------------------------------------------------------------
// SystemFunc
//----------------------------------------------------------------------------
SystemFunc::SystemFunc(SystemFuncType type, PresetId id, MoniqueModule from_module)
{
    // Initialise class data
    this->type = type;
    this->value = 0;
    this->preset_id = id;
    this->linked_param = nullptr;
    this->from_module = from_module;
}

//----------------------------------------------------------------------------
// SystemFunc
//----------------------------------------------------------------------------
SystemFunc::SystemFunc(SystemFuncType type, uint num, MoniqueModule from_module)
{
    // Initialise class data
    this->type = type;
    this->num = num;
    this->value = 0;
    this->linked_param = nullptr;
    this->from_module = from_module;
    this->result = false;
}

//----------------------------------------------------------------------------
// ~SystemFunc
//----------------------------------------------------------------------------
SystemFunc::~SystemFunc()
{
    // Nothing specific to do
}
