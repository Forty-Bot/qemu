/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu_ldst.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "tcg/tcg-op.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

static TCGv cpu_regs[NUM_REGS];
static TCGv cpu_v, cpu_gie, cpu_n, cpu_z, cpu_c;
static TCGv cpu_pending_gie;

typedef struct DisasContext {
    DisasContextBase base;
    CPUMSP430State *env;
    TCGv addr;
    bool pending_gie;
} DisasContext;

#include "decode-insn.c.inc"

#define DISAS_GIE   DISAS_TARGET_0

static void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    if (translator_use_goto_tb(&ctx->base, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(cpu_regs[R_PC], dest);
        tcg_gen_exit_tb(ctx->base.tb, n);
    } else {
        tcg_gen_movi_i32(cpu_regs[R_PC], dest);
        tcg_gen_lookup_and_goto_ptr();
    }
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_nz(TCGv val, bool byte)
{
    tcg_gen_shri_i32(cpu_n, val, byte ? 7 : 15);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_z, val, 0);
}

static void gen_vc_logic(TCGv val)
{
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_c, val, 0);
    tcg_gen_movi_i32(cpu_v, 0);
}

static void gen_c_add(TCGv res, bool byte)
{
    tcg_gen_setcondi_i32(TCG_COND_GTU, cpu_c, res, byte ? 0xff : 0xffff);
}

static void gen_v_arith(TCGv res, TCGv arg1, TCGv arg2, bool add, bool byte)
{
    TCGv tmp = tcg_temp_new_i32();

    tcg_gen_xor_i32(tmp, arg1, arg2);
    tcg_gen_xor_i32(cpu_v, arg1, res);
    if (add) {
        tcg_gen_andc_i32(cpu_v, cpu_v, tmp);
    } else {
        tcg_gen_andc_i32(cpu_v, tmp, cpu_v);
    }

    tcg_gen_shri_i32(cpu_v, cpu_v, byte ? 7 : 15);
    if (byte) {
        tcg_gen_andi_i32(cpu_v, cpu_v, 1);
    }
}

static void setup_dst(DisasContext *ctx, int rd, int ad, bool byte)
{
    uint16_t off;

    if (!ad || rd == R_CG) {
        return;
    }

    off = cpu_lduw_code(ctx->env, ctx->base.pc_next);
    if (rd == R_PC) {
        off += ctx->base.pc_next;
        if (!byte) {
            off &= 0xfffe;
        }
        ctx->addr = tcg_constant_i32(off);
    } else if (rd == R_SR) {
        if (!byte) {
            off &= 0xfffe;
        }
        ctx->addr = tcg_constant_i32(off);
    } else {
        ctx->addr = tcg_temp_new_i32();
        tcg_gen_addi_i32(ctx->addr, cpu_regs[rd], off);
        if (byte) {
            tcg_gen_andi_i32(ctx->addr, ctx->addr, 0xffff);
        } else {
            tcg_gen_andi_i32(ctx->addr, ctx->addr, 0xfffe);
        }
    }
    ctx->base.pc_next += 2;
}

static TCGv get_arg(DisasContext *ctx, int r, int a, bool byte)
{
    int postinc = 0;
    TCGv arg;

    switch (a) {
    case 0: /* register */
        if (r == R_PC) {
            return tcg_constant_i32(ctx->base.pc_next);
        }

        if (r == R_CG) {
            return tcg_constant_i32(0);
        }

        if (r == R_SR) {
            gen_helper_get_sr(cpu_regs[R_SR], cpu_env);
        }

        return cpu_regs[r];
    case 1: /* indexed */
        if (r == R_CG) {
            return tcg_constant_i32(1);
        }

        setup_dst(ctx, r, a, byte);
        break;
    case 3: /* indirect autoincrement */
        if (r == R_CG) {
            return tcg_constant_i32(0xffff);
        }

        if (r == R_SR) {
            return tcg_constant_i32(8);
        }

        if (r == R_PC) {
            uint16_t imm = cpu_ldsw_code(ctx->env, ctx->base.pc_next);

            ctx->base.pc_next += 2;
            return tcg_constant_i32(imm);
        }

        if (r == R_SP) {
            postinc = 2;
        } else {
            postinc = byte ? 1 : 2;
        }

        /* fallthrough */
    case 2: /* indirect register */
        if (r == R_CG) {
            return tcg_constant_i32(2);
        }

        if (r == R_SR) {
            return tcg_constant_i32(4);
        }

        if (r == R_PC) {
            ctx->addr = tcg_constant_i32(ctx->base.pc_next);
        } else if (byte) {
            ctx->addr = cpu_regs[r];
        } else {
            ctx->addr = tcg_temp_new_i32();
            tcg_gen_andi_i32(ctx->addr, cpu_regs[r], 0xfffe);
        }
        break;
    }

    arg = tcg_temp_new_i32();
    if (byte) {
        tcg_gen_qemu_ld_i32(arg, ctx->addr, 0, MO_UB);
    } else {
        tcg_gen_qemu_ld_i32(arg, ctx->addr, 0, MO_TEUW);
    }

    if (postinc) {
        tcg_gen_addi_i32(cpu_regs[r], cpu_regs[r], postinc);
    }
    return arg;
}

