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

#include "hw/char/msp430_usart.h"
#include "hw/char/msp430_usci.h"
#include "hw/core/split-irq.h"
#include "hw/gpio/msp430_lcd.h"
#include "hw/gpio/msp430_port.h"
#include "hw/intc/msp430_sfr.h"
#include "hw/irq.h"
#include "hw/msp430/bcmp.h"
#include "hw/msp430/fllp.h"
#include "hw/msp430/mpy.h"
#include "hw/or-irq.h"
#include "hw/timer/msp430_bt.h"
#include "hw/timer/msp430_timer.h"
#include "qom/object.h"
#include "target/msp430/cpu.h"

#define TYPE_MSP430_MCU         "msp430"
#define TYPE_MSP430X2XX_MCU     "msp430x2xx"
#define TYPE_MSP430F2012_MCU    "msp430f2012"
#define TYPE_MSP430G2553_MCU    "msp430g2553"
#define TYPE_MSP430X4XX_MCU     "msp430x4xx"
#define TYPE_MSP430F449_MCU     "msp430f449"

typedef struct {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    MSP430CPU cpu;
    MemoryRegion flash, info, sram, mirror, bsl;
    SplitIRQ reset_ack, nmi_ack;
    OrIRQState cpu_irq[NUM_IRQS];
    MSP430SFRState sfr;
    union {
        MSP430BCMPState bcmp;
        MSP430FLLPState fllp;
    };
    Clock *xt1, *xt2, *aclk, *mclk, *smclk;
    MSP430MPYState mpy;
    MSP430PortState port[6];
    MSP430Port16State port16[2];
    MSP430BasicTimerState bt;
    MSP430TimerState timer[2];
    union {
        struct {
            MSP430USCIAState usci_a[2];
            MSP430USCIBState usci_b[2];
        };
        MSP430USARTState usart[2];
    };
    MSP430LCDState lcd;
} MSP430State;

DECLARE_INSTANCE_CHECKER(MSP430State, MSP430_MCU, TYPE_MSP430_MCU)

void msp430_load_kernel(MSP430State *s, const char *filename);
void msp430_load_bsl(MSP430State *s, const char *filename);

#endif /* HW_MSP430_H */
