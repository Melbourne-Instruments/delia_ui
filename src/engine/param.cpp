/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  param.cpp
 * @brief Param implementation.
 *-----------------------------------------------------------------------------
 */

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <math.h>
#include "param.h"
#include "base_manager.h"
#include "utils.h"
#include "data_conversion.h"

// Constants
constexpr char ARP_PARAM_PATH_PREFIX[]            = "/arp/";
constexpr char DAW_PARAM_PATH_PREFIX[]            = "/daw/";
constexpr char MIDI_CONTROL_PATH_PREFIX[]         = "/mid/";
constexpr char SEQ_PARAM_PATH_PREFIX[]            = "/seq/";
constexpr char SFC_CONTROL_PARAM_PATH_PREFIX[]    = "/sfc/";
constexpr char SYSTEM_PARAM_PATH_PREFIX[]         = "/syp/";
constexpr char SYSTEM_FUNC_PARAM_PATH_PREFIX[]    = "/syf/";
constexpr char KNOB_CONTROL_BASE_NAME[]           = "Knob";
constexpr char KNOB_CONTROL_PATH_NAME[]           = "Knob_";
constexpr char SWITCH_CONTROL_BASE_NAME[]         = "Switch";
constexpr char SWITCH_CONTROL_PATH_NAME[]         = "Switch_";
constexpr char TEMPO_BMP_PARAM_NAME[]             = "tempo_bpm";
constexpr uint DEFAULT_DISPLAY_RANGE_MIN          = 0;
constexpr uint DEFAULT_DISPLAY_RANGE_MAX          = 100;
constexpr uint16_t KNOB_HW_VALUE_NORMAL_THRESHOLD = (FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR * (300.0f/360.0f)) / 1401.0f;
constexpr uint16_t KNOB_HW_VALUE_LARGE_THRESHOLD  = (KNOB_HW_VALUE_NORMAL_THRESHOLD * 2 * 2);
constexpr uint16_t KNOB_HW_INDENT_WIDTH           = 0.03 * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
constexpr uint16_t KNOB_HW_INDENT_THRESHOLD       = 5;

