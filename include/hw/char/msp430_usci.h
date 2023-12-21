/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_USCI_H
#define MSP430_USCI_H

#include "chardev/char-fe.h"
#include "hw/clock.h"
#include "hw/i2c/i2c.h"
#include "hw/ssi/ssi.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MSP430_USCI "msp430-usci"
#define TYPE_MSP430_USCI_A "msp430-usci-a"
#define TYPE_MSP430_USCI_B "msp430-usci-b"
#define TYPE_MSP430_USCI_SPI "msp430-usci-spi-slave"
#define TYPE_MSP430_USCI_I2C "msp430-usci-i2c-slave"

OBJECT_DECLARE_SIMPLE_TYPE(MSP430USCIState, MSP430_USCI)
OBJECT_DECLARE_SIMPLE_TYPE(MSP430USCIAState, MSP430_USCI_A)
OBJECT_DECLARE_SIMPLE_TYPE(MSP430USCIBState, MSP430_USCI_B)
OBJECT_DECLARE_SIMPLE_TYPE(MSP430USCISPIState, MSP430_USCI_SPI)
OBJECT_DECLARE_SIMPLE_TYPE(MSP430USCII2CState, MSP430_USCI_I2C)

enum usci_mode {
    USCI_UART,
    USCI_SPI,
    USCI_I2C,
};

struct MSP430USCIState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    SSIBus *spi_bus;
    MemoryRegion memory;
    QEMUTimer timer;
    qemu_irq rx_irq, tx_irq;
    qemu_irq clear_rx, clear_tx;
    Clock *aclk, *smclk;
    
    uint16_t br;
    uint8_t ctl0, ctl1;
    uint8_t mctl;
    uint8_t stat;
    uint8_t rxbuf;
    uint8_t txbuf;

    enum usci_mode mode;
    bool tx_busy, tx_ready, rx_unread, ste;
    int64_t char_time_ns, rx_next;
};

struct MSP430USCIAState {
    /*< private >*/
    MSP430USCIState parent_obj;
    /*< public >*/

    CharBackend chr;
};

struct MSP430USCIBState {
    /*< private >*/
    MSP430USCIState parent_obj;
    /*< public >*/

    MemoryRegion i2c_memory;
    I2CBus *i2c_bus;
    I2CSlave *i2c_slave;

    uint16_t own_addr;
    uint16_t slave_addr;

    bool expect_10bit;
};

struct MSP430USCISPIState {
    SSIPeripheral parent_obj;
    MSP430USCIState *usci;
};

struct MSP430USCII2CState {
    I2CSlave parent_obj;
};

#endif /* MSP430_USCI_H */
