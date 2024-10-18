/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  gui_utils.h
 * @brief GUI Manager class implementation (utils).
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
#include "gui_manager.h"
#include "seq_manager.h"
#include "logger.h"
#include "utils.h"

// Constants
constexpr uint PARAM_SHORT_CHANGE_TIMER_TIMEOUT = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(3000)).count();
constexpr uint PARAM_MORPH_CHANGE_TIMER_TIMEOUT = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(6000)).count();

//----------------------------------------------------------------------------
// _reset_gui_state_and_show_home_screen
//----------------------------------------------------------------------------
void GuiManager::_reset_gui_state_and_show_home_screen(std::string preset_name) 
{
    // Reset the GUI state and show the home screen
    _reset_gui_state();
    _show_home_screen(preset_name);
}

//----------------------------------------------------------------------------
// _reset_gui_state
//----------------------------------------------------------------------------
void GuiManager::_reset_gui_state() 
{
	// Get the GUI mutex
	std::lock_guard<std::mutex> guard(_gui_mutex);

    // We need to make sure that the UI state always shows Layer 1 if Layer 2
    // has no voices
    if ((_gui_state == GuiState::SHOW_PARAM) && (utils::get_layer_info(LayerId::D1).num_voices() == 0)) {
        // Make sure the Layer state reflects Layer 1
        auto param = utils::get_param(utils::ParamRef::SELECTED_LAYER);
        param->set_value(0);
        utils::set_current_layer(LayerId::D0);
        _set_sys_func_switch(SystemFuncType::SELECT_LAYER_1, ON);
        _set_sys_func_switch(SystemFuncType::SELECT_LAYER_2, OFF);        
    }

    // If we are editing a param AND exiting for other reasons than pressing EDIT again
    if (_editing_param) {
        // We need to handle the FX Type edit exit as a special case before reseting the state
        _handle_fx_type_edit_exit();
    }

    // Reset the relevant GUI variables
    _show_param_list = false;
    _param_shown = nullptr;
    _param_shown_root = nullptr;
    _params_list.clear();
    _param_shown_index = -1;
    _new_param_list = false;
    _editing_param = false;
    _param_change_available = false;
    _num_list_items = 0;
    _list_items.clear();    
    _new_mod_matrix_param_list = false;
    _selected_mod_matrix_src_index = -1;
    _selected_preset_id.set_id("", "");
    _system_menu_state = SystemMenuState::SHOW_OPTIONS;
    _selected_system_menu_item = 0;
    _filenames.clear();
    _manage_preset_state = ManagePresetState::LOAD;
    _select_preset_state = SelectPresetState::SELECT_PRESET;
    _edit_name_state = EditNameState::NONE;
    _selected_preset_index = -1;
    _loaded_preset_index = -1;
    _save_edit_name = "";
    _calibrate_state = CalibrateState::CALIBRATE_STARTED;
    _progress_state = ProgressState::NOT_STARTED;
    _renaming_patch = false;
    _show_patch_names = false;
    _showing_param_shortcut = false;
    _showing_reset_conf_screen = false;
    _showing_additional_mod_dst_params = false;
    _bank_management_state = BankManagmentState::SHOW_LIST;
    _import_bank_state = ImportBankState::NONE;
    _export_bank_state = ExportBankState::NONE;
    _clear_bank_state = ClearBankState::NONE;    
    _selected_bank_management_item = 0;
    _selected_bank_archive = 0;
    _selected_bank_archive_name = "";
    _selected_bank_dest = 0;
    _selected_bank_dest_name = "";    
    _wt_management_state = WtManagmentState::SHOW_LIST;
    _showing_wt_prune_confirm_screen = false;
    _selected_wt_management_item = 0;
    _backup_state = BackupState::BACKUP_STARTED;
    _show_ext_system_menu = false;
    _ext_sys_menu_just_shown = false;
    _wheels_calibrate_state = WheelsCalibrateState::WHEELS_CALIBRATE_NOT_STARTED;
    _show_scope = true;
    _selected_patch_src_index = 0;
    _running_background_test = false;
    _wheel_check_val_ok = false;
    _layer_2_voices_changed = false;
    _show_undo_last_load = false;
    _qa_check_ok = false;
    _mix_vca_cal_status_ok = false;
    _filter_cal_status_ok = false;
    _wheels_cal_status_ok = false;
    _run_diag_script_state = RunDiagScriptState::NONE;
    _selected_diag_script = 0;

    // Make sure any pop-up box is hidden
    _hide_msg_box();
}

//----------------------------------------------------------------------------
// _set_left_status
//----------------------------------------------------------------------------
void GuiManager::_set_left_status(const char *status, bool modified)
{
    // Update the Left status
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    msg.left_status.modified = modified;
    _strcpy_to_gui_msg(msg.left_status.status, status);
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _set_layer_status
//----------------------------------------------------------------------------
void GuiManager::_set_layer_status()
{
    bool twelve_note_mode = false;

    // Get the 12 Note mode param
    auto param = utils::get_param(utils::ParamRef::TWELVE_NOTE_MODE);
    if (param) {
        // Indicate whether 12 note mode is active or not
        twelve_note_mode = param->hr_value() == 0 ? false : true;
    }

    // Update the Layer status
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LAYER_STATUS;
    msg.layer_status.current_layer = ((utils::get_current_layer_info().layer_id() == LayerId::D0) || (utils::get_layer_info(LayerId::D1).num_voices() == 0) ?
                                            Layer::LAYER_1 : Layer::LAYER_2);
    msg.layer_status.twelve_note_mode = twelve_note_mode;
    msg.layer_status.l1_num_voices = utils::get_layer_info(LayerId::D0).num_voices();
    msg.layer_status.l2_num_voices = utils::get_layer_info(LayerId::D1).num_voices();
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _set_tempo_status
//----------------------------------------------------------------------------
void GuiManager::_set_tempo_status(std::string tempo)
{
    // Update the Tempo Status
    auto msg = GuiMsg(GuiMsgType::SET_TEMPO_STATUS);
    _strcpy_to_gui_msg(msg.tempo_status.tempo_value, tempo.c_str());
    _post_gui_msg(msg); 
}

//----------------------------------------------------------------------------
// _set_home_screen_soft_buttons
//----------------------------------------------------------------------------
void GuiManager::_set_home_screen_soft_buttons()
{
    // Show the soft buttons
    auto msg = GuiMsg(GuiMsgType::SET_SOFT_BUTTONS_TEXT);
    if (_scope_mode == SoundScopeMode::SCOPE_MODE_OFF) {
        std::strcpy(msg.soft_buttons_text.button1_text, "SCOPE OSC");
    }
    else if (_scope_mode == SoundScopeMode::SCOPE_MODE_OSC) {
        std::strcpy(msg.soft_buttons_text.button1_text, "SCOPE XY");
    }
    else {
        std::strcpy(msg.soft_buttons_text.button1_text, "SCOPE OFF");
    }
    std::strcpy(msg.soft_buttons_text.button2_text, "SYSTEM");
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _set_standard_param_soft_buttons
//----------------------------------------------------------------------------
void GuiManager::_set_standard_param_gui_state()
{
    // Set the standard param state
    _gui_state = _show_param_list ?
                        GuiState::SHOW_PARAM : 
                        (_param_shown_root == utils::get_morph_value_param()) ?
                        GuiState::SHOW_MORPH_PARAM :
                        GuiState::SHOW_PARAM_SHORT;
}

//----------------------------------------------------------------------------
// _set_standard_param_soft_buttons
//----------------------------------------------------------------------------
void GuiManager::_set_standard_param_soft_buttons()
{
    // Are we showing a param list?
    if (_show_param_list) {
        // Set the soft button text
        auto msg = GuiMsg();
        msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
        _param_shown->type() == ParamType::SYSTEM_FUNC ?
            _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "ENTER") :
            _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "EDIT");
        _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "EXIT");
        _post_gui_msg(msg);
    }
    else {
        // Are we showing the morph param?
        if (_gui_state == GuiState::SHOW_MORPH_PARAM) {
            // Set the soft button text
            auto msg = GuiMsg();
            msg.type = GuiMsgType::SET_SOFT_BUTTONS_TEXT;
            _strcpy_to_gui_msg(msg.soft_buttons_text.button1_text, "SAVE TO A");
            _strcpy_to_gui_msg(msg.soft_buttons_text.button2_text, "SAVE TO B");
            _post_gui_msg(msg);            
        }
        else {
            // Make sure the Home screen buttons are shown!
            _set_home_screen_soft_buttons();
        }         
    }
}

