/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "chardev/char-serial.h"
#include "hw/char/msp430_usart.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

/* Primary address space */
REG8(CTL, 0)
    /* Common */
    FIELD(CTL, LISTEN, 3, 1)
    FIELD(CTL, SYNC, 2, 1)
    FIELD(CTL, MM, 1, 1)
    /* UART */
    FIELD(CTL, PENA, 7, 1)
    FIELD(CTL, PEV, 6, 1)
    FIELD(CTL, SPB, 5, 1)
    /* UART/SPI */
    FIELD(CTL, CHAR, 4, 1)
    FIELD(CTL, SWRST, 0, 1)
    /* I2C/SPI */
    FIELD(CTL, I2C, 5, 1)
    /* I2C */
    FIELD(CTL, RXDMAEN, 7, 1)
    FIELD(CTL, TXDMAEN, 6, 1)
    FIELD(CTL, XA, 4, 1)
    FIELD(CTL, I2CEN, 0, 1)
REG8(TCTL, 1)
    /* Common */
    FIELD(TCTL, SSEL, 4, 2)
    /* UART */
    FIELD(TCTL, URXSE, 3, 1)
    FIELD(TCTL, TXWAKE, 2, 1)
    /* UART/SPI */
    FIELD(TCTL, CKPL, 6, 1)
    FIELD(TCTL, TXEPT, 0, 1)
    /* SPI */
    FIELD(TCTL, CKPH, 7, 1)
    /* I2C */
    FIELD(TCTL, WORD, 7, 1)
    FIELD(TCTL, RM, 6, 1)
    FIELD(TCTL, TRX, 3, 1)
    FIELD(TCTL, STB, 2, 1)
    FIELD(TCTL, STP, 1, 1)
    FIELD(TCTL, STT, 0, 1)
/* UART/SPI */
REG8(RCTL, 2)
    /* Common */
    FIELD(RCTL, FE, 7, 1)
    FIELD(RCTL, OE, 5, 1)
    /* UART */
    FIELD(RCTL, PE, 6, 1)
    FIELD(RCTL, BRK, 4, 1)
    FIELD(RCTL, URXEIE, 3, 1)
    FIELD(RCTL, URXWIE, 2, 1)
    FIELD(RCTL, RXWAKE, 1, 1)
    FIELD(RCTL, RXERR, 0, 1)
/* I2C */
REG8(DCTL, 2)
    FIELD(DCTL, BUSY, 5, 1)
    FIELD(DCTL, SCLLOW, 4, 1)
    FIELD(DCTL, SBD, 3, 1)
    FIELD(DCTL, TXUDF, 2, 1)
    FIELD(DCTL, RXOVR, 1, 1)
    FIELD(DCTL, BB, 0, 1)
/* UART/SPI */
REG8(MCTL, 3)
REG8(BR0, 4)
REG8(BR1, 5)
REG8(RXBUF, 6)
REG8(TXBUF, 7)
/* I2C */
REG8(PSC, 3)
REG8(SCLH, 4)
REG8(SCLL, 5)
REG8(DRL, 6)
REG8(DRH, 7)

/* I2C address space 0 */
REG16(I2COA, 0)
    FIELD(I2COA, ADDR, 0, 10)
REG16(I2CSA, 2)
    FIELD(I2CSA, ADDR, 0, 10)
REG16(I2CIV, 4)

/* I2C address space 1 */
REG8(I2CIE, 0)
REG8(I2CIFG, 1)
    FIELD(I2CI, STT, 7, 1)
    FIELD(I2CI, GC, 6, 1)
    FIELD(I2CI, TXRDY, 5, 1)
    FIELD(I2CI, RXRDY, 4, 1)
    FIELD(I2CI, ARDY, 3, 1)
    FIELD(I2CI, OA, 2, 1)
    FIELD(I2CI, NACK, 1, 1)
    FIELD(I2CI, AL, 0, 1)
REG8(I2CNDAT, 2)

#define R_RCTL_RX_MASK (R_RCTL_FE_MASK | R_RCTL_PE_MASK | R_RCTL_OE_MASK | \
                        R_RCTL_RXERR_MASK)

