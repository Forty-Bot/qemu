; SPDX-License-Identifier: GPL-2.0-or-later
; Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>

#include <msp430.h>

#include "macros.S"

check:
    cmp r4, r6
    jne 1f
    cmp r5, r7
    jne 1f
    br #pass
1:
    br #fail

.macro test_jump jcond, exp
.section .rodata
99:
    .asciz "test_\jcond"

.text
test_\jcond:
    mov #0, r2
    mov #0, r4
    mov #0, r5
    mov #0, r6
    mov #0, r7
    \jcond 1f
2:  setc { \jcond 2f
3:  setz { \jcond 3f
4:  clrc { \jcond 4f
5:
    swpb r6
    setn { \jcond 5f
6:  setc { \jcond 6f
7:  clrz { \jcond 7f
8:  clrc { \jcond 8f
9:
    bis #V, r2
    \jcond 9f
10: setc { \jcond 10f
11: setz { \jcond 11f
12: clrc { \jcond 12f
13:
    swpb r7
    clrn { \jcond 13f
14: setc { \jcond 14f
15: clrz { \jcond 15f
16: clrc { \jcond 16f
    jmp 17f
1:  bis #1, r6 { jmp 2b
2:  bis #2, r6 { jmp 3b
3:  bis #8, r6 { jmp 4b
4:  bis #4, r6 { jmp 5b
5:  bis #4, r6 { jmp 6b
6:  bis #8, r6 { jmp 7b
7:  bis #2, r6 { jmp 8b
8:  bis #1, r6 { jmp 9b
9:  bis #1, r7 { jmp 10b
10: bis #2, r7 { jmp 11b
11: bis #8, r7 { jmp 12b
12: bis #4, r7 { jmp 13b
13: bis #4, r7 { jmp 14b
14: bis #8, r7 { jmp 15b
15: bis #2, r7 { jmp 16b
16: bis #1, r7
17:
    .exp1 = ((\exp & 0xf0) >> 4) | ((\exp & 0xf) << 8)
    .exp2 = ((\exp & 0xf000) >> 4) | ((\exp & 0xf00) >> 8)
    mov #.exp1, r4
    mov #.exp2, r5
    mov #99b, r12
    call #check
.endm

.macro jump_nop_direct insn
    \insn r4, r0
.endm

.macro jump_nop_indexed insn, base
    .reloc 1f + 2, R_MSP430_16_BYTE, test_src - \base
    mov r4, &test_src
1:
    mov #1000, r13
    \insn \base(r13), r0
.endm

.macro jump_nop_indirect insn
    mov #test_src, r13
    mov r4, &test_src
    \insn @r13, r0
.endm

.macro jump_nop_autoincrement insn
    mov r4, &test_src
    mov #test_src, r13
    \insn @r13+, r0
.endm

.macro jump_nop_symbolic insn
    mov r4, &test_src
    \insn test_src, r0
.endm

.macro jump_nop_immediate insn, arg1
    \insn #\arg1, r0
.endm

.macro jump_nop_absolute insn
    mov r4, &test_src
    \insn &test_src, r0
.endm

.macro test_jump_nop mode, insn, setup, incr, exec_args:vararg
.section .rodata
9:
    .asciz "test_jump_\insn\()_\mode\()_\@"

.text
test_\insn\()_\mode\()_\@:
    \setup
    jump_nop_\mode \insn \exec_args
.if \incr
    cmp #test_src + \incr, r13
    jeq 1f
    mov 9b, r12
    call #fail
1:
.endif
    call #pass
.endm

.macro test_jump_nops insn, arg, setup=no_setup, incr=2, base=0x0123
    mov #\arg, r4

    test_jump_nop direct, \insn, \setup, 0
    test_jump_nop indexed, \insn, \setup, 0, \base
    test_jump_nop indirect, \insn, \setup, 0
    test_jump_nop autoincrement, \insn, \setup, \incr
    test_jump_nop symbolic, \insn, \setup, 0
    test_jump_nop immediate, \insn, \setup, 0, \arg
    test_jump_nop absolute, \insn, \setup, 0
.endm

.macro branch_mov target
    .reloc 1f + 2, R_MSP430_16_BYTE, \target
1:
    mov #1000, r0
.endm

.macro branch_add target, insn, setup=no_setup
    .reloc 1f + 2, R_MSP430_16_PCREL_BYTE, \target - 4
    \setup
1:
    \insn #1000, r0
.endm

.macro branch_sub target, insn, setup=no_setup, off=0
    .reloc 1f + 2, R_MSP430_16_PCREL_BYTE, \target + \off - 6
    mov #0, r12
1:
    sub #1000, r12
    \setup
    \insn r12, r0
.endm

.macro test_branch branch, branch_args:vararg
.section .rodata
9:
    .asciz "test_\branch\()_\@ \branch_args"

.text
test_\branch\()_\@:
    push #4f
    \branch 3f, \branch_args
2:
    mov #9b, r12
    br #fail
3: 
    br #pass
4:
    push #5f
    \branch 3b, \branch_args
    mov #9b, r12
    br #fail
5:
.endm

.global main
main:
    mov #184, r12
    call #plan

                   ; C-C-C-C-C-C-C-C-
                   ; ZZ--ZZ--ZZ--ZZ--
                   ; NNNN----NNNN----
                   ; VVVVVVVV--------
    test_jump jz,  0b1100110011001100 ; Z=1
    test_jump jnz, 0b0011001100110011 ; Z=0
    test_jump jc,  0b1010101010101010 ; C=1
    test_jump jnc, 0b0101010101010101 ; C=0
    test_jump jn,  0b1111000011110000 ; N=1
    test_jump jge, 0b1111000000001111 ; N^V=0
    test_jump jl,  0b0000111111110000 ; N^V=1
    test_jump jmp, 0b1111111111111111 ; 1

    ; Test max-range jumps
    ; gas will convert the `jmp`s to `br`s if the displacement is too large (whoops)
.section .rodata
1:
    .asciz "test_max_range"

.text
test_max_range:
    mov #pass, r4
    mov #fail, r5
    mov #1b, r12
    push #3f
    jmp 2f
1:
    br r4
.rept 510
    br r5
.endr
2:
    jmp 1b

3:

    ; And now for some NOP jumps
    ; Ideally we would test flags as well, along with byte variants and one-ops
    ; But that is a bit too complex, since all the flags (except Z) may change
    ; depending on where we get linked to
    test_jump_nops add, 0
    test_jump_nops add, 1
    test_jump_nops addc, 0, clear_carry
    test_jump_nops addc, 1, clear_carry
    test_jump_nops addc, 0, set_carry
    test_jump_nops dadd, 0, clear_carry
    test_jump_nops dadd, 1, clear_carry
    test_jump_nops dadd, 0, set_carry

    test_jump_nops sub, 0
    test_jump_nops subc, 0, set_carry
    test_jump_nops cmp, 0
    test_jump_nops cmp, 0xffff
    test_jump_nops cmp, 0x5678

    test_jump_nops and, 0xffff
    test_jump_nops and, 0xfffe
    test_jump_nops bit, 0
    test_jump_nops bit, 0xffff
    test_jump_nops xor, 0
    test_jump_nops xor, 1
    test_jump_nops bis, 0
    test_jump_nops bis, 1
    test_jump_nops bic, 0
    test_jump_nops bic, 1

    ; OK now for some (small) relative jumps
    test_branch branch_mov

    test_branch branch_add, add
    test_branch branch_add, addc, clear_carry
    test_branch branch_add, addc, set_carry

    test_branch branch_sub, sub
    test_branch branch_sub, subc, set_carry, -2
    test_branch branch_sub, subc, clear_carry, -1

    mov #0, r15
    call #exit
