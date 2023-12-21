/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/msp430/bcm.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

REG8(DCOCTL, 0)
    FIELD(DCOCTL, DCO, 5, 3)
    FIELD(DCOCTL, MOD, 0, 5)
REG8(BCSCTL1, 1)
    FIELD(BCSCTL1, XT2OFF, 7, 1)
    FIELD(BCSCTL1, XTS, 6, 1)
    FIELD(BCSCTL1, DIVA, 4, 2)
    FIELD(BCSCTL1, XT5V, 3, 1)
    FIELD(BCSCTL1, RSEL_BCM, 0, 3)
    FIELD(BCSCTL1, RSEL, 0, 4)
REG8(BCSCTL2, 2)
    FIELD(BCSCTL2, SELM, 6, 2)
    FIELD(BCSCTL2, DIVM, 4, 2)
    FIELD(BCSCTL2, SELS, 3, 1)
    FIELD(BCSCTL2, DIVS, 1, 2)
    FIELD(BCSCTL2, DCOR, 0, 1)
/* BCSCTL3 */
    FIELD(BCSCTL3, XT2S, 6, 2)
    FIELD(BCSCTL3, LFXT1S, 4, 2)
    FIELD(BCSCTL3, XCAP, 2, 2)
    FIELD(BCSCTL3, XT2OF, 1, 1)
    FIELD(BCSCTL3, LFXT1OF, 0, 1)

#define R_BCSCTL3_OF_MASK (R_BCSCTL3_LFXT1OF_MASK | R_BCSCTL3_XT2OF_MASK)

typedef struct {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    bool plus;
} MSP430BCMClass;

DECLARE_CLASS_CHECKERS(MSP430BCMClass, MSP430_BCM, TYPE_MSP430_BCM)

static const struct {
    uint64_t min, max;
} xt_range[] = {
    {  400000,  1000000 },
    { 1000000,  3000000 },
    { 3000000, 16000000 },
    {  400000, 16000000 },
};

static uint64_t bcm_dcoclk(MSP430BCMState *bcm, uint8_t rsel, uint8_t dco,
                           uint8_t mod)
{
    uint64_t dcoclk = dcoclk = bcm->dco_freq[rsel][dco];

    if (dco < 7) {
        dcoclk = (32 - mod) * dcoclk + mod * bcm->dco_freq[rsel][dco + 1];
        dcoclk /= 32;
    }

    return dcoclk;
}

static void bcm_set_clocks(MSP430BCMState *bcm)
{
    const MSP430BCMClass *bc = MSP430_BCM_GET_CLASS(bcm);
    uint64_t xt1 = clock_get_hz(bcm->xt1);
    uint64_t xt2 = clock_get_hz(bcm->xt2);
    uint64_t lfxt1 = 0, dcoclk, mclk, smclk;
    uint8_t mod = FIELD_EX8(bcm->dcoctl, DCOCTL, MOD);
    uint8_t dco = FIELD_EX8(bcm->dcoctl, DCOCTL, DCO);
    uint8_t xts = FIELD_EX8(bcm->bcsctl1, BCSCTL1, XTS);
    uint8_t old_bcsctl3 = bcm->bcsctl3;
    uint8_t xt2s = FIELD_EX8(bcm->bcsctl3, BCSCTL3, XT2S);
    uint8_t lfxt1s = FIELD_EX8(bcm->bcsctl3, BCSCTL3, LFXT1S);
    uint8_t rsel;

    if (bcm->has_xts && xts) {
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
            lfxt1 = bcm->vlo_freq;
            break;
        case 3:
            if (xt1 >= 10000 && xt1 <= 50000) {
                lfxt1 = xt1;
            }
            break;
        }
    }

    if (xt2 < xt_range[xt2s].min || xt2 > xt_range[xt2s].max) {
        xt2 = 0;
    }

    if (xt2 || !clock_has_source(bcm->xt2)) {
        bcm->bcsctl3 &= ~R_BCSCTL3_XT2OF_MASK;
    } else {
        bcm->bcsctl3 |= R_BCSCTL3_XT2OF_MASK;
    }

    if (bcm->bcsctl3 != old_bcsctl3) {
        if (bcm->bcsctl3 & R_BCSCTL3_OF_MASK) {
            qemu_irq_raise(bcm->irq);
        } else {
            qemu_irq_lower(bcm->irq);
        }
    }

    if (bc->plus) {
        rsel = FIELD_EX8(bcm->bcsctl1, BCSCTL1, RSEL);
    } else {
        rsel = FIELD_EX8(bcm->bcsctl1, BCSCTL1, RSEL_BCM);
    }

    dcoclk = bcm->dco_freq[rsel][dco];
    if (dco < 7) {
        dcoclk = (32 - mod) * dcoclk + mod * bcm->dco_freq[rsel][dco + 1];
        dcoclk /= 32;
    }

    dcoclk = bcm_dcoclk(bcm, rsel, dco, mod);
    clock_set_hz(bcm->aclk, lfxt1 >> FIELD_EX8(bcm->bcsctl1, BCSCTL1, DIVA));

    mclk = dcoclk;
    switch (FIELD_EX8(bcm->bcsctl2, BCSCTL2, SELM)) {
    case 2:
        if (xt2 && clock_has_source(bcm->xt2)) {
            mclk = xt2;
        }
        /* fallthrough */
    case 3:
        if (lfxt1) {
            mclk = lfxt1;
        }
        break;
    }

    mclk >>= FIELD_EX8(bcm->bcsctl2, BCSCTL2, DIVM);
    if (bcm->cpuoff) {
        mclk = 0;
    }
    clock_set_hz(bcm->mclk, mclk);
    
    if (bcm->bcsctl2 & R_BCSCTL2_SELS_MASK) {
        smclk = xt2;
    } else {
        smclk = dcoclk;
    }

    smclk >>= FIELD_EX8(bcm->bcsctl2, BCSCTL2, DIVS);
    if (bcm->scg1) {
        smclk = 0;
    }
    clock_set_hz(bcm->smclk, smclk);

    clock_propagate(bcm->aclk);
    clock_propagate(bcm->mclk);
    clock_propagate(bcm->smclk);
}

