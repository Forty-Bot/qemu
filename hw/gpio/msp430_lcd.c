/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/gpio/msp430_lcd.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

REG8(CTL, 0)
    FIELD(CTL, P, 5, 3)
    FIELD(CTL, MX, 3, 2)
    FIELD(CTL, SON, 2, 1)
    FIELD(CTL, ON, 0, 1)
REG8(M1, 1)

static void lcd_clk_callback(void *opaque, ClockEvent event)
{
    MSP430LCDState *lcd = opaque;

    lcd->clk_on = clock_is_enabled(lcd->clk);
}

static void lcd_set_out_m(MSP430LCDState *lcd, int i)
{
    int j;

    for (j = 0; j < 8; j++) {
        int c = j & 3;
        int s = (i << 1) | (j >> 2);
        int level = !!(lcd->m[i] & BIT(j));

        if (!(lcd->ctl & R_CTL_ON_MASK)) {
            level = -1;
        } else if (!lcd->clk_on || !(lcd->ctl & R_CTL_SON_MASK)) {
            level = 0;
        } else if (c > FIELD_EX8(lcd->ctl, CTL, MX)) {
            level = 1;
        }

        qemu_set_irq(lcd->out[c][s], level);
    }
}

static void lcd_set_out(MSP430LCDState *lcd)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(lcd->m); i++) {
        lcd_set_out_m(lcd, i);
    }
}

static uint64_t lcd_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430LCDState *lcd = opaque;

    if (addr == A_CTL) {
        return lcd->ctl;
    }

    return lcd->m[addr - 1];
}

static void lcd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430LCDState *lcd = opaque;

    if (addr == A_CTL) {
        lcd->ctl = val;
        lcd_set_out(lcd);
    } else {
        lcd->m[addr - 1] = val;
        lcd_set_out_m(lcd, addr - 1);
    }
}

static const MemoryRegionOps lcd_ops = {
    .write = lcd_write,
    .read  = lcd_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void lcd_reset_hold(Object *obj)
{
    MSP430LCDState *lcd = MSP430_LCD(obj);

    lcd->ctl = 0x00;
    lcd_set_out(lcd);
}

static const ClockPortInitArray lcd_clocks = {
    QDEV_CLOCK_IN(MSP430LCDState, clk, lcd_clk_callback, ClockUpdate),
    QDEV_CLOCK_END,
};

static void lcd_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MSP430LCDState *lcd = MSP430_LCD(obj);
    int i;

    memory_region_init_io(&lcd->memory, OBJECT(lcd), &lcd_ops, lcd,
                          "msp430-lcd", 21);
    sysbus_init_mmio(d, &lcd->memory);

    qdev_init_clocks(DEVICE(obj), lcd_clocks);
    lcd_clk_callback(lcd, ClockUpdate);

    for (i = 0; i < MSP430_LCD_COMMON; i++) {
        g_autofree char *name = g_strdup_printf("out[%d]", i);

        qdev_init_gpio_out_named(DEVICE(lcd), lcd->out[i], name,
                                 MSP430_LCD_SEGMENTS);
    }
}

static int lcd_post_load(void *opaque, int version_id)
{
    MSP430LCDState *lcd = opaque;

    lcd_clk_callback(lcd, ClockUpdate);
    return 0;
}

static const VMStateDescription vmstate_lcd = {
    .name = "msp430-lcd",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = lcd_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(ctl, MSP430LCDState),
        VMSTATE_UINT8_ARRAY(m, MSP430LCDState, MSP430_LCD_SEGMENTS / 2),
        VMSTATE_CLOCK(clk, MSP430LCDState),
        VMSTATE_END_OF_LIST()
    }
};

static void lcd_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "MSP430 LCD controller";
    dc->vmsd = &vmstate_lcd;
    rc->phases.hold = lcd_reset_hold;
}

static const TypeInfo lcd_types[] = {
    {
        .parent = TYPE_SYS_BUS_DEVICE,
        .name = TYPE_MSP430_LCD,
        .instance_size = sizeof(MSP430LCDState),
        .instance_init = lcd_init,
        .class_init = lcd_class_init,
    },
};

DEFINE_TYPES(lcd_types)
