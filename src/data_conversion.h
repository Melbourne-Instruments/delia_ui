/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  data_conversion.h
 * @brief Data conversion interface.
 *-----------------------------------------------------------------------------
 */
#ifndef _DATA_CONVERSION_H
#define _DATA_CONVERSION_H

#include "ui_common.h"

namespace dataconv
{
    // Convert TO a normalised float
    float to_normalised_float(MoniqueModule module, int param_id, float value);
    float pitch_bend_to_normalised_float(float value);
    float aftertouch_to_normalised_float(float value);
    float midi_cc_to_normalised_float(float value);

    // Convert FROM a normalised float
    float from_normalised_float(MoniqueModule module, int param_id, float value);
    float pitch_bend_from_normalised_float(float value);
    float aftertouch_from_normalised_float(float value);
    float midi_cc_from_normalised_float(float value); 
};

#endif  // _DATA_CONVERSION_H
