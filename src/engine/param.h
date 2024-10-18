/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  param.h
 * @brief Parameter class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _PARAM_H
#define _PARAM_H

#include <memory>
#include <string>
#include <cstring>
#include <functional>
#include <mutex>
#include <vector>
#include "ui_common.h"
#include "system_func.h"
#include "sfc.h"

// External classes
class BaseManager;

// Param Type
enum class ParamType
{
    GLOBAL,
    PRESET_COMMON,
    LAYER,
    PATCH_COMMON,
    PATCH_STATE,
    SYSTEM_FUNC
};

// Param Data Type
enum class ParamDataType
{
    FLOAT,
    STRING
};

// Param List Type
enum class ParamListType
{
    NORMAL,
    ADSR_ENVELOPE,
    VCF_CUTOFF
};

// Switch type
enum class SwitchType
{
    NORMAL,
    TRI_STATE
};

// Switch value
enum SwitchValue
{
    OFF,
    ON,
    ON_TRI
};

// Context Specific Param
struct ContextSpecificParams
{
    Param *context_param;
    float context_value;
    std::vector<Param *> param_list;
};

// Base Param class
class Param 
{
public:
    // Helper functions
    static std::string ParamPath(std::string name);
    static std::string ParamPath(const BaseManager *msg, std::string name);
    static std::string ParamPath(MoniqueModule module, std::string name);
    static std::unique_ptr<Param> CreateParam(int param_id, std::string param_name, std::string display_name, ParamDataType data_type=ParamDataType::FLOAT);
    static std::unique_ptr<Param> CreateParam(const BaseManager *mgr, int param_id, std::string param_name, std::string display_name, ParamDataType data_type=ParamDataType::FLOAT);
    static std::unique_ptr<Param> CreateParam(MoniqueModule module, int param_id, std::string param_name, std::string display_name, ParamDataType data_type=ParamDataType::FLOAT);

    // Constructors
    Param(const Param &param);
    Param(MoniqueModule module, ParamDataType data_type);
    virtual std::unique_ptr<Param> clone() const;

    // Destructor
    virtual ~Param();

    // General public functions
    MoniqueModule module() const { return _module; }
    ParamDataType data_type() const { return _data_type; }
    ParamType type() const;
    int processor_id() const;
    virtual int param_id() const;
    bool preset() const;
    bool save() const;
    std::string path() const;
    std::string ref() const;
    const char *display_name() const;
    std::string param_list_name() const;
    std::string param_list_display_name() const;
    ParamListType param_list_type() const;
    std::vector<Param *> param_list() const;
    bool cmp_path(std::string path) const;
    void set_type(ParamType type);
    virtual void set_processor_id(int processor_id);
    void set_preset(bool preset);
    void set_save(bool save);
    void set_ref(std::string ref);
    void set_display_name(std::string name);
    void set_param_list_name(std::string name);
    void set_param_list_display_name(std::string name);
    void set_param_list_type(ParamListType type);
    void set_param_list(std::vector<Param *> list);
    void set_context_specific_param_list(std::vector<ContextSpecificParams>& list);
    virtual std::vector<Param *> mapped_params(const Param *containing_param) const;
    virtual void add_mapped_param(Param *param);
    virtual void clear_mapped_params();
    bool is_linked_param() const;
    bool is_linked_param_enabled() const;
    void enable_linked_param(bool enable);
    void set_as_linked_param();
    bool sfc_control() const;

    // Float value public functions
    virtual float hr_value() const;
    virtual float value() const;
    virtual std::pair<bool, std::string> display_string() const;
    virtual std::string position_string(uint pos) const;
    virtual std::string display_tag() const;
    virtual bool display_enum_list() const;
    virtual int position_value() const;
    virtual uint num_positions() const;
    virtual float position_increment() const;
    virtual float physical_position_increment() const;
    virtual void set_hr_value(float value);
    virtual void set_value(float value);
    virtual void set_value_from_param(const Param& param);
    virtual void set_value_from_position(uint position, bool force=false);
    virtual void set_position_param(uint num_positions);
    virtual void set_actual_num_positions(uint num_positions);
    virtual void set_display_min_value(int min);
    virtual void set_display_max_value(int min);
    virtual void set_display_decimal_places(uint num);
    virtual void set_display_as_numeric(bool display);
    virtual void set_display_enum_list(bool display);
    virtual void set_display_hr_value(bool display);
    void add_value_string(std::string value);
    void add_value_tag(std::string tag);

