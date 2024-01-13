; SPDX-License-Identifier: GPL-2.0-or-later
; Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>

#include <msp430.h>

#include "macros.S"

.macro test_rrz arg, exp, sr
    test_op1_single rrc, \arg, check, setupc, \exp, \sr, 0
.endm

.macro test_rrc arg, exp, sr
    test_op1_single rrc, \arg, check, setupc, \exp, \sr, 1
.endm

.macro test_rra arg, exp, sr
    test_op1_single rra, \arg, check, setup, \exp, \sr
.endm

.macro test_rrzb arg, exp, sr
    test_op1_single rrc.b, \arg, check, setupc, \exp, \sr, 0
.endm

.macro test_rrcb arg, exp, sr
    test_op1_single rrc.b, \arg, check, setupc, \exp, \sr, 1
.endm

.macro test_rrab arg, exp, sr
    test_op1_single rra.b, \arg, check, setup, \exp, \sr
.endm

.macro test_swpb arg, exp
    test_op1_single swpb, \arg, check, setup, \exp, 0
    test_op1_single swpb, \exp, check, setup, \arg, 0
.endm

.macro test_sxt arg, exp, sr
    test_op1_single sxt, \arg, check, setup, \exp, \sr
.endm

.global main
main:
    mov #97, r12
    call #plan

    ; Rotates

    test_rrz 0x0000, 0x0000, Z
    test_rrz 0x0001, 0x0000, Z | C
    test_rrz 0xffff, 0x7fff, C
    test_rrz 0x8000, 0x4000, 0
    test_rrz 0x7fff, 0x3fff, C
    test_rrz 0x8181, 0x40c0, C

    test_rrc 0x0000, 0x8000, N
    test_rrc 0x0001, 0x8000, N | C
    test_rrc 0xffff, 0xffff, N | C
    test_rrc 0x8000, 0xc000, N
    test_rrc 0x7fff, 0xbfff, N | C
    test_rrc 0x8181, 0xc0c0, N | C

    test_rra 0x0000, 0x0000, Z
    test_rra 0x0001, 0x0000, Z | C
    test_rra 0xffff, 0xffff, N | C
    test_rra 0x8000, 0xc000, N
    test_rra 0x7fff, 0x3fff, C
    test_rra 0x8181, 0xc0c0, N | C

    ; Byte variants

    test_rrzb 0x0000, 0x0000, Z
    test_rrzb 0x0001, 0x0000, Z | C
    test_rrzb 0x00ff, 0x007f, C
    test_rrzb 0xff80, 0x0040, 0
    test_rrzb 0x007f, 0x003f, C
    test_rrzb 0xff81, 0x0040, C
    test_rrzb 0xff12, 0x0009, 0
    test_rrzb 0x0092, 0x0049, 0

    test_rrcb 0x0000, 0x0080, N
    test_rrcb 0x0001, 0x0080, N | C
    test_rrcb 0x00ff, 0x00ff, N | C
    test_rrcb 0xff80, 0x00c0, N
    test_rrcb 0x007f, 0x00bf, N | C
    test_rrcb 0xff81, 0x00c0, N | C
    test_rrcb 0xff12, 0x0089, N
    test_rrcb 0x0092, 0x00c9, N

    test_rrab 0x0000, 0x0000, Z
    test_rrab 0x0001, 0x0000, Z | C
    test_rrab 0x00ff, 0x00ff, N | C
    test_rrab 0xff80, 0x00c0, N
    test_rrab 0x007f, 0x003f, C
    test_rrab 0xff81, 0x00c0, N | C
    test_rrab 0xff12, 0x0009, 0
    test_rrab 0x0092, 0x00c9, N

    ; Misc

    test_swpb 0x1234, 0x3412
    test_swpb 0xabcd, 0xcdab
    test_swpb 0xaa55, 0x55aa
    test_swpb 0xa55a, 0x5aa5
    test_swpb 0x00ff, 0xff00
    test_swpb 0x0ff0, 0xf00f

    test_sxt 0x0000, 0x0000, Z
    test_sxt 0x1234, 0x0034, C
    test_sxt 0xabcd, 0xffcd, N | C
    test_sxt 0x007f, 0x007f, C
    test_sxt 0x0080, 0xff80, N | C
    test_sxt 0xff7f, 0x007f, C
    test_sxt 0xff80, 0xff80, N | C

test_rrc_modes:
    setup 0xc0b4, N | C
    test_op1_modes rrc, 0x8169, check, set_carry

test_rra_modes:
    setup 0xc0b4, N | C
    test_op1_modes rra, 0x8169, check

test_rrcb_modes:
    setupb 0x00b4, 0x81b4, N | C
    test_op1_modes rrc.b, 0x8169, checkb, set_carry, incr=1

test_rrab_modes:
    setupb 0x0034, 0x8134, C
    test_op1_modes rra.b, 0x8169, checkb, set_carry, incr=1

test_swbp_modes:
    setup 0x7856, 0
    test_op1_modes swpb, 0x5678, check

test_sxt_modes:
    setup 0xff87, N | C
    test_op1_modes sxt, 0x6587, check

    mov #0, r12
    call #exit
