/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"

#define BTCTL   0x40

#define BTCTL_HOLD      BIT(6)
#define BTCTL_DIV       BIT(5)

#define LCDCTL  0x90
#define LCDM(n) (0x91 + (n))

#define LCDCTL_MX(n)    ((n) << 3)
#define LCDCTL_SON      BIT(2)
#define LCDCTL_ON       BIT(0)

static void test_lcd(gconstpointer test_data)
{
    QTestState *qts = qtest_init("-machine virt,mcu-type=msp430f449,xt1-frequency=32768");
    int common = (uintptr_t)test_data;
    int i, j;
    char *name = g_strdup_printf("out[%d]", common);

    qtest_irq_intercept_out_named(qts, "/machine/mcu/lcd", name);
    g_free(name);

    qtest_writeb(qts, BTCTL, BTCTL_DIV);
    qtest_writeb(qts, LCDCTL, LCDCTL_MX(3) | LCDCTL_SON | LCDCTL_ON);

    /* Test outputs */
    for (i = 0; i < 40; i++) {
        qtest_writeb(qts, LCDM(i / 2), BIT((i % 2) * 4 + common));
        g_assert_cmpint(qtest_get_irq(qts, i), ==, 1);
        qtest_writeb(qts, LCDM(i / 2), 0);
        g_assert_false(qtest_get_irq(qts, i));
    }

    /* Unused segments get turned on */
    for (j = 0; j < 3; j++) {
        qtest_writeb(qts, LCDCTL, LCDCTL_MX(j) | LCDCTL_SON | LCDCTL_ON);
        for (i = 0; i < 40; i++) {
            g_assert_cmpint(qtest_get_irq(qts, i), ==, common > j);
        }
    }

    /* Turn the device off */
    qtest_writeb(qts, LCDCTL, LCDCTL_MX(3));
    for (i = 0; i < 40; i++) {
        g_assert_cmpint(qtest_get_irq(qts, i), ==, -1);
    }

    /* Turn all the segments on */
    for (i = 0; i < 20; i++) {
        qtest_writeb(qts, LCDM(i), 0xff);
    }
    /* But disable the segment lines */
    qtest_writeb(qts, LCDCTL, LCDCTL_MX(3) | LCDCTL_ON);
    for (i = 0; i < 40; i++) {
        g_assert_false(qtest_get_irq(qts, i));
    }

    /* Enable SON and make sure everything's lit up again */
    qtest_writeb(qts, LCDCTL, LCDCTL_MX(3) | LCDCTL_SON | LCDCTL_ON);
    for (i = 0; i < 40; i++) {
        g_assert_cmpint(qtest_get_irq(qts, i), ==, 1);
    }

    /* Turn off the clock */
    qtest_writeb(qts, BTCTL, BTCTL_HOLD | BTCTL_DIV);
    for (i = 0; i < 40; i++) {
        g_assert_cmpint(qtest_get_irq(qts, i), ==, -1);
    }

    /* Test the (partial) reset behavior */
    qtest_set_irq_in(qts, "/machine/mcu/lcd", "puc", 0, 1);
    g_assert_cmphex(qtest_readb(qts, LCDCTL), ==, 0);
    for (i = 0; i < 20; i++) {
        g_assert_cmphex(qtest_readb(qts, LCDM(i)), ==, 0xff);
    }

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_data_func("lcd/0", (void *)0, test_lcd);
    qtest_add_data_func("lcd/1", (void *)1, test_lcd);
    qtest_add_data_func("lcd/2", (void *)2, test_lcd);
    qtest_add_data_func("lcd/3", (void *)3, test_lcd);

    return g_test_run();
}
