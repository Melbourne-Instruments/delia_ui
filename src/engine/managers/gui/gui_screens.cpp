/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  gui_screens.h
 * @brief GUI Manager class implementation (screens).
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <cstdlib>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include <regex>
#include <algorithm>
#include <filesystem>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/reboot.h>
#include <ifaddrs.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include "gui_manager.h"
#include "file_manager.h"
#include "sw_manager.h"
#include "utils.h"
#include "logger.h"
#include "version.h"

// Constants
constexpr char INIT_PRESET_LIST_TEXT[]    = "000 <INIT PRESET>";
constexpr uint TRUNC_SERIAL_NUM_SIZE      = 8;
constexpr uint WT_NUM_EXCEEDED_ERROR_CODE = 127;
constexpr char WT_ERROR_FILENAME[]        = "/tmp/wavetable_error.txt";

// Private variables
const char *_system_menu_options[] = {
    "PITCH/MOD WHEEL CALIBRATION",
    "FACTORY SOAK TEST",
    "QC STATUS",
    "MOTOR TEST",
    "MIX VCA CALIBRATION",
    "FILTER CALIBRATION",
    "RUN DIAGNOSTIC SCRIPT",      
    "GLOBAL SETTINGS",
    "BANK/PRESET MANAGEMENT",
    "WAVETABLE MANAGEMENT",
    "BACKUP",
    "RESTORE BACKUP",
    "STORE DEMO MODE: ",
    "ABOUT"
};

// Static functions
static void *_process_msd_event(void* data);

//----------------------------------------------------------------------------
// _show_home_screen
//----------------------------------------------------------------------------
void GuiManager::_show_home_screen(std::string preset_name)
{
    // Show the loaded preset for this Layer
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_HOME_SCREEN;
    msg.home_screen.preset_modified = utils::preset_modified();
    _strcpy_to_gui_msg(msg.home_screen.preset_name, preset_name.c_str());
    msg.home_screen.scope_mode = _scope_mode;
    _post_gui_msg(msg);

    // Set the Layer status
    _set_layer_status(); 

    // Show the soft buttons
    _set_home_screen_soft_buttons();

    // If we are not in the HOME SCREEN state
    if (_gui_state != GuiState::HOME_SCREEN) {
        // Set the GUI state
        _gui_state = GuiState::HOME_SCREEN;                

        // Set the various panel knob states
        utils::set_controls_state(utils::default_ui_state(), _event_router);
        utils::set_controls_state(utils::osc_ui_state(), _event_router);
        utils::set_controls_state(utils::lfo_ui_state(), _event_router);
        utils::set_controls_state(utils::res_ui_state(), _event_router);
        utils::set_controls_state(utils::eg_ui_state(), _event_router);
        utils::set_controls_state(utils::fx_ui_state(), _event_router);
        utils::set_controls_state(utils::osc_tune_ui_state(), _event_router);
        utils::set_controls_state(utils::lfo_rate_ui_state(), _event_router);
        utils::set_controls_state(utils::tempo_glide_ui_state(), _event_router);

        // Reset the relevant system function switches
        _config_sys_func_switches(true);
        _reset_sys_func_switches(SystemFuncType::UNKNOWN);

        // Reset the multi-function switches
        utils::reset_multifn_switches(_event_router);
    }
    
    // Reset the data knob
    _config_data_knob();
    _config_soft_button_1(false);    
}

//----------------------------------------------------------------------------
// _show_mod_matrix_src_screen
//----------------------------------------------------------------------------
void GuiManager::_show_mod_matrix_src_screen()
{
    Param *param = nullptr;

    // Get the first mod matrix source dest entry
    std::string param_path = !_showing_additional_mod_dst_params ?
                                    _mod_matrix_param_path(_selected_mod_matrix_src_index, 
                                                           (_selected_mod_matrix_src_index == (int)Monique::ModMatrixSrc::OSC_1 ? 
                                                                (uint)Monique::ModMatrixDst::OSC_2_PITCH :
                                                                (uint)Monique::ModMatrixDst::OSC_1_PITCH)) :
                                    _mod_matrix_param_path(_selected_mod_matrix_src_index, (uint)Monique::ModMatrixDst::PAN);
    param = utils::get_param(param_path);
    if (param) {
        // This param will become the root param - get the index
        // of this param in the param list
        auto index = _get_param_list_index(param, param);
        if (index >= 0) {
            // Setup the param shown settings
            _param_shown_root = param;
            _param_shown = param;
            _param_shown_index = index;
            _show_param_list = true;
            _new_param_list = true;
            _new_mod_matrix_param_list = true;
            _editing_param = false;

            // Show the param normally
            _config_soft_button_1(true, true);
            _show_param();
        }
    }
}

//----------------------------------------------------------------------------
// _show_select_preset_load_save_screen
//----------------------------------------------------------------------------
void GuiManager::_show_select_preset_load_save_screen(bool save, std::string new_bank)
{
    auto msg = GuiMsg();

    // Parse the presets bank folder
    _list_items = _parse_bank_folder(MONIQUE_PRESET_FILE_PATH((new_bank.size() > 0 ? new_bank : _selected_preset_id.bank_folder())));

    // If there are actually presets in the folder - if not then the bank folder is invalid, should never happen
    if (_list_items.size() > 0) {
        // Has a new bank folder been selected?
        if (new_bank.size() > 0) {
            // Set the first item in the list
            _selected_preset_id.set_id(new_bank, _list_items.begin()->second);
            _selected_preset_index = save ? 0 : 1;
        }

        // If the Preset ID is valid
        if (_selected_preset_id.is_valid()) {
            // Add the INIT preset if we are in LOAD
            if (!save) {
                _list_items[0] = INIT_PRESET_LIST_TEXT;
            }

            // Truncate the list if too large
            uint list_size = (_list_items.size() < LIST_MAX_LEN) ? _list_items.size() : LIST_MAX_LEN;

            // Set the left status = select the layers
            _set_left_status(_format_folder_name(_selected_preset_id.bank_folder().c_str()).c_str());

            // Show the list of presets to choose from
            msg.type = GuiMsgType::SHOW_LIST_ITEMS;
            msg.list_items.num_items = list_size;
            msg.list_items.process_enabled_state = false;
            auto list_itr = _list_items.begin();
            for (uint i=0; i<list_size; i++) {
                auto filename = (*list_itr).second;
                _strcpy_to_gui_msg(msg.list_items.list_items[i], _format_folder_name(filename.c_str()).c_str());
                list_itr++;
                if ((_selected_preset_index == -1) && filename == _selected_preset_id.preset_name()) {
                    _selected_preset_index = i;
                }
            }
            if (_selected_preset_index == -1) {
                _selected_preset_index = save ? 0 : 1;
            }
            _loaded_preset_index = _selected_preset_index;
            msg.list_items.selected_item = _selected_preset_index; 
            _post_gui_msg(msg);

            // Set the soft buttons text
            msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
            save ?
                _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "BANK") :
                _selected_preset_index ?
                    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "OPTIONS") :
                    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "----");
            save ? 
                _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "SAVE") : 
                _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "LOAD");
            _post_gui_msg(msg);
        
            // Set the data knob to the select preset state
            _num_list_items = list_size;
            _config_data_knob(_num_list_items);
            _config_soft_button_1(false);

            // Set the save edit name if not already set
            // This is always shown regardless of the patch selected to save over
            if (save && _save_edit_name.size() == 0) {
                _save_edit_name = _selected_preset_id.preset_edit_name();
                _save_preset_state = SavePresetState::SELECT_PRESET;
            }

            // Set the GUI state
            _gui_state = GuiState::MANAGE_PRESET;
            _manage_preset_state = save ? ManagePresetState::SAVE : ManagePresetState::LOAD;
            _select_preset_state = SelectPresetState::SELECT_PRESET;
        }
    }
}

//----------------------------------------------------------------------------
// _show_select_bank_screen
//----------------------------------------------------------------------------
void GuiManager::_show_select_bank_screen()
{
    auto msg = GuiMsg();

    // Parse the presets folder containing the banks
    _list_items = _parse_presets_folder();
    if (_list_items.size() > 0) {
        uint list_size = (_list_items.size() < LIST_MAX_LEN) ? _list_items.size() : LIST_MAX_LEN;

        // Set the left status = select the bank
        _set_left_status("BANKS");

        // Show the list of banks to choose from
        msg.type = GuiMsgType::SHOW_LIST_ITEMS;
        msg.list_items.num_items = list_size;
        msg.list_items.process_enabled_state = false;
        auto list_itr = _list_items.begin();
        for (uint i=0; i<list_size; i++) {
            auto folder_name = (*list_itr).second;
            _strcpy_to_gui_msg(msg.list_items.list_items[i], _format_folder_name(folder_name.c_str()).c_str());
            list_itr++;
            if (folder_name == _selected_preset_id.bank_folder()) {
                _selected_bank_index = i;
            }
        }
        msg.list_items.selected_item = _selected_bank_index;
        _post_gui_msg(msg);

        // Set the soft buttons text
        msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
        _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "RENAME");
        _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "ENTER");
        _post_gui_msg(msg);

        // Set the data knob to the list selector state
        _num_list_items = list_size;
        _config_data_knob(list_size);
        _config_soft_button_1(false);

        // Set the select preset state
        _select_preset_state = SelectPresetState::SELECT_BANK;
    }
}

//----------------------------------------------------------------------------
// _show_preset_load_option_select_patch_screen
//----------------------------------------------------------------------------
void GuiManager::_show_preset_load_option_select_patch_screen()
{
    bool show_undo_last_load = utils::system_config()->prev_preset_id().is_valid();

    // Get the selected preset
    PresetId preset_id;
    preset_id.set_id(_selected_preset_id.bank_folder(), _list_item_from_index(_selected_preset_index).second);

    // Show the list of patch sources to choose from
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    _selected_patch_src_index = 0;
    msg.list_items.num_items = show_undo_last_load ? 3 : 2;
    msg.list_items.selected_item = _selected_patch_src_index;
    msg.list_items.process_enabled_state = false;
    _strcpy_to_gui_msg(msg.list_items.list_items[0], std::string("USE L1: " + FileManager::PresetLayerName(preset_id, LayerId::D0)).c_str());
    _strcpy_to_gui_msg(msg.list_items.list_items[1], std::string("USE L2: " + FileManager::PresetLayerName(preset_id, LayerId::D1)).c_str());
    if (show_undo_last_load) {
        msg.list_items.list_item_separator[1] = true;
        _strcpy_to_gui_msg(msg.list_items.list_items[2], "UNDO LAST LOAD");
    }
    _post_gui_msg(msg);

    // Set the soft buttons text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "BACK");
    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "SELECT");
    _post_gui_msg(msg);

    // Set the data knob to the select preset state
    _num_list_items = show_undo_last_load ? 3 : 2;
    _config_data_knob(show_undo_last_load ? 3 : 2);
    _config_soft_button_1(false);

    // Set the Preset state
    _manage_preset_state = ManagePresetState::LOAD_INTO_SELECT_SRC;
    _select_preset_state = SelectPresetState::SELECT_OPTION;
}