//---------------------------
// Param class implementation
//---------------------------

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string Param::ParamPath(std::string param_name)
{
    return SYSTEM_PARAM_PATH_PREFIX + param_name;
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string Param::ParamPath(const BaseManager *mgr, std::string param_name)
{
    return Param::ParamPath(mgr->module(), param_name);
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string Param::ParamPath(MoniqueModule module, std::string param_name)
{
    std::string path_prefix;

    // Set the path based on the module
    switch (module)
    {
        case MoniqueModule::DAW:
            path_prefix = DAW_PARAM_PATH_PREFIX;
            break;

        case MoniqueModule::SEQ:
            path_prefix = SEQ_PARAM_PATH_PREFIX;
            break;

        case MoniqueModule::ARP:
            path_prefix = ARP_PARAM_PATH_PREFIX;
            break;

        case MoniqueModule::SFC_CONTROL:
            path_prefix = SFC_CONTROL_PARAM_PATH_PREFIX;
            break;

        case MoniqueModule::MIDI_DEVICE:
            path_prefix = MIDI_CONTROL_PATH_PREFIX;
            break;

        default:
            // Shouldn't get here, set any empty string for the prefix
            path_prefix = "";
            break;
    }
    return (path_prefix + param_name);
}

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<Param> Param::CreateParam(int param_id, std::string param_name, std::string display_name, ParamDataType data_type) 
{ 
    // Create as system param (no associated module)
    auto param = std::make_unique<Param>(MoniqueModule::SYSTEM, data_type);
    param->_param_id = param_id;
    param->_path = Param::ParamPath(param_name);
    param->_display_name = display_name;
    return param;
}

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<Param> Param::CreateParam(const BaseManager *mgr, int param_id, std::string param_name, std::string display_name, ParamDataType data_type) 
{
    // Create as module param
    auto param = std::make_unique<Param>(mgr->module(), data_type);
    param->_param_id = param_id;    
    param->_path = Param::ParamPath(mgr->module(), param_name);
    param->_display_name = display_name;
    return param;
}

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<Param> Param::CreateParam(MoniqueModule module, int param_id, std::string param_name, std::string display_name, ParamDataType data_type) 
{
    // Create as module param
    auto param = std::make_unique<Param>(module, data_type);
    param->_param_id = param_id;    
    param->_path = Param::ParamPath(module, param_name);
    param->_display_name = display_name;
    return param;
}

//----------------------------------------------------------------------------
// Param
//----------------------------------------------------------------------------
Param::Param(const Param& param) : 
    _module(param._module),
    _data_type(param._data_type)
{
    // Copy the class data
    _type = param._type;
    _processor_id = param._processor_id;
    _param_id = param._param_id;
    _preset = param._preset;
    _save = param._save;
    _path = param._path;
    _ref = param._ref;
    _display_name = param._display_name;
    _param_list_name = param._param_list_name;
    _param_list_display_name = param._param_list_display_name;
    _param_list_type = param._param_list_type;
    _param_list = param._param_list;
    _mapped_params = param._mapped_params;
    _value =  param._value;
    _num_positions = param._num_positions;
    _actual_num_positions = param._actual_num_positions;
    _position_increment = param._position_increment;
    _physical_pos_increment = param._physical_pos_increment;
    _display_range_min = param._display_range_min;
    _display_range_max = param._display_range_max;
    _display_decimal_places = param._display_decimal_places; 
    _value_strings = param._value_strings;
    _value_tags = param._value_tags;
    _display_as_numeric = param._display_as_numeric;
    _display_enum_list = param._display_enum_list;
    _display_hr_value = param._display_hr_value;
    _str_value = param._str_value;
    _linked_param = param._linked_param;
    _linked_param_enabled = param._linked_param_enabled;
    _sfc_control = param._sfc_control;
    _mod_matrix_param = param._mod_matrix_param;
    _mod_src_name = param._mod_src_name;
    _mod_dst_name = param._mod_dst_name;
    _is_seq_chunk_param = param._is_seq_chunk_param;    
}

//----------------------------------------------------------------------------
// Param
//----------------------------------------------------------------------------
Param::Param(MoniqueModule module, ParamDataType data_type) :
    _module(module),
    _data_type(data_type)
{
    // Initialise class data
    _type = ParamType::GLOBAL;
    _processor_id = -1;
    _param_id = -1;
    _preset = true;
    _save = true;
    _path = "";
    _ref = "";
    _display_name = "";
    _param_list_name = "";
    _param_list_display_name = "";
    _param_list_type = ParamListType::NORMAL;
    _param_list.clear();
    _mapped_params.clear();
    _value = 0.0;
    _num_positions = 0;
    _actual_num_positions = 0;
    _position_increment = 0.0;
    _physical_pos_increment = 0.0;
    _display_range_min = DEFAULT_DISPLAY_RANGE_MIN;
    _display_range_max = DEFAULT_DISPLAY_RANGE_MAX;
    _display_decimal_places = 0;   
    _value_strings.clear();
    _value_tags.clear();
    _display_as_numeric = false;
    _display_enum_list = true;
    _display_hr_value = false;
    _str_value = "";
    _linked_param = false;
    _linked_param_enabled = false;
    _sfc_control = false;
    _mod_matrix_param = false;
    _mod_src_name = "";
    _mod_dst_name = "";
    _is_seq_chunk_param = false;    
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> Param::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<Param>(*this);
}

//----------------------------------------------------------------------------
// ~Param
//----------------------------------------------------------------------------
Param::~Param()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// type
//----------------------------------------------------------------------------
ParamType Param::type() const
{
    // Return the param type
    return _type;
}

//----------------------------------------------------------------------------
// processor_id
//----------------------------------------------------------------------------
int Param::processor_id() const
{
    // Return the processor ID
    return _processor_id;
}

//----------------------------------------------------------------------------
// param_id
//----------------------------------------------------------------------------
int Param::param_id() const
{
    // Return the param ID
    return _param_id;
}

//----------------------------------------------------------------------------
// preset
//----------------------------------------------------------------------------
bool Param::preset() const
{
    // Return if this is a preset or not
    return _preset;
}

//----------------------------------------------------------------------------
// save
//----------------------------------------------------------------------------
bool Param::save() const
{
    // Return if this preset param is saved in the preset file or not
    return _preset && _save;
}

//----------------------------------------------------------------------------
// path
//----------------------------------------------------------------------------
std::string Param::path() const
{
    // Return the param path
    return _path;
}

//----------------------------------------------------------------------------
// ref
//----------------------------------------------------------------------------
std::string Param::ref() const
{
    // Return the param reference
    return _ref;
}

//----------------------------------------------------------------------------
// display_name
//----------------------------------------------------------------------------
const char *Param::display_name() const
{
    // Return the display name
    return _display_name.c_str();
}

//----------------------------------------------------------------------------
// param_list_name
//----------------------------------------------------------------------------
std::string Param::param_list_name() const
{
    // Return the param list name
    return _param_list_name;
}

//----------------------------------------------------------------------------
// param_list_display_name
//----------------------------------------------------------------------------
std::string Param::param_list_display_name() const
{
    // Return the param list display name
    return _param_list_display_name;
}

//----------------------------------------------------------------------------
// param_list_type
//----------------------------------------------------------------------------
ParamListType Param::param_list_type() const
{
    // Return the param list type
    return _param_list_type;
}

//----------------------------------------------------------------------------
// param_list_name
//----------------------------------------------------------------------------
std::vector<Param *> Param::param_list() const
{
    // Does this param have a basic param list?
    if (_param_list.size() > 0) {
        return _param_list;
    }
    // Does this param have a context param list?
    else if (_context_specific_param_list.size() > 0) {
        // Go through each context specific param list
        for (const ContextSpecificParams& csp : _context_specific_param_list) {
            // If the context param exists
            if (csp.context_param) {
                // If the context param value matches
                if (csp.context_param->num_positions()) {
                    if (csp.context_param->position_value() == csp.context_value) {
                        return csp.param_list;
                    }
                }
                else if (csp.context_param->value() == csp.context_value) {
                    return csp.param_list;
                }
            }
        }
    }
    return std::vector<Param *>();
}

//----------------------------------------------------------------------------
// cmp_path
//----------------------------------------------------------------------------
bool Param::cmp_path(std::string path) const
{
    // Compare the passed control path with this param path
    return _path == path;
}

//----------------------------------------------------------------------------
// set_type
//----------------------------------------------------------------------------
void Param::set_type(ParamType type)
{
    // Set the param type
    _type = type;
}

//----------------------------------------------------------------------------
// set_processor_id
//----------------------------------------------------------------------------
void Param::set_processor_id(int processor_id)
{
    // Set the processor ID
    _processor_id = processor_id;
}

//----------------------------------------------------------------------------
// set_preset
//----------------------------------------------------------------------------
void Param::set_preset(bool preset)
{
    // Set as a preset or not
    _preset = preset;
}

//----------------------------------------------------------------------------
// set_save
//----------------------------------------------------------------------------
void Param::set_save(bool save)
{
    // Set if this param is saved or not (preset params only)
    _save = save;
}

//----------------------------------------------------------------------------
// set_ref
//----------------------------------------------------------------------------
void Param::set_ref(std::string ref)
{
    // Set the param reference
    _ref = ref;
}

//----------------------------------------------------------------------------
// set_display_name
//----------------------------------------------------------------------------
void Param::set_display_name(std::string name)
{
    // Set the display name
    _display_name = name;
}

//----------------------------------------------------------------------------
// set_param_list_name
//----------------------------------------------------------------------------
void Param::set_param_list_name(std::string name)
{
    // Set the param list name
    _param_list_name = name;
}

//----------------------------------------------------------------------------
// set_param_list_display_name
//----------------------------------------------------------------------------
void Param::set_param_list_display_name(std::string name)
{
    // Set the param list display name
    _param_list_display_name = name;
}

//----------------------------------------------------------------------------
// set_param_list_type
//----------------------------------------------------------------------------
void Param::set_param_list_type(ParamListType type)
{
    // Set the param list type
    _param_list_type = type;
}

//----------------------------------------------------------------------------
// set_param_list
//----------------------------------------------------------------------------
void Param::set_param_list(std::vector<Param *> list)
{
    // Set the param list
    _param_list = list;
}

//----------------------------------------------------------------------------
// set_context_specific_param_list
//----------------------------------------------------------------------------
void Param::set_context_specific_param_list(std::vector<ContextSpecificParams>& list)
{
    // Set the context specific param list
    _context_specific_param_list = list;
}

//----------------------------------------------------------------------------
// mapped_params
//----------------------------------------------------------------------------
std::vector<Param *> Param::mapped_params(const Param *containing_param) const
{
    // Should we check for a map containing the passed param
    if (containing_param) {
        // Search the mapped params for this param
        for (const Param *mp : _mapped_params) {
            // Param match?
            if (mp == containing_param) {
                // Found, return the vector
                return _mapped_params;
            }
        }

        // Not found so return an empty vector
        return std::vector<Param *>();
    }

    // Containing param is null so just return the mapped params
    return _mapped_params;
}

//----------------------------------------------------------------------------
// add_mapped_param
//----------------------------------------------------------------------------
void Param::add_mapped_param(Param *param)
{
    _mapped_params.push_back(param);
}

//----------------------------------------------------------------------------
// clear_mapped_params
//----------------------------------------------------------------------------
void Param::clear_mapped_params()
{
    _mapped_params.clear();
}

//----------------------------------------------------------------------------
// is_linked_param
//----------------------------------------------------------------------------
bool Param::is_linked_param() const
{
    return _linked_param;
}

//----------------------------------------------------------------------------
// is_linked_param
//----------------------------------------------------------------------------
bool Param::is_linked_param_enabled() const
{
    return _linked_param_enabled;
}

//----------------------------------------------------------------------------
// enable_linked_param
//----------------------------------------------------------------------------
void Param::enable_linked_param(bool enable)
{
    _linked_param_enabled = enable;
}

//----------------------------------------------------------------------------
// set_as_linked_param
//----------------------------------------------------------------------------
void Param::set_as_linked_param()
{
    _linked_param = true;
}

//----------------------------------------------------------------------------
// sfc_control
//----------------------------------------------------------------------------
bool Param::sfc_control() const
{
    return _sfc_control;
}

//----------------------------------------------------------------------------
// hr_value
//----------------------------------------------------------------------------
float Param::hr_value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the converted human readable normalised value
    return dataconv::from_normalised_float(_module, _param_id, _value);
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float Param::value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the normalised value
    return _value;
}

//----------------------------------------------------------------------------
// display_string
//----------------------------------------------------------------------------
std::pair<bool, std::string> Param::display_string() const
{
    // Should we always display the HR value?
    if (_display_hr_value) {
        // Get the HR value
        auto value = hr_value();

        // Format the string, with decimal places if needed
        std::string format;
        if (_display_decimal_places == 2) {
            value = std::ceil(value * 100.0) / 100.0;
            format = "%.2f";       
        }
        else if (_display_decimal_places == 1) {
            value = std::ceil(value * 10.0) / 10.0;
            format = "%.1f";    
        }    
        else {
            value = std::roundf(value);
            format = "%.0f";  
        }
        if (_display_range_min < 0) {
            value += _display_range_min;
        }
        char buf[sizeof("123456")];
        std::sprintf(buf, format.c_str(), value);
        return std::pair<bool, std::string>(true, buf);        
    }

    // Get the display string based on the param type and settings
    // Is this a position based param?
    float val = value();
    if (_num_positions > 0) {
        // Convert the value to an integer position
        val = std::roundf(val / _position_increment);
        if (val >= _num_positions)
            val = _num_positions - 1;
        // TODO - fix this
        if (val < 0) {
            val = 0;
        }

        // If a value string can be shown, return it
        if (val < _value_strings.size()) {
            // Check if this string is a numeric value or not
            bool is_numeric = true;
            if (!_display_as_numeric) {
                for (uint i=0; i<_value_strings[val].size(); i++) {
                    if (!std::isdigit(_value_strings[val].at(i))) {
                        is_numeric = false;
                        break;
                    }
                }
            }
            return std::pair<bool, std::string>(is_numeric, _value_strings[val]);
        }

        // Calculate the display range
        uint range;
        if (_display_range_min < 0) {
            range = std::abs(_display_range_min) + _display_range_max;
        }
        else {
            range = _display_range_max - _display_range_min;
        }

        // Range the integer value
        val *= range / (_num_positions - 1);
        if (_display_range_min)
            val += _display_range_min;          
        return std::pair<bool, std::string>(true, std::to_string(static_cast<int>(val)));
    }

    // Convert the float to the required display value (with or without decimal places)
    int min = _display_range_min;
    int max = _display_range_max;
    if (_display_range_min < 0) {
        min = 0;
        max += std::abs(_display_range_min);
    }
    float value = min + ((max - min) * val);

    // Format the string, with decimal places if needed
    std::string format;
    if (_display_decimal_places == 2) {
        value = std::ceil(value * 100.0) / 100.0;
        format = "%.2f";       
    }
    else if (_display_decimal_places == 1) {
        value = std::ceil(value * 10.0) / 10.0;
        format = "%.1f";    
    }    
    else {
        value = std::roundf(value);
        format = "%.0f";  
    }
    if (_display_range_min < 0) {
        value += _display_range_min;
    }
    char buf[sizeof("123456")];
    std::sprintf(buf, format.c_str(), value);
    return std::pair<bool, std::string>(true, buf);
}

//----------------------------------------------------------------------------
// position_string
//----------------------------------------------------------------------------
std::string Param::position_string(uint pos) const
{
    // Is this a position based param?
    if (_num_positions > 0) {
        if (pos < _value_strings.size())
            return _value_strings.at(pos);
        if (_display_range_min)
            pos += _display_range_min;
        return std::to_string(static_cast<int>(pos));
    }
    return "";
}

//----------------------------------------------------------------------------
// display_tag
//----------------------------------------------------------------------------
std::string Param::display_tag() const
{
    // Is there just one tag in the array?
    // If so, always return it
    if (_value_tags.size() == 1) {
        return _value_tags[0];
    }

    // Is this an enum param?
    if (_num_positions > 0) {
        // Does it have an equivalent value tag?
        auto pos_value = position_value();
        if ((pos_value >=0) && ((uint)pos_value < _value_tags.size())) {
            return _value_tags[pos_value];
        }
    }
    return "";
}

//----------------------------------------------------------------------------
// display_enum_list
//----------------------------------------------------------------------------
bool Param::display_enum_list() const
{
    // Return if this param should be displayed as an enum list
    return (_num_positions > 0) && _display_enum_list;
}

//----------------------------------------------------------------------------
// position_value
//----------------------------------------------------------------------------
int Param::position_value() const
{
    int val = -1;

    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Is this a position param?
    if (_num_positions) {
        // Get the position value
        val = _position_value(_value, _position_increment);
    }
    return val;
}

//----------------------------------------------------------------------------
// num_positions
//----------------------------------------------------------------------------
uint Param::num_positions() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the number of positions
    return _num_positions <= _actual_num_positions ? _num_positions : _actual_num_positions;
}

//----------------------------------------------------------------------------
// position_increment
//----------------------------------------------------------------------------
float Param::position_increment() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the position increment
    return _position_increment;
}

//----------------------------------------------------------------------------
// physical_position_increment
//----------------------------------------------------------------------------
float Param::physical_position_increment() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the physical position increment
    return _physical_pos_increment;
}

//----------------------------------------------------------------------------
// set_hr_value
//----------------------------------------------------------------------------
void Param::set_hr_value(float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the normalised value from the human readable value
    _value = dataconv::to_normalised_float(_module, _param_id, value);
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void Param::set_value(float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the normalised value
    _value = value;
}

//----------------------------------------------------------------------------
// set_value_from_param
//----------------------------------------------------------------------------
void Param::set_value_from_param(const Param &param)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // If the passed param is the same as this param, do not process
    if (this == &param) {
        return;
    }

    // Set the value from the passed param
    _value =_value_from_param(param);
}

