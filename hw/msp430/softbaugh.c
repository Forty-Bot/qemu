/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "hw/display/gpio_lcd.h"
#include "hw/misc/led.h"
#include "hw/qdev-properties.h"
#include "msp430.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"
#include "qom/object.h"
#include "sysemu/reset.h"

typedef struct {
    MachineState parent_obj;

    MSP430State mcu;
    GPIOLCDState lcd;
    SplitIRQ unused[4];
} ES449MachineState;

#define TYPE_ES449_MACHINE MACHINE_TYPE_NAME("es449")
DECLARE_INSTANCE_CHECKER(ES449MachineState, ES449_MACHINE, TYPE_ES449_MACHINE)

static void es449_create_led(ES449MachineState *ems, const char *desc,
                             int port, int io)
{
    LEDState *led = led_create_simple(OBJECT(ems), GPIO_POLARITY_ACTIVE_LOW,
                                      LED_COLOR_GREEN, desc);

    qdev_connect_gpio_out(DEVICE(&ems->mcu.port[port]), io,
                          qdev_get_gpio_in(DEVICE(led), 0));
}

/* "seventh digit" map SBLCDA2 -> MSP430 */
static const int segment_map[] = {
    37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27,
    26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16,
    15, 14, 13, 12,  4,  5,  6,  7,  8,  9, 10,
    11,  3,  2,  0, -1, -1,  0,  0,  1, 39, 38,
};

static void es449_machine_init(MachineState *machine)
{
    ES449MachineState *ems = ES449_MACHINE(machine);
    DeviceState *flash, *lcd, *lcdc;
    DriveInfo *dinfo;
    int i;

    object_initialize_child(OBJECT(machine), "mcu", &ems->mcu,
                            TYPE_MSP430F449_MCU);
    clock_set_hz(ems->mcu.xt1, 32768);
    lcdc = DEVICE(&ems->mcu.lcd);
    qdev_realize(DEVICE(&ems->mcu), NULL, &error_fatal);

    if (machine->kernel_filename) {
        msp430_load_kernel(&ems->mcu, machine->kernel_filename);
    }

    if (machine->firmware) {
        msp430_load_bsl(&ems->mcu, machine->firmware);
    }

    object_initialize_child(OBJECT(machine), "lcd", &ems->lcd, "sblcda2");
    lcd = DEVICE(&ems->lcd);
    qdev_realize(lcd, NULL, &error_fatal);

    for (i = 0; i < ARRAY_SIZE(ems->unused); i++) {
        g_autofree char *name = g_strdup_printf("unused-segment%d", i);
        g_autofree char *out = g_strdup_printf("out[%d]", i);
        DeviceState *unused;

        object_initialize_child(OBJECT(machine), name, &ems->unused[i],
                                TYPE_SPLIT_IRQ);
        unused = DEVICE(&ems->unused[i]);
        object_property_set_int(OBJECT(unused), "num-lines", 3, &error_fatal);
        qdev_connect_gpio_out_named(lcdc, out, 0, qdev_get_gpio_in(unused, 0));
        qdev_realize(unused, NULL, &error_fatal);
        qdev_connect_gpio_out(unused, 0, qdev_get_gpio_in(lcd, i * 44 + 37));
        qdev_connect_gpio_out(unused, 1, qdev_get_gpio_in(lcd, i * 44 + 40));
        qdev_connect_gpio_out(unused, 2, qdev_get_gpio_in(lcd, i * 44 + 41));
    }

    assert(ARRAY_SIZE(segment_map) == 44);
    for (i = 0; i < ARRAY_SIZE(segment_map); i++) {
        if (segment_map[i] <= 0) {
            continue;
        }

        qdev_connect_gpio_out_named(lcdc, "out[0]", segment_map[i],
                                    qdev_get_gpio_in(lcd, i + 1));
        qdev_connect_gpio_out_named(lcdc, "out[1]", segment_map[i],
                                    qdev_get_gpio_in(lcd, 44 + i + 1));
        qdev_connect_gpio_out_named(lcdc, "out[2]", segment_map[i],
                                    qdev_get_gpio_in(lcd, 88 + i + 1));
        qdev_connect_gpio_out_named(lcdc, "out[3]", segment_map[i],
                                    qdev_get_gpio_in(lcd, 132 + i + 1));
    }

    flash = qdev_new("sst25vf020");
    dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        qdev_prop_set_drive_err(flash, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
    }
    qdev_realize_and_unref(flash, BUS(ems->mcu.usart[1].spi_bus), &error_fatal);
    qdev_connect_gpio_out(DEVICE(&ems->mcu.port[2]), 0,
                          qdev_get_gpio_in_named(flash, SSI_GPIO_CS, 0));

    es449_create_led(ems, "D1", 0, 0);
    es449_create_led(ems, "D2", 0, 1);
}

static void es449_cpu_reset(MachineState *ms, ShutdownCause reason)
{
    ES449MachineState *ems = ES449_MACHINE(ms);
    MSP430CPU *cpu = &ems->mcu.cpu;

    qemu_devices_reset(reason);
    cpu_reset(CPU(cpu));
}

static void es449_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    /*
     * https://web.archive.org/web/20150611085105/http://www.softbaugh.com/ProductPage.cfm?strPartNo=ES449
     */
    mc->desc = "SoftBaugh ES449 evaluation system";
    mc->init = es449_machine_init;
    mc->reset = es449_cpu_reset;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_sdcard = 1;
};

static const TypeInfo launchpad_machine_types[] = {
    {
        .name = TYPE_ES449_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(ES449MachineState),
        .class_init = es449_class_init,
    },
};

DEFINE_TYPES(launchpad_machine_types)
