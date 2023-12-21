/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_BCMP_H
#define MSP430_BCMP_H

#include "hw/clock.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MSP430_BCMP "msp430-bcm+"
OBJECT_DECLARE_SIMPLE_TYPE(MSP430BCMPState, MSP430_BCMP)

struct MSP430BCMPState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion memory;
    Clock *xt1, *xt2, *aclk, *mclk, *smclk;
    qemu_irq irq;

    uint8_t dcoctl;
    uint8_t bcsctl1;
    uint8_t bcsctl2;
    uint8_t bcsctl3;
    int cpuoff, scg1;

    bool has_xts;
};

#endif /* MSP430_BCMP_H */
