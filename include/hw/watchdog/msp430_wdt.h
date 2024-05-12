/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_WDT_H
#define MSP430_WDT_H

#include "chardev/char-fe.h"
#include "hw/clock.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MSP430_WDT "msp430-wdt"
#define TYPE_MSP430_WDTP "msp430-wdtp"
OBJECT_DECLARE_SIMPLE_TYPE(MSP430WDTState, MSP430_WDT)
OBJECT_DECLARE_SIMPLE_TYPE(MSP430WDTPState, MSP430_WDTP)

struct MSP430WDTState {
    SysBusDevice parent_obj;

    MemoryRegion memory;
    QEMUTimer timer;
    qemu_irq nmi, puc, irq;
    Clock *aclk, *smclk;

    int64_t last_updated;
    uint16_t event_cycles;
    bool rst_nmi_level;

    uint16_t cnt;
    uint8_t ctl;
};

struct stateful_irq {
    qemu_irq irq;
    bool level;
};

struct MSP430WDTPState {
    MSP430WDTState parent_obj;

    struct stateful_irq mclk_req, aclk_req, smclk_req;
    Clock *mclk;
};

#endif /* MSP430_WDT_H */
