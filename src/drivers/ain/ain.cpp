/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  ain.cpp
 * @brief Analog Input (AIN) driver implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <fstream>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
extern "C" {
#include "interface/vmcs_host/vc_vchi_gencmd.h"
#include "interface/vmcs_host/vc_gencmd_defs.h"
}
#include "ui_common.h"
#include "logger.h"
#include "ain.h"

// ADCs I2C bus
#define ADCS_I2C_BUS_NUM    "0"
#define ADCS_DEV_NAME       "/dev/i2c-" ADCS_I2C_BUS_NUM

// Constants
constexpr uint8_t ADC1_I2C_SLAVE_ADDR                           = 24;
constexpr uint8_t ADC2_I2C_SLAVE_ADDR                           = 27;
constexpr uint I2C_READ_RETRY_COUNT                             = 5;
constexpr uint I2C_WRITE_RETRY_COUNT                            = 5;
constexpr uint8_t ADC_PAGE_CONTROL_REG                          = 0;
constexpr uint8_t SW_RESET_REG                                  = 1;
constexpr uint8_t CLOCK_GEN_MULTIPLEXING_REG                    = 4;
constexpr uint8_t ADC_NADC_CLOCK_DIVIDER_REG                    = 18;
constexpr uint8_t ADC_MADC_CLOCK_DIVIDER_REG                    = 19;
constexpr uint8_t ADC_AOSR_REG                                  = 20;
constexpr uint8_t DITHER_CONTROL_REG                            = 26;
constexpr uint8_t ADC_AUDIO_INTERFACE_CONTROL1_REG              = 27;
constexpr uint8_t ADC_DIGITAL_REG                               = 81;
constexpr uint8_t ADC_FINE_VOLUME_CONTROL_REG                   = 82;
constexpr uint8_t LEFT_ADC_VOLUME_CONTROL_REG                   = 83;
constexpr uint8_t RIGHT_ADC_VOLUME_CONTROL_REG                  = 84;
constexpr uint8_t LEFT_ADC_INPUT_SELECTION_FOR_LEFT_PGA_1_REG   = 52;
constexpr uint8_t LEFT_ADC_INPUT_SELECTION_FOR_LEFT_PGA_2_REG   = 54;
constexpr uint8_t RIGHT_ADC_INPUT_SELECTION_FOR_RIGHT_PGA_1_REG = 55;
constexpr uint8_t RIGHT_ADC_INPUT_SELECTION_FOR_RIGHT_PGA_2_REG = 57;
constexpr uint8_t LEFT_ANALOG_PGA_SETTINGS_REG                  = 59;
constexpr uint8_t RIGHT_ANALOG_PGA_SETTINGS_REG                 = 60;
constexpr char VC_CMD_READ_AIN0_FORMAT[]                        = "pmicrd 1d";
constexpr char VC_RESP_READ_AIN0_FORMAT[]                       = "[1d] = %x";
constexpr char VC_CMD_READ_AIN1_FORMAT_CM4_REV0[]               = "pmicrd 1c";
constexpr char VC_RESP_READ_AIN1_FORMAT_CM4_REV0[]              = "[1c] = %x";
constexpr char VC_CMD_READ_AIN1_FORMAT_CM4_REV1_PLUS[]          = "pmicrd 12";
constexpr char VC_RESP_READ_AIN1_FORMAT_CM4_REV1_PLUS[]         = "[12] = %x";
constexpr float AIN1_SCALING_FACTOR_CM4_REV0                    = (255.0f / 254.0);
constexpr float AIN1_SCALING_FACTOR_CM4_REV1_PLUS               = (255.0f / 124.0);

// Private data
std::mutex _mutex;
int _i2c_handle = -1;
bool _adc1_ok = false;
bool _adc2_ok = false;
VCHI_INSTANCE_T _vchi_inst;
VCHI_CONNECTION_T *_vchi_conn = nullptr;
bool _ain_init = false;
const char *_vc_cmd_read_ain1_format;
const char *_vc_resp_read_ain1_format;
float (*_normalise_ain1_value)(int val);

