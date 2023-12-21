/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/timer/msp430_timer.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

REG16(CTL, 0x0)
    FIELD(CTL, CLGRP, 13, 2)
    FIELD(CTL, CNTL, 11, 2)
    FIELD(CTL, SSEL, 8, 2)
    FIELD(CTL, ID, 6, 2)
    FIELD(CTL, MC, 4, 2)
    FIELD(CTL, CLR, 2, 1)
    FIELD(CTL, IE, 1, 1)
    FIELD(CTL, IFG, 1, 1)
REG16(R, 0x10)
/* Common CCTL fields */
    FIELD(CCTL, CM, 14, 2)
    FIELD(CCTL, IS, 12, 2)
    FIELD(CCTL, SCS, 11, 1)
    FIELD(CCTL, SCCI, 10, 1)
    FIELD(CCTL, CLLD, 9, 2)
    FIELD(CCTL, CAP, 8, 1)
    FIELD(CCTL, OUTMOD, 5, 3)
    FIELD(CCTL, IE, 4, 1)
    FIELD(CCTL, IN, 3, 1)
    FIELD(CCTL, OUT, 2, 1)
    FIELD(CCTL, OV, 1, 1)
    FIELD(CCTL, IFG, 0, 1)

#define R_CCTL_INTERRUPT_MASK (R_CCTL_IFG_MASK | R_CCTL_IE_MASK)
#define R_CTL_INTERRUPT_MASK (R_CTL_IFG_MASK | R_CTL_IE_MASK)

typedef struct {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    bool is_a;
} MSP430TimerClass;

DECLARE_CLASS_CHECKERS(MSP430TimerClass, MSP430_TIMER, TYPE_MSP430_TIMER)

#define CLK_TO_NS (CLOCK_PERIOD_1SEC / NANOSECONDS_PER_SECOND)

static bool msp430_timer_load(MSP430TimerState *t, int i)
{
    const MSP430TimerClass *tc = MSP430_TIMER_GET_CLASS(t);

    if (tc->is_a) {
        t->cl[i] = t->ccr[i];
        return true;
    }

    switch (FIELD_EX16(t->ctl, CTL, CLGRP)) {
    case 0:
        t->cl[i] = t->ccr[i];
        return true;
    case 1:
        if (i == 0) {
            t->cl[0] = t->ccr[0];
        } else if (i == 1) {
            t->cl[1] = t->ccr[1];
            t->cl[2] = t->ccr[2];
        } else if (i == 3) {
            t->cl[3] = t->ccr[3];
            t->cl[4] = t->ccr[4];
        } else if (i == 5) {
            t->cl[5] = t->ccr[5];
            t->cl[6] = t->ccr[6];
        }
        return true;
    case 2:
        if (i == 0) {
            t->cl[0] = t->ccr[0];
        } else if (i == 1) {
            t->cl[1] = t->ccr[1];
            t->cl[2] = t->ccr[2];
            t->cl[3] = t->ccr[3];
        } else if (i == 4) {
            t->cl[4] = t->ccr[4];
            t->cl[5] = t->ccr[5];
            t->cl[6] = t->ccr[6];
        }
        return true;
    case 3:
        if (i == 1) {
            for (i = 0; i < TIMER_CCRS; i++) {
                t->cl[i] = t->ccr[i];
            }
        }
        return true;
    }

    return false;
}

static bool msp430_timer_set_irq(MSP430TimerState *t, int i)
{
    if (FIELD_EX16(t->cctl[i], CCTL, IFG) &&
        FIELD_EX16(t->cctl[i], CCTL, IE)) {
        if (i) {
            return true;
        }

        qemu_irq_raise(t->ccr0_irq);
    } else if (!i) {
        qemu_irq_lower(t->ccr0_irq);
    }

    return false;
}

static bool msp430_timer_cci(MSP430TimerState *t, int i)
{
        switch (FIELD_EX16(t->cctl[i], CCTL, IS)) {
        case 0:
            return t->ccia[i];
        case 1:
            return t->ccib[i];
        case 2:
            return false;
        case 3:
            return true;
        default:
            g_assert_not_reached();
        }
}