//----------------------------------------------------------------------------
// set_value_from_position
//----------------------------------------------------------------------------
void Param::set_value_from_position(uint position, bool force)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    if ((_num_positions > 0) && ((position < _actual_num_positions) || force)) {
        // Calculate the multi-position value as a float
        _value = position * _position_increment;
    }
}

//----------------------------------------------------------------------------
// set_position_param
//----------------------------------------------------------------------------
void Param::set_position_param(uint num_positions)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set this param as a position param
    _num_positions = num_positions;
    _actual_num_positions = num_positions;
    _position_increment = 1.0 / _num_positions;
    _physical_pos_increment = 1.0 / (_num_positions - 1);
}

//----------------------------------------------------------------------------
// set_actual_num_positions
//----------------------------------------------------------------------------
void Param::set_actual_num_positions(uint num_positions)
{
    // Update the actual number of positions
    if (num_positions < _num_positions) {
        _actual_num_positions = num_positions;
    }
    else {
        _actual_num_positions = _num_positions;
    }
}

//----------------------------------------------------------------------------
// set_display_range_min
//----------------------------------------------------------------------------
void Param::set_display_min_value(int min)
{
    // Set the display range min
    _display_range_min = min;
}

//----------------------------------------------------------------------------
// set_display_range_max
//----------------------------------------------------------------------------
void Param::set_display_max_value(int max)
{
    // Set the display range max
    _display_range_max = max;
}

//----------------------------------------------------------------------------
// set_display_decimal_places
//----------------------------------------------------------------------------
void Param::set_display_decimal_places(uint num)
{
    // Check the passsed number of places is valid
    if (num < 3) {
        // Set the display number of decimal places
        _display_decimal_places = num;
    }
}

//----------------------------------------------------------------------------
// set_display_as_numeric
//----------------------------------------------------------------------------
void Param::set_display_as_numeric(bool display)
{
    // Set if this should be displayed as if it were a numeric param
    _display_as_numeric = display;
}

//----------------------------------------------------------------------------
// set_display_enum_list
//----------------------------------------------------------------------------
void Param::set_display_enum_list(bool display)
{
    // Set if this should be displayed as an enum list or not (if an
    // enum param)
    _display_enum_list = display;
}

//----------------------------------------------------------------------------
// set_display_hr_value
//----------------------------------------------------------------------------
void Param::set_display_hr_value(bool display)
{
    // Set if we should always just display the HR value
    _display_hr_value = display;
}

//----------------------------------------------------------------------------
// add_value_string
//----------------------------------------------------------------------------
void Param::add_value_string(std::string value)
{
    _value_strings.push_back(value);
}

//----------------------------------------------------------------------------
// add_value_tag
//----------------------------------------------------------------------------
void Param::add_value_tag(std::string tag)
{
    _value_tags.push_back(tag);
}

//----------------------------------------------------------------------------
// str_value
//----------------------------------------------------------------------------
std::string Param::str_value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the string value
    return _str_value;
}

//----------------------------------------------------------------------------
// set_str_value
//----------------------------------------------------------------------------
void Param::set_str_value(std::string value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the string value
    _str_value = value;
}

//----------------------------------------------------------------------------
// mod_matrix_param
//----------------------------------------------------------------------------
bool Param::mod_matrix_param() const
{
    // Return if this is a mod matrix param or not
    return _mod_matrix_param;
}

//----------------------------------------------------------------------------
// mod_src_name
//----------------------------------------------------------------------------
std::string Param::mod_src_name() const
{
    // Return the mod src name
    return _mod_src_name;
}

//----------------------------------------------------------------------------
// mod_dst_name
//----------------------------------------------------------------------------
std::string Param::mod_dst_name() const
{
    // Return the mod dst name
    return _mod_dst_name;
}

//----------------------------------------------------------------------------
// set_as_mod_matrix_param
//----------------------------------------------------------------------------
void Param::set_as_mod_matrix_param(std::string src_name, std::string dst_name)
{
    // Indicate this is a mod matrix param
    _mod_matrix_param = true;
    _mod_src_name = src_name;
    _mod_dst_name = dst_name;
}

//----------------------------------------------------------------------------
// seq_chunk_param
//----------------------------------------------------------------------------
bool Param::seq_chunk_param() const
{
    // Return if this is a Sequencer chunk param
    return _is_seq_chunk_param;
}

//----------------------------------------------------------------------------
// set_as_seq_chunk_param
//----------------------------------------------------------------------------
void Param::set_as_seq_chunk_param()
{
    // Set this as a Sequencer chunk param
    _is_seq_chunk_param = true;
}

//----------------------------------------------------------------------------
// seq_chunk_param_is_reset
//----------------------------------------------------------------------------
bool Param::seq_chunk_param_is_reset() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);
    
    // Check if the Sequencer chunk is the reset value
    return _str_value == utils::seq_chunk_param_reset_value();
}

//----------------------------------------------------------------------------
// reset_seq_chunk_param
//----------------------------------------------------------------------------
void Param::reset_seq_chunk_param()
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);
    
    // Reset the Sequencer chunk value
    _str_value = utils::seq_chunk_param_reset_value();
}

//----------------------------------------------------------------------------
// _position_value
//----------------------------------------------------------------------------
int Param::_position_value(float from_value, float pos_increment) const
{
    // Calculate the physical pos
    int val = _num_positions;
    auto v_inc = from_value + (pos_increment / 2);
    if (v_inc < 1.0) {
        auto v = fmod(v_inc, 1.0);
        val = floor(v / pos_increment);
    }

    // Clip in case it exceeds the max positions
    if ((uint)val >= _num_positions) {
        // Clip the returned value
        val = _num_positions - 1;
    }
    return val;
}

//----------------------------------------------------------------------------
// _value_from_param
//----------------------------------------------------------------------------
float Param::_value_from_param(const Param& param) const
{
    float val;

    // Check if this param is a position param
    // Position params can only have a number of fixed values,
    // based on a position
    if (_num_positions) {
        // Get the pos increment
        auto pos_increment = (sfc_control() || param.sfc_control()) ? _physical_pos_increment : _position_increment;

        // Calculate the position value as a float
        val = _position_value(param.value(), pos_increment) * _position_increment;
    }
    // Check if the passed param is a position param
    else if (param.num_positions()) {
        // We need to calculate the integer position from the
        // position float value
        auto pos = std::round(param.value() / param.position_increment());

        // Convert it to a physical position
        val = pos * param.physical_position_increment();
    }                         
    else {
        // Get the normalised value
        val = param.value();
    }
    return val;
}

//--------------------------------
// LayerParam class implementation
//--------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<LayerParam> LayerParam::CreateParam(int param_id, std::string param_name, std::string display_name, ParamDataType data_type)
{
    // Create as system param (no associated module)
    auto param = std::make_unique<LayerParam>(MoniqueModule::SYSTEM, data_type);
    param->_path = Param::ParamPath(param_name);
    param->_param_id = param_id;
    param->_param_id_d1 = param_id;
    param->_display_name = display_name;
    return param;
}

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<LayerParam> LayerParam::CreateParam(const BaseManager *mgr, int param_id, std::string param_name, std::string display_name, ParamDataType data_type)
{
    // Create as module param
    auto param = std::make_unique<LayerParam>(mgr->module(), data_type);
    param->_path = Param::ParamPath(mgr->module(), param_name);
    param->_param_id = param_id;
    param->_param_id_d1 = param_id;
    param->_display_name = display_name;
    return param;
}

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<LayerParam> LayerParam::CreateParam(const BaseManager *mgr, std::string param_name, std::string display_name, ParamDataType data_type)
{
    // Create as module param
    auto param = std::make_unique<LayerParam>(mgr->module(), data_type);
    param->_path = Param::ParamPath(mgr->module(), param_name);
    param->_display_name = display_name;
    return param;
}

//----------------------------------------------------------------------------
// LayerParam
//----------------------------------------------------------------------------
LayerParam::LayerParam(const LayerParam& param) : Param(param)
{
    // Copy the class data
    _param_id_d1 = param._param_id_d1;
    _value_d1 = param._value_d1;
    _str_value_d1 = param._str_value_d1;
}

//----------------------------------------------------------------------------
// LayerParam
//----------------------------------------------------------------------------
LayerParam::LayerParam(MoniqueModule module, ParamDataType data_type) : Param(module, data_type)
{
    // Initialise class data
    _param_id_d1 = -1;
    _value_d1 = 0.0;
    _str_value_d1 = "";
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> LayerParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<LayerParam>(*this);
}

//----------------------------------------------------------------------------
// ~LayerParam
//----------------------------------------------------------------------------
LayerParam::~LayerParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// param_id
//----------------------------------------------------------------------------
int LayerParam::param_id() const
{
    // Return the param ID baed on the current layer
    return param_id(utils::get_current_layer_info().layer_id());
}

//----------------------------------------------------------------------------
// param_id
//----------------------------------------------------------------------------
int LayerParam::param_id(LayerId layer_id) const
{
    // Return the param ID baed on the passed layer
    return layer_id == LayerId::D0 ? _param_id : _param_id_d1;
}

//----------------------------------------------------------------------------
// set_param_id
//----------------------------------------------------------------------------
void LayerParam::set_param_id(LayerId layer_id, int param_id)
{
    // Set the param ID based on the passed layer
    layer_id == LayerId::D0 ? _param_id = param_id : _param_id_d1 = param_id;
}

//----------------------------------------------------------------------------
// hr_value
//----------------------------------------------------------------------------
float LayerParam::hr_value() const
{
    // Return the specified layer value
    return hr_value(utils::get_current_layer_info().layer_id());
}

