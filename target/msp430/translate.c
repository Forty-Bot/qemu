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
#include "semihosting/semihost.h"
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
    TCGv real;
    TCGv *surrogate;
    bool pending_gie;
} DisasContext;

#include "decode-insn.c.inc"

/* Interrupts got enabled or we performed semihosting */
#define DISAS_IO   DISAS_TARGET_0

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
            gen_helper_get_sr(cpu_regs[R_SR], tcg_env);
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

        ctx->addr = tcg_temp_new_i32();
        if (byte) {
            tcg_gen_mov_i32(ctx->addr, cpu_regs[r]);
        } else {
            tcg_gen_andi_i32(ctx->addr, cpu_regs[r], 0xfffe);
        }
        break;
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
        ctx->real = cpu_regs[r];
        ctx->surrogate = &cpu_regs[r];
        cpu_regs[r] = tcg_temp_new_i32();
        tcg_gen_addi_i32(cpu_regs[r], ctx->real, postinc);
    }
    return arg;
}

static void commit(DisasContext *ctx)
{
    if (ctx->surrogate) {
        tcg_gen_mov_i32(ctx->real, *ctx->surrogate);
        *ctx->surrogate = ctx->real;
        ctx->surrogate = NULL;
    }
}

static void put_dst(DisasContext *ctx, TCGv val, int rd, int ad, bool byte)
{
    if (rd == R_CG) {
        commit(ctx);
        return;
    }

    if (ad) {
        if (byte) {
            tcg_gen_qemu_st_i32(val, ctx->addr, 0, MO_UB);
        } else {
            tcg_gen_qemu_st_i32(val, ctx->addr, 0, MO_TEUW);
        }

        commit(ctx);
        return;
    }

    commit(ctx);
    if (rd == R_PC || rd == R_SP) {
        tcg_gen_andi_i32(val, val, 0xfffe);
    }

    if (rd == R_SR) {
        ctx->pending_gie = 1;
        tcg_gen_andi_i32(cpu_pending_gie, val, R_SR_GIE);
        tcg_gen_setcond_i32(TCG_COND_GT, cpu_pending_gie, cpu_gie, cpu_pending_gie);
        gen_helper_set_sr(tcg_env, val);
    } else {
        tcg_gen_mov_i32(cpu_regs[rd], val);
    }

    if (rd == R_PC) {
        tcg_gen_lookup_and_goto_ptr();
        ctx->base.is_jmp = DISAS_NORETURN;
    }
}

static TCGv mask_width(TCGv val, bool byte)
{
    TCGv masked;

    if (!byte) {
        return val;
    }

    masked = tcg_temp_new_i32();
    tcg_gen_andi_i32(masked, val, 0xff);
    return masked;
}

static void gen_add(TCGv res, TCGv src, TCGv dst, bool byte, bool carry)
{
    src = mask_width(src, byte);
    dst = mask_width(dst, byte);

    if (carry) {
        tcg_gen_add_i32(res, src, cpu_c);
        tcg_gen_add_i32(res, res, dst);
    } else {
        tcg_gen_add_i32(res, src, dst);
    }

    gen_c_add(res, byte);
    tcg_gen_andi_i32(res, res, byte ? 0xff : 0xffff);
    gen_v_arith(res, src, dst, true, byte);
    gen_nz(res, byte);
}

static bool trans_ADD(DisasContext *ctx, arg_ADD *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    gen_add(tmp, src, dst, a->bw, false);

    put_dst(ctx, tmp, a->rd, a->ad, a->bw);
    return true;
}

static bool trans_ADDC(DisasContext *ctx, arg_ADDC *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    gen_add(tmp, src, dst, a->bw, true);

    put_dst(ctx, tmp, a->rd, a->ad, a->bw);
    return true;
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

    put_dst(ctx, tmp, a->rd, a->ad, a->bw);
    return true;
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

    put_dst(ctx, tmp, a->rd, a->ad, a->bw);
    return true;
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

    put_dst(ctx, tmp, a->rd, a->ad, a->bw);
    return true;
}

static bool trans_BIT(DisasContext *ctx, arg_BIT *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    gen_and(tmp, src, dst, a->bw);

    commit(ctx);
    return true;
}

