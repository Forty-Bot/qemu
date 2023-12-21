/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_BCM_H
#define MSP430_BCM_H

#include "hw/clock.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MSP430_BCM "msp430-bcm"
#define TYPE_MSP430_BCMP "msp430-bcmp"
OBJECT_DECLARE_SIMPLE_TYPE(MSP430BCMState, MSP430_BCM)

struct MSP430BCMState {
    SysBusDevice parent_obj;

    MemoryRegion iomem, bcsctl3mem;
    Clock *xt1, *xt2, *aclk, *mclk, *smclk;
    qemu_irq irq;

    uint8_t dcoctl;
    uint8_t bcsctl1;
    uint8_t bcsctl2;
    uint8_t bcsctl3;
    int cpuoff, scg1;

    bool has_xts;
    uint64_t vlo_freq;
    uint64_t dco_freq[16][8];
};

void bcm_find_closest(MSP430BCMState *bcm, uint64_t freq, uint8_t *best_dcoctl,
                      uint8_t *best_bcsctl1);

#endif /* MSP430_BCM_H */