//----------------------------------------------------------------------------
// hr_value
//----------------------------------------------------------------------------
float LayerParam::hr_value(LayerId layer_id) const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);                

    // Return the param human readable value based on the current layer
    return dataconv::from_normalised_float(_module, _param_id, 
                                           (layer_id == LayerId::D0 ? _value : _value_d1));
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float LayerParam::value() const
{
    // Return the current layer value
    return value(utils::get_current_layer_info().layer_id());
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float LayerParam::value(LayerId layer_id) const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the normalised value
    return layer_id == LayerId::D0 ? _value : _value_d1;
}

//----------------------------------------------------------------------------
// position_value
//----------------------------------------------------------------------------
int LayerParam::position_value() const
{
    // Return the position value
    return position_value(utils::get_current_layer_info().layer_id());
}

//----------------------------------------------------------------------------
// position_value
//----------------------------------------------------------------------------
int LayerParam::position_value(LayerId layer_id) const
{
    int val = -1;

    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Is this a position param?
    if (_num_positions) {
        // Get the position value
        val = _position_value((layer_id == LayerId::D0 ? _value : _value_d1), _position_increment);
    }
    return val;
}

//----------------------------------------------------------------------------
// set_hr_value
//----------------------------------------------------------------------------
void LayerParam::set_hr_value(float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the normalised value from the human readable value
    auto val = dataconv::to_normalised_float(_module, _param_id, value);
    utils::is_current_layer(LayerId::D0) ? 
        _value = val : 
        _value_d1 = val;
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void LayerParam::set_value(float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the normalised value
    utils::is_current_layer(LayerId::D0) ? 
        _value = value : 
        _value_d1 = value;
}

//----------------------------------------------------------------------------
// set_value_from_param
//----------------------------------------------------------------------------
void LayerParam::set_value_from_param(const Param &param)
{
    // Set the value from the passed param for the current layer
    set_value_from_param(utils::get_current_layer_info().layer_id(), param);
}

//----------------------------------------------------------------------------
// set_value_from_param
//----------------------------------------------------------------------------
void LayerParam::set_value_from_param(LayerId layer_id, const Param &param)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // If the passed param is the same as this param, do not process
    if (this == &param) {
        return;
    }

    // Get a reference to the value to set
    float& value = layer_id == LayerId::D0 ? _value : _value_d1;

    // Set the value from the param
    value = _value_from_param(param);
}

//----------------------------------------------------------------------------
// set_value_from_position
//----------------------------------------------------------------------------
void LayerParam::set_value_from_position(uint position, bool force)
{
    // Set the value from the position
    set_value_from_position(utils::get_current_layer_info().layer_id(), position, force);
}

//----------------------------------------------------------------------------
// set_value_from_position
//----------------------------------------------------------------------------
void LayerParam::set_value_from_position(LayerId layer_id, uint position, bool force)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    if ((_num_positions > 0) && ((position < _actual_num_positions) || force)) {
        // Calculate the multi-position value as a float
        layer_id == LayerId::D0 ?
            _value = position * _position_increment :
            _value_d1 = position * _position_increment;
    }
}

//----------------------------------------------------------------------------
// set_hr_value
//----------------------------------------------------------------------------
void LayerParam::set_hr_value(LayerId layer_id, float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the value
    auto val = dataconv::to_normalised_float(_module, _param_id, value);
    layer_id == LayerId::D0 ? 
        _value = val : 
        _value_d1 = val;
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void LayerParam::set_value(LayerId layer_id, float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the value
    layer_id == LayerId::D0 ? 
        _value = value : 
        _value_d1 = value;
}

//----------------------------------------------------------------------------
// str_value
//----------------------------------------------------------------------------
std::string LayerParam::str_value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the normalised value
    return utils::is_current_layer(LayerId::D0) ? _str_value : _str_value_d1;
}

//----------------------------------------------------------------------------
// set_str_value
//----------------------------------------------------------------------------
void LayerParam::set_str_value(std::string value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the value
    utils::is_current_layer(LayerId::D0) ? 
        _str_value = value : 
        _str_value_d1 = value;
}

//----------------------------------------------------------------------------
// set_str_value
//----------------------------------------------------------------------------
void LayerParam::set_str_value(LayerId layer_id, std::string value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the value
    layer_id == LayerId::D0 ? 
        _str_value = value : 
        _str_value_d1 = value;
}

//-------------------------------------
// LayerStateParam class implementation
//-------------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<LayerStateParam> LayerStateParam::CreateParam(int param_id, std::string param_name, std::string display_name, ParamDataType data_type)
{
    // Create as system param (no associated module)
    auto param = std::make_unique<LayerStateParam>(MoniqueModule::SYSTEM, data_type);
    param->_type = ParamType::PATCH_STATE;
    param->_path = Param::ParamPath(param_name);
    param->_param_id = param_id;
    param->_param_id_d1 = param_id;
    param->_param_id_d0_state_b = param_id;
    param->_param_id_d1_state_b = param_id;
    param->_display_name = display_name;
    return param;
}

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<LayerStateParam> LayerStateParam::CreateParam(const BaseManager *mgr, std::string param_name, std::string display_name, ParamDataType data_type)
{
    // Create as module param
    auto param = std::make_unique<LayerStateParam>(mgr->module(), data_type);
    param->_type = ParamType::PATCH_STATE;
    param->_path = Param::ParamPath(mgr->module(), param_name);
    param->_display_name = display_name;
    return param;
}

//----------------------------------------------------------------------------
// LayerStateParam
//----------------------------------------------------------------------------
LayerStateParam::LayerStateParam(const LayerStateParam& param) : LayerParam(param)
{
    // Copy the class data
    _state_a_only_param = param._state_a_only_param;
    _param_id_d0_state_b = param._param_id_d0_state_b;
    _param_id_d1_state_b = param._param_id_d1_state_b;
    _value_d0_state_b = param._value_d0_state_b;
    _value_d1_state_b = param._value_d1_state_b; 
    _str_value_d0_state_b = param._str_value_d0_state_b;
    _str_value_d1_state_b = param._str_value_d1_state_b;
}

//----------------------------------------------------------------------------
// LayerStateParam
//----------------------------------------------------------------------------
LayerStateParam::LayerStateParam(MoniqueModule module, ParamDataType data_type) : LayerParam(module, data_type)
{
    // Initialise class data
    _state_a_only_param = false;
    _param_id_d0_state_b = -1;
    _param_id_d1_state_b = -1;
    _value_d0_state_b = 0.0;
    _value_d1_state_b = 0.0;
    _str_value_d0_state_b = "";
    _str_value_d1_state_b = "";
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> LayerStateParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<LayerStateParam>(*this);
}

//----------------------------------------------------------------------------
// ~FloatStateParam
//----------------------------------------------------------------------------
LayerStateParam::~LayerStateParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// param_id
//----------------------------------------------------------------------------
int LayerStateParam::param_id() const
{
    // Return the param ID baed on the current layer
    return param_id(utils::get_current_layer_info().layer_id());
}

//----------------------------------------------------------------------------
// param_id
//----------------------------------------------------------------------------
int LayerStateParam::param_id(LayerId layer_id) const
{
    // Return the param ID baed on the passed layer
    return layer_id == LayerId::D0 ?
                (utils::get_layer_info(layer_id).layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                        _param_id : _param_id_d0_state_b) :
                (utils::get_layer_info(layer_id).layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                        _param_id_d1 : _param_id_d1_state_b);
}

//----------------------------------------------------------------------------
// param_id
//----------------------------------------------------------------------------
int LayerStateParam::param_id(LayerState state) const
{
    // Return the param ID baed on the current layer
    return param_id(utils::get_current_layer_info().layer_id(), state);
}

//----------------------------------------------------------------------------
// param_id
//----------------------------------------------------------------------------
int LayerStateParam::param_id(LayerId layer_id, LayerState state) const
{
    // Return the param ID baed on the current layer
    return layer_id == LayerId::D0 ?
                (state == LayerState::STATE_A || _state_a_only_param ? 
                        _param_id : _param_id_d0_state_b) :
                (state == LayerState::STATE_A || _state_a_only_param ? 
                        _param_id_d1 : _param_id_d1_state_b);
}

//----------------------------------------------------------------------------
// state_a_only_param
//----------------------------------------------------------------------------
bool LayerStateParam::state_a_only_param() const
{
    // Return if this is a state A only param or not
    return _state_a_only_param;
}

//----------------------------------------------------------------------------
// set_param_id
//----------------------------------------------------------------------------
void LayerStateParam::set_param_id(LayerId layer_id, LayerState state, int param_id)
{
    // Set the param ID based on the passed layer and state
    layer_id == LayerId::D0 ? 
        (state == LayerState::STATE_A ? _param_id = param_id : _param_id_d0_state_b = param_id) :
        (state == LayerState::STATE_A ? _param_id_d1 = param_id : _param_id_d1_state_b = param_id);
}

//----------------------------------------------------------------------------
// set_state_a_only_param
//----------------------------------------------------------------------------
void LayerStateParam::set_state_a_only_param(bool set)
{
    // Set if this is a state A only param or not
    _state_a_only_param = set;
}