static void usart_ste(void *opaque, int irq, int level)
{
    MSP430USARTState *usart = opaque;

    usart->ste = level;
}

static void usart_enable_rx(void *opaque, int irq, int level)
{
    MSP430USARTState *usart = opaque;

    usart->rx_enabled = level;
}

static void usart_enable_tx(void *opaque, int irq, int level)
{
    MSP430USARTState *usart = opaque;

    usart->tx_enabled = level;
}

static enum usart_mode usart_mode(uint8_t ctl)
{
    if (ctl & R_CTL_SYNC_MASK) {
        if (ctl & R_CTL_I2C_MASK) {
            return USART_I2C;
        }

        return USART_SPI;
    }

    return USART_UART;
}

static int usart_uart_can_receive(void *opaque)
{
    MSP430USARTState *usart = opaque;

    if (!usart->rx_enabled) {
        return 0;
    }

    if (usart->ctl & (R_CTL_SWRST_MASK | R_CTL_LISTEN_MASK)) {
        return 0;
    }

    if (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) < usart->rx_next) {
        return 0;
    }

    return 1;
}

static void usart_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    MSP430USARTState *usart = opaque;

    if (!usart->rx_enabled) {
        return;
    }

    usart->rx_next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + usart->char_time_ns;
    if (usart->rx_unread || size > 1) {
        usart->rctl |= R_RCTL_OE_MASK | R_RCTL_RXERR_MASK;
        if (usart->rctl & R_RCTL_URXEIE_MASK) {
            qemu_irq_raise(usart->rx_irq);
        }
    }

    if (!usart->rx_unread) {
        usart->rxbuf = buf[0];
        usart->rx_unread = true;
        if (!(usart->rctl & R_RCTL_URXWIE_MASK)) {
            qemu_irq_raise(usart->rx_irq);
        }
    }
}

static void usart_uart_event(void *opaque, QEMUChrEvent event)
{
    MSP430USARTState *usart = opaque;

    if (event == CHR_EVENT_BREAK) {
        uint8_t nul = '\0';

        usart->rctl |= R_RCTL_BRK_MASK;
        usart_uart_receive(usart, &nul, 1);
    }
}

static uint8_t usart_get_txbuf(MSP430USARTState *usart)
{
    if (!(usart->ctl & R_CTL_CHAR_MASK)) {
        return usart->txbuf & 0x3f;
    }

    return usart->txbuf;
}

static void usart_send_char(MSP430USARTState *usart)
{
    if (usart->tx_enabled) {
        if (qemu_chr_fe_backend_connected(&usart->chr)) {
            uint8_t c = usart_get_txbuf(usart);

            qemu_chr_fe_write_all(&usart->chr, &c, 1);
        }

        if (usart->ctl & R_CTL_LISTEN_MASK) {
            usart_uart_receive(usart, &usart->txbuf, 1);
        }
    }

    timer_mod(&usart->timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + usart->char_time_ns);
    qemu_irq_raise(usart->tx_irq);
    usart->tx_busy = true;
    usart->tx_ready = true;
}

static void usart_complete(void *opaque)
{
    MSP430USARTState *usart = opaque;

    if (usart->tx_ready) {
        usart->tx_busy = false;
    } else {
        usart_send_char(usart);
    }
}

