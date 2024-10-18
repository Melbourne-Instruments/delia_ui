/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  gui_manager.h
 * @brief GUI Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _GUI_MANAGER_H
#define _GUI_MANAGER_H

#include <mqueue.h>
#include <fstream>
#include <map>
#include "base_manager.h"
#include "daw_manager.h"
#include "sw_manager.h"
#include "event.h"
#include "event_router.h"
#include "param.h"
#include "gui_msg.h"
#include "gui_state.h"
#include "timer.h"
#include "utils.h"

// Constants
constexpr uint NUM_CHARSET_CHARS             = (1 + 26 + 10 + 1);
constexpr uint DEFAULT_CHARSET_CHAR          = ' ';
constexpr char MIX_VCA_CAL_STATUS_FILENAME[] = "vca_cal_status.txt";
constexpr char FILTER_CAL_STATUS_FILENAME[]  = "filter_cal_status.txt";
constexpr char WHEELS_CAL_STATUS_FILENAME[]  = "wheels_cal_status.txt";

// GUI Manager class
class GuiManager: public BaseManager
{
public:
    // Constructor
    GuiManager(EventRouter *event_router);

    // Destructor
    ~GuiManager();

    // Public functions
    bool start();
    void process();
    void process_event(const BaseEvent *event);
    void process_msd_event();
    void show_msg_box(const char *line1, const char *line2);

private:
    // Private variables
    SwManager *_sw_manager;
    EventListener *_param_changed_listener;
    EventListener *_system_func_listener;
    EventListener *_reload_presets_listener;
    EventListener *_midi_event_listener;
    Timer *_gui_param_change_send_timer;
    Timer *_param_change_timer;
    Timer *_demo_mode_timer;
    mqd_t _gui_mq_desc;
    std::mutex _gui_mutex;      
    GuiState _gui_state;
    std::thread *_msd_event_thread;
    bool _run_msd_event_thread;
    int _selected_bank_index;
    int _selected_preset_index;
    int _loaded_preset_index;
    int _selected_patch_src_index;
    uint _selected_patch_dst_index;
    bool _show_undo_last_load;
    PresetId _selected_preset_id;
    bool _show_param_list;
    Param *_param_shown;
    Param *_param_shown_root;
    std::vector<Param *> _params_list;
    int _param_shown_index;
    bool _new_param_list;
    bool _editing_param;
    bool _showing_param_shortcut;
    bool _param_change_available;
    uint _num_list_items;
    std::map<uint, std::string> _list_items;
    std::vector<std::string> _filenames;
    std::chrono::_V2::steady_clock::time_point _param_shown_start_time{};
    std::vector<std::string> _mod_matrix_src_names;
    std::vector<std::string> _mod_matrix_dst_names;
    bool _new_mod_matrix_param_list;
    int _selected_mod_matrix_src_index;
    SystemMenuState _system_menu_state;
    uint _selected_system_menu_item;
    SoundScopeMode _scope_mode;
    bool _show_scope;
    ManagePresetState _manage_preset_state;
    SelectPresetState _select_preset_state;
    SavePresetState _save_preset_state;
    EditNameState _edit_name_state;
    std::string _edit_name;
    std::string _save_edit_name;
    uint _selected_char_index;
    uint _selected_list_char;
    uint _reload_presets_from_select_preset_load;
    CalibrateState _calibrate_state;
    ProgressState _progress_state;
    bool _renaming_patch;
    bool _soft_button_1_is_latched_edit;
    bool _show_patch_names;
    bool _showing_reset_conf_screen;
    bool _showing_additional_mod_dst_params;
    BankManagmentState _bank_management_state;
    ImportBankState _import_bank_state;
    ExportBankState _export_bank_state;
    ClearBankState _clear_bank_state;    
    uint _selected_bank_management_item;
    uint _selected_bank_archive;
    std::string _selected_bank_archive_name;
    uint _selected_bank_dest;
    std::string _selected_bank_dest_name;      
    WtManagmentState _wt_management_state;
    bool _showing_wt_prune_confirm_screen;
    uint _selected_wt_management_item;
    BackupState _backup_state;
    bool _show_ext_system_menu;
    bool _ext_sys_menu_just_shown;
    WheelsCalibrateState _wheels_calibrate_state;
    bool _running_background_test;
    bool _wheel_check_val_ok;
    bool _layer_2_voices_changed;
    bool _qa_check_ok;
    bool _mix_vca_cal_status_ok;
    bool _filter_cal_status_ok;
    bool _wheels_cal_status_ok;
    RunDiagScriptState _run_diag_script_state;
    uint _selected_diag_script;