static bool trans_CALL(DisasContext *ctx, arg_CALL *a)
{
    TCGv dst = get_arg(ctx, a->rsd, a->asd, false);
    TCGv ret = tcg_constant_i32(ctx->base.pc_next);
    TCGv tmp = tcg_temp_new_i32();

    tcg_gen_subi_i32(tmp, cpu_regs[R_SP], 2);
    tcg_gen_andi_i32(tmp, tmp, 0xffff);
    tcg_gen_qemu_st_i32(ret, tmp, 0, MO_TEUW);

    commit(ctx);
    tcg_gen_mov_i32(cpu_regs[R_SP], tmp);
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
    src = mask_width(src, byte);
    dst = mask_width(dst, byte);

    if (carry) {
        tcg_gen_not_i32(res, src);
        tcg_gen_add_i32(res, res, cpu_c);
        tcg_gen_add_i32(res, res, dst);
    } else {
        tcg_gen_sub_i32(res, dst, src);
    }
    tcg_gen_setcondi_i32(TCG_COND_LEU, cpu_c, res, byte ? 0xff : 0xffff);
    tcg_gen_andi_i32(res, res, byte ? 0xff : 0xffff);
    gen_v_arith(res, src, dst, false, byte);
    gen_nz(res, byte);
}

static bool trans_CMP(DisasContext *ctx, arg_CMP *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    gen_sub(tmp, src, dst, a->bw, false);

    commit(ctx);
    return true;
}

static bool trans_DADD(DisasContext *ctx, arg_DADD *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    if (a->bw) {
        gen_helper_daddb(tmp, src, dst, cpu_c);
    } else {
        gen_helper_dadd(tmp, src, dst, cpu_c);
    }
    gen_c_add(tmp, a->bw);
    tcg_gen_andi_i32(tmp, tmp, a->bw ? 0xff : 0xffff);
    gen_nz(tmp, a->bw);
    /* V is undefined, so don't bother updating it */

    put_dst(ctx, tmp, a->rd, a->ad, a->bw);
    return true;
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
    put_dst(ctx, tmp, a->rd, a->ad, a->bw);
    return true;
}

static bool trans_NOP(DisasContext *ctx, arg_NOP *a)
{
    return true;
}

