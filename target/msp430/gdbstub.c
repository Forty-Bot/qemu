/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "gdbstub/helpers.h"

int msp430_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    MSP430CPU *cpu = MSP430_CPU(cs);

    if (n == R_SR) {
        return gdb_get_reg32(mem_buf, msp430_cpu_get_sr(&cpu->env));
    }

    if (n < NUM_REGS) {
        return gdb_get_reg32(mem_buf, cpu->env.regs[n]);
    }

    return 0;
}

int msp430_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    MSP430CPU *cpu = MSP430_CPU(cs);

    if (n > NUM_REGS)
        return 1;

    if (n == R_SR) {
        msp430_cpu_set_sr(&cpu->env, ldl_p(mem_buf));
    } else {
        cpu->env.regs[n] = ldl_p(mem_buf);
    }

    return 2;
}