static void bcm_set_cpuoff(void *opaque, int irq, int level)
{
    MSP430BCMState *bcm = opaque;
    
    bcm->cpuoff = level;
    bcm_set_clocks(bcm);
}

static void bcm_set_scg1(void *opaque, int irq, int level)
{
    MSP430BCMState *bcm = opaque;
    
    bcm->scg1 = level;
    bcm_set_clocks(bcm);
}

static void bcm_clk_callback(void *opaque, ClockEvent event)
{
    MSP430BCMState *bcm = opaque;

    bcm_set_clocks(bcm);
}

static uint64_t bcm_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430BCMState *bcm = opaque;

    switch (addr) {
    case A_DCOCTL:
        return bcm->dcoctl;
    case A_BCSCTL1:
        return bcm->bcsctl1;
    case A_BCSCTL2:
        return bcm->bcsctl2;
    }

    g_assert_not_reached();
    return UINT64_MAX;
}

static void bcm_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430BCMState *bcm = opaque;

    switch (addr) {
    case A_DCOCTL:
        bcm->dcoctl = val;
        break;
    case A_BCSCTL1:
        if (!bcm->has_xts && val & R_BCSCTL1_XTS_MASK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "msp430_bcm: XTS=1 not supported on this hardware\n");
        }

        bcm->bcsctl1 = val;
        break;
    case A_BCSCTL2:
        if (val & R_BCSCTL2_DCOR_MASK) {
            qemu_log_mask(LOG_UNIMP, "msp430_bcm: DCOR=1 not implemented\n");
        }

        bcm->bcsctl2 = val;
        break;
    default:
        g_assert_not_reached();
    }

    bcm_set_clocks(bcm);
}

static const MemoryRegionOps bcm_ops = {
    .write = bcm_write,
    .read  = bcm_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static uint64_t bcsctl3_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430BCMState *bcm = opaque;

    return bcm->bcsctl3;
}

static void bcsctl3_write(void *opaque, hwaddr addr, uint64_t val,
                          unsigned size)
{
    MSP430BCMState *bcm = opaque;

    bcm->bcsctl3 &= R_BCSCTL3_OF_MASK;
    bcm->bcsctl3 |= val & ~R_BCSCTL3_OF_MASK;
    bcm_set_clocks(bcm);
}

