/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/intc/intc.h"
#include "hw/intc/msp430_sfr.h"
#include "hw/irq.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

REG8(IE1, 0)
REG8(IE2, 1)
REG8(IFG1, 2)
REG8(IFG2, 3)
REG8(ME1, 4)
REG8(ME2, 5)
REG8(UC1IE, 6)
REG8(UC1IFG, 7)

static void sfr_set_irq(void *opaque, int irq, int level)
{
    MSP430SFRState *sfr = opaque;
    uint32_t old_irq = sfr->ifg & sfr->ie;

    if (level) {
        sfr->ifg |= BIT(irq);
    } else {
        sfr->ifg &= ~BIT(irq);
    }

    if ((sfr->ifg & sfr->ie) != old_irq) {
        if (level) {
            sfr->irq_stats[irq]++;
        }
        qemu_set_irq(sfr->irq[irq], level);
    }
}

static void sfr_clear_ie(void *opaque, int irq, int level)
{
    MSP430SFRState *sfr = opaque;

    if (sfr->ifg & sfr->ie & BIT(irq)) {
        qemu_irq_lower(sfr->irq[irq]);
    }

    sfr->ie &= ~(BIT(irq));
}

static void sfr_ack_irq(void *opaque, int irq, int level)
{
    MSP430SFRState *sfr = opaque;

    if (sfr->ifg & sfr->ie & BIT(irq)) {
        qemu_irq_lower(sfr->irq[irq]);
    }

    /* These are edge-triggered, so leave the flags set */
    if (irq != SFR_POR && irq != SFR_RST) {
        sfr->ifg &= ~(BIT(irq));
    }
}

static uint64_t sfr_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430SFRState *sfr = opaque;

    switch (addr) {
    case A_IE1:
        return sfr->ie & ~(BIT(SFR_POR) | BIT(SFR_RST));
    case A_IE2:
        return sfr->ie >> 8;
    case A_IFG1:
        if (sfr->has_reset_cause) {
            return sfr->ifg & ~BIT(SFR_ACCV);
        }
        return sfr->ifg & ~(BIT(SFR_ACCV) | BIT(SFR_POR) | BIT(SFR_RST));
    case A_IFG2:
        return sfr->ifg >> 8;
    case A_ME1:
        return sfr->me;
    case A_ME2:
        return sfr->me >> 8;
    case A_UC1IE:
        return sfr->ie >> 16;
    case A_UC1IFG:
        return sfr->ifg >> 16;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "msp430_sfr: Register 0x%" HWADDR_PRIX " not implemented.\n",
                      addr);
        return UINT64_MAX;
    }
}

static void sfr_recalculate_irq(MSP430SFRState *sfr, uint32_t old_irq)
{
    uint32_t irq_changed = (sfr->ifg & sfr->ie) ^ old_irq;
    int i;

    for (i = 0; i < MSP430_SFR_IRQS; i++) {
        if (irq_changed & BIT(i)) {
            int level = !!(sfr->ifg & sfr->ie & BIT(i));

            if (level) {
                sfr->irq_stats[i]++;
            }
            qemu_set_irq(sfr->irq[i], level);
        }
    }
}

static void sfr_recalculate_me(MSP430SFRState *sfr, uint16_t old_me)
{
    uint16_t me_changed = sfr->me ^ old_me;
    int i;

    for (i = 0; i < MSP430_SFR_MES; i++) {
        if (me_changed & BIT(i)) {
            qemu_set_irq(sfr->me_irq[i], !!(sfr->me & BIT(i)));
        }
    }
}

static void sfr_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430SFRState *sfr = opaque;
    uint32_t old_irq = sfr->ifg & sfr->ie;
    uint16_t old_me = sfr->me;

    switch (addr) {
    case A_IE1:
        sfr->ie &= ~0xff;
        sfr->ie |= val;
        sfr_recalculate_irq(sfr, old_irq);
        return;
    case A_IE2:
        sfr->ie &= ~0xff00;
        sfr->ie |= val << 8;
        sfr_recalculate_irq(sfr, old_irq);
        return;
    case A_IFG1:
        sfr->ifg &= ~0xff;
        sfr->ifg |= val;
        sfr_recalculate_irq(sfr, old_irq);
        return;
    case A_IFG2:
        sfr->ifg &= ~0xff00;
        sfr->ifg |= val << 8;
        sfr_recalculate_irq(sfr, old_irq);
        return;
    case A_ME1:
        sfr->me &= 0xff00;
        sfr->me |= val;
        sfr_recalculate_me(sfr, old_me);
        return;
    case A_ME2:
        sfr->me &= 0x00ff;
        sfr->me |= val << 8;
        sfr_recalculate_me(sfr, old_me);
        return;
    case A_UC1IE:
        sfr->ie &= ~0xff0000;
        sfr->ie |= val << 16;
        sfr_recalculate_irq(sfr, old_irq);
        return;
    case A_UC1IFG:
        sfr->ifg &= ~0xff0000;
        sfr->ifg |= val << 16;
        sfr_recalculate_irq(sfr, old_irq);
        return;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "msp430_sfr: Register 0x%" HWADDR_PRIX " not implemented.\n",
                      addr);
        return;
    }
}

