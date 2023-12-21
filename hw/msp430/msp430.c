/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "exec/address-spaces.h"
#include "hw/loader.h"
#include "hw/misc/unimp.h"
#include "hw/qdev-clock.h"
#include "msp430.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"

enum clock_type {
    CLOCK_BCMP,
    CLOCK_FLLP,
};

typedef struct {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/

    uint32_t sfr_map[NUM_IRQS];

    size_t flash_size;
    size_t sram_size;
    size_t bsl_size;

    const char *timer_type[2];
    int timer_count[2];
    int timer_irq[2];

    enum clock_type clock_type;
    union {
        bool bcmp_has_xts;
        struct {
            bool fllp_has_xts, fllp_has_sel, fllp_has_vlo;
        };
    };

    bool port[7], port16[2];
    int port_irq[2];
    bool ports_have_sel2, ports_have_ren;

    bool usci_a[2], usci_b[2];
    bool usart[2];
} MSP430Class;

DECLARE_CLASS_CHECKERS(MSP430Class, MSP430_MCU, TYPE_MSP430_MCU)

#if 0
static void msp430_forward(void *opaque, int irq, int level)
{
    MSP430State *s = opaque;

    qemu_set_irq(s->in[irq], level);
}
#endif

static const uint8_t tlv[] = {
    0xa0, 0xcf, /* checksum */
    0xfe, 0x32, /* empty tag, length */
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0x01, 0x08, /* DCO tag, length */
    0x80, 0x0f, /* 16 MHz */
    0x00, 0x0f, /* 12 MHz */
    0xe0, 0x0c, /* 8 MHz */
    0xc0, 0x06, /* 1 MHz */
};

static void msp430_forward_gpios(MSP430State *s, DeviceState *port, int base,
                                 int count)
{
#if 0
    int i;

    qdev_init_gpio_in(DEVICE(s), msp430_forward, count);
    qdev_init_gpio_out(DEVICE(s), &s->out[base], count);

    assert(base + count < ARRAY_SIZE(s->in));
    for (i = 0; i < count; i++) {
        s->in[base + i] = qdev_get_gpio_in(port, i);
        qdev_connect_gpio_out(port, i, s->out[base + i]);
    }
#endif
}

struct port_addrs {
    hwaddr io, sel2, ren;
};

struct port_addrs port_addrs[] = {
    { 0x20, 0x41 },
    { 0x28, 0x42 },
    { 0x18, 0x43, 0x10 },
    { 0x1c, 0x44, 0x11 },
    { 0x30, 0x45, 0x12 },
    { 0x34, 0x46, 0x13 },
};

static void msp430_realize_port(MSP430State *s, int i, Error **errp)
{
    const MSP430Class *mc = MSP430_MCU_GET_CLASS(s);
    SysBusDevice *port = SYS_BUS_DEVICE(&s->port[i]);

    if (i < 2) {
        object_property_set_bool(OBJECT(port), "has_irq", true, errp);
    }
    msp430_forward_gpios(s, DEVICE(port), i * MSP430_PORT_GPIOS,
                         MSP430_PORT_GPIOS);

    sysbus_realize(port, errp);
    if (i < 2) {
        sysbus_connect_irq(port, 0, qdev_get_gpio_in(DEVICE(&s->cpu),
                                                     mc->port_irq[i]));
    }

    sysbus_mmio_map(port, 0, port_addrs[i].io);
    if (mc->ports_have_sel2) {
        sysbus_mmio_map(port, 1, port_addrs[i].sel2);
    }
    if (i >= 2 && mc->ports_have_ren) {
        sysbus_mmio_map(port, 2, port_addrs[i].ren);
    }
}

struct port_addrs port16_addrs[] = {
    { 0x38, 0x47, 0x14 },
    { 0x08, 0x49, 0x16 },
};

