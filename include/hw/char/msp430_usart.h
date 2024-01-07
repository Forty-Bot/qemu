/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_USART_H
#define MSP430_USART_H

#include "chardev/char-fe.h"
#include "hw/clock.h"
#include "hw/i2c/i2c.h"
#include "hw/ssi/ssi.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MSP430_USART "msp430-usart"
#define TYPE_MSP430_USART_SPI "msp430-usart-spi-slave"
#define TYPE_MSP430_USART_I2C "msp430-usart-i2c-slave"

OBJECT_DECLARE_SIMPLE_TYPE(MSP430USARTState, MSP430_USART)
OBJECT_DECLARE_SIMPLE_TYPE(MSP430USARTSPIState, MSP430_USART_SPI)
OBJECT_DECLARE_SIMPLE_TYPE(MSP430USARTI2CState, MSP430_USART_I2C)

enum usart_mode {
    USART_UART,
    USART_SPI,
    USART_I2C,
};

struct MSP430USARTState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    CharBackend chr;
    SSIBus *spi_bus;
    I2CBus *i2c_bus;
    I2CSlave *i2c_slave;
    MemoryRegion memory, i2c_memory[2];
    QEMUTimer timer;
    qemu_irq rx_irq, tx_irq, i2c_irq;
    qemu_irq clear_rx, clear_tx;
    Clock *aclk, *smclk;

    uint16_t br;
    uint8_t ctl, tctl, rctl, mctl;
    uint8_t rxbuf;
    uint8_t txbuf;

    uint8_t i2cie, i2cifg, i2cndat;

    uint16_t own_addr;
    uint16_t slave_addr;

    enum usart_mode mode;
    bool rx_enabled, tx_enabled, has_i2c, expect_10bit;
    bool tx_busy, tx_ready, rx_unread, ste;
    int64_t char_time_ns, rx_next;
};

struct MSP430USARTSPIState {
    SSIPeripheral parent_obj;
    MSP430USARTState *usart;
};

struct MSP430USARTI2CState {
    I2CSlave parent_obj;
};

#endif /* MSP430_USART_H */
