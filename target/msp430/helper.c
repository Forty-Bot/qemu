/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "cpu.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/log.h"
#include "hw/irq.h"

void msp430_cpu_unpack_sr(CPUMSP430State *env, uint16_t sr)
{
    env->v = !!(sr & R_SR_V);
    env->gie = !!(sr & R_SR_GIE);
    env->n = !!(sr & R_SR_N);
    env->z = !!(sr & R_SR_Z);
    env->c = !!(sr & R_SR_C);
}

uint32_t helper_get_sr(CPUMSP430State *env)
{
    return msp430_cpu_pack_sr(env);
}

void helper_set_sr(CPUMSP430State *env, uint32_t sr)
{
    msp430_cpu_unpack_sr(env, sr);
}

void helper_unsupported(CPUMSP430State *env)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_DEBUG;
    if (qemu_loglevel_mask(LOG_UNIMP)) {
        qemu_log("UNSUPPORTED\n");
        cpu_dump_state(cs, stderr, 0);
    }
    cpu_loop_exit(cs);
}

void msp430_cpu_do_interrupt(CPUState *cs)
{
    MSP430CPU *cpu = MSP430_CPU(cs);
    CPUMSP430State *env = &cpu->env;

    cpu_stw_data(env, --env->regs[R_SP], env->regs[R_PC]);
    cpu_stw_data(env, --env->regs[R_SP], env->regs[R_SR]);
    helper_set_sr(env, 0);
    env->regs[R_PC] = cpu_ldsw_data(env, 0xfffe - (clz64(env->irq) << 1));
}

bool msp430_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    MSP430CPU *cpu = MSP430_CPU(cs);
    CPUMSP430State *env = &cpu->env;

    if (msp430_cpu_has_work(cs)) {
        msp430_cpu_do_interrupt(cs);

        env->irq &= BIT(63 - clz64(env->irq)) - 1;
        if (!env->irq) {
            cs->interrupt_request &= ~CPU_INTERRUPT_HARD;
        }
        return true;
    }

    return false;
}

hwaddr msp430_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    return addr;
}