//----------------------------------------------------------------------------
// _show_preset_load_option_select_dest_screen
//----------------------------------------------------------------------------
void GuiManager::_show_preset_load_option_select_dest_screen()
{
    // Should we show the undo option?
    if (_show_undo_last_load && !utils::system_config()->prev_preset_id().is_valid()) {
        _show_undo_last_load = false;
    }

    // Show the list of patch destinations to choose from
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    _selected_patch_dst_index = 0;
    msg.list_items.selected_item = _selected_patch_dst_index;
    msg.list_items.process_enabled_state = false;
    uint item_index = 0;
    _strcpy_to_gui_msg(msg.list_items.list_items[item_index++], "LOAD INTO: LAYER 1");
    _strcpy_to_gui_msg(msg.list_items.list_items[item_index], "LOAD INTO: LAYER 2");
    msg.list_items.list_item_separator[item_index++] = true;
    _strcpy_to_gui_msg(msg.list_items.list_items[item_index], "LOAD INTO: LAYER 1 SOUND B");
    if (utils::get_layer_info(LayerId::D1).num_voices() > 0) {
        _strcpy_to_gui_msg(msg.list_items.list_items[++item_index], "LOAD INTO: LAYER 2 SOUND B");
    }
    if (_show_undo_last_load) {
        msg.list_items.list_item_separator[item_index++] = true;
        _strcpy_to_gui_msg(msg.list_items.list_items[item_index], "UNDO LAST LOAD");    
    }
    msg.list_items.num_items = item_index + 1;
    msg.list_items.selected_item  = 0; 
    _post_gui_msg(msg);

    // Set the soft buttons text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "BACK");
    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "OK");
    _post_gui_msg(msg);

    // Set the data knob
    _num_list_items = item_index + 1;
    _config_data_knob(item_index + 1);
    _config_soft_button_1(false);

    // Set the Preset state
    _manage_preset_state = ManagePresetState::LOAD_INTO_SELECT_DST;
    _select_preset_state = SelectPresetState::SELECT_OPTION;
}

//----------------------------------------------------------------------------
// _show_edit_name_select_char_screen
//----------------------------------------------------------------------------
void GuiManager::_show_edit_name_select_char_screen()
{
    // Show the name we are currently editing
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_EDIT_NAME;
    std::memset(msg.edit_name.name, 0, sizeof(msg.edit_name.name));
    _strcpy_to_gui_msg(msg.edit_name.name, _edit_name.c_str());
    _post_gui_msg(msg);

    // Are we entering the rename state?
    if (_edit_name_state == EditNameState::NONE)  {
        // Yes - set the left status to indicate this
        std::string status;
        if (_renaming_patch) {
            status = "RENAME PATCH";
        }
        else if (_select_preset_state == SelectPresetState::SELECT_PRESET) {
            status = "RENAME PRESET";
        }
        else {
            status = "RENAME BANK";
        }
        _set_left_status(status.c_str());

        // Reset the selected character/list character indexes
        _selected_char_index = (_edit_name.size() < EDIT_NAME_STR_LEN) ? _edit_name.size() : (EDIT_NAME_STR_LEN - 1);
        _selected_list_char = 0;
    }

    // Set the soft buttons text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "EDIT");
    if (!_renaming_patch && _select_preset_state == SelectPresetState::SELECT_PRESET) {
        _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "SAVE");
    }
    else {
        _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "ENTER");
    }
    _post_gui_msg(msg);

    // Enter the edit name - select character state
    _edit_name_state = EditNameState::SELECT_CHAR;

    // Configure the data knob
    _config_data_knob(EDIT_NAME_STR_LEN);
}

//----------------------------------------------------------------------------
// _show_edit_name_change_char_screen
//----------------------------------------------------------------------------
void GuiManager::_show_edit_name_change_char_screen()
{
    // Show the selected char
    if (_selected_char_index < _edit_name.size()) {
        // If not a valid char to show, show the default
        _selected_list_char = _edit_name[_selected_char_index];
        if (!_char_is_charset_valid(_selected_list_char)) {
            _selected_list_char = DEFAULT_CHARSET_CHAR;
        }
    }
    else {
        // Show the default char for new characters
        _selected_list_char = DEFAULT_CHARSET_CHAR;
    }

    // Get the index into the character ser
    _selected_list_char = _char_to_charset_index(_selected_list_char);

    // Show the change character control
    auto msg = GuiMsg();
    msg.type = GuiMsgType::EDIT_NAME_CHANGE_CHAR;
    msg.edit_name_change_char.change_char = _selected_list_char;                  
    _post_gui_msg(msg);

    // Enter the edit name - change character state
    _edit_name_state = EditNameState::CHANGE_CHAR;

    // Configure the data knob
    _config_data_knob(NUM_CHARSET_CHARS);
}

//----------------------------------------------------------------------------
// _show_param
//----------------------------------------------------------------------------
void GuiManager::_show_param()
{
    // Parse the GUI state
    switch (_gui_state) {
        case GuiState::HOME_SCREEN:
        case GuiState::SHOW_PARAM:
        case GuiState::SHOW_PARAM_SHORT:
        case GuiState::SHOW_MORPH_PARAM: {
            // Show the param as a standard param
            _show_standard_param();
            break;
        }

        case GuiState::MOD_MATRIX: {
            // Show the Mod Matrix para,
            _show_mod_matrix_param();
            break;
        }

        default: {
            break;
        } 
    }     
}

//----------------------------------------------------------------------------
// _show_standard_param
//----------------------------------------------------------------------------
void GuiManager::_show_standard_param()
{
    // Set the GUI state
    _set_standard_param_gui_state();

    // Create the param list
    if ((_params_list.size() == 0) || _new_param_list) {
        // Get the params list
        _params_list = _param_shown_root->param_list();
        
        // Special handling if the param root is the Layer 2 number of voices AND Layer 2
        // has no voices allocated (disabled)
        if (_param_shown_root == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES) &&
            (utils::get_layer_info(LayerId::D1).num_voices() == 0)) {
            // Just show the number of voices and no other params
            _params_list.resize(1);
            _param_shown_index = 0;
        }
    }

    // Parse the param list type
    switch (_param_shown->param_list_type()) {
        case ParamListType::NORMAL: {
            // Show the param as a normal param and list
            _show_normal_param();
            break;
        }

        case ParamListType::ADSR_ENVELOPE: {
            // If the param is a specific ADSR param, then we show this with the
            // visualiser - otherwise a normal param and list
            int _attack_param_shown_index = 1;
            if (utils::eg_state() == utils::EgState::AUX_EG_STATE) {
                _attack_param_shown_index++;
            }
            (_param_shown_index >= _attack_param_shown_index) && (_param_shown_index < (_attack_param_shown_index + 5)) ?
                _show_adsr_env_param() :
                _show_normal_param();
            break;
        }

        case ParamListType::VCF_CUTOFF: {
            // TODO: Just show this as a normal param for now, until the VCF visualiser
            // is sorted out
            _show_normal_param();
            // The first 2 params in the list must be the VCF Cutoff
            // Show these as VCF Cutoff params, otherwise a normal param and list
            //(_param_shown_index < 2) ?
            //    _show_vcf_cutoff_param() :
            //    _show_normal_param();
            break;
        }        
    }

    // Set the standard param soft buttons
    _set_standard_param_soft_buttons();

    // Are we showing a param list?
    if (_show_param_list) {
        // Configure the data knob
        if (_editing_param) {
            // If this is an enum param (not shown as a list)
            _param_shown->num_positions() > 0 ?
                _config_data_knob(_param_shown->num_positions()) :
                _config_data_knob(-1, _param_shown->value());
        }
        else {
            _config_data_knob(_params_list.size());
        }
    }
    else {
        // Show the loaded preset - we need to update this in case it has been modified (likely)
        _set_left_status(utils::system_config()->preset_id().preset_display_name().c_str(), utils::preset_modified());        
    }

    // Start the param change timer
    _start_param_change_timer();
}

//----------------------------------------------------------------------------
// _show_normal_param
//----------------------------------------------------------------------------
void GuiManager::_show_normal_param()
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_NORMAL_PARAM;
    msg.show_normal_param.screen_orientation = ScreenOrientation::LEFT_RIGHT;

    // Should we show this as a param list?
    if (_show_param_list) {
        // Create the param list
        msg.show_normal_param.num_items = _params_list.size();
        msg.show_normal_param.selected_item = _param_shown_index;
        uint i = 0;
        for (auto p : _params_list) {
            _strcpy_to_gui_msg(msg.show_normal_param.list_items[i], p->display_name());
            msg.show_normal_param.list_item_separator[i] = false; //p->separator;
            msg.show_normal_param.list_item_enabled[i++] = true;
        }

        // Set the param list name - append the Preset name if showing the MULTI list, and Patch name if showing the PRESET menu
        if ((_param_shown_root == utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES)) || (_param_shown_root == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES))) {
            _strcpy_to_gui_msg(msg.show_normal_param.name, (_param_shown_root->param_list_display_name() + ":" + utils::system_config()->preset_id().preset_display_name_short()).c_str());

            // Orient the screen RIGHT LEFT if we are showing the Layer 2 menu
            if (_param_shown_root == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES)) {
                msg.show_normal_param.screen_orientation = ScreenOrientation::RIGHT_LEFT;
            }
        }
        else if (_param_shown_root == utils::get_param(MoniqueModule::SYSTEM, SystemParamId::PATCH_NAME_PARAM_ID)) {
            _strcpy_to_gui_msg(msg.show_normal_param.name, (_param_shown_root->param_list_display_name() + ":" + utils::get_current_layer_info().patch_name()).c_str());
        }
        else {
            _strcpy_to_gui_msg(msg.show_normal_param.name, (_param_shown_root->param_list_display_name() + ":").c_str());
        }

        if (_show_patch_names) {
            _strcpy_to_gui_msg(msg.show_normal_param.l1_patch_name, utils::get_layer_info(LayerId::D0).patch_name().c_str());
            if (utils::get_layer_info(LayerId::D1).num_voices()) {
                _strcpy_to_gui_msg(msg.show_normal_param.l2_patch_name, utils::get_layer_info(LayerId::D1).patch_name().c_str());
            }
            else {
                _strcpy_to_gui_msg(msg.show_normal_param.l2_patch_name, "DISABLED");
            }
        }        
    }
    else {
        // No list items
        msg.show_normal_param.num_items = 0;

        // Set the param name
        _strcpy_to_gui_msg(msg.show_normal_param.name, _param_shown->display_name());
    }
    msg.show_normal_param.force_show_list = false;
    msg.show_normal_param.show_scope = _show_scope;

    // Check for the special case of Wavetable Select - in this case we show the actual filename, not
    // the value of the Wavetable Select float
    if (_show_param_as_wt_file_browser(_param_shown)) {
        // Show the actual filename
        auto filename_param = utils::get_wt_filename_param();
        if (filename_param) {        
            _strcpy_to_gui_msg(msg.show_normal_param.display_string, filename_param->str_value().c_str());
        }
        else {
           _strcpy_to_gui_msg(msg.show_normal_param.display_string, "Unknown file"); 
        }   
    }
    // Is this the special case of the patch Name param?
    else if ((_param_shown->module() == MoniqueModule::SYSTEM) && (_param_shown->param_id() == SystemParamId::PATCH_NAME_PARAM_ID)) {
        // Show the patch name
        _strcpy_to_gui_msg(msg.show_normal_param.display_string, utils::get_current_layer_info().patch_name().c_str());
    }
    else {
        // Get the value as a string to show
        auto str = _param_shown->display_string();
        str.first ?
            _strcpy_to_gui_msg(msg.show_normal_param.value_string, str.second.c_str()) :
            _strcpy_to_gui_msg(msg.show_normal_param.display_string, str.second.c_str());
    }

    // If there is a value tag, show it
    if (_param_shown->display_tag().size() > 0) {
        _strcpy_to_gui_msg(msg.show_normal_param.value_tag, _param_shown->display_tag().c_str());
    }  
    _post_gui_msg(msg); 
}

