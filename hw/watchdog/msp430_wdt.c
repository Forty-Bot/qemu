/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/watchdog/msp430_wdt.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "qapi/qapi-events-run-state.h"
#include "sysemu/runstate.h"
#include "sysemu/watchdog.h"

/* CTL */
FIELD(CTL, PW, 8, 8)
FIELD(CTL, HOLD, 7, 1)
FIELD(CTL, NMIES, 6, 1)
FIELD(CTL, NMI, 5, 1)
FIELD(CTL, TMSEL, 4, 1)
FIELD(CTL, CNTCL, 3, 1)
FIELD(CTL, SSEL, 2, 1)
FIELD(CTL, IS, 0, 2)

#define CLK_TO_NS (CLOCK_PERIOD_1SEC / NANOSECONDS_PER_SECOND)

typedef struct {
    SysBusDeviceClass parent_class;

    bool plus;
} MSP430WDTClass;

DECLARE_CLASS_CHECKERS(MSP430WDTClass, MSP430_WDT, TYPE_MSP430_WDT)

static void wdt_rst_nmi(void *opaque, int irq, int level)
{
    MSP430WDTState *wdt = opaque;

    if (wdt->ctl & R_CTL_NMI_MASK) {
        bool rising = !wdt->rst_nmi_level && level;
        bool falling = wdt->rst_nmi_level && !level;

        if (wdt->ctl & R_CTL_NMIES_MASK) {
            if (falling) {
                qemu_irq_raise(wdt->nmi);
            }
        } else if (rising) {
            qemu_irq_raise(wdt->nmi);
        }
    } else if (!level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
    wdt->rst_nmi_level = level;
}

static void wdt_ack(void *opaque, int irq, int level)
{
    MSP430WDTState *wdt = opaque;

    if (wdt->ctl & R_CTL_TMSEL_MASK) {
        qemu_irq_lower(wdt->irq);
    }
}

static void set_irq(struct stateful_irq *irq, int level)
{
    if (irq->level != level) {
        qemu_set_irq(irq->irq, level);
    }
    irq->level = level;
}

static int64_t wdt_clock_period(MSP430WDTState *wdt)
{
    const MSP430WDTClass *wc = MSP430_WDT_GET_CLASS(wdt);
    MSP430WDTPState *wdtp = NULL;
    uint64_t period;

    if (wc->plus) {
        wdtp = MSP430_WDTP(wdt);
        if (wdt->ctl & R_CTL_TMSEL_MASK) {
            set_irq(&wdtp->mclk_req, 0);
            set_irq(&wdtp->aclk_req, 0);
            set_irq(&wdtp->smclk_req, 0);
            wdtp = NULL;
        }
    }

    if (wdt->ctl & R_CTL_SSEL_MASK) {
        if (wdtp) {
            set_irq(&wdtp->aclk_req, 1);
            set_irq(&wdtp->smclk_req, 0);
        }
        period = clock_get(wdt->aclk);
    } else {
        if (wdtp) {
            set_irq(&wdtp->aclk_req, 0);
            set_irq(&wdtp->smclk_req, 1);
        }
        period = clock_get(wdt->smclk);
    }

    if (wdtp) {
        if (!period) {
            set_irq(&wdtp->mclk_req, 1);
            period = clock_get(wdtp->mclk);
        } else {
            set_irq(&wdtp->mclk_req, 0);
        }
    }

    return period;
}

static const uint16_t wdt_is_per[] = {
    32768,
    8192,
    512,
    64,
};

static uint16_t wdt_per(MSP430WDTState *wdt)
{
    return wdt_is_per[FIELD_EX8(wdt->ctl, CTL, IS)];
}

static void wdt_recalculate(MSP430WDTState *wdt)
{
    int64_t event_time_ns, clk_per;

    clk_per = wdt_clock_period(wdt);
    if (wdt->ctl & R_CTL_HOLD_MASK) {
        wdt->event_cycles = 0;
    } else {
        uint16_t per = wdt_is_per[FIELD_EX8(wdt->ctl, CTL, IS)];

        wdt->event_cycles = per - (wdt->cnt & (per - 1));
    }

    event_time_ns = wdt->event_cycles * (clk_per / CLK_TO_NS);
    if (event_time_ns) {
        timer_mod(&wdt->timer, wdt->last_updated + event_time_ns);
    } else {
        timer_del(&wdt->timer);
    }
}

static void wdt_expire(MSP430WDTState *wdt)
{
    qemu_irq_raise(wdt->irq);
    if (!(wdt->ctl & R_CTL_TMSEL_MASK)) {
        /*
         * Emulate a PUC instead of doing qemu_system_reset_request() which is
         * equivalent to a POR.
         */
        if (get_watchdog_action() == WATCHDOG_ACTION_RESET) {
            qapi_event_send_watchdog(WATCHDOG_ACTION_RESET);
            qemu_irq_raise(wdt->puc);
        } else {
            watchdog_perform_action();
        }
    }
}

static void wdt_update(MSP430WDTState *wdt)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t actual_cycles, clk_per;

    clk_per = wdt_clock_period(wdt);
    if (clk_per) {
        actual_cycles = (now - wdt->last_updated) / (clk_per / CLK_TO_NS);
    } else {
        actual_cycles = 0;
    }

    wdt->cnt += actual_cycles;
    wdt->last_updated = now;

    if (actual_cycles >= wdt->event_cycles) {
        wdt_expire(wdt);
        wdt_recalculate(wdt);
    }
}

