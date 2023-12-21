/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "hw/loader.h"
#include "hw/misc/unimp.h"
#include "hw/qdev-clock.h"
#include "msp430.h"
#include "qapi/error.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"

void msp430_load_kernel(MSP430State *s, const char *filename)
{
    CPUState *cs = CPU(&s->cpu);

    if (load_elf_as(filename, NULL, NULL, NULL, NULL, NULL, NULL, NULL, false,
                    EM_MSP430, 0, 0, cs->as) >= 0) {
        return;
    }

    if (load_targphys_hex_as(filename, NULL, cs->as) >= 0) {
        return;
    }

    if (load_image_mr(filename, &s->flash) >= 0) {
        return;
    }

    error_report("Failed to load '%s'", filename);
    exit(1);
}

void msp430_load_bsl(MSP430State *s, const char *filename)
{
    char *file = qemu_find_file(QEMU_FILE_TYPE_BIOS, filename);

    if (!file) {
        error_report("Unable to find '%s'", filename);
        exit(1);
    }

    if (load_image_mr(file, &s->bsl) >= 0) {
        return;
    }

    error_report("Failed to load '%s'", filename);
    exit(1);
}

enum clock_type {
    CLOCK_BCMP,
    CLOCK_FLLP,
};

struct timer_config {
    hwaddr io, iv;
    const char *type;
    int timers, irq;
};

static const struct timer_config timer_configs[] = {
    { 0x160, 0x12e },
    { 0x180, 0x11e },
};

struct port_config {
    hwaddr io, sel2, ren;
    int irq;
    bool present, has_irq;
};

static const struct port_config port_configs[] = {
    { 0x20, 0x41, .has_irq = true },
    { 0x28, 0x42, .has_irq = true },
    { 0x18, 0x43, 0x10 },
    { 0x1c, 0x44, 0x11 },
    { 0x30, 0x45, 0x12 },
    { 0x34, 0x46, 0x13 },
};

static const struct port_config port16_configs[] = {
    { 0x38, 0x47, 0x14 },
    { 0x08, 0x49, 0x16 },
};

struct usci_config {
    hwaddr io, i2c;
    int rx_sfr, tx_sfr;
    bool present;
};

static const struct usci_config usci_a_configs[] = {
    { 0x5d, 0, SFR_UCA0RX, SFR_UCA0TX },
    { 0xcd, 0, SFR_UCA1RX, SFR_UCA1TX },
};

static const struct usci_config usci_b_configs[] = {
    { 0x68, 0x118, SFR_UCB0RX, SFR_UCB0TX },
    { 0xd8, 0x17c, SFR_UCB1RX, SFR_UCB1TX },
};

struct usart_config {
    hwaddr io, i2c0, i2c1;
    int rx_sfr, tx_sfr, rxe, txe, irq;
    bool i2c, present;
};

static const struct usart_config usart_configs[] = {
    { 0x70, 0x118, 0x50, SFR_URX0, SFR_UTX0, ME_URXE0, ME_UTXE0 },
    { 0x78, 0, 0, SFR_URX1, SFR_UTX1, ME_URXE1, ME_UTXE1 },
};

typedef struct {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/

    size_t flash_size;
    size_t sram_size;
    size_t bsl_size;

    const char *mpy_type;
    struct timer_config timer[2];
    struct port_config port[6], port16[2];
    struct usci_config usci_a[2], usci_b[2];
    struct usart_config usart[2];
    int uart_irq[2];
    uint32_t sfr_map[NUM_IRQS];

    enum clock_type clock_type;
    union {
        bool bcmp_has_xts;
        struct {
            bool fllp_has_xts, fllp_has_sel, fllp_has_vlo;
        };
    };

    bool ports_have_sel2, ports_have_ren;
    bool has_bt, has_lcd;
} MSP430Class;

DECLARE_CLASS_CHECKERS(MSP430Class, MSP430_MCU, TYPE_MSP430_MCU)

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

static void msp430_realize_port(MSP430State *s, SysBusDevice *port,
                                const struct port_config *config,
                                Error **errp)
{
    const MSP430Class *mc = MSP430_MCU_GET_CLASS(s);

    object_property_set_bool(OBJECT(port), "has_irq", config->has_irq, errp);
    sysbus_realize(port, errp);

    sysbus_mmio_map(port, 0, config->io);
    if (mc->ports_have_sel2) {
        sysbus_mmio_map(port, 1, config->sel2);
    }
    if (mc->ports_have_ren && config->ren) {
        sysbus_mmio_map(port, 2, config->ren);
    }
    if (config->has_irq) {
        sysbus_connect_irq(port, 0, qdev_get_gpio_in(DEVICE(&s->cpu),
                           config->irq));
    }
}