static int64_t msp430_timer_clock_period(MSP430TimerState *t)
{
    uint64_t clk_per;

    switch (FIELD_EX16(t->ctl, CTL, SSEL)) {
    case 0:
        clk_per = clock_get(t->tclk);
        break;
    case 1:
        clk_per = clock_get(t->aclk);
        break;
    case 2:
        clk_per = clock_get(t->smclk);
        break;
    case 3:
        clk_per = clock_get(t->inclk);
        break;
    }

    return clk_per << FIELD_EX16(t->ctl, CTL, ID);
}

static void msp430_timer_compare(MSP430TimerState *t, bool counts)
{
    const MSP430TimerClass *tc = MSP430_TIMER_GET_CLASS(t);
    bool load[TIMER_CCRS] = { };
    int level = 0;
    int i;

    for (i = 0; i < t->timers; i++) {
        if (t->cl[i] == t->r) {
            if (!(t->cctl[i] & R_CCTL_CAP_MASK)) {
                t->cctl[i] |= R_CCTL_IFG_MASK;
            }

            if (tc->is_a) {
                t->cctl[i] &= ~R_CCTL_SCCI_MASK;
                t->cctl[i] |= msp430_timer_cci(t, i) ? R_CCTL_SCCI_MASK : 0;
            } else {
                switch (FIELD_EX16(t->cctl[i], CCTL, CLLD)) {
                case 1:
                    load[i] = counts && !t->r;
                    break;
                case 2:
                    if (FIELD_EX16(t->ctl, CTL, MC) == 3) {
                        load[i] = counts && (t->r == t->ccr[0] || !t->r);
                    } else {
                        load[i] = counts && !t->r;
                    }
                    break;
                case 3:
                    load[i] = counts;
                    break;
                }
            }
        }

        if (msp430_timer_set_irq(t, i)) {
            level = 1;
        }

        /* TODO: set out */
    }
    
    qemu_set_irq(t->irq, level);

    for (i = 0; i < t->timers; i++) {
        if (load[i]) {
            msp430_timer_load(t, i);
        }
    }
}

static const uint16_t cntl_max[] = {
    0xffff,
    0x0fff,
    0x03ff,
    0x00ff,
};

#include "qemu/qemu-print.h"

static void msp430_timer_recalculate(MSP430TimerState *t)
{
    uint16_t mc = FIELD_EX16(t->ctl, CTL, MC);
    int64_t event_time_ns, clk_per = msp430_timer_clock_period(t);
    uint16_t max;
    int i;

    max = cntl_max[FIELD_EX16(t->ctl, CTL, CNTL)];
    if (mc != 2 && t->cl[0] < max) {
        max = t->cl[0];
        t->r %= max + 1;
    } else {
        t->r &= max;
    }

    switch (mc) {
    case 0:
        t->event_cycles = 0;
        break;
    case 3:
        if (t->down) {
            t->event_cycles = t->r ?: max + 1;
            break;
        }
        /* fallthrough */
    case 1:
    case 2:
        t->event_cycles = max - t->r ?: max + 1;
        break;
    }

    assert(!clk_per || !mc || t->event_cycles);
    for (i = 0; i < t->timers; i++) {
        uint32_t timer_cycles;

        switch (mc) {
        case 0:
            timer_cycles = 0;
            break;
        case 3:
            if (t->down) {
                if (t->r > t->cl[i]) {
                    timer_cycles = t->r - t->cl[i];
                } else {
                    timer_cycles = 2 * (t->cl[0] + 1) - (t->cl[i] - t->r);
                }
                break;
            }
            /* fallthrough */
        case 1:
        case 2:
            if (t->r < t->cl[i]) {
                timer_cycles = t->cl[i] - t->r;
            } else {
                timer_cycles = max + 1 - (t->r - t->cl[i]);
            }
            break;
        }

        if (timer_cycles < t->event_cycles) {
            t->event_cycles = timer_cycles;
        }
    }

    assert(!clk_per || !mc || t->event_cycles);
    event_time_ns = t->event_cycles * (clk_per / CLK_TO_NS);
    if (event_time_ns) {
        //qemu_printf("scheduling %10" PRIi64 " %10" PRIi64 " after %10" PRIi64 "\n",
        //            t->last_updated + event_time_ns, event_time_ns, t->last_updated);
        timer_mod(&t->timer, t->last_updated + event_time_ns);
    } else {
        timer_del(&t->timer);
    }
}

