/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "msp430.h"
#include "qom/object.h"

typedef struct {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    MSP430State mcu;
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

static void launchpad_machine_init(MachineState *machine)
{
    LaunchpadMachineClass *lmc = LAUNCHPAD_MACHINE_GET_CLASS(machine);
    LaunchpadMachineState *lms = LAUNCHPAD_MACHINE(machine);

    object_initialize_child(OBJECT(machine), "mcu", &lms->mcu, lmc->mcu_type);
    qdev_realize(DEVICE(&lms->mcu), NULL, &error_abort);

    if (machine->firmware &&
        !load_image_mr(machine->firmware, &lms->mcu.flash)) {
        exit(1);
    }
}

static void launchpad_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = launchpad_machine_init;
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