static void msp430_realize_port16(MSP430State *s, SysBusDevice *port,
                                  const struct port_config *config,
                                  Error **errp)
{
    const MSP430Class *mc = MSP430_MCU_GET_CLASS(s);

    sysbus_mmio_map(port, 0, config->io);
    if (mc->ports_have_sel2) {
        sysbus_mmio_map(port, 1, config->sel2);
    }
    if (mc->ports_have_ren) {
        sysbus_mmio_map(port, 2, config->ren);
    }

    sysbus_realize(port, errp);
}

static void msp430_realize_timer(MSP430State *s, SysBusDevice *timer,
                                 const struct timer_config *config,
                                 Error **errp)
{
    qemu_irq irq[2], ack;

    object_property_set_uint(OBJECT(timer), "timers", config->timers,
                             &error_fatal);
    sysbus_mmio_map(timer, 0, config->io);
    sysbus_mmio_map(timer, 1, config->iv);

    qdev_connect_clock_in(DEVICE(timer), "aclk", s->aclk);
    qdev_connect_clock_in(DEVICE(timer), "smclk", s->smclk);
    irq[0] = qdev_get_gpio_in(DEVICE(&s->cpu), config->irq);
    irq[1] = qdev_get_gpio_in(DEVICE(&s->cpu), config->irq - 1);
    ack = qdev_get_gpio_in_named(DEVICE(timer), "ack", 0);
    sysbus_connect_irq(timer, 0, irq[0]);
    sysbus_connect_irq(timer, 1, irq[1]);
    qdev_connect_gpio_out_named(DEVICE(&s->cpu), "ack", config->irq, ack);

    sysbus_realize(timer, errp);
}

static void msp430_realize_usci(MSP430State *s, SysBusDevice *usci,
                                const struct usci_config *config,
                                Error **errp)
{
    DeviceState *sfr = DEVICE(&s->sfr);
    qemu_irq clear_rx, clear_tx;

    sysbus_mmio_map(usci, 0, config->io);
    if (config->i2c) {
        sysbus_mmio_map(usci, 1, config->i2c);
    }
    sysbus_connect_irq(usci, 0, qdev_get_gpio_in(sfr, config->rx_sfr));
    sysbus_connect_irq(usci, 1, qdev_get_gpio_in(sfr, config->tx_sfr));
    clear_rx = qdev_get_gpio_in_named(sfr, "clear", config->rx_sfr);
    clear_tx = qdev_get_gpio_in_named(sfr, "clear", config->tx_sfr);
    qdev_connect_gpio_out_named(DEVICE(usci), "clear_rx", 0, clear_rx);
    qdev_connect_gpio_out_named(DEVICE(usci), "clear_tx", 0, clear_tx);
    qdev_connect_clock_in(DEVICE(usci), "aclk", s->aclk);
    qdev_connect_clock_in(DEVICE(usci), "smclk", s->smclk);

    sysbus_realize(usci, errp);
}

