/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_PORT_H
#define MSP430_PORT_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_MSP430_PORT "msp430-port"
#define TYPE_MSP430_PORT16 "msp430-port16"
OBJECT_DECLARE_SIMPLE_TYPE(MSP430PortState, MSP430_PORT)
OBJECT_DECLARE_SIMPLE_TYPE(MSP430Port16State, MSP430_PORT16)

#define MSP430_PORT_GPIOS   8
#define MSP430_PORT16_GPIOS (2 * MSP430_PORT_GPIOS)

struct MSP430PortState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem, sel2mem, renmem;
    qemu_irq irq, output[MSP430_PORT_GPIOS];

    uint32_t reg_shift;

    uint8_t in, out, dir, ifg, ies, ie, sel, sel2, ren;

    uint8_t ext_level, ext_driven;
    bool has_irq, has_sel2, has_ren;
};

struct MSP430Port16State {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MSP430PortState port[2];
    MemoryRegion iomem, renmem, sel2mem;
    qemu_irq in[MSP430_PORT16_GPIOS], out[MSP430_PORT16_GPIOS];

    bool has_sel2, has_ren;
};

#endif /* MSP430_PORT_H */
