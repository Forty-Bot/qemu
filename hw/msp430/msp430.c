/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "msp430.h"
#include "qom/object.h"

typedef struct {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/
    size_t flash_size;
    size_t sram_size;
    size_t bsl_size;
} MSP430Class;

DECLARE_CLASS_CHECKERS(MSP430Class, MSP430_MCU, TYPE_MSP430_MCU)

static void msp430_realize(DeviceState *dev, Error **errp)
{
    MSP430State *s = MSP430_MCU(dev);
    const MSP430Class *mc = MSP430_MCU_GET_CLASS(dev);

    /* CPU */
    object_initialize_child(OBJECT(dev), "cpu", &s->cpu, TYPE_MSP430_CPU);
    qdev_realize(DEVICE(&s->cpu), NULL, &error_abort);

    /* SRAM */
    memory_region_init_ram(&s->sram, OBJECT(dev), "sram", mc->sram_size,
                           &error_abort);
    if (mc->sram_size > 0x800) {
        memory_region_add_subregion(get_system_memory(), 0x1100, &s->sram);
        memory_region_init_alias(&s->mirror, OBJECT(dev), "mirror", &s->sram,
                                 0, 0x800);
        memory_region_add_subregion(&s->sram, 0x200, &s->mirror);
    } else {
        memory_region_add_subregion(get_system_memory(), 0x200, &s->sram);
    }

    /* Flash */
    memory_region_init_rom(&s->flash, OBJECT(dev), "flash", mc->flash_size,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x10000 - mc->flash_size,
                                &s->flash);
    memory_region_init_rom(&s->info, OBJECT(dev), "info", 0x100, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1000, &s->info);

    /* ROM */
    if (mc->bsl_size) {
        memory_region_init_rom(&s->bsl, OBJECT(dev), "bsl", mc->bsl_size,
                               &error_fatal);
        memory_region_add_subregion(get_system_memory(), 0xc00, &s->bsl);
    }

    /*
     * I/O
     *
     * 0x000 - 0x01f: SFRs
     * 0x020 - 0x0ff: 8-bit I/O
     * 0x100 - 0x1ff: 16-bit I/O
     */
    create_unimplemented_device("msp430-io", 0, 0x200);
}

static void msp430_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = msp430_realize;
}

static void msp430g2553_class_init(ObjectClass *oc, void *data)
{
    MSP430Class *mc = MSP430_MCU_CLASS(oc);

    mc->flash_size = 16 * KiB;
    mc->sram_size = 512;
    mc->bsl_size = KiB;
}

static const TypeInfo msp430_mcu_types[] = {
    {
        .name           = TYPE_MSP430_MCU,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(MSP430State),
        .class_size     = sizeof(MSP430Class),
        .class_init     = msp430_class_init,
        .abstract       = true,
    },
    {
        .name           = TYPE_MSP430G2553_MCU,
        .parent         = TYPE_MSP430_MCU,
        .class_init     = msp430g2553_class_init,
    },
};

DEFINE_TYPES(msp430_mcu_types)