static void msp430_realize_usart(MSP430State *s, SysBusDevice *usart,
                                 const struct usart_config *config,
                                 Error **errp)
{
    DeviceState *sfr = DEVICE(&s->sfr);
    qemu_irq clear_rx, clear_tx, rx_ack, tx_ack, rxe, txe;

    sysbus_mmio_map(usart, 0, config->io);

    sysbus_connect_irq(usart, 0, qdev_get_gpio_in(sfr, config->rx_sfr));
    sysbus_connect_irq(usart, 1, qdev_get_gpio_in(sfr, config->tx_sfr));
    clear_rx = qdev_get_gpio_in_named(sfr, "clear", config->rx_sfr);
    clear_tx = qdev_get_gpio_in_named(sfr, "clear", config->tx_sfr);
    qdev_connect_gpio_out_named(DEVICE(usart), "clear_rx", 0, clear_rx);
    qdev_connect_gpio_out_named(DEVICE(usart), "clear_tx", 0, clear_tx);
    rx_ack = qdev_get_gpio_in_named(sfr, "ack", config->rx_sfr);
    tx_ack = qdev_get_gpio_in_named(sfr, "ack", config->tx_sfr);
    qdev_connect_gpio_out_named(DEVICE(&s->cpu), "ack", config->irq, rx_ack);
    qdev_connect_gpio_out_named(DEVICE(&s->cpu), "ack", config->irq - 1, tx_ack);
    rxe = qdev_get_gpio_in_named(DEVICE(usart), "enable_rx", 0);
    txe = qdev_get_gpio_in_named(DEVICE(usart), "enable_tx", 0);
    qdev_connect_gpio_out_named(sfr, "me", config->rxe, rxe);
    qdev_connect_gpio_out_named(sfr, "me", config->txe, txe);

    qdev_connect_clock_in(DEVICE(usart), "aclk", s->aclk);
    qdev_connect_clock_in(DEVICE(usart), "smclk", s->smclk);

    sysbus_realize(usart, errp);
    if (config->i2c) {
        DeviceState *irq = DEVICE(&s->cpu_irq[config->irq - 1]);

        sysbus_mmio_map(usart, 1, config->i2c0);
        sysbus_mmio_map(usart, 2, config->i2c1);

        object_property_set_int(OBJECT(irq), "num-lines", 2, &error_fatal);
        qdev_connect_gpio_out(irq, 0, qdev_get_gpio_in(DEVICE(&s->cpu),
                                                       config->irq - 1));
        qdev_realize(irq, NULL, &error_fatal);

        sysbus_connect_irq(SYS_BUS_DEVICE(sfr), config->tx_sfr,
                           qdev_get_gpio_in(irq, 0));
        sysbus_connect_irq(usart, 2, qdev_get_gpio_in(irq, 0));
    }
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
    if (clock_has_source(s->xt1) || clock_is_enabled(s->xt1)) {
        qdev_connect_clock_in(clock, "xt1", s->xt1);
    }
    if (clock_has_source(s->xt2) || clock_is_enabled(s->xt1)) {
        qdev_connect_clock_in(clock, "xt2", s->xt2);
    }
    s->aclk = qdev_get_clock_out(clock, "aclk");
    s->smclk = qdev_get_clock_out(clock, "smclk");
    s->mclk = qdev_get_clock_out(clock, "mclk");
    qdev_connect_clock_in(cpu, "mclk", s->mclk);
    sysbus_mmio_map(SYS_BUS_DEVICE(clock), 0, 0x50);
    sysbus_realize(SYS_BUS_DEVICE(clock), &error_fatal);

    qdev_realize(cpu, NULL, &error_fatal);

    if (mc->mpy_type) {
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->mpy), 0, 0x130);
        sysbus_realize(SYS_BUS_DEVICE(&s->mpy), &error_fatal);
    }

    for (i = 0; i < ARRAY_SIZE(mc->port); i++) {
        if (mc->port[i].present) {
            msp430_realize_port(s, SYS_BUS_DEVICE(&s->port[i]),
                                &mc->port[i], &error_fatal);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->port16); i++) {
        if (mc->port16[i].present) {
            msp430_realize_port16(s, SYS_BUS_DEVICE(&s->port16[i]),
                                  &mc->port[i], &error_fatal);
        }
    }

    if (mc->has_bt) {
        SysBusDevice *bt = SYS_BUS_DEVICE(&s->bt);

        sysbus_mmio_map(bt, 0, 0x40);
        sysbus_mmio_map(bt, 1, 0x46);
        qdev_connect_clock_in(DEVICE(bt), "aclk", s->aclk);
        qdev_connect_clock_in(DEVICE(bt), "smclk", s->smclk);
        sysbus_connect_irq(bt, 0, qdev_get_gpio_in(sfr, SFR_BT));
        sysbus_realize(bt, &error_fatal);
    }

    for (i = 0; i < ARRAY_SIZE(mc->timer); i++) {
        if (mc->timer[i].type) {
            msp430_realize_timer(s, SYS_BUS_DEVICE(&s->timer[i]),
                                 &mc->timer[i], &error_fatal);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->usci_a); i++) {
        if (mc->usci_a[i].present) {
            qdev_prop_set_chr(DEVICE(&s->usci_a[i]), "chardev", serial_hd(i));
            msp430_realize_usci(s, SYS_BUS_DEVICE(&s->usci_a[i]),
                                &mc->usci_a[i], &error_fatal);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->usci_b); i++) {
        if (mc->usci_b[i].present) {
            msp430_realize_usci(s, SYS_BUS_DEVICE(&s->usci_b[i]),
                                &mc->usci_b[i], &error_fatal);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->usart); i++) {
        if (mc->usart[i].present) {
            qdev_prop_set_chr(DEVICE(&s->usart[i]), "chardev", serial_hd(i));
            msp430_realize_usart(s, SYS_BUS_DEVICE(&s->usart[i]),
                                 &mc->usart[i], &error_fatal);
        }
    }

    if (mc->has_lcd) {
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->lcd), 0, 0x90);
        qdev_connect_clock_in(DEVICE(&s->lcd), "clk", s->bt.lcdclk);
        sysbus_realize(SYS_BUS_DEVICE(&s->lcd), &error_fatal);
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

    if (mc->mpy_type) {
        object_initialize_child(obj, "mpy", &s->mpy, mc->mpy_type);
    }

    for (i = 0; i < ARRAY_SIZE(mc->port); i++) {
        if (mc->port[i].present) {
            g_autofree char *name = g_strdup_printf("port%d", i + 1);

            object_initialize_child(obj, name, &s->port[i], TYPE_MSP430_PORT);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->port16); i++) {
        if (mc->port16[i].present) {
            g_autofree char *name = g_strdup_printf("port%c", 'A' + i);

            object_initialize_child(obj, name, &s->port16[i], TYPE_MSP430_PORT16);
        }
    }

    if (mc->has_bt) {
        object_initialize_child(obj, "basic-timer1", &s->bt, TYPE_MSP430_BASIC_TIMER);
    }

    for (i = 0; i < ARRAY_SIZE(mc->timer); i++) {
        if (mc->timer[i].type) {
            g_autofree char *name = g_strdup_printf("timer%d", i);

            object_initialize_child(obj, name, &s->timer[i], mc->timer[i].type);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->usci_a); i++) {
        if (mc->usci_a[i].present) {
            g_autofree char *name = g_strdup_printf("usci-a%d", i);

            object_initialize_child(obj, name, &s->usci_a[i], TYPE_MSP430_USCI_A);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->usci_b); i++) {
        if (mc->usci_b[i].present) {
            g_autofree char *name = g_strdup_printf("usci-b%d", i);

            object_initialize_child(obj, name, &s->usci_b[i], TYPE_MSP430_USCI_B);
        }
    }

    for (i = 0; i < ARRAY_SIZE(mc->usart); i++) {
        if (mc->usart[i].present) {
            g_autofree char *name = g_strdup_printf("usart%d", i);

            object_initialize_child(obj, name, &s->usart[i], TYPE_MSP430_USART);
        }

        if (mc->usart[i].i2c) {
            int irq = mc->usart[i].irq - 1;
            g_autofree char *name = g_strdup_printf("cpu-irq%d", irq);

            object_initialize_child(obj, name, &s->cpu_irq[irq], TYPE_OR_IRQ);
        }
    }

    if (mc->has_lcd) {
        object_initialize_child(obj, "lcd", &s->lcd, TYPE_MSP430_LCD);
    }
}

