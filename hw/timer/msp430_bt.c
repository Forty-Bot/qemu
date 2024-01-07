/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/timer/msp430_bt.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

/* CTL */
FIELD(CTL, SSEL, 7, 1)
FIELD(CTL, HOLD, 6, 1)
FIELD(CTL, DIV, 5, 1)
FIELD(CTL, FRFQ, 3, 2)
FIELD(CTL, IP, 0, 3)

#define CLK_TO_NS (CLOCK_PERIOD_1SEC / NANOSECONDS_PER_SECOND)

static bool basic_timer_using_smclk(MSP430BasicTimerState *bt)
{
    return (bt->ctl & R_CTL_SSEL_MASK) && !(bt->ctl & R_CTL_DIV_MASK);
}

static int64_t basic_timer_clock_period(MSP430BasicTimerState *bt, int64_t *aclk_per)
{
    *aclk_per = clock_get(bt->aclk);
    if (basic_timer_using_smclk(bt)) {
        return clock_get(bt->smclk);
    }

    return *aclk_per;
}

static uint16_t basic_timer_cnt(MSP430BasicTimerState *bt)
{
    uint16_t cnt = bt->cnt[1];

    if (bt->ctl & R_CTL_DIV_MASK) {
        cnt = (cnt << 8) | bt->cnt[0];
    }

    return cnt;
}

static void basic_timer_recalculate(MSP430BasicTimerState *bt)
{
    int64_t event_time_ns, lcdclk_per, clk_per, aclk_per;

    clk_per = basic_timer_clock_period(bt, &aclk_per);
    lcdclk_per = aclk_per << (FIELD_EX8(bt->ctl, CTL, FRFQ) + 5);
    if (bt->ctl & R_CTL_HOLD_MASK) {
        if (bt->ctl & R_CTL_DIV_MASK) {
            lcdclk_per = 0;
        }
        bt->event_cycles = 0;
    } else {
        uint16_t cnt = basic_timer_cnt(bt);
        uint32_t per = BIT(FIELD_EX8(bt->ctl, CTL, IP) + 1);

        if (bt->ctl & R_CTL_DIV_MASK) {
            per <<= 8;
        }
        bt->event_cycles = (per - cnt) & ((per << 1) - 1) ?: per;
    }

    clock_set(bt->lcdclk, lcdclk_per);
    clock_propagate(bt->lcdclk);

    event_time_ns = bt->event_cycles * (clk_per / CLK_TO_NS);
    if (event_time_ns) {
        timer_mod(&bt->timer, bt->last_updated + event_time_ns);
    } else {
        timer_del(&bt->timer);
    }
}

static void basic_timer_update(MSP430BasicTimerState *bt)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t actual_cycles, aclk_per, clk_per;
    uint16_t cnt;

    clk_per = basic_timer_clock_period(bt, &aclk_per);
    if (clk_per) {
        actual_cycles = (now - bt->last_updated) / (clk_per / CLK_TO_NS);
    } else {
        actual_cycles = 0;
    }

    cnt = (basic_timer_cnt(bt) + actual_cycles);
    if (bt->ctl & R_CTL_DIV_MASK) {
        bt->cnt[0] = cnt & 0xff;
        bt->cnt[1] = cnt >> 8;
    } else {
        int64_t aclk_cycles = actual_cycles;

        if (basic_timer_using_smclk(bt)) {
            if (aclk_per) {
                aclk_cycles = (now - bt->last_updated) / (aclk_per / CLK_TO_NS);
            } else {
                aclk_cycles = 0;
            }
        }

        bt->cnt[0] += aclk_cycles;
        bt->cnt[1] = cnt & 0xff;
    }
    bt->last_updated = now;

    if (actual_cycles >= bt->event_cycles) {
        qemu_irq_raise(bt->irq);
        basic_timer_recalculate(bt);
    }
}

static void basic_timer_event(void *opaque)
{
    MSP430BasicTimerState *bt = opaque;

    basic_timer_update(bt);
}