//----------------------------------------------------------------------------
// hr_value
//----------------------------------------------------------------------------
float LayerStateParam::hr_value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the param human readable value based on the current state
    return dataconv::from_normalised_float(_module, _param_id,
                                           utils::is_current_layer(LayerId::D0) ?
                                                (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                                                            _value : _value_d0_state_b) :
                                                (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                                                            _value_d1 : _value_d1_state_b));
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float LayerStateParam::value() const
{
    // Return the normalised value for the current layer
    return value(utils::get_current_layer_info().layer_id());
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float LayerStateParam::value(LayerId layer_id) const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the normalised value
    return layer_id == LayerId::D0 ?
                (utils::get_layer_info(layer_id).layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                            _value : _value_d0_state_b) :
                (utils::get_layer_info(layer_id).layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                            _value_d1 : _value_d1_state_b);
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float LayerStateParam::value(LayerState state) const
{
    // Return the normalised value for the current layer and specified state
    return value(utils::get_current_layer_info().layer_id(), state);
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float LayerStateParam::value(LayerId id, LayerState state) const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the normalised value
    return id == LayerId::D0 ?
                (state == LayerState::STATE_A || _state_a_only_param ? 
                            _value : _value_d0_state_b) :
                (state == LayerState::STATE_A || _state_a_only_param ? 
                            _value_d1 : _value_d1_state_b);
}

//----------------------------------------------------------------------------
// position_value
//----------------------------------------------------------------------------
int LayerStateParam::position_value() const
{
    // Return the position value
    return position_value(utils::get_current_layer_info().layer_id(), utils::get_current_layer_info().layer_state());
}

//----------------------------------------------------------------------------
// position_value
//----------------------------------------------------------------------------
int LayerStateParam::position_value(LayerId layer_id, LayerState state) const
{
    int val = -1;

    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Is this a position param?
    if (_num_positions) {
        // Get the position value
        val = _position_value((layer_id == LayerId::D0 ? 
                                    (state == LayerState::STATE_A || _state_a_only_param ? 
                                                _value : _value_d0_state_b) :
                                    (state == LayerState::STATE_A || _state_a_only_param ? 
                                                _value_d1 : _value_d1_state_b)),
                              _position_increment);
    }
    return val;
}

//----------------------------------------------------------------------------
// set_hr_value
//----------------------------------------------------------------------------
void LayerStateParam::set_hr_value(float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the human readable value as normalised
    auto val = dataconv::to_normalised_float(_module, _param_id, value);
    utils::is_current_layer(LayerId::D0) ?
        (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                    _value = val : _value_d0_state_b = val) :
        (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                    _value_d1 = val : _value_d1_state_b = val);
}

//----------------------------------------------------------------------------
// set_hr_value
//----------------------------------------------------------------------------
void LayerStateParam::set_hr_value(LayerId layer_id, float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the value
    auto val = dataconv::to_normalised_float(_module, _param_id, value);
    layer_id == LayerId::D0 ?
        (utils::get_layer_info(LayerId::D0).layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                    _value = val : _value_d0_state_b = val) :
        (utils::get_layer_info(LayerId::D1).layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                    _value_d1 = val : _value_d1_state_b = val);
}

//----------------------------------------------------------------------------
// set_hr_value
//----------------------------------------------------------------------------
void LayerStateParam::set_hr_value(LayerId layer_id, LayerState state, float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the value
    auto val = dataconv::to_normalised_float(_module, _param_id, value);
    layer_id == LayerId::D0 ?
        (state == LayerState::STATE_A || _state_a_only_param ? 
                    _value = val : _value_d0_state_b = val) :
        (state == LayerState::STATE_A || _state_a_only_param ? 
                    _value_d1 = val : _value_d1_state_b = val);
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void LayerStateParam::set_value(float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the normalised value
    utils::is_current_layer(LayerId::D0) ?
        (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                    _value = value : _value_d0_state_b = value) :
        (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                    _value_d1 = value : _value_d1_state_b = value);
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void LayerStateParam::set_value(LayerId layer_id, float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the normalised value
    layer_id == LayerId::D0 ?
        (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                    _value = value : _value_d0_state_b = value) :
        (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                    _value_d1 = value : _value_d1_state_b = value);
}

//----------------------------------------------------------------------------
// set_value_from_param
//----------------------------------------------------------------------------
void LayerStateParam::set_value_from_param(const Param &param)
{
    // Set the value from the passed param for the current layer
    set_value_from_param(utils::get_current_layer_info().layer_id(), param);
}

//----------------------------------------------------------------------------
// set_value_from_param
//----------------------------------------------------------------------------
void LayerStateParam::set_value_from_param(LayerId layer_id, const Param &param)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // If the passed param is the same as this param, do not process
    if (this == &param) {
        return;
    }

    // Get a reference to the value to set
    float& value = layer_id == LayerId::D0 ?
                        (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                                    _value : _value_d0_state_b) :
                        (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                                    _value_d1 : _value_d1_state_b);

    // Set the value from the passed param
    value = _value_from_param(param);
}

//----------------------------------------------------------------------------
// set_value_from_position
//----------------------------------------------------------------------------
void LayerStateParam::set_value_from_position(uint position, bool force)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    if ((_num_positions > 0) && ((position < _actual_num_positions) || force)) {
        // Calculate the multi-position value as a float
        auto val = position * _position_increment;
        utils::is_current_layer(LayerId::D0) ?
            (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                        _value = val : _value_d0_state_b = val) :
            (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                        _value_d1 = val : _value_d1_state_b = val);
    }
}

//----------------------------------------------------------------------------
// set_state_value
//----------------------------------------------------------------------------
void LayerStateParam::set_state_value(LayerId layer_id, LayerState state, float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);
    
    // Set the state value
    layer_id == LayerId::D0 ?
        (state == LayerState::STATE_A || _state_a_only_param ? _value = value : _value_d0_state_b = value) :
        (state == LayerState::STATE_A || _state_a_only_param ? _value_d1 = value : _value_d1_state_b = value);
}

//----------------------------------------------------------------------------
// str_value
//----------------------------------------------------------------------------
std::string LayerStateParam::str_value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the param value based on the current state
    return utils::is_current_layer(LayerId::D0) ?
                (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                            _str_value : _str_value_d0_state_b) :
                (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                            _str_value_d1 : _str_value_d1_state_b);
}

//----------------------------------------------------------------------------
// str_value
//----------------------------------------------------------------------------
std::string LayerStateParam::str_value(LayerId layer_id, LayerState state) const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the param value for the specified Layer and State
    return layer_id == LayerId::D0 ?
                (state == LayerState::STATE_A || _state_a_only_param ? 
                    _str_value : _str_value_d0_state_b) :
                (state == LayerState::STATE_A || _state_a_only_param ? 
                    _str_value_d1 : _str_value_d1_state_b);
}

//----------------------------------------------------------------------------
// set_str_value
//----------------------------------------------------------------------------
void LayerStateParam::set_str_value(std::string value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the value
    utils::is_current_layer(LayerId::D0) ?
        (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ? 
                    _str_value = value : _str_value_d0_state_b = value) :
        (utils::get_current_layer_info().layer_state() == LayerState::STATE_A || _state_a_only_param ?
                     _str_value_d1 = value : _str_value_d1_state_b = value);
}

//----------------------------------------------------------------------------
// set_str_state_value
//----------------------------------------------------------------------------
void LayerStateParam::set_str_state_value(LayerId layer_id, LayerState state, std::string value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the value
    layer_id == LayerId::D0 ?
        (state == LayerState::STATE_A || _state_a_only_param ? _str_value = value : _str_value_d0_state_b = value) :
        (state == LayerState::STATE_A || _state_a_only_param ? _str_value_d1 = value : _str_value_d1_state_b = value);
}

//-------------------------------------
// SfcControlParam class implementation
//-------------------------------------

//----------------------------------------------------------------------------
// SfcControlParam
//----------------------------------------------------------------------------
SfcControlParam::SfcControlParam(const SfcControlParam& param) : Param(param)
{
    // Copy the class data
    _type = param._type;
    _control_type = param._control_type;
    _control_num = param._control_num;
    _control_states = param._control_states;
    _current_control_state = param._current_control_state;
    _default_control_state_name = param._default_control_state_name;
}

//----------------------------------------------------------------------------
// SfcControlParam
//----------------------------------------------------------------------------
SfcControlParam::SfcControlParam(sfc::ControlType control_type, uint control_num) 
    : Param(MoniqueModule::SFC_CONTROL, ParamDataType::FLOAT)
{
    // Initialise the knob variables
    _control_type = control_type;
    _control_num = control_num;
    _param_id = control_num;
    _preset = false;
    _save = false;
    _sfc_control = true;

    // Always add the default state
    auto control_state = SfcControlState();
    control_state.state = utils::default_ui_state();
    _control_states.push_back(control_state);
    _current_control_state = &_control_states.at(0);
    _default_control_state_name = control_state.state;
}

//----------------------------------------------------------------------------
// ~SfcControlParam
//----------------------------------------------------------------------------
SfcControlParam::~SfcControlParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> SfcControlParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<SfcControlParam>(*this);
}

//----------------------------------------------------------------------------
// control_type
//----------------------------------------------------------------------------
sfc::ControlType SfcControlParam::control_type() const
{
    // Return the control type
    return _control_type;
}

//----------------------------------------------------------------------------
// control_type
//----------------------------------------------------------------------------
uint SfcControlParam::control_num() const
{
    // Return the control type
    return _control_num;
}

//----------------------------------------------------------------------------
// mapped_params
//----------------------------------------------------------------------------
std::vector<Param *> SfcControlParam::mapped_params(const Param *containing_param) const
{
    // Should we check for a map containing the passed param
    if (containing_param) {
        // Search the specified state that contains this param as a mapped param
        for (const SfcControlState& cs : _control_states) {
            for (Param *mp : cs.mapped_params) {
                if (mp == containing_param) {
                    // Found, return the vector
                    return cs.mapped_params;
                }
            }
        }

        // Not found so return an empty vector
        return std::vector<Param *>();
    }

    // Containing param is null so just return the current mapped params
    return _current_control_state->mapped_params;
}

//----------------------------------------------------------------------------
// mapped_params
//----------------------------------------------------------------------------
std::vector<Param *> SfcControlParam::mapped_params(uint control_state_index) const
{
    // Return the indexed mapped params
    return _control_states[control_state_index].mapped_params;
}

//----------------------------------------------------------------------------
// add_mapped_param
//----------------------------------------------------------------------------
void SfcControlParam::add_mapped_param(Param *param)
{
    _current_control_state->mapped_params.push_back(param);
}

//----------------------------------------------------------------------------
// clear_mapped_params
//----------------------------------------------------------------------------
void SfcControlParam::clear_mapped_params()
{
    _current_control_state->mapped_params.clear();
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float SfcControlParam::value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the normalised value
    return _current_control_state->value;
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float SfcControlParam::value(uint control_state_index) const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the normalised value
    return _control_states[control_state_index].value;
}