//----------------------------------------------------------------------------
// _select_layer_name
//----------------------------------------------------------------------------
void GuiManager::_select_layer_name(LayerId layer)
{
    // Select the Layer name, but don't change the actual Layer - leave Layer 1 loaded
    auto msg = GuiMsg();           
    msg.type = GuiMsgType::SELECT_LAYER_NAME;
    msg.select_layer_name.selected_layer = layer == LayerId::D0 ? Layer::LAYER_1 : Layer::LAYER_2;
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _update_param_list
//----------------------------------------------------------------------------
void GuiManager::_update_param_list()
{
    // This param will become the root param - get the index
    // of this param in the param list
    auto index = _get_param_list_index(_param_shown_root, _param_shown);
    if (index >= 0) {
        // Should we show this param as an enum list?
        if (_editing_param && _show_param_as_enum_list(_param_shown)) {
            // Show the param as an enum list                   
            _show_enum_param();
        }
        else {
            // Show the param normally
            _show_param();
        }
    }
}

//----------------------------------------------------------------------------
// _process_rename_edit_button
//----------------------------------------------------------------------------
void GuiManager::_process_rename_edit_button(bool selected)
{
    // If the EDIT button is pressed (held down)
    if (selected) {
        // Show the Change Char screen
        _show_edit_name_change_char_screen();
    }
    else {
        // Get the changed character
        char new_char = _charset_index_to_char(_selected_list_char);

        // Character changed, show the Select Char screen
        if (_selected_char_index < _edit_name.size()) {
            _edit_name.at(_selected_char_index) = new_char;
        }
        else {               
            _edit_name.append(1, new_char);
        }
        _show_edit_name_select_char_screen();
    }
}

//----------------------------------------------------------------------------
// _process_rename_select_char
//----------------------------------------------------------------------------
void GuiManager::_process_rename_select_char(KnobParam &data_knob)
{
    // Get the knob position value
    auto value = data_knob.position_value(_selected_char_index);
    if ((value != _selected_char_index) && (value <= _edit_name.size())) {
        // Set the new selected character index
        _selected_char_index = value;

        // Update the edit name to show the selected character
        auto msg = GuiMsg();
        msg.type = GuiMsgType::EDIT_NAME_SELECT_CHAR;
        msg.edit_name_select_char.selected_char = _selected_char_index;
        _post_gui_msg(msg);                
    }
}

//----------------------------------------------------------------------------
// _process_rename_change_char
//----------------------------------------------------------------------------
void GuiManager::_process_rename_change_char(KnobParam &data_knob)
{
    // Get the knob position value
    auto value = data_knob.position_value(_selected_list_char);
    if (value != _selected_list_char) {
        // Set the new selected list character index
        _selected_list_char = value;

        // Update the list to show the selected character
        auto msg = GuiMsg();
        msg.type = GuiMsgType::EDIT_NAME_CHANGE_CHAR;
        msg.edit_name_change_char.change_char = _selected_list_char;
        _post_gui_msg(msg);
    } 
}

//----------------------------------------------------------------------------
// _post_soft_button_state_update
//----------------------------------------------------------------------------
void GuiManager::_post_soft_button_state_update(uint state, SoftButtonId soft_button)
{
    // Update the soft button states
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_STATE;
    msg.soft_buttons_state.button1_state = (soft_button == SoftButtonId::BUTTON_1 ? state : -1);
    msg.soft_buttons_state.button2_state = (soft_button == SoftButtonId::BUTTON_2 ? state : -1);
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _post_update_selected_list_item
//----------------------------------------------------------------------------
void GuiManager::_post_update_selected_list_item(uint selected_item)
{
    // Update the selected list item
    auto msg = GuiMsg();
    msg.type = GuiMsgType::LIST_SELECT_ITEM;
    msg.list_select_item.selected_item = selected_item;
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _post_gui_msg
//----------------------------------------------------------------------------
void GuiManager::_post_gui_msg(const GuiMsg &msg)
{
    // If the GUI Message Queue is valid
    if (_gui_mq_desc != (mqd_t)-1)
    {
        // Write the message to the GUI Message Queue
        if (::mq_send(_gui_mq_desc, (char *)&msg, sizeof(msg), 0) == -1)
        {
            // An error occured
            MSG("ERROR: Sending GUI Message: " << errno);
        }
    }
}

//----------------------------------------------------------------------------
// _gui_param_change_send_callback
//----------------------------------------------------------------------------
void GuiManager::_gui_param_change_send_callback()
{
	// Get the GUI mutex
	std::lock_guard<std::mutex> guard(_gui_mutex);

    // Param change available?
    if (_param_change_available && _param_shown) {
        // If this is a new param list to show
        if (_new_param_list) {
            // Show the new param and param list
            _show_param();
            _new_param_list = false;
        }
        else {
            // Show the param update only
            _show_param_update(true);
        }
        _param_change_available = false;
    }  
}

//----------------------------------------------------------------------------
// _get_root_param_list_index
//----------------------------------------------------------------------------
int GuiManager::_get_root_param_list_index(const Param *param)
{
    return _get_param_list_index(_param_shown_root, param);
}

//----------------------------------------------------------------------------
// _get_param_list_index
//----------------------------------------------------------------------------
int GuiManager::_get_param_list_index(const Param *root_param, const Param *param)
{
    int ret = -1;

    // If the root param has actually been specified
    if (root_param) {
        // Check if the param in the root param list
        uint index = 0;
        for (const Param *p : root_param->param_list()) {
            if (p->cmp_path(param->path())) {
                ret = index;
                break;
            }
            index++;
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _process_param_changed_mapped_params
//----------------------------------------------------------------------------
void GuiManager::_process_param_changed_mapped_params(const Param *param, float diff, const Param *skip_param)
{
    // Get the mapped params
    auto mapped_params = param->mapped_params(skip_param);
    for (Param *mp : mapped_params) {
        // Because this function is recursive, we need to skip the param that
        // caused any recursion, so it is not processed twice
        if (skip_param && (mp == skip_param))
            continue;

        // If this a normal param (not a system function)
        if (mp->type() != ParamType::SYSTEM_FUNC) {
            // Get the current param value
            auto current_value = mp->value();

            // Are both parameters linked to each other?
            if (param->is_linked_param() && mp->is_linked_param()) {
                // Is the linked functionality enabled?
                // If not, ignore this mapping
                if (param->is_linked_param_enabled() || mp->is_linked_param_enabled()) {
                    // Linked parameters are linked by the change (difference) in one of the values
                    // This change is reflected in the other linked param
                    mp->set_value(mp->value() + diff);
                }
                else {
                    // Ignore this mapping
                    continue;
                }
            }
            else {
                // Update the mapped parameter value
                mp->set_value_from_param(*static_cast<const Param *>(param));
            }

            // Create the mapped param change event if it has changed
            if (current_value != mp->value()) {
                // Send the param changed event
                auto param_change = ParamChange(mp, module());
                param_change.display = false;
                _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
            }

            // We need to recurse each mapped param and process it
            _process_param_changed_mapped_params(mp, diff, param);             
        }
        else
        {
            auto sfc_param = static_cast<const SfcControlParam *>(param);
            auto value = sfc_param->value();

            // Send the System Function event
            auto sys_func = SystemFunc(static_cast<const SystemFuncParam *>(mp)->system_func_type(), 
                                       value,
                                       module());
            sys_func.linked_param = static_cast<const SystemFuncParam *>(mp)->linked_param();
            //sys_func.type = sfc_param->system_func_type();
            //if (system_func.type == SystemFuncType::MULTIFN_SWITCH) 
            //{
            //    // Calculate the value from the switch number
            //    system_func.num = param->param_id - utils::system_config()->get_first_multifn_switch_num();
            //}
            _event_router->post_system_func_event(new SystemFuncEvent(sys_func));                   
        }
    }
}

//----------------------------------------------------------------------------
// _start_param_change_timer
//----------------------------------------------------------------------------
void GuiManager::_start_param_change_timer() 
{
    // Only start the timer if we are not showing a param list
    if ((_gui_state == GuiState::SHOW_PARAM_SHORT) || (_gui_state == GuiState::SHOW_MORPH_PARAM)) {
        // Timer active?
        if (_param_change_timer->is_running()) {
            // Stop the timer before restarting it
            _param_change_timer->stop();        
        }

        // Start the param changed timer
        _param_change_timer->start((_gui_state == GuiState::SHOW_PARAM_SHORT ? PARAM_SHORT_CHANGE_TIMER_TIMEOUT : PARAM_MORPH_CHANGE_TIMER_TIMEOUT), 
                                   std::bind(&GuiManager::_param_change_timer_callback, this));
    }
}

//----------------------------------------------------------------------------
// _stop_param_change_timer
//----------------------------------------------------------------------------
void GuiManager::_stop_param_change_timer() 
{
    // The timer is disabled, but stop it anyway just in case
    _param_change_timer->stop();
}

//----------------------------------------------------------------------------
// _param_change_timer_callback
//----------------------------------------------------------------------------
void GuiManager::_param_change_timer_callback()
{
    // Reset the GUI state and show the home screen
    _reset_gui_state_and_show_home_screen();
}

//----------------------------------------------------------------------------
// _start_demo_mode_timer
//----------------------------------------------------------------------------
void GuiManager::_start_demo_mode_timer() 
{
    // If demo mode enabled
    if (utils::system_config()->get_demo_mode()) {
        // Stop the demo mode timer
        _stop_demo_mode_timer();

        // If a demo timeout has been specified and we're not in maintenance mode
        if (utils::system_config()->get_demo_mode_timeout() && !utils::maintenance_mode()) {
            // Start the demo mode timer
            _demo_mode_timer->start((utils::system_config()->get_demo_mode_timeout() * 1000000), std::bind(&GuiManager::_process_demo_mode_timeout, this));
        }
    }
}

//----------------------------------------------------------------------------
// _stop_demo_mode_timer
//----------------------------------------------------------------------------
void GuiManager::_stop_demo_mode_timer()
{
    // Stop the demo mode timer
    if (_demo_mode_timer->is_running()) {
        _demo_mode_timer->stop();
    }

    // If the demo mode is running - kill it
    if (utils::demo_mode()) {
        _sw_manager->end_background_test();
        utils::set_demo_mode(false);

        // The current layer must also be re-loaded
        auto sys_func = utils::get_current_layer_info().layer_id() == LayerId::D0 ? SystemFuncType::LOAD_LAYER_1 : SystemFuncType::LOAD_LAYER_2;
        _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(sys_func, MoniqueModule::GUI)));         
    }    
}

//----------------------------------------------------------------------------
// _process_demo_mode_timeout
//----------------------------------------------------------------------------
void GuiManager::_process_demo_mode_timeout()
{
    // Don't do anything if we are in maintenance mode
    if (!utils::maintenance_mode()) {
        // Reset the GUI state
        _reset_gui_state_and_show_home_screen();

        // Run the demo mode script
        _sw_manager->run_demo_mode_script();
        utils::set_demo_mode(true);   
    }
}

//----------------------------------------------------------------------------
// _handle_fx_type_edit_exit
//----------------------------------------------------------------------------
void GuiManager::_handle_fx_type_edit_exit()
{
    bool send_fx_type_param = false;

    // Special handling if the param is FX 1/2 Type and editing is exited
    // We have to make sure that if an effect is selected in an FX that is already used in the other
    // FX, that we set the other FX to None
    if (_param_shown == utils::get_param(utils::ParamRef::FX_1_TYPE)) {
        // Is the FX 1 type not None?
        if (_param_shown->hr_value() != (uint)Monique::FxType::NONE) {
            // Get the FX 2 type
            auto fx2_type_param = utils::get_param(utils::ParamRef::FX_2_TYPE);
            if (fx2_type_param) {
                // Is this the same type as FX 1?
                if (_param_shown->hr_value() == fx2_type_param->hr_value()) {
                    // Set the FX 2 type to None
                    fx2_type_param->set_hr_value((uint)Monique::FxType::NONE);
                    auto param_change = ParamChange(fx2_type_param, module());
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));                            
                }
            }
        }
        send_fx_type_param = true;
    }
    else if (_param_shown == utils::get_param(utils::ParamRef::FX_2_TYPE)) {
        // Is the FX 2 type not None?
        if (_param_shown->hr_value() != (uint)Monique::FxType::NONE) {
            // Get the FX 1 type
            auto fx1_type_param = utils::get_param(utils::ParamRef::FX_1_TYPE);
            if (fx1_type_param) {
                // Is this the same type as FX 2?
                if (_param_shown->hr_value() == fx1_type_param->hr_value()) {
                    // Set the FX 1 type to None
                    fx1_type_param->set_hr_value((uint)Monique::FxType::NONE);
                    auto param_change = ParamChange(fx1_type_param, module());
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));                            
                }
            }
        }
        send_fx_type_param = true;
    }

    // If this is an FX Type param, send the param change as this is not done when being edited
    if (send_fx_type_param) {
        auto param_change = ParamChange(_param_shown, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change)); 
    }  
}