static void msp430_class_render(MSP430Class *mc, const char *name)
{
    bool has_usci = false, has_usart = false;
    DeviceClass *dc = DEVICE_CLASS(mc);
    int i;

    dc->desc = g_strdup_printf("%s mixed signal microcontroller", name);

    for (i = 0; i < 2; i++) {
        uint32_t rx_irq = 0, tx_irq = 0;

        if (mc->usci_a[i].present) {
            has_usci = true;
            rx_irq |= BIT(mc->usci_a[i].rx_sfr);
            tx_irq |= BIT(mc->usci_a[i].tx_sfr);
        }

        if (mc->usci_b[i].present) {
            has_usci = true;
            rx_irq |= BIT(mc->usci_b[i].rx_sfr);
            tx_irq |= BIT(mc->usci_b[i].tx_sfr);
        }

        mc->usart[i].irq = mc->uart_irq[i];
        if (mc->usart[i].present) {
            has_usart = true;
            rx_irq |= BIT(mc->usart[i].rx_sfr);
            if (!mc->usart[i].i2c) {
                tx_irq |= BIT(mc->usart[i].tx_sfr);
            }
        }

        mc->sfr_map[mc->uart_irq[i]] |= rx_irq;
        mc->sfr_map[mc->uart_irq[i] - 1] |= tx_irq;
    }

    assert(!(has_usci && has_usart));
    assert(!mc->has_lcd || mc->has_bt);
}

