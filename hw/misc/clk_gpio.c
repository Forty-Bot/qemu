/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/misc/clk_gpio.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "hw/qdev-clock.h"
#include "qemu/timer.h"

#define CLK_TO_NS (CLOCK_PERIOD_1SEC / NANOSECONDS_PER_SECOND)

static void clk_gpio_event(void *opaque)
{
    ClockGPIOState *cg = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t clk_per = clock_get(cg->clk);

    if (clk_per) {
        cg->val = !cg->val;
        qemu_set_irq(cg->out, cg->val);
        timer_mod(&cg->timer, now + clk_per / CLK_TO_NS / 2);
    } else {
        timer_del(&cg->timer);
    }
}

static void clk_gpio_set(void *opaque, ClockEvent event)
{
    ClockGPIOState *cg = opaque;

    if (clock_get(cg->clk) && !timer_pending(&cg->timer)) {
        clk_gpio_event(cg);
    }
}

static void clk_gpio_init(Object *obj)
{
    ClockGPIOState *cg = CLK_GPIO(obj);
    DeviceState *dev = DEVICE(obj);

    qdev_init_gpio_out(dev, &cg->out, 1);
    timer_init_ns(&cg->timer, QEMU_CLOCK_VIRTUAL, clk_gpio_event, cg);
    cg->clk = qdev_init_clock_in(dev, "clk", clk_gpio_set, cg, ClockUpdate);
}

static const VMStateDescription vmstate_clk_gpio = {
    .name = "clk-gpio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_CLOCK(clk, ClockGPIOState),
        VMSTATE_TIMER(timer, ClockGPIOState),
        VMSTATE_BOOL(val, ClockGPIOState),
        VMSTATE_END_OF_LIST()
    },
};

static void clk_gpio_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = "Virtual clock GPIO output";
    dc->vmsd = &vmstate_clk_gpio;
}

static const TypeInfo clk_gpio_info = {
    .name = TYPE_CLK_GPIO,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(ClockGPIOState),
    .instance_init = clk_gpio_init,
    .class_init = clk_gpio_class_init,
};

static void clk_gpio_register(void)
{
    type_register_static(&clk_gpio_info);
}
type_init(clk_gpio_register)