// Private functions
float _normalise_ain1_value_cm4_rev0(int val);
float _normalise_ain1_value_cm4_rev1_plus(int val);
void _config_adcs();
int _config_adc1();
int _config_adc2();
int _config_adc(uint8_t left_adc_input_sel1, uint8_t left_adc_input_sel2, uint8_t right_adc_input_sel1, uint8_t right_adc_input_sel2);
int _select_adc_slave(uint8_t addr);
int _write_adc_reg(uint8_t reg, uint8_t val);    
int _i2c_select_adc_slave(uint8_t addr);
int _i2c_read_adc(void *buf, size_t buf_len);
int _i2c_write_adc(const void *buf, size_t buf_len);

//----------------------------------------------------------------------------
// init
//----------------------------------------------------------------------------
void ain::init()
{
    // Open the I2C bus
    auto ret = ::open(ADCS_DEV_NAME, O_RDWR);
    if (ret < 0) {
        // An error occurred opening the I2C device
        MSG("ERROR: Could not open the ADCs I2C port");
        MONIQUE_LOG_CRITICAL(MoniqueModule::SYSTEM, "Could not open the ADCs I2C port: {}", ret);        
        return;
    }

    // Save the device handle
    _i2c_handle = ret;    

    // Configure Analog Input ADCs
    _config_adcs();

    // All Analog Input ADCs configured OK?
    if (_adc1_ok && _adc2_ok){
        // Log success
        DEBUG_MSG("Config Analog Input ADCs 1-2: OK");
        MONIQUE_LOG_INFO(MoniqueModule::SYSTEM, "Config Analog Input ADCs 1-2: OK");
    }
    else{
        // Log one or more failures
        if (_adc1_ok)
            MONIQUE_LOG_INFO(MoniqueModule::SYSTEM, "Config Analog Input ADC1: OK");
        else {
            MSG("ERROR: Config Analog Input ADC1: FAILED");
            MONIQUE_LOG_INFO(MoniqueModule::SYSTEM, "Config Analog Input ADC1: FAILED");
        }
        if (_adc2_ok)
            MONIQUE_LOG_INFO(MoniqueModule::SYSTEM, "Config Analog Input ADC2: OK");
        else {
            MSG("ERROR: Config Analog Input ADC2: FAILED");
            MONIQUE_LOG_INFO(MoniqueModule::SYSTEM, "Config Analog Input ADC2: FAILED");
        }
    }

    // Initialise the VCHI
    ret = ::vchi_initialise(&_vchi_inst);
    if (ret == 0) {
        // Create a VCHI connection
        ret = ::vchi_connect(nullptr, 0, _vchi_inst);
        if (ret == 0) {
            // Initialise the VCHI gencmd
            ::vc_vchi_gencmd_init(_vchi_inst, &_vchi_conn, 1);
            _ain_init = true;
        }
        else {
            // Could not create a VCHI connection, show an error
            MSG("ERROR: Could not create a VCHI connection for the Expression pedal");
        }
    }
    else {
        // Could not initialise the VCHI, show an error
        MSG("ERROR: Could not initialise VCHI for the Expression pedal");
        MONIQUE_LOG_CRITICAL(MoniqueModule::SYSTEM, "Could not initialise VCHI for the Expression pedal: {}", ret);
    }

    // Get the CPU Info and parse it to get the revision of CM4 being used
    // This matters as the AIN registers are different, and have different scaling
    // Assume the CM4 is Revision 1+
    _vc_cmd_read_ain1_format = VC_CMD_READ_AIN1_FORMAT_CM4_REV1_PLUS;
    _vc_resp_read_ain1_format = VC_RESP_READ_AIN1_FORMAT_CM4_REV1_PLUS;
    _normalise_ain1_value = _normalise_ain1_value_cm4_rev1_plus;
    std::string line; 
    std::ifstream file("/proc/cpuinfo");
    bool rev0 = false;
    while (std::getline(file, line)) {
        if (line.substr(0, (sizeof("Revision") - 1)) == "Revision") {
            // Strip any trailing whitespace
            auto end = line.find_last_not_of(" ");
            if (end != std::string::npos) {
                line = line.substr(0, end + 1);
            }
            
            // Get the revision - the last 6 characters of the string (hex()
            auto rev_str = line.substr((line.size() - 6), (line.size() - 1));
            auto rev = std::stoi(rev_str, nullptr, 16);
            
            // Mask off the last 4 bits - these specify the actual hardware revision
            if ((rev & 0x0F) == 0) {
                // Revision 0
                 _vc_cmd_read_ain1_format = VC_CMD_READ_AIN1_FORMAT_CM4_REV0;
                 _vc_resp_read_ain1_format = VC_RESP_READ_AIN1_FORMAT_CM4_REV0;
                 _normalise_ain1_value = _normalise_ain1_value_cm4_rev0;
                 rev0 = true;
            }
            break;
        }
    }

    // Show and log which CM4 was detected
    MSG("RPi CM4 Revision " << (rev0 ? "0 detected" : "1+ detected"));
    MONIQUE_LOG_INFO(MoniqueModule::SYSTEM, "RPi CM4 Revision {}", (rev0 ? "0 detected" : "1+ detected"));
    MONIQUE_LOG_FLUSH();
}

