/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/gpio/msp430_port.h"
#include "hw/irq.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

REG8(IN, 0)
REG8(OUT, 1)
REG8(DIR, 2)
REG8(SEL, 3)
REG8(IFG, 3)
REG8(IES, 4)
REG8(IE, 5)
REG8(SEL_IRQ, 6)
REG8(REN, 7)

static void port_recalculate_irq(MSP430PortState *port, uint8_t old_irq)
{
    uint8_t new_irq = port->ifg & port->ie;

    if (old_irq && !new_irq) {
        qemu_irq_lower(port->irq);
    } else if (!old_irq && new_irq) {
        qemu_irq_raise(port->irq);
    }
}

static void port_recalculate_input(MSP430PortState *port)
{
    uint8_t old_in = port->in;
    uint8_t old_irq = port->ifg & port->ie;
    uint8_t unset, rising, falling;
    
    port->in = port->dir & port->out;
    unset = ~port->dir;
    port->in |= port->ext_driven & unset & port->ext_level;
    unset &= ~port->ext_driven;
    port->in |= port->ren & unset & port->out;
    unset &= ~port->ren;
    port->in |= unset & old_in;

    if (!port->has_irq) {
        return;
    }

    rising = port->in & ~old_in;
    falling = ~port->in & old_in;
    port->ifg |= (port->ies & falling) | (~port->ies & rising);

    port_recalculate_irq(port, old_irq);
}

static void port_set_input(void *opaque, int irq, int level)
{
    MSP430PortState *port = opaque;

    if (level < 0) {
        port->ext_driven &= ~BIT(irq);
    } else if (level) {
        port->ext_level |= BIT(irq);
    } else {
        port->ext_level &= ~BIT(irq);
    }

    port_recalculate_input(port);
}

static void port_set_out(MSP430PortState *port, uint8_t val)
{
    int i;

    for (i = 0; i < MSP430_PORT_GPIOS; i++) {
        if ((port->out ^ val) & port->dir & BIT(i)) {
            qemu_set_irq(port->output[i], !!(val & BIT(i)));
        }
    }

    port->out = val;
    port_recalculate_input(port);
}

static void port_set_dir(MSP430PortState *port, uint8_t val)
{
    int i;

    for (i = 0; i < MSP430_PORT_GPIOS; i++) {
        if ((port->dir ^ val) & BIT(i)) {
            if (val & BIT(i)) {
                qemu_set_irq(port->output[i], !!(port->dir & BIT(i)));
            } else {
                qemu_set_irq(port->output[i], -1);
            }
        }
    }

    port->dir = val;
    port_recalculate_input(port);
}

static uint64_t port_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430PortState *port = opaque;

    switch (addr) {
    case A_IN:
        return port->in;
    case A_OUT:
        return port->out;
    case A_DIR:
        return port->dir;
    case A_SEL:
        return port->sel;
    }

    g_assert_not_reached();
}

static void port_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430PortState *port = opaque;

    switch (addr) {
    case A_IN:
        qemu_log_mask(LOG_GUEST_ERROR, "msp430_port: IN is read-only.\n");
        return;
    case A_OUT:
        port_set_out(port, val);
        return;
    case A_DIR:
        port_set_dir(port, val);
        return;
    case A_SEL:
        qemu_log_mask(LOG_UNIMP, "msp430_port: SEL not implemented.\n");
        port->sel = val;
        return;
    }

    g_assert_not_reached();
}

static const MemoryRegionOps port_ops = {
    .write = port_write,
    .read  = port_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static uint64_t port_sel2_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430PortState *port = opaque;

    return port->sel2;
}

static void port_sel2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430PortState *port = opaque;

    qemu_log_mask(LOG_UNIMP, "msp430_port: SEL2 not implemented.\n");
    port->sel2 = val;
}

static const MemoryRegionOps port_sel2_ops = {
    .write = port_sel2_write,
    .read  = port_sel2_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static uint64_t port_ren_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430PortState *port = opaque;

    return port->ren;
}

static void port_ren_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430PortState *port = opaque;

    port->ren = val;
    port_recalculate_input(port);
}