//----------------------------------------------------------------------------
// _save_morph_to_layer_state
//----------------------------------------------------------------------------
void GuiManager::_save_morph_to_layer_state(LayerState state)
{
    // Store the morph position to required Layer state
    auto sys_func = SystemFunc(SystemFuncType::STORE_MORPH_TO_PRESET_SOUND, MoniqueModule::GUI);
    sys_func.dst_layer_state = state;
    _event_router->post_system_func_event(new SystemFuncEvent(sys_func));

    // Reset the GUI state and show the home screen
    _stop_param_change_timer();          
    _reset_gui_state_and_show_home_screen();
}

//----------------------------------------------------------------------------
// _undo_last_preset_load
//----------------------------------------------------------------------------
void GuiManager::_undo_last_preset_load()
{
    // Restore the last preset load
    _reload_presets_from_select_preset_load++;
    _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::RESTORE_PREV_PRESET, MoniqueModule::GUI))); 
}

//----------------------------------------------------------------------------
// _set_kbd_octave_led_states
//----------------------------------------------------------------------------
void GuiManager::_set_kbd_octave_led_states(int octave_offset)
{
    // Get the KBD mapped params (switches)
    auto octave_inc_dec_params = utils::kbd_octave_mapped_params();
    if (octave_inc_dec_params.first && octave_inc_dec_params.second) {
        // Set the LED states
        auto led_states = utils::kbd_octave_led_states(octave_offset);
        auto sfc_func = SfcFunc(SfcFuncType::SET_SWITCH_LED_STATE, MoniqueModule::GUI);
        sfc_func.param = octave_inc_dec_params.first;
        sfc_func.switch_value = led_states.first;
        _event_router->post_sfc_func_event(new SfcFuncEvent(sfc_func));
        sfc_func.param = octave_inc_dec_params.second;
        sfc_func.switch_value = led_states.second;
        _event_router->post_sfc_func_event(new SfcFuncEvent(sfc_func));
    }
}