//----------------------------------------------------------------------------
// _show_adsr_env_param
//----------------------------------------------------------------------------
void GuiManager::_show_adsr_env_param()
{
    // Set the ADSR parameters
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_ADSR_ENV_PARAM;
    auto itr = _params_list.begin();
    itr++;
    if (utils::eg_state() == utils::EgState::AUX_EG_STATE) {
        itr++;
    }    
    msg.show_adsr_env_param.attack = (*itr++)->value();
    msg.show_adsr_env_param.decay = (*itr++)->value();
    msg.show_adsr_env_param.sustain = (*itr++)->value();
    msg.show_adsr_env_param.release = (*itr++)->value();
    msg.show_adsr_env_param.level = (*itr++)->value();

    //if (_param_shown_index < 4) {
    //    _param_shown_index = 0;
    //}
    // Should we show this as a param list?
    if (_show_param_list) {
        _strcpy_to_gui_msg(msg.show_adsr_env_param.name, (_param_shown->param_list_display_name() + ":").c_str());

        // Now copy the other ADSR envelope params to show
        msg.show_adsr_env_param.num_items = _params_list.size();
        msg.show_adsr_env_param.selected_item = _param_shown_index;           
        uint i = 0;
        for (auto p : _params_list) {
            _strcpy_to_gui_msg(msg.show_adsr_env_param.list_items[i], p->display_name());
            msg.show_adsr_env_param.list_item_separator[i] = false; // p->separator;
            msg.show_adsr_env_param.list_item_enabled[i++] = true;
        }
    }
    else {
        // No list items
        msg.show_adsr_env_param.num_items = 0;

        // Set the param name
        _strcpy_to_gui_msg(msg.show_adsr_env_param.name, _param_shown->display_name());       
    }

    // Get the value as a string to show
    auto str = _param_shown->display_string();
    str.first ?
        _strcpy_to_gui_msg(msg.show_adsr_env_param.value_string, str.second.c_str()) :
        _strcpy_to_gui_msg(msg.show_adsr_env_param.display_string, str.second.c_str());     
    
    // Post the GUI message
    _post_gui_msg(msg); 
}

//----------------------------------------------------------------------------
// _show_vcf_cutoff_param
//----------------------------------------------------------------------------
void GuiManager::_show_vcf_cutoff_param()
{
    // Set the VCF Cutoff parameters
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_VCF_CUTOFF_PARAM;
    _strcpy_to_gui_msg(msg.show_vcf_cutoff_param.name, (_param_shown->param_list_display_name() + ":").c_str());
    auto itr = _params_list.begin();
    msg.show_vcf_cutoff_param.hp_filter = (*itr++)->value();
    msg.show_vcf_cutoff_param.lp_filter = (*itr++)->value();

    //if (_param_shown_index < 4) {
    //    _param_shown_index = 0;
    //}
    // Should we show this as a param list?
    if (_show_param_list) {
        // Now copy the other VCF envelope params to show
        msg.show_vcf_cutoff_param.num_items = _params_list.size();
        msg.show_vcf_cutoff_param.selected_item = _param_shown_index;           
        uint i = 0;
        for (auto p : _params_list) {
            _strcpy_to_gui_msg(msg.show_vcf_cutoff_param.list_items[i], p->display_name());
            msg.show_vcf_cutoff_param.list_item_separator[i] = false; // p->separator;
            msg.show_vcf_cutoff_param.list_item_enabled[i++] = true;
        }
    }
    else {
        // No list items
        msg.show_vcf_cutoff_param.num_items = 0;

        // Set the param name
        _strcpy_to_gui_msg(msg.show_vcf_cutoff_param.name, _param_shown->display_name());        
    }

    // Get the value as a string to show
    auto str = _param_shown->display_string();
    str.first ?
        _strcpy_to_gui_msg(msg.show_vcf_cutoff_param.value_string, str.second.c_str()) :
        _strcpy_to_gui_msg(msg.show_vcf_cutoff_param.display_string, str.second.c_str()); 

    // Post the GUI message
    _post_gui_msg(msg); 
}

//----------------------------------------------------------------------------
// _show_mod_matrix_param
//----------------------------------------------------------------------------
void GuiManager::_show_mod_matrix_param()
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_NORMAL_PARAM;

    // Go through the param list for this mod matrix source
    uint num_items = 0;
    msg.show_normal_param.selected_item = 0;
    bool found = false;
    auto params_list_cpy = _params_list;
    _params_list.clear();
    for (auto p : _param_shown_root->param_list()) {
        // If we are showing a new list
        if (_new_mod_matrix_param_list) {          
            // If either:
            // - The value of the mod matrix entry is not disabled, or
            // - We should always show this parameter
            // Then add it to the display list
            if (_mod_matrix_param_enabled(p) || _showing_additional_mod_dst_params) {
                // Add it to the display list
                // If the value is disabled but we always show it, show it disabled in the list
                _strcpy_to_gui_msg(msg.show_normal_param.list_items[num_items], p->display_name());
                msg.show_normal_param.list_item_enabled[num_items] = _mod_matrix_param_enabled(p);
                _params_list.push_back(p);

                // If this param is the param to show
                if (p == _param_shown) {
                    // Indicate it is found and set the selected item - always show the selected
                    // item as enabled
                    found = true;
                    msg.show_normal_param.list_item_enabled[num_items] = true;
                    msg.show_normal_param.selected_item = num_items;
                }
                num_items++;
            }
        }
        else {
            // Not showing a new list, so we need to process the previous list first
            bool shown = false;
            for (const Param *sp : params_list_cpy) {
                // If the param is in the previous list
                if (p == sp) {
                    // Indicate this param is shown and add it to the display list
                    // If the value is disabled but we always show it, show it disabled in the list
                    shown = true;
                    _strcpy_to_gui_msg(msg.show_normal_param.list_items[num_items], p->display_name());
                    msg.show_normal_param.list_item_enabled[num_items] = _mod_matrix_param_enabled(p);
                    _params_list.push_back(p); 
                                        
                    // If this param is the param to show
                    if (p == _param_shown) {
                        // Indicate it is found and set the selected item - always show the selected
                        // item as enabled
                        found = true;
                        msg.show_normal_param.list_item_enabled[num_items] = true;
                        msg.show_normal_param.selected_item = num_items;
                    }
                    num_items++;
                    break;
                }
            }           
            // If this param did not already exist in the previous list
            if (!shown) {
                // If this is the param to show
                if (p == _param_shown) {
                    // Add it to the display list
                    // If the value is disabled but we always show it, show it disabled in the list           
                    _strcpy_to_gui_msg(msg.show_normal_param.list_items[num_items], p->display_name());
                    msg.show_normal_param.list_item_enabled[num_items] = _mod_matrix_param_enabled(p);
                    _params_list.push_back(p);                    

                    // Indicate it is found and set the selected item                       
                    found = true;
                    msg.show_normal_param.list_item_enabled[num_items] = true;
                    msg.show_normal_param.selected_item = num_items;
                    num_items++;
                }
                else {
                    // Should this param be in the list?
                    if (_mod_matrix_param_enabled(p)) {
                        // Add it to the display list
                        // If the value is disabled but we always show it, show it disabled in the list
                        _strcpy_to_gui_msg(msg.show_normal_param.list_items[num_items], p->display_name());
                        msg.show_normal_param.list_item_enabled[num_items] = _mod_matrix_param_enabled(p);
                        msg.show_normal_param.list_item_enabled[num_items] = true;
                        _params_list.push_back(p);
                        num_items++;
                    }                   
                }
            }
        }     
    }
    _new_mod_matrix_param_list = false;

    // If the param to show was not found but there are items in the list,
    // then just default to the first item in the list
    // We also force a list to be shown, even if empty
    if (!found && num_items) {
        _param_shown = _params_list.front();
    }
    msg.show_normal_param.num_items = num_items;
    _param_shown_index =  msg.show_normal_param.selected_item;
    msg.show_normal_param.force_show_list = true;

    // Set the parameter name
    _strcpy_to_gui_msg(msg.show_normal_param.name, (_param_shown->param_list_display_name() + ":").c_str());

    // Set the parameter value  
    auto str = _param_shown->display_string();
    if (str.first) {
        _strcpy_to_gui_msg(msg.show_normal_param.value_string, str.second.c_str());
    }
    else {
        _strcpy_to_gui_msg(msg.show_normal_param.display_string, str.second.c_str());
    }
    
    // If there is a value tag, show it
    if (_param_shown->display_tag().size() > 0) {
        _strcpy_to_gui_msg(msg.show_normal_param.value_tag, _param_shown->display_tag().c_str());
    }    
    _post_gui_msg(msg);

    // Set the soft button text
    msg = GuiMsg();
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _params_list.size() > 0 ?
        _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "EDIT") :
        _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "----");
    _showing_additional_mod_dst_params ?
        _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "BACK") :
        _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "MORE DESTS");
    _post_gui_msg(msg);

    // Configure the data knob
    _config_data_knob(_params_list.size());

    // Configure the data knob
    if (_params_list.size() > 0) {
        _config_data_knob( num_items);
        _config_soft_button_1(true);
    }
    else {
        _config_data_knob();
        _config_soft_button_1(false);
    }
}

//----------------------------------------------------------------------------
// _show_param_update
//----------------------------------------------------------------------------
void GuiManager::_show_param_update(bool select_list_item)
{
    // Parse the GUI state
    switch (_gui_state) {
        case GuiState::HOME_SCREEN:
        case GuiState::SHOW_PARAM:
        case GuiState::SHOW_PARAM_SHORT:
        case GuiState::SHOW_MORPH_PARAM: {
            // Show the param update for a standard param
            _show_standard_param_update(select_list_item);
            break;
        }

        case GuiState::MOD_MATRIX: {
            // Show the param update for a Mod Matrix param
            _show_mod_matrix_param_update(select_list_item);
            break;
        }

        default: {
            break;
        } 
    }
}

//----------------------------------------------------------------------------
// _show_standard_param_update
//----------------------------------------------------------------------------
void GuiManager::_show_standard_param_update(bool select_list_item)
{
    // Set the GUI state
    _set_standard_param_gui_state();

    // Parse the param list type
    switch (_param_shown->param_list_type()) {
        case ParamListType::NORMAL: {
            // Show the param update for a normal param
            _show_normal_param_update(select_list_item);
            break;
        }

        case ParamListType::ADSR_ENVELOPE: {
            // If the param is a specific ADSR param, then we show this with the
            // visualiser - otherwise a normal param and list
            int _attack_param_shown_index = 1;
            if (utils::eg_state() == utils::EgState::AUX_EG_STATE) {
                _attack_param_shown_index++;
            }
            (_param_shown_index >= _attack_param_shown_index) && (_param_shown_index < (_attack_param_shown_index + 5)) ?
                _show_adsr_env_param_update(select_list_item) :
                _show_normal_param_update(select_list_item);
            break;
        }

        case ParamListType::VCF_CUTOFF: {
            // TODO: Just show this as a normal param for now, until the VCF visualiser
            // is sorted out            
            _show_normal_param_update(select_list_item);
            // The first 2 params in the list must be the VCF Cutoff parameters
            // Show these as a VCF Cutoff param update, otherwise a normal param update
            //(_param_shown_index < 2) ?
            //    _show_vcf_cutoff_param_update(select_list_item) :
            //    _show_normal_param_update(select_list_item);
            break;
        }        
    }

    // Set the standard param soft buttons (if needed)
    if (!_show_param_list || select_list_item) {
        _set_standard_param_soft_buttons();
    }      
}

