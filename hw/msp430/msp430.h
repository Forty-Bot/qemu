/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023-24 Sean Anderson <seanga2@gmail.com>
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
#include "hw/misc/clk_gpio.h"
#include "hw/msp430/bcm.h"
#include "hw/msp430/fllp.h"
#include "hw/msp430/mpy.h"
#include "hw/or-irq.h"
#include "hw/timer/msp430_bt.h"
#include "hw/timer/msp430_timer.h"
#include "hw/watchdog/msp430_wdt.h"
#include "qom/object.h"
#include "target/msp430/cpu.h"

#define TYPE_MSP430_MCU         "msp430"
#define TYPE_MSP430X1XX_MCU     "msp430x1xx"
#define TYPE_MSP430F1611_MCU    "msp430f1611"
#define TYPE_MSP430X2XX_MCU     "msp430x2xx"
#define TYPE_MSP430F2012_MCU    "msp430f2012"
#define TYPE_MSP430G2553_MCU    "msp430g2553"
#define TYPE_MSP430X4XX_MCU     "msp430x4xx"
#define TYPE_MSP430F449_MCU     "msp430f449"
#define TYPE_MSP430F4794_MCU    "msp430f4794"

typedef struct {
    DeviceState parent_obj;

    MSP430CPU cpu;
    MemoryRegion flash, info, sram, mirror, bsl;
    SplitIRQ puc, reset_ack, nmi_ack, aclk_cci;
    OrIRQState puc_latch, cpu_irq[NUM_IRQS];
    MSP430SFRState sfr;
    union {
        MSP430BCMState bcm;
        MSP430FLLPState fllp;
    };
    Clock *xt1, *xt2, *aclk, *mclk, *smclk;
    ClockGPIOState aclk_gpio;
    MSP430MPYState mpy;
    MSP430PortState port[6];
    MSP430Port16State port16[2];
    union {
        MSP430WDTState wdt;
        MSP430WDTPState wdtp;
    };
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

    qemu_irq puc_level;
} MSP430State;

DECLARE_INSTANCE_CHECKER(MSP430State, MSP430_MCU, TYPE_MSP430_MCU)

void msp430_load_kernel(MSP430State *s, const char *filename);
void msp430_load_bsl(MSP430State *s, const char *filename);

#endif /* HW_MSP430_H */
