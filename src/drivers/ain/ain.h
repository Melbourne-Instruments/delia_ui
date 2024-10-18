/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  ain.h
 * @brief Analog Input (AIN) driver interface.
 *-----------------------------------------------------------------------------
 */
#ifndef _AIN_H
#define _AIN_H

namespace ain
{
    // Inteface functions
    void init();
    void deinit();
    bool read_ain0(float& normalised_value);
    bool read_ain1(float& normalised_value);
}

#endif  // _AIN_H
