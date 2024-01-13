/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include <string.h>

#include "bcd.h"
#include "tap.h"

int main(void)
{
    char buf[13];

    plan(246);

#define test(n) do { \
    const char *exp = #n; \
    const char *got = itoa16(n, buf); \
    if (strcmp(exp + 2, got)) \
        failf("expected %s, got %s\n", exp + 2, got); \
    else \
        pass(); \
} while (0)

    test(0x0);
    test(0x1111);
    test(0x2222);
    test(0x3333);
    test(0x4444);
    test(0x5555);
    test(0x6666);
    test(0x7777);
    test(0x8888);
    test(0x9999);
    test(0xaaaa);
    test(0xbbbb);
    test(0xcccc);
    test(0xdddd);
    test(0xeeee);
    test(0xffff);

    test(0x1);
    test(0x2);
    test(0x3);
    test(0x4);
    test(0x7);
    test(0x8);
    test(0xf);
    test(0x10);
    test(0x1f);
    test(0x20);
    test(0x3f);
    test(0x40);
    test(0x7f);
    test(0x80);
    test(0xff);
    test(0x100);
    test(0x1ff);
    test(0x200);
    test(0x3ff);
    test(0x400);
    test(0x7ff);
    test(0x800);
    test(0xfff);
    test(0x1000);
    test(0x1fff);
    test(0x2000);
    test(0x3fff);
    test(0x4000);
    test(0x7fff);
    test(0x8000);

#undef test
#define test(n) do { \
    const char *exp = #n; \
    const char *got = ltoa16(n, buf); \
    if (strcmp(exp + 2, got)) \
        failf("expected %s, got %s\n", exp + 2, got); \
    else \
        pass(); \
} while (0)

    test(0x0);
    test(0x11111111);
    test(0x22222222);
    test(0x33333333);
    test(0x44444444);
    test(0x55555555);
    test(0x66666666);
    test(0x77777777);
    test(0x88888888);
    test(0x99999999);
    test(0xaaaaaaaa);
    test(0xbbbbbbbb);
    test(0xcccccccc);
    test(0xdddddddd);
    test(0xeeeeeeee);
    test(0xffffffff);

    test(0x1);
    test(0x2);
    test(0x3);
    test(0x4);
    test(0x7);
    test(0x8);
    test(0xf);
    test(0x10);
    test(0x1f);
    test(0x20);
    test(0x3f);
    test(0x40);
    test(0x7f);
    test(0x80);
    test(0xff);
    test(0x100);
    test(0x1ff);
    test(0x200);
    test(0x3ff);
    test(0x400);
    test(0x7ff);
    test(0x800);
    test(0xfff);
    test(0x1000);
    test(0x1fff);
    test(0x2000);
    test(0x3fff);
    test(0x4000);
    test(0x7fff);
    test(0x8000);
    test(0xffff);
    test(0x10000);
    test(0x1ffff);
    test(0x20000);
    test(0x3ffff);
    test(0x40000);
    test(0x7ffff);
    test(0x80000);
    test(0xfffff);
    test(0x100000);
    test(0x1fffff);
    test(0x200000);
    test(0x3fffff);
    test(0x400000);
    test(0x7fffff);
    test(0x800000);
    test(0xffffff);
    test(0x1000000);
    test(0x1ffffff);
    test(0x2000000);
    test(0x3ffffff);
    test(0x4000000);
    test(0x7ffffff);
    test(0x8000000);
    test(0xfffffff);
    test(0x10000000);
    test(0x1fffffff);
    test(0x20000000);
    test(0x3fffffff);
    test(0x40000000);
    test(0x7fffffff);
    test(0x80000000);

#undef test
#define test(n) do { \
    const char *s = itoa10(n, buf); \
    if (strcmp(#n, s)) \
        failf("expected %s, got %s\n", #n, s); \
    else \
        pass(); \
} while (0)

    test(0);
    test(1);
    test(9);
    test(10);
    test(99);
    test(100);
    test(999);
    test(1000);
    test(9999);
    test(10000);

    test(2);
    test(3);
    test(4);
    test(7);
    test(8);
    test(15);
    test(16);
    test(31);
    test(32);
    test(63);
    test(64);
    test(127);
    test(128);
    test(255);
    test(256);
    test(511);
    test(512);
    test(1023);
    test(1024);
    test(2047);
    test(2048);
    test(4095);
    test(4096);
    test(8191);
    test(8192);
    test(16383);
    test(16384);
    test(32767);
    test(32768);
    test(65535);

#undef test
#define test(n) do { \
    const char *s = ltoa10(n, buf); \
    if (strcmp(#n, s)) \
        failf("expected %s, got %s\n", #n, s); \
    else \
        pass(); \
} while (0)

    test(0);
    test(1);
    test(9);
    test(10);
    test(99);
    test(100);
    test(999);
    test(1000);
    test(9999);
    test(10000);
    test(99999);
    test(100000);
    test(999999);
    test(1000000);
    test(9999999);
    test(10000000);
    test(99999999);
    test(100000000);
    test(999999999);
    test(1000000000);

    test(2);
    test(3);
    test(4);
    test(7);
    test(8);
    test(15);
    test(16);
    test(31);
    test(32);
    test(63);
    test(64);
    test(127);
    test(128);
    test(255);
    test(256);
    test(511);
    test(512);
    test(1023);
    test(1024);
    test(2047);
    test(2048);
    test(4095);
    test(4096);
    test(8191);
    test(8192);
    test(16383);
    test(16384);
    test(32767);
    test(32768);
    test(65535);
    test(65536);
    test(131071);
    test(131072);
    test(262143);
    test(262144);
    test(524287);
    test(524288);
    test(1048575);
    test(1048576);
    test(2097151);
    test(2097152);
    test(4194303);
    test(4194304);
    test(8388607);
    test(8388608);
    test(16777215);
    test(16777216);
    test(33554431);
    test(33554432);
    test(67108863);
    test(67108864);
    test(134217727);
    test(134217728);
    test(268435455);
    test(268435456);
    test(536870911);
    test(536870912);
    test(1073741823);
    test(1073741824);
    test(2147483647);
    test(2147483648);
    test(4294967295);

    return 0;
}