static void msp430_realize_port16(MSP430State *s, int i, Error **errp)
{
    const MSP430Class *mc = MSP430_MCU_GET_CLASS(s);
    SysBusDevice *port = SYS_BUS_DEVICE(&s->port16[i]);

    sysbus_mmio_map(port, 0, port16_addrs[i].io);
    if (mc->ports_have_sel2) {
        sysbus_mmio_map(port, 1, port16_addrs[i].sel2);
    }
    if (i >= 2 && mc->ports_have_ren) {
        sysbus_mmio_map(port, 2, port16_addrs[i].ren);
    }

    msp430_forward_gpios(s, DEVICE(port), ARRAY_SIZE(s->port) * MSP430_PORT_GPIOS +
                         i * MSP430_PORT16_GPIOS, MSP430_PORT16_GPIOS);
    sysbus_realize(port, errp);
}

static void msp430_realize_timer(MSP430State *s, int i, Error **errp)
{
    const MSP430Class *mc = MSP430_MCU_GET_CLASS(s);
    SysBusDevice *timer = SYS_BUS_DEVICE(&s->timer[i]);

    object_property_set_uint(OBJECT(timer), "timers", mc->timer_count[i],
                             &error_fatal);
    if (i) {
        sysbus_mmio_map(timer, 0, 0x180);
        sysbus_mmio_map(timer, 1, 0x11e);
    } else {
        sysbus_mmio_map(timer, 0, 0x160);
        sysbus_mmio_map(timer, 1, 0x12e);
    }

    qdev_connect_clock_in(DEVICE(timer), "aclk", s->aclk);
    qdev_connect_clock_in(DEVICE(timer), "smclk", s->smclk);
    sysbus_connect_irq(timer, 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu), mc->timer_irq[i]));
    sysbus_connect_irq(timer, 1,
                       qdev_get_gpio_in(DEVICE(&s->cpu), mc->timer_irq[i] - 1));
    qdev_connect_gpio_out_named(DEVICE(&s->cpu), "ack", mc->timer_irq[i] - 1,
                                qdev_get_gpio_in_named(DEVICE(timer), "ack", 0));
    sysbus_realize(timer, errp);
}


static void msp430_realize_usci(MSP430State *s, SysBusDevice *usci, int rx_sfr,
                                int tx_sfr, Error **errp)
{
    DeviceState *sfr = DEVICE(&s->sfr);
    qemu_irq clear_rx, clear_tx;

    sysbus_connect_irq(usci, 0, qdev_get_gpio_in(sfr, rx_sfr));
    sysbus_connect_irq(usci, 1, qdev_get_gpio_in(sfr, tx_sfr));
    clear_rx = qdev_get_gpio_in_named(sfr, "clear", rx_sfr);
    clear_tx = qdev_get_gpio_in_named(sfr, "clear", tx_sfr);
    qdev_connect_gpio_out_named(DEVICE(usci), "clear_rx", 0, clear_rx);
    qdev_connect_gpio_out_named(DEVICE(usci), "clear_tx", 0, clear_tx);
    qdev_connect_clock_in(DEVICE(usci), "aclk", s->aclk);
    qdev_connect_clock_in(DEVICE(usci), "smclk", s->smclk);
    sysbus_realize(usci, errp);
}

static void msp430_realize_usci_a(MSP430State *s, int i, Error **errp)
{
    SysBusDevice *usci = SYS_BUS_DEVICE(&s->usci_a[i]);
    int rx_sfr, tx_sfr;

    qdev_prop_set_chr(DEVICE(usci), "chardev", serial_hd(i));
    if (i) {
        sysbus_mmio_map(usci, 0, 0xcd);
        rx_sfr = SFR_UCA1RX;
        tx_sfr = SFR_UCA1TX;
    } else {
        sysbus_mmio_map(usci, 0, 0x5d);
        rx_sfr = SFR_UCA0RX;
        tx_sfr = SFR_UCA0TX;
    }

    msp430_realize_usci(s, usci, rx_sfr, tx_sfr, errp);
}

