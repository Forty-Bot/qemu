/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "chardev/char-serial.h"
#include "hw/char/msp430_usci.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

/* Primary address space */
REG8(CTL0, 0)
    /* Common */
    FIELD(CTL0, MODE, 1, 2)
    FIELD(CTL0, SYNC, 0, 1)
    /* UART/SPI */
    FIELD(CTL0, MSB, 5, 1)
    FIELD(CTL0, 7BIT, 4, 1)
    /* UART */
    FIELD(CTL0, PEN, 7, 1)
    FIELD(CTL0, PAR, 6, 1)
    FIELD(CTL0, SPB, 3, 1)
    /* SPI/I2C */
    FIELD(CTL0, MST, 3, 1)
    /* SPI */
    FIELD(CTL0, CKPH, 7, 1)
    FIELD(CTL0, CKPL, 6, 1)
    /* I2C */
    FIELD(CTL0, A10, 7, 1)
    FIELD(CTL0, SLA10, 6, 1)
    FIELD(CTL0, MM, 5, 1)
REG8(CTL1, 1)
    /* Common */
    FIELD(CTL1, SSEL, 6, 2)
    FIELD(CTL1, SWRST, 0, 1)
    /* UART */
    FIELD(CTL1, RXEIE, 5, 1)
    FIELD(CTL1, BRKIE, 4, 1)
    FIELD(CTL1, DORM, 3, 1)
    FIELD(CTL1, TXADDR, 2, 1)
    FIELD(CTL1, TXBRK, 1, 1)
    /* I2C */
    FIELD(CTL1, TR, 4, 1)
    FIELD(CTL1, TXNACK, 3, 1)
    FIELD(CTL1, TXSTP, 2, 1)
    FIELD(CTL1, TXSTT, 1, 1)
REG8(BR0, 2)
REG8(BR1, 3)
/* UART */
REG8(MCTL, 4)
    FIELD(MCTL, BRF, 4, 4)
    FIELD(MCTL, BRS, 1, 3)
    FIELD(MCTL, OS16, 0, 1)
/* I2C */
REG8(I2CIE, 4)
    FIELD(I2CIE, NACKIE, 3, 1)
    FIELD(I2CIE, STPIE, 2, 1)
    FIELD(I2CIE, STTIE, 1, 1)
    FIELD(I2CIE, ALIE, 0, 1)
REG8(STAT, 5)
    /* UART/SPI */
    FIELD(STAT, LISTEN, 7, 1)
    FIELD(STAT, FE, 6, 1)
    FIELD(STAT, OE, 5, 1)
    FIELD(STAT, BUSY, 0, 1)
    /* UART */
    FIELD(STAT, PE, 4, 1)
    FIELD(STAT, BRK, 3, 1)
    FIELD(STAT, RXERR, 2, 1)
    FIELD(STAT, ADDR, 1, 1)
    FIELD(STAT, IDLE, 1, 1)
    /* I2C */
    FIELD(STAT, SCLLOW, 6, 1)
    FIELD(STAT, GC, 5, 1)
    FIELD(STAT, BBUSY, 4, 1)
    FIELD(STAT, NACKIFG, 3, 1)
    FIELD(STAT, STPIFG, 2, 1)
    FIELD(STAT, STTIFG, 1, 1)
    FIELD(STAT, ALIFG, 0, 1)
REG8(RXBUF, 6)
REG8(TXBUF, 7)

/* I2C address space */
REG16(I2COA, 0)
    FIELD(I2COA, GCEN, 15, 1)
    FIELD(I2COA, ADDR, 0, 10)
REG16(I2CSA, 2)
    FIELD(I2CSA, ADDR, 0, 10)

#define R_STAT_RX_MASK (R_STAT_FE_MASK | R_STAT_OE_MASK | R_STAT_PE_MASK | \
                        R_STAT_RXERR_MASK | R_STAT_IDLE_MASK)

