/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_MPY_H
#define MSP430_MPY_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_MSP430_MPY "msp430-mpy"
#define TYPE_MSP430_MPY32 "msp430-mpy32"
OBJECT_DECLARE_SIMPLE_TYPE(MSP430MPYState, MSP430_MPY)

struct MSP430MPYState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion memory;

    uint64_t res;
    uint32_t op1, op2;
    uint16_t sumext, ctl0;

    bool expecting_op2h;
};

#endif /* MSP430_MPY_H */
