/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  sfc_control.cpp
 * @brief Surface Control driver implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <stdint.h>
#include <cstring>
#include <thread>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <fcntl.h>
#include <errno.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include "sfc.h"
#include "utils.h"
#include "logger.h"

// The *default* is for the MC read packet size to be used
// Comment this out to perform MC reads without a max packet size
//#define I2C_MC_READ_USE_PACKET_SIZE     1

// Surface Control types
constexpr char KNOB_TYPE_STRING[]         = "knob";
constexpr char SWITCH_TYPE_STRING[]       = "switch";
constexpr uint16_t HAPTIC_KNOB_MIN_WIDTH  = 30;
constexpr uint16_t HAPTIC_KNOB_MAX_WIDTH  = 330;
constexpr int HAPTIC_KNOB_MAX_NUM_INDENTS = 32;

// Common registers for bootloader and controllers
enum CommonRegMap : int
{
    CONFIG_DEVICE = 0,
    CHECK_FIRMWARE
};

// Bootloader specific register map
enum BootloaderRegMap : int
{
    START_FIRMWARE = 2,
    START_PROGRAMMING,
    WRITE_FIRMWARE,
    STATUS
};

// Bootloader status
enum BootloaderStatus : int {
    IDLE = 0,
    BUSY,
    PARAM_SIZE_ERROR,
    FIRMWARE_OK,
    INVALID_FIRMWARE,
    INVALID_ADDRESS,
    READY_FOR_DATA,
    CHECKSUM_ERROR,
    PROGRAMMING_COMPLETE
};

// Motor Controller specific register map
enum McRegMap : int
{
    SAMPLER_BUFFER_READ = 0x0A,
    ENCODER_A_OFFSET = 0x0B,
    ENCODER_A_GAIN = 0x0C,
    ENCODER_B_OFFSET = 0x0D,
    ENCODER_B_GAIN = 0x0E,
    ENCODER_DATUM_THRESHOLD = 0x0F,    
    MOTION_HAPTIC_CONFIG = 0x21,
    MOTION_MODE_FIND_DATUM = 0x25,
    MOTION_MODE_POSITION = 0x29,
    MOTION_MODE_HAPTIC = 0x2A,
    MOTOR_STATUS = 0x2B,
    MC_REBOOT = 0x2F,
    CAL_ENC_PARAMS = 0x33
};

// Panel Controller specific register map
enum PcRegMap : int
{
    SWITCH_STATE = 0,
    LED_STATE,
    LED_BACKLIGHT,
    LED_BLANKING,
    PC_REBOOT,
    LATCH_PB_MIN,
    LATCH_PB_MID_1,
    LATCH_PB_MAX,
    LATCH_MW_MIN,
    LATCH_MW_MAX,
    FLASH_CONFIG_WRITE,
    LATCH_PB_MID_2,
    SET_AT_SCALE
};

// Audio Control device name
#define SFC_HW_I2C_BUS_NUM       "6"
#define AUDIO_CONTROL_DEV_NAME   "/dev/i2c-" SFC_HW_I2C_BUS_NUM

// Array sizes
constexpr int NUM_SWITCH_BYTES = 6;
constexpr int NUM_LED_BYTES    = NUM_SWITCH_BYTES;
constexpr int NUM_BITS_IN_BYTE = 8;

// I2C slave addresses
constexpr uint8_t MC_DEFAULT_I2C_SLAVE_ADDR = 8;
constexpr uint8_t MC_BASE_I2C_SLAVE_ADDR    = 50;
constexpr uint8_t PC_I2C_SLAVE_ADDR         = 100;

// Bootloader constants
constexpr uint8_t CONFIG_DEVICE_SCL_LOOP_OUT = 0x80;
constexpr uint8_t FIRMWARE_TYPE_BOOTLOADER   = 0x80;

// Motor Control constants
constexpr int FIRMWARE_VER_SIZE               = 16;
constexpr int CAL_ENCODER_PARAMS_OK           = 0x01;
constexpr int CALIBRATION_STATUS_DATUM_FOUND  = 0x01;
constexpr int HAPTIC_CONFIG_MIN_NUM_BYTES     = 9;
constexpr int MOTOR_STATUS_RESP_NUM_WORDS     = 2;
constexpr uint CAL_MOTORS_RETRY_COUNT         = 3;
constexpr uint CAL_ENCODER_PARAMS_RETRY_COUNT = 5;

// I2C constants
constexpr uint I2C_PC_WRITE_MAX_PACKET_SIZE = 4;
constexpr uint I2C_MC_READ_MAX_PACKET_SIZE  = 4;
constexpr uint I2C_MC_READ_PACKET_DELAY_US  = 20;
constexpr uint I2C_READ_RETRY_COUNT         = 5;
constexpr uint I2C_ROBUST_WRITE_RETRY_COUNT = 5;
constexpr uint I2C_WRITE_RETRY_COUNT        = 5;

// Private data
int _dev_handle;
bool _pc_active;
bool _mc_active[NUM_PHYSICAL_KNOBS];
bool _mc_knob_state_requested[NUM_PHYSICAL_KNOBS];
bool _mc_haptic_set[NUM_PHYSICAL_KNOBS];
std::string _mc_haptic_mode[NUM_PHYSICAL_KNOBS];
uint8_t *_led_states;
std::mutex _controller_mutex;
int _selected_controller_addr;

// Private functuions
void _init_controllers();
int _pc_reboot();
int _pc_set_addr();
int _pc_get_firmware_ver(uint8_t *ver);
int _start_pc();
int _mc_request_status(uint8_t mc_num);
int _mc_reboot(uint8_t mc_num); 
int _mc_set_addr(uint8_t mc_num);
int _mc_get_firmware_ver(uint8_t mc_num, uint8_t *ver);     
int _start_mc(uint8_t mc_num);
int _mc_request_cal_enc_params(uint8_t mc_num);
int _mc_check_cal_enc_params_status(uint8_t mc_num, uint8_t *status);
int _mc_request_find_datum(uint8_t mc_num);
int _mc_read_find_datum_status(uint8_t mc_num, uint8_t *status);
int _mc_set_haptic_mode(uint8_t mc_num, const sfc::HapticMode& haptic_mode);
int _mc_request_knob_state(uint8_t mc_num);
int _mc_read_knob_state(uint8_t mc_num, sfc::KnobState *states);
int _mc_set_position(uint8_t mc_num, uint16_t position, bool robust);
int _pc_read_switch_states(uint8_t *switch_states);
int _pc_set_led_states(uint8_t *led_states);
int _select_mc_default();
int _select_mc(uint8_t num);
int _select_pc();
int _i2c_select_slave(uint8_t addr);
inline int _i2c_pc_read(void *buf, size_t buf_len);
inline int _i2c_mc_read(void *buf, size_t buf_len);
inline int _i2c_pc_robust_write(const void *buf, size_t buf_len, bool readback, uint8_t readback_value=0);
inline int _i2c_mc_robust_write(const void *buf, size_t buf_len, bool readback, uint8_t readback_value=0);
inline int _i2c_pc_write(const void *buf, size_t buf_len);
inline int _i2c_mc_write(const void *buf, size_t buf_len);
int _i2c_read(void *buf, size_t buf_len, size_t packet_len);
int _i2c_robust_write(const void *buf, size_t buf_len, size_t packet_len, bool readback, uint8_t readback_value);
int _i2c_write(const void *buf, size_t buf_len, size_t packet_len);

