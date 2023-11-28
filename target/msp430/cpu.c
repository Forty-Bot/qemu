/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "cpu.h"
#include "exec/exec-all.h"

static void msp430_cpu_set_irq(void *opaque, int irq, int level)
{
    MSP430CPU *cpu = opaque;
    CPUMSP430State *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    if (level) {
        env->irq |= BIT_ULL(irq);
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        env->irq &= ~BIT_ULL(irq);
        if (env->irq == 0) {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
}

static void msp430_cpu_init(Object *obj)
{
    MSP430CPU *cpu = MSP430_CPU(obj);

    cpu_set_cpustate_pointers(cpu);

    qdev_init_gpio_in(DEVICE(cpu), msp430_cpu_set_irq,
                      sizeof(cpu->env.irq) * CHAR_BIT);
}

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps msp430_sysemu_ops = {
    .get_phys_page_debug = msp430_cpu_get_phys_page_debug,
};

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

    cpu->env.regs[R_PC] = *data;
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
    qemu_init_vcpu(cs);
    cpu_reset(cs);

    mcc->parent_realize(dev, errp);
}

static void msp430_cpu_reset_hold(Object *obj)
{
    CPUState *cs = CPU(obj);
    MSP430CPU *cpu = MSP430_CPU(cs);
    MSP430CPUClass *mcc = MSP430_CPU_GET_CLASS(cpu);
    CPUMSP430State *env = &cpu->env;

    if (mcc->parent_phases.hold) {
        mcc->parent_phases.hold(obj);
    }

    env->v = 0;
    env->gie = 0;
    env->n = 0;
    env->z = 0;
    env->c = 0;

    env->regs[R_PC] = cpu_ldsw_data(env, 0xfffe);
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

    return cs->interrupt_request & CPU_INTERRUPT_HARD &&
            (env->irq & (IRQ_RESET | IRQ_NMI) || env->gie);
}

static void msp430_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    MSP430CPU *cpu = MSP430_CPU(cs);
    CPUMSP430State *env = &cpu->env;
    int i;

    qemu_fprintf(f, "\n");
    qemu_fprintf(f, "PC: %04x\n", env->regs[R_PC]);
    qemu_fprintf(f, "SP: %04x\n", env->regs[R_SP]);
    qemu_fprintf(f, "SR: [ %c %s %c %c %c ]\n",
                 env->v ? 'V' : '-',
                 env->gie ? "GIE" : "-",
                 env->n ? 'N' : '-',
                 env->z ? 'Z' : '-',
                 env->c ? 'C' : '-');
    qemu_fprintf(f, "\n");
    for (i = R_CG + 1; i < 10; i++) {
        qemu_fprintf(f, "R%d:  %04x ", i, env->regs[i]);
    }
    qemu_fprintf(f, "\n");
    for (; i < NUM_REGS; i++) {
        qemu_fprintf(f, "R%02d: %04x ", i, env->regs[i]);
    }
    qemu_fprintf(f, "\n");
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

    device_class_set_parent_realize(dc, msp430_cpu_realize, &mcc->parent_realize);

    resettable_class_set_parent_phases(rc, NULL, msp430_cpu_reset_hold, NULL,
                                       &mcc->parent_phases);

    cc->class_by_name = msp430_cpu_class_by_name;

    cc->has_work = msp430_cpu_has_work;
    cc->dump_state = msp430_cpu_dump_state;
    cc->set_pc = msp430_cpu_set_pc;
    cc->get_pc = msp430_cpu_get_pc;
    cc->sysemu_ops = &msp430_sysemu_ops;
    cc->gdb_read_register = msp430_cpu_gdb_read_register;
    cc->gdb_write_register = msp430_cpu_gdb_write_register;
    cc->gdb_num_core_regs = NUM_REGS;
    cc->gdb_core_xml_file = "msp430-cpu.xml";
    cc->tcg_ops = &msp430_tcg_ops;
}

static const TypeInfo msp430_cpu_type_info[] = {
    {
        .name = TYPE_MSP430_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(MSP430CPU),
        .instance_init = msp430_cpu_init,
        .class_size = sizeof(MSP430CPUClass),
        .class_init = msp430_cpu_class_init,
    },
};

DEFINE_TYPES(msp430_cpu_type_info)

static void msp430_cpu_list_entry(gpointer data, gpointer user_data)
{
    const char *typename = object_class_get_name(OBJECT_CLASS(data));

    qemu_printf("%s\n", typename);
}

void msp430_cpu_list(void)
{
    GSList *list;
    list = object_class_get_list_sorted(TYPE_MSP430_CPU, false);
    g_slist_foreach(list, msp430_cpu_list_entry, NULL);
    g_slist_free(list);
}
