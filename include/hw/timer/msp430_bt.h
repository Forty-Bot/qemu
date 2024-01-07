/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_BASIC_TIMER_H
#define MSP430_BASIC_TIMER_H

#include "chardev/char-fe.h"
#include "hw/clock.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MSP430_BASIC_TIMER "msp430-basic-timer"
OBJECT_DECLARE_SIMPLE_TYPE(MSP430BasicTimerState, MSP430_BASIC_TIMER)

struct MSP430BasicTimerState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion memctl, memcnt;
    QEMUTimer timer;
    qemu_irq irq;
    Clock *aclk, *smclk, *lcdclk;

    int64_t last_updated;
    uint32_t event_cycles;

    uint8_t ctl, cnt[2];
};

#endif /* MSP430_BASIC_TIMER_H */