static bool put_dst(DisasContext *ctx, TCGv val, int rd, int ad, bool byte)
{
    if (rd == R_CG) {
        return true;
    }

    if (ad) {
        if (byte) {
            tcg_gen_qemu_st_i32(val, ctx->addr, 0, MO_UB);
        } else {
            tcg_gen_qemu_st_i32(val, ctx->addr, 0, MO_TEUW);
        }

        return true;
    }

    if (rd == R_PC || rd == R_SP) {
        tcg_gen_andi_i32(val, val, 0xfffe);
    }

    if (rd == R_SR) {
        ctx->pending_gie = 1;
        tcg_gen_andi_i32(cpu_pending_gie, val, R_SR_GIE);
        tcg_gen_setcond_i32(TCG_COND_GT, cpu_pending_gie, cpu_gie, cpu_pending_gie);
        gen_helper_set_sr(cpu_env, val);
    } else {
        tcg_gen_mov_i32(cpu_regs[rd], val);
    }

    if (rd == R_PC) {
        tcg_gen_lookup_and_goto_ptr();
        ctx->base.is_jmp = DISAS_NORETURN;
    }

    return true;
}

static void gen_add(TCGv res, TCGv src, TCGv dst, bool byte, bool carry)
{
    if (carry) {
        tcg_gen_add_i32(res, src, cpu_c);
        tcg_gen_add_i32(res, res, dst);
    } else {
        tcg_gen_add_i32(res, src, dst);
    }

    gen_c_add(res, byte);
    tcg_gen_andi_i32(res, res, byte ? 0xff : 0xffff);
    gen_v_arith(res, src, dst, byte, true);
    gen_nz(res, byte);
}

static bool trans_ADD(DisasContext *ctx, arg_ADD *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    gen_add(tmp, src, dst, a->bw, false);

    return put_dst(ctx, tmp, a->rd, a->ad, a->bw);
}

static bool trans_ADDC(DisasContext *ctx, arg_ADDC *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    gen_add(tmp, src, dst, a->bw, true);

    return put_dst(ctx, tmp, a->rd, a->ad, a->bw);
}

static void gen_and(TCGv res, TCGv src, TCGv dst, bool byte)
{
    tcg_gen_and_i32(res, src, dst);
    if (byte) {
        tcg_gen_andi_i32(res, res, 0xff);
    }

    gen_vc_logic(res);
    gen_nz(res, byte);
}

static bool trans_AND(DisasContext *ctx, arg_AND *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    gen_and(tmp, src, dst, a->bw);

    return put_dst(ctx, tmp, a->rd, a->ad, a->bw);
}

static bool trans_BIC(DisasContext *ctx, arg_BIC *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    tcg_gen_andc_i32(tmp, dst, src);
    if (a->bw) {
        tcg_gen_andi_i32(tmp, tmp, 0xff);
    }

    return put_dst(ctx, tmp, a->rd, a->ad, a->bw);
}

static bool trans_BIS(DisasContext *ctx, arg_BIS *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    tcg_gen_or_i32(tmp, dst, src);
    if (a->bw) {
        tcg_gen_andi_i32(tmp, tmp, 0xff);
    }

    return put_dst(ctx, tmp, a->rd, a->ad, a->bw);
}

static bool trans_BIT(DisasContext *ctx, arg_BIT *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    gen_and(tmp, src, dst, a->bw);

    return true;
}

