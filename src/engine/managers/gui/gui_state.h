/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  gui_state.h
 * @brief GUI State definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _GUI_STATE_H
#define _GUI_STATE_H

#include "ui_common.h"

// GUI States
enum class GuiState
{
    HOME_SCREEN,
    SHOW_PARAM_SHORT,
    SHOW_MORPH_PARAM,
    SHOW_PARAM,
    SYSTEM_MENU,
    MOD_MATRIX,
    MANAGE_PRESET,
    SW_UPDATE,
    BACKUP,
    QA_STATUS,
    CALIBRATE,
    WHEELS_CALIBRATE,
    WAVETABLE_MANAGEMENT,
    BANK_MANAGMENT,
    RUN_DIAG_SCRIPT,
    MOTOR_STARTUP_FAILED,
    INVALID
};

// Manage Setup States
enum class ManagePresetState {
    LOAD,
    LOAD_INTO_SELECT_SRC,
    LOAD_INTO_SELECT_DST,
    SAVE,
    STORE_MORPH
};

// Select Preset States
enum class SelectPresetState {
    SELECT_PRESET,
    SELECT_BANK,
    SELECT_OPTION
};

// Save Preset States
enum class SavePresetState {
    SELECT_PRESET,
    SAVE_PRESET
};

// Manage Layers States
enum class ManageLayersState {
    SETUP_LAYERS,
    LOAD_LAYERS,
    SAVE_LAYERS
};

// Save Layers States
enum class SaveLayersState {
    LAYERS_SELECT,
    LAYERS_SAVE
};

// Edit Name States
enum class EditNameState {
    NONE,
    SELECT_CHAR,
    CHANGE_CHAR
};

// System Menu States
enum SystemMenuState {
    SHOW_OPTIONS,
    OPTION_ACTIONED
};

// System Menu Option
enum SystemMenuOption : int {
    WHEELS_CALIBRATION,
    FACTORY_SOAK_TEST,
    CALIBRATION_STATUS,
    MOTOR_TEST,
    MIX_VCA_CALIBRATION,
    FILTER_CALIBRATION,
    RUN_DIAG_SCRIPT,
    GLOBAL_SETTINGS,
    BANK_MANAGMENT,
    WAVETABLE_MANAGEMENT,
    BACKUP,
    RESTORE_BACKUP,
    STORE_DEMO_MODE,
    ABOUT
};

// Software Update States
enum class SwUpdateState {
    SW_UPDATE_STARTED,
    SW_UPDATE_FINISHED
};

// Backup States
enum class BackupState {
    BACKUP_STARTED,
    BACKUP_FINISHED
};

// Calibrate States
enum class CalibrateState {
    CALIBRATION_STATUS,
    CALIBRATE_STARTED,
    CALIBRATE_FINISHED
};

// Wheels Calibrate States
enum class WheelsCalibrateState {
    WHEELS_CALIBRATE_NOT_STARTED,
    PITCH_BEND_WHEEL_TOP_CALIBRATE,
    PITCH_BEND_WHEEL_MID_CALIBRATE_1,
    PITCH_BEND_WHEEL_BOTTOM_CALIBRATE,
    PITCH_BEND_WHEEL_MID_CALIBRATE_2,
    MOD_WHEEL_TOP_CALIBRATE,
    MOD_WHEEL_BOTTOM_CALIBRATE,
    PITCH_BEND_WHEEL_TOP_CHECK,
    PITCH_BEND_WHEEL_MID_CHECK,
    PITCH_BEND_WHEEL_BOTTOM_CHECK,    
    MOD_WHEEL_TOP_CHECK,
    MOD_WHEEL_BOTTOM_CHECK
};

// Run Diag Script States
enum class RunDiagScriptState {
    NONE,
    SELECT_DIAG_SCRIPT,
    CONFIRM_DIAG_SCRIPT,
    RUN_DIAG_SCRIPT
};

// Bank Management States
enum class BankManagmentState {
    SHOW_LIST,
    IMPORT,
    EXPORT,
    ADD,
    CLEAR
};

// Import Bank States
enum class ImportBankState {
    NONE,
    SELECT_ARCHIVE,
    SELECT_DEST,
    IMPORT_METHOD
};

// Export Bank State
enum class ExportBankState {
    NONE,
    SELECT_BANK
};

// Clear Bank State
enum class ClearBankState {
    NONE,
    SELECT_BANK,
    CONFIRM
};

// Wavetable Management States
enum class WtManagmentState {
    SHOW_LIST,
    IMPORT,
    EXPORT,
    PRUNE
};

// General progress states
enum class ProgressState {
    NOT_STARTED,
    FAILED,
    FINISHED
};

#endif  // _GUI_STATE_H
