/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_FLLP_H
#define MSP430_FLLP_H

#include "hw/clock.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MSP430_FLLP "msp430-fllp"
OBJECT_DECLARE_SIMPLE_TYPE(MSP430FLLPState, MSP430_FLLP)

struct MSP430FLLPState {
    SysBusDevice parent_obj;

    MemoryRegion memory;
    Clock *xt1, *xt2, *aclk, *aclk_n, *mclk, *smclk;
    qemu_irq irq;

    uint8_t scfqctl;
    uint8_t scfi0;
    uint8_t scfi1;
    uint8_t fll_ctl0;
    uint8_t fll_ctl1;
    uint8_t fll_ctl2;
    int cpuoff, oscoff;

    bool has_xts, has_sel, has_vlo;

    uint64_t vlo_freq;
};

#endif /* MSP430_FLLP_H */