    // String value public functions
    virtual std::string str_value() const;
    virtual void set_str_value(std::string value);

    // Specific for Mod Matrix params
    bool mod_matrix_param() const;
    std::string mod_src_name() const;
    std::string mod_dst_name() const;     
     void set_as_mod_matrix_param(std::string src_name, std::string mod_dst);

    // Specific for Sequencer Chunk params
    bool seq_chunk_param() const;
    void set_as_seq_chunk_param();
    bool seq_chunk_param_is_reset() const;
    void reset_seq_chunk_param();

protected:
    // Protected variables
    mutable std::mutex _mutex;
    const MoniqueModule _module;
    const ParamDataType _data_type;
    ParamType _type;
    int _processor_id;
    int _param_id;
    bool _preset;
    bool _save;
    std::string _path;
    std::string _ref;
    std::string _display_name;
    std::string _param_list_name;
    std::string _param_list_display_name;
    ParamListType _param_list_type;
    std::vector<Param *> _param_list;
    std::vector<ContextSpecificParams> _context_specific_param_list;
    std::vector<Param *> _mapped_params;
    bool _linked_param;
    bool _linked_param_enabled;
    bool _sfc_control;
    float _value;
    uint _num_positions;
    uint _actual_num_positions;
    float _position_increment;
    float _physical_pos_increment;
    float _display_range_min;
    float _display_range_max;
    uint _display_decimal_places;
    std::vector<std::string> _value_strings;
    std::vector<std::string> _value_tags;
    bool _display_as_numeric;
    bool _display_enum_list;
    bool _display_hr_value;
    std::string _str_value;
    bool _mod_matrix_param;
    std::string _mod_src_name;
    std::string _mod_dst_name;
    bool _is_seq_chunk_param;

    // Private functions
    int _position_value(float from_value, float pos_increment) const;
    float _value_from_param(const Param& param) const;
};

// Layer param
class LayerParam : public Param
{
public:
    // Helper functions
    static std::unique_ptr<LayerParam> CreateParam(int param_id, std::string param_name, std::string display_name, ParamDataType data_type=ParamDataType::FLOAT);
    static std::unique_ptr<LayerParam> CreateParam(const BaseManager *mgr, int param_id, std::string param_name, std::string display_name, ParamDataType data_type=ParamDataType::FLOAT);
    static std::unique_ptr<LayerParam> CreateParam(const BaseManager *mgr, std::string param_name, std::string display_name, ParamDataType data_type=ParamDataType::FLOAT);

    // Constructors
    LayerParam(const LayerParam &param);
    LayerParam(MoniqueModule module, ParamDataType data_type);
    virtual std::unique_ptr<Param> clone() const;

    // Destructor
    ~LayerParam();

    // General public functions
    virtual int param_id() const;
    virtual int param_id(LayerId layer_id) const;
    void set_param_id(LayerId layer_id, int param_id);

    // Float value public functions
    float hr_value() const;
    float hr_value(LayerId layer_id) const;
    float value() const;
    virtual float value(LayerId layer_id) const;
    virtual int position_value() const;
    int position_value(LayerId layer_id) const;
    void set_hr_value(float value);
    virtual void set_hr_value(LayerId layer_id, float value);
    void set_value(float value);
    virtual void set_value(LayerId layer_id, float value);
    void set_value_from_param(const Param &param);
    virtual void set_value_from_param(LayerId layer_id, const Param &param);
    void set_value_from_position(uint position, bool force=false);
    void set_value_from_position(LayerId layer_id, uint position, bool force=false);

    // String value public functions
    std::string str_value() const;
    void set_str_value(std::string value);
    void set_str_value(LayerId layer_id, std::string value);

protected:
    // Protected variables
    int _param_id_d1;
    float _value_d1;
    std::string _str_value_d1;
};

