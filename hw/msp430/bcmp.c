/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/msp430/bcmp.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

REG8(DCOCTL, 6)
    FIELD(DCOCTL, DCO, 5, 3)
    FIELD(DCOCTL, MOD, 0, 5)
REG8(BCSCTL1, 7)
    FIELD(BCSCTL1, XT2OFF, 7, 1)
    FIELD(BCSCTL1, XTS, 6, 1)
    FIELD(BCSCTL1, DIVA, 4, 2)
    FIELD(BCSCTL1, RSEL, 0, 4)
REG8(BCSCTL2, 8)
    FIELD(BCSCTL2, SELM, 6, 2)
    FIELD(BCSCTL2, DIVM, 4, 2)
    FIELD(BCSCTL2, SELS, 3, 1)
    FIELD(BCSCTL2, DIVS, 1, 2)
    FIELD(BCSCTL2, DCOR, 0, 1)
REG8(BCSCTL3, 3)
    FIELD(BCSCTL3, XT2S, 6, 2)
    FIELD(BCSCTL3, LFXT1S, 4, 2)
    FIELD(BCSCTL3, XCAP, 2, 2)
    FIELD(BCSCTL3, XT2OF, 1, 1)
    FIELD(BCSCTL3, LFXT1OF, 0, 1)
REG8(SVSCTL, 5)

#define R_BCSCTL3_OF_MASK (R_BCSCTL3_LFXT1OF_MASK | R_BCSCTL3_XT2OF_MASK)

static const struct {
    uint64_t min, max;
} xt_range[] = {
    {  400000,  1000000 },
    { 1000000,  3000000 },
    { 3000000, 16000000 },
    {  400000, 16000000 },
};

/* Based on SLAS753J, adjusted for DCO=0 assuming S_DCO=1.08 */
static const uint64_t dco_freq[] = {
    95260,
    119075,
    166705,
    238150,
    325471,
    460423,
    635066,
    912907,
    1270132,
    1825814,
    2699030,
    3373787,
    4604227,
    6191891,
    8930613,
    12105942,
};

static void bcmp_set_clocks(MSP430BCMPState *bcmp)
{
    uint64_t xt1 = clock_get_hz(bcmp->xt1);
    uint64_t xt2 = clock_get_hz(bcmp->xt2);
    uint64_t lfxt1 = 0, dcoclk, mclk, smclk;
    uint8_t mod = FIELD_EX8(bcmp->dcoctl, DCOCTL, MOD);
    uint8_t dco = FIELD_EX8(bcmp->dcoctl, DCOCTL, DCO);
    uint8_t xts = FIELD_EX8(bcmp->bcsctl1, BCSCTL1, XTS);
    uint8_t old_bcsctl3 = bcmp->bcsctl3;
    uint8_t xt2s = FIELD_EX8(bcmp->bcsctl3, BCSCTL3, XT2S);
    uint8_t lfxt1s = FIELD_EX8(bcmp->bcsctl3, BCSCTL3, LFXT1S);
    int i;

    if (bcmp->has_xts && xts) {
        if (xt1 >= xt_range[lfxt1s].min && xt1 <= xt_range[lfxt1s].max) {
            lfxt1 = xt1;
        }
    } else {
        switch (lfxt1s) {
        case 0:
            if (xt1 == 32768) {
                lfxt1 = xt1;
            }
            break;
        case 2:
            lfxt1 = 12000;
            break;
        case 3:
            if (xt1 >= 10000 && xt1 <= 50000) {
                lfxt1 = xt1;
            }
            break;
        }
    }

    if (lfxt1) {
        bcmp->bcsctl3 &= ~R_BCSCTL3_LFXT1OF_MASK;
    } else {
        bcmp->bcsctl3 |= R_BCSCTL3_LFXT1OF_MASK;
    }

    if (xt2 < xt_range[xt2s].min || xt2 > xt_range[xt2s].max) {
        xt2 = 0;
    }

    if (xt2 || !clock_has_source(bcmp->xt2)) {
        bcmp->bcsctl3 &= ~R_BCSCTL3_XT2OF_MASK;
    } else {
        bcmp->bcsctl3 |= R_BCSCTL3_XT2OF_MASK;
    }

    if (bcmp->bcsctl3 != old_bcsctl3) {
        if (bcmp->bcsctl3 & R_BCSCTL3_OF_MASK) {
            qemu_irq_raise(bcmp->irq);
        } else {
            qemu_irq_lower(bcmp->irq);
        }
    }

    dcoclk = dco_freq[FIELD_EX8(bcmp->bcsctl1, BCSCTL1, RSEL)];
    for (i = 0; i < dco; i++) {
        dcoclk += dcoclk * 2 / 25;
    }
    if (dco < 7) {
        dcoclk = (32 - mod) * dcoclk + mod * (dcoclk + dcoclk * 2 / 25);
        dcoclk /= 32;
    }

    clock_set_hz(bcmp->aclk, lfxt1 >> FIELD_EX8(bcmp->bcsctl1, BCSCTL1, DIVA));

    mclk = dcoclk;
    switch (FIELD_EX8(bcmp->bcsctl2, BCSCTL2, SELM)) {
    case 2:
        if (clock_has_source(bcmp->xt2)) {
            if (xt2) {
                mclk = xt2;
            }
        }
        /* fallthrough */
    case 3:
        if (lfxt1) {
            mclk = lfxt1;
        }
        break;
    }

    mclk >>= FIELD_EX8(bcmp->bcsctl2, BCSCTL2, DIVM);
    if (bcmp->cpuoff) {
        mclk = 0;
    }
    clock_set_hz(bcmp->mclk, mclk);
    
    if (bcmp->bcsctl2 & R_BCSCTL2_SELS_MASK) {
        smclk = xt2;
    } else {
        smclk = dcoclk;
    }

    smclk >>= FIELD_EX8(bcmp->bcsctl2, BCSCTL2, DIVS);
    if (bcmp->scg1) {
        smclk = 0;
    }
    clock_set_hz(bcmp->smclk, smclk);

    clock_propagate(bcmp->aclk);
    clock_propagate(bcmp->mclk);
    clock_propagate(bcmp->smclk);
}

