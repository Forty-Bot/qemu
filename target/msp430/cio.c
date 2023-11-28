/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "elf.h"
#include "exec/helper-proto.h"
#include "gdbstub/syscalls.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "semihosting/semihost.h"
#include "semihosting/uaccess.h"
#include "semihosting/syscalls.h"

#define DTOPEN         0xF0
#define DTCLOSE        0xF1
#define DTREAD         0xF2
#define DTWRITE        0xF3
#define DTLSEEK        0xF4
#define DTUNLINK       0xF5
#define DTGETENV       0xF6
#define DTRENAME       0xF7
#define DTGETTIME      0xF8
#define DTGETCLK       0xF9
#define DTGETTIME64    0xFA
#define DTSYNC         0xFF

#define CIO_WRONLY      0x0001
#define CIO_RDWR        0x0002
#define CIO_APPEND      0x0008
#define CIO_CREAT       0x0200
#define CIO_TRUNC       0x0400
#define CIO_BINARY      0x8000
#define CIO_GDB_MASK   (CIO_WRONLY | CIO_RDWR | CIO_APPEND | CIO_CREAT | \
                        CIO_TRUNC)

struct request {
    int16_t length;
    uint8_t command;
    union {
        struct {
            int16_t mode;
            int16_t flags;
        } QEMU_PACKED open;
        struct {
            int16_t dev_fd;
        } QEMU_PACKED close;
        struct {
            int16_t dev_fd;
            int16_t in_length;
        } QEMU_PACKED rw;
        struct {
            int16_t dev_fd;
            int32_t offset;
            int16_t origin;
        } QEMU_PACKED lseek;
    } QEMU_PACKED;
} QEMU_PACKED;

G_STATIC_ASSERT(sizeof(struct request) == 11);

struct response {
    int16_t length;
    union {
        int16_t result16;
        int32_t result32;
        uint32_t time32;
        int64_t time64;
    } QEMU_PACKED;
} QEMU_PACKED;

G_STATIC_ASSERT(sizeof(struct response) == 10);

G_NORETURN void helper_cio_exit(void)
{
    gdb_exit(0);
    exit(0);
}

static int cio_read_req(CPUState *cs, struct request *req)
{
    MSP430CPU *cpu = MSP430_CPU(cs);
    CPUMSP430State *env = &cpu->env;
    int ret;

    ret = cpu_memory_rw_debug(cs, env->cio_buf, req, sizeof(*req), false);
    if (ret) {
        warn_report("cio: could not read request from _CIOBUF_");
    }

    return ret;
}

static void cio_write_res(CPUState *cs, struct response *res)
{
    MSP430CPU *cpu = MSP430_CPU(cs);
    CPUMSP430State *env = &cpu->env;

    if (cpu_memory_rw_debug(cs, env->cio_buf, res, sizeof(*res), true)) {
        warn_report("cio: could not write response to _CIOBUF_");
    }
}

static void cio_complete16(CPUState *cs, uint64_t ret, int err)
{
    struct response res = {
        .length = 0,
    };

    if (err) {
        res.result16 = -1;
    } else {
        assert(ret < INT16_MAX);
        res.result16 = cpu_to_le16(ret);
    }

    cio_write_res(cs, &res);
}

static void cio_complete_dummy(CPUState *cs, uint64_t ret, int err)
{
}

static void cio_complete_open(CPUState *cs, uint64_t ret, int err)
{
    struct response res = {
        .length = 0,
    };

    if (err) {
        res.result16 = -1;
    } else if (ret > INT16_MAX) {
        semihost_sys_close(cs, cio_complete_dummy, ret);
        res.result16 = -1;
    } else {
        res.result16 = cpu_to_le16(ret);
    }

    cio_write_res(cs, &res);
}

static void cio_complete_read(CPUState *cs, uint64_t ret, int err)
{
    struct response res = { };

    if (err) {
        res.result16 = -1;
        res.length = 0;
    } else {
        /*
         * No need to check for overflow, since we can't read more than we
         * requested
         */
        res.result16 = tswap16(ret);
        res.length = cpu_to_le16(ret);
    }

    cio_write_res(cs, &res);
}

static void cio_complete_lseek(CPUState *cs, uint64_t ret, int err)
{
    struct response res = {
        .length = 0,
    };

    if (err) {
        res.result32 = -1;
    } else if (ret > INT32_MAX) {
        struct request req;

        assert(!cio_read_req(cs, &req));
        /* Seek again, this time to the maximum value we support */
        semihost_sys_lseek(cs, cio_complete_lseek,
                           le16_to_cpu(req.lseek.dev_fd), INT32_MAX,
                           GDB_SEEK_SET);
        return;
    } else {
        res.result32 = cpu_to_le16(ret);
    }

    cio_write_res(cs, &res);
}