//----------------------------------------------------------------------------
// _show_normal_param_update
//----------------------------------------------------------------------------
void GuiManager::_show_normal_param_update(bool select_list_item)
{
    bool latched_edit = true;
    
    // Set the message type
    auto msg = GuiMsg();           
    msg.type = GuiMsgType::SHOW_NORMAL_PARAM_UPDATE;
    msg.show_normal_param_update.screen_orientation = ScreenOrientation::LEFT_RIGHT;

    // Set the param name
    _strcpy_to_gui_msg(msg.show_normal_param.name, _param_shown->display_name());

    // Check for the special case of Wavetable Select - in this case we show the actual filename, not
    // the value of the Wavetable Select float
    if (_show_param_as_wt_file_browser(_param_shown)) {
        // Show the actual filename
        auto filename_param = utils::get_wt_filename_param();
        if (filename_param) {        
            _strcpy_to_gui_msg(msg.show_normal_param_update.display_string, filename_param->str_value().c_str());
        }
        else {
            _strcpy_to_gui_msg(msg.show_normal_param_update.display_string, "Unknown file"); 
        }    
    }
    // Is this the special case of the patch Name param?
    else if ((_param_shown->module() == MoniqueModule::SYSTEM) && (_param_shown->param_id() == SystemParamId::PATCH_NAME_PARAM_ID)) {
        // Show the patch name
        _strcpy_to_gui_msg(msg.show_normal_param_update.display_string, utils::get_current_layer_info().patch_name().c_str());
        latched_edit = false;
    }    
    else {
        // Get the value as a string to show
        auto str = _param_shown->display_string();
        str.first ?
            _strcpy_to_gui_msg(msg.show_normal_param_update.value_string, str.second.c_str()) :
            _strcpy_to_gui_msg(msg.show_normal_param_update.display_string, str.second.c_str());

        // Orient the screen RIGHT LEFT if we are showing the Layer 2 menu
        if (_param_shown_root == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES)) {
            msg.show_normal_param_update.screen_orientation = ScreenOrientation::RIGHT_LEFT;
        }

        // Don't latch if this is a System Function
        if (_param_shown->type() == ParamType::SYSTEM_FUNC) {
            latched_edit = false;
        }     
    }

    // Is this the special case of the Layer Number Voices?
    if ((_param_shown == utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES)) || (_param_shown == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES))) {
        // We also need special handling if the Layer number of voices parameter is changed
        // Set the new number of voices, and process this as a full layer param update (updates
        // the list)
        // We also have to adjust the other layers number of voices, so all voices are always allocated
        if (_param_shown == utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES)) {
            // If the number of voices has actually changed
            if (utils::get_layer_info(LayerId::D0).num_voices() != _param_shown->hr_value()) {            
                uint leftover_voices = common::NUM_VOICES - _param_shown->hr_value();
                utils::get_layer_info(LayerId::D0).set_num_voices(_param_shown->hr_value());
                utils::get_layer_info(LayerId::D1).set_num_voices(leftover_voices);
                auto param = utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES);
                if (param) {
                    param->set_hr_value(leftover_voices);
                    auto param_change = ParamChange(param, module());
                    param_change.display = false;
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));                
                }
            }
        }
        else if (_param_shown == utils::get_param(utils::ParamRef::LAYER_2_NUM_VOICES)) {
            // If the number of voices has actually changed
            uint prev_num_voices = utils::get_layer_info(LayerId::D1).num_voices();
            if (utils::get_layer_info(LayerId::D1).num_voices() != _param_shown->hr_value()) {
                uint leftover_voices = common::NUM_VOICES - _param_shown->hr_value();
                utils::get_layer_info(LayerId::D0).set_num_voices(leftover_voices);
                utils::get_layer_info(LayerId::D1).set_num_voices(_param_shown->hr_value());
                auto param = utils::get_param(utils::ParamRef::LAYER_1_NUM_VOICES);
                if (param) {
                    param->set_hr_value(leftover_voices);
                    auto param_change = ParamChange(param, module());
                    param_change.display = false;
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));                
                }

                // If Layer 2 is now enabled
                if (prev_num_voices == 0) {
                    // Load layer 2
                    _layer_2_voices_changed = true;
                    utils::set_current_layer(LayerId::D1);
                    _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::LOAD_LAYER_2, MoniqueModule::GUI)));                    
                }
                // If Layer 2 is now disabled
                else if (_param_shown->hr_value() == 0) {
                    // Load layer 1
                    _layer_2_voices_changed = true;
                    utils::set_current_layer(LayerId::D0);
                    _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::LOAD_LAYER_1, MoniqueModule::GUI))); 
                }
            }
        }
        _set_layer_status();
        _show_normal_param();
        return;
    }
    else if (_param_shown == utils::get_param(utils::ParamRef::MIDI_CHANNEL_FILTER)) {
        // We also need special handling if the Layer MIDI Channel Filter parameter is changed
        utils::get_current_layer_info().set_midi_channel_filter(_param_shown->position_value());
    }

    // If there is a value tag, show it
    if (_param_shown->display_tag().size() > 0) {
        _strcpy_to_gui_msg(msg.show_normal_param_update.value_tag, _param_shown->display_tag().c_str());
    }

    // Select the list item if needed (-1 means leave the current selection as is)
    select_list_item ?
        msg.show_normal_param_update.selected_item = _param_shown_index :
        msg.show_normal_param_update.selected_item = -1;
    _post_gui_msg(msg);

    // Configure soft button 2
    if (_show_param_list && select_list_item) {
        // Configure soft button 2
        _config_soft_button_1(latched_edit);
    } 

}

//----------------------------------------------------------------------------
// _show_adsr_env_param_update
//----------------------------------------------------------------------------
void GuiManager::_show_adsr_env_param_update(bool select_list_item)
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_ADSR_ENV_PARAM_UPDATE;

    // Set the param name
    _strcpy_to_gui_msg(msg.show_adsr_env_param_update.name, _param_shown->display_name());

    // Get the value as a string to show
    auto str = _param_shown->display_string();
    str.first ?
        _strcpy_to_gui_msg(msg.show_adsr_env_param_update.value_string, str.second.c_str()) :
        _strcpy_to_gui_msg(msg.show_adsr_env_param_update.display_string, str.second.c_str());  

    // Set the ADSR parameters
    uint index = 1;
    if (utils::eg_state() == utils::EgState::AUX_EG_STATE) {
        index++;
    }    
    msg.show_adsr_env_param_update.attack = _params_list[index++]->value();
    msg.show_adsr_env_param_update.decay = _params_list[index++]->value();
    msg.show_adsr_env_param_update.sustain = _params_list[index++]->value();
    msg.show_adsr_env_param_update.release = _params_list[index++]->value();
    msg.show_adsr_env_param_update.level = _params_list[index]->value();

    // Select the list item if needed (-1 means leave the current selection as is)
    (select_list_item) ?
        msg.show_adsr_env_param_update.selected_item = _param_shown_index :
        msg.show_adsr_env_param_update.selected_item = -1;
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _show_vcf_cutoff_param_update
//----------------------------------------------------------------------------
void GuiManager::_show_vcf_cutoff_param_update(bool select_list_item)
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_VCF_CUTOFF_PARAM_UPDATE;

    // Set the param name
    _strcpy_to_gui_msg(msg.show_vcf_cutoff_param_update.name, _param_shown->display_name());

    // Get the value as a string to show
    auto str = _param_shown->display_string();
    str.first ?
        _strcpy_to_gui_msg(msg.show_vcf_cutoff_param_update.value_string, str.second.c_str()) :
        _strcpy_to_gui_msg(msg.show_vcf_cutoff_param_update.display_string, str.second.c_str());  

    // Set the VCF Cutoff parameters
    msg.show_vcf_cutoff_param_update.hp_filter = _params_list[0]->value();
    msg.show_vcf_cutoff_param_update.lp_filter = _params_list[1]->value();

    // Select the list item if needed (-1 means leave the current selection as is)
    (select_list_item) ?
        msg.show_vcf_cutoff_param_update.selected_item = _param_shown_index :
        msg.show_vcf_cutoff_param_update.selected_item = -1;
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _show_mod_matrix_param_update
//----------------------------------------------------------------------------
void GuiManager::_show_mod_matrix_param_update(bool select_list_item)
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_NORMAL_PARAM_UPDATE;

    // Set the parameter value
    auto str = _param_shown->display_string();
    if (str.first) {
        _strcpy_to_gui_msg(msg.show_normal_param_update.value_string, str.second.c_str());
    }
    else {
        _strcpy_to_gui_msg(msg.show_normal_param_update.display_string, str.second.c_str());
    }

    // If there is a value tag, show it
    if (_param_shown->display_tag().size() > 0) {
        _strcpy_to_gui_msg(msg.show_normal_param_update.value_tag, _param_shown->display_tag().c_str());
    }

    // Select the list item if needed (-1 means leave the current selection as is)
    (select_list_item) ?
        msg.show_normal_param_update.selected_item = _param_shown_index :
        msg.show_normal_param_update.selected_item = -1;
    _post_gui_msg(msg);    
}

//----------------------------------------------------------------------------
// _show_enum_param
//----------------------------------------------------------------------------
void GuiManager::_show_enum_param()
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_ENUM_PARAM_UPDATE;
    msg.enum_param_update.wt_list = false;

    // Get the number of positions and current value for this enum list param
    uint num_pos = 0;
    uint pos_value = 0;    
    //if (_param_shown->display_switch) {
    //    num_pos = 2;
    //    pos_value = (_param_shown->get_value() == 0.0) ? 0 : 1;
    //}
    //else {
        num_pos = _param_shown->num_positions();
        pos_value = _param_shown->position_value();
    //}

    // Set the parameter name
    auto param_name = std::string(_param_shown->display_name());
    _string_toupper(param_name);
    auto str = _param_shown_root->param_list_display_name() + ":" + param_name;
    _strcpy_to_gui_msg(msg.enum_param_update.name, str.c_str());

    // Set the param enum list and selected item
    msg.enum_param_update.num_items = num_pos;
    msg.enum_param_update.selected_item = pos_value;
    for (uint i=0; i<num_pos; i++) {
        _strcpy_to_gui_msg(msg.enum_param_update.list_items[i], _param_shown->position_string(i).c_str());
    }
    _post_gui_msg(msg);

    // Set the soft button text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _show_param_list ?
        _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "EDIT") :
        _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "----");
    _show_param_list ?
        _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "----") :
        _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "EXIT");
    _post_gui_msg(msg);

    // Reset the button 2 state if not showing a param list
    if (!_show_param_list) {
        _post_soft_button_state_update(false, SoftButtonId::BUTTON_2);
    }

    // Configure the data knob
    _config_data_knob(num_pos);
}

//----------------------------------------------------------------------------
// _show_enum_param_update
//----------------------------------------------------------------------------
void GuiManager::_show_enum_param_update(uint value)
{
    bool wt_list = false;

    // Update the parameter value
    if (_param_shown->num_positions() > 0) {
        // Set the param position value
        _param_shown->set_value_from_position(value);

        // Special case - if we are changing Wavetable
        if (_show_param_as_wt_file_browser(_param_shown)) {
            auto filename_param = utils::get_wt_filename_param();
            if (filename_param) {
                filename_param->set_str_value(_filenames[value]);
                auto param_change = ParamChange(filename_param, module());
                _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
            }
            wt_list = true; 
        }        
    }

    // Update the selected enum param value from the list
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_ENUM_PARAM_UPDATE_VALUE;
    msg.list_select_item.selected_item = value;
    msg.list_select_item.wt_list = wt_list;
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _show_wt_file_browser_param
//----------------------------------------------------------------------------
void GuiManager::_show_wt_file_browser_param()
{
    // This param will become the root param - get the index
    // of this param in the param list
    auto filename_param = utils::get_wt_filename_param();
    if (filename_param) {
        // Parse the wavetable folder and get the number of wavetables
        _filenames = _parse_wavetable_folder();

        // Set the number of positions for this param
        _param_shown->set_position_param(_filenames.size());

        // Set the message type
        auto msg = GuiMsg();
        msg.type = GuiMsgType::SHOW_ENUM_PARAM_UPDATE;
        msg.enum_param_update.wt_list = true;

        // Set the parameter name
        uint selected_item = 0;
        uint num_items = _filenames.size();
        auto param_name = std::string(_param_shown->display_name());
        _string_toupper(param_name);
        auto str = _param_shown->param_list_display_name() + ":" + param_name;
        _strcpy_to_gui_msg(msg.enum_param_update.name, str.c_str());

        // Set the param enum list and selected item
        auto filename_param = utils::get_wt_filename_param();
        for (uint i=0; i<num_items; i++) {
            _strcpy_to_gui_msg(msg.enum_param_update.list_items[i], _filenames[i].c_str());
            if (filename_param && (_filenames[i] == filename_param->str_value())) {
                selected_item = i;
            }
        }
        msg.enum_param_update.num_items = num_items;
        msg.enum_param_update.selected_item = selected_item;
        _post_gui_msg(msg);

        // Set the soft button text
        msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
        _show_param_list ?
            _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "EDIT") :
            _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "----");
        _show_param_list ?
            _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "----") :
            _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "EXIT");
        _post_gui_msg(msg);

        // Reset the button 2 state if not showing a param list
        if (!_show_param_list) {
            _post_soft_button_state_update(false, SoftButtonId::BUTTON_2);
        }

        // Configure the data knob
        _config_data_knob(num_items);
    }
}

