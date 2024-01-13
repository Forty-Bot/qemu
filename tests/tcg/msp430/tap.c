/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "tap.h"

unsigned next_test;

void plan(unsigned tests)
{
    next_test = 1;
    printf("1..%u\n", tests);
}

void pass(void)
{
    printf("ok %u\n", next_test++);
}

void fail(const char *reason)
{
    printf("not ok %u - %s\n", next_test++, reason);
}

void failf(const char *fmt, ...)
{
    va_list ap;

    printf("not ok %u - ", next_test++);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

void skip(void)
{
    printf("ok %u # SKIP\n", next_test++);
}

__attribute__((__noreturn__)) void bail(void)
{
    const char msg[] = "Bail out!\n";

    write(1, msg, sizeof(msg) - 1);
    exit(1);
}