static void usart_set_params(MSP430USARTState *usart, const char *cause)
{
    QEMUSerialSetParams ssp;
    uint64_t baud_time, brclk_time;
    int bits = 1;

    /*
     * If cause is set, force a reconfiguration if we are not reset. Otherwise,
     * we can just wait for SWRST to be cleared
     */
    if (cause) {
        if ((usart->ctl & R_CTL_SWRST_MASK)) {
            return;
        }

        qemu_log_mask(LOG_GUEST_ERROR,
                      "msp430_usart: %s modified while not in reset\n",
                      cause);
    }

    usart->mode = usart_mode(usart->ctl);

    switch (FIELD_EX8(usart->tctl, TCTL, SSEL)) {
    case 0:
        qemu_log_mask(LOG_UNIMP, "msp430_usart: UCSSEL=UCLK not implemented\n");
        brclk_time = 0;
        break;
    case 1:
        brclk_time = clock_get(usart->aclk);
        break;
    case 2:
    case 3:
        brclk_time = clock_get(usart->smclk);
        break;
    }

    if (usart->mode == USART_I2C) {
        /* TODO */
        baud_time = 42949672960000;
    } else {
        baud_time = brclk_time * (usart->br * 8 + ctpop8(usart->mctl)) / 8;
    }
    ssp.speed = CLOCK_PERIOD_TO_HZ(baud_time);

    if (usart->ctl & R_CTL_PENA_MASK) {
        bits++;
        if (usart->ctl & R_CTL_PEV_MASK) {
            ssp.parity = 'E';
        } else {
            ssp.parity = 'O';
        }
    } else {
        ssp.parity = 'N';
    }

    ssp.data_bits = usart->ctl & R_CTL_CHAR_MASK ? 8 : 7;
    bits += ssp.data_bits;
    ssp.stop_bits = usart->ctl & R_CTL_SPB_MASK ? 2 : 1;
    bits += ssp.stop_bits;

    if (usart->mode == USART_SPI) {
        bits = ssp.data_bits;
    } else if (usart->mode == USART_I2C) {
        bits = 9;
    }

    usart->char_time_ns = baud_time * bits /
                         (CLOCK_PERIOD_1SEC / NANOSECONDS_PER_SECOND);
    if (!usart->char_time_ns) {
        usart->char_time_ns = INT64_MAX;
    }

    if (usart->mode == USART_UART) {
        qemu_chr_fe_ioctl(&usart->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
    }
    timer_del(&usart->timer);
}

static void usart_aclk_callback(void *opaque, ClockEvent event)
{
    MSP430USARTState *usart = opaque;

    if (FIELD_EX8(usart->tctl, TCTL, SSEL) == 1) {
        usart_set_params(usart, "ACLK");
    }
}

static void usart_smclk_callback(void *opaque, ClockEvent event)
{
    MSP430USARTState *usart = opaque;

    if (FIELD_EX8(usart->tctl, TCTL, SSEL) >= 2) {
        usart_set_params(usart, "SMCLK");
    }
}

static uint64_t usart_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430USARTState *usart = opaque;

    switch (addr) {
    case A_CTL:
        return usart->ctl;
    case A_TCTL:
        FIELD_DP8(usart->tctl, TCTL, TXEPT, !usart->tx_busy);
        return usart->tctl;
    case A_RCTL:
        return usart->rctl;
    case A_MCTL:
        return usart->mctl;
    case A_BR0:
        return usart->br;
    case A_BR1:
        return usart->br >> 8;
    case A_RXBUF:
        qemu_irq_lower(usart->rx_irq);
        usart->rctl &= ~R_RCTL_RX_MASK;
        usart->rx_unread = false;
        return usart->rxbuf;
    case A_TXBUF:
        return usart->txbuf;
    }

    g_assert_not_reached();
    return UINT64_MAX;
}