//----------------------------------------------------------------------------
// _config_data_knob
//----------------------------------------------------------------------------
void GuiManager::_config_data_knob(int num_selectable_positions, float value)
{
    constexpr char HAPTIC_MODE_360_DEG[] = "360_deg";
    constexpr char HAPTIC_MODE_16_STEP_ENDLESS[] = "16_step_endless";
    constexpr uint DATA_KNOB_DEFAULT_NUM_POS = 16;

    // Get the data knob param
    auto param = utils::get_data_knob_param();
    if (param) {
        std::string haptic_mode = HAPTIC_MODE_360_DEG;

        // Number of positions specified?
        if (num_selectable_positions > 0) {
            param->set_position_param(DATA_KNOB_DEFAULT_NUM_POS, num_selectable_positions);
            haptic_mode = HAPTIC_MODE_16_STEP_ENDLESS;
        }
        // Value specified?
        else if (value >= 0.0f) {
            // Set as a relative value param
            param->set_relative_value_param(value);
        }
        else {
            // Reset the param to a default state
            param->reset();
        }

        // Set the haptic mode
        auto sfc_func = SfcFunc(SfcFuncType::SET_CONTROL_HAPTIC_MODE, MoniqueModule::GUI);
        sfc_func.param = param;
        sfc_func.haptic_mode = haptic_mode;
        _event_router->post_sfc_func_event(new SfcFuncEvent(sfc_func));        
    }
}

//----------------------------------------------------------------------------
// _config_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_config_soft_button_1(bool latched_edit, bool reset)
{
    // If we are changing modes
    if ((_soft_button_1_is_latched_edit != latched_edit) || reset) {
        // Turn the switch OFF and configure the haptic mode
        _post_soft_button_state_update(OFF, SoftButtonId::BUTTON_1);
        _config_switch(SystemFuncType::SOFT_BUTTON_1, (latched_edit ? "latch_push" : "push"), OFF);
        _soft_button_1_is_latched_edit = latched_edit;
    }
}

//----------------------------------------------------------------------------
// _config_sys_func_switches
//----------------------------------------------------------------------------
void GuiManager::_config_sys_func_switches(bool enable)
{
    auto haptic_mode = ((_gui_state == GuiState::MOD_MATRIX) && !enable) ? "push" : "toggle";

    // Note: This function only processes the system function switches
    // that are configurable. All other system function switches are
    // left untouched

    // Enable/disable the configurable system function switches
    // The Sequencer cannot be run in the LOAD/SAVE state, and REC/SETTINGS are disabled in the mod
    // matrix state
    //_config_switch(SystemFuncType::SEQ_RUN, ((_gui_state == GuiState::MANAGE_PATCH) && !enable) ? "push" : "toggle");
    //_config_switch(SystemFuncType::SEQ_REC, (((_gui_state == GuiState::MOD_MATRIX_DST) && !enable)  ? "push" : "toggle_led_pulse"));
    //_config_switch(SystemFuncType::SEQ_SETTINGS, haptic_mode);

    // Disable the menu function switches if in MANAGE PRESET state or higher
    //_config_switch(SystemFuncType::MULTI_MENU, ((_gui_state >= GuiState::MANAGE_PATCH) && !enable) ? "push" : "toggle");

    // The OSC Coarse and LFO Shape must also be disabled in the Mod Matrix state
    utils::osc_state() == utils::OscState::OSC_1_OSC_2_STATE ?
        _set_sys_func_switch_led_state(SystemFuncType::OSC_1_OSC_2_SELECT, SwitchValue::ON) :
        _set_sys_func_switch_led_state(SystemFuncType::OSC_3_OSC_4_SELECT, SwitchValue::ON);

    bool osc_coarse_on = enable ? (utils::osc_tune_state() == utils::OscTuneState::OSC_TUNE_FINE ? OFF : ON) : OFF;
    _config_switch(SystemFuncType::OSC_COARSE, haptic_mode, osc_coarse_on);
    _config_switch(SystemFuncType::LFO_SHAPE, haptic_mode, OFF);
}

