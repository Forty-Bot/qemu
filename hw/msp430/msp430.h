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

#include "hw/char/msp430_usci.h"
#include "hw/core/split-irq.h"
#include "hw/gpio/msp430_port.h"
#include "hw/intc/msp430_sfr.h"
#include "hw/irq.h"
#include "hw/or-irq.h"
#include "hw/timer/msp430_timer.h"
#include "hw/msp430/fllp.h"
#include "hw/msp430/bcmp.h"
#include "qom/object.h"
#include "target/msp430/cpu.h"

#define TYPE_MSP430_MCU         "msp430"
#define TYPE_MSP430X2XX_MCU     "msp430x2xx"
#define TYPE_MSP430G2553_MCU    "msp430g2553"
#define TYPE_MSP430X4XX_MCU     "msp430x4xx"
#define TYPE_MSP430F449_MCU     "msp430f449"

typedef struct {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    MSP430CPU cpu;
    SplitIRQ reset_ack, nmi_ack;
    OrIRQState cpu_irq[NUM_IRQS];
    MSP430SFRState sfr;
    union {
        MSP430BCMPState bcmp;
        MSP430FLLPState fllp;
    };
    MSP430TimerState timer[2];
    MSP430PortState port[6];
    MSP430Port16State port16[2];
    MSP430USCIAState usci_a[2];
    MSP430USCIBState usci_b[2];
    MemoryRegion flash, info, sram, mirror, bsl;
    Clock *xt1, *xt2, *aclk, *mclk, *smclk;
    qemu_irq in[80], out[80];
} MSP430State;

DECLARE_INSTANCE_CHECKER(MSP430State, MSP430_MCU, TYPE_MSP430_MCU)

#endif /* HW_MSP430_H */