// Layer State param
class LayerStateParam : public LayerParam
{
public:
    // Helper functions
    static std::unique_ptr<LayerStateParam> CreateParam(int param_id, std::string param_name, std::string display_name, ParamDataType data_type=ParamDataType::FLOAT);
    static std::unique_ptr<LayerStateParam> CreateParam(const BaseManager *mgr, std::string param_name, std::string display_name, ParamDataType data_type=ParamDataType::FLOAT);

    // Constructors
    LayerStateParam(const LayerStateParam &param);
    LayerStateParam(MoniqueModule module, ParamDataType data_type);
    virtual std::unique_ptr<Param> clone() const;

    // Destructor
    ~LayerStateParam();

    // General public functions
    int param_id() const;
    int param_id(LayerId layer_id) const;
    int param_id(LayerState state) const;
    int param_id(LayerId layer_id, LayerState state) const;
    bool state_a_only_param() const;
    void set_param_id(LayerId layer_id, LayerState state, int param_id);
    void set_state_a_only_param(bool set);

    // Float value Public functions
    float hr_value() const;
    float value() const;
    float value(LayerId layer_id) const;
    float value(LayerState state) const;
    float value(LayerId layer_id, LayerState state) const;
    int position_value() const;
    int position_value(LayerId layer_id, LayerState state) const;
    void set_hr_value(float value);
    void set_hr_value(LayerId layer_id, float value);
    void set_hr_value(LayerId layer_id, LayerState state, float value);
    void set_value(float value);
    void set_value(LayerId layer_id, float value);
    void set_value_from_param(const Param &param);
    void set_value_from_param(LayerId layer_id, const Param &param);
    void set_value_from_position(uint position, bool force=false);
    void set_state_value(LayerId layer_id, LayerState state, float value);

    // String value public functions
    std::string str_value() const;
    std::string str_value(LayerId layer_id, LayerState state) const;
    void set_str_value(std::string value);
    void set_str_state_value(LayerId layer_id, LayerState state, std::string value);    

protected:
    // Protected variables
    bool _state_a_only_param;
    int _param_id_d0_state_b;
    int _param_id_d1_state_b;
    float _value_d0_state_b;
    float _value_d1_state_b;
    bool _seq_chunk_param;
    std::string _str_value_d0_state_b;
    std::string _str_value_d1_state_b;
};

// Surface Control State struct
struct SfcControlState
{
    float value;
    std::string state;
    std::string haptic_mode_name;
    std::vector<Param *> mapped_params;
    std::string group_name;
    Param *group_param;
    int multifn_switch_index;
    bool morphable;

    SfcControlState() {
        value = 0.0f;
        state = "";
        haptic_mode_name = "";
        mapped_params.clear();
        group_name = "";
        group_param = nullptr;
        multifn_switch_index = -1;
        morphable = false;
    }
};

// Surface Control Param class
class SfcControlParam : public Param
{
public:
    // Constructor
    SfcControlParam(const SfcControlParam &param);
    SfcControlParam(sfc::ControlType control_type, uint control_num);
    virtual std::unique_ptr<Param> clone() const;

    // Destructor
    ~SfcControlParam();

    // General public functions
    sfc::ControlType control_type() const;
    uint control_num() const;
    std::vector<Param *> mapped_params(const Param *containing_param) const;
    std::vector<Param *> mapped_params(uint control_state_index) const;
    void add_mapped_param(Param *param);
    void clear_mapped_params();

    // Float value public functions 
    virtual float value() const;
    virtual float value(uint control_state_index) const;
    virtual void set_value_from_param(const Param& param);
    virtual void set_value_from_param(uint control_state_index, const Param& param);