//----------------------------------------------------------------------------
// deinit
//----------------------------------------------------------------------------
void ain::deinit()
{
    // If initialised
    if (_ain_init) {
        // Stop the VC gencmd
        ::vc_gencmd_stop();

        //close the vchi connection
        ::vchi_disconnect(_vchi_inst);
    }
}

//----------------------------------------------------------------------------
// read_ain0
//----------------------------------------------------------------------------
bool ain::read_ain0(float& normalised_value)
{
    // If initialised
    if (_ain_init) {    
        char buffer[GENCMDSERVICE_MSGFIFO_SIZE];

        // Get the mutex
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Request to read the expression pedal value
        auto ret = ::vc_gencmd_send("%s", VC_CMD_READ_AIN0_FORMAT);
        if (ret == 0) {
            // Get the response
            ret = ::vc_gencmd_read_response(buffer, sizeof( buffer));
            if (ret == 0) {
                // Get the pedal value
                int value;
                if (std::sscanf(buffer, VC_RESP_READ_AIN0_FORMAT, &value) == 1) {
                    // Clamp the value and convert to a normalised float
                    value = std::clamp(value, 0, UINT8_MAX);
                    normalised_value = value / (float)UINT8_MAX;
                    return true;                   
                }
            }
            else {
                DEBUG_MSG("read_ain0 (vc_gencmd_read_response) failed: " << ret); 
            }            
        }
        else {
            DEBUG_MSG("read_ain0 (vc_gencmd_send) failed: " << ret); 
        }
    }
    return false;
}

//----------------------------------------------------------------------------
// read_ain1
//----------------------------------------------------------------------------
bool ain::read_ain1(float& normalised_value)
{
    // If initialised
    if (_ain_init) {     
        char buffer[GENCMDSERVICE_MSGFIFO_SIZE];

        // Get the mutex
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Request to read the expression pedal value
        auto ret = ::vc_gencmd_send("%s", _vc_cmd_read_ain1_format);
        if (ret == 0) {
            // Get the response
            ret = ::vc_gencmd_read_response(buffer, sizeof( buffer));
            if (ret == 0) {
                // Get the pedal value
                int value;
                if (std::sscanf(buffer, _vc_resp_read_ain1_format, &value) == 1) {
                    // Normalise the pedal value
                    normalised_value = _normalise_ain1_value(value);
                    return true;                   
                }
            }
            else {
                DEBUG_MSG("read_ain1 (vc_gencmd_read_response) failed: " << ret); 
            }            
        }
        else {
            DEBUG_MSG("read_ain1 (vc_gencmd_send) failed: " << ret); 
        }
    }
    return false;
}