static void msp430_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MSP430Class *mc = MSP430_MCU_CLASS(oc);

    dc->realize = msp430_realize;

    mc->sfr_map[IRQ_NMI] = BIT(SFR_OF) | BIT(SFR_NMI) | BIT(SFR_ACCV);
    mc->sfr_map[IRQ_RESET] = BIT(SFR_POR) | BIT(SFR_RST);
    mc->sfr_map[IRQ_WDT] = BIT(SFR_WDT);
    memcpy(mc->port, port_configs, sizeof(mc->port));
    memcpy(mc->port16, port16_configs, sizeof(mc->port16));
    memcpy(mc->timer, timer_configs, sizeof(mc->timer));
    memcpy(mc->usci_a, usci_a_configs, sizeof(mc->usci_a));
    memcpy(mc->usci_b, usci_b_configs, sizeof(mc->usci_b));
    memcpy(mc->usart, usart_configs, sizeof(mc->usart));
}

static void msp430x2xx_class_init(ObjectClass *oc, void *data)
{
    MSP430Class *mc = MSP430_MCU_CLASS(oc);

    mc->clock_type = CLOCK_BCMP;
    mc->ports_have_sel2 = true;
    mc->ports_have_ren = true;
    mc->port[0].irq = NUM_IRQS - 14;
    mc->port[1].irq = NUM_IRQS - 13;
    mc->timer[0].irq = NUM_IRQS - 7;
    mc->timer[1].irq = NUM_IRQS - 3;
    mc->uart_irq[0] = NUM_IRQS - 9;
    mc->uart_irq[1] = NUM_IRQS - 15;
}

static void msp430f2012_class_init(ObjectClass *oc, void *data)
{
    MSP430Class *mc = MSP430_MCU_CLASS(oc);

    mc->flash_size = 2 * KiB;
    mc->sram_size = 128;
    mc->port[0].present = true;
    mc->port[1].present = true;
    mc->timer[0].type = TYPE_MSP430_TIMER_A;
    mc->timer[0].timers = 2;

    msp430_class_render(mc, "MSP430F2012");
}

static void msp430g2553_class_init(ObjectClass *oc, void *data)
{
    MSP430Class *mc = MSP430_MCU_CLASS(oc);
    int i;

    mc->flash_size = 16 * KiB;
    mc->sram_size = 512;
    mc->bsl_size = KiB;
    for (i = 0; i < 6; i++) {
        mc->port[i].present = true;
    }
    mc->timer[0].type = TYPE_MSP430_TIMER_A;
    mc->timer[0].timers = 3;
    mc->timer[1].type = TYPE_MSP430_TIMER_A;
    mc->timer[1].timers = 3;
    mc->usci_a[0].present = true;
    mc->usci_b[0].present = true;

    msp430_class_render(mc, "MSP430G2553");
}

static void msp430x4xx_class_init(ObjectClass *oc, void *data)
{
    MSP430Class *mc = MSP430_MCU_CLASS(oc);

    mc->clock_type = CLOCK_FLLP;
    mc->ports_have_ren = true;
    mc->port[0].irq = NUM_IRQS - 12;
    mc->port[1].irq = NUM_IRQS - 15;
    mc->timer[0].irq = NUM_IRQS - 10;
    mc->timer[1].irq = NUM_IRQS - 3;
    mc->uart_irq[0] = NUM_IRQS - 7;
    mc->uart_irq[1] = NUM_IRQS - 13;
    mc->sfr_map[NUM_IRQS - 16] = BIT(SFR_BT);
    mc->has_bt = true;
    mc->has_lcd = true;
}

static void msp430f449_class_init(ObjectClass *oc, void *data)
{
    MSP430Class *mc = MSP430_MCU_CLASS(oc);
    int i;

    mc->flash_size = 59 * KiB + 768;
    mc->sram_size = 2 * KiB;
    mc->bsl_size = KiB;
    mc->fllp_has_xts = true;
    mc->fllp_has_sel = true;
    mc->mpy_type = TYPE_MSP430_MPY;
    mc->ports_have_ren = false;
    for (i = 0; i < 6; i++) {
        mc->port[i].present = true;
    }
    mc->timer[0].type = TYPE_MSP430_TIMER_A;
    mc->timer[0].timers = 3;
    mc->timer[1].type = TYPE_MSP430_TIMER_B;
    mc->timer[1].timers = 7;
    mc->usart[0].present = true;
    mc->usart[1].present = true;

    msp430_class_render(mc, "MSP430F449");
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
        .name           = TYPE_MSP430F2012_MCU,
        .parent         = TYPE_MSP430X2XX_MCU,
        .class_init     = msp430f2012_class_init,
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
