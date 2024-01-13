/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#ifndef TAP_H
#define TAP_H

extern unsigned next_test;

void plan(unsigned tests);
void pass(void);
void fail(const char *reason);
void failf(const char *fmt, ...);
void skip(void);
__attribute__((__noreturn__)) void bail(void);

#define ok(cond) ({ \
    int _cond = !!(cond); \
    if (_cond) { \
        pass(); \
    } else { \
        fail(#cond); \
    } \
    _cond; \
})

#define is(expr1, expr2) ({ \
    typeof(expr1) _val1 = (expr1); \
    typeof(expr2) _val2 = (expr2); \
    if (_val1 == _val2) { \
        pass(); \
    } else { \
        failf(#expr1 " != " #expr2 ": expected %x, got %x\n", _val1, _val2); \
    } \
    _val1 == _val2; \
})

#endif /* TAP_H */
