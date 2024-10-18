/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  system_func.h
 * @brief System Function definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _SYSTEM_FUNC_H
#define _SYSTEM_FUNC_H

#include <string>
#include "ui_common.h"
#include "preset_id.h"

// External classes
class Param;

// System Function types
enum class SystemFuncType
{
    SFC_INIT = 0,
    SOFT_BUTTON_1,
    SOFT_BUTTON_2,
    DATA_KNOB,
    MOD_MATRIX,
    LOAD,
    LOAD_PRESET,
    LOAD_PRESET_LAYER,
    LOAD_PRESET_SOUND,
    RESTORE_PREV_PRESET,
    SAVE,
    SAVE_PRESET,
    STORE_MORPH_TO_PRESET_SOUND,
    BANK_RENAMED,
    PATCH_RENAMED,
    TOGGLE_PATCH_STATE,
    OSC_1_OSC_2_SELECT,
    OSC_3_OSC_4_SELECT,
    OSC_COARSE,
    LFO_1_SELECT,
    LFO_2_SELECT,
    LFO_3_SELECT,
    LFO_SHAPE,
    VCF_LP_CUTOFF_MODE,
    VCF_CUTOFF_LINK,
    VCF_RES_SELECT,
    VCF_EG_SELECT,
    VCA_EG_SELECT,
    AUX_EG_SELECT,
    FX_MACRO,
    FX_PARAM,
    OCTAVE_DEC,
    OCTAVE_INC,
    SEQ_MENU,
    SEQ_REC,
    SEQ_RUN,
    SEQ_RESET,
    PRESET_INC,
    PRESET_DEC,
    BANK,
    SELECT_LAYER_1,
    SELECT_LAYER_2,
    LOAD_LAYER_1,
    LOAD_LAYER_2,
    PATCH_MENU,
    MULTI_MENU,
    WAVE_MENU,
    ARP_MENU,
    OSC_MENU,
    LFO_MENU,
    VCF_MENU,
    ENV_MENU,
    FX1_MENU,
    FX2_MENU,
    TEMPO_SELECT,
    GLIDE_SELECT,
    MULTIFN_SWITCH,
    SET_MOD_SRC_NUM,
    START_SW_UPDATE,
    FINISH_SW_UPDATE,
    SET_SYSTEM_COLOUR,
    INIT_PRESET,
    SAVE_DEMO_MODE,
    RESET_GLOBAL_SETTINGS,
    SCREEN_CAPTURE_JPG,
    NUL,
    UNKNOWN
};

// System Function struct
class SystemFunc
{
public:
    // Public data
    MoniqueModule from_module;
    SystemFuncType type;
    float value;
    PresetId preset_id;
    LayerId src_layer_id;
    LayerId dst_layer_id;
    LayerState dst_layer_state;
    uint num;
    std::string str_value;
    std::string str_value_2;
    bool result;
    Param *linked_param;

    // Helper functions
    static void RegisterParams(void);
    static const std::string TypeName(SystemFuncType type);

    // Constructor/Destructor
    SystemFunc();
    SystemFunc(SystemFuncType type, MoniqueModule from_module);
    SystemFunc(SystemFuncType type, float value, MoniqueModule from_module);
    SystemFunc(SystemFuncType type, PresetId id, MoniqueModule from_module);
    SystemFunc(SystemFuncType type, uint num, MoniqueModule from_module);
    ~SystemFunc();

private:
    // Private data
    static const char *_type_names[];
};

#endif  // _SYSTEM_FUNC_H