//----------------------------------------------------------------------------
// _show_reset_seq_screen
//----------------------------------------------------------------------------
void GuiManager::_show_reset_seq_screen()
{
    // Show the reset Sequencer screen
    auto msg = GuiMsg();
    _set_left_status("SEQ: RESET");
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Reset");
    std::strcpy(msg.msg_box.line_2, "SEQUENCER?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "OK");
    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _showing_reset_conf_screen = true;
}

//----------------------------------------------------------------------------
// _hide_reset_seq_screen
//----------------------------------------------------------------------------
void GuiManager::_hide_reset_seq_screen()
{
    // Hide the reset Sequencer screen
    _hide_msg_box();
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "ENTER");
    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "EXIT");
    _post_gui_msg(msg);                        
    _showing_reset_conf_screen = false;
}

//----------------------------------------------------------------------------
// _show_system_menu_screen
//----------------------------------------------------------------------------
void GuiManager::_show_system_menu_screen()
{
    auto msg = GuiMsg();
    auto first_item_index = static_cast<int>(SystemMenuOption::WHEELS_CALIBRATION);

    // Adjust the selected item for extended or normal system menu
    if (!_show_ext_system_menu) {
        first_item_index += SystemMenuOption::GLOBAL_SETTINGS;
    }

    // Setup the system list
    uint index = 0;
    _list_items.clear();
    for (uint i=first_item_index; i<=SystemMenuOption::ABOUT; i++) {
        // If this entry is for store demo mode, add the demo mode status to the menu item
        if (i == SystemMenuOption::STORE_DEMO_MODE) {
            _list_items[index++] = std::string(_system_menu_options[i]) + (utils::system_config()->get_demo_mode() ? "ON" : "OFF");
        }
        else {
            _list_items[index++] = _system_menu_options[i];
        }
    }

    // Set the left status
    _set_left_status("SYSTEM");

    // Show the system list
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    msg.list_items.num_items = _list_items.size();
    msg.list_items.selected_item = _selected_system_menu_item;
    msg.list_items.process_enabled_state = true;
    for (uint i=0; i<_list_items.size(); i++) {
        _strcpy_to_gui_msg(msg.list_items.list_items[i], _list_items[i].c_str());
        if (((i + first_item_index) == SystemMenuOption::BACKUP) && !_sw_manager->msd_mounted()) {
            msg.list_items.list_item_enabled[i] = false;
        }
        else if (((i + first_item_index) == SystemMenuOption::RESTORE_BACKUP) && !_sw_manager->restore_backup_archives_present()) {
            msg.list_items.list_item_enabled[i] = false;
        }
        else if (((i + first_item_index) == SystemMenuOption::RUN_DIAG_SCRIPT) && !_sw_manager->diag_script_present()) {
            msg.list_items.list_item_enabled[i] = false;
        }
        else {
            msg.list_items.list_item_enabled[i] = true;
        }
    }
    _post_gui_msg(msg);

    // Set the soft button text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "ENTER");
    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "EXIT");
    _post_gui_msg(msg);

    // Set the GUI state
    _gui_state = GuiState::SYSTEM_MENU;
    _system_menu_state = SystemMenuState::SHOW_OPTIONS;
    _progress_state = ProgressState::NOT_STARTED;

    // Set the data knob to the list selector state
    //_num_list_items = _list_items.size();
    _config_data_knob(_list_items.size());

    // Create a thread to poll for MSD insertion events
    if (!_msd_event_thread) {
        _run_msd_event_thread = true;
        _msd_event_thread = new std::thread(_process_msd_event, this);
    }
}

//----------------------------------------------------------------------------
// _show_calibration_status_screen
//----------------------------------------------------------------------------
void GuiManager::_show_calibration_status_screen(bool run_qa_check, int pitch_val, uint mod_val, uint at_val)
{
    std::string status = "-1";

    // Run the QA check script - only ONCE when the screen is first shown
    if (run_qa_check) {
        // Get the Mix VCA calibration status
        _mix_vca_cal_status_ok = false;
        if (std::filesystem::exists(MONIQUE_CALIBRATION_FILE(MIX_VCA_CAL_STATUS_FILENAME))) {
            // Open the file and read the status
            std::ifstream file(MONIQUE_CALIBRATION_FILE(MIX_VCA_CAL_STATUS_FILENAME));
            std::getline(file, status);
            if (status == "0") {
                _mix_vca_cal_status_ok = true;
            }
            file.close();
        }

        // Get the Filter calibration status
        _filter_cal_status_ok = false;
        if (std::filesystem::exists(MONIQUE_CALIBRATION_FILE(FILTER_CAL_STATUS_FILENAME))) {
            // Open the file and read the status
            std::ifstream file(MONIQUE_CALIBRATION_FILE(FILTER_CAL_STATUS_FILENAME));
            std::getline(file, status);
            if (status == "0") {
                _filter_cal_status_ok = true;
            }
            file.close();
        }

        // Get the Wheels calibration status
        _wheels_cal_status_ok = false; 
        if (std::filesystem::exists(MONIQUE_CALIBRATION_FILE(WHEELS_CAL_STATUS_FILENAME))) {
            // Open the file and read the status
            std::ifstream file(MONIQUE_CALIBRATION_FILE(WHEELS_CAL_STATUS_FILENAME));
            std::getline(file, status);
            if (status == "0") {
                _wheels_cal_status_ok = true;
            }
            file.close();
        }        

        // Run the QA check
        _qa_check_ok = static_cast<SwManager *>(utils::get_manager(MoniqueModule::SOFTWARE))->run_qa_check() == 0;
        MSG("QA check script: " << (_qa_check_ok ? "COMPLETE" : "FAILED"));
        if (_qa_check_ok == 0) {
            MONIQUE_LOG_INFO(module(), "QA check script: COMPLETE");
        }
        else {
            MONIQUE_LOG_ERROR(module(), "QA check script: FAILED");
        }

        // We also reset the global settings if all tests pass
        if (_mix_vca_cal_status_ok && _filter_cal_status_ok && _wheels_cal_status_ok && _qa_check_ok) {
            // Reset the global params
            _reset_global_params();        
        }        
    }

    // Show a confirmation popup
    auto msg = GuiMsg();
    _set_left_status("SYSTEM");   
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    auto str1 = std::string("Cal Mix VCA, Filter:") + (_mix_vca_cal_status_ok ? " OK, " : " NG, ") + (_filter_cal_status_ok ? "OK" : "NG");
    auto str2 = std::string("L: ") + std::to_string(pitch_val) + ", R: " + std::to_string(mod_val) + ", AT: " + std::to_string(at_val);
    auto str3 = std::string("System and Presets:") + (_qa_check_ok ? " OK" : " NG");
    std::strcpy(msg.msg_box.line_1, str1.c_str());
    std::strcpy(msg.msg_box.line_2, str2.c_str());
    std::strcpy(msg.msg_box.line_3, str3.c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "OK");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::QA_STATUS;
}

//----------------------------------------------------------------------------
// _show_sys_menu_calibrate_screen
//----------------------------------------------------------------------------
void GuiManager::_show_calibrate_screen(CalMode mode)
{
    std::string run_cal_str1;
    std::string run_cal_str2;
    std::string cal_type_str;

    // Set the calibration strings
    switch (mode) {
        case CalMode::FILTER:
            run_cal_str1 = "Running Filter calibration";
            run_cal_str2 = "Please wait (long process)";
            cal_type_str = "Filter calibration";
            break;

        case CalMode::MIX_VCA:
        default:
            run_cal_str1 = "Running Mix VCA calibration";
            run_cal_str2 = "Please wait (long process)";
            cal_type_str = "Mix VCA calibration";
            break;
    }

    // Show a confirmation popup
    auto msg = GuiMsg();
    _set_left_status("SYSTEM");           
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    std::strcpy(msg.msg_box.line_1, run_cal_str1.c_str());
    std::strcpy(msg.msg_box.line_2, run_cal_str2.c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::CALIBRATE;
    _calibrate_state = CalibrateState::CALIBRATE_STARTED;

    // Run the calibration script
    auto ret = static_cast<SwManager *>(utils::get_manager(MoniqueModule::SOFTWARE))->run_calibration_script(mode);
    MSG(cal_type_str + " script: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        MONIQUE_LOG_INFO(module(), cal_type_str + " script: COMPLETE");
    }
    else {
        MONIQUE_LOG_ERROR(module(), cal_type_str + " script: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, cal_type_str.c_str());
    std::strcpy(msg.msg_box.line_2, ((ret == 0) ? " Complete" : " FAILED"));
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "OK");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::CALIBRATE;
    _calibrate_state = CalibrateState::CALIBRATE_FINISHED;
}

//----------------------------------------------------------------------------
// _show_factory_soak_test_screen
//----------------------------------------------------------------------------
void GuiManager::_show_factory_soak_test_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    _set_left_status("SYSTEM");           
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    std::strcpy(msg.msg_box.line_1, "Running Factory soak test");
    std::strcpy(msg.msg_box.line_2, "Please wait (long process)");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::CALIBRATE;
    _calibrate_state = CalibrateState::CALIBRATE_STARTED;
    _running_background_test = true;

    // Run the soak test script
    auto ret = static_cast<SwManager *>(utils::get_manager(MoniqueModule::SOFTWARE))->run_factory_soak_test();
    MSG("Factory soak test script: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        MONIQUE_LOG_INFO(module(), "Factory soak test script: COMPLETE");
    }
    else {
        MONIQUE_LOG_ERROR(module(), "Factory soak test script: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Factory soak test");
    std::strcpy(msg.msg_box.line_2, ((ret == 0) ? " Complete" : " FAILED"));
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "OK");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::CALIBRATE;
    _calibrate_state = CalibrateState::CALIBRATE_FINISHED;
}

//----------------------------------------------------------------------------
// _show_pitch_bend_wheel_top_calibration_screen
//----------------------------------------------------------------------------
void GuiManager::_show_pitch_bend_wheel_top_calibration_screen()
{
    // Set the left status
    _set_left_status("CALIBRATION");

    // Show  message box
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Set PITCH WHEEL top");
    std::strcpy(msg.msg_box.line_2, "Press OK");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "OK");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _wheels_calibrate_state = WheelsCalibrateState::PITCH_BEND_WHEEL_TOP_CALIBRATE; 
}

//----------------------------------------------------------------------------
// _show_pitch_bend_wheel_mid_calibration_screen
//----------------------------------------------------------------------------
void GuiManager::_show_pitch_bend_wheel_mid_calibration_screen(bool first_cal)
{
    // Show  message box
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Set PITCH WHEEL middle");
    std::strcpy(msg.msg_box.line_2, "Press OK");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "OK");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _wheels_calibrate_state = first_cal? WheelsCalibrateState::PITCH_BEND_WHEEL_MID_CALIBRATE_1 :  WheelsCalibrateState::PITCH_BEND_WHEEL_MID_CALIBRATE_2;
}

//----------------------------------------------------------------------------
// _show_pitch_bend_wheel_bottom_calibration_screen
//----------------------------------------------------------------------------
void GuiManager::_show_pitch_bend_wheel_bottom_calibration_screen()
{
    // Set the left status
    _set_left_status("CALIBRATION");

    // Show  message box
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Set PITCH WHEEL bottom");
    std::strcpy(msg.msg_box.line_2, "Press OK");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "OK");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _wheels_calibrate_state = WheelsCalibrateState::PITCH_BEND_WHEEL_BOTTOM_CALIBRATE;    
}

