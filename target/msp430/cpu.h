/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_CPU_H
#define MSP430_CPU_H

#include "cpu-qom.h"
#include "exec/cpu-defs.h"

#ifdef CONFIG_USER_ONLY
#error "MSP430 does not support user mode"
#endif

#define MSP430_CPU_TYPE_SUFFIX ("-" TYPE_MSP430_CPU)
#define MSP430_CPU_TYPE_NAME(model) (model MSP430_CPU_TYPE_SUFFIX)
#define CPU_RESOLVING_TYPE TYPE_MSP430_CPU

#define NUM_REGS 16
#define R_PC 0
#define R_SP 1
#define R_SR 2
#define R_CG 3

#define R_SR_V      BIT(8)
#define R_SR_SCG1   BIT(7)
#define R_SR_SCG0   BIT(6)
#define R_SR_OSCOFF BIT(5)
#define R_SR_CPUOFF BIT(4)
#define R_SR_GIE    BIT(3)
#define R_SR_N      BIT(2)
#define R_SR_Z      BIT(1)
#define R_SR_C      BIT(0)

#define IRQ_RESET   BIT_ULL(63)
#define IRQ_NMI     BIT_ULL(62)

typedef struct CPUArchState {
    uint32_t regs[NUM_REGS];
    uint64_t irq;
    uint32_t v, gie, n, z, c;
} CPUMSP430State;

struct ArchCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUNegativeOffsetState neg;
    CPUMSP430State env;
};

bool msp430_cpu_has_work(CPUState *cs);
void msp430_cpu_do_interrupt(CPUState *cs);
bool msp430_cpu_exec_interrupt(CPUState *cs, int interrupt_request);
hwaddr msp430_cpu_get_phys_page_debug(CPUState *cs, vaddr addr);

void msp430_translate_init(void);
void msp430_cpu_list(void);
void msp430_cpu_unpack_sr(CPUMSP430State *env, uint16_t sr);

int msp430_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n);
int msp430_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n);

#define cpu_list msp430_cpu_list

#include "exec/cpu-all.h"

static inline void cpu_get_tb_cpu_state(CPUMSP430State *env, vaddr *pc,
                                        uint64_t *cs_base, uint32_t *flags)
{
    *pc = env->regs[R_PC];
    *cs_base = 0;
    *flags = 0;
}

static inline int cpu_mmu_index(CPUMSP430State *env, bool ifetch)
{
    return 0;
}

static inline uint16_t msp430_cpu_pack_sr(CPUMSP430State *env)
{
    uint16_t sr;

    sr = env->v ? R_SR_V : 0;
    sr |= env->gie ? R_SR_GIE : 0;
    sr |= env->n ? R_SR_N : 0;
    sr |= env->z ? R_SR_Z : 0;
    sr |= env->c ? R_SR_C : 0;
    return sr;
}

#endif /* MSP430_CPU_H */