static void bcmp_set_cpuoff(void *opaque, int irq, int level)
{
    MSP430BCMPState *bcmp = opaque;
    
    bcmp->cpuoff = level;
    bcmp_set_clocks(bcmp);
}

static void bcmp_set_scg1(void *opaque, int irq, int level)
{
    MSP430BCMPState *bcmp = opaque;
    
    bcmp->scg1 = level;
    bcmp_set_clocks(bcmp);
}

static void bcmp_clk_callback(void *opaque, ClockEvent event)
{
    MSP430BCMPState *bcmp = opaque;

    bcmp_set_clocks(bcmp);
}

static void bcmp_reset_hold(Object *obj)
{
    MSP430BCMPState *bcmp = MSP430_BCMP(obj);

    if (bcmp->bcsctl3 & R_BCSCTL3_XT2OF_MASK &&
        !(bcmp->bcsctl3 & R_BCSCTL3_LFXT1OF_MASK)) {
        qemu_irq_lower(bcmp->irq);
    }

    bcmp->dcoctl = 0x60;
    bcmp->bcsctl1 &= ~R_BCSCTL1_RSEL_MASK;
    bcmp->bcsctl1 |= 0x07;
    bcmp->bcsctl2 = 0x00;
    bcmp->bcsctl3 &= R_BCSCTL3_LFXT1OF_MASK;
    bcmp->bcsctl3 |= 0x04;

    bcmp_set_clocks(bcmp);
}

static uint64_t bcmp_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430BCMPState *bcmp = opaque;

    switch (addr) {
    case A_DCOCTL:
        return bcmp->dcoctl;
    case A_BCSCTL1:
        return bcmp->bcsctl1;
    case A_BCSCTL2:
        return bcmp->bcsctl2;
    case A_BCSCTL3:
        return bcmp->bcsctl3;
    case A_SVSCTL:
        qemu_log_mask(LOG_UNIMP, "msp430_bcmp: SVSCTL not implemented\n");
        return UINT64_MAX;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "msp430_bcmp: No register at 0x%" HWADDR_PRIX "\n", addr);
    return UINT64_MAX;
}