//----------------------------------------------------------------------------
// _show_pitch_bend_wheel_top_calibration_screen
//----------------------------------------------------------------------------
void GuiManager::_show_pitch_bend_wheel_top_check_screen(int val)
{
    // Get the check status
    _wheel_check_val_ok = val == MIDI_PITCH_BEND_MAX_VALUE;

    // Set the left status
    _set_left_status("CALIBRATION");

    // Show  message box
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Check PITCH WHEEL top");
    std::strcpy(msg.msg_box.line_2, (std::to_string(val) + (_wheel_check_val_ok ? " OK" : " NG")).c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _wheel_check_val_ok ?
        std::strcpy(msg.soft_buttons_text.button1_text, "OK") :
        std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "EXIT");
    _post_gui_msg(msg);
    _wheels_calibrate_state = WheelsCalibrateState::PITCH_BEND_WHEEL_TOP_CHECK;
}

//----------------------------------------------------------------------------
// _show_pitch_bend_wheel_mid_calibration_screen
//----------------------------------------------------------------------------
void GuiManager::_show_pitch_bend_wheel_mid_check_screen(int val)
{
    // Get the check status
    _wheel_check_val_ok = val == MIDI_PITCH_BEND_MID_VALUE;

    // Set the left status
    _set_left_status("CALIBRATION");

    // Show  message box
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Check PITCH WHEEL middle");
    std::strcpy(msg.msg_box.line_2, (std::to_string(val) + (_wheel_check_val_ok ? " OK" : " NG")).c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _wheel_check_val_ok ?
        std::strcpy(msg.soft_buttons_text.button1_text, "OK") :
        std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "EXIT");
    _post_gui_msg(msg);
    _wheels_calibrate_state = WheelsCalibrateState::PITCH_BEND_WHEEL_MID_CHECK;
}

//----------------------------------------------------------------------------
// _show_pitch_bend_wheel_bottom_calibration_screen
//----------------------------------------------------------------------------
void GuiManager::_show_pitch_bend_wheel_bottom_check_screen(int val)
{
    // Get the check status
    _wheel_check_val_ok = val == MIDI_PITCH_BEND_MIN_VALUE;
    
    // Set the left status
    _set_left_status("CALIBRATION");

    // Show  message box
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Check PITCH WHEEL bottom");
    std::strcpy(msg.msg_box.line_2, (std::to_string(val) + (_wheel_check_val_ok ? " OK" : " NG")).c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _wheel_check_val_ok ?
        std::strcpy(msg.soft_buttons_text.button1_text, "OK") :
        std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "EXIT");
    _post_gui_msg(msg);
    _wheels_calibrate_state = WheelsCalibrateState::PITCH_BEND_WHEEL_BOTTOM_CHECK; 
}

//----------------------------------------------------------------------------
// _show_mod_wheel_top_calibration_screen
//----------------------------------------------------------------------------
void GuiManager::_show_mod_wheel_top_calibration_screen()
{
    // Set the left status
    _set_left_status("CALIBRATION");

    // Show  message box
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Set MOD WHEEL top");
    std::strcpy(msg.msg_box.line_2, "Press OK");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "OK");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _wheels_calibrate_state = WheelsCalibrateState::MOD_WHEEL_TOP_CALIBRATE;   
}

//----------------------------------------------------------------------------
// _show_mod_wheel_bottom_calibration_screen
//----------------------------------------------------------------------------
void GuiManager::_show_mod_wheel_bottom_calibration_screen()
{
    // Set the left status
    _set_left_status("CALIBRATION");

    // Show  message box
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Set MOD WHEEL bottom");
    std::strcpy(msg.msg_box.line_2, "Press OK");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "OK");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _wheels_calibrate_state = WheelsCalibrateState::MOD_WHEEL_BOTTOM_CALIBRATE;    
}

//----------------------------------------------------------------------------
// _show_mod_wheel_top_check_screen
//----------------------------------------------------------------------------
void GuiManager::_show_mod_wheel_top_check_screen(uint val)
{
    // Get the check status
    _wheel_check_val_ok = val == MIDI_CC_MAX_VALUE;
    
    // Set the left status
    _set_left_status("CALIBRATION");

    // Show  message box
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Check MOD WHEEL top");
    std::strcpy(msg.msg_box.line_2, (std::to_string(val) + (_wheel_check_val_ok ? " OK" : " NG")).c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _wheel_check_val_ok ?
        std::strcpy(msg.soft_buttons_text.button1_text, "OK") :
        std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "EXIT");
    _post_gui_msg(msg);
    _wheels_calibrate_state = WheelsCalibrateState::MOD_WHEEL_TOP_CHECK;
}

//----------------------------------------------------------------------------
// _show_mod_wheel_bottom_check_screen
//----------------------------------------------------------------------------
void GuiManager::_show_mod_wheel_bottom_check_screen(uint val)
{
    // Get the check status
    _wheel_check_val_ok = val == MIDI_CC_MIN_VALUE;
 
    // Set the left status
    _set_left_status("CALIBRATION");

    // Show  message box
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Check MOD WHEEL bottom");
    std::strcpy(msg.msg_box.line_2, (std::to_string(val) + (_wheel_check_val_ok ? " OK" : " NG")).c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _wheel_check_val_ok ?
        std::strcpy(msg.soft_buttons_text.button1_text, "OK") :
        std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "EXIT");
    _post_gui_msg(msg);
    _wheels_calibrate_state = WheelsCalibrateState::MOD_WHEEL_BOTTOM_CHECK;
}

//----------------------------------------------------------------------------
// _show_motor_test_screen
//----------------------------------------------------------------------------
void GuiManager::_show_motor_test_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    _set_left_status("SYSTEM");           
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    std::strcpy(msg.msg_box.line_1, "Running Motor/LED test");
    std::strcpy(msg.msg_box.line_2, "Press OK to stop");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "OK");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::CALIBRATE;
    _calibrate_state = CalibrateState::CALIBRATE_FINISHED;
    _running_background_test = true;

    // Run the motor test script
    auto ret = static_cast<SwManager *>(utils::get_manager(MoniqueModule::SOFTWARE))->run_motor_test();
    MSG("Motor test script: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        MONIQUE_LOG_INFO(module(), "Motor test script: COMPLETE");
    }
    else {
        MONIQUE_LOG_ERROR(module(), "Motor test script: FAILED");
    }
}

//----------------------------------------------------------------------------
// _show_select_diag_script_screen
//----------------------------------------------------------------------------
void GuiManager::_show_select_diag_script_screen()
{
    uint item = 0;

    // Set the left status
    _set_left_status("SELECT DIAG SCRIPT");

    // Get a list of the available diag scripts and show them
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    msg.list_items.process_enabled_state = false;
    _list_items.clear();    
    auto scripts = _sw_manager->get_diag_scripts();
    for (auto itr=scripts.begin(); itr<scripts.end(); itr++) {
        _list_items[item] = *itr;
        _strcpy_to_gui_msg(msg.list_items.list_items[item++], itr->c_str());
    }
    msg.list_items.num_items = item;
    msg.list_items.selected_item = _selected_diag_script; 
    _post_gui_msg(msg);

    // Set the soft buttons
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "RUN");
    std::strcpy(msg.soft_buttons_text.button2_text, "EXIT");
    _post_gui_msg(msg);
    _gui_state = GuiState::RUN_DIAG_SCRIPT;
    _run_diag_script_state = RunDiagScriptState::SELECT_DIAG_SCRIPT;

    // Configure the data knob
    _num_list_items = item;
    _config_data_knob(_num_list_items);      
}

//----------------------------------------------------------------------------
// _show_sys_menu_run_diag_script_confirm_screen
//----------------------------------------------------------------------------
void GuiManager::_show_run_diag_script_confirm_screen()
{
    // Set the left status
    _set_left_status("RUN DIAG SCRIPT");

    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Are you sure this script");
    std::strcpy(msg.msg_box.line_2, "is from a trusted source?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "YES");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _config_data_knob(-1, -1);
    _run_diag_script_state = RunDiagScriptState::CONFIRM_DIAG_SCRIPT;
}

//----------------------------------------------------------------------------
// _show_run_diag_script_screen
//----------------------------------------------------------------------------
void GuiManager::_show_run_diag_script_screen()
{
    // Set the left status
    _set_left_status("RUN DIAG SCRIPT");

    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    std::strcpy(msg.msg_box.line_1, "Running diagnostic script");
    std::strcpy(msg.msg_box.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);

    // Run the diagnositics script
    auto script = _list_items[_selected_diag_script];
    MONIQUE_LOG_INFO(module(), "Running diagnostic script: {}", script);
    auto ret = _sw_manager->run_diag_script(script.c_str());
    ::sync();
    MSG("Diagnostic script: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        MONIQUE_LOG_INFO(module(), "Diagnostic script: {}: COMPLETE", script);
    }
    else {
        MONIQUE_LOG_INFO(module(), "Diagnostic script: {} : FAILED", script);
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::string line1 = std::string("Diagnostic script") + ((ret == 0) ? " Complete" : " FAILED");
    std::string line2 = "";
    if (ret == 0) {
        // Get the script result message (if any)
        auto msg = _sw_manager->get_diag_script_result_msg(script.c_str());
        if (msg.size() == 2) {
            line1 = msg[0];
            line2 = msg[1];
        }
        else if (msg.size() == 1) {
            line1 = msg[0];
        }
    }
    std::strcpy(msg.msg_box.line_1, line1.c_str());
    std::strcpy(msg.msg_box.line_2, line2.c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _run_diag_script_state = RunDiagScriptState::RUN_DIAG_SCRIPT;
    _progress_state = ProgressState::FINISHED;
}

//----------------------------------------------------------------------------
// _show_global_settings_screen
//----------------------------------------------------------------------------
void GuiManager::_show_global_settings_screen()
{
    // Get the first global settings param
    auto param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::MIDI_CLK_IN_PARAM_ID);
    if (param) {
        // The param will become the root param - get the index
        // of this param in the param list
        auto index = _get_param_list_index(param, param);
        if (index >= 0) {
            // Reset the GUI state                  
            _reset_gui_state();
            _gui_state = GuiState::SHOW_PARAM;

            // Soft button 2 must be configured as EDIT mode
            _config_soft_button_1(true);

            // Setup the param shown settings
            _param_shown_root = param;
            _param_shown = param;
            _param_shown_index = index;
            _show_param_list = true;

            // Show the param
            _show_scope = false;
            _show_param();
        }
    }
}

//----------------------------------------------------------------------------
// _show_reset_to_factory_screen
//----------------------------------------------------------------------------
void GuiManager::_show_reset_to_factory_screen()
{
    // Show the reset to factory screen
    auto msg = GuiMsg();
    _set_left_status("GLOBAL SETTINGS: RESET");
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Reset all settings to");
    std::strcpy(msg.msg_box.line_2, "Factory defaults?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "OK");
    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _showing_reset_conf_screen = true;
}

//----------------------------------------------------------------------------
// _hide_reset_to_factory_screen
//----------------------------------------------------------------------------
void GuiManager::_hide_reset_to_factory_screen()
{
    // Hide the reset to factory screen
    _hide_msg_box();
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "ENTER");
    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "EXIT");
    _post_gui_msg(msg);                        
    _showing_reset_conf_screen = false;
}


//----------------------------------------------------------------------------
// _show_bank_management_screen
//----------------------------------------------------------------------------
void GuiManager::_show_bank_management_screen()
{
    uint item = 0;

    // Set the left status
    _set_left_status("BANKS");

    // Set the list of Wavetable Management options
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    msg.list_items.process_enabled_state = true;
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Import Bank");
    msg.list_items.list_item_enabled[item++] = _sw_manager->bank_archive_present();
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Export Bank");
    msg.list_items.list_item_enabled[item++] = _sw_manager->msd_mounted();
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Add Bank");
     msg.list_items.list_item_enabled[item++] = true;    
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Clear Bank");
     msg.list_items.list_item_enabled[item++] = true;
    msg.list_items.num_items = item;
    msg.list_items.selected_item = _selected_bank_management_item;    
    _post_gui_msg(msg);

    // Set the soft buttons
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "ENTER");
    std::strcpy(msg.soft_buttons_text.button2_text, "EXIT");
    _post_gui_msg(msg);
    _gui_state = GuiState::BANK_MANAGMENT;
    _bank_management_state = BankManagmentState::SHOW_LIST;
    _progress_state = ProgressState::NOT_STARTED;

    // Configure the data knob
    _num_list_items = item;
    _config_data_knob(_num_list_items);    
}

