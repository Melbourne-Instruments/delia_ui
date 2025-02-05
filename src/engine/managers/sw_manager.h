/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  sw_manager.h
 * @brief Software Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _SW_MANAGER_H
#define _SW_MANAGER_H

#include <thread>
#include <systemd/sd-device.h>
#include "base_manager.h"
#include "event_router.h"
#include "event.h"

// Software Manager class
class SwManager: public BaseManager
{

public:
    // Constructor
    SwManager(EventRouter *event_router);

    // Destructor
    ~SwManager();

    // Public functions
    int run_calibration_script(CalMode mode);
    int run_factory_soak_test();
    int run_qa_check();
    int run_motor_test();
    void end_background_test();
    bool diag_script_present();
    std::vector<std::string> get_diag_scripts();
    int run_diag_script(const char *script);
    std::vector<std::string> get_diag_script_result_msg(const char *script);
    bool bank_archive_present();
    std::vector<std::string> get_bank_archives();
    int run_bank_import_merge_check_script(const char *archive, const char *dest_bank);
    int run_bank_import_script(const char *archive, const char *dest_bank, bool merge);
    int run_bank_export_script(const char *bank);
    int run_bank_add_script();
    int run_bank_clear_script(const char *bank);
    bool wt_archive_present();
    int run_wt_import_script();
    int run_wt_export_script();
    int run_wt_prune_script();
    bool restore_backup_archives_present();
    int run_backup_script();
    int run_restore_backup_script();
    void run_demo_mode_script();
    bool msd_mounted();
    bool start();
    void stop();
    void process();
    void process_sw_update();
    void process_msd_event();
    const char *is_partition_valid(sd_device *device);
    void mount_msd(const char *dev_name);
    void umount_msd();
    void check_pc_fw_available();
    void check_mc_fw_available();

private:
    // Private variables
    std::thread *_sw_update_thread;
    std::thread *_msd_event_thread;
    bool _run_msd_event_thread;
    bool _msd_mounted;
    std::string _update_sw_ver;
    std::string _update_sw_file;
    std::string _update_partition;

    // Private functions
    int _spawn_bash_script(const char *cmd_line);
    int _spawn_python_script(const char *script_path, const char *arg);
};

#endif  // _SW_MANAGER_H
