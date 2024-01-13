/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include <minilib.h>
#include <unistd.h>

void __sys_outc(char c)
{
    write(1, &c, 1);
}