static bool msp430_timer_update(MSP430TimerState *t)
{
    int64_t clk_per = msp430_timer_clock_period(t);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t actual_cycles;
    bool recalculated = false;

    if (clk_per) {
        actual_cycles = (now - t->last_updated) / (clk_per / CLK_TO_NS);
    } else {
        actual_cycles = 0;
    }

    //qemu_printf("event at   %10" PRIi64 " %10" PRIi64 " since %10" PRIi64 "%10" PRIi64 " actual %10" PRIi32 " event_cycles\n",
    //            now, now - t->last_updated, t->last_updated, actual_cycles,
    //            t->event_cycles);

    while (actual_cycles) {
        uint32_t elapsed_cycles = MIN(actual_cycles, t->event_cycles);

        /* This can happen if the timer is disabled */
        if (!t->event_cycles) {
            t->last_updated = now;
            return recalculated;
        }

        if (t->down) {
            t->r -= elapsed_cycles;
        } else {
            t->r += elapsed_cycles;
        }

        actual_cycles -= elapsed_cycles;
        t->last_updated += elapsed_cycles * (clk_per / CLK_TO_NS);
        if (t->event_cycles <= elapsed_cycles) {
            msp430_timer_compare(t, true);
            msp430_timer_recalculate(t);
            recalculated = true;
        } else {
            t->event_cycles -= elapsed_cycles;
            //assert(recalculated);
        }
    }

    return recalculated;
}

static void msp430_timer_event(void *opaque)
{
    MSP430TimerState *t = opaque;

    msp430_timer_update(t);
}

static void msp430_timer_capture(MSP430TimerState *t, int i, bool old_cci)
{
    const MSP430TimerClass *tc = MSP430_TIMER_GET_CLASS(t);
    bool new_cci = msp430_timer_cci(t, i);

    if (old_cci == new_cci) {
        return;
    }

    switch (FIELD_EX16(t->cctl[i], CCTL, CM)) {
    case 0:
        return;
    case 1:
        if (!old_cci && new_cci) {
            break;
        }
        return;
    case 2:
        if (old_cci && !new_cci) {
            break;
        }
        return;
    case 3:
        if (old_cci != new_cci) {
            break;
        }
        return;
    }

    if (t->capture_unread[i]) {
        t->cctl[i] |= R_CCTL_OV_MASK;
    }
    t->capture_unread[i] = true;

    msp430_timer_update(t);
    t->ccr[i] = t->r;

    if (msp430_timer_set_irq(t, i)) {
        qemu_irq_raise(t->irq);
    }

    if (tc->is_a || !FIELD_EX16(t->cctl[i], CCTL, CLLD)) {
        if (msp430_timer_load(t, i)) {
            msp430_timer_recalculate(t);
        }
    }
}

static void msp430_timer_set_ccia(void *opaque, int irq, int level)
{
    MSP430TimerState *t = opaque;
    bool old_cci = msp430_timer_cci(t, irq);

    t->ccia[irq] = level;
    msp430_timer_capture(t, irq, old_cci);
}

static void msp430_timer_set_ccib(void *opaque, int irq, int level)
{
    MSP430TimerState *t = opaque;
    bool old_cci = msp430_timer_cci(t, irq);

    t->ccib[irq] = level;
    msp430_timer_capture(t, irq, old_cci);
}

static void msp430_timer_ack_irq(void *opaque, int irq, int level)
{
    MSP430TimerState *t = opaque;

    t->cctl[0] &= ~R_CCTL_IFG_MASK;
    qemu_irq_lower(t->ccr0_irq);
}

static void msp430_timer_tclk_callback(void *opaque, ClockEvent event)
{
    MSP430TimerState *t = opaque;

    if (!FIELD_EX16(t->ctl, CTL, SSEL)) {
        msp430_timer_recalculate(t);
    }
}

static void msp430_timer_aclk_callback(void *opaque, ClockEvent event)
{
    MSP430TimerState *t = opaque;

    if (FIELD_EX16(t->ctl, CTL, SSEL) == 1) {
        msp430_timer_recalculate(t);
    }
}