typedef struct {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    bool is_a;
} MSP430USCIClass;

DECLARE_CLASS_CHECKERS(MSP430USCIClass, MSP430_USCI, TYPE_MSP430_USCI)

static enum usci_mode usci_mode(uint8_t ctl0)
{
    if (ctl0 & R_CTL0_SYNC_MASK) {
        if (FIELD_EX8(ctl0, CTL0, MODE) == 3) {
            return USCI_I2C;
        }

        return USCI_SPI;
    }

    return USCI_UART;
}

static void usci_ste(void *opaque, int irq, int level)
{
    MSP430USCIState *usci = opaque;

    usci->ste = level;
}

static int usci_uart_can_receive(void *opaque)
{
    MSP430USCIState *usci = opaque;

    if (usci->ctl1 & R_CTL1_SWRST_MASK) {
        return 0;
    }

    if (usci->stat & R_STAT_LISTEN_MASK) {
        return 0;
    }

    if (usci->rx_next > qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)) {
        return 0;
    }

    return 1;
}

static void usci_set_rxbuf(MSP430USCIState *usci, uint8_t data)
{
    if (usci->ctl0 & R_CTL0_MSB_MASK) {
        usci->rxbuf = revbit8(data);
        if (usci->ctl0 & R_CTL0_7BIT_MASK) {
            usci->rxbuf >>= 1;
        }
    } else {
        usci->rxbuf = data;
    }
}

static void usci_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    MSP430USCIState *usci = opaque;

    usci->rx_next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + usci->char_time_ns;
    if (usci->rx_unread || size > 1) {
        usci->stat |= R_STAT_OE_MASK | R_STAT_RXERR_MASK;
        if (usci->ctl1 & R_CTL1_RXEIE_MASK) {
            qemu_irq_raise(usci->rx_irq);
        }
    }

    if (!usci->rx_unread) {
        usci_set_rxbuf(usci, buf[0]);
        usci->rx_unread = true;
        if (!(usci->ctl1 & R_CTL1_DORM_MASK)) {
            qemu_irq_raise(usci->rx_irq);
        }
    }
}

static void usci_uart_event(void *opaque, QEMUChrEvent event)
{
    MSP430USCIState *usci = opaque;

    if (event == CHR_EVENT_BREAK) {
        uint8_t nul = '\0';

        usci->stat |= R_STAT_BRK_MASK;
        if (usci->ctl1 & R_CTL1_BRKIE_MASK) {
            qemu_irq_raise(usci->rx_irq);
        }

        usci_uart_receive(usci, &nul, 1);
    }
}

static uint8_t usci_get_txbuf(MSP430USCIState *usci)
{
    uint8_t data = usci->txbuf;

    if (usci->ctl0 & R_CTL0_7BIT_MASK) {
        data &= 0x3ff;
    }

    if (usci->ctl0 & R_CTL0_MSB_MASK) {
        data = revbit8(data);
        if (usci->ctl0 & R_CTL0_7BIT_MASK) {
            data >>= 1;
        }
    }

    return data;
}

static void usci_send_char(MSP430USCIAState *usci_a)
{
    MSP430USCIState *usci = &usci_a->parent_obj;
    int break_enabled = 1;

    if (usci->ctl1 & R_CTL1_TXBRK_MASK) {
        qemu_chr_fe_ioctl(&usci_a->chr, CHR_IOCTL_SERIAL_SET_BREAK,
                          &break_enabled);
        usci->ctl1 &= ~R_CTL1_TXBRK_MASK;
    } else if (qemu_chr_fe_backend_connected(&usci_a->chr)) {
        uint8_t c = usci_get_txbuf(usci);

        qemu_chr_fe_write_all(&usci_a->chr, &c, 1);
    }

    if (usci->stat & R_STAT_LISTEN_MASK) {
        usci_uart_receive(usci, &usci->txbuf, 1);
    }

    timer_mod(&usci->timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + usci->char_time_ns);
    if (!usci->tx_ready) {
        qemu_irq_raise(usci->tx_irq);
    }
    usci->tx_busy = true;
    usci->tx_ready = true;
}