static void usart_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430USARTState *usart = opaque;

    switch (addr) {
    case A_CTL: {
        bool swrst_changed = (val ^ usart->ctl) & R_CTL_SWRST_MASK;
        bool param_changed = (val ^ usart->ctl) & ~R_CTL_SWRST_MASK;
        bool swrst = val & R_CTL_SWRST_MASK;

        if (val & R_CTL_SYNC_MASK) {
            if (usart->has_i2c && val & R_CTL_I2C_MASK) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "msp430_usart: USART does not support I2C mode\n");
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "msp430_usart: SPI and I2C not implemented\n");
            }
        }

        usart->ctl = val;
        if (swrst_changed) {
            if (swrst) {
                if (usart->tx_busy) {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "msp430_usart: SWRST set while transmitting\n");
                }

                timer_del(&usart->timer);
                qemu_irq_raise(usart->clear_rx);
                qemu_irq_raise(usart->clear_tx);
                qemu_irq_lower(usart->rx_irq);
                qemu_irq_raise(usart->tx_irq);
                usart->tx_busy = false;
                usart->tx_ready = true;
                usart->rx_unread = false;
                usart->rx_next = 0;
                usart->rctl &= ~R_RCTL_RX_MASK;
            } else {
                usart_set_params(usart, NULL);
            }
        } else if (param_changed) {
            if (!swrst) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "msp430_usart: CTL changed while not in reset\n");
            }

            usart_set_params(usart, NULL);
        }
        return;
    }
    case A_TCTL: {
        bool param_changed = (val ^ usart->tctl) &
                             (R_TCTL_CKPL_MASK | R_TCTL_SSEL_MASK |
                              R_TCTL_URXSE_MASK);

        usart->tctl = val;
        if (param_changed) {
            usart_set_params(usart, "TCTL");
        }
        return;
    }
    case A_RCTL:
        usart->rctl = val;
        return;
    case A_MCTL:
        usart->mctl = val;
        usart_set_params(usart, "MCTL");
        return;
    case A_BR0:
        usart->br &= 0xff00;
        usart->br |= val;
        usart_set_params(usart, "BR0");
        return;
    case A_BR1:
        usart->br &= 0xff;
        usart->br |= val << 8;
        usart_set_params(usart, "BR1");
        return;
    case A_RXBUF:
        qemu_log_mask(LOG_GUEST_ERROR, "msp430_usart: RXBUF is read-only\n");
        return;
    case A_TXBUF:
        usart->txbuf = val;
        if (usart->tx_busy) {
            if (usart->tx_ready) {
                qemu_irq_lower(usart->tx_irq);
            }
            usart->tx_ready = false;
        } else {
            usart_send_char(usart);
        }
        return;
    }
}

static const MemoryRegionOps usart_ops = {
    .write = usart_write,
    .read  = usart_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static uint16_t usart_i2civ_read(MSP430USARTState *usart)
{
    uint16_t irq = 8 - clz8(usart->i2cifg);

    usart->i2cifg &= BIT(irq) - 1;
    qemu_set_irq(usart->i2c_irq, !!(usart->i2cifg & usart->i2cie));
    return irq * 2;
}

static uint64_t usart_i2c0_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430USARTState *usart = MSP430_USART(opaque);

    switch (addr) {
    case A_I2COA:
        return usart->own_addr;
    case A_I2CSA:
        return usart->slave_addr;
    case A_I2CIV:
        return usart_i2civ_read(usart);        
    }

    g_assert_not_reached();
    return UINT64_MAX;
}

static void usart_i2c0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430USARTState *usart = MSP430_USART(opaque);

    switch (addr) {
    case A_I2COA:
        usart->own_addr = val & R_I2COA_ADDR_MASK;
        return;
    case A_I2CSA:
        usart->slave_addr = val & R_I2CSA_ADDR_MASK;
        return;
    case A_I2CIV:
        usart_i2civ_read(usart);
        return;
    }
}

static const MemoryRegionOps usart_i2c0_ops = {
    .read = usart_i2c0_read,
    .write = usart_i2c0_write,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
};

static uint64_t usart_i2c1_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430USARTState *usart = MSP430_USART(opaque);

    switch (addr) {
    case A_I2CIE:
        return usart->i2cie;
    case A_I2CIFG:
        return usart->i2cifg;
    case A_I2CNDAT:
        return usart->i2cndat;
    }

    g_assert_not_reached();
    return UINT64_MAX;
}

static void usart_i2c1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430USARTState *usart = MSP430_USART(opaque);

    switch (addr) {
    case A_I2CIE:
        usart->i2cie = val;
        break;
    case A_I2CIFG:
        usart->i2cifg = val;
        break;
    case A_I2CNDAT:
        /* FIXME */
        usart->i2cndat = val;
        return;
    }

    qemu_set_irq(usart->i2c_irq, !!(usart->i2cifg & usart->i2cie));
}

