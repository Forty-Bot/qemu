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
} VirtMachineState;

typedef struct {
    MachineClass parent_class;

    const char *const *mcu_support;
    const char *default_mcu_type;
    bool has_led[3];
} VirtMachineClass;

#define TYPE_VIRT_MACHINE MACHINE_TYPE_NAME("virt")
DECLARE_OBJ_CHECKERS(VirtMachineState, VirtMachineClass,
                     VIRT_MACHINE, TYPE_VIRT_MACHINE)

static void virt_create_led(VirtMachineState *vms, LEDColor color,
                                 const char *desc, int port, int io)
{
    LEDState *led = led_create_simple(OBJECT(vms), GPIO_POLARITY_ACTIVE_HIGH,
                                      color, desc);

    qdev_connect_gpio_out(DEVICE(&vms->mcu.port[port]), io,
                          qdev_get_gpio_in(DEVICE(led), 0));
}

static void virt_machine_init(MachineState *machine)
{
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(machine);
    VirtMachineState *vms = VIRT_MACHINE(machine);

    if (!vms->mcu_type) {
        vms->mcu_type = vmc->default_mcu_type;
    }

    object_initialize_child(OBJECT(machine), "mcu", &vms->mcu, vms->mcu_type);
    clock_set_hz(vms->mcu.xt1, vms->xt1_freq);
    qdev_realize(DEVICE(&vms->mcu), NULL, &error_fatal);

    if (machine->kernel_filename) {
        msp430_load_kernel(&vms->mcu, machine->kernel_filename);
    }

    if (machine->firmware) {
        msp430_load_bsl(&vms->mcu, machine->firmware);
    }

    if (vmc->has_led[0]) {
        virt_create_led(vms, LED_COLOR_GREEN, "LED1", 0, 0);
    }
    if (vmc->has_led[1]) {
        virt_create_led(vms, LED_COLOR_RED, "LED2", 0, 6);
    }
    if (vmc->has_led[2]) {
        virt_create_led(vms, LED_COLOR_BLUE, "LED3blue", 1, 5);
        virt_create_led(vms, LED_COLOR_RED, "LED3red", 1, 1);
        virt_create_led(vms, LED_COLOR_GREEN, "LED3green", 1, 3);
    }
}

static void virt_cpu_reset(MachineState *ms, ShutdownCause reason)
{
    VirtMachineState *vms = VIRT_MACHINE(ms);
    MSP430CPU *cpu = &vms->mcu.cpu;

    qemu_devices_reset(reason);
    cpu_reset(CPU(cpu));
}

static char *virt_get_mcu_type(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return g_strdup(vms->mcu_type);
}

static void virt_set_mcu_type(Object *obj, const char *mcu_type, Error **errp)
{
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(obj);
    VirtMachineState *vms = VIRT_MACHINE(obj);
    const char *const *mcu_typep;

    for (mcu_typep = vmc->mcu_support; *mcu_typep; mcu_typep++) {
        if (!strcmp(*mcu_typep, mcu_type)) {
            break;
        }
    }

    if (!*mcu_typep) {
        error_setg(errp, "Unsupported MCU type '%s'\n", mcu_type);
    } else {
        vms->mcu_type = *mcu_typep;
    }
}

static void virt_visit_xt1_freq(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    visit_type_uint32(v, name, &vms->xt1_freq, errp);
}

static const char *const virt_support[] = {
    TYPE_MSP430F1611_MCU,
    TYPE_MSP430F2012_MCU,
    TYPE_MSP430G2553_MCU,
    TYPE_MSP430F449_MCU,
    NULL,
};

static void virt_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(oc);

    object_class_property_add_str(oc, "mcu-type", virt_get_mcu_type,
                                  virt_set_mcu_type);
    object_class_property_set_description(oc, "mcu-type",
                                          "Type of the MCU in the socket");

    object_class_property_add(oc, "xt1-frequency", "uint32",
                              virt_visit_xt1_freq, virt_visit_xt1_freq,
                              NULL, NULL);
    object_class_property_set_description(oc, "xt1-frequency",
                                          "Frequency of XIN/XOUT. Set to 0 to disable (default)");

    mc->desc = "MSP430 virtual machine (no peripherals)";
    mc->init = virt_machine_init;
    mc->reset = virt_cpu_reset;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_sdcard = 1;

    vmc->mcu_support = virt_support;
    vmc->default_mcu_type = TYPE_MSP430F1611_MCU;
}

static const char *const msp_exp430g2_support[] = {
    TYPE_MSP430F2012_MCU,
    TYPE_MSP430G2553_MCU,
    NULL,
};

static void msp_exp430g2_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(oc);

    /*
     * https://dev.ti.com/tirex/explore/node?node=A__ABjGDxJw12fXjxtYpSJOow__msp430_devtools__FUz-xrs__LATEST
     * https://www.ti.com/lit/ds/slas735j/slau318g.pdf
     */
    mc->desc = "MSP-EXP430G2 Launchpad development kit";
    vmc->mcu_support = msp_exp430g2_support;
    vmc->default_mcu_type = TYPE_MSP430F2012_MCU;
    vmc->has_led[0] = true;
    vmc->has_led[1] = true;
};

static const char *const msp_exp430g2et_support[] = {
    TYPE_MSP430F2012_MCU,
    TYPE_MSP430G2553_MCU,
    NULL,
};

static void msp_exp430g2et_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(oc);

    /*
     * https://www.ti.com/tool/MSP-EXP430G2ET
     * https://www.ti.com/lit/ds/slas735j/slas735j.pdf
     */
    mc->desc = "MSP-EXP430G2ET Launchpad development kit";
    vmc->mcu_support = msp_exp430g2et_support;
    vmc->default_mcu_type = TYPE_MSP430G2553_MCU;
    vmc->has_led[0] = true;
    vmc->has_led[1] = true;
    vmc->has_led[2] = true;
};

static const TypeInfo virt_machine_types[] = {
    {
        .name = TYPE_VIRT_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(VirtMachineState),
        .class_size = sizeof(VirtMachineClass),
        .class_init = virt_machine_class_init,
    },
    {
        .name = MACHINE_TYPE_NAME("msp-exp430g2et"),
        .parent = TYPE_VIRT_MACHINE,
        .class_init = msp_exp430g2et_class_init,
    },
    {
        .name = MACHINE_TYPE_NAME("msp-exp430g2"),
        .parent = TYPE_VIRT_MACHINE,
        .class_init = msp_exp430g2_class_init,
    },
};

DEFINE_TYPES(virt_machine_types)