static void usci_complete(void *opaque)
{
    MSP430USCIAState *usci_a = opaque;
    MSP430USCIState *usci = &usci_a->parent_obj;

    if (usci->tx_ready) {
        usci->tx_busy = false;
    } else {
        usci_send_char(usci_a);
    }
}

static void usci_set_params(MSP430USCIState *usci, const char *cause)
{
    QEMUSerialSetParams ssp;
    uint64_t br = usci->br;
    uint64_t brs = FIELD_EX8(usci->mctl, MCTL, BRS);
    uint64_t brf = FIELD_EX8(usci->mctl, MCTL, BRF);
    uint64_t baud_time, brclk_time;
    int bits = 1;

    /*
     * If cause is set, force a reconfiguration if we are not reset. Otherwise,
     * we can just wait for SWRST to be cleared
     */
    if (cause) {
        if ((usci->ctl1 & R_CTL1_SWRST_MASK)) {
            return;
        }

        qemu_log_mask(LOG_GUEST_ERROR,
                      "msp430_usci: %s modified while not in reset\n",
                      cause);
    }

    usci->mode = usci_mode(usci->ctl0);

    switch (FIELD_EX8(usci->ctl1, CTL1, SSEL)) {
    case 0:
        qemu_log_mask(LOG_UNIMP, "msp430_usci: UCSSEL=UCLK not implemented\n");
        brclk_time = 0;
        break;
    case 1:
        brclk_time = clock_get(usci->aclk);
        break;
    case 2:
    case 3:
        brclk_time = clock_get(usci->smclk);
        break;
    }

    if (usci->mode != USCI_UART) {
        baud_time = brclk_time * br;
    } else if (usci->mctl & R_MCTL_OS16_MASK) {
        baud_time = brclk_time * (br * 256 + br * brs * 2 + brf) / 16;
    } else {
        baud_time = brclk_time * (br * 8 + brs) / 8;
    }
    ssp.speed = CLOCK_PERIOD_TO_HZ(baud_time);

    if (usci->ctl0 & R_CTL0_PEN_MASK) {
        bits++;
        if (usci->ctl0 & R_CTL0_PAR_MASK) {
            ssp.parity = 'E';
        } else {
            ssp.parity = 'O';
        }
    } else {
        ssp.parity = 'N';
    }

    ssp.data_bits = usci->ctl0 & R_CTL0_7BIT_MASK ? 7 : 8;
    bits += ssp.data_bits;
    ssp.stop_bits = usci->ctl0 & R_CTL0_SPB_MASK ? 2 : 1;
    bits += ssp.stop_bits;

    if (usci->mode == USCI_SPI) {
        bits = ssp.data_bits;
    } else if (usci->mode == USCI_I2C) {
        bits = 9;
    }

    usci->char_time_ns = baud_time * bits /
                         (CLOCK_PERIOD_1SEC / NANOSECONDS_PER_SECOND);
    if (!usci->char_time_ns) {
        usci->char_time_ns = INT64_MAX;
    }

    if (usci->mode == USCI_UART) {
        qemu_chr_fe_ioctl(&MSP430_USCI_A(usci)->chr,
                          CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
    }
    timer_del(&usci->timer);
}

static void usci_aclk_callback(void *opaque, ClockEvent event)
{
    MSP430USCIState *usci = opaque;

    if (FIELD_EX8(usci->ctl1, CTL1, SSEL) == 1) {
        usci_set_params(usci, "ACLK");
    }
}

static void usci_smclk_callback(void *opaque, ClockEvent event)
{
    MSP430USCIState *usci = opaque;

    if (FIELD_EX8(usci->ctl1, CTL1, SSEL) >= 2) {
        usci_set_params(usci, "SMCLK");
    }
}

static uint64_t usci_read(void *opaque, hwaddr uaddr, unsigned size)
{
    MSP430USCIState *usci = opaque;
    const MSP430USCIClass *uc = MSP430_USCI_GET_CLASS(usci);
    int addr = uaddr;

    if (uc->is_a) {
        addr -= 3;
    }

    switch (addr) {
    case A_CTL0:
        return usci->ctl0;
    case A_CTL1:
        return usci->ctl1;
    case A_BR0:
        return usci->br;
    case A_BR1:
        return usci->br >> 8;
    case A_MCTL:
        return usci->mctl;
    case A_STAT:
        FIELD_DP8(usci->stat, STAT, BUSY, usci->tx_busy);
        return usci->stat;
    case A_RXBUF:
        qemu_irq_lower(usci->rx_irq);
        usci->stat &= ~R_STAT_RX_MASK;
        usci->rx_unread = false;
        return usci->rxbuf;
    case A_TXBUF:
        return usci->txbuf;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "msp430_usci: Register 0x%x not implemented.\n", addr);
        return UINT64_MAX;
    }
}