//----------------------------------------------------------------------------
// init
//----------------------------------------------------------------------------
int sfc::init()
{
    // Initialise private data
    _dev_handle = -1;
    _pc_active = false;
    std::memset(_mc_active, false, sizeof(_mc_active));
    std::memset(_mc_knob_state_requested, false, sizeof(_mc_knob_state_requested));
    std::memset(_mc_haptic_set, false, sizeof(_mc_haptic_set));
    _led_states = new uint8_t[NUM_LED_BYTES];
    std::memset(_led_states, 0, sizeof(uint8_t[NUM_LED_BYTES]));
    _selected_controller_addr = -1;

    // Open the I2C bus
    auto handle = ::open(AUDIO_CONTROL_DEV_NAME, O_RDWR);
    if (handle < 0)
    {
        // An error occurred opening the I2C device
        return handle;
    }

    // Save the device handle
    _dev_handle = handle;

    // Initialise the Panel and Motor Controllers
    _init_controllers();

    // Indicate the controllers were initialised OK, and show the number of knobs
    // initialised
    uint num_mcs = 0;
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {
        // Is this Motor Controller active?
        if (_mc_active[i])
            num_mcs++;
    }
    MSG("Panel Controller: " << (_pc_active ? "OK": "FAILED"));
    MSG("Motor Controllers (" << num_mcs << "): " << (num_mcs ? "OK": "FAILED"));
    MONIQUE_LOG_INFO(MoniqueModule::SFC_CONTROL, "Panel Controller: {}", (_pc_active ? "OK": "FAILED"));
    MONIQUE_LOG_INFO(MoniqueModule::SFC_CONTROL, "Motor Controllers ({}): {}", num_mcs, (num_mcs ? "OK": "FAILED"));

    // If less than the expected Motor Controllers, show a warning
    if (num_mcs < NUM_PHYSICAL_KNOBS)
    {
        MSG("WARNING: Less Motor Controllers found than expected (" << NUM_PHYSICAL_KNOBS << ")");
        MONIQUE_LOG_WARNING(MoniqueModule::SFC_CONTROL, "Less Motor Controllers found than expected ({})", NUM_PHYSICAL_KNOBS);
    }
    MONIQUE_LOG_FLUSH();
    return 0;
}

//----------------------------------------------------------------------------
// reinit
//----------------------------------------------------------------------------
void sfc::reinit()
{
    // Initialise the Panel and Motor Controllers
    _pc_active = false;
    std::memset(_mc_active, false, sizeof(_mc_active));
    std::memset(_mc_knob_state_requested, false, sizeof(_mc_knob_state_requested));
    std::memset(_mc_haptic_set, false, sizeof(_mc_haptic_set));    
    _init_controllers();

    // Indicate the controllers were initialised OK, and show the number of knobs
    // initialised
    uint num_mcs = 0;
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {
        // Is this Motor Controller active?
        if (_mc_active[i])
            num_mcs++;
    }
    MSG("Panel Controller: " << (_pc_active ? "OK": "FAILED"));
    MSG("Motor Controllers (" << num_mcs << "): " << (num_mcs ? "OK": "FAILED"));
    MONIQUE_LOG_INFO(MoniqueModule::SFC_CONTROL, "Panel Controller: {}", (_pc_active ? "OK": "FAILED"));
    MONIQUE_LOG_INFO(MoniqueModule::SFC_CONTROL, "Motor Controllers ({}): {}", num_mcs, (num_mcs ? "OK": "FAILED"));

    // If less than the expected Motor Controllers, show a warning
    if (num_mcs < NUM_PHYSICAL_KNOBS)
    {
        MSG("WARNING: Less Motor Controllers found than expected (" << NUM_PHYSICAL_KNOBS << ")");
        MONIQUE_LOG_WARNING(MoniqueModule::SFC_CONTROL, "Less Motor Controllers found than expected ({})", NUM_PHYSICAL_KNOBS);
    }
    MONIQUE_LOG_FLUSH();
}

//----------------------------------------------------------------------------
// deinit
//----------------------------------------------------------------------------
void sfc::deinit()
{
    // Before we close the surface control device, disable haptics for all knobs
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {
        // Is this Motor Controller active?
        if (_mc_active[i])
        {        
            // Disable haptic mode (ignore the return value)
            // Note: The default haptic mode class has disabled haptics
            auto haptic_mode = sfc::HapticMode();
            (void)_mc_set_haptic_mode(i, haptic_mode);
        }
    }
    
    // If the device is open
    if (_dev_handle > 0)
    {
        // Close it
        ::close(_dev_handle);
        _dev_handle = -1;
        _selected_controller_addr = -1;       
    }

    // Clean up any allocated memory
    if (_led_states)
        delete [] _led_states;
}

//----------------------------------------------------------------------------
// lock
//----------------------------------------------------------------------------
void sfc::lock()
{
    // Lock the controller mutex
    _controller_mutex.lock();
}

//----------------------------------------------------------------------------
// unlock
//----------------------------------------------------------------------------
void sfc::unlock()
{
    // Unlock the controller mutex
    _controller_mutex.unlock();
}

//----------------------------------------------------------------------------
// knob_is_active
//----------------------------------------------------------------------------
bool sfc::knob_is_active(uint num)
{
    // If knob number is valid
    if (num < NUM_PHYSICAL_KNOBS)
    {
        // Return the active state for this knob
        return _mc_active[num];
    }
    return false;
}

//----------------------------------------------------------------------------
// request_knob_states
//----------------------------------------------------------------------------
int sfc::request_knob_states()
{
    int ret;

    // NOTE: Controller Mutex must be locked before calling this function

    // Return an error if not open
    if (_dev_handle == -1)
    {
        // Operation not permitted unless open first
        return -EPERM;
    }

    // Request the state of each knob
    std::memset(_mc_knob_state_requested, false, sizeof(_mc_knob_state_requested));
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {
        // Is this Motor Controller active?
        if (_mc_active[i])
        {
            // Request the knob state
            ret = _mc_request_knob_state(i);
            if (ret == 0)
            {
                // Request knob state requested OK
                _mc_knob_state_requested[i] = true;
            }
            else
            {
                // Request knob state FAILED
                // Note: Ignore the error as it is possible from time to time
                // that the knob is not responsive due to it being busy with
                // other motion tasks                
                //DEBUG_MSG("Request knob state: FAILED: " << ret);            
            }
        }
    }
    return 0;     
}

//----------------------------------------------------------------------------
// read_knob_states
//----------------------------------------------------------------------------
int sfc::read_knob_states(sfc::KnobState *states)
{
    sfc::KnobState state = {};
    int ret;

    // NOTE: Controller Mutex must be locked before calling this function

    // Return an error if not open
    if (_dev_handle == -1)
    {
        // Operation not permitted unless open first
        return -EPERM;
    }

    // Read the state of each knob
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {
        // Is this Motor Controller active and has the knob state been requested?
        if (_mc_active[i] && _mc_knob_state_requested[i])
        {        
            // Read the knob state
            ret = _mc_read_knob_state(i, &state);
            if (ret == 0)
            {
                // Return the knob state
                *states = state;
            }
            else
            {
                // Read knob state FAILED
                // Note: Ignore the error as it is possible from time to time
                // that the knob is not responsive due to it being busy with
                // other motion tasks
                //DEBUG_MSG("Read knob state: FAILED: " << ret);
            }
        }
        states++;
    }
    return 0;
}