static void msp430_timer_smclk_callback(void *opaque, ClockEvent event)
{
    MSP430TimerState *t = opaque;

    if (FIELD_EX16(t->ctl, CTL, SSEL) == 2) {
        msp430_timer_recalculate(t);
    }
}

static void msp430_timer_inclk_callback(void *opaque, ClockEvent event)
{
    MSP430TimerState *t = opaque;

    if (FIELD_EX16(t->ctl, CTL, SSEL) == 3) {
        msp430_timer_recalculate(t);
    }
}

static uint64_t msp430_timer_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430TimerState *t = opaque;
    int i = ((long)addr & 0xf) / 2 - 1;

    if (addr == A_CTL) {
        return t->ctl;
    }

    if (addr == A_R) {
        msp430_timer_update(t);
        return t->r;
    }

    if (i >= t->timers) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "msp430_timer: No register at 0x%" HWADDR_PRIX ".\n",
                      addr);
        return UINT64_MAX;
    }

    if (addr < A_R) {
        t->cctl[i] &= ~R_CCTL_IN_MASK;
        t->cctl[i] |= msp430_timer_cci(t, i) ? R_CCTL_IN_MASK : 0;
        return t->cctl[i];
    }

    t->capture_unread[i] = false;
    return t->ccr[i];
}

static void msp430_timer_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430TimerState *t = opaque;
    const MSP430TimerClass *tc = MSP430_TIMER_GET_CLASS(t);
    int i = ((long)addr & 0xf) / 2 - 1;

    msp430_timer_update(t);

    if (addr == A_CTL) {
        if (val & R_CTL_CLR_MASK) { 
            t->r = 0;
            t->down = false;
        }
        val &= ~R_CTL_CLR_MASK;
        if (tc->is_a) {
            val &= ~(R_CTL_CLGRP_MASK | R_CTL_CNTL_MASK);
        }
        t->ctl = val;
    } else if (addr == A_R) {
        t->r = val;
    } else if (i >= t->timers) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "msp430_timer: No register at 0x%" HWADDR_PRIX ".\n",
                      addr);
        return;
    } else if (addr < A_R) {
        if (FIELD_EX16(val, CCTL, OUTMOD)) {
            qemu_log_mask(LOG_UNIMP, "msp430_timer: OUTMOD not implemented\n");
        }

        if ((t->cctl[i] & R_CCTL_OV_MASK) && !(val & R_CCTL_OV_MASK)) {
            t->capture_unread[i] = false;
        }
        t->cctl[i] = val;
    } else {
        t->ccr[i] = val;
        if (tc->is_a || !FIELD_EX16(t->cctl[i], CCTL, CLLD)) {
            msp430_timer_load(t, i);
        }
    }

    msp430_timer_compare(t, false);
    msp430_timer_recalculate(t);
}

static const MemoryRegionOps msp430_timer_ops = {
    .write = msp430_timer_write,
    .read  = msp430_timer_read,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
};

static uint64_t msp430_timer_iv_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430TimerState *t = opaque;
    bool any_irq = false;
    uint64_t ret = 0;
    int i;

    for (i = 1; i < t->timers; i++) {
        if ((t->cctl[i] & R_CCTL_INTERRUPT_MASK) == R_CCTL_INTERRUPT_MASK) {
            if (ret) {
                any_irq = true;
            } else {
                t->cctl[i] &= ~R_CCTL_IFG_MASK;
                ret = i * 2;
            }
        }
    }

    if ((t->ctl & R_CTL_INTERRUPT_MASK) == R_CTL_INTERRUPT_MASK) {
        if (ret) {
            any_irq = true;
        } else {
            t->ctl &= ~R_CTL_IFG_MASK;
            ret = 0x0E;
        }
    }

    qemu_set_irq(t->irq, any_irq);
    return ret;
}

static void msp430_timer_iv_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    msp430_timer_iv_read(opaque, addr, size);
}

static const MemoryRegionOps msp430_timer_iv_ops = {
    .write = msp430_timer_iv_write,
    .read  = msp430_timer_iv_read,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
};