static void usci_write(void *opaque, hwaddr uaddr, uint64_t val, unsigned size)
{
    MSP430USCIState *usci = opaque;
    const MSP430USCIClass *uc = MSP430_USCI_GET_CLASS(usci);
    int addr = uaddr;

    if (uc->is_a) {
        addr -= 3;
    }

    switch (addr) {
    case A_CTL0: {
        uint8_t mode = FIELD_EX8(val, CTL0, MODE);

        if (val & R_CTL0_SYNC_MASK) {
            if (uc->is_a && mode == 3) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "msp430_usci: USCI A does not support I2C mode\n");
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "msp430_usci: SPI and I2C not implemented\n");
            }
        } else if (!uc->is_a) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "msp430_usci: USCI B does not support UART mode\n");
        } else if (mode != 0) {
            qemu_log_mask(LOG_UNIMP,
                          "msp430_usci: UCMODEx=%d not implemented\n", mode);
        }

        usci->ctl0 = val;
        usci_set_params(usci, "CTL0");
        return;
    }
    case A_CTL1: {
        bool swrst_changed = (val ^ usci->ctl1) & R_CTL1_SWRST_MASK;
        bool swrst = val & R_CTL1_SWRST_MASK;

        usci->ctl1 = val;
        if (swrst_changed) {
            if (swrst) {
                if (usci->tx_busy) {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "msp430_usci: UCSWRST set while transmitting\n");
                }

                timer_del(&usci->timer);
                qemu_irq_raise(usci->clear_rx);
                qemu_irq_raise(usci->clear_tx);
                qemu_irq_lower(usci->rx_irq);
                qemu_irq_raise(usci->tx_irq);
                usci->tx_busy = false;
                usci->tx_ready = true;
                usci->rx_unread = false;
                usci->rx_next = 0;
                usci->stat &= ~R_STAT_RX_MASK;
            } else {
                usci_set_params(usci, NULL);
            }
        } else if ((val ^ usci->ctl1) & R_CTL1_SSEL_MASK) {
            if (!swrst) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "msp430_usci: UCSSEL modified while not in reset\n");
            }
            usci_set_params(usci, NULL);
        }

        return;
    }
    case A_BR0:
        usci->br &= 0xff00;
        usci->br |= val;
        usci_set_params(usci, "BR0");
        return;
    case A_BR1:
        usci->br &= 0xff;
        usci->br |= val << 8;
        usci_set_params(usci, "BR1");
        return;
    case A_MCTL:
        usci->mctl = val;

        switch (usci->mode) {
        case USCI_UART:
            usci_set_params(usci, "MCTL");
            break;
        case USCI_SPI:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "msp430_usci: No register at 0x4.\n");
            break;
        case USCI_I2C:
            /* TODO */
            break;
        }

        break;
    case A_STAT: {
        bool listen_changed = (usci->stat ^ val) & R_STAT_LISTEN_MASK;

        /* FIXME: IRQ */
        usci->stat = val;
        if (usci->mode != USCI_I2C && listen_changed &&
            !(usci->ctl1 & R_CTL1_SWRST_MASK)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "msp430_usci: UCLISTEN modified while not in reset\n");
        }
        return;
    }
    case A_RXBUF:
        qemu_log_mask(LOG_GUEST_ERROR, "msp430_usci: RXBUF is read-only\n");
        return;
    case A_TXBUF:
        usci->txbuf = val;
        if (usci->mode == USCI_UART && (usci->ctl1 & R_CTL1_TXBRK_MASK) && usci->txbuf) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "msp430_usci: TXBUF should be 0 when UCTXBRK is set\n");
        }
        usci_send_char(MSP430_USCI_A(usci));
        return;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "msp430_sfr: Register 0x%x not implemented.\n", addr);
        return;
    }
}