//----------------------------------------------------------------------------
// read_switch_states
//----------------------------------------------------------------------------
int sfc::read_switch_states(bool *states)
{
    uint8_t read_switch_states[NUM_SWITCH_BYTES];
    uint8_t switch_states[NUM_SWITCH_BYTES];
    uint8_t *switch_states_ptr = switch_states;
    int ret;

    // NOTE: Controller Mutex must be locked before calling this function

    // Return an error if not open
    if (_dev_handle == -1)
    {
        // Operation not permitted unless open first
        return -EPERM;
    }

    // Is the Panel Controller active?
    if (_pc_active)
    {
        // Read the Switch States position
        ret = _pc_read_switch_states(read_switch_states);
        if (ret < 0)
        {
            // Read switch states failed
            //DEBUG_MSG("Read Panel Controller switch states: FAILED: " << ret);
            return ret;
        }

        // Byte swap this array
        switch_states[0] = read_switch_states[5];
        switch_states[1] = read_switch_states[4];
        switch_states[2] = read_switch_states[3];
        switch_states[3] = read_switch_states[2];
        switch_states[4] = read_switch_states[1];
        switch_states[5] = read_switch_states[0];

        // Return the switch values as booleans
        int bit_pos = 0;
        uint8_t states_byte = *switch_states_ptr;
        for (uint i=0; i<NUM_PHYSICAL_SWITCHES; i++)
        {
            // Set the switch state as a bool
            *states++ = (states_byte & (1 << bit_pos)) == 0 ? false : true;
            bit_pos++;

            // Have we parsed all bits in this byte?
            if (bit_pos >= NUM_BITS_IN_BYTE)
            {
                // Move to the next states byte, and reset the bit position
                states_byte = *(++switch_states_ptr);
                bit_pos = 0;

            }
        }
    }
    return 0;
}

//----------------------------------------------------------------------------
// set_switch_led_state
//----------------------------------------------------------------------------
int sfc::set_switch_led_state(unsigned int switch_num, bool led_on)
{
    // Return an error if not open
    if (_dev_handle == -1)
    {
        // Operation not permitted unless open first
        return -EPERM;
    }

    // Check the switch number is valid
    if (switch_num >= NUM_PHYSICAL_SWITCHES)
    {
        // Parameter is invalid
        return -EINVAL;
    }

    // Get the controller mutex
    std::lock_guard<std::mutex> lock(_controller_mutex);
    
    // Cache the LED state, we only commit it to hardware with the specific
    // commit function
    // This is done for efficiency to avoid multiple, sequential writes to the
    // hardware
    // Firstly get the array and bit position to set
    int array_pos = switch_num >> 3;
    int bit_pos = switch_num % NUM_BITS_IN_BYTE;

        // Check the array and bit positions are valid
    if ((array_pos > NUM_LED_BYTES) || (bit_pos >= NUM_BITS_IN_BYTE))
    {
        // This shouldn't really happen, but return an error anyway
        return -ENXIO;
    }

    // Check if we should set or clear the LED bit
    if (led_on)
    {
        // Set the LED bit
        _led_states[array_pos] |= (1 << bit_pos);
    }
    else
    {
        // Clear the LED bit
        _led_states[array_pos] &= ~(1 << bit_pos);
    }
    return 0;
}

//----------------------------------------------------------------------------
// set_all_switch_led_states
//----------------------------------------------------------------------------
void sfc::set_all_switch_led_states(bool leds_on)
{
    // Is the Panel Controller active?
    if (_pc_active)
    {    
        // Get the controller mutex
        std::lock_guard<std::mutex> lock(_controller_mutex);

        // Switching all LEDs on?
        if (leds_on)
        {
            // Set all bits ON
            for (uint i=0; i<NUM_LED_BYTES; i++)
                _led_states[i] = 0xFF;
        }
        else
        {
            // Set all bits OFF
            for (uint i=0; i<NUM_LED_BYTES; i++)
                _led_states[i] = 0x00;        
        }
    }
}

//----------------------------------------------------------------------------
// commit_led_states
//----------------------------------------------------------------------------
int sfc::commit_led_states()
{
    uint8_t led_states[NUM_LED_BYTES];
    int ret;

    // Is the Panel Controller active?
    if (_pc_active)
    {
        // Get the controller mutex
        std::lock_guard<std::mutex> lock(_controller_mutex);

        // Byte swap the LEDs array
        led_states[0] = _led_states[5];
        led_states[1] = _led_states[4];
        led_states[2] = _led_states[3];
        led_states[3] = _led_states[2];
        led_states[4] = _led_states[1];
        led_states[5] = _led_states[0];

        // Set the LED States
        ret = _pc_set_led_states(led_states);
        if (ret < 0)
        {
            // Set the LED States failed
            //DEBUG_MSG("Set Panel Controller LED states: FAILED");
            return ret;
        }
    }
    return 0;
}