static void msp430_realize_usci_b(MSP430State *s, int i, Error **errp)
{
    SysBusDevice *usci = SYS_BUS_DEVICE(&s->usci_b[i]);
    int rx_sfr, tx_sfr;

    if (i) {
        sysbus_mmio_map(usci, 0, 0xd8);
        sysbus_mmio_map(usci, 1, 0x17c);
        rx_sfr = SFR_UCB1RX;
        tx_sfr = SFR_UCB1TX;
    } else {
        sysbus_mmio_map(usci, 0, 0x68);
        sysbus_mmio_map(usci, 1, 0x118);
        rx_sfr = SFR_UCB0RX;
        tx_sfr = SFR_UCB0TX;
    }

    msp430_realize_usci(s, usci, rx_sfr, tx_sfr, errp);
}

static void msp430_realize(DeviceState *dev, Error **errp)
{
    MSP430State *s = MSP430_MCU(dev);
    const MSP430Class *mc = MSP430_MCU_GET_CLASS(dev);
    DeviceState *cpu = DEVICE(&s->cpu);
    DeviceState *sfr = DEVICE(&s->sfr);
    DeviceState *nmi_ack = DEVICE(&s->nmi_ack);
    DeviceState *reset_ack = DEVICE(&s->reset_ack);
    DeviceState *clock;
    int i;

    /* CPU */
    qdev_realize(cpu, NULL, &error_fatal);

    /* SRAM */
    memory_region_init_ram(&s->sram, OBJECT(dev), "sram", mc->sram_size,
                           &error_fatal);
    if (mc->sram_size > 0x800) {
        memory_region_add_subregion(get_system_memory(), 0x1100, &s->sram);
        memory_region_init_alias(&s->mirror, OBJECT(dev), "mirror", &s->sram,
                                 0, 0x800);
        memory_region_add_subregion(&s->sram, 0x200, &s->mirror);
    } else {
        memory_region_add_subregion(get_system_memory(), 0x200, &s->sram);
    }

    /* Flash */
    memory_region_init_rom(&s->flash, OBJECT(dev), "flash", mc->flash_size,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x10000 - mc->flash_size,
                                &s->flash);

    memory_region_init_rom(&s->info, OBJECT(dev), "info", 0x100, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1000, &s->info);

    /* ROM */
    if (mc->bsl_size) {
        memory_region_init_rom(&s->bsl, OBJECT(dev), "bsl", mc->bsl_size,
                               &error_fatal);
        memory_region_add_subregion(get_system_memory(), 0xc00, &s->bsl);
    }

    /*
     * I/O
     *
     * 0x000 - 0x00f: SFRs
     * 0x010 - 0x0ff: 8-bit I/O
     * 0x100 - 0x1ff: 16-bit I/O
     */
    object_property_set_int(OBJECT(reset_ack), "num-lines", 2, &error_fatal);
    qdev_connect_gpio_out_named(cpu, "ack", IRQ_RESET, qdev_get_gpio_in(reset_ack, 0));
    qdev_realize(reset_ack, NULL, &error_fatal);
    qdev_connect_gpio_out(reset_ack, 0, qdev_get_gpio_in_named(sfr, "ack",
                                                               SFR_POR));
    qdev_connect_gpio_out(reset_ack, 1, qdev_get_gpio_in_named(sfr, "ack",
                                                               SFR_RST));

    object_property_set_int(OBJECT(nmi_ack), "num-lines", 3, &error_fatal);
    qdev_connect_gpio_out_named(cpu, "ack", IRQ_NMI, qdev_get_gpio_in(nmi_ack, 0));
    qdev_realize(nmi_ack, NULL, &error_fatal);
    qdev_connect_gpio_out(nmi_ack, 0, qdev_get_gpio_in_named(sfr, "clear",
                                                             SFR_OF));
    qdev_connect_gpio_out(nmi_ack, 1, qdev_get_gpio_in_named(sfr, "clear",
                                                             SFR_NMI));
    qdev_connect_gpio_out(nmi_ack, 2, qdev_get_gpio_in_named(sfr, "clear",
                                                            SFR_ACCV));

    for (i = 0; i < NUM_IRQS; i++) {
        DeviceState *irq;
        int n = ctpop32(mc->sfr_map[i]);
        int j, k = n;

        if (!n) {
            continue;
        }

        irq = DEVICE(&s->cpu_irq[i]);
        object_property_set_int(OBJECT(irq), "num-lines", n, &error_fatal);
        qdev_connect_gpio_out(irq, 0, qdev_get_gpio_in(DEVICE(&s->cpu), i));
        qdev_realize(irq, NULL, &error_fatal);

        for (j = 0; j < MSP430_SFR_IRQS; j++) {
            if (mc->sfr_map[i] & BIT(j)) {
                sysbus_connect_irq(SYS_BUS_DEVICE(sfr), j, qdev_get_gpio_in(irq, --k));
                if (!k) {
                    break;
                }
            }
        }

        if (n == 1) {
            qdev_connect_gpio_out_named(cpu, "ack", i,
                                        qdev_get_gpio_in_named(sfr, "ack", j));
        }
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(sfr), 0, 0);
    sysbus_realize(SYS_BUS_DEVICE(sfr), &error_fatal);

    switch (mc->clock_type) {
    case CLOCK_BCMP:
        clock = DEVICE(&s->bcmp);
        object_property_set_bool(OBJECT(clock), "has_xts", mc->bcmp_has_xts,
                                 &error_fatal);

        qdev_connect_gpio_out_named(cpu, "cpuoff", 0,
                                    qdev_get_gpio_in_named(clock, "cpuoff", 0));
        qdev_connect_gpio_out_named(cpu, "scg", 1,
                                    qdev_get_gpio_in_named(clock, "scg1", 0));

        /* Not present on FLL+ systems AFAICT */
        rom_add_blob_fixed("tlv", tlv, sizeof(tlv), 0x10c0);
        break;
    case CLOCK_FLLP:
        clock = DEVICE(&s->fllp);
        object_property_set_bool(OBJECT(clock), "has_xts", mc->fllp_has_xts,
                                 &error_fatal);
        object_property_set_bool(OBJECT(clock), "has_sel", mc->fllp_has_sel,
                                 &error_fatal);
        object_property_set_bool(OBJECT(clock), "has_vlo", mc->fllp_has_vlo,
                                 &error_fatal);

        qdev_connect_gpio_out_named(cpu, "cpuoff", 0,
                                    qdev_get_gpio_in_named(clock, "cpuoff", 0));
        break;
    default:
        g_assert_not_reached();
    }

    sysbus_connect_irq(SYS_BUS_DEVICE(clock), 0, qdev_get_gpio_in(DEVICE(sfr),
                                                                  SFR_OF));
    if (clock_has_source(s->xt1)) {
        qdev_connect_clock_in(clock, "xt1", s->xt1);
    }
    if (clock_has_source(s->xt2)) {
        qdev_connect_clock_in(clock, "xt2", s->xt2);
    }
    s->aclk = qdev_get_clock_out(clock, "aclk");
    s->smclk = qdev_get_clock_out(clock, "smclk");
    s->mclk = qdev_get_clock_out(clock, "mclk");
    sysbus_mmio_map(SYS_BUS_DEVICE(clock), 0, 0x50);
    sysbus_realize(SYS_BUS_DEVICE(clock), &error_fatal);

    for (i = 0; i < ARRAY_SIZE(mc->port); i++) {
        if (mc->port[i]) {
            msp430_realize_port(s, i, &error_fatal);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->port16); i++) {
        if (mc->port16[i]) {
            msp430_realize_port16(s, i, &error_fatal);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->timer_type); i++) {
        if (mc->timer_type[i]) {
            msp430_realize_timer(s, i, &error_fatal);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->usci_a); i++) {
        if (mc->usci_a[i]) {
            msp430_realize_usci_a(s, i, &error_fatal);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->usci_b); i++) {
        if (mc->usci_b[i]) {
            msp430_realize_usci_b(s, i, &error_fatal);
        }
    }
}