//----------------------------------------------------------------------------
// _normalise_ain1_value_cm4_rev0
//----------------------------------------------------------------------------
float _normalise_ain1_value_cm4_rev0(int val)
{
    // Clamp the value and convert to a normalised float
    // Note: Emperical testing of this port indicates that the minimum value
    // returned is 1, even with a ground short
    // For this reason we scale the value so that the range is 0.0 - 1.0, with a
    // fail-safe if 0 is actually somehow returned by this port
    val = std::clamp(val, 0, UINT8_MAX);
    float nv = val > 0 ? ((val - 1) / (float)UINT8_MAX) * AIN1_SCALING_FACTOR_CM4_REV0 : 0.0f;
    return std::clamp(nv, 0.0f, 1.0f);
}

//----------------------------------------------------------------------------
// _normalise_ain1_value_cm4_rev1_plus
//----------------------------------------------------------------------------
float _normalise_ain1_value_cm4_rev1_plus(int val)
{
    // Clamp the value and convert to a normalised float
    // Note: Emperical testing of this port indicates that the maximum value
    // returned is 7C
    // For this reason we scale the value so that the range is 0.0 - 1.0
    val = std::clamp(val, 0, UINT8_MAX);
    float nv = std::clamp(((val / (float)UINT8_MAX) * AIN1_SCALING_FACTOR_CM4_REV1_PLUS), 0.0f, 1.0f);
    return std::clamp(nv, 0.0f, 1.0f);
}

//----------------------------------------------------------------------------
// _config_adcs
//----------------------------------------------------------------------------
void _config_adcs()
{
    // Configure the ADCs
    _adc1_ok = _config_adc1() == 0;
    _adc2_ok = _config_adc2() == 0;
}