static const MemoryRegionOps bcsctl3_ops = {
    .write = bcsctl3_write,
    .read  = bcsctl3_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void bcm_reset(MSP430BCMState *bcm, bool por)
{
    const MSP430BCMClass *bc = MSP430_BCM_GET_CLASS(bcm);

    if (bcm->bcsctl3 & R_BCSCTL3_XT2OF_MASK &&
        !(bcm->bcsctl3 & R_BCSCTL3_LFXT1OF_MASK)) {
        qemu_irq_lower(bcm->irq);
    }

    bcm->dcoctl = 0x60;

    if (por) {
        bcm->bcsctl1 = R_BCSCTL1_XT2OFF_MASK;
    } else {
        bcm->bcsctl1 &= ~R_BCSCTL1_RSEL_MASK;
    }
    bcm->bcsctl1 |= bc->plus ? 0x07 : 0x04;

    if (por || bc->plus) {
        bcm->bcsctl2 = 0x00;
    } else {
        bcm->bcsctl2 &= ~(R_BCSCTL2_SELS_MASK | R_BCSCTL2_DIVS_MASK |
                          R_BCSCTL2_DCOR_MASK);
    }

    bcm->bcsctl3 &= por ? 0 : R_BCSCTL3_LFXT1OF_MASK;
    bcm->bcsctl3 |= 0x04;

    bcm_set_clocks(bcm);
}

static void bcm_reset_hold(Object *obj, ResetType type)
{
    MSP430BCMState *bcm = MSP430_BCM(obj);

    bcm_reset(bcm, true);
}

static void bcm_puc(void *opaque, int irq, int level)
{
    if (level) {
        bcm_reset(opaque, false);
    }
}

static const ClockPortInitArray bcm_clocks = {
    QDEV_CLOCK_IN(MSP430BCMState, xt1, bcm_clk_callback, ClockUpdate),
    QDEV_CLOCK_IN(MSP430BCMState, xt2, bcm_clk_callback, ClockUpdate),
    QDEV_CLOCK_OUT(MSP430BCMState, aclk),
    QDEV_CLOCK_OUT(MSP430BCMState, mclk),
    QDEV_CLOCK_OUT(MSP430BCMState, smclk),
    QDEV_CLOCK_END,
};

struct freq_range {
    uint64_t min, max;
};

/* Based on SLAS368G at 3V, adjusted for DCO=0 assuming S_DCO=1.12 */
static const struct freq_range bcm_dco_range[] = {
    {   57000,  110000 },
    {  100000,  160000 },
    {  160000,  240000 },
    {  260000,  400000 },
    {  430000,  640000 },
    {  710000, 1100000 },
    { 1200000, 1600000 },
    { 1900000, 2600000 },
    { 2000000, 2400000 },
};

/* Based on SLAS735J, adjusted for DCO=0 assuming S_DCO=1.08 */
static const struct freq_range bcmp_dco_range[] = {
    {   56000,   130000 },
    {   95000,   140000 },
    {  130000,   200000 },
    {  190000,   290000 },
    {  260000,   390000 },
    {  370000,   550000 },
    {  430000,   840000 },
    {  640000,  1200000 },
    { 1000000,  1500000 },
    { 1500000,  2200000 },
    { 2200000,  3200000 },
    { 2700000,  4000000 },
    { 3400000,  5800000 },
    { 4800000,  7600000 },
    { 6800000, 11000000 },
    { 9300000, 15000000 },
};

static void bcm_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MSP430BCMState *bcm = MSP430_BCM(obj);
    const MSP430BCMClass *bc = MSP430_BCM_GET_CLASS(bcm);
    const struct freq_range *dco_range;
    uint64_t dco_min, dco_max;
    uint8_t rsel, dco, rsel_max;

    memory_region_init_io(&bcm->iomem, OBJECT(bcm), &bcm_ops, bcm,
                          "msp430-bcm", 3);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &bcm->iomem);
    if (bc->plus) {
        memory_region_init_io(&bcm->bcsctl3mem, OBJECT(bcm), &bcsctl3_ops,
                              bcm, "msp430-bcsctl3", 1);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &bcm->bcsctl3mem);
    } else {
        bcm->has_xts = true;
    }

    qdev_init_clocks(DEVICE(obj), bcm_clocks);
    sysbus_init_irq(d, &bcm->irq);
    qdev_init_gpio_in_named(DEVICE(d), bcm_puc, "puc", 1);
    qdev_init_gpio_in_named(DEVICE(d), bcm_set_cpuoff, "cpuoff", 1);
    qdev_init_gpio_in_named(DEVICE(d), bcm_set_scg1, "scg1", 1);

    bcm->vlo_freq = g_random_int_range(4000, 20000);

    /* Initialize the DCO frequencies. As an R/C oscillator, the DCO has a wide
     * variation in frequencies between parts. Simulate that by picking random
     * frequencies within the datasheet tolerances. We do this at init time so
     * that the calibration constants in the info flash will be accurate. And
     * these wouldn't really vary across power cycles.
     */
    if (bc->plus) {
        rsel_max = 16;
        dco_range = bcmp_dco_range;
        /* Multiply each DCO step by a random number in the range 1.07 to 1.09 */
        dco_min = 1148903751; /* 1.07 */
        dco_max = 1170378588; /* 1.09 */
    } else {
        rsel_max = 8;
        dco_range = bcm_dco_range;
        dco_min = 1148903751; /* 1.07 */
        dco_max = 1245540515; /* 1.16 */
    }

    for (rsel = 0; rsel < rsel_max; rsel++) {
        bcm->dco_freq[rsel][0] = g_random_int_range(dco_range[rsel].min,
                                                    dco_range[rsel].max);
        for (dco = 1; dco < 8; dco++) {
            /* Multiply each DCO step by a random ratio */
            bcm->dco_freq[rsel][dco] = bcm->dco_freq[rsel][dco - 1] *
                g_random_int_range(dco_min, dco_max) / 1073741824;
        }
    }
}