static bool trans_PUSH(DisasContext *ctx, arg_PUSH *a)
{
    TCGv src = get_arg(ctx, a->rsd, a->asd, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    tcg_gen_subi_i32(tmp, cpu_regs[R_SP], 2);
    tcg_gen_andi_i32(tmp, tmp, 0xffff);
    if (a->bw) {
        tcg_gen_qemu_st_i32(src, tmp, 0, MO_UB);
    } else {
        tcg_gen_qemu_st_i32(src, tmp, 0, MO_TEUW);
    }

    commit(ctx);
    tcg_gen_mov_i32(cpu_regs[R_SP], tmp);
    return true;
}

static bool trans_RETI(DisasContext *ctx, arg_RETI *a)
{
    TCGv new_sp = tcg_temp_new_i32();

    tcg_gen_qemu_ld_i32(cpu_regs[R_SR], cpu_regs[R_SP], 0, MO_TEUW);
    tcg_gen_addi_i32(new_sp, cpu_regs[R_SP], 2);
    tcg_gen_andi_i32(new_sp, new_sp, 0xffff);

    tcg_gen_qemu_ld_i32(cpu_regs[R_PC], new_sp, 0, MO_TEUW);
    tcg_gen_addi_i32(new_sp, new_sp, 2);
    tcg_gen_andi_i32(cpu_regs[R_SP], new_sp, 0xffff);
    gen_helper_set_sr(tcg_env, cpu_regs[R_SR]);
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

    put_dst(ctx, tmp, a->rsd, a->asd, a->bw);
    return true;
}

static bool trans_RRC(DisasContext *ctx, arg_RRC *a)
{
    TCGv dst = get_arg(ctx, a->rsd, a->asd, a->bw);
    TCGv msb = tcg_temp_new_i32();
    TCGv tmp = tcg_temp_new_i32();

    tcg_gen_movi_i32(cpu_v, 0);
    tcg_gen_shli_i32(msb, cpu_c, a->bw ? 7 : 15);
    tcg_gen_andi_i32(cpu_c, dst, 1);
    if (a->bw) {
        tcg_gen_andi_i32(tmp, dst, 0xff);
        tcg_gen_shri_i32(tmp, tmp, 1);
    } else {
        tcg_gen_shri_i32(tmp, dst, 1);
    }
    tcg_gen_or_i32(tmp, tmp, msb);
    gen_nz(tmp, a->bw);

    put_dst(ctx, tmp, a->rsd, a->asd, a->bw);
    return true;
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

    put_dst(ctx, tmp, a->rd, a->ad, a->bw);
    return true;
}

static bool trans_SUBC(DisasContext *ctx, arg_SUBC *a)
{
    TCGv src = get_arg(ctx, a->rs, a->as, a->bw);
    TCGv dst = get_arg(ctx, a->rd, a->ad, a->bw);
    TCGv tmp = tcg_temp_new_i32();

    gen_sub(tmp, src, dst, a->bw, true);

    put_dst(ctx, tmp, a->rd, a->ad, a->bw);
    return true;
}

static bool trans_SWPB(DisasContext *ctx, arg_SWPB *a)
{
    TCGv dst = get_arg(ctx, a->rsd, a->asd, false);
    TCGv tmp = tcg_temp_new_i32();

    tcg_gen_bswap16_i32(tmp, dst, TCG_BSWAP_IZ | TCG_BSWAP_OZ);

    put_dst(ctx, tmp, a->rsd, a->asd, false);
    return true;
}

static bool trans_SXT(DisasContext *ctx, arg_SXT *a)
{
    TCGv dst = get_arg(ctx, a->rsd, a->asd, false);
    TCGv tmp = tcg_temp_new_i32();

    tcg_gen_ext8s_i32(tmp, dst);
    tcg_gen_andi_i32(tmp, tmp, 0xffff);

    gen_nz(tmp, false);
    gen_vc_logic(tmp);

    put_dst(ctx, tmp, a->rsd, a->asd, false);
    return true;
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

    put_dst(ctx, tmp, a->rd, a->ad, a->bw);
    return true;
}

static void msp430_tr_init_disas_context(DisasContextBase *dcbase,
                                         CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->env = cpu_env(cs);
    ctx->pending_gie = ctx->env->pending_gie;
}

static void msp430_tr_tb_start(DisasContextBase *dcbase, CPUState *cs)
{
}

static void msp430_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next, ctx->pending_gie);
}

static void msp430_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    uint16_t insn;

    if (ctx->pending_gie) {
        tcg_gen_movi_i32(cpu_pending_gie, 0);
        ctx->base.is_jmp = DISAS_IO;
    }

    /* FIXME: use a breakpoint? */
    if (semihosting_enabled(false)) {
        if (ctx->env->cio_io && ctx->base.pc_next == ctx->env->cio_io) {
            gen_helper_cio_io(tcg_env);
            ctx->base.is_jmp = DISAS_IO;
        }

        if (ctx->env->cio_exit && ctx->base.pc_next == ctx->env->cio_exit) {
            gen_helper_cio_exit();
            ctx->base.is_jmp = DISAS_NORETURN;
        }
    }

    insn = cpu_lduw_code(ctx->env, ctx->base.pc_next);
    ctx->base.pc_next += 2;
    if (!decode(ctx, insn)) {
        gen_helper_unsupported(tcg_env, tcg_constant_i32(insn));
        ctx->base.is_jmp = DISAS_NORETURN;
    }
}

static void msp430_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
    case DISAS_IO:
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
                           vaddr pc, void *host_pc)
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
        cpu_regs[i] = tcg_global_mem_new_i32(tcg_env,
                                             offsetof(CPUMSP430State, regs[i]),
                                             names[i]);
    }

#define VAR(sym, name) do { \
    cpu_##sym = tcg_global_mem_new_i32(tcg_env, \
                                       offsetof(CPUMSP430State, sym), #name); \
} while(0)

    VAR(v, V);
    VAR(gie, GIE);
    VAR(n, N);
    VAR(z, Z);
    VAR(c, C);
    VAR(pending_gie, PENDING_GIE);
}