//----------------------------------------------------------------------------
// _show_select_bank_archive_screen
//----------------------------------------------------------------------------
void GuiManager::_show_select_bank_archive_screen()
{
    uint item = 0;

    // Set the left status
    _set_left_status("SELECT IMPORT ARCHIVE");

    // Get a list of the available bank archives and show them
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    msg.list_items.process_enabled_state = false;
    _list_items.clear();    
    auto banks = _sw_manager->get_bank_archives();
    for (auto itr=banks.begin(); itr<banks.end(); itr++) {
        _list_items[item] = *itr;
        _strcpy_to_gui_msg(msg.list_items.list_items[item++], itr->c_str());
    }
    msg.list_items.num_items = item;
    msg.list_items.selected_item = _selected_bank_archive;
    _selected_bank_archive_name = _list_items[_selected_bank_archive];
    _post_gui_msg(msg);

    // Set the soft buttons
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "NEXT");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _bank_management_state = BankManagmentState::IMPORT;
    _import_bank_state = ImportBankState::SELECT_ARCHIVE;

    // Configure the data knob
    _num_list_items = item;
    _config_data_knob(_num_list_items);      
}

//----------------------------------------------------------------------------
// _show_select_dest_bank_screen
//----------------------------------------------------------------------------
void GuiManager::_show_select_dest_bank_screen()
{
    // Set the left status
    std::string status;
    if (_bank_management_state == BankManagmentState::IMPORT) {
        status = "SELECT DEST BANK";
    }
    else {
        status = "SELECT BANK";
    }
    _set_left_status(status.c_str());

    // Parse the presets folder containing the banks
    _list_items = _parse_presets_folder();
    if (_list_items.size() > 0) {
        uint list_size = (_list_items.size() < LIST_MAX_LEN) ? _list_items.size() : LIST_MAX_LEN;

        // Show the list of banks to choose from
        auto msg = GuiMsg();
        msg.type = GuiMsgType::SHOW_LIST_ITEMS;
        msg.list_items.num_items = list_size;
        msg.list_items.selected_item = _selected_bank_dest;
        msg.list_items.process_enabled_state = false;
        auto list_itr = _list_items.begin();
        for (uint i=0; i<list_size; i++) {
            _strcpy_to_gui_msg(msg.list_items.list_items[i], _format_folder_name((*list_itr).second.c_str()).c_str());
            list_itr++;
        }
        _post_gui_msg(msg);

        // Set the soft buttons
        msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
        if (_bank_management_state == BankManagmentState::IMPORT) {
            std::strcpy(msg.soft_buttons_text.button1_text, "NEXT");
            _import_bank_state = ImportBankState::SELECT_DEST;
        }
        else if (_bank_management_state == BankManagmentState::EXPORT) {
            std::strcpy(msg.soft_buttons_text.button1_text, "EXPORT");
            _export_bank_state = ExportBankState::SELECT_BANK;
        }
        else {
            std::strcpy(msg.soft_buttons_text.button1_text, "CLEAR");
            _clear_bank_state = ClearBankState::SELECT_BANK;
        }
        std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
        _post_gui_msg(msg);
        _selected_bank_dest_name = _list_item_from_index(_selected_bank_dest).second;

        // Configure the data knob
        _num_list_items = list_size;
        _config_data_knob(_num_list_items);
    }    
}