static int bcm_post_load(void *opaque, int version_id)
{
    MSP430BCMState *bcm = opaque;

    bcm_set_clocks(bcm);
    return 0;
}

static const VMStateDescription vmstate_bcm = {
    .name = "msp430-bcm",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = bcm_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(dcoctl, MSP430BCMState),
        VMSTATE_UINT8(bcsctl1, MSP430BCMState),
        VMSTATE_UINT8(bcsctl2, MSP430BCMState),
        VMSTATE_UINT8(bcsctl3, MSP430BCMState),
        VMSTATE_CLOCK(xt1, MSP430BCMState),
        VMSTATE_CLOCK(xt2, MSP430BCMState),
        VMSTATE_UINT64(vlo_freq, MSP430BCMState),
        VMSTATE_UINT64_2DARRAY(dco_freq, MSP430BCMState, 16, 8),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "MSP430 basic clock module";
    dc->vmsd = &vmstate_bcm;
    rc->phases.hold = bcm_reset_hold;
}

static Property bcmp_properties[] = {
    DEFINE_PROP_BOOL("has_xts", MSP430BCMState, has_xts, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void bcmp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MSP430BCMClass *bc = MSP430_BCM_CLASS(oc);

    dc->desc = "MSP430 basic clock module+";
    device_class_set_props(dc, bcmp_properties);
    bc->plus = true;
}

static const TypeInfo bcm_types[] = {
        {
            .parent = TYPE_SYS_BUS_DEVICE,
            .name = TYPE_MSP430_BCM,
            .instance_size = sizeof(MSP430BCMState),
            .instance_init = bcm_init,
            .class_init = bcm_class_init,
        },
        {
            .parent = TYPE_MSP430_BCM,
            .name = TYPE_MSP430_BCMP,
            .class_init = bcmp_class_init,
        },
};

DEFINE_TYPES(bcm_types)

void bcm_find_closest(MSP430BCMState *bcm, uint64_t freq, uint8_t *best_dcoctl,
                      uint8_t *best_bcsctl1)
{
    const MSP430BCMClass *bc = MSP430_BCM_GET_CLASS(bcm);
    uint64_t best_error = UINT64_MAX;
    uint8_t rsel, dco, mod;

    for (rsel = 0; rsel < (bc->plus ? 16 : 8); rsel++) {
        for (dco = 0; dco < 8; dco++) {
            for (mod = 0; mod < 32; mod++) {
                uint64_t dcoclk = bcm_dcoclk(bcm, rsel, dco, mod);
                uint64_t error = freq < dcoclk ? dcoclk - freq : freq - dcoclk;

                if (error < best_error) {
                    *best_dcoctl = (dco << R_DCOCTL_DCO_SHIFT) | mod;
                    *best_bcsctl1 = rsel;
                    best_error = error;
                }
            }
        }
    }

    rsel = *best_bcsctl1;
    dco = *best_dcoctl >> R_DCOCTL_DCO_SHIFT;
    mod = *best_dcoctl & R_DCOCTL_MOD_MASK;
}
