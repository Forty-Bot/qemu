/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#ifdef CONFIG_PNG
#include <png.h>
#endif
#include "hw/display/gpio_lcd.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/datadir.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"

typedef struct {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/

    const char *lcdmap_file;
} GPIOLCDClass;

DECLARE_CLASS_CHECKERS(GPIOLCDClass, GPIO_LCD, TYPE_GPIO_LCD)

static void gpio_lcd_segment(void *opaque, int irq, int level)
{
    GPIOLCDState *lcd = opaque;

    if (level > 0) {
        if (!test_and_set_bit(irq, lcd->segments)) {
            lcd->dirty = true;
        }
    } else {
        if (test_and_clear_bit(irq, lcd->segments)) {
            lcd->dirty = true;
        }
    }
}

static void gpio_lcd_invalidate(void *opaque)
{
    GPIOLCDState *lcd = opaque;

    lcd->dirty = true;
}

static uint32_t pixel32_to_surface(DisplaySurface *surface, uint32_t color)
{
    uint32_t r = color >> 16 & 0xff;
    uint32_t g = color >> 8 & 0xff;
    uint32_t b = color & 0xff;

    switch (surface_bits_per_pixel(surface)) {
    case 8:
        return rgb_to_pixel8(r, g, b);
    case 15:
        return rgb_to_pixel15(r, g, b);
    case 16:
        return rgb_to_pixel16(r, g, b);
    default:
        return color;
    }
}

static void gpio_lcd_update(void *opaque)
{
    GPIOLCDState *lcd = opaque;

    if (lcd->dirty) {
        DisplaySurface *surface = qemu_console_surface(lcd->con);
        uint32_t foreground = pixel32_to_surface(surface, lcd->foreground);
        uint32_t background = pixel32_to_surface(surface, lcd->background);
        void *data = surface_data(surface);
        int bpp = surface_bytes_per_pixel(surface);
        int stride = surface_stride(surface);
        int x, y;

        for (y = 0; y < lcd->height; y++) {
            for (x = 0; x < lcd->width; x++) {
                uint32_t color;

                if (test_bit(lcd->lcdmap[y * lcd->width + x],
                             lcd->segments)) {
                    color = foreground;
                } else {
                    color = background;
                }

                memcpy(data + stride * y + bpp * x, &color, bpp);
            }
        }

        lcd->dirty = false;
    }

    dpy_gfx_update_full(lcd->con);
}

static const GraphicHwOps gpio_lcd_ops = {
    .invalidate = gpio_lcd_invalidate,
    .gfx_update = gpio_lcd_update,
};

static void gpio_lcd_realize(DeviceState *dev, Error **errp)
{
#ifdef CONFIG_PNG
    GPIOLCDState *lcd = GPIO_LCD(dev);
    GPIOLCDClass *lc = GPIO_LCD_GET_CLASS(dev);
    g_autofree uint8_t *colormap = NULL;
    png_image image = {
        .version = PNG_IMAGE_VERSION,
    };
    char *file;

    file = qemu_find_file(QEMU_FILE_TYPE_LCDMAP, lc->lcdmap_file);
    if (!file) {
        error_setg(errp, "Unable to find '%s'", lc->lcdmap_file);
        return;
    }

    if (!png_image_begin_read_from_file(&image, file)) {
        error_setg(errp, "Could not open '%s': %s", file,
                   image.message);
        goto out;
    }

    if (!(image.format & PNG_FORMAT_FLAG_COLORMAP)) {
        error_setg(errp, "LCD segment map '%s' must use indexed colors", file);
        goto out;
    }

    assert(image.colormap_entries <= GPIO_LCD_MAX_SEGMENTS);
    lcd->width = image.width;
    lcd->height = image.height;

    image.format = PNG_FORMAT_RGB_COLORMAP;
    lcd->lcdmap = g_malloc(PNG_IMAGE_SIZE(image));
    /*
     * We don't care what the colors are mapped to, but we have to provide
     * something to make libpng happy
     */
    colormap = g_malloc(PNG_IMAGE_COLORMAP_SIZE(image));

    if (!png_image_finish_read(&image, NULL, lcd->lcdmap, 0, colormap)) {
        error_setg(errp, "Could not read '%s': %s", file, image.message);
        goto out;
    }

    qdev_init_gpio_in(dev, gpio_lcd_segment, image.colormap_entries);

    lcd->dirty = true;
    lcd->con = graphic_console_init(dev, 0, &gpio_lcd_ops, lcd);
    qemu_console_resize(lcd->con, lcd->width, lcd->height);

out:
    png_image_free(&image);
#else
    error_setg(errp, "Enable PNG support with libpng for gpio-lcd");
#endif
}

static Property gpio_lcd_properties[] = {
    DEFINE_PROP_UINT32("foreground-color", GPIOLCDState, foreground, 0x1b2d43),
    DEFINE_PROP_UINT32("background-color", GPIOLCDState, background, 0xa1b093),
    DEFINE_PROP_END_OF_LIST(),
};

static void gpio_lcd_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = gpio_lcd_realize;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    device_class_set_props(dc, gpio_lcd_properties);
}

static const TypeInfo gpio_lcd_info = {
    .name = TYPE_GPIO_LCD,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(GPIOLCDState),
    .class_size = sizeof(GPIOLCDClass),
    .class_init = gpio_lcd_class_init,
    .abstract = true,
};

struct gpio_lcd_data {
    const char *name, *file, *desc;
};

static void gpio_lcd_subclass_init(ObjectClass *oc, void *data)
{
    const struct gpio_lcd_data *lcd_data = data;
    GPIOLCDClass *lc = GPIO_LCD_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = lcd_data->desc;
    lc->lcdmap_file = lcd_data->file;
}

#define LCD(_name, _desc) { \
    .name = _name, \
    .file = _name ".png", \
    .desc = _desc \
}

static const struct gpio_lcd_data gpio_lcd_data[] = {
    LCD("sblcda2", "SoftBaugh SBLCDA2 display"),
};

static void gpio_lcd_register(void)
{
    int i;

    type_register_static(&gpio_lcd_info);

    for (i = 0; i < ARRAY_SIZE(gpio_lcd_data); i++) {
        TypeInfo lcd_info = {
            .name = gpio_lcd_data[i].name,
            .parent = TYPE_GPIO_LCD,
            .class_init = gpio_lcd_subclass_init,
            .class_data = (void *)&gpio_lcd_data[i],
        };

        type_register(&lcd_info);
    }
}
type_init(gpio_lcd_register)
