/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023-24 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "hw/intc/intc.h"
#include "hw/qdev-clock.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "sysemu/reset.h"

static void msp430_cpu_set_irq(void *opaque, int irq, int level)
{
    MSP430CPU *cpu = opaque;
    CPUMSP430State *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    if (level) {
        env->irq |= BIT(irq);
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        env->irq &= ~BIT(irq);
        if (env->irq == 0) {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
}

static void msp430_cpu_mclk_update(void *opaque, ClockEvent event)
{
    CPUMSP430State *env = opaque;
    CPUState *cs = env_cpu(env);

    if (clock_is_enabled(env->mclk)) {
        if (cs->halted) {
            cs->halted = 0;
            qemu_cpu_kick(cs);
        }
    } else if (!cs->halted) {
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
        cpu_exit(cs);
    }
}

static void msp430_cpu_init(Object *obj)
{
    MSP430CPU *cpu = MSP430_CPU(obj);
    CPUMSP430State *env = &cpu->env;

    env->mclk = qdev_init_clock_in(DEVICE(cpu), "mclk", msp430_cpu_mclk_update,
                                   env, ClockUpdate);
    qdev_init_gpio_in(DEVICE(cpu), msp430_cpu_set_irq, NUM_IRQS);
    qdev_init_gpio_out_named(DEVICE(cpu), env->ack, "ack", NUM_IRQS);
    qdev_init_gpio_out_named(DEVICE(cpu), &env->cpuoff, "cpuoff", 1);
    qdev_init_gpio_out_named(DEVICE(cpu), &env->oscoff, "oscoff", 1);
    qdev_init_gpio_out_named(DEVICE(cpu), env->scg, "scg", 2);
}

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps msp430_sysemu_ops = {
    .get_phys_page_debug = msp430_cpu_get_phys_page_debug,
};

static void msp430_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->print_insn = msp430_print_insn;
}

#include "hw/core/tcg-cpu-ops.h"

static void msp430_cpu_synchronize_from_tb(CPUState *cs,
                                           const TranslationBlock *tb)
{
    MSP430CPU *cpu = MSP430_CPU(cs);

    cpu->env.regs[R_PC] = tb->pc;
}

static void msp430_restore_state_to_opc(CPUState *cs,
                                        const TranslationBlock *tb,
                                        const uint64_t *data)
{
    MSP430CPU *cpu = MSP430_CPU(cs);

    cpu->env.regs[R_PC] = data[0];
    cpu->env.pending_gie = data[1];
}

static bool msp430_cpu_tlb_fill(CPUState *cs, vaddr addr, int size,
                                MMUAccessType access_type, int mmu_idx,
                                bool probe, uintptr_t retaddr)
{
    uint16_t page = addr & TARGET_PAGE_MASK;

    tlb_set_page(cs, page, page, PAGE_READ | PAGE_WRITE | PAGE_EXEC, mmu_idx,
                 TARGET_PAGE_SIZE);
    return true;
}

static const struct TCGCPUOps msp430_tcg_ops = {
    .initialize = msp430_translate_init,
    .synchronize_from_tb = msp430_cpu_synchronize_from_tb,
    .restore_state_to_opc = msp430_restore_state_to_opc,
    .cpu_exec_interrupt = msp430_cpu_exec_interrupt,
    .tlb_fill = msp430_cpu_tlb_fill,
    .do_interrupt = msp430_cpu_do_interrupt,
};

static void msp430_cpu_reset(void *opaque)
{
    CPUState *cs = opaque;

    cpu_reset(cs);
}

static void msp430_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    MSP430CPUClass *mcc = MSP430_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_register_reset(msp430_cpu_reset, cs);
    qemu_init_vcpu(cs);
    mcc->parent_realize(dev, errp);
}

static void msp430_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    MSP430CPU *cpu = MSP430_CPU(cs);
    MSP430CPUClass *mcc = MSP430_CPU_GET_CLASS(cpu);
    CPUMSP430State *env = &cpu->env;

    if (mcc->parent_phases.hold) {
        mcc->parent_phases.hold(obj, type);
    }

    memset(env->regs, 0, sizeof(env->regs));
    msp430_cpu_set_sr(env, 0);
    env->pending_gie = false;
}

static bool msp430_get_irq_stats(InterruptStatsProvider *obj,
                                 uint64_t **irq_counts, unsigned int *nb_irqs)
{
    MSP430CPU *cpu = MSP430_CPU(obj);
    CPUMSP430State *env = &cpu->env;

    *irq_counts = env->irq_stats;
    *nb_irqs = ARRAY_SIZE(env->irq_stats);
    return true;
}

static ObjectClass *msp430_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;

    oc = object_class_by_name(cpu_model);
    if (object_class_dynamic_cast(oc, TYPE_MSP430_CPU) == NULL ||
        object_class_is_abstract(oc)) {
        oc = NULL;
    }
    return oc;
}

