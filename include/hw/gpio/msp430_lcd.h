/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_LCD_H
#define MSP430_LCD_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_MSP430_LCD "msp430-lcd"
OBJECT_DECLARE_SIMPLE_TYPE(MSP430LCDState, MSP430_LCD)

#define MSP430_LCD_SEGMENTS 40
#define MSP430_LCD_COMMON   4

struct MSP430LCDState {
    SysBusDevice parent_obj;

    MemoryRegion memory;
    Clock *clk;
    qemu_irq out[MSP430_LCD_COMMON][MSP430_LCD_SEGMENTS];

    uint8_t ctl, m[MSP430_LCD_SEGMENTS / 2];

    bool clk_on;
};

#endif /* MSP430_LCD_H */