//----------------------------------------------------------------------------
// set_at_scaling
//----------------------------------------------------------------------------
int sfc::set_at_scaling(uint8_t scaling)
{
    // Get the controller mutex
    std::lock_guard<std::mutex> lock(_controller_mutex);

    // Select the Panel Controller
    int ret = _select_pc();
    if (ret == 0) {
        // Latch the Pitch Wheel min
        uint8_t cmd[] = { PcRegMap::SET_AT_SCALE, scaling };
        ret = _i2c_pc_write(cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// latch_pitch_wheel_min
//----------------------------------------------------------------------------
int sfc::latch_pitch_wheel_min()
{
    // Get the controller mutex
    std::lock_guard<std::mutex> lock(_controller_mutex);

    // Select the Panel Controller
    int ret = _select_pc();
    if (ret == 0) {
        // Latch the Pitch Wheel min
        uint8_t cmd = PcRegMap::LATCH_PB_MIN;
        ret = _i2c_pc_write(&cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// latch_pitch_wheel_max
//----------------------------------------------------------------------------
int sfc::latch_pitch_wheel_max()
{
    // Get the controller mutex
    std::lock_guard<std::mutex> lock(_controller_mutex);
    
    // Select the Panel Controller
    int ret = _select_pc();
    if (ret == 0) {
        // Latch the Pitch Wheel max
        uint8_t cmd = PcRegMap::LATCH_PB_MAX;
        ret = _i2c_pc_write(&cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// latch_pitch_wheel_mid_1
//----------------------------------------------------------------------------
int sfc::latch_pitch_wheel_mid_1()
{
    // Get the controller mutex
    std::lock_guard<std::mutex> lock(_controller_mutex);
    
    // Select the Panel Controller
    int ret = _select_pc();
    if (ret == 0) {
        // Latch the Pitch Wheel mid (1)
        uint8_t cmd = PcRegMap::LATCH_PB_MID_1;
        ret = _i2c_pc_write(&cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// latch_pitch_wheel_mid_2
//----------------------------------------------------------------------------
int sfc::latch_pitch_wheel_mid_2()
{
    // Get the controller mutex
    std::lock_guard<std::mutex> lock(_controller_mutex);
    
    // Select the Panel Controller
    int ret = _select_pc();
    if (ret == 0) {
        // Latch the Pitch Wheel mid (2)
        uint8_t cmd = PcRegMap::LATCH_PB_MID_2;
        ret = _i2c_pc_write(&cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// latch_mod_wheel_min
//----------------------------------------------------------------------------
int sfc::latch_mod_wheel_min()
{
    // Get the controller mutex
    std::lock_guard<std::mutex> lock(_controller_mutex);
    
    // Select the Panel Controller
    int ret = _select_pc();
    if (ret == 0) {
        // Latch the Mod Wheel min
        uint8_t cmd = PcRegMap::LATCH_MW_MIN;
        ret = _i2c_pc_write(&cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// latch_mod_wheel_max
//----------------------------------------------------------------------------
int sfc::latch_mod_wheel_max()
{
    // Get the controller mutex
    std::lock_guard<std::mutex> lock(_controller_mutex);
    
    // Select the Panel Controller
    int ret = _select_pc();
    if (ret == 0) {
        // Latch the Mod Wheel min
        uint8_t cmd = PcRegMap::LATCH_MW_MAX;
        ret = _i2c_pc_write(&cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// save_pitch_mod_wheel_config
//----------------------------------------------------------------------------
int sfc::save_pitch_mod_wheel_config()
{
    // Select the Panel Controller
    int ret = _select_pc();
    if (ret == 0) {
        // Save the Pitch/Mod Wheel config
        uint8_t cmd = PcRegMap::FLASH_CONFIG_WRITE;
        ret = _i2c_pc_write(&cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// set_knob_haptic_mode
//----------------------------------------------------------------------------
int sfc::set_knob_haptic_mode(unsigned int knob_num, const sfc::HapticMode& haptic_mode)
{
    // Is this Motor Controller active?
    if (_mc_active[knob_num])
    {
        // Get the controller mutex
        std::lock_guard<std::mutex> lock(_controller_mutex);

        // Set the haptic mode (if any)
        int ret = _mc_set_haptic_mode(knob_num, haptic_mode);
        if (ret < 0)
        {
            // Set Motor Controller haptic mode failed
            DEBUG_MSG("Motor Controller " << knob_num << " set haptic mode: FAILED");
            return ret;
        }
    }
    return 0;
}

//----------------------------------------------------------------------------
// set_knob_position
//----------------------------------------------------------------------------
int sfc::set_knob_position(unsigned int knob_num, uint16_t position, bool robust)
{
    // Return an error if not open
    if (_dev_handle == -1)
    {
        // Operation not permitted unless open first
        return -EPERM;
    }

    // Check the Motor Controller is active
    if (_mc_active[knob_num])
    {
        // Get the controller mutex
        std::lock_guard<std::mutex> lock(_controller_mutex);

        // Set the knob position
        int ret = _mc_set_position(knob_num, position, robust);
        if (ret < 0)
        {
            // Set Motor Controller position failed
            DEBUG_MSG("Set Motor Controller position: FAILED");
            return ret;
        }
    }
    return 0;   
}

//----------------------------------------------------------------------------
// control_type_from_string
//----------------------------------------------------------------------------
sfc::ControlType sfc::control_type_from_string(const char *type) 
{ 
    // Return the Surface Control type from the string
    if (std::strcmp(type, KNOB_TYPE_STRING) == 0)
        return sfc::ControlType::KNOB;
    else if (std::strcmp(type, SWITCH_TYPE_STRING) == 0)
        return sfc::ControlType::SWITCH;			
    return sfc::ControlType::UNKNOWN;
}

//----------------------------------------------------------------------------
// _init_controllers
//----------------------------------------------------------------------------
void _init_controllers()
{
    uint8_t switch_states[NUM_SWITCH_BYTES];
    uint num_mcs_found = 0;
    bool mc_started[NUM_PHYSICAL_KNOBS] = { 0 };
    uint mc_cal_enc_params_retry_count[NUM_PHYSICAL_KNOBS];
    bool mc_enc_params_calibration_requested[NUM_PHYSICAL_KNOBS] = { 0 };
    bool mc_enc_params_calibration_resp_rx[NUM_PHYSICAL_KNOBS] = { 0 };
    bool mc_enc_params_calibrated[NUM_PHYSICAL_KNOBS] = { 0 };
    bool mc_find_datum_requested[NUM_PHYSICAL_KNOBS] = { 0 };
    bool mc_find_datum_calibrated[NUM_PHYSICAL_KNOBS] = { 0 };
    int ret;

    // Note!: For REV A boards, the panel controller is always present at address 100
    // For REV B+ boards, the panel controller is daisy chained with the motor controllers,
    // and needs the address set before starting

    // Attempt to read the Panel Controller switch states position (from address 100)
    ret = _pc_read_switch_states(switch_states);
    if (ret == 0) {
        // The panel is present and address valid
        // Always reboot the panel controller so that it is in a known state    
        ret = _pc_reboot();
        if (ret < 0) {
            // Reboot Motor Controller failed
            MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, "Reboot Panel Controller: FAILED");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));        
    }

    // Attempt to read the Panel Controller switch states position again (from address 100)
    ret = _pc_read_switch_states(switch_states);
    if (ret < 0) {
        // REV B+ Board - we need to set the panel controller address
        ret = _pc_set_addr();
        if (ret < 0) {
            // Set Panel Controller address failed
            // This should never happen if the Panel and Motor Controllers are installed
            // and working correctly
            // Return as its a critical error, motors will not be accessible
            MONIQUE_LOG_CRITICAL(MoniqueModule::SFC_CONTROL, "Set Panel Controller address: FAILED");
            return;
        }
    }

    // We need to now check if each Motor Controller has had its
    // address set, and if so reboot that motor controller
    // If the address is not set, or after the reboot, we then set the 
    // Motor Controller address
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++) {
        // Request the Motor Controller status
        ret = _mc_request_status(i);
        if (ret == 0) {
            // The request succeeded, so the Motor Controller address has been set
            // Reboot the the Motor Controller            
            ret = _mc_reboot(i);
            if (ret < 0) {
                // Reboot Motor Controller failed
                MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, "Reboot Motor Controller {} address: FAILED", (i + 1));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Set the Motor Controller address
        ret = _mc_set_addr(i);
        if (ret < 0) {
            // Set Motor Controller address failed - if this fails
            // then assume there are no more motors available
            // This should never happen if all Motor Controllers are installed
            // and working correctly
            MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, "Set Motor Controller {} address: FAILED", (i + 1));
            break;
        }

        // Increment the number of Motor Controllers found
        num_mcs_found++;
    }

    // Log the firmware version for the Panel Controller each Motor Controller found
    for (uint i=0; i<(1 + num_mcs_found); i++) {
        // Get the firmware version for the panel/motor controller
        uint8_t firmware_ver[FIRMWARE_VER_SIZE];
        ret = i == 0 ? _pc_get_firmware_ver(firmware_ver) : _mc_get_firmware_ver((i - 1), firmware_ver);
        if (ret == 0) {
            // Log the firmware version
            char fw_ver_str[(FIRMWARE_VER_SIZE*2)+1];
            std::sprintf(fw_ver_str, "%02X%02X%02X%02X%02X%02X%02X%02X%c%c%c%c%c%c%c%c",
                         firmware_ver[0], firmware_ver[1], firmware_ver[2], firmware_ver[3],
                         firmware_ver[4], firmware_ver[5], firmware_ver[6], firmware_ver[7],
                         (char)firmware_ver[8], (char)firmware_ver[9], (char)firmware_ver[10], (char)firmware_ver[11],
                         (char)firmware_ver[12], (char)firmware_ver[13], (char)firmware_ver[14], (char)firmware_ver[15]);
            i == 0 ?
                MONIQUE_LOG_INFO(MoniqueModule::SFC_CONTROL, "Panel Controller FW Ver: {}", fw_ver_str) :
                MONIQUE_LOG_INFO(MoniqueModule::SFC_CONTROL, "Motor Controller {} FW Ver: {}", i, fw_ver_str);
        }
        else {
            i == 0 ?
                MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, "Get Panel Controller FW Ver: FAILED") :
                MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, "Get Motor Controller {} FW Ver: FAILED", i); 
        }
    }

    // Start the Panel Controller
    ret = _start_pc();
    if (ret == 0) {
        // Panel Controller active
        _pc_active = true;
    }
    else {      
        // Start Panel Controller failed - this should never fail
        MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, "Start Panel Controller: FAILED");
    }

    // Go through and start each Motor Controller
    // Like the Panel Controller, we do this regardless of it's state (bootloader 
    // or started), as if it is already started the start request will simply 
    // be ignored    
    for (uint i=0; i<num_mcs_found; i++) {
        // Start the Motor Controller
        ret = _start_mc(i);
        if (ret ==0) {
            // Motor Controller started
            mc_started[i] = true;
        }
        else {
            // Could not start the Motor Controller - this should never fail
            MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, "Start Motor Controller {}: FAILED", (i + 1));
        }
    }

    // If any motor controllers were found
    if (num_mcs_found)  {
        // Once the motor controllers have been started, we need to request each
        // motor to calibrate its encoder params
        // Note: If the motor controller calibrate encoder params succeeds, but the find datum
        // fails, we retry the entire calibration process for that motor
        uint cal_retry_count = CAL_MOTORS_RETRY_COUNT;
        while (cal_retry_count--) {
            bool enc_params_cal_done = false;

            // Initialise the calibration retry counts
            for (uint i=0; i<num_mcs_found; i++) {
                // Reset the calibrate encoder params status for each motor that has not
                // been calibrated yet
                if (mc_started[i] && !mc_find_datum_calibrated[i]) {
                    // Reset the calibration status
                    mc_enc_params_calibration_requested[i] = false;
                    mc_enc_params_calibration_resp_rx[i] = false;
                    mc_enc_params_calibrated[i] = false;
                    mc_cal_enc_params_retry_count[i] = CAL_ENCODER_PARAMS_RETRY_COUNT;
                }
            }

            // Loop performing encoder params calibration
            while (!enc_params_cal_done) {
                // Loop and find motors that need to perform encoder params calibration
                for (uint i=0; i<num_mcs_found; i++) {
                    // Has this Motor Controller been started and not calibrated?
                    if (mc_started[i] && !mc_enc_params_calibrated[i]) {
                        // Request the Motor Controllor to calibrate its encoder params
                        ret = _mc_request_cal_enc_params(i);
                        if (ret == 0) {
                            // Motor Controller calibrate encoder params requested
                            mc_enc_params_calibration_requested[i] = true;                
                        }
                        else {
                            // Motor Controller could not accept the calibrate encoder params request - this should never fail
                            MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, "Request Motor Controller {} calibrate encoder params: FAILED", (i + 1));

                            // Decrement the retry count
                            if (mc_cal_enc_params_retry_count[i]) {
                                mc_cal_enc_params_retry_count[i]--;
                            }                  
                        }

                        // Wait 50ms before kicking off the next motor
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                }

                // Loop checking each Motor Controller calibrate encoder params status, waiting for the
                // status to indicate the calibration is complete
                // Try for a maximum of 100 times before returning an error
                uint wait_count = 100;
                while (wait_count) {
                    // Read and check the calibrate encoder params status for each Motor Controller 
                    // NOT calibrated
                    for (uint i=0; i<num_mcs_found; i++) {
                        // If calibration requested and no response received?
                        if (mc_enc_params_calibration_requested[i] && !mc_enc_params_calibration_resp_rx[i] && !mc_enc_params_calibrated[i]) {
                            uint8_t cal_enc_params_status = 0xFF;

                            // Wait for the motor to complete calibration of its encoder params
                            if (_mc_check_cal_enc_params_status(i, &cal_enc_params_status) == 0) {
                                // Has this Motor Controller calibrated its encoder params?
                                if (cal_enc_params_status == CAL_ENCODER_PARAMS_OK) {
                                    // Yes, indicate it has now calibrated its encoder params
                                    mc_enc_params_calibrated[i] = true;                        
                                }
                                else {
                                    // This is an unexpected response, log an error
                                    MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, "Calibrate Motor Controller {} encoder params returned: {}", (i + 1), cal_enc_params_status);

                                    // Decrement the retry count
                                    if (mc_cal_enc_params_retry_count[i]) {
                                        mc_cal_enc_params_retry_count[i]--;
                                    }
                                }

                                // Response received
                                mc_enc_params_calibration_resp_rx[i] = true;
                            }
                        }
                    }

                    // Have all Motor Controllers received a calibrate encoder params response?              
                    if (std::memcmp(mc_enc_params_calibration_requested, 
                                    mc_enc_params_calibration_resp_rx, 
                                    sizeof(mc_enc_params_calibration_requested)) == 0) {
                        // Yep - break from this loop
                        break;
                    }

                    // Waiting for one or more Motor Controllers to become calibrated
                    // Decrement the wait count, and sleep for 50ms before checking again
                    wait_count--;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                // Are there any motors to retry?
                enc_params_cal_done = true;
                for (uint i=0; i<num_mcs_found; i++) {
                    // If the motor encoder params could not be calibrated AND we can retry
                    if (mc_started[i] && !mc_enc_params_calibrated[i] && mc_cal_enc_params_retry_count[i]) {
                        // Reset the encoder params calibration status
                        mc_enc_params_calibration_requested[i] = false;
                        mc_enc_params_calibration_resp_rx[i] = false;
                        enc_params_cal_done = false;
                    }
                }
            }

            // Log any Motor Controllers that could not calibrate their encoder params
            for (uint i=0; i<num_mcs_found; i++) {
                // Did they succeed?
                if (mc_started[i] && !mc_enc_params_calibrated[i]) {
                    MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, "Calibrate Motor Controller {} encoder params: FAILED", (i + 1));
                }
            }

            // Once the motor controllers have had their encoder params calibrated, we need to request each
            // motor to find it's datum point
            // Loop requesting each motor to do this
            for (uint i=0; i<num_mcs_found; i++) {
                // Has this Motor Controller had its encoder params calibrated and not found datum yet?
                if (mc_enc_params_calibrated[i] && !mc_find_datum_calibrated[i]) {
                    // Request the Motor Controllor to find it's datum point
                    mc_find_datum_requested[i] = false;
                    ret = _mc_request_find_datum(i);
                    if (ret == 0) {
                        // Motor Controller calibration requested
                        mc_find_datum_requested[i] = true;                
                    }
                    else {
                        // Motor Controller could not request it's datum - this should never fail
                        MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, "Request Motor Controller {} datum: FAILED", (i + 1));
                    }

                    // Wait 50ms before kicking off the next motor
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }

            // Loop checking each Motor Controller datum status, waiting for the
            // status to indicate the datum has been found
            // Try for a maximum of 100 times before returning an error
            uint wait_count = 100;
            while (wait_count) {
                // Read and check the datum status for each Motor Controller 
                // NOT calibrated
                for (uint i=0; i<num_mcs_found; i++) {
                    // If calibration requested and not calibrated?
                    if (mc_find_datum_requested[i] && !mc_find_datum_calibrated[i]) {            
                        uint8_t datum_status = 0;

                        // Read the datum status
                        // If the read fails ignore it as it may be busy still finding
                        // the datum
                        if (_mc_read_find_datum_status(i, &datum_status) == 0) {
                            // Has this Motor Controller found the datum OK?
                            if (datum_status == CALIBRATION_STATUS_DATUM_FOUND) {
                                // Yes, indicate it is now calibrated and active
                                mc_find_datum_calibrated[i] = true;
                                _mc_active[i] = true;
                            }
                        }
                    }
                }

                // Have all Motor Controllers tried to find datum?
                if (std::memcmp(mc_find_datum_requested, 
                                mc_find_datum_calibrated, 
                                sizeof(mc_find_datum_calibrated)) == 0) {
                    // Yep - break from this loop, calibration is complete
                    break;
                }

                // Waiting for one or more Motor Controllers to find datum
                // Decrement the wait count, and sleep for 50ms before checking again
                wait_count--;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            // Log any Motor Controllers that could find datum
            auto motors_ok = true;
            for (uint i=0; i<num_mcs_found; i++) {
                // Not calibrated?
                if (mc_started[i] && !mc_find_datum_calibrated[i]) {
                    motors_ok = false;
                    MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, "Calibrate Motor Controller {} find datum: FAILED", (i + 1));
                }
            }
            if (motors_ok)
                break;
        }
    }
}

//----------------------------------------------------------------------------
// _mc_request_status
//----------------------------------------------------------------------------
int _mc_request_status(uint8_t mc_num)
{
    // Select the specific Motor Controller
    int ret = _select_mc(mc_num);
    if (ret == 0)
    {
        uint8_t cmd = CommonRegMap::CONFIG_DEVICE;

        // Request the controller slave status - robust write
        ret = _i2c_mc_robust_write(&cmd, sizeof(cmd), true, (_selected_controller_addr + CONFIG_DEVICE_SCL_LOOP_OUT));
    }
    return ret;
}

//----------------------------------------------------------------------------
// _mc_reboot
//----------------------------------------------------------------------------
int _mc_reboot(uint8_t mc_num)
{
    // Select the specific Motor Controller
    int ret = _select_mc(mc_num);
    if (ret == 0)
    {
        // Reboot the controller
        uint8_t cmd = McRegMap::MC_REBOOT;
        ret = _i2c_mc_write(&cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// _mc_set_addr
//----------------------------------------------------------------------------
int _mc_set_addr(uint8_t mc_num)
{
    // Select the default Motor Controller
    int ret = _select_mc_default();
    if (ret  == 0)
    {
        uint8_t cmd[] = { CommonRegMap::CONFIG_DEVICE, 
                          (uint8_t)(MC_BASE_I2C_SLAVE_ADDR + mc_num + CONFIG_DEVICE_SCL_LOOP_OUT) };

        // Configure the I2C slave address - robust write
        ret = _i2c_mc_robust_write(cmd, sizeof(cmd), false);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _mc_get_firmware_ver
//----------------------------------------------------------------------------
int _mc_get_firmware_ver(uint8_t mc_num, uint8_t *ver)
{
    // Select the specific Motor Controller
    int ret = _select_mc(mc_num);
    if (ret == 0)
    {
        uint8_t cmd = CommonRegMap::CHECK_FIRMWARE;

        // Request the controller firmware version
        ret = _i2c_mc_write(&cmd, sizeof(cmd));
        if (ret == 0)
        {
            // Read the firmware version
            ret = _i2c_mc_read(ver, FIRMWARE_VER_SIZE);
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _pc_reboot
//----------------------------------------------------------------------------
int _pc_reboot()
{
    // Select the Panel Controller
    int ret = _select_pc();
    if (ret == 0) {
        // Reboot the controller
        uint8_t cmd = PcRegMap::PC_REBOOT;
        ret = _i2c_pc_write(&cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// _pc_set_addr
//----------------------------------------------------------------------------
int _pc_set_addr()
{
    // Select the default Motor Controller address
    int ret = _select_mc_default();
    if (ret  == 0) {
        uint8_t cmd[] = { CommonRegMap::CONFIG_DEVICE, 
                          (uint8_t)(PC_I2C_SLAVE_ADDR + CONFIG_DEVICE_SCL_LOOP_OUT) };

        // Configure the I2C slave address - robust write
        ret = _i2c_pc_robust_write(cmd, sizeof(cmd), false);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _pc_get_firmware_ver
//----------------------------------------------------------------------------
int _pc_get_firmware_ver(uint8_t *ver)
{
    // Select the specific Motor Controller
    int ret = _select_pc();
    if (ret == 0) {
        uint8_t cmd = CommonRegMap::CHECK_FIRMWARE;

        // Request the controller firmware version
        ret = _i2c_mc_write(&cmd, sizeof(cmd));
        if (ret == 0) {
            // Read the firmware version
            ret = _i2c_mc_read(ver, FIRMWARE_VER_SIZE);
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _start_pc
//----------------------------------------------------------------------------
int _start_pc()
{
    // Select the Panel Controller
    int ret = _select_pc();
    if (ret == 0) {
        uint8_t cmd = BootloaderRegMap::START_FIRMWARE;

        // Start the Panel Controller - robust write
        ret = _i2c_pc_robust_write(&cmd, sizeof(cmd), false, 0);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _start_mc
//----------------------------------------------------------------------------
int _start_mc(uint8_t mc_num)
{
    // Select the Motor Controller
    int ret = _select_mc(mc_num);
    if (ret == 0) {
        uint8_t cmd = BootloaderRegMap::START_FIRMWARE;

        // Start the Motor Controller - robust write
        ret = _i2c_pc_robust_write(&cmd, sizeof(cmd), false, 0);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _mc_request_cal_enc_params
//----------------------------------------------------------------------------
int _mc_request_cal_enc_params(uint8_t mc_num)
{
    // Select the specific Motor Controller
    int ret = _select_mc(mc_num);
    if (ret == 0)
    {
        uint8_t cmd = McRegMap::CAL_ENC_PARAMS;
        
        // Request the Motor Controllor to calibrate its encoder params - robust write
        // with no readback, as this is done later using the command below
        ret = _i2c_mc_robust_write(&cmd, sizeof(cmd), false);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _mc_check_cal_enc_params_status
//----------------------------------------------------------------------------
int _mc_check_cal_enc_params_status(uint8_t mc_num, uint8_t *status)
{
    // Select the specific Motor Controller
    int ret = _select_mc(mc_num);
    if (ret == 0)
    {
        uint8_t resp;

        // Read the Motor Controller calibrate encoder params status
        ret = _i2c_mc_read(&resp, sizeof(resp));
        if (ret == 0)
        {
            // Return the datum status
            *status = resp;
        }        
    }
    return ret;
}

//----------------------------------------------------------------------------
// _mc_request_find_datum
//----------------------------------------------------------------------------
int _mc_request_find_datum(uint8_t mc_num)
{
    // Select the specific Motor Controller
    int ret = _select_mc(mc_num);
    if (ret == 0)
    {
        uint8_t cmd = McRegMap::MOTION_MODE_FIND_DATUM;
        
        // Request the Motor Controllor to find it's datum point - robust write
        // with no readback, as this is done later using the command below
        ret = _i2c_mc_robust_write(&cmd, sizeof(cmd), false);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _mc_read_find_datum_status
//----------------------------------------------------------------------------
int _mc_read_find_datum_status(uint8_t mc_num, uint8_t *status)
{
    // Select the specific Motor Controller
    int ret = _select_mc(mc_num);
    if (ret == 0)
    {
        uint8_t resp;

        // Read the Motor Controller datum status
        ret = _i2c_mc_read(&resp, sizeof(resp));
        if (ret == 0)
        {
            // Return the datum status
            *status = resp;
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _mc_set_haptic_mode
//----------------------------------------------------------------------------
int _mc_set_haptic_mode(uint8_t mc_num, const sfc::HapticMode& haptic_mode)
{
    // Select the specific Motor Controller
    int ret = _select_mc(mc_num);
    if (ret == 0)
    {
        // Are we actually changing the haptic mode?
        if (haptic_mode.name != _mc_haptic_mode[mc_num])
        {        
            // If knob haptics are switched on
            if (haptic_mode.knob_haptics_on())
            {
                auto knob_width = haptic_mode.knob_width;
                uint8_t config_cmd[HAPTIC_CONFIG_MIN_NUM_BYTES + (HAPTIC_KNOB_MAX_NUM_INDENTS * sizeof(uint16_t))] = {};
                uint8_t config_cmd_len = HAPTIC_CONFIG_MIN_NUM_BYTES;
                uint8_t mode_cmd[] = { McRegMap::MOTION_MODE_HAPTIC, 0x01};                      
                uint8_t *cmd_ptr = &config_cmd[sizeof(uint8_t)];
                uint8_t detent_strength = 0x00;            
                uint16_t start_pos;
                uint16_t width;
                uint8_t num_indents = 0;

                // Calculate the number of indents to set in hardware
                if (haptic_mode.knob_indents.size()) {
                    for (uint i=0; i<haptic_mode.knob_indents.size(); i++)
                    {
                        // If this indent is active in hardware
                        if (haptic_mode.knob_indents[i].first) {
                            num_indents++;
                        }
                    }
                }               

                // Setup the config command byte
                config_cmd[0] = McRegMap::MOTION_HAPTIC_CONFIG;

                // Has the width benn specified?
                if (knob_width < 360)
                {
                    // The width must be within the specified haptic range
                    // Clip to these values                
                    if (knob_width < HAPTIC_KNOB_MIN_WIDTH)
                        knob_width = HAPTIC_KNOB_MIN_WIDTH;
                    else if (knob_width > HAPTIC_KNOB_MAX_WIDTH)
                        knob_width = HAPTIC_KNOB_MAX_WIDTH;
                }

                // Have detents been specified?
                if (haptic_mode.knob_num_detents)
                {
                    // Set the detent strength
                    detent_strength = haptic_mode.knob_detent_strength;
                }

                // Has the knob start pos been specified?
                if (haptic_mode.knob_start_pos != -1)
                {
                    // Make sure the start pos and width do not exceed the knob limits
                    if ((haptic_mode.knob_start_pos + knob_width) > 360)
                    {
                        // Truncate the knob width
                        knob_width = 360 - haptic_mode.knob_start_pos;
                    }

                    // Calculate the start position
                    start_pos = (haptic_mode.knob_start_pos / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
                }
                else
                {
                    // Calculate the start position
                    start_pos = (((360.0f - knob_width) / 2) / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
                }

                // Calculate the width
                width = (knob_width / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;

                // Clip the number of indents if needed
                if (num_indents > HAPTIC_KNOB_MAX_NUM_INDENTS)
                    num_indents = HAPTIC_KNOB_MAX_NUM_INDENTS;

                // Setup the command data
                *cmd_ptr++ = (uint8_t)haptic_mode.knob_friction;        // Friction
                *cmd_ptr++ = (uint8_t)haptic_mode.knob_num_detents;     // Number of detents
                *cmd_ptr++ = detent_strength;                           // Detent strength
                *cmd_ptr++ = (uint8_t)(start_pos & 0xFF);               // Start pos (LSB)
                *cmd_ptr++ = (uint8_t)(start_pos >> 8);                 // Start pos (MSB)
                *cmd_ptr++ = (uint8_t)(width & 0xFF);                   // Width (LSB)
                *cmd_ptr++ = (uint8_t)(width >> 8);                     // Width (MSB)
                *cmd_ptr++ = num_indents;                               // Number of indents

                // Copy the indents, if any
                if (num_indents)
                {
                    // Process each indent
                    for (uint i=0; i<num_indents; i++)
                    {
                        // If this indent is active in hardware
                        if (haptic_mode.knob_indents[i].first) {
                            // Set the indent in the command data
                            *cmd_ptr++ = (uint8_t)(haptic_mode.knob_indents[i].second & 0xFF);
                            *cmd_ptr++ = (uint8_t)(haptic_mode.knob_indents[i].second >> 8);
                            config_cmd_len += sizeof(uint16_t);
                        }
                    }
                }

                // Set the haptic config - robust write
                ret = _i2c_mc_robust_write(config_cmd, config_cmd_len, true, config_cmd[sizeof(uint8_t)]);

                // Was the haptic config successfully set?
                if (!_mc_haptic_set[mc_num] && ret == 0)
                {
                    // Once the config has been set, enable haptic mode in the Motor Controller
                    // Use a robust write
                    ret = _i2c_mc_robust_write(mode_cmd, sizeof(mode_cmd), true, mode_cmd[sizeof(uint8_t)]);
                    _mc_haptic_set[mc_num] = true;
                }

                // Was the haptic mode correctly set and enabled if required?
                if (ret == 0)
                {
                    // Save the haptic details
                    _mc_haptic_mode[mc_num] = haptic_mode.name;
                    _mc_haptic_set[mc_num] = true;
                }
            }
            else
            {
                // Disable the haptic mode
                uint8_t mode_cmd[] = { McRegMap::MOTION_MODE_HAPTIC, 0x00};
                ret = _i2c_mc_robust_write(mode_cmd, sizeof(mode_cmd), true, mode_cmd[sizeof(uint8_t)]);

                // Was the haptic mode correctly disabled?
                if (ret == 0)
                {
                    // Save the haptic details
                    _mc_haptic_mode[mc_num] = haptic_mode.name;
                    _mc_haptic_set[mc_num] = false;
                }
            }
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _mc_request_knob_state
//----------------------------------------------------------------------------
int _mc_request_knob_state(uint8_t mc_num)
{
    // Select the specific Motor Controller
    int ret = _select_mc(mc_num);
    if (ret == 0)
    {
        uint8_t cmd = McRegMap::MOTOR_STATUS;

        // Request the knob state
        ret = _i2c_mc_write(&cmd, sizeof(cmd));        
    }
    return ret;
}

//----------------------------------------------------------------------------
// _mc_read_knob_state
//----------------------------------------------------------------------------
int _mc_read_knob_state(uint8_t mc_num, sfc::KnobState *state)
{
    // Select the specific Motor Controller
    int ret = _select_mc(mc_num);
    if (ret == 0)
    {
        uint16_t resp[MOTOR_STATUS_RESP_NUM_WORDS];

        // Read the knob status
        ret = _i2c_mc_read(resp, sizeof(resp));
        if (ret == 0)
        {
            // Make sure the response is valid
            if (resp[0] > FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR)
            {
                // The response is not valid
                return -EIO;
            }

            // Return the position and state
            state->position = resp[0];
            state->state = resp[1] & (sfc::KnobState::STATE_MOVING_TO_TARGET+sfc::KnobState::STATE_TAP_DETECTED);
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _mc_set_position
//----------------------------------------------------------------------------
int _mc_set_position(uint8_t mc_num, uint16_t position, bool robust)
{
    // Select the specific Motor Controller
    int ret = _select_mc(mc_num);
    if (ret == 0)
    {    
        uint8_t cmd[] = { McRegMap::MOTION_MODE_POSITION, 0, 0};

        // Set the motor position
        *(uint16_t *)&cmd[sizeof(uint8_t)] = position;

        // Write the command - robust write if requested
        if (robust)
            ret = _i2c_mc_robust_write(&cmd, sizeof(cmd), true, cmd[sizeof(uint8_t)]);
        else
            ret = _i2c_mc_write(&cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// _pc_read_switch_states
//----------------------------------------------------------------------------
int _pc_read_switch_states(uint8_t *switch_states)
{
    // Select the Panel Controller
    int ret = _select_pc();
    if (ret == 0)
    {    
        // Read the switch states
        ret = _i2c_pc_read(switch_states, NUM_SWITCH_BYTES);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _pc_set_led_states
//----------------------------------------------------------------------------
int _pc_set_led_states(uint8_t *led_states)
{
    // Select the Panel Controller
    int ret = _select_pc();
    if (ret == 0)
    {    
        uint8_t cmd[] = { PcRegMap::LED_STATE, 0, 0, 0, 0, 0, 0};

        // Copy the LED states to set
        memcpy(&cmd[sizeof(uint8_t)], led_states, NUM_LED_BYTES);

        // Write the command
        ret = _i2c_pc_write(cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// _select_mc_default
//----------------------------------------------------------------------------
int _select_mc_default()
{
    // Set the selected controller address
    _selected_controller_addr = MC_DEFAULT_I2C_SLAVE_ADDR;

    // Select the default Motor Controller slave
    return _i2c_select_slave(_selected_controller_addr);
}

//----------------------------------------------------------------------------
// _select_mc
//----------------------------------------------------------------------------
int _select_mc(uint8_t num)
{
    // Set the selected controller address
    _selected_controller_addr = MC_BASE_I2C_SLAVE_ADDR + num;

    // Select the specific Motor Controller I2C slave
    return _i2c_select_slave(_selected_controller_addr);
}

//----------------------------------------------------------------------------
// _select_pc
//----------------------------------------------------------------------------
int _select_pc()
{
    // Set the selected controller address
    _selected_controller_addr = PC_I2C_SLAVE_ADDR;
    
    // Select the Panel Controller I2C slave
    return _i2c_select_slave(PC_I2C_SLAVE_ADDR);
}

//------------------------
// Low-level I2C functions
//------------------------

//----------------------------------------------------------------------------
// _i2c_select_slave
//----------------------------------------------------------------------------
int _i2c_select_slave(uint8_t addr)
{
    // Select the requested I2C slave
    int ret = ioctl(_dev_handle, I2C_SLAVE, addr);
    if (ret < 0) {
        // Select save failed
        return -errno;
    }
    return 0;
}

//----------------------------------------------------------------------------
// _i2c_pc_read
//----------------------------------------------------------------------------
inline int _i2c_pc_read(void *buf, size_t buf_len)
{
    // No max packet size, just read the data in a single read
    return _i2c_read(buf, buf_len, buf_len);
}

//----------------------------------------------------------------------------
// _i2c_mc_read
//----------------------------------------------------------------------------
inline int _i2c_mc_read(void *buf, size_t buf_len)
{
#ifdef I2C_MC_READ_USE_PACKET_SIZE
    // Read the data in packets if needed
    return _i2c_read(buf, buf_len, I2C_MC_READ_MAX_PACKET_SIZE);
#else
    // No max packet size, just read the data in a single read
    return _i2c_read(buf, buf_len, buf_len);
#endif
}

//----------------------------------------------------------------------------
// _i2c_pc_robust_write
//----------------------------------------------------------------------------
inline int _i2c_pc_robust_write(const void *buf, size_t buf_len, bool readback, uint8_t readback_value)
{
    // Write the data in packets if needed
    return _i2c_robust_write(buf, buf_len, I2C_PC_WRITE_MAX_PACKET_SIZE, readback, readback_value);
}

//----------------------------------------------------------------------------
// _i2c_mc_robust_write
//----------------------------------------------------------------------------
inline int _i2c_mc_robust_write(const void *buf, size_t buf_len, bool readback, uint8_t readback_value)
{
    // No max packet size, just write the data in a single write
    return _i2c_robust_write(buf, buf_len, buf_len, readback, readback_value);
}

//----------------------------------------------------------------------------
// _i2c_pc_write
//----------------------------------------------------------------------------
inline int _i2c_pc_write(const void *buf, size_t buf_len)
{
    // Write the data in packets if needed
    return _i2c_write(buf, buf_len, I2C_PC_WRITE_MAX_PACKET_SIZE);
}

//----------------------------------------------------------------------------
// _i2c_mc_write
//----------------------------------------------------------------------------
inline int _i2c_mc_write(const void *buf, size_t buf_len)
{
    // No max packet size, just write the data in a single write
    return _i2c_write(buf, buf_len, buf_len);
}

//----------------------------------------------------------------------------
// _i2c_read
//----------------------------------------------------------------------------
int _i2c_read(void *buf, size_t buf_len, size_t packet_len) 
{
    uint8_t *buf_ptr = static_cast<uint8_t *>(buf);
    size_t len = buf_len > packet_len ? packet_len : buf_len;
    uint retry_count = I2C_READ_RETRY_COUNT;
    int ret = 0;

    // While there is data to read
    while ((buf_len > 0) && (ret == 0)) {
        // Reset the retry count and read the next byte
        retry_count = I2C_READ_RETRY_COUNT;
        while (retry_count--) {
            // Read bytes from the I2C slave
            ret = read(_dev_handle, buf_ptr, len);
            if (ret >= 0) {
                // Were the required number of bytes read?
                if ((size_t)ret == len) {
                    // Read was successful
                    ret = 0;               
                }
                else {
                    // The number of required bytes were not returned, this
                    // is treated as a read error
                    ret = -EIO;                 
                }
                break;
            }
            else
            {
                // Set the return value to -errno in case we need to return
                ret = -errno;

                // Did a timeout occur?
                if (errno == ETIMEDOUT) {
                    // If a timeout occurred then there is no I2C contact
                    // with the device, and we can stop trying the read               
                    break;
                }          
            }
        }

        // If the byte read was successful
        if (ret == 0) {
            // Increment the buffer pointer and decrement the buffer len for
            // the next read, if any
            buf_ptr += len;
            buf_len -= len;
            len = buf_len > packet_len ? packet_len : buf_len;

            // Sleep between each read (not the last read)
            if (buf_len > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(I2C_MC_READ_PACKET_DELAY_US));
            }
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _i2c_robust_write
//----------------------------------------------------------------------------
int _i2c_robust_write(const void *buf, size_t buf_len, size_t packet_len, bool readback, uint8_t readback_value) 
{
    uint8_t resp;
    int ret;

    // A robust write tries the write for a maximum of X times with a 1ms delay
    // between each attempt
    // Note this does not include the Y retry for the standard I2C write
    uint retry_count = I2C_ROBUST_WRITE_RETRY_COUNT;
    while (retry_count--) {
        // Write the command
        ret = _i2c_write(buf, buf_len, packet_len);

        // Was the write successful?
        if (ret == 0) {
            // The write succeeded, do we now need to perform
            // a read-back to ensure it was processed?
            if (readback) {
                // Perform a read-back, with retries
                uint readback_retry_count = I2C_ROBUST_WRITE_RETRY_COUNT;
                while (readback_retry_count--) {
                    // The Motor Controller always returns data as part of its protocol
                    // For efficiency, just check the first byte returned is as expected
                    ret = _i2c_read(&resp, sizeof(resp), sizeof(resp));
                    if ((ret == 0) && (resp == readback_value)) {
                        // Read-back succeeded
                        break;
                    }

                    // Sleep for 1ms before trying again
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

                // If we could not read-back the expected byte of data
                if ((ret == 0) && (resp != readback_value)) {
                    // Indicate an error
                    ret = -EIO;
                }
            }
            
            // Did the read-back (if performed) succeed?
            if (ret == 0) {
                // Robust write succeeded
                break;
            }
            
        }
        // Did a timeout occur? If so it means that there is no communications
        // with the controller, most likely because it does not exist or has failed
        else if (ret == -ETIMEDOUT) {
            // Stop attempting the write
            break;
        }

        // Sleep for 1ms before trying again (if possible)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // If the robust write failed, add an entry to the Monique log
    if (ret != 0) {
        // Log the error
        MONIQUE_LOG_ERROR(MoniqueModule::SFC_CONTROL, 
                          "I2C robust write failed, address {}, command: {:02d}, ret: {}", _selected_controller_addr, (int)*(uint8_t *)buf, ret); 
    }  
    return ret;
}

//----------------------------------------------------------------------------
// _i2c_write
//----------------------------------------------------------------------------
int _i2c_write(const void *buf, size_t buf_len, size_t packet_len) 
{
    const uint8_t *buf_ptr = static_cast<const uint8_t *>(buf);
    size_t len = buf_len > packet_len ? packet_len : buf_len;
    uint retry_count = I2C_WRITE_RETRY_COUNT;
    int ret = 0;

    // While there is data to write
    while ((buf_len > 0) && (ret == 0)) {
        // Perform the I2C write with retries
        retry_count = I2C_WRITE_RETRY_COUNT;
        while (retry_count--) {
            // Write the bytes to the I2C device
            ret = write(_dev_handle, buf_ptr, len);
            if (ret >= 0) {
                // Were the required number of bytes written?
                if ((size_t)ret == len) {
                    // Write was successful
                    ret = 0;              
                }
                else {
                    // The number of required bytes were not written, this
                    // is treated as a write error
                    ret = -EIO;                
                }
                break;
            }
            else {
                // Set the return value to -errno in case we need to return
                ret = -errno;

                // Did a timeout occur?
                if (errno == ETIMEDOUT)
                {
                    // If a timeout occurred then there is no I2C contact
                    // with the device, and we can stop trying the write             
                    break;
                }
            }
        }

        // If the write was successful
        if (ret == 0) {
            // Increment the buffer pointer and decrement the buffer len for
            // the next packet, if any
            buf_ptr += len;
            buf_len -= len;
            len = buf_len > packet_len ? packet_len : buf_len;
        }        
    }
    return ret; 
}
