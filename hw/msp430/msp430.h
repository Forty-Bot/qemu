/*
 * QEMU ATmega MCU
 *
 * Copyright (c) 2019-2020 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MSP430_H
#define HW_MSP430_H

#include "target/msp430/cpu.h"
#include "qom/object.h"

#define TYPE_MSP430_MCU      "msp430"
#define TYPE_MSP430G2553_MCU "msp430g2553"

typedef struct {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    MSP430CPU cpu;
    MemoryRegion flash, info, sram, mirror, bsl;
} MSP430State;

DECLARE_INSTANCE_CHECKER(MSP430State, MSP430_MCU, TYPE_MSP430_MCU)

#endif /* HW_MSP430_H */
