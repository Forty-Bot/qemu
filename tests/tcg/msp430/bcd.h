/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#ifndef BCD_H
#define BCD_H

unsigned long itod(unsigned int u);
unsigned long long ltod(unsigned long u);
char *dtoa10(size_t n, char *buf, unsigned int d, ...);
char *strnffnz(char *s, unsigned int n);
char *itoa10(unsigned int n, char buf[9]);
char *ltoa10(unsigned long n, char buf[13]);
char *itoa16(unsigned int n, char buf[5]);
char *ltoa16(unsigned long n, char buf[9]);

#endif /* BCD_H */