static const MemoryRegionOps port_ren_ops = {
    .write = port_ren_write,
    .read  = port_ren_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static uint64_t port_irq_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430PortState *port = opaque;

    switch (addr) {
    case A_IN:
        return port->in;
    case A_OUT:
        return port->out;
    case A_DIR:
        return port->dir;
    case A_IFG:
        return port->ifg;
    case A_IES:
        return port->ies;
    case A_IE:
        return port->ie;
    case A_SEL_IRQ:
        return port->sel;
    case A_REN:
        return port->ren;
    }

    g_assert_not_reached();
}

static void port_irq_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430PortState *port = opaque;
    uint8_t old_irq = port->ifg & port->ie;

    switch (addr) {
    case A_IN:
        qemu_log_mask(LOG_GUEST_ERROR, "msp430_port: IN is read-only.\n");
        return;
    case A_OUT:
        port_set_out(port, val);
        return;
    case A_DIR:
        port_set_dir(port, val);
        return;
    case A_IFG:
        port->ifg = val;
        port_recalculate_irq(port, old_irq);
        return;
    case A_IES:
        port->ies = val;
        return;
    case A_IE:
        port->ie = val;
        port_recalculate_irq(port, old_irq);
        return;
    case A_SEL_IRQ:
        qemu_log_mask(LOG_UNIMP, "msp430_port: SEL not implemented.\n");
        port->sel = val;
        return;
    case A_REN:
        port->ren = val;
        port_recalculate_input(port);
        return;
    }

    g_assert_not_reached();
}

static const MemoryRegionOps port_irq_ops = {
    .write = port_irq_write,
    .read  = port_irq_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void port_reset_hold(Object *obj)
{
    MSP430PortState *port = MSP430_PORT(obj);
    int i;

    if (port->ifg & port->ie) {
        qemu_irq_lower(port->irq);
    }

    for (i = 0; i < MSP430_PORT_GPIOS; i++) {
        if (port->dir & BIT(i)) {
            qemu_set_irq(port->output[i], -1);
        }
    }

    port->dir = 0;
    port->ifg = 0;
    port->ie = 0;
    port->sel = 0;
    port->sel2 = 0;
    port->ren = 0;
}

static void port_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    MSP430PortState *port = MSP430_PORT(dev);

    if (port->has_irq) {
        memory_region_init_io(&port->iomem, OBJECT(port), &port_irq_ops, port,
                              "msp430-port", 8);
        sysbus_init_irq(d, &port->irq);
    } else {
        memory_region_init_io(&port->iomem, OBJECT(port), &port_ops, port,
                              "msp430-port", 4);
    }

    sysbus_init_mmio(d, &port->iomem);
    memory_region_init_io(&port->sel2mem, OBJECT(port), &port_sel2_ops, port,
                          "msp430-port-sel2", 1);
    sysbus_init_mmio(d, &port->sel2mem);

    if (!port->has_irq) {
        memory_region_init_io(&port->renmem, OBJECT(port), &port_ren_ops, port,
                              "msp430-port-ren", 1);
        sysbus_init_mmio(d, &port->renmem);
    }
}

static void port_init(Object *obj)
{
    MSP430PortState *port = MSP430_PORT(obj);

    qdev_init_gpio_in(DEVICE(port), port_set_input, MSP430_PORT_GPIOS);
    qdev_init_gpio_out(DEVICE(port), port->output, MSP430_PORT_GPIOS);
}

static const VMStateDescription vmstate_port = {
    .name = "msp430-port",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(in, MSP430PortState),
        VMSTATE_UINT8(out, MSP430PortState),
        VMSTATE_UINT8(dir, MSP430PortState),
        VMSTATE_UINT8(ifg, MSP430PortState),
        VMSTATE_UINT8(ies, MSP430PortState),
        VMSTATE_UINT8(ie, MSP430PortState),
        VMSTATE_UINT8(sel, MSP430PortState),
        VMSTATE_UINT8(sel2, MSP430PortState),
        VMSTATE_UINT8(ren, MSP430PortState),
        VMSTATE_UINT8(ext_level, MSP430PortState),
        VMSTATE_UINT8(ext_driven, MSP430PortState),
        VMSTATE_END_OF_LIST()
    }
};

