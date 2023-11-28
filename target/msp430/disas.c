/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "disas/dis-asm.h"

typedef struct {
    disassemble_info *info;
    bfd_vma pc;
    size_t len;
    int status;
    int column;
} DisasContext;

/* Include the auto-generated decoder.  */
#include "decode-insn.c.inc"

static uint16_t msp430_read_word(DisasContext *ctx)
{
    bfd_byte buf[2] = { };

    if (!ctx->status) {
        ctx->status = ctx->info->read_memory_func(ctx->pc, buf, 2, ctx->info);
        if (ctx->status) {
            ctx->info->memory_error_func(ctx->status, ctx->pc, ctx->info);
        }
    }

    ctx->pc += 2;
    return bfd_getl16(buf);
}

static const char *reg_name[] = {
    "PC", "SP", "SR",  "CG",  "R4",  "R5",  "R6",  "R7",
    "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",
};

#define disprintf(ctx, fmt, ...) \
    (ctx)->column += (ctx)->info->fprintf_func((ctx)->info->stream, \
                                               fmt, ##__VA_ARGS__)

static void print_arg(DisasContext *ctx, int r, int a)
{
    switch (a) {
    case 0:
        if (r == R_CG) {
            disprintf(ctx, "#0");
        } else {
            disprintf(ctx, "%s", reg_name[r]);
        }
        break;
    case 1:
        if (r == R_PC) {            
            disprintf(ctx, "0x%hx", msp430_read_word(ctx));
        } else if (r == R_SR) {
            disprintf(ctx, "&0x%hx", msp430_read_word(ctx));
        } else if (r == R_CG) {
            disprintf(ctx, "#1");
        } else {
            disprintf(ctx, "0x%hx(%s)", msp430_read_word(ctx), reg_name[r]);
        }
        break;
    case 2:
        if (r == R_SR) {
            disprintf(ctx, "#4");
        } else if (r == R_CG) {
            disprintf(ctx, "#2");
        } else {
            disprintf(ctx, "@%s", reg_name[r]);
        }
        break;
    case 3:
        if (r == R_PC) {
            disprintf(ctx, "#0x%hx", msp430_read_word(ctx));
        } else if (r == R_SR) {
            disprintf(ctx, "#8");
        } else if (r == R_CG) {
            disprintf(ctx, "#-1");
        } else {
            disprintf(ctx, "@%s+", reg_name[r]);
        }
        break;
    }
}

#define pad_to(ctx, col, delim) \
    disprintf(ctx, "%-*s", col - (ctx)->column, delim)

#define INSN0(opcode) \
static bool trans_##opcode(DisasContext *ctx, arg_##opcode *a) \
{ \
    disprintf(ctx, #opcode); \
    return true; \
}

#define INSN1(opcode) \
static bool trans_##opcode(DisasContext *ctx, arg_##opcode *a) \
{ \
    disprintf(ctx, "%s%s", #opcode, a->bw ? ".B" : ""); \
    pad_to(ctx, 7, " "); \
    print_arg(ctx, a->rsd, a->asd); \
    return true; \
}

#define INSN1W(opcode) \
static bool trans_##opcode(DisasContext *ctx, arg_##opcode *a) \
{ \
    pad_to(ctx, 7, #opcode " "); \
    print_arg(ctx, a->rsd, a->asd); \
    return true; \
}

#define INSN2(opcode) \
static bool trans_##opcode(DisasContext *ctx, arg_##opcode *a) \
{ \
    disprintf(ctx, "%s%s", #opcode, a->bw ? ".B" : ""); \
    pad_to(ctx, 7, " "); \
    print_arg(ctx, a->rs, a->as); \
    pad_to(ctx, 20, ", "); \
    print_arg(ctx, a->rd, a->ad); \
    return true; \
}

INSN2(MOV)
INSN2(ADD)
INSN2(ADDC)
INSN2(SUBC)
INSN2(SUB)
INSN2(CMP)
INSN2(DADD)
INSN2(BIT)
INSN0(CLRC)
INSN0(CLRZ)
INSN0(CLRN)
INSN0(DINT)
INSN2(BIC)
INSN0(SETC)
INSN0(SETZ)
INSN0(SETN)
INSN0(EINT)
INSN2(BIS)
INSN2(XOR)
INSN2(AND)

INSN1(RRC)
INSN1W(SWPB)
INSN1(RRA)
INSN1W(SXT)
INSN1(PUSH)
INSN1W(CALL)
INSN0(RETI);

static bool trans_JMP(DisasContext *ctx, arg_JMP *a)
{
    pad_to(ctx, 7, "JMP ");
    disprintf(ctx, "$%hx", a->off * 2);
    return true;
}

static const char *jmp_cond[] = {
    "NE", "EQ", "LO", "HI", "N ", "GE", "L",
};

static bool trans_Jcond(DisasContext *ctx, arg_Jcond *a)
{
    disprintf(ctx, "J%s", jmp_cond[a->cond]);
    pad_to(ctx, 7, " ");
    disprintf(ctx, "$%hx", a->off * 2);
    return true;
}

int msp430_print_insn(bfd_vma addr, disassemble_info *info)
{
    DisasContext ctx = {
        .info = info,
        .pc = addr,
    };
    uint16_t insn;

    insn = msp430_read_word(&ctx);
    if (ctx.status)
        return ctx.status;

    if (!decode(&ctx, insn)) {
        disprintf(&ctx, ".dw     0x%04x", insn);
    }

    return ctx.status ?: ctx.pc - addr;
}