static bool trans_CALL(DisasContext *ctx, arg_CALL *a)
{
    TCGv dst = get_arg(ctx, a->rsd, a->asd, false);
    TCGv ret = tcg_constant_i32(ctx->base.pc_next);

    tcg_gen_subi_i32(cpu_regs[R_SP], cpu_regs[R_SP], 2);
    tcg_gen_qemu_st_i32(ret, cpu_regs[R_SP], 0, MO_TEUW);

    tcg_gen_mov_i32(cpu_regs[R_PC], dst);
    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_CLRC(DisasContext *ctx, arg_CLRC *a)
{
    tcg_gen_movi_i32(cpu_c, 0);
    return true;
}

static bool trans_CLRN(DisasContext *ctx, arg_CLRN *a)
{
    tcg_gen_movi_i32(cpu_n, 0);
    return true;
}

static bool trans_CLRZ(DisasContext *ctx, arg_CLRZ *a)
{
    tcg_gen_movi_i32(cpu_z, 0);
    return true;
}

static void gen_sub(TCGv res, TCGv src, TCGv dst, bool byte, bool carry)
{
    if (carry) {
        tcg_gen_not_i32(res, src);
        tcg_gen_add_i32(res, res, cpu_c);
        tcg_gen_add_i32(res, res, dst);
    } else {
        tcg_gen_sub_i32(res, dst, src);
    }
    tcg_gen_setcondi_i32(TCG_COND_LEU, cpu_c, res, byte ? 0xff : 0xffff);
    tcg_gen_andi_i32(res, res, byte ? 0xff : 0xffff);
    gen_v_arith(res, src, dst, byte, false);
    gen_nz(res, byte);
}

static bool trans_CMP(DisasContext *ctx, arg_CMP *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    gen_sub(tmp, src, dst, a->bw, false);

    return true;
}

static bool trans_DADD(DisasContext *ctx, arg_DADD *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();
    TCGv mask, tmp3;
    TCGv tmp2 = tcg_temp_new_i32();

    /* Pre-emptively add 6 to propagate carries */
    tcg_gen_addi_i32(tmp, src, a->bw ? 0x66 : 0x6666);
    /* Binary sum without carries */
    tcg_gen_xor_i32(tmp2, tmp, cpu_c);
    tcg_gen_xor_i32(tmp2, tmp2, dst);
    /* Binary sum with carries */
    tcg_gen_add_i32(tmp, tmp, cpu_c);
    tcg_gen_add_i32(tmp, tmp, dst);
    /* Carry bits */
    tcg_gen_xor_i32(tmp2, tmp2, tmp);
    /* If we didn't have a carry, then we need to remove the 6 from above */
    tcg_gen_andc_i32(tmp, tcg_constant_i32(a->bw ? 0x10 : 0x1110), tmp);
    /* Generate the 6s */
    tmp3 = tcg_temp_new_i32();
    tcg_gen_shri_i32(tmp3, tmp2, 3);
    tcg_gen_shri_i32(tmp2, tmp2, 2);
    tcg_gen_or_i32(tmp2, tmp2, tmp3);
    /* Subtract them off */
    tcg_gen_sub_i32(tmp, tmp, tmp2);

    /* Force a binary overflow if we have a decimal overflow */
    tcg_gen_addi_i32(tmp2, tmp, a->bw ? 0x60 : 0x6000);
    /* Detect it */
    gen_c_add(tmp2, a->bw);
    mask = tcg_constant_i32(a->bw ? 0xff : 0xffff);
    /* If we have an overflow, move the overflowed result in */
    tcg_gen_movcond_i32(TCG_COND_GEU, tmp, tmp2, mask, tmp2, tmp);
    /* And mask off the overflow */
    tcg_gen_and_i32(tmp, tmp, mask);
    gen_nz(tmp, a->bw);
    /* V is undefined, so don't bother updating it */

    return put_dst(ctx, tmp, a->rd, a->ad, a->bw);
}

static bool trans_DINT(DisasContext *ctx, arg_DINT *a)
{
    tcg_gen_movi_i32(cpu_gie, 0);
    return true;
}

static bool trans_EINT(DisasContext *ctx, arg_EINT *a)
{
    ctx->pending_gie = 1;
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_pending_gie, cpu_gie, 0);
    tcg_gen_movi_i32(cpu_gie, 1);
    return true;
}