static Property port_properties[] = {
    DEFINE_PROP_BOOL("has_irq", MSP430PortState, has_irq, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void port_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "MSP430 digital I/O port";
    dc->realize = port_realize;
    dc->vmsd = &vmstate_port;
    device_class_set_props(dc, port_properties);
    rc->phases.hold = port_reset_hold;
}

static void port16_forward(void *opaque, int irq, int level)
{
    MSP430Port16State *port16 = opaque;

    qemu_set_irq(port16->in[irq], level);
}

static uint64_t port16_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430Port16State *port16 = opaque;

    return port_read(&port16->port[addr & 1], addr >> 1, size);
}

static void port16_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430Port16State *port16 = opaque;

    port_write(&port16->port[addr & 1], addr >> 1, val, size);
}

static const MemoryRegionOps port16_ops = {
    .write = port16_write,
    .read  = port16_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static uint64_t port16_sel2_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430Port16State *port16 = opaque;

    return port_sel2_read(&port16->port[!!addr], 0, size);
}

static void port16_sel2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430Port16State *port16 = opaque;

    port_sel2_write(&port16->port[!!addr], 0, val, size);
}

static const MemoryRegionOps port16_sel2_ops = {
    .write = port16_sel2_write,
    .read  = port16_sel2_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static uint64_t port16_ren_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430Port16State *port16 = opaque;

    return port_ren_read(&port16->port[!!addr], 0, size);
}

static void port16_ren_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430Port16State *port16 = opaque;

    port_ren_write(&port16->port[!!addr], 0, val, size);
}

static const MemoryRegionOps port16_ren_ops = {
    .write = port16_ren_write,
    .read  = port16_ren_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void port16_realize(DeviceState *dev, Error **errp)
{
    MSP430Port16State *port16 = MSP430_PORT16(dev);
    int i;

    for (i = 0; i < ARRAY_SIZE(port16->port); i++) {
        object_property_set_bool(OBJECT(&port16->port[i]), "has_irq", false,
                                 errp);
        sysbus_realize(SYS_BUS_DEVICE(&port16->port[i]), errp);
    }
}

static void port16_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MSP430Port16State *port16 = MSP430_PORT16(obj);
    int i;

    object_initialize_child(obj, "even", &port16->port[0], TYPE_MSP430_PORT);
    object_initialize_child(obj, "odd", &port16->port[1], TYPE_MSP430_PORT);

    memory_region_init_io(&port16->iomem, OBJECT(port16), &port16_ops, port16,
                          "msp430-port16", 8);
    sysbus_init_mmio(d, &port16->iomem);
    memory_region_init_io(&port16->sel2mem, OBJECT(port16), &port16_sel2_ops, port16,
                          "msp430-port16-sel2", 2);
    sysbus_init_mmio(d, &port16->sel2mem);
    memory_region_init_io(&port16->renmem, OBJECT(port16), &port16_ren_ops, port16,
                          "msp430-port16-ren", 2);
    sysbus_init_mmio(d, &port16->renmem);

    qdev_init_gpio_in(DEVICE(port16), port16_forward, MSP430_PORT16_GPIOS);
    qdev_init_gpio_out(DEVICE(port16), port16->out, MSP430_PORT16_GPIOS);

    for (i = 0; i < ARRAY_SIZE(port16->in); i++) {
        port16->in[i] =
            qdev_get_gpio_in(DEVICE(&port16->port[i / MSP430_PORT_GPIOS]), i);
        qdev_connect_gpio_out(DEVICE(&port16->port[i / MSP430_PORT_GPIOS]),
                              i, port16->out[i]);
    }
}

static void port16_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = "Two interleaved MSP430 digital I/O ports";
    dc->realize = port16_realize;
}

static const TypeInfo port_types[] = {
    {
        .parent = TYPE_SYS_BUS_DEVICE,
        .name = TYPE_MSP430_PORT,
        .instance_size = sizeof(MSP430PortState),
        .instance_init = port_init,
        .class_init = port_class_init,
    },
    {
        .parent = TYPE_SYS_BUS_DEVICE,
        .name = TYPE_MSP430_PORT16,
        .instance_size = sizeof(MSP430Port16State),
        .instance_init = port16_init,
        .class_init = port16_class_init,
    },
};

DEFINE_TYPES(port_types)