void helper_cio_io(CPUMSP430State *env)
{
    CPUState *cs = env_cpu(env);
    static bool warned_ciobuf = false;
    struct request req;
    int16_t length;

    if (!env->cio_buf) {
        warn_report_once_cond(&warned_ciobuf,
                              "cio: C$$IO$$ is defined but _CIOBUF_ isn't");
        return;
    }

    if (cio_read_req(cs, &req)) {
        return;
    }

    length = tswap16(req.length);
    switch (req.command) {
    case DTOPEN:
        if (length < 0) {
            cio_complete16(cs, -1, -1);
            return;
        }

        semihost_sys_open(cs, cio_complete_open, env->cio_buf + sizeof(req),
                          req.length, le16_to_cpu(req.open.flags) & CIO_GDB_MASK,
                          le16_to_cpu(req.open.mode));
        return;
    case DTCLOSE:
        semihost_sys_close(cs, cio_complete16, le16_to_cpu(req.close.dev_fd));
        return;
    case DTREAD:
        length = le16_to_cpu(req.rw.in_length);
        if (length < 0) {
            cio_complete16(cs, -1, -1);
            return;
        }

        semihost_sys_read(cs, cio_complete_read, le16_to_cpu(req.rw.dev_fd),
                          env->cio_buf + sizeof(struct response), length);
        return;
    case DTWRITE:
        if (length < 0) {
            cio_complete16(cs, -1, -1);
            return;
        }

        semihost_sys_write(cs, cio_complete16, le16_to_cpu(req.rw.dev_fd),
                           env->cio_buf + sizeof(req), length);
        return;
    case DTLSEEK:
        semihost_sys_lseek(cs, cio_complete_lseek,
                           le16_to_cpu(req.lseek.dev_fd),
                           le32_to_cpu(req.lseek.offset),
                           le16_to_cpu(req.lseek.origin));
        return;
    case DTUNLINK: {
        ssize_t pathlen = target_strlen(env->cio_buf + sizeof(req));

        if (pathlen < 0) {
            cio_complete16(cs, -1, -1);
            return;
        }

        semihost_sys_remove(cs, cio_complete16, env->cio_buf + sizeof(req),
                            pathlen);
        return;
    }
    case DTRENAME: {
        ssize_t oldlen = target_strlen(env->cio_buf + sizeof(req));

        if (oldlen < 0 || oldlen + 2 > length) {
            cio_complete16(cs, -1, -1);
        }

        semihost_sys_rename(cs, cio_complete16, env->cio_buf + sizeof(req),
                            oldlen, env->cio_buf + sizeof(req) + oldlen + 1,
                            length - oldlen - 2);
        return;
    }
    case DTGETENV: {
        struct response res = {
            .length = 0,
        };
        char val = '\0';

        /*
         * This seems like a fundamentally bad idea. Just return an empty
         * string.
         */
        if (cpu_memory_rw_debug(cs, env->cio_buf + sizeof(res), &val,
                                sizeof(val), true)) {
            warn_report("msp430: cio: could not write data to _CIOBUF_");
        }
        cio_write_res(cs, &res);
        return;
    }
    /*
     * We don't bother with semihost_sys_gettimeofday for these time calls
     * since it writes directly to guest memory in the wrong format (and the
     * wrong epoch). These calls don't have any state (like file descriptors),
     * so GDB won't miss 'em.
     */
    case DTGETTIME: {
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_HOST);
        struct response res = {
            .length = 0,
        };

        now /= NANOSECONDS_PER_SECOND;
        /* Seconds between 1900-01-01T00:00-06:00 and 1970-01-01T00:00+00:00 */
        now += 2208967200000;
        res.time32 = cpu_to_le32(now);
        cio_write_res(cs, &res);
        return;
    }
    case DTGETTIME64: {
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_HOST);
        struct response res = {
            .length = 0,
        };

        now /= NANOSECONDS_PER_SECOND;
        res.time64 = cpu_to_le32(now);
        cio_write_res(cs, &res);
        return;
    }
    case DTGETCLK: {
        /*
         * Pretend the processor is running at 1 MHz.
         * TODO: Calculate this based on mclk? Does anyone care?
         */
        int64_t now = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
        struct response res = {
            .length = 0,
            .time32 = cpu_to_le32(now),
        };

        cio_write_res(cs, &res);
        return;
    }
    case DTSYNC:
        /* Nothing to do */
        return;
    }

    warn_report("cio: unknown call %02x", req.command);
}

void msp430_cio_symbol_callback(void *opaque, const char *st_name, int st_info,
                                uint64_t st_value, uint64_t st_size)
{
    CPUState *cs = opaque;
    MSP430CPU *cpu = MSP430_CPU(cs);
    CPUMSP430State *env = &cpu->env;
    uint64_t *val = NULL;

    if (*st_name != 'C' && *st_name != '_') {
        return;
    }

    if (ELF_ST_BIND(st_info) != STB_GLOBAL) {
        return;
    }

    if (!semihosting_enabled(false)) {
        return;
    }

    if (!strcmp("C$$EXIT", st_name)) {
        val = &env->cio_exit;
    } else if (!strcmp("C$$IO$$", st_name)) {
        val = &env->cio_io;
    } else if (!strcmp("_CIOBUF_", st_name)) {
        val = &env->cio_buf;
    }

    if (val) {
        if (*val && *val != st_value) {
            warn_report("cio: ignoring redefinition of %s from %04" PRIx64 " to %04" PRIx64 ")\n",
                        st_name, *val, st_value);
        } else {
            *val = st_value;
        }
    }
}