static bool trans_Jcond(DisasContext *ctx, arg_Jcond *a)
{
    TCGLabel *not_taken = gen_new_label();
    TCGv var;
    TCGCond cond;

    switch (a->cond) {
    case 0: /* JNE/JNZ */
        var = cpu_z;
        cond = TCG_COND_NE;
        break;
    case 1: /* JEQ/JZ */
        var = cpu_z;
        cond = TCG_COND_EQ;
        break;
    case 2: /* JNC/JLO */
        var = cpu_c;
        cond = TCG_COND_NE;
        break;
    case 3: /* JC/JHS */
        var = cpu_c;
        cond = TCG_COND_EQ;
        break;
    case 4: /* JN */
        var = cpu_n;
        cond = TCG_COND_EQ;
        break;
    case 5: /* JGE */
        var = tcg_temp_new();
        tcg_gen_xor_i32(var, cpu_n, cpu_v);
        cond = TCG_COND_NE;
        break;
    case 6: /* JL */
        var = tcg_temp_new();
        tcg_gen_xor_i32(var, cpu_n, cpu_v);
        cond = TCG_COND_EQ;
        break;
    default:
        g_assert_not_reached();
    }

    tcg_gen_brcondi_i32(cond, var, 0, not_taken);
    gen_goto_tb(ctx, 0, ctx->base.pc_next + (a->off << 1));
    gen_set_label(not_taken);
    gen_goto_tb(ctx, 1, ctx->base.pc_next);
    return true;
}

static bool trans_JMP(DisasContext *ctx, arg_JMP *a)
{
    gen_goto_tb(ctx, 0, ctx->base.pc_next + (a->off << 1));
    return true;
}

static bool trans_MOV(DisasContext *ctx, arg_MOV *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    if (a->bw) {
        tcg_gen_andi_i32(tmp, src, 0xff);
    } else {
        tcg_gen_mov_i32(tmp, src);
    }

    setup_dst(ctx, a->rd, a->ad, a->bw);
    return put_dst(ctx, tmp, a->rd, a->ad, a->bw);
}

static bool trans_PUSH(DisasContext *ctx, arg_PUSH *a)
{
    TCGv src = get_arg(ctx, a->rsd, a->asd, a->bw);

    tcg_gen_subi_i32(cpu_regs[R_SP], cpu_regs[R_SP], 2);
    if (a->bw) {
        tcg_gen_qemu_st_i32(src, cpu_regs[R_SP], 0, MO_UB);
    } else {
        tcg_gen_qemu_st_i32(src, cpu_regs[R_SP], 0, MO_TEUW);
    }

    return true;
}

static bool trans_RETI(DisasContext *ctx, arg_RETI *a)
{
    tcg_gen_qemu_ld_i32(cpu_regs[R_SR], cpu_regs[R_SP], 0, MO_TEUW);
    gen_helper_set_sr(cpu_env, cpu_regs[R_SR]);
    tcg_gen_addi_i32(cpu_regs[R_SP], cpu_regs[R_SP], 2);

    tcg_gen_qemu_ld_i32(cpu_regs[R_PC], cpu_regs[R_SP], 0, MO_TEUW);
    tcg_gen_addi_i32(cpu_regs[R_SP], cpu_regs[R_SP], 2);
    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

static bool trans_RRA(DisasContext *ctx, arg_RRA *a)
{
    TCGv dst = get_arg(ctx, a->rsd, a->asd, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    tcg_gen_movi_i32(cpu_v, 0);
    tcg_gen_andi_i32(cpu_c, dst, 1);
    if (a->bw) {
        tcg_gen_ext8s_i32(tmp, dst);
    } else {
        tcg_gen_ext16s_i32(tmp, dst);
    }
    tcg_gen_shri_i32(tmp, tmp, 1);
    tcg_gen_andi_i32(tmp, tmp, a->bw ? 0xff : 0xffff);
    gen_nz(tmp, a->bw);

    return put_dst(ctx, tmp, a->rsd, a->asd, a->bw);
}

static bool trans_RRC(DisasContext *ctx, arg_RRC *a)
{
    TCGv dst = get_arg(ctx, a->rsd, a->asd, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    tcg_gen_movi_i32(cpu_v, 0);
    tcg_gen_shli_i32(tmp, cpu_c, a->bw ? 8 : 16);
    tcg_gen_or_i32(tmp, tmp, dst);
    tcg_gen_andi_i32(cpu_c, dst, 1);
    tcg_gen_shri_i32(tmp, tmp, 1);
    if (a->bw) {
        tcg_gen_andi_i32(tmp, tmp, 0xff);
    }
    gen_nz(tmp, a->bw);

    return put_dst(ctx, tmp, a->rsd, a->asd, a->bw);
}

static bool trans_SETC(DisasContext *ctx, arg_SETC *a)
{
    tcg_gen_movi_i32(cpu_c, 1);
    return true;
}

static bool trans_SETN(DisasContext *ctx, arg_SETN *a)
{
    tcg_gen_movi_i32(cpu_n, 1);
    return true;
}

static bool trans_SETZ(DisasContext *ctx, arg_SETZ *a)
{
    tcg_gen_movi_i32(cpu_z, 1);
    return true;
}

static bool trans_SUB(DisasContext *ctx, arg_SUB *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    gen_sub(tmp, src, dst, a->bw, false);

    return put_dst(ctx, tmp, a->rd, a->ad, a->bw);
}

static bool trans_SUBC(DisasContext *ctx, arg_SUBC *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    gen_sub(tmp, src, dst, a->bw, true);

    return put_dst(ctx, tmp, a->rd, a->ad, a->bw);
}

static bool trans_SWPB(DisasContext *ctx, arg_SWPB *a)
{
    TCGv dst = get_arg(ctx, a->rsd, a->asd, false);
    TCGv tmp = tcg_temp_new_i32();

    tcg_gen_bswap16_i32(tmp, dst, TCG_BSWAP_IZ | TCG_BSWAP_OZ);

    return put_dst(ctx, tmp, a->rsd, a->asd, false);
}

static bool trans_SXT(DisasContext *ctx, arg_SXT *a)
{
    TCGv dst = get_arg(ctx, a->rsd, a->asd, false);
    TCGv tmp = tcg_temp_new_i32();

    tcg_gen_ext8s_i32(tmp, dst);
    tcg_gen_andi_i32(tmp, tmp, 0xffff);

    gen_nz(tmp, false);
    gen_vc_logic(tmp);

    return put_dst(ctx, tmp, a->rsd, a->asd, false);
}

static bool trans_XOR(DisasContext *ctx, arg_XOR *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();
    TCGv tmp2;

    tcg_gen_xor_i32(tmp, src, dst);
    if (a->bw) {
        tcg_gen_andi_i32(tmp, tmp, 0xff);
    }

    gen_nz(tmp, a->bw);
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_c, tmp, 0);

    tmp2 = tcg_temp_new_i32();
    tcg_gen_and_i32(tmp2, src, dst);
    tcg_gen_shri_i32(cpu_v, tmp2, a->bw ? 7 : 15);
    if (a->bw) {
        tcg_gen_andi_i32(cpu_v, cpu_v, 1);
    }

    return put_dst(ctx, tmp, a->rd, a->ad, a->bw);
}

static void msp430_tr_init_disas_context(DisasContextBase *dcbase,
                                         CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->env = cs->env_ptr;
    ctx->pending_gie = ctx->env->pending_gie;
}

static void msp430_tr_tb_start(DisasContextBase *dcbase, CPUState *cs)
{
}

static void msp430_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next);
}

