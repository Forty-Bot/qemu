/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
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

    const char *mcu_type;
    uint32_t xt1_freq;
} LaunchpadMachineState;

typedef struct {
    MachineClass parent_class;

    const char *const *mcu_support;
    const char *default_mcu_type;
    bool has_led3;
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

    if (!lms->mcu_type) {
        lms->mcu_type = lmc->default_mcu_type;
    }

    object_initialize_child(OBJECT(machine), "mcu", &lms->mcu, lms->mcu_type);
    clock_set_hz(lms->mcu.xt1, lms->xt1_freq);
    qdev_realize(DEVICE(&lms->mcu), NULL, &error_fatal);

    if (machine->kernel_filename) {
        msp430_load_kernel(&lms->mcu, machine->kernel_filename);
    }

    if (machine->firmware) {
        msp430_load_bsl(&lms->mcu, machine->firmware);
    }

    launchpad_create_led(lms, LED_COLOR_GREEN, "LED1", 0, 0);
    launchpad_create_led(lms, LED_COLOR_RED, "LED2", 0, 6);
    if (lmc->has_led3) {
        launchpad_create_led(lms, LED_COLOR_BLUE, "LED3blue", 1, 5);
        launchpad_create_led(lms, LED_COLOR_RED, "LED3red", 1, 1);
        launchpad_create_led(lms, LED_COLOR_GREEN, "LED3green", 1, 3);
    }
}

static void launchpad_cpu_reset(MachineState *ms, ShutdownCause reason)
{
    LaunchpadMachineState *lms = LAUNCHPAD_MACHINE(ms);
    MSP430CPU *cpu = &lms->mcu.cpu;

    qemu_devices_reset(reason);
    cpu_reset(CPU(cpu));
}

static char *launchpad_get_mcu_type(Object *obj, Error **errp)
{
    LaunchpadMachineState *lms = LAUNCHPAD_MACHINE(obj);

    return g_strdup(lms->mcu_type);
}

static void launchpad_set_mcu_type(Object *obj, const char *mcu_type, Error **errp)
{
    LaunchpadMachineClass *lmc = LAUNCHPAD_MACHINE_GET_CLASS(obj);
    LaunchpadMachineState *lms = LAUNCHPAD_MACHINE(obj);
    const char *const *mcu_typep;

    for (mcu_typep = lmc->mcu_support; *mcu_typep; mcu_typep++) {
        if (!strcmp(*mcu_typep, mcu_type)) {
            break;
        }
    }

    if (!*mcu_typep) {
        error_setg(errp, "Unsupported MCU type '%s'\n", mcu_type);
    } else {
        lms->mcu_type = *mcu_typep;
    }
}

static void launchpad_visit_xt1_freq(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    LaunchpadMachineState *lms = LAUNCHPAD_MACHINE(obj);

    visit_type_uint32(v, name, &lms->xt1_freq, errp);
}

static void launchpad_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    object_class_property_add_str(oc, "mcu-type", launchpad_get_mcu_type,
                                  launchpad_set_mcu_type);
    object_class_property_set_description(oc, "mcu-type",
                                          "Type of the MCU in the socket");

    object_class_property_add(oc, "xt1-frequency", "uint32",
                              launchpad_visit_xt1_freq, launchpad_visit_xt1_freq,
                              NULL, NULL);
    object_class_property_set_description(oc, "xt1-frequency",
                                          "Frequency of XIN/XOUT. Set to 0 to disable (default)");

    mc->init = launchpad_machine_init;
    mc->reset = launchpad_cpu_reset;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_sdcard = 1;
}

static const char *const msp_exp430g2_support[] = {
    TYPE_MSP430F2012_MCU,
    TYPE_MSP430G2553_MCU,
    NULL,
};

static void msp_exp430g2_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    LaunchpadMachineClass *lmc = LAUNCHPAD_MACHINE_CLASS(oc);

    /*
     * https://dev.ti.com/tirex/explore/node?node=A__ABjGDxJw12fXjxtYpSJOow__msp430_devtools__FUz-xrs__LATEST
     * https://www.ti.com/lit/ds/slas735j/slau318g.pdf
     */
    mc->desc = "MSP-EXP430G2 LaunchPad development kit";
    lmc->mcu_support = msp_exp430g2_support;
    lmc->default_mcu_type = TYPE_MSP430F2012_MCU;
};

static const char *const msp_exp430g2et_support[] = {
    TYPE_MSP430F2012_MCU,
    TYPE_MSP430G2553_MCU,
    NULL,
};

static void msp_exp430g2et_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    LaunchpadMachineClass *lmc = LAUNCHPAD_MACHINE_CLASS(oc);

    /*
     * https://www.ti.com/tool/MSP-EXP430G2ET
     * https://www.ti.com/lit/ds/slas735j/slas735j.pdf
     */
    mc->desc = "MSP-EXP430G2ET LaunchPad development kit";
    lmc->mcu_support = msp_exp430g2et_support;
    lmc->default_mcu_type = TYPE_MSP430G2553_MCU;
    lmc->has_led3 = true;
};

static const TypeInfo launchpad_machine_types[] = {
    {
        .name = TYPE_LAUNCHPAD_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(LaunchpadMachineState),
        .class_size = sizeof(LaunchpadMachineClass),
        .class_init = launchpad_machine_class_init,
        .abstract = true,
    },
    {
        .name = MACHINE_TYPE_NAME("msp-exp430g2et"),
        .parent = TYPE_LAUNCHPAD_MACHINE,
        .class_init = msp_exp430g2et_class_init,
    },
    {
        .name = MACHINE_TYPE_NAME("msp-exp430g2"),
        .parent = TYPE_LAUNCHPAD_MACHINE,
        .class_init = msp_exp430g2_class_init,
    },
};

DEFINE_TYPES(launchpad_machine_types)