bool msp430_cpu_has_work(CPUState *cs)
{
    MSP430CPU *cpu = MSP430_CPU(cs);
    CPUMSP430State *env = &cpu->env;

    if (!(cs->interrupt_request & CPU_INTERRUPT_HARD)) {
        return false;
    }

    if (env->irq & (BIT(IRQ_RESET) | BIT(IRQ_NMI))) {
        return true;
    }

    return env->gie && !env->pending_gie;
}


static int msp430_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return 0;
}

static int msp430_cpu_debug_read(bfd_vma memaddr, bfd_byte *myaddr, int length,
                                  struct disassemble_info *info)
{
    CPUState *cs = info->application_data;

    if (cpu_memory_rw_debug(cs, memaddr, myaddr, length, 0)) {
        return EIO;
    }

    return 0;
}

static void msp430_cpu_debug_error(int status, bfd_vma memaddr,
                                   struct disassemble_info *info)
{
    /* We don't care */
}

static void msp430_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    MSP430CPU *cpu = MSP430_CPU(cs);
    CPUMSP430State *env = &cpu->env;
    int i;

    qemu_fprintf(f, "PC:  %04x ", env->regs[R_PC]);
    qemu_fprintf(f, "SP:  %04x ", env->regs[R_SP]);
    qemu_fprintf(f, "SR: [ %c %s %c %c %c ] (%04x)\n",
                 env->v ? 'V' : '-',
                 env->gie ? "GIE" : "-",
                 env->n ? 'N' : '-',
                 env->z ? 'Z' : '-',
                 env->c ? 'C' : '-',
                 msp430_cpu_get_sr(env));
    for (i = R_CG + 1; i < 10; i++) {
        qemu_fprintf(f, "R%d:  %04x ", i, env->regs[i]);
    }
    qemu_fprintf(f, "\n");
    for (; i < NUM_REGS; i++) {
        qemu_fprintf(f, "R%02d: %04x ", i, env->regs[i]);
    }
    qemu_fprintf(f, "\n");

    if (flags & CPU_DUMP_CODE) {
        struct disassemble_info info = {
            .application_data = cs,
            .arch = bfd_arch_obscure,
            .read_memory_func = msp430_cpu_debug_read,
            .memory_error_func = msp430_cpu_debug_error,
            .fprintf_func = qemu_fprintf,
            .stream = f,
            .buffer_vma = env->regs[R_PC],
            .buffer_length = 6, /* Max instruction length */
        };

        qemu_fprintf(f, "=> 0x%04x:  ", env->regs[R_PC]);
        msp430_print_insn(env->regs[R_PC], &info);
        qemu_fprintf(f, "\n");
    }
}

static void msp430_cpu_set_pc(CPUState *cs, vaddr value)
{
    MSP430CPU *cpu = MSP430_CPU(cs);

    cpu->env.regs[R_PC] = value;
}

static vaddr msp430_cpu_get_pc(CPUState *cs)
{
    MSP430CPU *cpu = MSP430_CPU(cs);

    return cpu->env.regs[R_PC];
}

static void msp430_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    MSP430CPUClass *mcc = MSP430_CPU_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    InterruptStatsProviderClass *ic = INTERRUPT_STATS_PROVIDER_CLASS(oc);

    dc->desc = "MSP430 16-bit CPU";
    device_class_set_parent_realize(dc, msp430_cpu_realize, &mcc->parent_realize);

    resettable_class_set_parent_phases(rc, NULL, msp430_cpu_reset_hold, NULL,
                                       &mcc->parent_phases);
    ic->get_statistics = msp430_get_irq_stats;

    cc->class_by_name = msp430_cpu_class_by_name;
    cc->has_work = msp430_cpu_has_work;
    cc->mmu_index = msp430_cpu_mmu_index;
    cc->dump_state = msp430_cpu_dump_state;
    cc->set_pc = msp430_cpu_set_pc;
    cc->get_pc = msp430_cpu_get_pc;
    cc->sysemu_ops = &msp430_sysemu_ops;
    cc->gdb_read_register = msp430_cpu_gdb_read_register;
    cc->gdb_write_register = msp430_cpu_gdb_write_register;
    cc->gdb_num_core_regs = NUM_REGS;
    cc->gdb_core_xml_file = "msp430-cpu.xml";
    cc->disas_set_info = msp430_disas_set_info;
    cc->tcg_ops = &msp430_tcg_ops;
}

static const TypeInfo msp430_cpu_type_info[] = {
    {
        .name = TYPE_MSP430_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(MSP430CPU),
        .instance_align = __alignof(MSP430CPU),
        .instance_init = msp430_cpu_init,
        .class_size = sizeof(MSP430CPUClass),
        .class_init = msp430_cpu_class_init,
        .interfaces = (InterfaceInfo[]) {
            { TYPE_INTERRUPT_STATS_PROVIDER },
            { },
        },
    },
};

DEFINE_TYPES(msp430_cpu_type_info)