//----------------------------------------------------------------------------
// set_value_from_param
//----------------------------------------------------------------------------
void SfcControlParam::set_value_from_param(const Param& param)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // If the passed param is the same as this param, do not process
    if (this == &param) {
        return;
    }

    // Find the specified state that contains this param as a mapped param
    for (SfcControlState& cs : _control_states) {
        for (Param *mp : cs.mapped_params) {
            if (mp == &param) {
                // Set the value from the passed param
                cs.value = _value_from_param(param);
                break;
            }
        }
    }
}

//----------------------------------------------------------------------------
// set_value_from_param
//----------------------------------------------------------------------------
void SfcControlParam::set_value_from_param(uint control_state_index, const Param& param)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // If the passed param is the same as this param, do not process
    if (this == &param) {
        return;
    }

    // Set the value from the passed param
    _control_states[control_state_index].value = _value_from_param(param);
}

//----------------------------------------------------------------------------
// grouped_control
//----------------------------------------------------------------------------
bool SfcControlParam::grouped_control() const
{
    // Return if this a grouped control or not
    return _current_control_state->group_name.size() > 0;
}

//----------------------------------------------------------------------------
// group_name
//----------------------------------------------------------------------------
std::string SfcControlParam::group_name() const
{
    // Return the group name
    return _current_control_state->group_name;
}

//----------------------------------------------------------------------------
// group_param
//----------------------------------------------------------------------------
Param *SfcControlParam::group_param() const
{
    // Return the group param
    return _current_control_state->group_param;
}

//----------------------------------------------------------------------------
// haptic_mode
//----------------------------------------------------------------------------
const sfc::HapticMode& SfcControlParam::haptic_mode() const
{   
    // Return the haptic mode
    return utils::get_haptic_mode(_control_type, _current_control_state->haptic_mode_name);
}

//----------------------------------------------------------------------------
// control_state
//----------------------------------------------------------------------------
uint SfcControlParam::num_control_states() const
{
    return _control_states.size();
}

//----------------------------------------------------------------------------
// control_state
//----------------------------------------------------------------------------
std::string SfcControlParam::control_state() const
{
    return _current_control_state->state;
}

//----------------------------------------------------------------------------
// is_current_control_state
//----------------------------------------------------------------------------
bool SfcControlParam::is_current_control_state(uint control_state_index) const
{
    return &_control_states[control_state_index] == _current_control_state;
}

//----------------------------------------------------------------------------
// morphable
//----------------------------------------------------------------------------
bool SfcControlParam::morphable() const
{
    // Return if this param is morphable or not
    return _current_control_state->morphable;
}

//----------------------------------------------------------------------------
// morphable
//----------------------------------------------------------------------------
bool SfcControlParam::morphable(uint control_state_index) const
{
    // Return if this param is morphable or not
    return _control_states[control_state_index].morphable;
}

//----------------------------------------------------------------------------
// set_group_name
//----------------------------------------------------------------------------
void SfcControlParam::set_group_name(const std::string group_name)
{
    // Save the group name
    _current_control_state->group_name = group_name;
}

//----------------------------------------------------------------------------
// set_group_param
//----------------------------------------------------------------------------
void SfcControlParam::set_group_param(Param *group_param)
{
    // Save the group param
    _current_control_state->group_param = group_param;
}

//----------------------------------------------------------------------------
// set_haptic_mode
//----------------------------------------------------------------------------
void SfcControlParam::set_haptic_mode(const std::string haptic_mode_name)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);
     
    // Save the haptic mode name
    _current_control_state->haptic_mode_name = haptic_mode_name;

    if (haptic_mode_name == "toggle_tri_state") {
        // Set this param as a position param
        _num_positions = 3;
        _position_increment = 1.0 / _num_positions;
    }
}

//----------------------------------------------------------------------------
// has_control_state
//----------------------------------------------------------------------------
bool SfcControlParam::has_control_state(std::string state)
{
    // Find the specified state
    for (SfcControlState& cs : _control_states) {
        if (cs.state == state) {
            // Found
            return true;
        }
    }
    return false;
}

//----------------------------------------------------------------------------
// add_control_state
//----------------------------------------------------------------------------
void SfcControlParam::add_control_state(std::string state)
{
    // Check if this state already exists
    for (SfcControlState& cs : _control_states) {
        if (cs.state == state) {
            // Already exists, so do nothing
            return;
        }
    }

    // This is a new state to add
    auto control_state = SfcControlState();
    control_state.state = state;

    // If this is the *first* new state added AND the default
    // state has not been set, make this the new default
    // We can check if the default state hasn been set by seeing
    // if there are any mapped params for that state
    if ((_control_states.size() == 1) && (_control_states.at(0).mapped_params.size() == 0)) {
        // Make this state the new default by overwriting the default state
        _control_states.at(0) = control_state;
        _default_control_state_name = state;
    }
    else {
        // Push the new control state
        _control_states.push_back(control_state);
    }
}

//----------------------------------------------------------------------------
// set_default_control_state
//----------------------------------------------------------------------------
void SfcControlParam::set_default_control_state()
{
    // Select the default control state
    (void)set_control_state(_default_control_state_name);
}