static void wdt_event(void *opaque)
{
    wdt_update(opaque);
}

static void wdt_mclk_callback(void *opaque, ClockEvent event)
{
    MSP430WDTPState *wdtp = opaque;

    if (wdtp->mclk_req.level) {
        wdt_recalculate(&wdtp->parent_obj);
    }
}

static void wdt_aclk_callback(void *opaque, ClockEvent event)
{
    MSP430WDTState *wdt = opaque;

    if (wdt->ctl & R_CTL_SSEL_MASK) {
        wdt_recalculate(wdt);
    }
}

static void wdt_smclk_callback(void *opaque, ClockEvent event)
{
    MSP430WDTState *wdt = opaque;

    if (!(wdt->ctl & R_CTL_SSEL_MASK)) {
        wdt_recalculate(wdt);
    }
}

static uint64_t wdt_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430WDTState *wdt = opaque;

    return 0x6900 | wdt->ctl;
}

static void wdt_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430WDTState *wdt = opaque;
    bool oldes = wdt->ctl & R_CTL_NMIES_MASK;
    bool newes = val & R_CTL_NMIES_MASK;

    wdt_update(wdt);

    if (FIELD_EX16(val, CTL, PW) != 0x5a) {
        qemu_irq_raise(wdt->puc);
    }

    if (val & R_CTL_CNTCL_MASK) {
        wdt->cnt = 0;
    }

    if ((val & R_CTL_NMI_MASK) &&
        ((wdt->rst_nmi_level && !oldes && newes) ||
         (!wdt->rst_nmi_level && oldes && !newes))) {
            qemu_irq_raise(wdt->nmi);
    }

    wdt->ctl = val & ~R_CTL_CNTCL_MASK;
    if (wdt_per(wdt) & wdt->cnt) {
        wdt_expire(wdt);
    }
    wdt_recalculate(wdt);
}

static const MemoryRegionOps wdt_ops = {
    .write = wdt_write,
    .read  = wdt_read,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
};

