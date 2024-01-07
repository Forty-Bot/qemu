/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#ifndef SIMPLE_LCD_H
#define SIMPLE_LCD_H

#include "qom/object.h"
#include "hw/qdev-core.h"

#define TYPE_GPIO_LCD "gpio-lcd"
OBJECT_DECLARE_SIMPLE_TYPE(GPIOLCDState, GPIO_LCD)

#define GPIO_LCD_MAX_SEGMENTS   256

struct GPIOLCDState {
    DeviceState parent_obj;

    QemuConsole *con;
    DECLARE_BITMAP(segments, GPIO_LCD_MAX_SEGMENTS);

    uint8_t *lcdmap;

    uint32_t width, height, foreground, background;
    bool dirty;
};

#endif /* MSP430_PORT_H */