//----------------------------------------------------------------------------
// set_control_state
//----------------------------------------------------------------------------
bool SfcControlParam::set_control_state(std::string state)
{
    bool ret = false;

    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);
    
    // If we are not already in that state
    if (_current_control_state->state != state) {
        // Find the specified state
        for (SfcControlState& cs : _control_states) {
            if (cs.state == state) {
                // Found, set it
                _current_control_state = &cs;
                ret = true;
                break;
            }
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// set_morphable
//----------------------------------------------------------------------------
void SfcControlParam::set_morphable(bool morphable)
{
    // Save the morphable state
    _current_control_state->morphable = morphable;
}

//-------------------------------
// KnobParam class implementation
//-------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<KnobParam> KnobParam::CreateParam(unsigned int control_num) 
{ 
    auto param = std::make_unique<KnobParam>(control_num);
    return param;
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string KnobParam::ParamPath(unsigned int control_num)
{
    return SFC_CONTROL_PARAM_PATH_PREFIX + std::string(KNOB_CONTROL_PATH_NAME) + std::to_string(control_num);
}

//----------------------------------------------------------------------------
// KnobParam
//----------------------------------------------------------------------------
KnobParam::KnobParam(const KnobParam& param) : SfcControlParam(param)
{
    // Clone any knob specific data
    _relative_value_control = param._relative_value_control;
    _relative_value_offset = param._relative_value_offset;
    _last_value = param._last_value;
    _raw_pos = param._raw_pos;
}

//----------------------------------------------------------------------------
// KnobParam
//----------------------------------------------------------------------------
KnobParam::KnobParam(unsigned int control_num) 
    : SfcControlParam(sfc::ControlType::KNOB, control_num)
{
    // Initialise the knob variables
    _path = KnobParam::ParamPath(control_num);
    _display_name = KNOB_CONTROL_BASE_NAME + std::string(" ") + std::to_string(control_num);
    _relative_value_control = false;
    _relative_value_offset = 0;
    _last_value = 0;
    _raw_pos = 0;
}

//----------------------------------------------------------------------------
// ~KnobParam
//----------------------------------------------------------------------------
KnobParam::~KnobParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> KnobParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<KnobParam>(*this);
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float KnobParam::value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // We need to check for the special cases of param before
    // processing as a normal value param
    // Firstly check if this param is a multi-position param
    // Multi-position params can only have a number of fixed values,
    // based on a position
    if (_num_positions) {
        float val = roundf(_current_control_state->value / _position_increment);
        if (val >= _num_positions)
            val = 0;
        return val;
    }
    return _current_control_state->value;
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float KnobParam::value(uint control_state_index) const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // We need to check for the special cases of param before
    // processing as a normal value param
    // Firstly check if this param is a multi-position param
    // Multi-position params can only have a number of fixed values,
    // based on a position
    if (_num_positions) {
        float val = roundf(_control_states[control_state_index].value / _position_increment);
        if (val >= _num_positions)
            val = 0;
        return val;
    }
    return _control_states[control_state_index].value;
}

//----------------------------------------------------------------------------
// position_value
//----------------------------------------------------------------------------
uint KnobParam::position_value(uint prev_pos)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    auto value = prev_pos;
    bool increment = true;
    uint new_pos = _num_positions - 1;

    // If this knob has a number of positions defined
    if (_num_positions > 0) {
        // Calculate the position
        auto v_inc = _current_control_state->value + (_position_increment / 2);
        auto v = fmod(v_inc, 1.0);
        new_pos = floor(v / _position_increment);

        // Clip in case it exceeds the max positions
        if (new_pos >= _num_positions) {
            // Clip the returned value
            new_pos = _num_positions - 1;
        }

        // If the last position is valid, and the new and last differ
        if ((_last_actual_pos != -1) && (new_pos != (uint)_last_actual_pos)) {
            // Calc the difference between the two positions
            auto diff = std::abs((int)new_pos - _last_actual_pos);

            // Is the new pos less than the last?
            if (new_pos < (uint)_last_actual_pos) {
                // If the difference is less than half the number of positions,
                // decrement - otherwise assume a wrap-around and increment
                if ((uint)diff < (_num_positions / 2))
                    increment = false;
            }
            else {
                // If the difference is greater than or equal to half the number 
                // of positions, assume a wrap-around and decrement - otherwise increment           
                if ((uint)diff >= (_num_positions / 2))
                    increment = false;
            }
            // Increment if less than the max positions
            if (increment && (value < (_num_selectable_positions - 1))) {
                value++;
            }
            // Decrement if greater than zero
            else if (!increment && (value > 0)) {
                value--;
            }
        }
        _last_actual_pos = new_pos;
    }
    return value;
}

//----------------------------------------------------------------------------
// hw_value
//----------------------------------------------------------------------------
uint32_t KnobParam::hw_value() const
{
    uint32_t knob_min = 0;
    uint32_t knob_max = FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
    uint32_t hw_value;

    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Get the knob control haptic mode
    auto mode = haptic_mode();
    auto knob_width = mode.knob_actual_width;

    // If the knob does not have the full 360 degrees of movement
    if (knob_width < 360.0f)
    {
        // Has the knob start pos been specified?
        if (mode.knob_actual_start_pos != -1.0f)
        {
            // Make sure the start pos and width do not exceed the knob limits
            if ((mode.knob_actual_start_pos + knob_width) > 360.0f)
            {
                // Truncate the knob width
                knob_width = 360.0f - mode.knob_actual_start_pos;
            }

            // Calculate the knob min and max
            knob_min = (mode.knob_actual_start_pos / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
            knob_max = ((mode.knob_actual_start_pos + knob_width) / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
        }
        else
        {
            // Normalise the knob position from from the specified width
            float offset = ((360.0f - mode.knob_actual_width)/360.0f) / 2.0f;
            knob_min = offset * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
            knob_max = (1.0 - offset) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
        }
    }

    // Convert the normalised value to a knob position
    hw_value = (_current_control_state->value * (knob_max - knob_min)) + knob_min;

    // Make sure the min/max knob value has not been exceeded
    hw_value = std::clamp(hw_value, knob_min, knob_max);

    // Does this knob currently have haptic indents?
    if (mode.knob_indents.size() > 0)
    {
        uint32_t indent_range_start = knob_min;
        uint32_t indent_range_end;
        uint32_t knob_range_start = knob_min;
        uint32_t knob_range_end;
        bool is_indent = false;
        uint index = 0;

        // The knob has indents
        // Check if the knob position is any indent
        // Note: Assumes the indents are in increasing order
        while (true)
        {
            uint32_t deadzone_start = knob_min;
            uint32_t deadzone_end = knob_max;

            // If the knob position is an indent (with a small threshold), just break from 
            // the search and use that value
            if ((hw_value > (mode.knob_indents[index].second - KNOB_HW_INDENT_THRESHOLD)) && 
                (hw_value < (mode.knob_indents[index].second + KNOB_HW_INDENT_THRESHOLD)))
            {
                // The knob position is an indent
                hw_value = mode.knob_indents[index].second;
                is_indent = true;
                break;                
            }

            // Make sure we can safely calculate the start of the deadzone, otherwise
            // use the minimum knob position
            if ((mode.knob_indents[index].second - (KNOB_HW_INDENT_WIDTH >> 1)) > knob_min)
            {
                // Calculate the start
                deadzone_start = mode.knob_indents[index].second - (KNOB_HW_INDENT_WIDTH >> 1);
            }

            // Make sure we can safely calculate the end of the deadzone, otherwise
            // use the maximum knob position
            if ((mode.knob_indents[index].second + (KNOB_HW_INDENT_WIDTH >> 1)) < knob_max)
            {
                // Calculate the end
                deadzone_end = mode.knob_indents[index].second + (KNOB_HW_INDENT_WIDTH >> 1);
            }

            // If the knob position is less than this indent, break from the search
            if (hw_value < mode.knob_indents[index].second)
            {
                // Set the range end
                indent_range_end = mode.knob_indents[index].second;
                knob_range_end = deadzone_start;
                break;
            }

            // Set the range start to this indent
            indent_range_start = mode.knob_indents[index].second;
            knob_range_start = deadzone_end;

            // Have we parsed the last indent? If so, stop parsing the indents, otherwise check
            // the next indent
            if (index >= (mode.knob_indents.size() - 1))
            {
                // Set the range end
                indent_range_end = knob_max;
                knob_range_end = knob_max;
                break;
            }

            // Process the next indent
            index++;
        }

        // Is the knob position not an indent?
        if (!is_indent)
        {
            // Convert the scaled knob position to a real position, taking into account
            // the indent deadzone
            float indent_range = indent_range_end - indent_range_start;
            float knob_range = knob_range_end - knob_range_start;
            hw_value = (uint32_t)(knob_range_start + ((knob_range)*((hw_value - indent_range_start) / indent_range)));
        }
    }

    // Make sure the min/max knob value has not been exceeded
    hw_value = std::clamp(hw_value, knob_min, knob_max);  
    return hw_value;
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void KnobParam::set_value(float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the normalised value - clamped
    std::clamp(value, 0.0f, 1.0f);
    _current_control_state->value = value;
}

//----------------------------------------------------------------------------
// set_position_param
//----------------------------------------------------------------------------
void KnobParam::set_position_param(uint position, uint num_selectable_positions)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);
    
    // Set this param as a position param
    _relative_value_control = false;

    // Update the number of positions
    _num_positions = position;

    // Calculate the position increment
    _position_increment = 1.0 / position;
    _num_selectable_positions = num_selectable_positions;
    _last_actual_pos = -1;
}

//----------------------------------------------------------------------------
// set_relative_value_param
//----------------------------------------------------------------------------
void KnobParam::set_relative_value_param(float value)
{    
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Get the raw knob position
    auto val = _raw_pos;

    // Clear the position param variables
    _num_positions = 0;
    
    // Set the relative position based on the last position value
    _last_value = value * KNOB_HW_VALUE_MAX_VALUE;
    if (_last_value > 0) {
        if (val < _last_value) {
            val += KNOB_HW_VALUE_MAX_VALUE - _last_value;
        }
        else {
            val -= _last_value;
        }
    }
    _relative_value_offset = val;

    // Indicate this is a relative value param
    _relative_value_control = true;
}

//----------------------------------------------------------------------------
// reset_param
//----------------------------------------------------------------------------
void KnobParam::reset()
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);
    
    // Reset this knob as either a fixed position or relative position control
    _num_positions = 0;
    _relative_value_control = false;
    _relative_value_offset = 0;
    _last_value = 0;
}

