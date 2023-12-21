/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_TIMER_H
#define MSP430_TIMER_H

#include "chardev/char-fe.h"
#include "hw/clock.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MSP430_TIMER "msp430-timer"
#define TYPE_MSP430_TIMER_A "msp430-timer-a"
#define TYPE_MSP430_TIMER_B "msp430-timer-b"
OBJECT_DECLARE_SIMPLE_TYPE(MSP430TimerState, MSP430_TIMER)

#define TIMER_CCRS 7

struct MSP430TimerState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion memory, memory_iv;
    QEMUTimer timer;
    qemu_irq ccr0_irq, irq;
    qemu_irq out_signal[TIMER_CCRS];
    Clock *tclk, *aclk, *smclk, *inclk;

    int64_t last_updated;
    uint32_t event_cycles;
    int ccia[TIMER_CCRS], ccib[TIMER_CCRS];
    uint32_t timers;
    bool capture_unread[TIMER_CCRS];
    bool out[TIMER_CCRS];
    bool down;

    uint16_t ctl, r;
    uint16_t cctl[TIMER_CCRS], ccr[TIMER_CCRS], cl[TIMER_CCRS];
};

#endif /* MSP430_TIMER_H */