static void wdt_reset(MSP430WDTState *wdt)
{
    wdt->last_updated = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    wdt->event_cycles = 0;
    wdt->cnt = 0;
    wdt->ctl = 0;
    wdt_recalculate(wdt);
    if (!wdt->rst_nmi_level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void wdt_reset_hold(Object *obj, ResetType type)
{
    MSP430WDTState *wdt = MSP430_WDT(obj);

    wdt_reset(wdt);
}

static void wdt_puc(void *opaque, int irq, int level)
{
    MSP430WDTState *wdt = opaque;

    if (level) {
        wdt_reset(wdt);
    }
}

static const ClockPortInitArray wdt_clocks = {
    QDEV_CLOCK_IN(MSP430WDTState, aclk, wdt_aclk_callback, ClockUpdate),
    QDEV_CLOCK_IN(MSP430WDTState, smclk, wdt_smclk_callback, ClockUpdate),
    QDEV_CLOCK_END,
};

static void wdt_init(Object *obj)
{
    DeviceState *d = DEVICE(obj);
    MSP430WDTState *wdt = MSP430_WDT(obj);

    memory_region_init_io(&wdt->memory, obj, &wdt_ops, wdt, "msp430-wdt", 1);
    sysbus_init_mmio(SYS_BUS_DEVICE(d), &wdt->memory);

    timer_init_ns(&wdt->timer, QEMU_CLOCK_VIRTUAL, wdt_event, wdt);
    qdev_init_clocks(d, wdt_clocks);
    qdev_init_gpio_in_named(d, wdt_puc, "puc_in", 1);
    qdev_init_gpio_in_named(d, wdt_rst_nmi, "rst_nmi", 1);
    qdev_init_gpio_in_named(d, wdt_ack, "ack", 1);
    qdev_init_gpio_out_named(d, &wdt->nmi, "nmi", 1);
    qdev_init_gpio_out_named(d, &wdt->puc, "puc_out", 1);
    qdev_init_gpio_out_named(d, &wdt->irq, "irq", 1);

    wdt->rst_nmi_level = 1;
}

static const VMStateDescription vmstate_wdt = {
    .name = "msp430-wdt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(ctl, MSP430WDTState),
        VMSTATE_UINT16(cnt, MSP430WDTState),
        VMSTATE_UINT16(event_cycles, MSP430WDTState),
        VMSTATE_INT64(last_updated, MSP430WDTState),
        VMSTATE_BOOL(rst_nmi_level, MSP430WDTState),
        VMSTATE_CLOCK(aclk, MSP430WDTState),
        VMSTATE_CLOCK(smclk, MSP430WDTState),
        VMSTATE_TIMER(timer, MSP430WDTState),
        VMSTATE_END_OF_LIST()
    }
};

static void wdt_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "MSP430 Watchdog Timer";
    dc->vmsd = &vmstate_wdt;
    rc->phases.hold = wdt_reset_hold;
}

static const ClockPortInitArray wdtp_clocks = {
    QDEV_CLOCK_IN(MSP430WDTPState, mclk, wdt_mclk_callback, ClockUpdate),
    QDEV_CLOCK_END,
};

static void wdtp_init(Object *obj)
{
    DeviceState *d = DEVICE(obj);
    MSP430WDTPState *wdtp = MSP430_WDTP(obj);

    qdev_init_clocks(d, wdtp_clocks);
    qdev_init_gpio_out_named(d, &wdtp->mclk_req.irq, "mclk_req", 1);
    qdev_init_gpio_out_named(d, &wdtp->aclk_req.irq, "aclk_req", 1);
    qdev_init_gpio_out_named(d, &wdtp->smclk_req.irq, "smclk_req", 1);
}

static const VMStateDescription vmstate_wdtp = {
    .name = "msp430-wdt+",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, MSP430WDTPState, 1, vmstate_wdt,
                       MSP430WDTState),
        VMSTATE_BOOL(mclk_req.level, MSP430WDTPState),
        VMSTATE_BOOL(aclk_req.level, MSP430WDTPState),
        VMSTATE_BOOL(smclk_req.level, MSP430WDTPState),
        VMSTATE_CLOCK(mclk, MSP430WDTPState),
        VMSTATE_END_OF_LIST()
    }
};

static void wdtp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = "MSP430 Watchdog Timer+";
    dc->vmsd = &vmstate_wdtp;
}

static const TypeInfo wdt_types[] = {
    {
        .name = TYPE_MSP430_WDT,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(MSP430WDTState),
        .instance_init = wdt_init,
        .class_init = wdt_class_init,
    },
    {
        .name = TYPE_MSP430_WDTP,
        .parent = TYPE_MSP430_WDT,
        .instance_size = sizeof(MSP430WDTPState),
        .instance_init = wdtp_init,
        .class_init = wdtp_class_init,
    },
};

DEFINE_TYPES(wdt_types)