//----------------------------------------------------------------------------
// _config_switch
//----------------------------------------------------------------------------
void GuiManager::_config_switch(SystemFuncType system_func_type, std::string haptic_mode, uint value)
{
    // Get the system func param
    auto param = utils::get_sys_func_param(system_func_type);
    if (param) {
        // Get the mapped params for this system function, and set the haptic mode for each
        // Note: Typically there will only be one mapped param
        auto mapped_params = param->mapped_params(nullptr);
        for (Param *mp : mapped_params) {
            // Is this a Surface Control param?
            if (mp->module() == MoniqueModule::SFC_CONTROL) {
                // Reset the switch
                _set_switch(static_cast<SwitchParam *>(mp), value);

                // Set the switch haptic mode
                auto sfc_func = SfcFunc(SfcFuncType::SET_CONTROL_HAPTIC_MODE, module());
                sfc_func.param = static_cast<SwitchParam *>(mp);
                sfc_func.haptic_mode = haptic_mode;
                _event_router->post_sfc_func_event(new SfcFuncEvent(sfc_func));
            }
        }
    }
}

//----------------------------------------------------------------------------
// _reset_sys_func_switches
//----------------------------------------------------------------------------
void GuiManager::_reset_sys_func_switches(SystemFuncType except_system_func_type)
{
    // If valid, make SURE the specified switch function type is ON
    if (except_system_func_type != SystemFuncType::UNKNOWN) {
        _set_sys_func_switch(except_system_func_type, ON);
    }

    // Now set the status of the other switches
    if (except_system_func_type != SystemFuncType::LOAD) {
        _set_sys_func_switch(SystemFuncType::LOAD, OFF);
    }
    if (except_system_func_type != SystemFuncType::SAVE) {
        _set_sys_func_switch(SystemFuncType::SAVE, OFF);
    }
    if (except_system_func_type != SystemFuncType::BANK) {
        _set_sys_func_switch(SystemFuncType::BANK, OFF);
    }
    if (except_system_func_type != SystemFuncType::MOD_MATRIX) {
        _set_sys_func_switch(SystemFuncType::MOD_MATRIX, OFF);
    }
    if (except_system_func_type != SystemFuncType::OSC_COARSE) {  
        // Only reset the OSC Coarse system function if processing Mod Matrix
        if (except_system_func_type == SystemFuncType::MOD_MATRIX) {
            _set_sys_func_switch(SystemFuncType::OSC_COARSE, OFF);
        }
    }
    if (except_system_func_type != SystemFuncType::LFO_SHAPE) {
        _set_sys_func_switch(SystemFuncType::LFO_SHAPE, OFF);
    }
    if (except_system_func_type != SystemFuncType::SEQ_MENU) {
        _set_sys_func_switch(SystemFuncType::SEQ_MENU, OFF); 
    }
    if (except_system_func_type != SystemFuncType::PATCH_MENU) {
        _set_sys_func_switch(SystemFuncType::PATCH_MENU, OFF); 
    }
    if (except_system_func_type != SystemFuncType::MULTI_MENU) {
        _set_sys_func_switch(SystemFuncType::MULTI_MENU, OFF); 
    }    
    if (except_system_func_type != SystemFuncType::WAVE_MENU) {
        _set_sys_func_switch(SystemFuncType::WAVE_MENU, OFF); 
    }    
    if (except_system_func_type != SystemFuncType::ARP_MENU) {
        _set_sys_func_switch(SystemFuncType::ARP_MENU, OFF); 
    }

    if (except_system_func_type != SystemFuncType::OSC_1_OSC_2_SELECT &&
        except_system_func_type != SystemFuncType::OSC_3_OSC_4_SELECT &&
        except_system_func_type != SystemFuncType::OSC_MENU) {
        utils::osc_state() == utils::OscState::OSC_1_OSC_2_STATE ?
            _set_sys_func_switch_led_state(SystemFuncType::OSC_1_OSC_2_SELECT, SwitchValue::ON) :
            _set_sys_func_switch_led_state(SystemFuncType::OSC_3_OSC_4_SELECT, SwitchValue::ON);
        _set_sys_func_switch(SystemFuncType::OSC_MENU, OFF);
    }

    if (except_system_func_type != SystemFuncType::LFO_1_SELECT &&
        except_system_func_type != SystemFuncType::LFO_2_SELECT &&
        except_system_func_type != SystemFuncType::LFO_3_SELECT &&
        except_system_func_type != SystemFuncType::LFO_MENU) {
        _set_sys_func_switch(SystemFuncType::LFO_MENU, OFF); 
    }
    
    if (except_system_func_type != SystemFuncType::VCF_EG_SELECT &&
        except_system_func_type != SystemFuncType::VCA_EG_SELECT &&
        except_system_func_type != SystemFuncType::AUX_EG_SELECT &&
        except_system_func_type != SystemFuncType::ENV_MENU) {
        _set_sys_func_switch(SystemFuncType::ENV_MENU, OFF); 
    }
      
    if (except_system_func_type != SystemFuncType::VCF_MENU)
        _set_sys_func_switch(SystemFuncType::VCF_MENU, OFF);

    if (except_system_func_type != SystemFuncType::FX1_MENU)
        _set_sys_func_switch(SystemFuncType::FX1_MENU, OFF);        
    if (except_system_func_type != SystemFuncType::FX2_MENU)
        _set_sys_func_switch(SystemFuncType::FX2_MENU, OFF);        

    //if (except_system_func_type != SystemFuncType::SYSTEM_MENU)
    //    _set_sys_func_switch(SystemFuncType::SYSTEM_MENU, OFF);
    //if (except_system_func_type != SystemFuncType::UNKNOWN)
    //    _set_sys_func_switch(except_system_func_type, true);            
}

//----------------------------------------------------------------------------
// _reset_param_shortcut_switches
//----------------------------------------------------------------------------
void GuiManager::_reset_param_shortcut_switches()
{
    // If not in the Mod Matrix state
    if (_gui_state != GuiState::MOD_MATRIX) {
        // Reset the relevant param short-cut switch
        _set_sys_func_switch(SystemFuncType::LFO_SHAPE, OFF);
    }
}