static void bcmp_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430BCMPState *bcmp = opaque;

    switch (addr) {
    case A_DCOCTL:
        bcmp->dcoctl = val;
        break;
    case A_BCSCTL1:
        if (!bcmp->has_xts && val & R_BCSCTL1_XTS_MASK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "msp430_bcmp: XTS=1 not supported on this hardware\n");
        }

        bcmp->bcsctl1 = val;
        break;
    case A_BCSCTL2:
        if (val & R_BCSCTL2_DCOR_MASK) {
            qemu_log_mask(LOG_UNIMP, "msp430_bcmp: DCOR=1 not implemented\n");
        }

        bcmp->bcsctl2 = val;
        break;
    case A_BCSCTL3:
        bcmp->bcsctl3 &= R_BCSCTL3_OF_MASK;
        bcmp->bcsctl3 |= val & ~R_BCSCTL3_OF_MASK;
        break;
    case A_SVSCTL:
        qemu_log_mask(LOG_UNIMP, "msp430_bcmp: SVSCTL not implemented\n");
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "msp430_bcmp: No register at 0x%" HWADDR_PRIX "\n",
                      addr);
        return;
    }

    bcmp_set_clocks(bcmp);
}

static const MemoryRegionOps bcmp_ops = {
    .write = bcmp_write,
    .read  = bcmp_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const ClockPortInitArray bcmp_clocks = {
    QDEV_CLOCK_IN(MSP430BCMPState, xt1, bcmp_clk_callback, ClockUpdate),
    QDEV_CLOCK_IN(MSP430BCMPState, xt2, bcmp_clk_callback, ClockUpdate),
    QDEV_CLOCK_OUT(MSP430BCMPState, aclk),
    QDEV_CLOCK_OUT(MSP430BCMPState, mclk),
    QDEV_CLOCK_OUT(MSP430BCMPState, smclk),
    QDEV_CLOCK_END,
};

static void bcmp_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MSP430BCMPState *bcmp = MSP430_BCMP(obj);

    memory_region_init_io(&bcmp->memory, OBJECT(bcmp), &bcmp_ops, bcmp,
                          "msp430-bcm+", 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &bcmp->memory);

    qdev_init_clocks(DEVICE(obj), bcmp_clocks);
    sysbus_init_irq(d, &bcmp->irq);
    qdev_init_gpio_in_named(DEVICE(d), bcmp_set_cpuoff, "cpuoff", 1);
    qdev_init_gpio_in_named(DEVICE(d), bcmp_set_scg1, "scg1", 1);

    bcmp->dcoctl = 0x60;
    bcmp->bcsctl1 = 0x87;
    bcmp->bcsctl2 = 0x00;
    bcmp->bcsctl3 = 0x04;
}

static int bcmp_post_load(void *opaque, int version_id)
{
    MSP430BCMPState *bcmp = opaque;

    bcmp_set_clocks(bcmp);
    return 0;
}

static const VMStateDescription vmstate_bcmp = {
    .name = "msp430-bcm+",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = bcmp_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(dcoctl, MSP430BCMPState),
        VMSTATE_UINT8(bcsctl1, MSP430BCMPState),
        VMSTATE_UINT8(bcsctl2, MSP430BCMPState),
        VMSTATE_UINT8(bcsctl3, MSP430BCMPState),
        VMSTATE_CLOCK(xt1, MSP430BCMPState),
        VMSTATE_CLOCK(xt2, MSP430BCMPState),
        VMSTATE_END_OF_LIST()
    }
};

static Property bcmp_properties[] = {
    DEFINE_PROP_BOOL("has_xts", MSP430BCMPState, has_xts, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void bcmp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "MSP430 basic clock module+";
    dc->vmsd = &vmstate_bcmp;
    rc->phases.hold = bcmp_reset_hold;
    device_class_set_props(dc, bcmp_properties);
}

static const TypeInfo bcmp_info = {
    .parent = TYPE_SYS_BUS_DEVICE,
    .name = TYPE_MSP430_BCMP,
    .instance_size = sizeof(MSP430BCMPState),
    .instance_init = bcmp_init,
    .class_init = bcmp_class_init,
};

static void bcmp_register_types(void)
{
    type_register_static(&bcmp_info);
}
type_init(bcmp_register_types);