static const MemoryRegionOps usci_ops = {
    .write = usci_write,
    .read  = usci_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void usci_reset_hold(Object *obj)
{
    MSP430USCIState *usci = MSP430_USCI(obj);

    usci->ctl0 = 0x00;
    usci->ctl1 = 0x01;
    usci->br = 0x0000;
    usci->mctl = 0x00;
    usci->stat = 0x00;
    usci->rxbuf = 0x00;
    usci->txbuf = 0x00;
    usci->tx_busy = false;
    usci->tx_ready = true;
    usci->rx_unread = false;
    usci->rx_next = 0;
    qemu_irq_raise(usci->tx_irq);
}

static const ClockPortInitArray usci_clocks = {
    QDEV_CLOCK_IN(MSP430USCIState, aclk, usci_aclk_callback, ClockUpdate),
    QDEV_CLOCK_IN(MSP430USCIState, smclk, usci_smclk_callback, ClockUpdate),
    QDEV_CLOCK_END,
};

static void usci_a_realize(DeviceState *dev, Error **errp)
{
    MSP430USCIAState *usci_a = MSP430_USCI_A(dev);

    qemu_chr_fe_set_handlers(&usci_a->chr, usci_uart_can_receive,
                             usci_uart_receive, usci_uart_event, NULL,
                             usci_a, NULL, true);
}

static void usci_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MSP430USCIState *usci = MSP430_USCI(obj);
    const MSP430USCIClass *uc = MSP430_USCI_GET_CLASS(usci);

    memory_region_init_io(&usci->memory, OBJECT(usci), &usci_ops, usci,
                          "msp430-usci", uc->is_a ? 11 : 8);
    sysbus_init_mmio(d, &usci->memory);
    usci->spi_bus = ssi_create_bus(DEVICE(d), "spi");
    //usci->spi_slave = ssi_create_peripheral(usci->spi_bus,
    //                                        TYPE_MSP430_USCI_SPI);

    timer_init_ns(&usci->timer, QEMU_CLOCK_VIRTUAL, usci_complete, usci);
    qdev_init_clocks(DEVICE(obj), usci_clocks);
    qdev_init_gpio_in_named(DEVICE(d), usci_ste, "ste", 1);
    sysbus_init_irq(d, &usci->rx_irq);
    sysbus_init_irq(d, &usci->tx_irq);
    qdev_init_gpio_out_named(DEVICE(d), &usci->clear_rx, "clear_rx", 1);
    qdev_init_gpio_out_named(DEVICE(d), &usci->clear_tx, "clear_tx", 1);
}

static int usci_post_load(void *opaque, int version_id)
{
    MSP430USCIState *usci = opaque;

    usci->mode = usci_mode(usci->ctl0);
    return 0;
}