static const MemoryRegionOps usart_i2c1_ops = {
    .read = usart_i2c1_read,
    .write = usart_i2c1_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void usart_reset_hold(Object *obj)
{
    MSP430USARTState *usart = MSP430_USART(obj);

    usart->ctl = 0x01;
    usart->tctl = 0x01;
    usart->rctl = 0x00;
    usart->i2cie = 0x00;
    usart->i2cifg = 0x00;
    usart->i2cndat = 0x00;
    usart->own_addr = 0x0000;
    usart->slave_addr = 0x0000;
    usart->tx_busy = false;
    usart->tx_ready = true;
    usart->rx_unread = false;
    usart->rx_next = 0;
    qemu_irq_raise(usart->tx_irq);
}

static const ClockPortInitArray usart_clocks = {
    QDEV_CLOCK_IN(MSP430USARTState, aclk, usart_aclk_callback, ClockUpdate),
    QDEV_CLOCK_IN(MSP430USARTState, smclk, usart_smclk_callback, ClockUpdate),
    QDEV_CLOCK_END,
};

static void usart_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    MSP430USARTState *usart = MSP430_USART(dev);

    qemu_chr_fe_set_handlers(&usart->chr, usart_uart_can_receive,
                             usart_uart_receive, usart_uart_event, NULL,
                             usart, NULL, true);

    if (usart->has_i2c) {
        memory_region_init_io(&usart->i2c_memory[0], OBJECT(usart),
                              &usart_i2c0_ops, usart, "msp430-i2c-0", 6);
        sysbus_init_mmio(d, &usart->i2c_memory[0]);
        memory_region_init_io(&usart->i2c_memory[1], OBJECT(usart),
                              &usart_i2c1_ops, usart, "msp430-i2c-1", 3);
        sysbus_init_mmio(d, &usart->i2c_memory[1]);

        sysbus_init_irq(d, &usart->i2c_irq);

        usart->i2c_bus = i2c_init_bus(DEVICE(d), "i2c");
        usart->i2c_slave = i2c_slave_create_simple(usart->i2c_bus,
                                                   TYPE_MSP430_USART_I2C, 0);
    }
}

static void usart_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MSP430USARTState *usart = MSP430_USART(obj);

    memory_region_init_io(&usart->memory, OBJECT(usart), &usart_ops, usart,
                          "msp430-usart", 8);
    sysbus_init_mmio(d, &usart->memory);
    usart->spi_bus = ssi_create_bus(DEVICE(d), "spi");
    //usart->spi_slave = ssi_create_peripheral(usart->spi_bus,
    //                                         TYPE_MSP430_USART_SPI);

    timer_init_ns(&usart->timer, QEMU_CLOCK_VIRTUAL, usart_complete, usart);
    qdev_init_clocks(DEVICE(obj), usart_clocks);
    qdev_init_gpio_in_named(DEVICE(d), usart_ste, "ste", 1);
    qdev_init_gpio_in_named(DEVICE(d), usart_enable_rx, "enable_rx", 1);
    qdev_init_gpio_in_named(DEVICE(d), usart_enable_tx, "enable_tx", 1);
    sysbus_init_irq(d, &usart->rx_irq);
    sysbus_init_irq(d, &usart->tx_irq);
    qdev_init_gpio_out_named(DEVICE(d), &usart->clear_rx, "clear_rx", 1);
    qdev_init_gpio_out_named(DEVICE(d), &usart->clear_tx, "clear_tx", 1);
}

static int usart_post_load(void *opaque, int version_id)
{
    MSP430USARTState *usart = opaque;

    usart->mode = usart_mode(usart->ctl);
    return 0;
}

static const VMStateDescription vmstate_usart = {
    .name = "msp430-usart",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = usart_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(ctl, MSP430USARTState),
        VMSTATE_UINT8(tctl, MSP430USARTState),
        VMSTATE_UINT8(rctl, MSP430USARTState),
        VMSTATE_UINT8(mctl, MSP430USARTState),
        VMSTATE_UINT16(br, MSP430USARTState),
        VMSTATE_UINT8(rxbuf, MSP430USARTState),
        VMSTATE_UINT8(txbuf, MSP430USARTState),
        VMSTATE_BOOL(ste, MSP430USARTState),
        VMSTATE_BOOL(rx_enabled, MSP430USARTState),
        VMSTATE_BOOL(tx_enabled, MSP430USARTState),
        VMSTATE_BOOL(tx_busy, MSP430USARTState),
        VMSTATE_BOOL(tx_ready, MSP430USARTState),
        VMSTATE_BOOL(rx_unread, MSP430USARTState),
        VMSTATE_INT64(char_time_ns, MSP430USARTState),
        VMSTATE_INT64(rx_next, MSP430USARTState),
        VMSTATE_CLOCK(aclk, MSP430USARTState),
        VMSTATE_CLOCK(smclk, MSP430USARTState),
        VMSTATE_TIMER(timer, MSP430USARTState),
        VMSTATE_END_OF_LIST()
    }
};