    // Surface Control specific functions
    bool grouped_control() const;
    std::string group_name() const;
    Param *group_param() const;
    const sfc::HapticMode& haptic_mode() const;
    uint num_control_states() const;
    bool is_current_control_state(uint control_state_index) const;
    std::string control_state() const;
    bool morphable() const;
    bool morphable(uint control_state_index) const;
    void set_group_name(const std::string group_name);
    void set_group_param(Param *param);
    void set_haptic_mode(const std::string haptic_mode_name);
    bool has_control_state(std::string state);
    void add_control_state(std::string state);
    void set_default_control_state();
    bool set_control_state(std::string state);
    void set_morphable(bool morphable);

protected:
    // Protected variables
    sfc::ControlType _control_type;
    uint _control_num;
    std::vector<SfcControlState> _control_states;
    SfcControlState *_current_control_state;
    std::string _default_control_state_name;
};

// Knob Param class
class KnobParam : public SfcControlParam
{
public:
    // Helper functions
    static std::unique_ptr<KnobParam> CreateParam(unsigned int control_num);
    static std::string ParamPath(unsigned int control_num);

    // Constructor
    KnobParam(const KnobParam &param);
    KnobParam(unsigned int control_num);
    std::unique_ptr<Param> clone() const;

    // Destructor
    ~KnobParam();

    // Public functions
    float value() const;
    float value(uint control_state_index) const;
    uint position_value(uint prev_pos);
    uint32_t hw_value() const;
    void set_value(float value);
    void set_position_param(uint position, uint num_selectable_positions);
    void set_relative_value_param(float value);
    void reset();
    void set_value_from_hw(uint32_t hw_value);
    bool hw_delta_outside_target_threshold(int32_t hw_delta, bool use_large_threshold);
    bool hw_value_within_target_threshold(uint32_t hw_value, uint32_t hw_target);
 
private:
    // Private variables
    bool _relative_value_control;
    int _last_actual_pos;
    uint _num_selectable_positions;
    uint32_t _relative_value_offset;
    uint32_t _last_value;
    uint32_t _raw_pos;
};

// Switch Param class
class SwitchParam : public SfcControlParam
{
public:
    // Helper functions
    static std::unique_ptr<SwitchParam> CreateParam(unsigned int control_num);
    static std::string ParamPath(unsigned int control_num);

    // Constructor
    SwitchParam(const SwitchParam &param);
    SwitchParam(unsigned int control_num);
    std::unique_ptr<Param> clone() const;
    
    // Destructor
    ~SwitchParam();

    // Public functions
    SwitchType switch_type() const { return _switch_type; }
    bool multifn_switch() const;
    int multifn_switch_index() const;
    float value() const;
    float value(uint control_state_index) const;
    void set_value(float value);
    void set_switch_type(SwitchType type);
    void set_as_multifn_switch(uint index);

private:
    // Private variables
    SwitchType _switch_type;
};

// System Func Param class
class SystemFuncParam : public Param
{
public:
    // Helper functions
    static std::unique_ptr<SystemFuncParam> CreateParam(SystemFuncType system_func_type);
    static std::string ParamPath(SystemFuncType system_func_type);

    // Constructor
    SystemFuncParam(const SystemFuncParam &param);
    SystemFuncParam(SystemFuncType system_func_type);
    virtual std::unique_ptr<Param> clone() const;
    
    // Destructor
    ~SystemFuncParam();

    // Public functions
    SystemFuncType system_func_type() const;
    Param *linked_param() const;
    void set_linked_param(Param *param);
    std::pair<bool, std::string> display_string() const;

private:
    // Private variables
    SystemFuncType _system_func_type;
    Param *_linked_param;
};

// Dummy Param class
class DummyParam : public Param
{
public:
    // Helper functions
    static std::unique_ptr<DummyParam> CreateParam(MoniqueModule module, std::string path);

    // Constructor
    DummyParam(MoniqueModule module);
    DummyParam(const DummyParam& param);
    std::unique_ptr<Param> clone() const;

    // Destructor
    ~DummyParam();
};

// Param change
struct ParamChange
{
    // Constructor
    ParamChange() {}
    ParamChange(const Param *param, MoniqueModule from_module);

    // Public variables
    const Param *param;
    MoniqueModule from_module;
    bool display;
    uint layer_id_mask;
};

#endif  // _PARAM_H