    // Manager main functions
    void _process_param_changed_event(const ParamChange &data);
    void _process_system_func_event(const SystemFunc &data);
    void _process_reload_presets(const ReloadPresetsEvent *event);
    void _process_midi_event(const snd_seq_event_t &seq_event);
    void _process_mod(const SystemFunc& sys_func);
    void _process_load_save(bool selected, bool save);
    void _process_osc_select(const SystemFunc &sys_func, utils::OscState state);
    void _process_osc_coarse(utils::OscTuneState state);
    void _process_lfo_select(const SystemFunc &sys_func, utils::LfoState state);
    void _process_lfo_shape(const SystemFunc& sys_func);
    void _process_vcf_lp_cutoff_mode(const SystemFunc& sys_func);
    void _process_vcf_cutoff_link(const SystemFunc& sys_func);
    void _process_res_select(utils::ResState state);
    void _process_eg_select(const SystemFunc &data, utils::EgState state);
    void _process_fx_select(utils::FxState state);
    void _process_fx_param(float value);
    void _process_system_func_param(const SystemFunc &data, Param *linked_param, bool show_patch_names=false, bool force_show_list=false);
    void _process_octave_inc(bool selected);
    void _process_octave_dec(bool selected);    
    void _process_seq_rec(bool selected);
    void _process_seq_run(bool selected);
    void _process_preset_inc(bool selected);
    void _process_preset_dec(bool selected);
    void _process_select_layer_1();
    void _process_select_layer_2();
    void _process_select_bank();
    void _process_multi_menu(const SystemFunc& sys_func);
    void _process_wave_menu(const SystemFunc& sys_func);
    void _process_osc_menu(const SystemFunc& sys_func);
    void _process_lfo_menu(const SystemFunc& sys_func);
    void _process_env_menu(const SystemFunc& sys_func);
    void _process_tempo_select(const SystemFunc& sys_func);
    void _process_glide_select(const SystemFunc& sys_func);
    void _process_toggle_patch_state(const SystemFunc& sys_func);
    void _process_mod_matrix_multifn_switch(uint switch_index);
    void _process_soft_button_1(bool selected);
    void _process_show_param_soft_button_1(bool selected);
    void _process_manage_preset_soft_button_1(bool selected);
    void _process_load_preset_soft_button_1();
    void _process_save_preset_soft_button_1(bool selected);
    void _process_bank_management_soft_button_1();
    void _process_system_menu_soft_button_1();
    void _process_wheels_calibration_soft_button_1();
    void _process_wt_management_soft_button_1();
    void _process_backup_soft_button_1();
    void _process_soft_button_2(bool selected);
    void _process_param_update_soft_button_2(bool selected);
    void _process_select_bank_soft_button_2();
    void _process_manage_preset_soft_button_2();
    void _process_select_setup_load_soft_button_2();
    void _process_select_setup_save_soft_button_2();
    void _process_select_option_soft_button_2();
    void _process_bank_management_soft_button_2();
    void _process_select_bank_data_knob(KnobParam &data_knob);
    void _process_select_preset_data_knob(KnobParam &data_knob);
    void _process_select_patch_src_data_knob(KnobParam &data_knob);
    void _process_select_patch_dst_data_knob(KnobParam &data_knob);
    void _process_show_param_data_knob(KnobParam &data_knob);
    void _process_system_menu_data_knob(KnobParam &data_knob);
    void _process_bank_management_data_knob(KnobParam &data_knob);
    void _process_wt_management_data_knob(KnobParam &data_knob);
    void _process_run_diag_script_data_knob(KnobParam &data_knob);
    void _process_system_menu(bool selected);

    // Screens
    void _show_home_screen(std::string preset_name);
    void _show_mod_matrix_src_screen();
    void _show_select_preset_load_save_screen(bool save, std::string new_bank="");
    void _show_select_bank_screen();
    void _show_preset_load_option_select_patch_screen();
    void _show_preset_load_option_select_dest_screen();
    void _show_edit_name_select_char_screen();
    void _show_edit_name_change_char_screen();
    void _show_param();
    void _show_standard_param();
    void _show_normal_param();
    void _show_adsr_env_param();
    void _show_vcf_cutoff_param();
    void _show_mod_matrix_param();
    void _show_param_update(bool select_list_item);
    void _show_standard_param_update(bool select_list_item);
    void _show_normal_param_update(bool select_list_item);
    void _show_adsr_env_param_update(bool select_list_item);
    void _show_vcf_cutoff_param_update(bool select_list_item);
    void _show_mod_matrix_param_update(bool select_list_item);
    void _show_enum_param();
    void _show_enum_param_update(uint value);
    void _show_wt_file_browser_param();
    void _show_reset_seq_screen();
    void _hide_reset_seq_screen();
    void _show_system_menu_screen();
    void _show_calibration_status_screen(bool run_qa_check, int pitch_val, uint mod_val, uint at_val);
    void _show_calibrate_screen(CalMode mode);
    void _show_factory_soak_test_screen();
    void _show_pitch_bend_wheel_top_calibration_screen();
    void _show_pitch_bend_wheel_mid_calibration_screen(bool first_cal);
    void _show_pitch_bend_wheel_bottom_calibration_screen();
    void _show_pitch_bend_wheel_top_check_screen(int val);
    void _show_pitch_bend_wheel_mid_check_screen(int val);
    void _show_pitch_bend_wheel_bottom_check_screen(int val);
    void _show_mod_wheel_top_calibration_screen();
    void _show_mod_wheel_bottom_calibration_screen();
    void _show_mod_wheel_top_check_screen(uint val);
    void _show_mod_wheel_bottom_check_screen(uint val);
    void _show_motor_test_screen();
    void _show_select_diag_script_screen();
    void _show_run_diag_script_confirm_screen();
    void _show_run_diag_script_screen();
    void _show_global_settings_screen();
    void _show_reset_to_factory_screen();
    void _hide_reset_to_factory_screen();
    void _show_bank_management_screen();
    void _show_bank_import_screen();
    void _show_select_bank_archive_screen();
    void _show_select_dest_bank_screen();
    void _show_bank_import_method_screen();
    void _show_import_bank_screen(bool merge);
    void _show_export_bank_screen();
    void _show_add_bank_screen();
    void _show_clear_bank_confirm_screen();
    void _show_clear_bank_screen();
    void _show_wt_management_screen();
    void _show_wt_import_screen();
    void _show_wt_export_screen();
    void _show_wt_prune_confirm_screen();
    void _show_wt_prune_screen();
    void _show_backup_screen();
    void _show_restore_screen();
    void _show_about_screen();
    void _show_start_sw_update_screen(std::string sw_version);
    void _show_finish_sw_update_screen(std::string sw_version, bool result);
    void _show_msg_popup(const char *line1, const char *line2);
    void _hide_msg_box();

