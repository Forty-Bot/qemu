/*
 * SPDX-License-Identifie: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/msp430/fllp.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

REG8(SCFQCTL, 2)
    FIELD(SCFQCTL, SCFQ_M, 7, 1)
    FIELD(SCFQCTL, N, 0, 7)
REG8(SCFI0, 0)
    FIELD(SCFI0, FLLD, 6, 2)
    FIELD(SCFI0, FN, 2, 4)
    FIELD(SCFI0, MOD, 0, 2)
REG8(SCFI1, 1)
    FIELD(SCFI1, DCO, 3, 5)
    FIELD(SCFI1, MOD, 0, 3)
REG8(FLL_CTL0, 3)
    FIELD(FLL_CTL0, DCOPLUS, 7, 1)
    FIELD(FLL_CTL0, XTS_FLL, 6, 1)
    FIELD(FLL_CTL0, XCAPPF, 4, 2)
    FIELD(FLL_CTL0, XT2OF, 3, 1)
    FIELD(FLL_CTL0, XT1OF, 2, 1)
    FIELD(FLL_CTL0, LFOF, 1, 1)
    FIELD(FLL_CTL0, DCOF, 0, 1)
REG8(FLL_CTL1, 4)
    FIELD(FLL_CTL1, LFXT1DIG, 7, 1)
    FIELD(FLL_CTL1, SMCLKOFF, 6, 1)
    FIELD(FLL_CTL1, XT2OFF, 5, 1)
    FIELD(FLL_CTL1, SELM, 3, 2)
    FIELD(FLL_CTL1, SELS, 2, 1)
    FIELD(FLL_CTL1, FLL_DIV, 0, 2)
REG8(FLL_CTL2, 5)
    FIELD(FLL_CTL2, XT2S, 6, 2)
    FIELD(FLL_CTL2, LFXT1S, 4, 2)
REG8(SVSCTL, 6)

#define R_FLL_CTL0_OF_MASK (R_FLL_CTL0_XT2OF_MASK | R_FLL_CTL0_XT1OF_MASK | \
                            R_FLL_CTL0_LFOF_MASK | R_FLL_CTL0_DCOF_MASK)

static const struct {
    uint64_t min, max;
} dco_range[] = {
    {  650000,  6100000 },
    { 1300000, 12100000 },
    { 2000000, 17900000 },
    { 2800000, 26600000 },
    { 4200000, 46000000},
};

static const struct {
    uint64_t min, max;
} xt_range[] = {
    {  400000,  1000000 },
    { 1000000,  3000000 },
    { 3000000, 16000000 },
    {  400000, 16000000 },
};

static void fllp_set_clocks(MSP430FLLPState *fllp)
{
    uint64_t xt1 = clock_get_hz(fllp->xt1);
    uint64_t xt2 = clock_get_hz(fllp->xt2);
    uint64_t lfxt1 = 0, dcoclk, mclk, smclk;
    uint8_t xts_fll = FIELD_EX8(fllp->fll_ctl0, FLL_CTL0, XTS_FLL);
    uint8_t old_fll_ctl0 = fllp->fll_ctl0;
    uint8_t xt2s = FIELD_EX8(fllp->fll_ctl2, FLL_CTL2, XT2S);
    uint8_t lfxt1s = FIELD_EX8(fllp->fll_ctl2, FLL_CTL2, LFXT1S);
    int i;

    fllp->fll_ctl0 &= ~(R_FLL_CTL0_XT1OF_MASK | R_FLL_CTL0_LFOF_MASK);
    if (fllp->has_xts && xts_fll) {
        if (xt1 >= 450000) {
            lfxt1 = xt1;
        } else {
            fllp->fll_ctl0 |= R_FLL_CTL0_XT1OF_MASK;
        }
    } else {
        switch (lfxt1s) {
        case 0:
            if (xt1 == 32768) {
                lfxt1 = xt1;
            }
            break;
        case 2:
            if (fllp->has_vlo) {
                lfxt1 = 12000;
            }
            break;
        }

        if (!lfxt1) {
            fllp->fll_ctl0 |= R_FLL_CTL0_LFOF_MASK;
        }
    }

    if (xt2 < xt_range[xt2s].min || xt2 > xt_range[xt2s].max) {
        xt2 = 0;
    }

    if (xt2 || !clock_has_source(fllp->xt2)) {
        fllp->fll_ctl0 &= ~R_FLL_CTL0_LFOF_MASK;
    } else {
        fllp->fll_ctl0 |= R_FLL_CTL0_LFOF_MASK;
    }

    dcoclk = lfxt1;
    dcoclk *= FIELD_EX8(fllp->scfqctl, SCFQCTL, N) + 1;
    i = 8 - clz8(FIELD_EX8(fllp->scfi0, SCFI0, FN));
    if (dcoclk < dco_range[i].min) {
        dcoclk = dco_range[i].min;
        fllp->fll_ctl0 |= R_FLL_CTL0_DCOF_MASK;
    } else if (dcoclk > dco_range[i].max) {
        dcoclk = dco_range[i].max;
        fllp->fll_ctl0 |= R_FLL_CTL0_DCOF_MASK;
    } else {
        fllp->fll_ctl0 &= ~R_FLL_CTL0_DCOF_MASK;
    }

    if (fllp->scfqctl & R_SCFQCTL_SCFQ_M_MASK) {
        dcoclk >>= FIELD_EX8(fllp->scfi0, SCFI0, FLLD);
    }

    if (fllp->fll_ctl0 != old_fll_ctl0) {
        if (fllp->fll_ctl0 & R_FLL_CTL0_OF_MASK) {
            qemu_irq_raise(fllp->irq);
        } else {
            qemu_irq_lower(fllp->irq);
        }
    }

    clock_set_hz(fllp->aclk, lfxt1);
    clock_set_hz(fllp->aclk_n, lfxt1 >> FIELD_EX8(fllp->fll_ctl1, FLL_CTL1,
                                                  FLL_DIV));

    mclk = dcoclk;

    if (fllp->has_sel) {
        switch (FIELD_EX8(fllp->fll_ctl1, FLL_CTL1, SELM)) {
        case 2:
            if (clock_has_source(fllp->xt2)) {
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
    }

    if (fllp->cpuoff) {
        mclk = 0;
    }
    clock_set_hz(fllp->mclk, mclk);
    
    if (fllp->has_sel && (fllp->fll_ctl1 & R_FLL_CTL1_SELS_MASK)) {
        smclk = xt2;
    } else {
        smclk = dcoclk;
    }

    if (fllp->fll_ctl1 & R_FLL_CTL1_SMCLKOFF_MASK) {
        smclk = 0;
    }
    clock_set_hz(fllp->smclk, smclk);

    clock_propagate(fllp->aclk);
    clock_propagate(fllp->aclk_n);
    clock_propagate(fllp->mclk);
    clock_propagate(fllp->smclk);
}

static void fllp_set_cpuoff(void *opaque, int irq, int level)
{
    MSP430FLLPState *fllp = opaque;
    
    fllp->cpuoff = level;
    fllp_set_clocks(fllp);
}

static void fllp_clk_callback(void *opaque, ClockEvent event)
{
    MSP430FLLPState *fllp = opaque;

    fllp_set_clocks(fllp);
}

static void fllp_reset_hold(Object *obj)
{
    MSP430FLLPState *fllp = MSP430_FLLP(obj);

    if (!(fllp->fll_ctl0 & R_FLL_CTL0_LFOF_MASK)) {
        qemu_irq_lower(fllp->irq);
    }

    fllp->scfqctl = 0x1f;
    fllp->scfi0 = 0x40;
    fllp->scfi1 = 0x00;
    fllp->fll_ctl0 &= R_FLL_CTL0_LFOF_MASK;
    fllp->fll_ctl0 |= 0x01;
    fllp->fll_ctl1 &= ~(R_FLL_CTL1_LFXT1DIG_MASK | R_FLL_CTL1_SMCLKOFF_MASK);
    fllp->fll_ctl2 = 0x00;

    fllp_set_clocks(fllp);
}

static uint64_t fllp_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430FLLPState *fllp = opaque;

    switch (addr) {
    case A_SCFQCTL:
        return fllp->scfqctl;
    case A_SCFI0:
        return fllp->scfi0;
    case A_SCFI1:
        return fllp->scfi1;
    case A_FLL_CTL0:
        return fllp->fll_ctl0;
    case A_FLL_CTL1:
        return fllp->fll_ctl1;
    case A_FLL_CTL2:
        return fllp->fll_ctl2;
    case A_SVSCTL:
        qemu_log_mask(LOG_UNIMP, "msp430_fllp: SVSCTL not implemented\n");
        return UINT64_MAX;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "msp430_fllp: No register at 0x%" HWADDR_PRIX "\n", addr);
    return UINT64_MAX;
}

static void fllp_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430FLLPState *fllp = opaque;

    switch (addr) {
    case A_SCFQCTL:
        fllp->scfqctl = val;
        break;
    case A_SCFI0:
        fllp->scfi0 = val;
        break;
    case A_SCFI1:
        fllp->scfi1 = val;
        break;
    case A_FLL_CTL0:
        fllp->fll_ctl0 &= R_FLL_CTL0_OF_MASK;
        fllp->fll_ctl0 |= val & ~R_FLL_CTL0_OF_MASK;
        break;
    case A_FLL_CTL1:
        fllp->fll_ctl1 = val;
        break;
    case A_FLL_CTL2:
        fllp->fll_ctl2 = val;
        break;
    case A_SVSCTL:
        qemu_log_mask(LOG_UNIMP, "msp430_fllp: SVSCTL not implemented\n");
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "msp430_fllp: No register at 0x%" HWADDR_PRIX "\n",
                      addr);
        return;
    }

    fllp_set_clocks(fllp);
}

static const MemoryRegionOps fllp_ops = {
    .write = fllp_write,
    .read  = fllp_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const ClockPortInitArray fllp_clocks = {
    QDEV_CLOCK_IN(MSP430FLLPState, xt1, fllp_clk_callback, ClockUpdate),
    QDEV_CLOCK_IN(MSP430FLLPState, xt2, fllp_clk_callback, ClockUpdate),
    QDEV_CLOCK_OUT(MSP430FLLPState, aclk),
    QDEV_CLOCK_OUT(MSP430FLLPState, aclk_n),
    QDEV_CLOCK_OUT(MSP430FLLPState, mclk),
    QDEV_CLOCK_OUT(MSP430FLLPState, smclk),
    QDEV_CLOCK_END,
};

static void fllp_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MSP430FLLPState *fllp = MSP430_FLLP(obj);

    memory_region_init_io(&fllp->memory, OBJECT(fllp), &fllp_ops, fllp,
                          "msp430-bcm+", 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &fllp->memory);

    qdev_init_clocks(DEVICE(obj), fllp_clocks);
    sysbus_init_irq(d, &fllp->irq);
    qdev_init_gpio_in_named(DEVICE(d), fllp_set_cpuoff, "cpuoff", 1);

    fllp->scfqctl = 0x1f;
    fllp->scfi0 = 0x40;
    fllp->scfi1 = 0x00;
    fllp->fll_ctl0 = 0x03;
    fllp->fll_ctl1 = 0x20;
    fllp->fll_ctl2 = 0x00;
}

static int fllp_post_load(void *opaque, int version_id)
{
    MSP430FLLPState *fllp = opaque;

    fllp_set_clocks(fllp);
    return 0;
}

static const VMStateDescription vmstate_fllp = {
    .name = "msp430-fll+",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = fllp_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(scfqctl, MSP430FLLPState),
        VMSTATE_UINT8(scfi0, MSP430FLLPState),
        VMSTATE_UINT8(scfi1, MSP430FLLPState),
        VMSTATE_UINT8(fll_ctl0, MSP430FLLPState),
        VMSTATE_UINT8(fll_ctl1, MSP430FLLPState),
        VMSTATE_UINT8(fll_ctl2, MSP430FLLPState),
        VMSTATE_CLOCK(xt1, MSP430FLLPState),
        VMSTATE_CLOCK(xt2, MSP430FLLPState),
        VMSTATE_END_OF_LIST()
    }
};

static Property fllp_properties[] = {
    DEFINE_PROP_BOOL("has_xts", MSP430FLLPState, has_xts, true),
    DEFINE_PROP_BOOL("has_sel", MSP430FLLPState, has_sel, true),
    DEFINE_PROP_BOOL("has_vlo", MSP430FLLPState, has_vlo, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void fllp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "MSP430 frequency-locked loop (FLL+) clock module";
    dc->vmsd = &vmstate_fllp;
    rc->phases.hold = fllp_reset_hold;
    device_class_set_props(dc, fllp_properties);
}

static const TypeInfo fllp_info = {
    .parent = TYPE_SYS_BUS_DEVICE,
    .name = TYPE_MSP430_FLLP,
    .instance_size = sizeof(MSP430FLLPState),
    .instance_init = fllp_init,
    .class_init = fllp_class_init,
};

static void fllp_register_types(void)
{
    type_register_static(&fllp_info);
}
type_init(fllp_register_types);