static Property usart_properties[] = {
    DEFINE_PROP_CHR("chardev", MSP430USARTState, chr),
    DEFINE_PROP_BOOL("has_i2c", MSP430USARTState, has_i2c, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void usart_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc =
        "MSP430 universal synchronous/asynchronous receive/transmit (USART) peripheral interface";
    dc->realize = usart_realize;
    dc->vmsd = &vmstate_usart;
    device_class_set_props(dc, usart_properties);
    rc->phases.hold = usart_reset_hold;
}

static uint32_t usart_spi_transfer(SSIPeripheral *sp, uint32_t val)
{
    MSP430USARTSPIState *usart_spi = MSP430_USART_SPI(sp);
    MSP430USARTState *usart = usart_spi->usart;

    if (usart->mode != USART_SPI) {
        return 0;
    }

    if (usart->ctl & (R_CTL_SWRST_MASK | R_CTL_LISTEN_MASK | R_CTL_MM_MASK)) {
        return 0;
    }

    if (usart->ste) {
        return 0;
    }

    if (usart->rx_unread) {
        usart->rctl |= R_RCTL_OE_MASK;
    } else {
        usart->rxbuf = val;
        usart->rx_unread = true;
        qemu_irq_raise(usart->rx_irq);
    }

    qemu_irq_raise(usart->tx_irq);
    usart->tx_ready = true;
    return usart_get_txbuf(usart);
}

static const VMStateDescription vmstate_usart_spi = {
    .name = "msp430-usart-spi-slave",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_POINTER(usart, MSP430USARTSPIState, vmstate_usart, MSP430USARTState),
        VMSTATE_END_OF_LIST()
    }
};

static void usart_spi_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    SSIPeripheralClass *spc = SSI_PERIPHERAL_CLASS(oc);

    dc->desc = "MSP430 USART SPI slave pseudo-device";
    dc->vmsd = &vmstate_usart_spi;
    /* Must have usart populated */
    dc->user_creatable = false;
    spc->transfer_raw = usart_spi_transfer;
}

static bool usart_i2c_match(I2CSlave *candidate, uint8_t address,
                           bool broadcast, I2CNodeList *current_devs)
{
    BusState *bus = qdev_get_parent_bus(DEVICE(candidate));
    MSP430USARTState *usart = MSP430_USART(bus);
    struct I2CNode *node;
    uint16_t match;

    if (broadcast) {
        return false;
    }

    match = FIELD_EX16(usart->own_addr, I2COA, ADDR);
    if (usart->ctl & R_CTL_XA_MASK) {
        if (address != ((match >> 8) | 0x78)) {
            return false;
        }

        usart->expect_10bit = true;
    } else if (address != (match & 0x7f)) {
        return false;
    }

    node = g_new(struct I2CNode, 1);
    node->elt = candidate;
    QLIST_INSERT_HEAD(current_devs, node, next);
    return true;
}

static const VMStateDescription vmstate_usart_i2c = {
    .name = "msp430-usart-i2c-slave",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, MSP430USARTI2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void usart_i2c_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(oc);

    dc->desc = "MSP430 USART I2C slave pseudo-device";
    dc->vmsd = &vmstate_usart_i2c;
    /* Must have the correct parent */
    dc->user_creatable = false;
    isc->match_and_add = usart_i2c_match;
}

static const TypeInfo msp430_usart_types[] = {
    {
        .name = TYPE_MSP430_USART,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(MSP430USARTState),
        .instance_init = usart_init,
        .class_init = usart_class_init,
    },
    {
        .name = TYPE_MSP430_USART_SPI,
        .parent = TYPE_SSI_PERIPHERAL,
        .instance_size = sizeof(MSP430USARTSPIState),
        .class_init = usart_spi_class_init,
    },
    {
        .name = TYPE_MSP430_USART_I2C,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(MSP430USARTI2CState),
        .class_init = usart_i2c_class_init,
    },
};

DEFINE_TYPES(msp430_usart_types)