static const VMStateDescription vmstate_usci = {
    .name = "msp430-usci",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = usci_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(ctl0, MSP430USCIState),
        VMSTATE_UINT8(ctl1, MSP430USCIState),
        VMSTATE_UINT16(br, MSP430USCIState),
        VMSTATE_UINT8(stat, MSP430USCIState),
        VMSTATE_UINT8(rxbuf, MSP430USCIState),
        VMSTATE_UINT8(txbuf, MSP430USCIState),
        VMSTATE_BOOL(ste, MSP430USCIState),
        VMSTATE_BOOL(tx_busy, MSP430USCIState),
        VMSTATE_BOOL(tx_ready, MSP430USCIState),
        VMSTATE_BOOL(rx_unread, MSP430USCIState),
        VMSTATE_INT64(char_time_ns, MSP430USCIState),
        VMSTATE_INT64(rx_next, MSP430USCIState),
        VMSTATE_CLOCK(aclk, MSP430USCIState),
        VMSTATE_CLOCK(smclk, MSP430USCIState),
        VMSTATE_TIMER(timer, MSP430USCIState),
        VMSTATE_END_OF_LIST()
    }
};

static void usci_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->vmsd = &vmstate_usci;
    rc->phases.hold = usci_reset_hold;
}

static Property usci_a_properties[] = {
    DEFINE_PROP_CHR("chardev", MSP430USCIAState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void usci_a_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MSP430USCIClass *uc = MSP430_USCI_CLASS(oc);

    dc->desc = "MSP430 universal serial communications interface (USCI) A";
    dc->realize = usci_a_realize;
    device_class_set_props(dc, usci_a_properties);
    uc->is_a = true;
}

static uint64_t usci_i2c_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430USCIBState *usci_b = MSP430_USCI_B(opaque);

    if (addr == A_I2COA) {
        return usci_b->own_addr;
    }

    return usci_b->slave_addr;
}

static void usci_i2c_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430USCIBState *usci_b = MSP430_USCI_B(opaque);

    if (addr == A_I2COA) {
        usci_b->own_addr = val;
        usci_b->own_addr &= R_I2COA_GCEN_MASK | R_I2COA_ADDR_MASK;
    } else {
        usci_b->slave_addr = val;
        usci_b->slave_addr &= R_I2CSA_ADDR_MASK;
    }
}

static const MemoryRegionOps usci_i2c_ops = {
    .read = usci_i2c_read,
    .write = usci_i2c_write,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
};

static void usci_b_reset_hold(Object *obj)
{
    MSP430USCIBState *usci_b = MSP430_USCI_B(obj);

    usci_b->own_addr = 0x0000;
    usci_b->slave_addr = 0x0000;
    usci_reset_hold(OBJECT(usci_b));
}

static void usci_b_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MSP430USCIBState *usci_b = MSP430_USCI_B(obj);

    memory_region_init_io(&usci_b->i2c_memory, obj, &usci_i2c_ops, usci_b,
                          "msp430-usci-i2c", 4);
    sysbus_init_mmio(d, &usci_b->i2c_memory);

    usci_b->i2c_bus = i2c_init_bus(DEVICE(d), "i2c");
    usci_b->i2c_slave = i2c_slave_create_simple(usci_b->i2c_bus,
                                                TYPE_MSP430_USCI_I2C, 0);
}

static void usci_b_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "MSP430 universal serial communications interface (USCI) B";
    rc->phases.hold = usci_b_reset_hold;
}