static void msp430_init(Object *obj)
{
    MSP430State *s = MSP430_MCU(obj);
    const MSP430Class *mc = MSP430_MCU_GET_CLASS(obj);
    int i;

    object_initialize_child(obj, "cpu", &s->cpu, TYPE_MSP430_CPU);

    create_unimplemented_device("msp430-io", 0, 0x200);

    object_initialize_child(obj, "sfr", &s->sfr, TYPE_MSP430_SFR);
    object_initialize_child(obj, "reset-ack", &s->reset_ack, TYPE_SPLIT_IRQ);
    object_initialize_child(obj, "nmi-ack", &s->nmi_ack, TYPE_SPLIT_IRQ);
    for (i = 0; i < NUM_IRQS; i++) {
        g_autofree char *name = g_strdup_printf("cpu-irq%d", i);

        if (mc->sfr_map[i]) {
            object_initialize_child(obj, name, &s->cpu_irq[i], TYPE_OR_IRQ);
        }
    }

    switch (mc->clock_type) {
    case CLOCK_BCMP:
        object_initialize_child(obj, "bcm+", &s->bcmp, TYPE_MSP430_BCMP);
        break;
    case CLOCK_FLLP:
        object_initialize_child(obj, "fll+", &s->fllp, TYPE_MSP430_FLLP);
        break;
    default:
        g_assert_not_reached();
    }
    s->xt1 = qdev_init_clock_in(DEVICE(obj), "xt1", NULL, NULL, 0);
    s->xt2 = qdev_init_clock_in(DEVICE(obj), "xt2", NULL, NULL, 0);

    for (i = 0; i < ARRAY_SIZE(mc->port); i++) {
        if (mc->port[i]) {
            g_autofree char *name = g_strdup_printf("port%d", i);

            object_initialize_child(obj, name, &s->port[i], TYPE_MSP430_PORT);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->port16); i++) {
        if (mc->port16[i]) {
            g_autofree char *name = g_strdup_printf("port%c", 'A' + i);

            object_initialize_child(obj, name, &s->port16[i], TYPE_MSP430_PORT16);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->timer_type); i++) {
        if (mc->timer_type[i]) {
            g_autofree char *name = g_strdup_printf("timer%d", i);

            object_initialize_child(obj, name, &s->timer[i], mc->timer_type[i]);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->usci_a); i++) {
        if (mc->usci_a[i]) {
            g_autofree char *name = g_strdup_printf("usci-a%d", i);

            object_initialize_child(obj, name, &s->usci_a[i], TYPE_MSP430_USCI_A);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->usci_b); i++) {
        if (mc->usci_b[i]) {
            g_autofree char *name = g_strdup_printf("usci-b%d", i);

            object_initialize_child(obj, name, &s->usci_b[i], TYPE_MSP430_USCI_B);
        }
    }
}