static void basic_timer_aclk_callback(void *opaque, ClockEvent event)
{
    MSP430BasicTimerState *bt = opaque;

    basic_timer_recalculate(bt);
}

static void basic_timer_smclk_callback(void *opaque, ClockEvent event)
{
    MSP430BasicTimerState *bt = opaque;

    if (basic_timer_using_smclk(bt)) {
        basic_timer_recalculate(bt);
    }
}

static uint64_t basic_timer_ctl_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430BasicTimerState *bt = opaque;

    return bt->ctl;
}

static void basic_timer_ctl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430BasicTimerState *bt = opaque;

    basic_timer_update(bt);
    bt->ctl = val;
    basic_timer_recalculate(bt);
}

static const MemoryRegionOps basic_timer_ctl_ops = {
    .write = basic_timer_ctl_write,
    .read  = basic_timer_ctl_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static uint64_t basic_timer_cnt_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430BasicTimerState *bt = opaque;

    basic_timer_update(bt);
    return bt->cnt[addr];
}

static void basic_timer_cnt_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430BasicTimerState *bt = opaque;
    uint8_t per = BIT(FIELD_EX8(bt->ctl, CTL, IP));

    basic_timer_update(bt);
    if (addr && (val & per) && !(bt->cnt[1] & per)) {
        qemu_irq_raise(bt->irq);
    }

    bt->cnt[addr] = val;
    basic_timer_recalculate(bt);
}

static const MemoryRegionOps basic_timer_cnt_ops = {
    .write = basic_timer_cnt_write,
    .read  = basic_timer_cnt_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const ClockPortInitArray basic_timer_clocks = {
    QDEV_CLOCK_IN(MSP430BasicTimerState, aclk, basic_timer_aclk_callback, ClockUpdate),
    QDEV_CLOCK_IN(MSP430BasicTimerState, smclk, basic_timer_smclk_callback, ClockUpdate),
    QDEV_CLOCK_OUT(MSP430BasicTimerState, lcdclk),
    QDEV_CLOCK_END,
};

static void basic_timer_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MSP430BasicTimerState *bt = MSP430_BASIC_TIMER(obj);

    memory_region_init_io(&bt->memctl, obj, &basic_timer_ctl_ops, bt,
                          "msp430-basic-timer-ctl", 1);
    sysbus_init_mmio(d, &bt->memctl);

    memory_region_init_io(&bt->memcnt, obj, &basic_timer_cnt_ops, bt,
                          "msp430-basic-timer-cnt", 2);
    sysbus_init_mmio(d, &bt->memcnt);

    timer_init_ns(&bt->timer, QEMU_CLOCK_VIRTUAL, basic_timer_event, bt);
    qdev_init_clocks(DEVICE(obj), basic_timer_clocks);
    sysbus_init_irq(d, &bt->irq);
}

static const VMStateDescription vmstate_basic_timer = {
    .name = "msp430-basic-timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(ctl, MSP430BasicTimerState),
        VMSTATE_UINT8_ARRAY(cnt, MSP430BasicTimerState, 2),
        VMSTATE_CLOCK(aclk, MSP430BasicTimerState),
        VMSTATE_CLOCK(smclk, MSP430BasicTimerState),
        VMSTATE_CLOCK(lcdclk, MSP430BasicTimerState),
        VMSTATE_TIMER(timer, MSP430BasicTimerState),
        VMSTATE_INT64(last_updated, MSP430BasicTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void basic_timer_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = "MSP430 Basic Timer";
    dc->vmsd = &vmstate_basic_timer;
}

static const TypeInfo basic_timer_types[] = {
    {
        .name = TYPE_MSP430_BASIC_TIMER,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(MSP430BasicTimerState),
        .instance_init = basic_timer_init,
        .class_init = basic_timer_class_init,
    },
};

DEFINE_TYPES(basic_timer_types)