static uint32_t usci_spi_transfer(SSIPeripheral *sp, uint32_t val)
{
    MSP430USCISPIState *usci_spi = MSP430_USCI_SPI(sp);
    MSP430USCIState *usci = usci_spi->usci;

    if (usci->ctl1 & R_CTL1_SWRST_MASK) {
        return 0;
    }

    if (usci->stat & R_STAT_LISTEN_MASK) {
        return 0;
    }

    if (!(usci->ctl0 & R_CTL0_SYNC_MASK)) {
        return 0;
    }

    switch (FIELD_EX8(usci->ctl0, CTL0, MODE)) {
    case 0:
        break;
    case 1:
        if (!usci->ste) {
            return 0;
        }

        break;
    case 2:
        if (usci->ste) {
            return 0;
        }

        break;
    case 3:
        return 0;
    }

    if (usci->rx_unread) {
        usci->stat |= R_STAT_OE_MASK;
    } else {
        usci_set_rxbuf(usci, val);
        usci->rx_unread = true;
        qemu_irq_raise(usci->rx_irq);
    }

    qemu_irq_raise(usci->tx_irq);
    usci->tx_ready = true;
    return usci_get_txbuf(usci);
}

static const VMStateDescription vmstate_usci_spi = {
    .name = "msp430-usci-spi-slave",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_POINTER(usci, MSP430USCISPIState, vmstate_usci, MSP430USCIState),
        VMSTATE_END_OF_LIST()
    }
};

static void usci_spi_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    SSIPeripheralClass *spc = SSI_PERIPHERAL_CLASS(oc);

    dc->desc = "MSP430 USCI SPI slave pseudo-device";
    dc->vmsd = &vmstate_usci_spi;
    /* Must have usci populated */
    dc->user_creatable = false;
    spc->transfer_raw = usci_spi_transfer;
}

static bool usci_i2c_match(I2CSlave *candidate, uint8_t address,
                           bool broadcast, I2CNodeList *current_devs)
{
    BusState *bus = qdev_get_parent_bus(DEVICE(candidate));
    MSP430USCIState *usci = MSP430_USCI(bus);
    MSP430USCIBState *usci_b = MSP430_USCI_B(bus);
    struct I2CNode *node;
    uint16_t match;

    if (broadcast && !(usci_b->own_addr & R_I2COA_GCEN_MASK)) {
        return false;
    }

    match = FIELD_EX16(usci_b->own_addr, I2COA, ADDR);
    if (usci->ctl0 & R_CTL0_SLA10_MASK) {
        if (address != ((match >> 8) | 0x78)) {
            return false;
        }

        usci_b->expect_10bit = true;
    } else if (address != (match & 0x7f)) {
        return false;
    }

    node = g_new(struct I2CNode, 1);
    node->elt = candidate;
    QLIST_INSERT_HEAD(current_devs, node, next);
    return true;
}

static const VMStateDescription vmstate_usci_i2c = {
    .name = "msp430-usci-i2c-slave",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, MSP430USCII2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void usci_i2c_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(oc);

    dc->desc = "MSP430 USCI B I2C slave pseudo-device";
    dc->vmsd = &vmstate_usci_i2c;
    /* Must have the correct parent */
    dc->user_creatable = false;
    isc->match_and_add = usci_i2c_match;
}

static const TypeInfo msp430_usci_types[] = {
    {
        .name = TYPE_MSP430_USCI,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(MSP430USCIState),
        .instance_init = usci_init,
        .class_size = sizeof(MSP430USCIClass),
        .class_init = usci_class_init,
        .abstract = true,
    },
    {
        .name = TYPE_MSP430_USCI_A,
        .parent = TYPE_MSP430_USCI,
        .instance_size = sizeof(MSP430USCIAState),
        .class_init = usci_a_class_init,
    },
    {
        .name = TYPE_MSP430_USCI_B,
        .parent = TYPE_MSP430_USCI,
        .instance_size = sizeof(MSP430USCIBState),
        .instance_init = usci_b_init,
        .class_init = usci_b_class_init,
    },
    {
        .name = TYPE_MSP430_USCI_SPI,
        .parent = TYPE_SSI_PERIPHERAL,
        .instance_size = sizeof(MSP430USCISPIState),
        .class_init = usci_spi_class_init,
    },
    {
        .name = TYPE_MSP430_USCI_I2C,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(MSP430USCII2CState),
        .class_init = usci_i2c_class_init,
    },
};

DEFINE_TYPES(msp430_usci_types)
