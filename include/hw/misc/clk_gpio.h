/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#ifndef CLK_GPIO_H
#define CLK_GPIO_H

#include "chardev/char-fe.h"
#include "hw/clock.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_CLK_GPIO "clk-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(ClockGPIOState, CLK_GPIO)

struct ClockGPIOState {
    DeviceState parent_obj;

    Clock *clk;
    QEMUTimer timer;
    qemu_irq out;

    bool val;
};

#endif /* CLK_GPIO_H */