static void msp430_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    uint16_t insn = cpu_lduw_code(ctx->env, ctx->base.pc_next);

    if (ctx->pending_gie) {
        tcg_gen_movi_i32(cpu_pending_gie, 0);
        ctx->base.is_jmp = DISAS_GIE;
    }

    ctx->base.pc_next += 2;
    if (!decode(ctx, insn)) {
        gen_helper_unsupported(cpu_env, tcg_constant_i32(insn));
        ctx->base.is_jmp = DISAS_NORETURN;
    }
}

static void msp430_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
    case DISAS_GIE:
        gen_goto_tb(ctx, 1, ctx->base.pc_next);
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static void msp430_tr_disas_log(const DisasContextBase *dcbase,
                                CPUState *cs, FILE *logfile)
{
    fprintf(logfile, "IN: %s\n", lookup_symbol(dcbase->pc_first));
    target_disas(logfile, cs, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps msp430_tr_ops = {
    .init_disas_context = msp430_tr_init_disas_context,
    .tb_start           = msp430_tr_tb_start,
    .insn_start         = msp430_tr_insn_start,
    .translate_insn     = msp430_tr_translate_insn,
    .tb_stop            = msp430_tr_tb_stop,
    .disas_log          = msp430_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int *max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext ctx;

    translator_loop(cs, tb, max_insns, pc, host_pc, &msp430_tr_ops, &ctx.base);
}

void msp430_translate_init(void)
{
    int i;
    static const char *const names[NUM_REGS] = {
        "PC", "SP", "SR", "CG", "R4", "R5", "R6", "R7",
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",
    };

    for (i = 0; i < NUM_REGS; i++) {
        cpu_regs[i] = tcg_global_mem_new_i32(cpu_env,
                                             offsetof(CPUMSP430State, regs[i]),
                                             names[i]);
    }

#define VAR(sym, name) do { \
    cpu_##sym = tcg_global_mem_new_i32(cpu_env, \
                                       offsetof(CPUMSP430State, sym), #name); \
} while(0)

    VAR(v, V);
    VAR(gie, GIE);
    VAR(n, N);
    VAR(z, Z);
    VAR(c, C);
    VAR(pending_gie, PENDING_GIE);
}