//----------------------------------------------------------------------------
// _set_sys_func_switch
//----------------------------------------------------------------------------
void GuiManager::_set_sys_func_switch(SystemFuncType system_func_type, uint value)
{
    // Get the switch associated with the system function and reset it
    auto param = utils::get_param(SystemFuncParam::ParamPath(system_func_type).c_str());
    if (param) {
        auto mapped_params = param->mapped_params(nullptr);
        for (Param *mp : mapped_params) {
            if ((mp->module() == MoniqueModule::SFC_CONTROL) && 
                (static_cast<SfcControlParam *>(mp)->control_type() == sfc::ControlType::SWITCH)) {
                _set_switch(static_cast<SwitchParam *>(mp), value);
            }
        }
    }
}

//----------------------------------------------------------------------------
// _set_sys_func_switch_led_state
//----------------------------------------------------------------------------
void GuiManager::_set_sys_func_switch_led_state(SystemFuncType system_func_type, SwitchValue led_state)
{
    // Get the switch associated with the system function and set the LED behaviour
    auto param = utils::get_param(SystemFuncParam::ParamPath(system_func_type).c_str());
    if (param) {
        auto mapped_params = param->mapped_params(nullptr);
        for (Param *mp : mapped_params) {
            if ((mp->module() == MoniqueModule::SFC_CONTROL) && 
                (static_cast<SfcControlParam *>(mp)->control_type() == sfc::ControlType::SWITCH)) {
                auto sfc_func = SfcFunc(SfcFuncType::SET_SWITCH_LED_STATE, module());
                sfc_func.param = static_cast<SfcControlParam *>(mp);
                sfc_func.switch_value = led_state;
                _event_router->post_sfc_func_event(new SfcFuncEvent(sfc_func));  
            }
        }
    }
}

//----------------------------------------------------------------------------
// _set_switch
//----------------------------------------------------------------------------
void GuiManager::_set_switch(SwitchParam *param, uint value)
{
    // Reset the specified switch
    auto sfc_func = SfcFunc(SfcFuncType::SET_SWITCH_VALUE, module());
    sfc_func.param = param;
    sfc_func.switch_value = value;
    _event_router->post_sfc_func_event(new SfcFuncEvent(sfc_func));   
}

//----------------------------------------------------------------------------
// _show_param_as_enum_list
//----------------------------------------------------------------------------
bool GuiManager::_show_param_as_enum_list(const Param *param)
{
    // If the param has a number of positions, and should be displayed as a
    // list, then show this param as a list
    return (param->num_positions() && param->display_enum_list());
}

//----------------------------------------------------------------------------
// _show_param_as_wt_file_browser
//----------------------------------------------------------------------------
bool GuiManager::_show_param_as_wt_file_browser(const Param *param)
{
    // If this is the WT Select param
    return param == utils::get_param(utils::ParamRef::WT_SELECT);
}

//----------------------------------------------------------------------------
// _set_mod_matrix_state
//----------------------------------------------------------------------------
void GuiManager::_set_mod_matrix_state(Monique::ModMatrixSrc state)
{
    // For the LFOs, OSCs, and EGs, make sure the equivalent controls
    // are mapped to their default state - e.g. LFO 1 state, LFO 1 gain/rate
    // are default as they cannot be modulated
    switch(state) {
        case Monique::ModMatrixSrc::LFO_1:
        case Monique::ModMatrixSrc::LFO_2:
        case Monique::ModMatrixSrc::LFO_3:
            utils::set_controls_state(utils::lfo_ui_state(), _event_router);
            break;

        case Monique::ModMatrixSrc::OSC_1:
        case Monique::ModMatrixSrc::OSC_2:
        case Monique::ModMatrixSrc::OSC_3:
            utils::set_controls_state(utils::osc_tune_ui_state(), _event_router);
            utils::set_controls_state(utils::osc_ui_state(), _event_router);
            break;

        case Monique::ModMatrixSrc::FILTER_EG:
        case Monique::ModMatrixSrc::AMP_EG:
        case Monique::ModMatrixSrc::AUX_EG:
            utils::set_controls_state(utils::eg_ui_state(), _event_router);
            break;
           
        default:
            // No action
            break;
    } 

    // Set the Mod and controls states
    utils::set_mod_state(state);
    utils::set_controls_state(utils::mod_ui_state(), _event_router);
    utils::set_controls_state(utils::mod_osc_ui_state(), _event_router);
    utils::set_controls_state(utils::mod_lfo_ui_state(), _event_router);
    utils::set_controls_state(utils::mod_res_ui_state(), _event_router);
    utils::set_controls_state(utils::mod_eg_ui_state(), _event_router);
    utils::set_controls_state(utils::mod_fx_ui_state(), _event_router);
}

//----------------------------------------------------------------------------
// _is_main_mod_matrix_param
//----------------------------------------------------------------------------
bool GuiManager::_is_main_mod_matrix_param(Param *param)
{
    int index = -1;

    // Get the first mod matrix source dest entry
    std::string param_path = _mod_matrix_param_path(_selected_mod_matrix_src_index, 
                                                    (_selected_mod_matrix_src_index == (int)Monique::ModMatrixSrc::OSC_1 ? 
                                                        (uint)Monique::ModMatrixDst::OSC_2_PITCH :
                                                        (uint)Monique::ModMatrixDst::OSC_1_PITCH));
    auto rp = utils::get_param(param_path);
    if (rp) {
        // This param will become the root param - get the index
        // of this param in the param list
        index = _get_param_list_index(rp, param);
    }
    return index >= 0;
}

//----------------------------------------------------------------------------
// _mod_matrix_param_path
//----------------------------------------------------------------------------
std::string GuiManager::_mod_matrix_param_path(uint _src_index, uint _dst_index)
{
    // Create the Mod Matrix param path
    return std::regex_replace("/daw/delia/Mod_" + _mod_matrix_src_names[_src_index] + 
                              ":" + _mod_matrix_dst_names[_dst_index],
                              std::regex{" "}, "_");
}

//----------------------------------------------------------------------------
// _mod_matrix_param_enabled
//----------------------------------------------------------------------------
bool GuiManager::_mod_matrix_param_enabled(const Param *param)
{
    return (param->hr_value() < -0.005) || (param->hr_value() > 0.005);
}

//----------------------------------------------------------------------------
// _get_edit_name_from_index
//----------------------------------------------------------------------------
std::string GuiManager::_get_edit_name_from_index(uint index)
{
    std::string name = "";
    auto num = _list_item_from_index(index).first;
    name = _list_items[num].substr(4, _list_items[num].size() - 4);
    if (name.size() > EDIT_NAME_STR_LEN) {
        name.resize(EDIT_NAME_STR_LEN);
    }
    return std::regex_replace(name, std::regex{"_"}, " ");
}