//----------------------------------------------------------------------------
// set_value_from_hw
//----------------------------------------------------------------------------
void KnobParam::set_value_from_hw(uint32_t hw_value)
{
    uint32_t knob_min = 0;
    uint32_t knob_max = FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;

    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Save the raw knob position
    _raw_pos = hw_value;

    // Get the knob control haptic mode (param_id is the knob number)
    auto mode = haptic_mode();
    auto knob_width = mode.knob_actual_width;

    // If the knob does not have the full 360 degrees of movement
    if (knob_width < 360.0f)
    {
        // Has the knob start pos been specified?
        if (mode.knob_actual_start_pos != -1.0f)
        {
            // Make sure the start pos and width do not exceed the knob limits
            if ((mode.knob_actual_start_pos + knob_width) > 360.0f)
            {
                // Truncate the knob width
                knob_width = 360.0f - mode.knob_actual_start_pos;
            }

            // Calculate the knob min and max
            knob_min = (mode.knob_actual_start_pos / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
            knob_max = ((mode.knob_actual_start_pos + knob_width) / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
        }
        else
        {
            // Normalise the knob position from from the specified width
            float offset = ((360.0f - mode.knob_actual_width)/360.0f) / 2.0f;
            knob_min = offset * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
            knob_max = (1.0 - offset) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
        }
    }

    // Is this a relative position control?
    if (!_num_positions && _relative_value_control)
    {
        uint32_t new_hw_value = hw_value;

        // Get the relative hw position
        if (hw_value < _relative_value_offset) {
            new_hw_value += (KNOB_HW_VALUE_MAX_VALUE - _relative_value_offset);
        }
        else {
            new_hw_value -= _relative_value_offset;
        }

        // Calculate the difference between the new and last positions
        int diff = new_hw_value - _last_value;
        if (diff > (32767/2)) {
            _relative_value_offset = hw_value;
            hw_value = knob_min;
        }
        else if (diff < -(32767/2)) {
            _relative_value_offset = hw_value;
            hw_value = knob_max;
        }
        else {
            hw_value = new_hw_value;
        }
        _last_value = hw_value;
    }

    // Make sure the min/max knob value has not been exceeded
    hw_value = std::clamp(hw_value, knob_min, knob_max);       

    // Does this knob have haptic detents?
    /*if (mode.knob_num_detents > 0)
    {
        // Calculate the haptic offset
        auto offset = (1.0 / mode.knob_num_detents) / 2.0;
        //MSG(offset *FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR);
        hw_value += offset;
        if (hw_value > knob_max)
            hw_value = knob_max;        
    }*/

    // Does this knob currently have haptic indents?
    if (mode.knob_indents.size() > 0)
    {
        uint32_t indent_range_start = knob_min;
        uint32_t indent_range_end;
        uint32_t knob_range_start = knob_min;
        uint32_t knob_range_end;
        bool in_deadzone = false;
        uint index = 0;

        // The knob has indents
        // Check if the knob position is within any indent deadzone
        // Note: Assumes the indents are in increasing order
        while (true)
        {
            uint32_t deadzone_start = knob_min;
            uint32_t deadzone_end = knob_max;

            // Make sure we can safely calculate the start of the deadzone, otherwise
            // use the minimum knob position
            if ((mode.knob_indents[index].second - (KNOB_HW_INDENT_WIDTH >> 1)) > knob_min)
            {
                // Calculate the start
                deadzone_start = mode.knob_indents[index].second - (KNOB_HW_INDENT_WIDTH >> 1);
            }

            // Make sure we can safely calculate the end of the deadzone, otherwise
            // use the maximum knob position
            if ((mode.knob_indents[index].second + (KNOB_HW_INDENT_WIDTH >> 1)) < knob_max)
            {
                // Calculate the end
                deadzone_end = mode.knob_indents[index].second + (KNOB_HW_INDENT_WIDTH >> 1);
            }

            // If the knob position is less than the start of this indent deadzone, break
            // from the search
            if (hw_value < deadzone_start)
            {
                // Set the range end
                indent_range_end = mode.knob_indents[index].second;
                knob_range_end = deadzone_start;
                break;
            }

            // If the knob position is less than or equal to the end of the indent deadzone, indicate the
            // knob position is within a deadzone and break from the search
            if (hw_value <= deadzone_end) 
            {
                in_deadzone = true;
                break;
            }

            // Set the range start to this indent
            indent_range_start = mode.knob_indents[index].second;
            knob_range_start = deadzone_end;

            // Have we parsed the last indent? If so, stop parsing the indents, otherwise check
            // the next indent
            if (index >= (mode.knob_indents.size() - 1))
            {
                // Set the range end
                indent_range_end = knob_max;
                knob_range_end = knob_max;
                break;
            }

            // Process the next indent
            index++;
        }

        // Is the knob position within a deadzone?
        if (in_deadzone)
        {
            // Set the knob position to be the indent position for processing
            hw_value = mode.knob_indents[index].second;
        }
        else
        {
            // We need to scale the knob position so that we get the full range of values even with
            // the deadzone
            float indent_range = indent_range_end - indent_range_start;
            float knob_range = knob_range_end - knob_range_start;
            hw_value = (uint32_t)(indent_range_start + ((indent_range)*((hw_value - knob_range_start) / knob_range)));
        }
    }

    // Normalise the knob position to a value between 0.0 and 1.0
    _current_control_state->value = ((float)hw_value - (float)knob_min) / ((float)knob_max - (float)knob_min);

    // Make sure the value is clipped to the min/max
    _current_control_state->value = std::clamp(_current_control_state->value , 0.0f, 1.0f);
}

//----------------------------------------------------------------------------
// hw_delta_outside_target_threshold
//----------------------------------------------------------------------------
bool KnobParam::hw_delta_outside_target_threshold(int32_t hw_delta, bool use_large_threshold)
{
    // Is the h/w delta within the hardware threshold?
    if (use_large_threshold)
        return ((hw_delta < -KNOB_HW_VALUE_LARGE_THRESHOLD) || (hw_delta > KNOB_HW_VALUE_LARGE_THRESHOLD));
    return ((hw_delta < -KNOB_HW_VALUE_NORMAL_THRESHOLD) || (hw_delta > KNOB_HW_VALUE_NORMAL_THRESHOLD));
}

//----------------------------------------------------------------------------
// hw_value_within_target_threshold
//----------------------------------------------------------------------------
bool KnobParam::hw_value_within_target_threshold(uint32_t hw_value, uint32_t hw_target)
{
    uint32_t min_hw_threshold_value;
    uint32_t max_hw_threshold_value;

    // Calculate the min h/w threshold value
    // Is the target less than the threshold?
    if (hw_target < KNOB_HW_VALUE_NORMAL_THRESHOLD)
        min_hw_threshold_value = 0;
    else
        min_hw_threshold_value = hw_target - KNOB_HW_VALUE_NORMAL_THRESHOLD;

    // Calculate the max h/w threshold value
    // Is the (target + threshold) greater than the maximum h/w value allowed?
    if ((hw_target + KNOB_HW_VALUE_NORMAL_THRESHOLD) > KNOB_HW_VALUE_MAX_VALUE)
        max_hw_threshold_value = KNOB_HW_VALUE_MAX_VALUE;
    else
        max_hw_threshold_value = hw_target + KNOB_HW_VALUE_NORMAL_THRESHOLD;

    // Is the h/w value within the target threshold?
    // Return a boolean to indicate the result
    return (hw_value >= min_hw_threshold_value) && (hw_value <= max_hw_threshold_value);
}

//---------------------------------
// SwitchParam class implementation
//---------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<SwitchParam> SwitchParam::CreateParam(unsigned int control_num) 
{
    return std::make_unique<SwitchParam>(control_num);
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string SwitchParam::ParamPath(unsigned int control_num)
{
    return SFC_CONTROL_PARAM_PATH_PREFIX + std::string(SWITCH_CONTROL_PATH_NAME) + std::to_string(control_num);
}

//----------------------------------------------------------------------------
// SwitchParam
//----------------------------------------------------------------------------
SwitchParam::SwitchParam(const SwitchParam& param) : SfcControlParam(param)
{
    // Clone any switch specific data
    _switch_type = param._switch_type;
}

//----------------------------------------------------------------------------
// SwitchParam
//----------------------------------------------------------------------------
SwitchParam::SwitchParam(unsigned int control_num) 
    : SfcControlParam(sfc::ControlType::SWITCH, control_num)
{
    // Initialise the switch variables
    _path = SwitchParam::ParamPath(control_num);
    _display_name = SWITCH_CONTROL_BASE_NAME + std::string(" ") + std::to_string(control_num);
    _switch_type = SwitchType::NORMAL;
    _num_positions = 2;
    _position_increment = 1.0 / _num_positions;
    _physical_pos_increment = _position_increment;
}

//----------------------------------------------------------------------------
// ~SwitchParam
//----------------------------------------------------------------------------
SwitchParam::~SwitchParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> SwitchParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<SwitchParam>(*this);
}

//----------------------------------------------------------------------------
// multifn_switch
//----------------------------------------------------------------------------
bool SwitchParam::multifn_switch() const
{
    // Return if this is a multi-function switch or not
    return _current_control_state->multifn_switch_index >= 0;
}

//----------------------------------------------------------------------------
// multifn_switch_index
//----------------------------------------------------------------------------
int SwitchParam::multifn_switch_index() const
{
    // Return hte multi-function switch index
    return _current_control_state->multifn_switch_index;
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float SwitchParam::value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the position
    float val = roundf(_current_control_state->value / _position_increment);
    if (val >= _num_positions)
        val = _num_positions - 1;
    return val;
}

//----------------------------------------------------------------------------
// value
//----------------------------------------------------------------------------
float SwitchParam::value(uint control_state_index) const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the position
    float val = roundf(_control_states[control_state_index].value / _position_increment);
    if (val >= _num_positions)
        val = _num_positions - 1;       
    return val;    
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void SwitchParam::set_value(float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // We can safely assume the number of positions is valid
    auto val = value * _position_increment;
    _current_control_state->value = std::clamp(val, 0.0f, 1.0f);
}

//----------------------------------------------------------------------------
// set_switch_type
//----------------------------------------------------------------------------
void SwitchParam::set_switch_type(SwitchType type)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the number of positions based on the passed type
    (type == SwitchType::NORMAL) ?
        _num_positions = 2 :
        _num_positions = 3;

    // Set the position increment 
    _position_increment = 1.0 / _num_positions;
}

//----------------------------------------------------------------------------
// set_as_multifn_switch
//----------------------------------------------------------------------------
void SwitchParam::set_as_multifn_switch(uint index)
{
    // Set this switch as a multi-function switch
    _current_control_state->multifn_switch_index = index;
}

//-------------------------------------
// SystemFuncParam class implementation
//-------------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<SystemFuncParam> SystemFuncParam::CreateParam(SystemFuncType system_func_type) 
{ 
    return std::make_unique<SystemFuncParam>(system_func_type);
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string SystemFuncParam::ParamPath(SystemFuncType system_func_type)
{
    return SYSTEM_FUNC_PARAM_PATH_PREFIX + SystemFunc::TypeName(system_func_type);
}

//----------------------------------------------------------------------------
// SystemFuncParam
//----------------------------------------------------------------------------
SystemFuncParam::SystemFuncParam(const SystemFuncParam& param) : Param(param)
{
    _system_func_type = param._system_func_type;
    _linked_param = param._linked_param;
}

//----------------------------------------------------------------------------
// SystemFuncParam
//----------------------------------------------------------------------------
SystemFuncParam::SystemFuncParam(SystemFuncType system_func_type) : Param(MoniqueModule::SYSTEM, ParamDataType::FLOAT)
{
    // Indicate this is a system func param type
    _preset = false;
    _save = false;
    _type = ParamType::SYSTEM_FUNC;
    _system_func_type = system_func_type;
    _linked_param = nullptr;
    _path = SystemFuncParam::ParamPath(system_func_type);
}

//----------------------------------------------------------------------------
// ~SystemFuncParam
//----------------------------------------------------------------------------
SystemFuncParam::~SystemFuncParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> SystemFuncParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<SystemFuncParam>(*this);
}

//----------------------------------------------------------------------------
// system_func_type
//----------------------------------------------------------------------------
SystemFuncType SystemFuncParam::system_func_type() const
{
    // Return the system func type
    return _system_func_type;
}

//----------------------------------------------------------------------------
// linked_param
//----------------------------------------------------------------------------
Param *SystemFuncParam::linked_param() const
{
    // Return the linked param
    return _linked_param;
}

//----------------------------------------------------------------------------
// set_linked_param
//----------------------------------------------------------------------------
void SystemFuncParam::set_linked_param(Param *param)
{
    // Set the linked param
    _linked_param = param;
}

//----------------------------------------------------------------------------
// display_string
//----------------------------------------------------------------------------
std::pair<bool, std::string> SystemFuncParam::display_string() const
{
    // Return an empty string
    return std::pair<bool, std::string>(true, "");
}

//--------------------------------
// DummyParam class implementation
//--------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<DummyParam> DummyParam::CreateParam(MoniqueModule module, std::string path) 
{
    // Create the dummy param
    auto param = std::make_unique<DummyParam>(module);
    param->_type = ParamType::GLOBAL;
    param->_preset = false;
    param->_save = false;
    param->_path = path;
    param->_display_name = path;
    return param;
}

//----------------------------------------------------------------------------
// DummyParam
//----------------------------------------------------------------------------
DummyParam::DummyParam(MoniqueModule module) : Param(module, ParamDataType::FLOAT)
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// DummyParam
//----------------------------------------------------------------------------
DummyParam::DummyParam(const DummyParam& param) : Param(param)
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> DummyParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<DummyParam>(*this);
}

//----------------------------------------------------------------------------
// ~DummyParam
//----------------------------------------------------------------------------
DummyParam::~DummyParam()
{
    // Nothing specific to do
}

//---------------------------------
// ParamChange class implementation
//---------------------------------

//----------------------------------------------------------------------------
// ParamChange
//----------------------------------------------------------------------------
ParamChange::ParamChange(const Param *param, MoniqueModule from_module)
{
    this->param = param;
    this->from_module = from_module;
    this->display = (std::strlen(param->display_name()) > 0) &&
                    ((param->module() != MoniqueModule::SFC_CONTROL) &&
                     (param->module() != MoniqueModule::MIDI_DEVICE));
    this->layer_id_mask = utils::get_current_layer_info().layer_id();
}
