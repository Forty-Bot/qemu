// SPDX-License-Identifier: LGPL-2.1+
/*
 * Tiny printf
 *
 * Copied from U-Boot's lib/tiny-printf.c
 * Copied from:
 * http://www.sparetimelabs.com/printfrevisited/printfrevisited.php
 *
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 * Copyright (C) 2004,2008  Kustaa Nyholm
 */

#define _GNU_SOURCE
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bcd.h"

struct printf_info {
    int (*write)(struct printf_info *restrict info, const char *restrict s,
                 size_t size);
    union {
        FILE *stream;
        int fd;
        struct {
            char *str;
            size_t size;
        };
    };
};

static int _vprintf(struct printf_info *restrict info,
                    const char *restrict fmt, va_list va)
{
    char ch;
    char *s;
    char buf[15];
    size_t len;
    int written = 0;

#define out(s, len) do { \
    size_t _len = (len); \
    int err = info->write(info, s, _len); \
    if (err) \
        return err; \
    written += _len; \
} while (0)

    while (true) {
        bool alt = false;
        bool islong = false;
        const char *spec;

        spec = strchrnul(fmt, '%');
        out(fmt, spec - fmt);
        if (!*spec++)
            return written;
        fmt = spec;

        ch = *(fmt++);
        if (ch == '#') {
            ch = *(fmt++);
            alt = true;
        }

        if (ch == 'l') {
            ch = *(fmt++);
            islong = true;
        }

        switch (ch) {
        case '\0':
            return written;
        case 'd':
        case 'i':
        case 'u':
            if (islong) {
                unsigned long n = va_arg(va, unsigned long);

                s = buf + sizeof(buf) - 13;
                if (ch != 'u' && (long)n < 0) {
                    n = -(long)n;
                    s = ltoa10(n, s);
                    s -= 1;
                    s[0] = '-';
                } else {
                    s = ltoa10(n, s);
                }

                len = buf + sizeof(buf) - s - 1;
            } else {
                unsigned int n = va_arg(va, unsigned int);

                s = buf + sizeof(buf) - 9;
                if (ch != 'u' && (int)n < 0) {
                    n = -(int)n;
                    s = itoa10(n, s);
                    s -= 1;
                    s[0] = '-';
                } else {
                    s = itoa10(n, s);
                }

                len = buf + sizeof(buf) - s - 1;
            }
            break;
        case 'p':
            islong = sizeof(void *) == sizeof(long);
            /* fallthrough */
        case 'x':
            if (islong)
                s = ltoa16(va_arg(va, unsigned long), buf + sizeof(buf) - 9);
            else
                s = itoa16(va_arg(va, unsigned int), buf + sizeof(buf) - 5);

            len = buf + sizeof(buf) - s - 1;
            if (alt && *s != '0') {
                s -= 2;
                s[0] = '0';
                s[1] = 'x';
                len += 2;
            }
            break;
        default:
        case 'c':
            ch = va_arg(va, int);
            s = &ch;
            len = 1;
            break;
        case 's':
            s = va_arg(va, char*);
            len = strlen(s);
            break;
        case '%':
            s = &ch;
            len = 1;
            break;
        }

        out(s, len);
    }

    return written;
}

static int dwrite(struct printf_info *restrict info, const char *restrict s,
                  size_t size)
{
    do {
        ssize_t err = write(info->fd, s, size);

        if (err < 0)
            return err;
        size -= err;
    } while (size);

    return 0;
}

int vprintf(const char *restrict fmt, va_list va)
{
    struct printf_info info;

    info.write = dwrite;
    info.fd = 1;
    return _vprintf(&info, fmt, va);
}

int printf(const char *restrict fmt, ...)
{
    struct printf_info info;
    va_list va;
    int ret;

    info.write = dwrite;
    info.fd = 1;
    va_start(va, fmt);
    ret = _vprintf(&info, fmt, va);
    va_end(va);

    return ret;
}

int vdprintf(int fd, const char *restrict fmt, va_list va)
{
    struct printf_info info;

    info.write = dwrite;
    info.fd = fd;
    return _vprintf(&info, fmt, va);
}

int dprintf(int fd, const char *restrict fmt, ...)
{
    struct printf_info info;
    va_list va;
    int ret;

    info.write = dwrite;
    info.fd = fd;
    va_start(va, fmt);
    ret = _vprintf(&info, fmt, va);
    va_end(va);

    return ret;
}

static int _fwrite(struct printf_info *restrict info, const char *restrict s,
                   size_t size)
{
    if (fwrite(s, 1, size, info->stream) != size)
        return -1;
    return size;
}

int vfprintf(FILE *restrict stream, const char *restrict fmt, va_list va)
{
    struct printf_info info;

    info.write = _fwrite;
    info.stream = stream;
    return _vprintf(&info, fmt, va);
}

int fprintf(FILE *restrict stream, const char *restrict fmt, ...)
{
    struct printf_info info;
    va_list va;
    int ret;

    info.write = _fwrite;
    info.stream = stream;
    va_start(va, fmt);
    ret = _vprintf(&info, fmt, va);
    va_end(va);

    return ret;
}

static int swrite(struct printf_info *restrict info, const char *restrict s,
                  size_t size)
{
    memcpy(info->str, s, size);
    info->str += size;
    return 0;
}

int vsprintf(char *restrict str, const char *restrict fmt, va_list va)
{
    struct printf_info info;
    int ret;

    info.write = swrite;
    info.str = str;
    ret = _vprintf(&info, fmt, va);
    *info.str = '\0';

    return ret;
}

int sprintf(char *restrict str, const char *restrict fmt, ...)
{
    struct printf_info info;
    va_list va;
    int ret;

    va_start(va, fmt);
    info.write = swrite;
    info.str = str;
    ret = _vprintf(&info, fmt, va);
    va_end(va);
    *info.str = '\0';

    return ret;
}

static int snwrite(struct printf_info *restrict info, const char *restrict s,
                   size_t size)
{
    if (size > info->size)
        size = info->size;
    if (info->str)
        info->str = memcpy(info->str, s, size);
    info->str += size;
    return 0;
}

int vsnprintf(char *restrict str, size_t size, const char *restrict fmt,
              va_list va)
{
    struct printf_info info;
    int ret;

    info.write = snwrite;
    info.str = str;
    info.size = size;
    ret = _vprintf(&info, fmt, va);
    *info.str = '\0';

    return ret;
}

int snprintf(char *restrict str, size_t size, const char *restrict fmt, ...)
{
    struct printf_info info;
    va_list va;
    int ret;

    va_start(va, fmt);
    info.write = snwrite;
    info.str = str;
    info.size = size;
    ret = _vprintf(&info, fmt, va);
    va_end(va);
    *info.str = '\0';

    return ret;
}
