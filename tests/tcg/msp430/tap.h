/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#ifndef TAP_H
#define TAP_H

void plan(unsigned tests);
void pass(void);
void fail(const char *reason);
void failf(const char *fmt, ...);
void skip(void);
__attribute__((__noreturn__)) void bail(void);

#endif /* TAP_H */