//----------------------------------------------------------------------------
// _index_from_list_items
//----------------------------------------------------------------------------
int GuiManager::_index_from_list_items(uint key)
{
    int index = 0;

    // Parse the list until the key is found
    for (const auto& item : _list_items) {
        if (item.first == key) {
            return index;
        }
        index++;
    }
    return -1;
}

//----------------------------------------------------------------------------
// _list_item_from_index
//----------------------------------------------------------------------------
std::pair<uint, std::string> GuiManager::_list_item_from_index(uint index)
{
    // Parse the list index times
    auto itr = _list_items.begin();
    while (index--) {
        itr++;
    }
    return *itr; 
}

//----------------------------------------------------------------------------
// _format_folder_name
//----------------------------------------------------------------------------
std::string GuiManager::_format_folder_name(const char *folder)
{
    // Return the formatted folder name
    auto name = std::string(folder);
    uint index = (name[0] == '0') ? 1 : 0;                        
    name = name.substr(index, (name.size() - index));
    _string_toupper(name);
    return std::regex_replace(name, std::regex{"_"}, " ");
}

//----------------------------------------------------------------------------
// _format_filename
//----------------------------------------------------------------------------
std::string GuiManager::_format_filename(const char *filename)
{
    // Return the formatted filename
    auto name = std::string(filename);
    uint index = (name[0] == '0') ? 1 : 0;                        
    name = name.substr(index, (name.size() - index));
    _string_toupper(name);
    return std::regex_replace(name, std::regex{"_"}, " ");
}

//----------------------------------------------------------------------------
// _parse_presets_folder
//----------------------------------------------------------------------------
std::map<uint, std::string> GuiManager::_parse_presets_folder()
{
    std::map<uint, std::string> folder_names;
    struct dirent **dirent = nullptr;
    int num_files;

    // Scan the presets folder
    num_files = ::scandir(common::MONIQUE_PRESETS_DIR, &dirent, 0, ::versionsort);
    if (num_files > 0) {
        // Process each directory in the folder
        for (uint i=0; i<(uint)num_files; i++) {
            // Is this a directory?
            if (dirent[i]->d_type == DT_DIR) {
                // Get the bank index from the folder name
                // Note: If the folder name format is invalid, atoi will return 0 - which is ok
                // as this is an invalid bank index                
                uint index = std::atoi(dirent[i]->d_name);

                // Are the first four characters the bank index?
                if ((index > 0) && (dirent[i]->d_name[3] == '_')) {
                    // Has this folder index already been found?
                    // We ignore any duplicated folders with the same index
                    if (folder_names[index].empty()) {
                        // Add the bank name
                        folder_names[index] = dirent[i]->d_name;
                    }
                }
            }
            ::free(dirent[i]);
        }
    }
    else if ((num_files == -1) && (errno == ENOENT)) {
        // Presets folder does not exist - this is a critical error
        MSG("The presets folder does not exist: " << common::MONIQUE_PRESETS_DIR);
        MONIQUE_LOG_CRITICAL(module(), "The presets folder does not exist: {}", common::MONIQUE_PRESETS_DIR);
    }
    if (dirent) {
        ::free(dirent);
    }    
    return folder_names;
}

