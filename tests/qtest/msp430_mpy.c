/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"

#define MPY     0x130
#define MPYS    0x132
#define MAC     0x134
#define MACS    0x136
#define OP2     0x138
#define RESLO   0x13a
#define RESHI   0x13c
#define SUMEXT  0x13e

static const struct {
    uint8_t op1, op2;
} mul8[] = {
    { 0x00, 0x00 },
    { 0x01, 0x01 },
    { 0x03, 0x07 },
    { 0x00, 0xf8 },
    { 0x80, 0x00 },
    { 0x80, 0xf8 },
    { 0xab, 0x2d },
    { 0x2d, 0xab },
    { 0xf0, 0xf0 },
    { 0xff, 0xff },
    { 0xff, 0x01 },
    { 0x01, 0xff },
};

static const struct {
    uint16_t op1, op2;
} mul16[] = {
    { 0x0000, 0x0000 },
    { 0x0001, 0x0001 },
    { 0x0003, 0x0007 },
    { 0x0000, 0xff80 },
    { 0x8000, 0x0000 },
    { 0x8000, 0xff80 },
    { 0xaaab, 0x02fd },
    { 0x02fd, 0xaaab },
    { 0xff00, 0xff00 },
    { 0xffff, 0xffff },
    { 0xffff, 0x0001 },
    { 0x0001, 0xffff },
};

static const struct {
    uint32_t op1, op2;
} mul24[] = {
    { 0x000000, 0x000000 },
    { 0x000001, 0x000001 },
    { 0x000003, 0x000007 },
    { 0x000000, 0xfff800 },
    { 0x800000, 0x000000 },
    { 0x800000, 0xfff800 },
    { 0xaaaaab, 0x02fffd },
    { 0x02fffd, 0xaaaaab },
    { 0xfff000, 0xfff000 },
    { 0xffffff, 0xffffff },
    { 0xffffff, 0x000001 },
    { 0x000001, 0xffffff },
};

static const struct {
    uint32_t op1, op2;
} mul32[] = {
    { 0x00000000, 0x00000000 },
    { 0x00000001, 0x00000001 },
    { 0x00000003, 0x00000007 },
    { 0x00000000, 0xffff8000 },
    { 0x80000000, 0x00000000 },
    { 0x80000000, 0xffff8000 },
    { 0xaaaaaaab, 0x02fffffd },
    { 0x02fffffd, 0xaaaaaaab },
    { 0xffff0000, 0xffff0000 },
    { 0xffffffff, 0xffffffff },
    { 0xffffffff, 0x00000001 },
    { 0x00000001, 0xffffffff },
};

static const uint32_t mac32[] = {
    0x00000000,
    0x00000001,
    0x0000007f,
    0xffffff80,
    0x000000ff,
    0x00007fff,
    0xffff8000,
    0x0000ffff,
    0x7fffffff,
    0x80000000,
    0xffffffff,
};

static const uint64_t mac64[] = {
    0x0000000000000000,
    0x0000000000000001,
    0x000000000000007f,
    0xffffffffffffff80,
    0x00000000000000ff,
    0x0000000000007fff,
    0xffffffffffff8000,
    0x000000000000ffff,
    0x000000007fffffff,
    0xffffffff80000000,
    0x00000000ffffffff,
    0x00007fffffffffff,
    0xffff800000000000,
    0x0000ffffffffffff,
    0x7fffffffffffffff,
    0x8000000000000000,
    0xffffffffffffffff,
};

