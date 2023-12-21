/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_SFR_H
#define MSP430_SFR_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define SFR_WDT     0
#define SFR_OF      1
#define SFR_POR     2
#define SFR_RST     3
#define SFR_NMI     4
#define SFR_ACCV    5
#define SFR_URX0    6
#define SFR_UTX0    7
#define SFR_UCA0RX  8
#define SFR_UCA0TX  9
#define SFR_UCB0RX  10
#define SFR_UCB0TX  11
#define SFR_URX1    12
#define SFR_UTX1    13
#define SFR_BT      15
#define SFR_UCA1RX  16
#define SFR_UCA1TX  17
#define SFR_UCB1RX  18
#define SFR_UCB1TX  19

#define MSP430_SFR_IRQS     20

#define ME_URXE0    6
#define ME_UTXE0    7
#define ME_URXE0_12 8
#define ME_UTXE0_12 9
#define ME_URXE1    12
#define ME_UTXE1    13

#define MSP430_SFR_MES      14

#define TYPE_MSP430_SFR "msp430-sfr"
OBJECT_DECLARE_SIMPLE_TYPE(MSP430SFRState, MSP430_SFR)

struct MSP430SFRState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion memory;
    qemu_irq irq[MSP430_SFR_IRQS];
    qemu_irq me_irq[MSP430_SFR_MES];

    uint64_t irq_stats[MSP430_SFR_IRQS];

    uint32_t ie;
    uint32_t ifg;
    uint16_t me;
};

#endif /* MSP430_SFR_H */