static const MemoryRegionOps sfr_ops = {
    .write = sfr_write,
    .read  = sfr_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static bool sfr_get_irq_stats(InterruptStatsProvider *obj,
                              uint64_t **irq_counts, unsigned int *nb_irqs)
{
    MSP430SFRState *sfr = MSP430_SFR(obj);

    *irq_counts = sfr->irq_stats;
    *nb_irqs = ARRAY_SIZE(sfr->irq_stats);
    return true;
}

enum sfr_reset {
    POR,
    PIN,
    PUC,
};

static void sfr_reset(MSP430SFRState *sfr, enum sfr_reset type)
{
    uint32_t old_irq = sfr->ifg & sfr->ie;
    uint16_t old_me = sfr->me;

    sfr->ie = BIT(SFR_POR) | BIT(SFR_RST);
    sfr->me = 0;
    switch (type) {
    case POR:
        sfr->ifg |= BIT(SFR_POR);
        sfr->ifg &= ~BIT(SFR_RST);
        break;
    case PIN:
        sfr->ifg |= BIT(SFR_RST);
        break;
    case PUC:
        break;
    default:
        g_assert_not_reached();
    };
    
    sfr_recalculate_irq(sfr, old_irq);
    sfr_recalculate_me(sfr, old_me);
}

static void sfr_reset_hold(Object *obj, ResetType type)
{
    MSP430SFRState *sfr = MSP430_SFR(obj);

    sfr_reset(sfr, type == RESET_TYPE_GUEST ? PIN : POR);
}

static void sfr_puc(void *opaque, int irq, int level)
{
    if (level) {
        sfr_reset(opaque, PUC);
    }
}

static void sfr_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MSP430SFRState *sfr = MSP430_SFR(obj);

    memory_region_init_io(&sfr->memory, OBJECT(sfr), &sfr_ops, sfr,
                          "msp430-sfr", 0x10);
    sysbus_init_mmio(d, &sfr->memory);

    qdev_init_gpio_in(DEVICE(d), sfr_set_irq, MSP430_SFR_IRQS);
    qdev_init_gpio_in_named(DEVICE(d), sfr_puc, "puc", 1);
    qdev_init_gpio_in_named(DEVICE(d), sfr_clear_ie, "clear", MSP430_SFR_IRQS);
    qdev_init_gpio_in_named(DEVICE(d), sfr_ack_irq, "ack", MSP430_SFR_IRQS);
    qdev_init_gpio_out_named(DEVICE(d), sfr->irq, SYSBUS_DEVICE_GPIO_IRQ,
                             MSP430_SFR_IRQS);
    qdev_init_gpio_out_named(DEVICE(d), sfr->me_irq, "me", MSP430_SFR_MES);
}

static const VMStateDescription vmstate_sfr = {
    .name = "msp430-sfr",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ie, MSP430SFRState),
        VMSTATE_UINT32(ifg, MSP430SFRState),
        VMSTATE_UINT16(me, MSP430SFRState),
        VMSTATE_END_OF_LIST()
    }
};

static Property sfr_properties[] = {
    DEFINE_PROP_BOOL("has_reset_cause", MSP430SFRState, has_reset_cause, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void sfr_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    InterruptStatsProviderClass *ic = INTERRUPT_STATS_PROVIDER_CLASS(oc);

    dc->desc = "MSP430 special function registers (SFRs)";
    dc->vmsd = &vmstate_sfr;
    device_class_set_props(dc, sfr_properties);
    rc->phases.hold = sfr_reset_hold;
    ic->get_statistics = sfr_get_irq_stats;
}

static const TypeInfo sfr_info = {
    .parent = TYPE_SYS_BUS_DEVICE,
    .name = TYPE_MSP430_SFR,
    .instance_size = sizeof(MSP430SFRState),
    .instance_init = sfr_init,
    .class_init = sfr_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_INTERRUPT_STATS_PROVIDER },
        { },
    },
};

static void sfr_register_types(void)
{
    type_register_static(&sfr_info);
}
type_init(sfr_register_types);