//----------------------------------------------------------------------------
// _parse_bank_folder
//----------------------------------------------------------------------------
std::map<uint, std::string> GuiManager::_parse_bank_folder(const std::string bank_folder_path)
{
    std::map<uint, std::string> filenames;
    struct dirent **dirent = nullptr;
    int num_files;

    // Scan the bank folder
    num_files = ::scandir(bank_folder_path.c_str(), &dirent, 0, ::versionsort);
    if (num_files > 0) {
        // Process each file in the folder
        for (uint i=0; i<(uint)num_files; i++) {
            // Is this a normal file?
            if (dirent[i]->d_type == DT_REG)
            {
                // Get the patch index from the filename
                // Note: If the filename format is invalid, atoi will return 0 - which is ok
                // as this is an invalid patch index
                uint index = std::atoi(dirent[i]->d_name);

                // Are the first two characters the patch number?
                if ((index > 0) && (dirent[i]->d_name[3] == '_'))
                {
                    // Has this patch already been found?
                    // We ignore any duplicated patches with the same index
                    if (filenames[index].empty()) {                    
                        // Add the patch name
                        auto name = std::string(dirent[i]->d_name);
                        filenames[index] = name.substr(0, (name.size() - (sizeof(".json") - 1)));
                    }
                }
            }
            ::free(dirent[i]);
        }
    }
    else if ((num_files == -1) && (errno == ENOENT)) {
        // The bank folder does not exist - show and log the error
        MSG("The bank folder does not exist: " << bank_folder_path);
        MONIQUE_LOG_ERROR(module(), "The bank folder does not exist: {}", bank_folder_path);
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
    if (dirent) {
        ::free(dirent);
    }    
    return filenames;
}

//----------------------------------------------------------------------------
// _parse_wavetable_folder
//----------------------------------------------------------------------------
std::vector<std::string> GuiManager::_parse_wavetable_folder()
{
    std::vector<std::string> folder_names;
    struct dirent **dirent = nullptr;
    int num_files;

    // Scan the MONIQUE wavetables folder
    num_files = ::scandir(common::MONIQUE_WT_DIR, &dirent, 0, ::versionsort);
    if (num_files > 0) {
        // Process each file in the folder
        for (uint i=0; i<(uint)num_files; i++) {
            // If we've not found the max number of wavetables yet and this a normal file
            if ((folder_names.size() < common::MAX_NUM_WAVETABLE_FILES) && (dirent[i]->d_type == DT_REG))
            {
                // If it has a WAV file extension
                auto name = std::string(dirent[i]->d_name);
                if (name.substr((name.size() - (sizeof(".wav") - 1))) == ".wav") {
                    // Add the filename
                    folder_names.push_back(name.substr(0, (name.size() - (sizeof(".wav") - 1))));
                }
            }
            ::free(dirent[i]);
        }
    }
    else if ((num_files == -1) && (errno == ENOENT)) {
        // Wavetables folder does not exist - this is a critical error
        MSG("The wavetables folder does not exist: " << common::MONIQUE_WT_DIR);
        MONIQUE_LOG_CRITICAL(module(), "The wavetables folder does not exist: {}", common::MONIQUE_WT_DIR);
    }
    if (dirent) {
        ::free(dirent);
    }
    return folder_names;
}

//----------------------------------------------------------------------------
// _check_if_preset_id_still_valid
//----------------------------------------------------------------------------
void GuiManager::_check_if_preset_id_still_valid()
{
    // Is the current preset still valid?
    if (!utils::system_config()->preset_id().is_valid(true)) {
        // No - we have to set the fallback ID so that the system can keep
        // functioning
        PresetId preset_id;
        preset_id.set_fallback_id();
        utils::system_config()->set_preset_id(preset_id);
    }
}

//----------------------------------------------------------------------------
// _start_stop_seq_rec
//----------------------------------------------------------------------------
void GuiManager::_start_stop_seq_rec(bool start)
{
    // Get the Sequencer REC param
    auto param = utils::get_param(MoniqueModule::SEQ, SeqParamId::REC_PARAM_ID);
    if (param) {
        // Start/stop sequencer recording
        param->set_value(start ? 1.0 : 0.0);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
    }
}

//----------------------------------------------------------------------------
// _start_stop_seq_run
//----------------------------------------------------------------------------
void GuiManager::_start_stop_seq_run(bool start)
{
    // Get the Sequencer RUN param
    auto param = utils::get_param(MoniqueModule::SEQ, SeqParamId::RUN_PARAM_ID);
    if (param) {
        // Start/stop sequencer running
        param->set_value(start ? 1.0 : 0.0);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
    }
}

//----------------------------------------------------------------------------
// _reset_global_params
//----------------------------------------------------------------------------
void GuiManager::_reset_global_params()
{
    // Note: The parameters included here, and their defaults, are hardcoded
    // There is probably a better way to do this, but it's not happening now

    // Reset the UI specific global params
    auto param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::MIDI_CLK_IN_PARAM_ID);
    if (param) {
        param->set_hr_value(false);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));        
    }
    param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::MIDI_ECHO_FILTER_PARAM_ID);
    if (param) {
        param->set_hr_value((float)MidiEchoFilter::ECHO_FILTER);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));        
    }
    param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::KBD_MIDI_CHANNEL_PARAM_ID);
    if (param) {
        param->set_hr_value(0.0f);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));        
    }
    param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::AT_SENSITIVITY_PARAM_ID);
    if (param) {
        param->set_hr_value(5.0f);
        utils::set_at_sensitivity(param);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));    
    }
    param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::SUSTAIN_POLARITY_PARAM_ID);
    if (param) {
        param->set_hr_value((float)SustainPolarity::POSITIVE);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));        
    }
    param = utils::get_param(MoniqueModule::SYSTEM, SystemParamId::SYSTEM_COLOUR_PARAM_ID);
    if (param) {
        param->set_str_value(DEFAULT_SYSTEM_COLOUR);

        // Send a set system colour GUI message
        auto msg = GuiMsg();
        msg.type = GuiMsgType::SET_SYSTEM_COLOUR;
        _strcpy_to_gui_msg(msg.system_colour.colour, DEFAULT_SYSTEM_COLOUR);
        _post_gui_msg(msg);

        // Post a system function to indicate it has changed
        auto sys_func = SystemFunc(SystemFuncType::SET_SYSTEM_COLOUR, module());
        sys_func.str_value = DEFAULT_SYSTEM_COLOUR;
        _event_router->post_system_func_event(new SystemFuncEvent(sys_func));    
    }

    // Reset the DAW specific global params
    param = utils::get_param(MoniqueModule::DAW, Monique::gen_param_id(Monique::GlobalParams::MASTER_TUNE));
    if (param) {
        param->set_hr_value(0.5f);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));        
    }
    param = utils::get_param(MoniqueModule::DAW, Monique::gen_param_id(Monique::GlobalParams::CV_1_OFFSET));
    if (param) {
        param->set_hr_value(0.0f);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));        
    }
    param = utils::get_param(MoniqueModule::DAW, Monique::gen_param_id(Monique::GlobalParams::CV_1_GAIN));
    if (param) {
        param->set_hr_value(0.0f);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));        
    }
    param = utils::get_param(MoniqueModule::DAW, Monique::gen_param_id(Monique::GlobalParams::CV_1_MODE));
    if (param) {
        param->set_hr_value((float)Monique::CvInputMode::NEG_10_TO_10);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));        
    }
    param = utils::get_param(MoniqueModule::DAW, Monique::gen_param_id(Monique::GlobalParams::CV_2_OFFSET));
    if (param) {
        param->set_hr_value(0.0f);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));        
    }
    param = utils::get_param(MoniqueModule::DAW, Monique::gen_param_id(Monique::GlobalParams::CV_2_GAIN));
    if (param) {
        param->set_hr_value(0.0f);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));        
    }
    param = utils::get_param(MoniqueModule::DAW, Monique::gen_param_id(Monique::GlobalParams::CV_2_MODE));
    if (param) {
        param->set_hr_value((float)Monique::CvInputMode::NEG_10_TO_10);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));        
    }

    // Finally, send an all notes off
    param = utils::get_param(MoniqueModule::DAW, Monique::gen_param_id(Monique::GlobalParams::ALL_NOTES_OFF));
    if (param) {
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
    }   
}


//----------------------------------------------------------------------------
// _string_toupper
//----------------------------------------------------------------------------
void GuiManager::_string_toupper(std::string& str)
{
    // Transform the string to uppercase
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);  
}

//----------------------------------------------------------------------------
// _to_string
//----------------------------------------------------------------------------
std::string GuiManager::_to_string(int val, int width)
{
    // Convert the value to a string
    auto val_str = std::to_string(val);

    // If no width is specified just return the value as a string, otherwise
    // left-pad the string with zeros as required
    if (width == -1) {
        return val_str;
    }
    return std::string(width - std::min(width, (int)val_str.size()), '0') + val_str;
}

//----------------------------------------------------------------------------
// _char_is_charset_valid
//----------------------------------------------------------------------------
bool GuiManager::_char_is_charset_valid(char c)
{
    // Check if the char is valid for the character set
    return (c == ' ') || (c == '-') ||
           ((c >= '0') && (c <= '9')) ||
           ((c >= 'A') && (c <= 'Z'));
}

//----------------------------------------------------------------------------
// _char_to_charset_index
//----------------------------------------------------------------------------
uint GuiManager::_char_to_charset_index(char c)
{
    // Return the chararcter set index - assumes the passed char is valid for
    // the character set
    if (c == ' ')
        return 0;
    else if (c == '-')
        return NUM_CHARSET_CHARS - 1;
    else if (c >= 'A')
        return c - 'A' + 1;
    return c - '0' + (1 + 26);
}

//----------------------------------------------------------------------------
// _charset_index_to_char
//----------------------------------------------------------------------------
char GuiManager::_charset_index_to_char(uint index)
{
    // Return the chararcter set char from the passed index - assumes the
    // passed index is valid for the character set
    if (index == 0) {
        return ' ';
    }
    else if (index == (NUM_CHARSET_CHARS - 1)) {
        return '-';
    }
    else if (index < (1 + 26)) {
        return (char)(index - 1 + 'A');
    }
    else {
        return (char)(index - (1 + 26) + '0');
    }
}
