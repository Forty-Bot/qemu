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

uint16_t msp430_cpu_get_sr(CPUMSP430State *env)
{
    uint16_t sr = env->regs[R_SR];

    sr &= ~(R_SR_V | R_SR_GIE | R_SR_N | R_SR_Z | R_SR_C);
    sr |= env->v ? R_SR_V : 0;
    sr |= env->gie ? R_SR_GIE : 0;
    sr |= env->n ? R_SR_N : 0;
    sr |= env->z ? R_SR_Z : 0;
    sr |= env->c ? R_SR_C : 0;
    return sr;
}

uint32_t helper_get_sr(CPUMSP430State *env)
{
    return msp430_cpu_get_sr(env);
}

void msp430_cpu_set_sr(CPUMSP430State *env, uint16_t sr)
{
    env->regs[R_SR] = sr;
    env->v = !!(sr & R_SR_V);
    env->gie = !!(sr & R_SR_GIE);
    env->n = !!(sr & R_SR_N);
    env->z = !!(sr & R_SR_Z);
    env->c = !!(sr & R_SR_C);

    qemu_set_irq(env->cpuoff, !!(sr & R_SR_CPUOFF));
    qemu_set_irq(env->oscoff, !!(sr & R_SR_OSCOFF));
    qemu_set_irq(env->scg[0], !!(sr & R_SR_SCG0));
    qemu_set_irq(env->scg[1], !!(sr & R_SR_SCG1));
}

void helper_set_sr(CPUMSP430State *env, uint32_t sr)
{
    msp430_cpu_set_sr(env, sr);
}

void helper_unsupported(CPUMSP430State *env, uint32_t insn)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_HLT;
    if (qemu_loglevel_mask(LOG_UNIMP)) {
        qemu_log("Unsupported instruction %04x\n", insn);
        cpu_dump_state(cs, stderr, 0);
    }
    cpu_loop_exit(cs);
}

void msp430_cpu_do_interrupt(CPUState *cs)
{
    MSP430CPU *cpu = MSP430_CPU(cs);
    CPUMSP430State *env = &cpu->env;
    int bit, irq;

    env->regs[R_SP] -= 2;
    cpu_stw_data(env, env->regs[R_SP], env->regs[R_PC]);
    env->regs[R_SP] -= 2;
    cpu_stw_data(env, env->regs[R_SP], msp430_cpu_get_sr(env));
    msp430_cpu_set_sr(env, 0);

    bit = clz32(env->irq);
    irq = NUM_IRQS - bit - 1;
    qemu_irq_raise(env->ack[irq]);
    env->regs[R_PC] = cpu_lduw_data(env, 0xfffe - (bit << 1));
    qemu_log_mask(CPU_LOG_INT, "interrupt %d raised\n", irq);
}

bool msp430_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (msp430_cpu_has_work(cs)) {
        msp430_cpu_do_interrupt(cs);
        return true;
    }

    return false;
}

hwaddr msp430_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    return addr;
}
