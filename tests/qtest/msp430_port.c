/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"

#define REG(dev, off) ((dev)->base + ((off) << (dev)->shift))
#define IN(dev)  REG(dev, 0)
#define OUT(dev) REG(dev, 1)
#define DIR(dev) REG(dev, 2)
#define IFG(dev) REG(dev, 3)
#define IES(dev) REG(dev, 4)
#define IE(dev)  REG(dev, 5)

#define IO(dev, num) ((dev)->io_base + num)

struct device {
    uint64_t base, ren, shift;
    const char *name, *path;
    int io_base;
    bool has_irq;
};

#define _PORT(_name, _path, _base, _ren, _shift, _io_base, _irq) \
static const struct device _name = { \
    .name = #_name, \
    .path = _path, \
    .base = _base, \
    .ren = _ren, \
    .shift = _shift, \
    .io_base = _io_base, \
    .has_irq = _irq, \
}

#define PORT(num, base, ren, irq) \
    _PORT(port##num, "/machine/mcu/port" #num, base, ren, 0, 0, irq)

#define PORT16(letter, num, base, ren, io_base) \
    _PORT(port##num, "/machine/mcu/port" #letter, base, ren, 1, io_base, false)

PORT(1, 0x20, 0x27, true);
PORT(2, 0x28, 0x2f, true);
PORT(3, 0x18, 0x10, false);
PORT(4, 0x1c, 0x11, false);
PORT(5, 0x30, 0x12, false);
PORT(6, 0x34, 0x13, false);
PORT16(A, 7, 0x38, 0x14, 0);
PORT16(A, 8, 0x39, 0x15, 8);
PORT16(B, 9, 0x08, 0x16, 0);
PORT16(B, 10, 0x09, 0x17, 8);

struct test_case {
    const char *mcu;
    const struct device *dev;
    bool has_ren;
};

#define TEST(_mcu, _dev, _ren) { \
    .mcu = #_mcu, \
    .dev = &_dev, \
    .has_ren = _ren, \
}

static const struct test_case test_cases[] = {
    TEST(msp430f1611, port1, false),
    TEST(msp430f1611, port2, false),
    TEST(msp430f1611, port3, false),
    TEST(msp430f1611, port4, false),
    TEST(msp430f1611, port5, false),
    TEST(msp430f1611, port6, false),
    TEST(msp430f4794, port1, true),
    TEST(msp430f4794, port2, true),
    TEST(msp430f4794, port3, true),
    TEST(msp430f4794, port4, true),
    TEST(msp430f4794, port5, true),
    TEST(msp430f4794, port7, true),
    TEST(msp430f4794, port8, true),
    TEST(msp430f4794, port9, true),
    TEST(msp430f4794, port10, true),
};

static void test_basic(gconstpointer test_data)
{
    const struct test_case *test = test_data;
    QTestState *qts = qtest_initf("-machine virt,mcu-type=%s", test->mcu);
    const struct device *dev = test->dev;
    int i;

    qtest_irq_intercept_out(qts, dev->path);
    for (i = 0; i < 8; i++) {
        /* Test high input works */
        qtest_set_irq_in(qts, dev->path, NULL, IO(dev, i), 1);
        g_assert_cmphex(qtest_readb(qts, IN(dev)) & BIT(i), ==, BIT(i));

        /* Test output works */
        qtest_writeb(qts, OUT(dev), 0);
        qtest_writeb(qts, DIR(dev), BIT(i));
        g_assert_false(qtest_get_irq(qts, IO(dev, i)));
        qtest_writeb(qts, OUT(dev), BIT(i));
        g_assert_cmpint(qtest_get_irq(qts, IO(dev, i)), ==, 1);

        /* Test high input and direction */
        qtest_set_irq_in(qts, dev->path, NULL, IO(dev, i), 0);
        g_assert_cmphex(qtest_readb(qts, IN(dev)) & BIT(i), ==, BIT(i));
        qtest_writeb(qts, DIR(dev), 0);
        g_assert_cmphex(qtest_readb(qts, IN(dev)) & BIT(i), ==, 0);
        g_assert_cmpint(qtest_get_irq(qts, IO(dev, i)), ==, -1);

        if (test->has_ren) {
            /* Test pull-up with High-Z input */
            qtest_writeb(qts, dev->ren, BIT(i));
            qtest_set_irq_in(qts, dev->path, NULL, IO(dev, i), -1);
            g_assert_cmphex(qtest_readb(qts, IN(dev)) & BIT(i), ==, BIT(i));
            qtest_writeb(qts, OUT(dev), 0);
            g_assert_cmphex(qtest_readb(qts, IN(dev)) & BIT(i), ==, 0);

            /* Test pull-up with driven input */
            qtest_set_irq_in(qts, dev->path, NULL, IO(dev, i), 1);
            g_assert_cmphex(qtest_readb(qts, IN(dev)) & BIT(i), ==, BIT(i));
            qtest_writeb(qts, OUT(dev), BIT(i));
            qtest_set_irq_in(qts, dev->path, NULL, IO(dev, i), 0);
            g_assert_cmphex(qtest_readb(qts, IN(dev)) & BIT(i), ==, 0);
        }
    }

    /* Test the (partial) reset behavior */
    qtest_writeb(qts, OUT(dev), 0xff);
    qtest_writeb(qts, DIR(dev), 0xff);
    if (test->has_ren) {
        qtest_writeb(qts, dev->ren, 0xff);
    }
    qtest_set_irq_in(qts, dev->path, "puc", 0, 1);
    g_assert_cmphex(qtest_readb(qts, OUT(dev)), ==, 0xff);
    g_assert_cmphex(qtest_readb(qts, DIR(dev)), ==, 0);
    if (test->has_ren) {
        g_assert_cmphex(qtest_readb(qts, dev->ren), ==, 0);
    }

    qtest_quit(qts);
}

static bool
should_interrupt(bool old_in, bool old_out, bool old_dir, bool old_pol,
                 bool new_in, bool new_out, bool new_dir, bool new_pol)
{
    bool old_val = old_dir ? old_out : old_in;
    bool new_val = new_dir ? new_out : new_in;

    if (old_pol != new_pol && old_val != new_pol) {
        return true;
    }

    return new_pol ? old_val && !new_val : !old_val && new_val;
}

static void test_irq(gconstpointer test_data)
{
    const struct test_case *test = test_data;
    QTestState *qts = qtest_initf("-machine virt,mcu-type=%s", test->mcu);
    const struct device *dev = test->dev;
    int i, j;

    qtest_irq_intercept_out_named(qts, dev->path, "sysbus-irq");
    for (i = 0; i < 8; i++) {

        /* Test flags/enable */
        g_assert_false(qtest_get_irq(qts, 0));
        qtest_writeb(qts, IFG(dev), BIT(i));
        g_assert_false(qtest_get_irq(qts, 0));
        qtest_writeb(qts, IE(dev), BIT(i));
        g_assert_cmpint(qtest_get_irq(qts, 0), ==, 1);
        qtest_writeb(qts, IFG(dev), 0);
        g_assert_false(qtest_get_irq(qts, 0));

        /* Test all of the various triggers */
        for (j = 0; j < 256; j++) {
            bool old_in  = j & BIT(0);
            bool old_out = j & BIT(1);
            bool old_dir = j & BIT(2);
            bool old_pol = j & BIT(3);
            bool new_in  = j & BIT(4);
            bool new_out = j & BIT(5);
            bool new_dir = j & BIT(6);
            bool new_pol = j & BIT(7);

            qtest_set_irq_in(qts, dev->path, NULL, IO(dev, i), old_in);
            qtest_writeb(qts, OUT(dev), old_out ? BIT(i) : 0);
            qtest_writeb(qts, DIR(dev), old_dir ? BIT(i) : 0);
            qtest_writeb(qts, IES(dev), old_pol ? BIT(i) : 0);
            qtest_writeb(qts, IFG(dev), 0);

            qtest_writeb(qts, IES(dev), new_pol ? BIT(i) : 0);
            if (old_dir) {
                qtest_set_irq_in(qts, dev->path, NULL, IO(dev, i), new_in);
            } else {
                qtest_writeb(qts, OUT(dev), new_out ? BIT(i) : 0);
            }
            qtest_writeb(qts, DIR(dev), new_dir ? BIT(i) : 0);
            if (old_dir) {
                qtest_writeb(qts, OUT(dev), new_out ? BIT(i) : 0);
            } else {
                qtest_set_irq_in(qts, dev->path, NULL, IO(dev, i), new_in);
            }

            g_assert_cmpint(qtest_get_irq(qts, 0), ==,
                            should_interrupt(old_in, old_out, old_dir, old_pol,
                                             new_in, new_out, new_dir, new_pol));
        }
    }

    /* Test the (partial) reset behavior */
    qtest_writeb(qts, IFG(dev), 0xff);
    qtest_writeb(qts, IES(dev), 0xff);
    qtest_writeb(qts, IE(dev), 0xff);
    qtest_set_irq_in(qts, dev->path, "puc", 0, 1);
    g_assert_cmphex(qtest_readb(qts, IFG(dev)), ==, 0);
    g_assert_cmphex(qtest_readb(qts, IES(dev)), ==, 0xff);
    g_assert_cmphex(qtest_readb(qts, IE(dev)), ==, 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    int i;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
        const struct test_case *test = &test_cases[i];
        char *path = g_strdup_printf("%s/%s/basic", test->mcu, test->dev->name);

        qtest_add_data_func(path, test, test_basic);
        g_free(path);

        if (test->dev->has_irq) {
            path = g_strdup_printf("%s/%s/irq", test->mcu, test->dev->name);
            qtest_add_data_func(path, test, test_irq);
            g_free(path);
        }
    }

    return g_test_run();
}