static void msp430_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = msp430_realize;
}

static const uint32_t msp430x2xx_sfr_map[NUM_IRQS] = {
    [IRQ_NMI] = BIT(SFR_OF) | BIT(SFR_NMI) | BIT(SFR_ACCV),
    [IRQ_RESET] = BIT(SFR_POR) | BIT(SFR_RST),
    [IRQ_WDT] = BIT(SFR_WDT),
    [NUM_IRQS - 9] = BIT(SFR_UCA0RX) | BIT(SFR_UCB0RX),
    [NUM_IRQS - 10] = BIT(SFR_UCA0TX) | BIT(SFR_UCB0TX),
    [NUM_IRQS - 15] = BIT(SFR_UCA1RX) | BIT(SFR_UCB1RX),
    [NUM_IRQS - 16] = BIT(SFR_UCA1RX) | BIT(SFR_UCB1RX),
};

static void msp430x2xx_class_init(ObjectClass *oc, void *data)
{
    MSP430Class *mc = MSP430_MCU_CLASS(oc);

    memcpy(mc->sfr_map, msp430x2xx_sfr_map, sizeof(msp430x2xx_sfr_map));
    mc->clock_type = CLOCK_BCMP;
    mc->ports_have_sel2 = true;
    mc->ports_have_ren = true;
    mc->port_irq[0] = NUM_IRQS - 14;
    mc->port_irq[1] = NUM_IRQS - 13;
    mc->timer_irq[0] = NUM_IRQS - 7;
    mc->timer_irq[1] = NUM_IRQS - 3;
}