//----------------------------------------------------------------------------
// _show_bank_import_method_screen
//----------------------------------------------------------------------------
void GuiManager::_show_bank_import_method_screen()
{
    // Set the left status
    _set_left_status("IMPORT BANK");
    auto msg = GuiMsg();           
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Merge or overwrite existing");
    std::strcpy(msg.msg_box.line_2, "preset files?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "MERGE");
    std::strcpy(msg.soft_buttons_text.button2_text, "OVERWITE");
    _post_gui_msg(msg);
    _import_bank_state = ImportBankState::IMPORT_METHOD;
}

//----------------------------------------------------------------------------
// _show_import_bank_screen
//----------------------------------------------------------------------------
void GuiManager::_show_import_bank_screen(bool merge)
{
    std::string str1;
    std::string str2;
    int ret = 0;

    // Show a confirmation popup
    _set_left_status("IMPORT BANK");
    auto msg = GuiMsg();          
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    str1 = std::string("Importing bank ") + (merge ? "(merge)" : "(overwrite)");
    std::strcpy(msg.msg_box.line_1, str1.c_str());
    std::strcpy(msg.msg_box.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _progress_state = ProgressState::NOT_STARTED;

    // If we are merging, run the bank import check merge script first
    if (merge) {
        // Run the bank import merge check script
        ret = _sw_manager->run_bank_import_merge_check_script(_selected_bank_archive_name.c_str(), _selected_bank_dest_name.c_str());
        MSG("Bank import merge check: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
        if (ret == 0) {
            MONIQUE_LOG_INFO(module(), "Bank import merge check: COMPLETE");
        }
        else {
            // Merge check failed
            str1 = "Merge cannot be performed,";
            str2 = "not enough free patches";
            _progress_state = ProgressState::FAILED;            
            MONIQUE_LOG_INFO(module(), "Bank import merge check: FAILED");
        }
    }

    // If all is well, run the bank import script
    if (ret == 0) {
        // Run the bank import script
        ret = _sw_manager->run_bank_import_script(_selected_bank_archive_name.c_str(), _selected_bank_dest_name.c_str(), merge);
        MSG("Bank import: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
        if (ret == 0) {
            MONIQUE_LOG_INFO(module(), "Bank import: COMPLETE");
        }
        else {
            MONIQUE_LOG_INFO(module(), "Bank import: FAILED");
        }
        str1 = std::string("Bank import") + ((ret == 0) ? " Complete" : " FAILED");
        str2 = "Eject USB Drive?";
        _progress_state = ProgressState::FINISHED;
    }

    // Check if the current preset still valid
    _check_if_preset_id_still_valid();

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, str1.c_str());
    std::strcpy(msg.msg_box.line_2, str2.c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    if (_progress_state == ProgressState::FINISHED) {
        std::strcpy(msg.soft_buttons_text.button1_text, "EJECT");
    }
    else {
        std::strcpy(msg.soft_buttons_text.button1_text, "----");
    }
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _show_export_bank_screen
//----------------------------------------------------------------------------
void GuiManager::_show_export_bank_screen()
{
    // Show a confirmation popup
    _set_left_status("EXPORT BANK");
    auto msg = GuiMsg();           
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    std::strcpy(msg.msg_box.line_1, "Exporting bank");
    std::strcpy(msg.msg_box.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _progress_state = ProgressState::NOT_STARTED;

    // Run the bank export script
    auto ret = _sw_manager->run_bank_export_script(_selected_bank_dest_name.c_str());
    MSG("Bank export: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        MONIQUE_LOG_INFO(module(), "Bank export: COMPLETE");
    }
    else {
        MONIQUE_LOG_INFO(module(), "Bank export: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    auto str = std::string("Bank export") + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.msg_box.line_1, str.c_str());
    std::strcpy(msg.msg_box.line_2, "Eject USB Drive?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "EJECT");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
}

//----------------------------------------------------------------------------
// _show_add_bank_screen
//----------------------------------------------------------------------------
void GuiManager::_show_add_bank_screen()
{
    // Show a confirmation popup
    _set_left_status("ADD BANK");
    auto msg = GuiMsg();          
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    std::strcpy(msg.msg_box.line_1, "Adding Bank");
    std::strcpy(msg.msg_box.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _progress_state = ProgressState::NOT_STARTED;

    // Run the add bank scrept script
    auto ret = _sw_manager->run_bank_add_script();
    MSG("Add Bank: " << ((ret > 0) ? "COMPLETE" : "FAILED"));
    if (ret > 0) {
        MONIQUE_LOG_INFO(module(), "Add Bank: COMPLETE");
    }
    else {
        MONIQUE_LOG_INFO(module(), "Add Bank: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    auto str1 = std::string("Add Bank") + ((ret > 0) ? " Complete" : " FAILED");
    auto str2 = "New Bank " + std::to_string(ret) + " added";
    std::strcpy(msg.msg_box.line_1, str1.c_str());
    std::strcpy(msg.msg_box.line_2, str2.c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
}

//----------------------------------------------------------------------------
// _show_clear_bank_confirm_screen
//----------------------------------------------------------------------------
void GuiManager::_show_clear_bank_confirm_screen()
{
    // Set the left status
    _set_left_status("CLEAR BANK");

    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    auto str = std::string("Confirm clear bank");
    std::strcpy(msg.msg_box.line_1, str.c_str());
    std::strcpy(msg.msg_box.line_2, _selected_bank_dest_name.c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "YES");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _clear_bank_state = ClearBankState::CONFIRM;
}

//----------------------------------------------------------------------------
// _show_clear_bank_screen
//----------------------------------------------------------------------------
void GuiManager::_show_clear_bank_screen()
{
    // Set the left status
    _set_left_status("CLEAR BANK");

    // Run the bank clear script script
    auto ret = _sw_manager->run_bank_clear_script(_selected_bank_dest_name.c_str());
    MSG("Clear Bank: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        MONIQUE_LOG_INFO(module(), "Clear Bank: COMPLETE");
    }
    else {
        MONIQUE_LOG_INFO(module(), "Clear Bank: FAILED");
    }

    // Check if the current preset still valid
    _check_if_preset_id_still_valid();

    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    auto str = std::string("Clear Bank") + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.msg_box.line_1, str.c_str());
    std::strcpy(msg.msg_box.line_2, "");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
}

//----------------------------------------------------------------------------
// _show_wt_management_screen
//----------------------------------------------------------------------------
void GuiManager::_show_wt_management_screen()
{
    uint item = 0;

    // Set the left status
    _set_left_status("WAVETABLES");

    // Set the list of Wavetable Management options
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    msg.list_items.process_enabled_state = true;
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Import Wavetables");
    msg.list_items.list_item_enabled[item++] = _sw_manager->wt_archive_present();
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Export Wavetables");
    msg.list_items.list_item_enabled[item++] = _sw_manager->msd_mounted();  
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Delete Unused Wavetables");
     msg.list_items.list_item_enabled[item++] = true;
    msg.list_items.num_items = item;
    msg.list_items.selected_item = _selected_wt_management_item;    
    _post_gui_msg(msg);

    // Set the soft buttons
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "ENTER");
    std::strcpy(msg.soft_buttons_text.button2_text, "EXIT");
    _post_gui_msg(msg);
    _gui_state = GuiState::WAVETABLE_MANAGEMENT;
    _wt_management_state = WtManagmentState::SHOW_LIST;
    _progress_state = ProgressState::NOT_STARTED;
    _showing_wt_prune_confirm_screen = false;

    // Configure the data knob
    _num_list_items = item;
    _config_data_knob(_num_list_items);    
}

//----------------------------------------------------------------------------
// _show_wt_import_screen
//----------------------------------------------------------------------------
void GuiManager::_show_wt_import_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    _set_left_status("IMPORT");            
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    std::strcpy(msg.msg_box.line_1, "Importing wavetables");
    std::strcpy(msg.msg_box.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _wt_management_state = WtManagmentState::IMPORT;
    _progress_state = ProgressState::NOT_STARTED;

    // Run the wavetable import script
    std::string str;
    auto ret = _sw_manager->run_wt_import_script();
    MSG("Wavetable import: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        // Wavetable import was successful
        str = "Wavetable import Complete";
        MONIQUE_LOG_INFO(module(), "Wavetable import: COMPLETE");
    }
    else if (ret == WT_NUM_EXCEEDED_ERROR_CODE) {
        // Tried to import too many wavetables
        str = "FAILED: Exceeded max 127 files";
        MONIQUE_LOG_INFO(module(), "Wavetable import: FAILED, exceeded maximum 127 wavetables");
    }
    else {
        // The import failed - most likely due to one or more of the wavetables
        // being invalid
        // Was there an invalid wavetable?
        std::string invalid_wt = "";
        std::ifstream wt_error_file(WT_ERROR_FILENAME);
        std::getline(wt_error_file, invalid_wt);
        if (!invalid_wt.empty()) {
            // One or more wavetables were invalid
            str = "FAILED: " + invalid_wt;
            MONIQUE_LOG_INFO(module(), "Wavetable import: FAILED, invalid wavetable '{}'",  invalid_wt);
        }
        else {
            // Generic error
            str = "Wavetable import FAILED";
            MONIQUE_LOG_INFO(module(), "Wavetable import: FAILED");
        }
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    _strcpy_to_gui_msg(msg.msg_box.line_1, str.c_str());
    std::strcpy(msg.msg_box.line_2, "Eject USB Drive?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "EJECT");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
}

//----------------------------------------------------------------------------
// _show_wt_export_screen
//----------------------------------------------------------------------------
void GuiManager::_show_wt_export_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    _set_left_status("EXPORT");            
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    std::strcpy(msg.msg_box.line_1, "Exporting wavetables");
    std::strcpy(msg.msg_box.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _wt_management_state = WtManagmentState::EXPORT;
    _progress_state = ProgressState::NOT_STARTED;

    // Run the wavetable export script
    auto ret = _sw_manager->run_wt_export_script();
    MSG("Wavetable export: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        MONIQUE_LOG_INFO(module(), "Wavetable export: COMPLETE");
    }
    else {
        MONIQUE_LOG_INFO(module(), "Wavetable export: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    auto str = std::string("Wavetable export") + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.msg_box.line_1, str.c_str());
    std::strcpy(msg.msg_box.line_2, "Eject USB Drive?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "EJECT");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
}

//----------------------------------------------------------------------------
// _show_wt_prune_confirm_screen
//----------------------------------------------------------------------------
void GuiManager::_show_wt_prune_confirm_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    _set_left_status("WAVETABLES");
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Delete all unused");
    std::strcpy(msg.msg_box.line_2, "wavetables?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "YES");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _wt_management_state = WtManagmentState::PRUNE;
    _progress_state = ProgressState::NOT_STARTED;    
    _showing_wt_prune_confirm_screen = true;
}

//----------------------------------------------------------------------------
// _show_wt_prune_screen
//----------------------------------------------------------------------------
void GuiManager::_show_wt_prune_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    _set_left_status("WAVETABLES");          
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    std::strcpy(msg.msg_box.line_1, "Deleting unused wavetables");
    std::strcpy(msg.msg_box.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);

    // Run the wavetable prune script
    auto ret = _sw_manager->run_wt_prune_script();
    MSG("Delete unused wavetables: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        MONIQUE_LOG_INFO(module(), "Delete unused Wavetables: COMPLETE");
    }
    else {
        MONIQUE_LOG_INFO(module(), "Delete unused Wavetables: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    std::strcpy(msg.msg_box.line_1, "Delete unused wavetables");
    std::strcpy(msg.msg_box.line_2, ((ret == 0) ? "Complete" : "FAILED"));
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
    _showing_wt_prune_confirm_screen = false;
}

//----------------------------------------------------------------------------
// _show_backup_screen
//----------------------------------------------------------------------------
void GuiManager::_show_backup_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    _set_left_status("SYSTEM");            
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    std::strcpy(msg.msg_box.line_1, "Running backup script");
    std::strcpy(msg.msg_box.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::BACKUP;
    _backup_state = BackupState::BACKUP_STARTED; 

    // Run the backup script
    auto ret = _sw_manager->run_backup_script();
    MSG("Backup script: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        MONIQUE_LOG_INFO(module(), "Backup script: COMPLETE");
    }
    else {
        MONIQUE_LOG_INFO(module(), "Backup script: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    auto str = std::string("Backup script") + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.msg_box.line_1, str.c_str());
    std::strcpy(msg.msg_box.line_2, "Eject USB Drive?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "EJECT");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _gui_state = GuiState::BACKUP;
    _backup_state = BackupState::BACKUP_FINISHED;
}

//----------------------------------------------------------------------------
// _show_restore_screen
//----------------------------------------------------------------------------
void GuiManager::_show_restore_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    _set_left_status("SYSTEM");            
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    std::strcpy(msg.msg_box.line_1, "Restoring from Backup");
    std::strcpy(msg.msg_box.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "----");
    std::strcpy(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::BACKUP;
    _backup_state = BackupState::BACKUP_STARTED; 

    // Run the restore backup script
    auto ret = _sw_manager->run_restore_backup_script();
    MSG("Restore from Backup: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        MONIQUE_LOG_INFO(module(), "Restore from Backup: COMPLETE");
    }
    else {
        MONIQUE_LOG_INFO(module(), "Restore from Backup: FAILED");
    }

    // Check if the current preset still valid
    _check_if_preset_id_still_valid();

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    auto str = std::string("Restore from Backup") + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.msg_box.line_1, str.c_str());
    std::strcpy(msg.msg_box.line_2, "Eject USB Drive?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    std::strcpy(msg.soft_buttons_text.button1_text, "EJECT");
    std::strcpy(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    _gui_state = GuiState::BACKUP;
    _backup_state = BackupState::BACKUP_FINISHED;
}

//----------------------------------------------------------------------------
// _show_about_screen
//----------------------------------------------------------------------------
void GuiManager::_show_about_screen()
{
    auto msg = GuiMsg();
    struct ifaddrs *if_addrs = nullptr;
    std::string ip4_addr;

    // Get the network IF addresses and parse them until we find the
    // first valid one
    getifaddrs(&if_addrs);
    for (auto ifa = if_addrs; ifa != nullptr; ifa = ifa->ifa_next) {
        // If the next item address is null, skip it
        if (!ifa->ifa_addr) {
            continue;
        }

        // Is it an IPv4 interface?
        if (ifa->ifa_addr->sa_family == AF_INET) {
            // Get the IP address
            char ip_addr[INET_ADDRSTRLEN];
            std::memset(ip_addr, 0, INET_ADDRSTRLEN);
            inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, ip_addr, INET_ADDRSTRLEN);

            // Skip known addresses (localhost + fake USB ethernet address)
            if ((std::strcmp(ip_addr, "127.0.0.1") == 0) ||
                (std::strcmp(ip_addr, "10.0.0.1") == 0)) {
                continue;
            }

            // Assume this is the first "valid" IPv4 address
            ip4_addr = ip_addr;
            break;
        }
    }

    // Set the left status
    _set_left_status("ABOUT");

    // Show the list of versions
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    msg.list_items.num_items = 7;
    msg.list_items.selected_item = -1;
    msg.list_items.process_enabled_state = false;

    // Get the System Version
    uint item = 0;
    char sw_ver[_UTSNAME_VERSION_LENGTH*2];
    std::string sys_ver = "Unknown";
    std::ifstream sys_ver_file("/etc/sw_version");
    std::getline(sys_ver_file, sys_ver);

    // Show the System version
    std::sprintf(sw_ver, "SYSTEM: %s", sys_ver.c_str()); 
    _strcpy_to_gui_msg(msg.list_items.list_items[item++], sw_ver); 

    // UI Version
    std::sprintf(sw_ver, "UI: %d.%d.%d-%c%c%c%c%c%c%c", MONIQUE_UI_MAJOR_VERSION, MONIQUE_UI_MINOR_VERSION, MONIQUE_UI_PATCH_VERSION, 
                                                        MONIQUE_UI_GIT_COMMIT_HASH[0], MONIQUE_UI_GIT_COMMIT_HASH[1], MONIQUE_UI_GIT_COMMIT_HASH[2],
                                                        MONIQUE_UI_GIT_COMMIT_HASH[3], MONIQUE_UI_GIT_COMMIT_HASH[4], MONIQUE_UI_GIT_COMMIT_HASH[5],
                                                        MONIQUE_UI_GIT_COMMIT_HASH[6]);
    _strcpy_to_gui_msg(msg.list_items.list_items[item++], sw_ver);

    // GUI Version
    _strcpy_to_gui_msg(msg.list_items.list_items[item++], "GUI_VER"); // Placeholder, GUI replaces with its version

    // Sushi Version
    auto sushi_ver = static_cast<DawManager *>(utils::get_manager(MoniqueModule::DAW))->get_sushi_version();
    std::sprintf(sw_ver, "VST HOST: %s-%s", sushi_ver.version.c_str(), sushi_ver.commit_hash.substr(0,7).c_str());  
    _strcpy_to_gui_msg(msg.list_items.list_items[item++], sw_ver);

    // VST Version
    std::string vst_ver = "Unknown";
    std::ifstream vst_ver_file(common::MONIQUE_VST_CONTENTS_DIR + std::string("version.txt"));
    std::getline(vst_ver_file, vst_ver);
    std::sprintf(sw_ver, "VST: %s", vst_ver.c_str());  
    _strcpy_to_gui_msg(msg.list_items.list_items[item++], sw_ver);

    // Device ID (RPi Serial Number)
    std::string ser_num = "";
    std::ifstream ser_num_file("/sys/firmware/devicetree/base/serial-number");
    std::getline(ser_num_file, ser_num);
    std::sprintf(sw_ver, "DEVICE ID: %s", ser_num.substr((ser_num.size() - TRUNC_SERIAL_NUM_SIZE), TRUNC_SERIAL_NUM_SIZE).c_str());
    _strcpy_to_gui_msg(msg.list_items.list_items[item++], sw_ver);   

    // Hostname - append the IPv4 address if valid
    std::string hostname = "Unknown";
    std::ifstream hostname_file("/etc/hostname");
    std::getline(hostname_file, hostname);
    ip4_addr.size() > 0 ?
        std::sprintf(sw_ver, "HOSTNAME: %s (%s)", hostname.c_str(), ip4_addr.c_str()) :
        std::sprintf(sw_ver, "HOSTNAME: %s", hostname.c_str());  
    _strcpy_to_gui_msg(msg.list_items.list_items[item], sw_ver);
    _post_gui_msg(msg);

    // Set the soft button text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "----");
    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "BACK");
    _post_gui_msg(msg);
    
    // Set the system menu state
    _system_menu_state = SystemMenuState::OPTION_ACTIONED;
}

//----------------------------------------------------------------------------
// _show_start_sw_update_screen
//----------------------------------------------------------------------------
void GuiManager::_show_start_sw_update_screen(std::string sw_version)
{
    // Show the starting software update screen
    auto msg = GuiMsg();
    _set_left_status("SYSTEM");           
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = true;
    auto str = "Updating Software to v" + sw_version;
    std::strcpy(msg.msg_box.line_1, str.c_str());
    std::strcpy(msg.msg_box.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "----");
    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::SW_UPDATE;
}

//----------------------------------------------------------------------------
// _show_finish_sw_update_screen
//----------------------------------------------------------------------------
void GuiManager::_show_finish_sw_update_screen(std::string sw_version, bool result)
{
    // Show the finishing software update screen
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = true;
    msg.msg_box.show_hourglass = false;
    auto str = "Update to v" + sw_version + (result ? " Done" : " FAILED");
    std::strcpy(msg.msg_box.line_1, str.c_str());
    std::strcpy(msg.msg_box.line_2, "Please power-off DELIA");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
    _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "----");
    _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "----");
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _show_msg_popup
//----------------------------------------------------------------------------
void GuiManager::_show_msg_popup(const char *line1, const char *line2)
{
    // Show a confirmations creen
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_POPUP;
    std::strcpy(msg.msg_popup.line_1, line1);
    std::strcpy(msg.msg_popup.line_2, line2);
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _hide_msg_box
//----------------------------------------------------------------------------
void GuiManager::_hide_msg_box()
{
    // Hide any message popup showing
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_MSG_BOX;
    msg.msg_box.show = false;
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _process_msd_event
//----------------------------------------------------------------------------
static void *_process_msd_event(void* data)
{
    auto mgr = static_cast<GuiManager*>(data);
    mgr->process_msd_event();

    // To suppress warnings
    return nullptr;
}