    // Utils
    void _reset_gui_state_and_show_home_screen(std::string preset_name=utils::system_config()->preset_id().preset_display_name());
    void _reset_gui_state();
    void _set_left_status(const char *status, bool modified=false);
    void _set_layer_status();
    void _set_tempo_status(std::string tempo);
    void _set_home_screen_soft_buttons();
    void _set_standard_param_gui_state();
    void _set_standard_param_soft_buttons();
    void _select_layer_name(LayerId layer);
    void _update_param_list();
    void _process_rename_edit_button(bool selected);
    void _process_rename_select_char(KnobParam &data_knob);
    void _process_rename_change_char(KnobParam &data_knob);
    void _post_soft_button_state_update(uint state, SoftButtonId soft_button);
    void _post_update_selected_list_item(uint selected_item);
    void _post_gui_msg(const GuiMsg &msg);
    void _gui_param_change_send_callback();
    int _get_root_param_list_index(const Param *param);
    int _get_param_list_index(const Param *root_param, const Param *param);
    void _process_param_changed_mapped_params(const Param *param, float diff, const Param *skip_param);
    void _start_param_change_timer();
    void _stop_param_change_timer();
    void _param_change_timer_callback();
    void _start_demo_mode_timer();
    void _stop_demo_mode_timer();
    void _process_demo_mode_timeout();
    void _handle_fx_type_edit_exit();
    void _save_morph_to_layer_state(LayerState state);
    void _undo_last_preset_load();
    void _set_kbd_octave_led_states(int octave_offset);
    void _config_data_knob(int num_selectable_positions=-1, float pos=-1.0);
    void _config_soft_button_1(bool latched_edit, bool reset=false);
    void _config_sys_func_switches(bool enable);
    void _config_switch(SystemFuncType system_func_type, std::string haptic_mode, uint value);
    void _reset_sys_func_switches(SystemFuncType except_system_func_type);
    void _reset_param_shortcut_switches();
    void _set_sys_func_switch(SystemFuncType system_func_type, uint value);
    void _set_sys_func_switch_led_state(SystemFuncType system_func_type, SwitchValue led_state);
    void _set_switch(SwitchParam *param, uint value);
    bool _show_param_as_enum_list(const Param *param);
    bool _show_param_as_wt_file_browser(const Param *param);
    void _set_mod_matrix_state(Monique::ModMatrixSrc state);
    bool _is_main_mod_matrix_param(Param *param);
    std::string _mod_matrix_param_path(uint _src_index, uint _dst_index);
    bool _mod_matrix_param_enabled(const Param *param);
    std::string _get_edit_name_from_index(uint index);
    int _index_from_list_items(uint key);
    std::pair<uint, std::string> _list_item_from_index(uint index);
    std::string _format_folder_name(const char *folder);
    std::string _format_filename(const char *filename);
    std::map<uint, std::string> _parse_presets_folder();
    std::map<uint, std::string> _parse_bank_folder(const std::string bank_folder_path);
    std::vector<std::string> _parse_wavetable_folder();
    void _check_if_preset_id_still_valid();
    void _start_stop_seq_rec(bool start);
    void _start_stop_seq_run(bool start);
    void _reset_global_params();
    void _string_toupper(std::string& str);
    std::string _to_string(int val, int width=-1);
    bool _char_is_charset_valid(char c);
    uint _char_to_charset_index(char c);
    char _charset_index_to_char(uint index);

    // Inline functions
    inline void _strcpy_to_gui_msg(char *dest, const char *src) {
        // Copy the passed string to the GUI message destination - truncating if necessary
        // Note: The max size is decreased by 1 to account for the NULL terminator
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wstringop-truncation"
        std::strncpy(dest, src, (STD_STR_LEN-1));
        #pragma GCC diagnostic pop    
    }
};

#endif  // _GUI_MANAGER_H