static void msp430g2553_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MSP430Class *mc = MSP430_MCU_CLASS(oc);
    int i;

    dc->desc = "MSP430G2553 mixed signal microcontroller";
    mc->flash_size = 16 * KiB;
    mc->sram_size = 512;
    mc->bsl_size = KiB;
    for (i = 0; i < 6; i++) {
        mc->port[i] = true;
    }
    mc->timer_type[0] = TYPE_MSP430_TIMER_A;
    mc->timer_count[0] = 3;
    mc->timer_type[1] = TYPE_MSP430_TIMER_A;
    mc->timer_count[1] = 3;
    mc->usci_a[0] = true;
    mc->usci_b[0] = true;
}

static const uint32_t msp430x4xx_sfr_map[NUM_IRQS] = {
    [IRQ_NMI] = BIT(SFR_OF) | BIT(SFR_NMI) | BIT(SFR_ACCV),
    [IRQ_RESET] = BIT(SFR_POR) | BIT(SFR_RST),
    [IRQ_WDT] = BIT(SFR_WDT),
    [NUM_IRQS - 7] = BIT(SFR_UCA0RX) | BIT(SFR_UCB0RX),
    [NUM_IRQS - 8] = BIT(SFR_UCA0TX) | BIT(SFR_UCB0TX),
    [NUM_IRQS - 13] = BIT(SFR_UCA1RX) | BIT(SFR_UCB1RX),
    [NUM_IRQS - 14] = BIT(SFR_UCA1RX) | BIT(SFR_UCB1RX),
    [NUM_IRQS - 16] = BIT(SFR_BT),
};

static void msp430x4xx_class_init(ObjectClass *oc, void *data)
{
    MSP430Class *mc = MSP430_MCU_CLASS(oc);

    memcpy(mc->sfr_map, msp430x4xx_sfr_map, sizeof(msp430x4xx_sfr_map));
    mc->clock_type = CLOCK_FLLP;
    mc->ports_have_ren = true;
    mc->port_irq[0] = NUM_IRQS - 12;
    mc->port_irq[1] = NUM_IRQS - 15;
    mc->timer_irq[0] = NUM_IRQS - 10;
    mc->timer_irq[1] = NUM_IRQS - 3;
}

static void msp430f449_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MSP430Class *mc = MSP430_MCU_CLASS(oc);
    int i;

    dc->desc = "MSP430F449 mixed signal microcontroller";
    mc->flash_size = 60 * KiB;
    mc->sram_size = 2 * KiB;
    mc->bsl_size = KiB;
    mc->fllp_has_xts = true;
    mc->fllp_has_sel = true;
    mc->ports_have_ren = false;
    for (i = 0; i < 6; i++) {
        mc->port[i] = true;
    }
    mc->timer_type[0] = TYPE_MSP430_TIMER_A;
    mc->timer_count[0] = 3;
    mc->timer_type[1] = TYPE_MSP430_TIMER_B;
    mc->timer_count[1] = 7;
}

static const TypeInfo msp430_mcu_types[] = {
    {
        .name           = TYPE_MSP430_MCU,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(MSP430State),
        .instance_init  = msp430_init,
        .class_size     = sizeof(MSP430Class),
        .class_init     = msp430_class_init,
        .abstract       = true,
    },
    {
        .name           = TYPE_MSP430X2XX_MCU,
        .parent         = TYPE_MSP430_MCU,
        .class_init     = msp430x2xx_class_init,
        .abstract       = true,
    },
    {
        .name           = TYPE_MSP430G2553_MCU,
        .parent         = TYPE_MSP430X2XX_MCU,
        .class_init     = msp430g2553_class_init,
    },
    {
        .name           = TYPE_MSP430X4XX_MCU,
        .parent         = TYPE_MSP430_MCU,
        .class_init     = msp430x4xx_class_init,
        .abstract       = true,
    },
    {
        .name           = TYPE_MSP430F449_MCU,
        .parent         = TYPE_MSP430X4XX_MCU,
        .class_init     = msp430f449_class_init,
    },
};

DEFINE_TYPES(msp430_mcu_types)
