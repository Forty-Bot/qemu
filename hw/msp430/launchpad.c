/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/misc/led.h"
#include "msp430.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "qom/object.h"
#include "sysemu/reset.h"

typedef struct {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    MSP430State mcu;
    LEDState *d1, *d2, *d3r, *d3b, *d3g;
} LaunchpadMachineState;

typedef struct {
    /*< private >*/
    MachineClass parent_class;
    /*< public >*/
    const char *mcu_type;
} LaunchpadMachineClass;

#define TYPE_LAUNCHPAD_MACHINE MACHINE_TYPE_NAME("launchpad")
DECLARE_OBJ_CHECKERS(LaunchpadMachineState, LaunchpadMachineClass,
                     LAUNCHPAD_MACHINE, TYPE_LAUNCHPAD_MACHINE)

static void launchpad_create_led(LaunchpadMachineState *lms, LEDColor color,
                                 const char *desc, int port, int io)
{
    LEDState *led = led_create_simple(OBJECT(lms), GPIO_POLARITY_ACTIVE_HIGH,
                                      color, desc);

    qdev_connect_gpio_out(DEVICE(&lms->mcu.port[port]), io,
                          qdev_get_gpio_in(DEVICE(led), 0));
}

static void launchpad_machine_init(MachineState *machine)
{
    LaunchpadMachineClass *lmc = LAUNCHPAD_MACHINE_GET_CLASS(machine);
    LaunchpadMachineState *lms = LAUNCHPAD_MACHINE(machine);

    object_initialize_child(OBJECT(machine), "mcu", &lms->mcu, lmc->mcu_type);

    qemu_printf("load firmware %s\n", machine->firmware);
    if (machine->firmware &&
        rom_add_file_mr(machine->firmware, &lms->mcu.flash, 0) < 0) {
        error_report("Failed to load ROM image '%s'", machine->firmware);
        exit(1);
    }

    qdev_realize(DEVICE(&lms->mcu), NULL, &error_abort);

    launchpad_create_led(lms, LED_COLOR_GREEN, "LED1", 0, 0);
    launchpad_create_led(lms, LED_COLOR_RED, "LED2", 0, 6);
    launchpad_create_led(lms, LED_COLOR_BLUE, "LED3blue", 1, 5);
    launchpad_create_led(lms, LED_COLOR_RED, "LED3red", 1, 1);
    launchpad_create_led(lms, LED_COLOR_GREEN, "LED3green", 1, 3);
}

static void launchpad_cpu_reset(MachineState *ms, ShutdownCause reason)
{
    LaunchpadMachineState *lms = LAUNCHPAD_MACHINE(ms);
    MSP430CPU *cpu = &lms->mcu.cpu;

    qemu_devices_reset(reason);
    cpu_reset(CPU(cpu));
}

static void launchpad_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = launchpad_machine_init;
    mc->reset = launchpad_cpu_reset;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_sdcard = 1;
}

static void msp_exp430g2et_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    LaunchpadMachineClass *lmc = LAUNCHPAD_MACHINE_CLASS(oc);

    /*
     * https://www.ti.com/tool/MSP-EXP430G2ET
     * https://www.ti.com/lit/ds/slas735j/slas735j.pdf
     */
    mc->desc        = "MSP-EXP430G2ET LaunchPad development kit",
    lmc->mcu_type   = TYPE_MSP430G2553_MCU;
};

static const TypeInfo launchpad_machine_types[] = {
    {
        .name           = TYPE_LAUNCHPAD_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(LaunchpadMachineState),
        .class_size     = sizeof(LaunchpadMachineClass),
        .class_init     = launchpad_machine_class_init,
        .abstract       = true,
    },
    {
        .name          = MACHINE_TYPE_NAME("msp-exp430g2et"),
        .parent        = TYPE_LAUNCHPAD_MACHINE,
        .class_init    = msp_exp430g2et_class_init,
    },
};

DEFINE_TYPES(launchpad_machine_types)