static const ClockPortInitArray msp430_timer_clocks = {
    QDEV_CLOCK_IN(MSP430TimerState, tclk, msp430_timer_tclk_callback, ClockUpdate),
    QDEV_CLOCK_IN(MSP430TimerState, aclk, msp430_timer_aclk_callback, ClockUpdate),
    QDEV_CLOCK_IN(MSP430TimerState, smclk, msp430_timer_smclk_callback, ClockUpdate),
    QDEV_CLOCK_IN(MSP430TimerState, inclk, msp430_timer_inclk_callback, ClockUpdate),
    QDEV_CLOCK_END,
};

static void msp430_timer_realize(DeviceState *dev, Error **errp)
{
    MSP430TimerState *t = MSP430_TIMER(dev);

    qdev_init_gpio_in_named(dev, msp430_timer_set_ccia, "ccia", t->timers);
    qdev_init_gpio_in_named(dev, msp430_timer_set_ccib, "ccib", t->timers);
}

static void msp430_timer_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MSP430TimerState *t = MSP430_TIMER(obj);

    memory_region_init_io(&t->memory, obj, &msp430_timer_ops, t,
                          "msp430-timer", 0x20);
    sysbus_init_mmio(d, &t->memory);

    memory_region_init_io(&t->memory_iv, obj, &msp430_timer_iv_ops, t,
                          "msp430-timer-iv", 2);
    sysbus_init_mmio(d, &t->memory_iv);

    timer_init_ns(&t->timer, QEMU_CLOCK_VIRTUAL, msp430_timer_event, t);
    qdev_init_clocks(DEVICE(obj), msp430_timer_clocks);
    qdev_init_gpio_in_named(DEVICE(d), msp430_timer_ack_irq, "ack", 1);
    sysbus_init_irq(d, &t->ccr0_irq);
    sysbus_init_irq(d, &t->irq);
}

static const VMStateDescription vmstate_msp430_timer = {
    .name = "msp430-timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(ctl, MSP430TimerState),
        VMSTATE_UINT16(r, MSP430TimerState),
        VMSTATE_UINT16_ARRAY(cctl, MSP430TimerState, TIMER_CCRS),
        VMSTATE_UINT16_ARRAY(ccr, MSP430TimerState, TIMER_CCRS),
        VMSTATE_UINT16_ARRAY(cl, MSP430TimerState, TIMER_CCRS),
        VMSTATE_CLOCK(tclk, MSP430TimerState),
        VMSTATE_CLOCK(aclk, MSP430TimerState),
        VMSTATE_CLOCK(smclk, MSP430TimerState),
        VMSTATE_CLOCK(inclk, MSP430TimerState),
        VMSTATE_TIMER(timer, MSP430TimerState),
        VMSTATE_INT64(last_updated, MSP430TimerState),
        VMSTATE_BOOL_ARRAY(capture_unread, MSP430TimerState, TIMER_CCRS),
        VMSTATE_BOOL_ARRAY(out, MSP430TimerState, TIMER_CCRS),
        VMSTATE_BOOL(down, MSP430TimerState),
        VMSTATE_END_OF_LIST()
    }
};

static Property msp430_timer_properties[] = {
    DEFINE_PROP_UINT32("timers", MSP430TimerState, timers, 7),
    DEFINE_PROP_END_OF_LIST(),
};

static void msp430_timer_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = msp430_timer_realize;
    dc->vmsd = &vmstate_msp430_timer;
    device_class_set_props(dc, msp430_timer_properties);
}

static void msp430_timer_a_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MSP430TimerClass *tc = MSP430_TIMER_CLASS(oc);

    dc->desc = "MSP430 Timer A";
    tc->is_a = true;
}

static void msp430_timer_b_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = "MSP430 Timer B";
}

static const TypeInfo msp430_timer_types[] = {
    {
        .name = TYPE_MSP430_TIMER,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(MSP430TimerState),
        .instance_init = msp430_timer_init,
        .class_size = sizeof(MSP430TimerClass),
        .class_init = msp430_timer_class_init,
        .abstract = true,
    },
    {
        .name = TYPE_MSP430_TIMER_A,
        .parent = TYPE_MSP430_TIMER,
        .class_init = msp430_timer_a_class_init,
    },
    {
        .name = TYPE_MSP430_TIMER_B,
        .parent = TYPE_MSP430_TIMER,
        .class_init = msp430_timer_b_class_init,
    },
};

DEFINE_TYPES(msp430_timer_types)