static void test_mpy(gconstpointer test_data)
{
    const char *mcu = test_data;
    QTestState *qts = qtest_initf("-machine virt,mcu-type=%s", mcu);
    int i, j;

    /* Dummy MAC to clear MPYC */
    qtest_writeb(qts, MAC, 0);
    qtest_writeb(qts, OP2, 0);

#define TEST_MUL(mul, type) \
    for (i = 0; i < ARRAY_SIZE(mul); i++) { \
        printf("%s %s %04x %04x\n", #mul, #type, mul[i].op1, mul[i].op2); \
        type op1 = mul[i].op1; \
        type op2 = mul[i].op2;

#define CHECK_MUL(exp, sumext) \
    g_assert_cmphex(qtest_readw(qts, MPY), ==, (uint16_t)op1); \
    g_assert_cmphex(qtest_readw(qts, OP2), ==, (uint16_t)op2); \
    uint32_t act = qtest_readw(qts, RESLO); \
    act |= (uint32_t)qtest_readw(qts, RESHI) << 16; \
    g_assert_cmphex(act, ==, (uint32_t)(exp)); \
    g_assert_cmphex(qtest_readw(qts, SUMEXT), ==, sumext); \
}

    TEST_MUL(mul8, uint8_t)
        qtest_writeb(qts, MPY, op1);
        qtest_writeb(qts, OP2, op2);
    CHECK_MUL((uint32_t)op1 * op2, 0)

    TEST_MUL(mul8, int8_t)
        int32_t exp = (int32_t)op1 * op2;

        qtest_writeb(qts, MPYS, op1);
        qtest_writew(qts, MPYS, (int16_t)(int8_t)qtest_readb(qts, MPYS));
        qtest_writeb(qts, OP2, op2);
        qtest_writew(qts, OP2, (int16_t)(int8_t)qtest_readb(qts, OP2));
    CHECK_MUL(exp, exp < 0 ? 0xffff : 0)

    TEST_MUL(mul16, uint16_t)
        qtest_writew(qts, MPY, op1);
        qtest_writew(qts, OP2, op2);
    CHECK_MUL((uint32_t)op1 * op2, 0)

    TEST_MUL(mul16, int16_t)
        int32_t exp = (int32_t)op1 * op2;

        qtest_writew(qts, MPYS, op1);
        qtest_writew(qts, OP2, op2);
    CHECK_MUL(exp, exp < 0 ? 0xffff : 0)

#define TEST_MAC(mul, type) \
    for (j = 0; j < ARRAY_SIZE(mac32); j++) { \
        printf("MAC %08x\n", mac32[j]); \
        uint32_t acc = mac32[j]; \
        TEST_MUL(mul, type) \
            qtest_writew(qts, RESLO, acc & 0xffff); \
            qtest_writew(qts, RESHI, acc >> 16);

#define CHECK_MAC(exp, sumext) CHECK_MUL(exp, sumext) }

    TEST_MAC(mul8, uint8_t)
        uint32_t exp = (uint32_t)op1 * op2 + acc;

        qtest_writeb(qts, MAC, op1);
        qtest_writeb(qts, OP2, op2);
    CHECK_MAC(exp, exp < acc)

    TEST_MAC(mul8, int8_t)
        int32_t exp = (int32_t)op1 * op2 + acc;

        qtest_writeb(qts, MACS, op1);
        qtest_writew(qts, MACS, (int16_t)(int8_t)qtest_readb(qts, MACS));
        qtest_writew(qts, OP2, (int16_t)op2);
    CHECK_MAC(exp, exp < 0 ? 0xffff : 0)

    TEST_MAC(mul16, uint16_t)
        uint32_t exp = (uint32_t)op1 * op2 + acc;

        qtest_writew(qts, MAC, op1);
        qtest_writew(qts, OP2, op2);
    CHECK_MAC(exp, exp < acc)

    TEST_MAC(mul16, int16_t)
        int32_t exp = (int32_t)op1 * op2 + acc;

        qtest_writew(qts, MACS, op1);
        qtest_writew(qts, OP2, op2);
    CHECK_MAC(exp, exp < 0 ? 0xffff : 0)

#undef TEST_MUL
#undef TEST_MAC
#undef CHECK_MUL
#undef CHECK_MAC

    qtest_quit(qts);
}

#define MPY32L  0x140
#define MPY32H  0x142
#define MPYS32L 0x144
#define MPYS32H 0x146
#define MAC32L  0x148
#define MAC32H  0x14a
#define MACS32L 0x14c
#define MACS32H 0x14e
#define OP2L    0x150
#define OP2H    0x152
#define RES0    0x154
#define RES1    0x156
#define RES2    0x158
#define RES3    0x15a
#define CTL0    0x15c

static void test_mpy32(gconstpointer test_data)
{
    const char *mcu = test_data;
    QTestState *qts = qtest_initf("-machine virt,mcu-type=%s", mcu);
    int i, j;

    qtest_writew(qts, CTL0, 0);

#define TEST_MUL(mul, type) \
    for (i = 0; i < ARRAY_SIZE(mul); i++) { \
        printf("%s %s %08x %08x\n", #mul, #type, mul[i].op1, mul[i].op2); \
        type op1 = mul[i].op1; \
        type op2 = mul[i].op2;

#define CHECK_MUL(exp, sumext) \
    uint64_t act = qtest_readw(qts, RES0); \
    act |= (uint64_t)qtest_readw(qts, RES1) << 16; \
    act |= (uint64_t)qtest_readw(qts, RES2) << 32; \
    act |= (uint64_t)qtest_readw(qts, RES3) << 48; \
    g_assert_cmphex(act, ==, (uint64_t)(exp)); \
    g_assert_cmphex(qtest_readw(qts, SUMEXT), ==, sumext); \
}

    TEST_MUL(mul24, uint32_t)
        qtest_writew(qts, MPY32L, op1 & 0xffff);
        qtest_writeb(qts, MPY32H, op1 >> 16);
        qtest_writew(qts, OP2L, op2 & 0xffff);
        qtest_writeb(qts, OP2H, op2 >> 16);
    CHECK_MUL((uint64_t)op1 * op2, 0)

    TEST_MUL(mul24, int32_t)
        op1 = sextract32(op1, 0, 24);
        op2 = sextract32(op2, 0, 24);
        int64_t exp = (int64_t)op1 * op2;

        qtest_writew(qts, MPYS32L, op1 & 0xffff);
        qtest_writeb(qts, MPYS32H, op1 >> 16);
        qtest_writew(qts, OP2L, op2 & 0xffff);
        qtest_writeb(qts, OP2H, op2 >> 16);
    CHECK_MUL(exp, exp < 0 ? 0xffff : 0)

    TEST_MUL(mul32, uint32_t)
        qtest_writew(qts, MPY32L, op1 & 0xffff);
        qtest_writew(qts, MPY32H, op1 >> 16);
        qtest_writew(qts, OP2L, op2 & 0xffff);
        qtest_writew(qts, OP2H, op2 >> 16);
    CHECK_MUL((uint64_t)op1 * op2, 0)

    TEST_MUL(mul32, int32_t)
        int64_t exp = (int64_t)op1 * op2;

        qtest_writew(qts, MPYS32L, op1 & 0xffff);
        qtest_writew(qts, MPYS32H, op1 >> 16);
        qtest_writew(qts, OP2L, op2 & 0xffff);
        qtest_writew(qts, OP2H, op2 >> 16);
    CHECK_MUL(exp, exp < 0 ? 0xffff : 0)

#define TEST_MAC(mul, type) \
    for (j = 0; j < ARRAY_SIZE(mac64); j++) { \
        printf("MAC %016lx\n", mac64[j]); \
        uint64_t acc = mac64[j]; \
        TEST_MUL(mul, type) \
            qtest_writew(qts, RES0, acc & 0xffff); \
            qtest_writew(qts, RES1, (acc >> 16) & 0xffff); \
            qtest_writew(qts, RES2, (acc >> 32) & 0xffff); \
            qtest_writew(qts, RES3, acc >> 48);

#define CHECK_MAC(exp, sumext) CHECK_MUL(exp, sumext) }

    TEST_MAC(mul8, uint8_t)
        uint64_t exp = (uint64_t)op1 * op2 + acc;

        qtest_writeb(qts, MAC, op1);
        qtest_writeb(qts, OP2, op2);
    CHECK_MAC(exp, (uint32_t)exp < (uint32_t)acc)

    TEST_MAC(mul8, int8_t)
        int64_t exp = (int64_t)op1 * op2 + acc;

        qtest_writeb(qts, MACS, op1);
        qtest_writew(qts, MACS, (int16_t)(int8_t)qtest_readb(qts, MACS));
        qtest_writew(qts, OP2, (int16_t)op2);
    CHECK_MAC(exp, (int32_t)exp < 0 ? 0xffff : 0)

    TEST_MAC(mul16, uint16_t)
        uint64_t exp = (uint64_t)op1 * op2 + acc;

        qtest_writew(qts, MAC, op1);
        qtest_writew(qts, OP2, op2);
    CHECK_MAC(exp, (uint32_t)exp < (uint32_t)acc)

    TEST_MAC(mul16, int16_t)
        int64_t exp = (int64_t)op1 * op2 + acc;

        qtest_writew(qts, MACS, op1);
        qtest_writew(qts, OP2, op2);
    CHECK_MAC(exp, (int32_t)exp < 0 ? 0xffff : 0)

    TEST_MAC(mul24, uint32_t)
        uint64_t exp = (uint64_t)op1 * op2 + acc;

        qtest_writew(qts, MAC32L, op1 & 0xffff);
        qtest_writeb(qts, MAC32H, op1 >> 16);
        qtest_writew(qts, OP2L, op2 & 0xffff);
        qtest_writeb(qts, OP2H, op2 >> 16);
    CHECK_MAC(exp, exp < acc)

    TEST_MAC(mul24, int32_t)
        op1 = sextract32(op1, 0, 24);
        op2 = sextract32(op2, 0, 24);
        int64_t exp = (int64_t)op1 * op2 + acc;

        qtest_writew(qts, MACS32L, op1 & 0xffff);
        qtest_writeb(qts, MACS32H, op1 >> 16);
        qtest_writew(qts, OP2L, op2 & 0xffff);
        qtest_writeb(qts, OP2H, op2 >> 16);
    CHECK_MAC(exp, exp < 0 ? 0xffff : 0)

    TEST_MAC(mul32, uint32_t)
        uint64_t exp = (uint64_t)op1 * op2 + acc;

        qtest_writew(qts, MAC32L, op1 & 0xffff);
        qtest_writew(qts, MAC32H, op1 >> 16);
        qtest_writew(qts, OP2L, op2 & 0xffff);
        qtest_writew(qts, OP2H, op2 >> 16);
    CHECK_MAC(exp, exp < acc)

    TEST_MAC(mul32, int32_t)
        int64_t exp = (int64_t)op1 * op2 + acc;

        qtest_writew(qts, MACS32L, op1 & 0xffff);
        qtest_writew(qts, MACS32H, op1 >> 16);
        qtest_writew(qts, OP2L, op2 & 0xffff);
        qtest_writew(qts, OP2H, op2 >> 16);
    CHECK_MAC(exp, exp < 0 ? 0xffff : 0)

#undef TEST_MUL
#undef TEST_MAC
#undef CHECK_MUL
#undef CHECK_MAC

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

#define add_mcu_func(name, mcu, fn) \
    qtest_add_data_func("/" mcu name, mcu, fn)
    add_mcu_func("/mpy", "msp430f449", test_mpy);
    add_mcu_func("/mpy", "msp430f1611", test_mpy);
    add_mcu_func("/mpy", "msp430f4794", test_mpy);
    add_mcu_func("/mpy32", "msp430f4794", test_mpy32);

    return g_test_run();
}
