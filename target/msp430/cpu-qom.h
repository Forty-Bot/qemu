/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_CPU_QOM_H
#define MSP430_CPU_QOM_H

#include "hw/core/cpu.h"
#include "qom/object.h"

#define TYPE_MSP430_CPU "msp430-cpu"

OBJECT_DECLARE_CPU_TYPE(MSP430CPU, MSP430CPUClass, MSP430_CPU)

/*
 * MSP430CPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class' reset phase handlers.
 *
 * A MSP430 CPU model.
 */
struct MSP430CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

#endif /* MSP430_CPU_QOM_H */