//----------------------------------------------------------------------------
// _config_adc1
//----------------------------------------------------------------------------
int _config_adc1()
{
    int ret;

    // Select the ADC1 slave
    ret = _select_adc_slave(ADC1_I2C_SLAVE_ADDR);
    if (ret == 0) {
        // Configure the ADC1 - CH 1 LEFT, CH 2 RIGHT
        ret = _config_adc(0xFF, 0x0F, 0x3F, 0x3F);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _config_adc2
//----------------------------------------------------------------------------
int _config_adc2()
{
    int ret;

    // Select the ADC2 slave
    ret = _select_adc_slave(ADC2_I2C_SLAVE_ADDR);
    if (ret == 0) {
        // Configure ADC2 - MIX 1 LEFT, MIX 2 RIGHT
        ret = _config_adc(0xFC, 0x3F, 0xF3, 0x3F);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _config_adc
//----------------------------------------------------------------------------
int _config_adc(uint8_t left_adc_input_sel1, uint8_t left_adc_input_sel2, uint8_t right_adc_input_sel1, uint8_t right_adc_input_sel2)
{
    // Set page 0 registers
    if (int ret = _write_adc_reg(ADC_PAGE_CONTROL_REG, 0x00); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(SW_RESET_REG, 0x01); ret != 0)
        return ret;    
    if (int ret = _write_adc_reg(CLOCK_GEN_MULTIPLEXING_REG, 0x0C); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(ADC_NADC_CLOCK_DIVIDER_REG, 0x81); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(ADC_MADC_CLOCK_DIVIDER_REG, 0x84); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(ADC_AOSR_REG, 0x40); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(ADC_AUDIO_INTERFACE_CONTROL1_REG, 0xE0); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(ADC_DIGITAL_REG, 0xC0); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(ADC_FINE_VOLUME_CONTROL_REG, 0x00); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(LEFT_ADC_VOLUME_CONTROL_REG, 0x0A); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(RIGHT_ADC_VOLUME_CONTROL_REG, 0x0A); ret != 0)
        return ret;

    // Set page 1 registers
    if (int ret = _write_adc_reg(ADC_PAGE_CONTROL_REG, 0x01); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(DITHER_CONTROL_REG, 0xA9); ret != 0)
        return ret;        
    if (int ret = _write_adc_reg(LEFT_ADC_INPUT_SELECTION_FOR_LEFT_PGA_1_REG, left_adc_input_sel1); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(LEFT_ADC_INPUT_SELECTION_FOR_LEFT_PGA_2_REG, left_adc_input_sel2); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(RIGHT_ADC_INPUT_SELECTION_FOR_RIGHT_PGA_1_REG, right_adc_input_sel1); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(RIGHT_ADC_INPUT_SELECTION_FOR_RIGHT_PGA_2_REG, right_adc_input_sel2); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(LEFT_ANALOG_PGA_SETTINGS_REG, 0x00); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(RIGHT_ANALOG_PGA_SETTINGS_REG, 0x00); ret != 0)
        return ret;
    return 0;
}

//----------------------------------------------------------------------------
// _select_adc_slave
//----------------------------------------------------------------------------
int _select_adc_slave(uint8_t addr)
{
    // Select the ADC slave
    int ret = _i2c_select_adc_slave(addr);
    if (ret)
        DEBUG_MSG("Could not select the Analog Control ADC I2C slave: " << ret);
    return ret;
}

//----------------------------------------------------------------------------
// _write_adc_reg
//----------------------------------------------------------------------------
int _write_adc_reg(uint8_t reg, uint8_t val)
{
    uint8_t cmd[sizeof(uint8_t)*2];

    // Write the ADC register
    cmd[0] = reg;
    cmd[1] = val;
    int ret = _i2c_write_adc(cmd, sizeof(cmd));
    if (ret) 
        DEBUG_MSG("Error writing to the Analog Control ADC I2C slave: " << ret);
    return ret;
}

//------------------------
// Low-level I2C functions
//------------------------

//----------------------------------------------------------------------------
// _i2c_select_adc_slave
//----------------------------------------------------------------------------
int _i2c_select_adc_slave(uint8_t addr)
{
    // Select the requested I2C slave
    int ret = ioctl(_i2c_handle, I2C_SLAVE, addr);
    if (ret < 0) {
        // Select save failed
        return -errno;
    }
    return 0;
}

//----------------------------------------------------------------------------
// _i2c_read_adc
//----------------------------------------------------------------------------
int _i2c_read_adc(void *buf, size_t buf_len) 
{
    uint retry_count = I2C_READ_RETRY_COUNT;
    int ret;

    // Perform the I2C reads with retries
    while (retry_count--) {
        // Read the required bytes from the I2C slave
        ret = read(_i2c_handle, buf, buf_len);
        if (ret >= 0) {
            // Were the required number of bytes read?
            if ((size_t)ret == buf_len) {
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
        else {
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
    return ret;
}

//----------------------------------------------------------------------------
// _i2c_write_adc
//----------------------------------------------------------------------------
int _i2c_write_adc(const void *buf, size_t buf_len) 
{
    uint retry_count = I2C_WRITE_RETRY_COUNT;
    int ret;

    // Perform the I2C writes with retries
    while (retry_count--) {
        // Write the bytes to the I2C device
        ret = write(_i2c_handle, buf, buf_len);
        if (ret >= 0) {
            // Were the required number of bytes written?
            if ((size_t)ret == buf_len) {
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
        else
        {
            // Set the return value to -errno in case we need to return
            ret = -errno;

            // Did a timeout occur?
            if (errno == ETIMEDOUT) {
                // If a timeout occurred then there is no I2C contact
                // with the device, and we can stop trying the write              
                break;
            }
        }
    }
    return ret; 
}
