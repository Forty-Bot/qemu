; SPDX-License-Identifier: GPL-2.0-or-later
; Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>

.data
.balign 2
test_src:
    .skip 2
test_dst:
    .skip 2

.text

; parameters are passed as follows:
;
; inputs:
; r2: flags
; r4: arg1
; r5: arg2
;
; outputs:
; r2: flags
; r12: result
; r13: &arg1
; r14: &arg2 aka &result
;
; r13 and r14 will not be set if the instruction does not need them
; this means r6-r11 and r15 are free to use

; one-operand instructions

.macro exec_direct insn
    mov r4, r12
    \insn r12
.endm

.macro exec_indexed insn, base
    .reloc 1f + 2, R_MSP430_16_BYTE, test_src - \base
    mov r4, &test_src
1:
    mov #1000, r13
    \insn \base(r13)
    mov &test_src, r12
.endm

.macro exec_indirect insn
    mov r4, &test_src
    mov #test_src, r13
    \insn @r13
    mov &test_src, r12
.endm

.macro exec_autoincrement insn
    mov r4, &test_src
    mov #test_src, r13
    \insn @r13+
    mov &test_src, r12
.endm

.macro exec_symbolic insn
    mov r4, &test_src
    \insn test_src
    mov &test_src, r12
.endm

.macro exec_immediate insn, arg
    \insn #\arg
.endm

.macro exec_absolute insn
    mov r4, &test_src
    \insn &test_src
    mov &test_src, r12
.endm

; two-operand instructions with direct destinations

.macro exec_direct_direct insn
    mov r5, r12
    \insn r4, r12
.endm

.macro exec_indexed_direct insn, base
    .reloc 1f + 2, R_MSP430_16_BYTE, test_src - \base
    mov r4, &test_src
    mov r5, r12
1:
    mov #1000, r13
    \insn \base(r13), r12
.endm

.macro exec_indirect_direct insn
    mov #test_src, r13
    mov r4, &test_src
    mov r5, r12
    \insn @r13, r12
.endm

.macro exec_autoincrement_direct insn
    mov r4, &test_src
    mov r5, r12
    mov #test_src, r13
    \insn @r13+, r12
.endm

.macro exec_symbolic_direct insn
    mov r4, &test_src
    mov r5, r12
    \insn test_src, r12
.endm

.macro exec_immediate_direct insn, arg1
    mov r5, r12
    \insn #\arg1, r12
.endm

.macro exec_absolute_direct insn
    mov r4, &test_src
    mov r5, r12
    \insn &test_src, r12
.endm

; two-operand instructions with indexed destinations

.macro exec_direct_indexed insn, base
    .reloc 1f + 2, R_MSP430_16_BYTE, test_dst - \base
    mov r5, &test_dst
1:
    mov #1000, r14
    \insn r4, \base(r14)
    mov &test_dst, r12
.endm

.macro exec_indexed_indexed insn, base1, base2
    .reloc 1f + 2, R_MSP430_16_BYTE, test_src - \base1
    .reloc 2f + 2, R_MSP430_16_BYTE, test_dst - \base2
    mov r4, &test_src
    mov r5, &test_dst
1:
    mov #1000, r13
2:
    mov #1000, r14
    \insn \base1(r13), \base2(r14)
    mov &test_dst, r12
.endm

.macro exec_indirect_indexed insn, base
    .reloc 1f + 2, R_MSP430_16_BYTE, test_dst - \base
    mov r4, &test_src
    mov r5, &test_dst
    mov #test_src, r13
1:
    mov #1000, r14
    \insn @r13, \base(r14)
    mov &test_dst, r12
.endm

.macro exec_autoincrement_indexed insn, base
    .reloc 1f + 2, R_MSP430_16_BYTE, test_dst - \base
    mov r4, &test_src
    mov r5, &test_dst
    mov #test_src, r13
1:
    mov #1000, r14
    \insn @r13+, \base(r14)
    mov &test_dst, r12
.endm

.macro exec_symbolic_indexed insn, base
    .reloc 1f + 2, R_MSP430_16_BYTE, test_dst - \base
    mov r4, &test_src
    mov r5, &test_dst
1:
    mov #1000, r14
    \insn test_src, \base(r14)
    mov &test_dst, r12
.endm

.macro exec_immediate_indexed insn, arg1, base
    .reloc 1f + 2, R_MSP430_16_BYTE, test_dst - \base
    mov r5, &test_dst
1:
    mov #1000, r14
    \insn #\arg1, \base(r14)
    mov &test_dst, r12
.endm

.macro exec_absolute_indexed insn, base
    .reloc 1f + 2, R_MSP430_16_BYTE, test_dst - \base
    mov r4, &test_src
    mov r5, &test_dst
1:
    mov #1000, r14
    \insn &test_src, \base(r14)
    mov &test_dst, r12
.endm

; two-operand instructions with symbolic destinations

.macro exec_direct_symbolic insn
    mov r5, &test_dst
    \insn r4, test_dst
    mov &test_dst, r12
.endm

.macro exec_indexed_symbolic insn, base
    .reloc 1f + 2, R_MSP430_16_BYTE, test_src - \base
    mov r4, &test_src
    mov r5, &test_dst
1:
    mov #1000, r13
    \insn \base(r13), test_dst
    mov &test_dst, r12
.endm

.macro exec_indirect_symbolic insn
    mov r4, &test_src
    mov r5, &test_dst
    mov #test_src, r13
    \insn @r13, test_dst
    mov &test_dst, r12
.endm

.macro exec_autoincrement_symbolic insn
    mov r5, &test_dst
    mov r4, &test_src
    mov #test_src, r13
    \insn @r13+, test_dst
    mov &test_dst, r12
.endm

.macro exec_symbolic_symbolic insn
    mov r4, &test_src
    mov r5, &test_dst
    \insn test_src, test_dst
    mov &test_dst, r12
.endm

.macro exec_immediate_symbolic insn, arg1
    mov r5, &test_dst
    \insn #\arg1, test_dst
    mov &test_dst, r12
.endm

.macro exec_absolute_symbolic insn
    mov r4, &test_src
    mov r5, &test_dst
    \insn &test_src, test_dst
    mov &test_dst, r12
.endm

; two-operand instructions with absolute destinations

.macro exec_direct_absolute insn
    mov r5, &test_dst
    \insn r4, &test_dst
    mov &test_dst, r12
.endm

.macro exec_indexed_absolute insn, base
    .reloc 1f + 2, R_MSP430_16_BYTE, test_src - \base
    mov r4, &test_src
    mov r5, &test_dst
1:
    mov #1000, r13
    \insn \base(r13), &test_dst
    mov &test_dst, r12
.endm

.macro exec_indirect_absolute insn
    mov r5, &test_dst
    mov r4, &test_src
    mov #test_src, r13
    \insn @r13, &test_dst
    mov &test_dst, r12
.endm

.macro exec_autoincrement_absolute insn
    mov r4, &test_src
    mov r5, &test_dst
    mov #test_src, r13
    \insn @r13+, &test_dst
    mov &test_dst, r12
.endm

.macro exec_symbolic_absolute insn
    mov r5, &test_dst
    mov r4, &test_src
    \insn test_src, &test_dst
    mov &test_dst, r12
.endm

.macro exec_immediate_absolute insn, arg1
    mov r5, &test_dst
    \insn #\arg1, &test_dst
    mov &test_dst, r12
.endm

.macro exec_absolute_absolute insn
    mov r4, &test_src
    mov r5, &test_dst
    \insn &test_src, &test_dst
    mov &test_dst, r12
.endm

; two-operand instructions with the same source/destination

.macro exec_indexed_direct_same insn, arg2
    .reloc 1f + 2, R_MSP430_16_BYTE, test_src - \arg2
    mov r4, &test_src
    mov r5, r12
1:
    \insn 1000(r12), r12
.endm

.macro exec_direct_indexed_same insn, arg1
    .reloc 1f + 2, R_MSP430_16_BYTE, test_dst - \arg1
    mov r5, &test_dst
1:
    \insn r4, 1000(r4)
    mov &test_dst, r12
.endm

.macro exec_indexed_indexed_same insn, base
    .reloc 1f + 2, R_MSP430_16_BYTE, test_src - \base
    .reloc 1f + 4, R_MSP430_16_BYTE, test_dst - \base
    mov r4, &test_src
    mov r5, &test_dst
    mov #\base, r13
1:
    \insn 1000(r13), 1000(r13)
    mov &test_dst, r12
.endm

.macro exec_indirect_indexed_same insn
    .reloc 1f + 2, R_MSP430_16_BYTE, test_dst - test_src
    mov r4, &test_src
    mov r5, &test_dst
    mov #test_src, r13
1:
    \insn @r13, 1000(r13)
    mov &test_dst, r12
.endm

.macro exec_autoincrement_indexed_same insn, incr
    .reloc 1f + 2, R_MSP430_16_BYTE, test_dst - test_src - \incr
    mov r4, &test_src
    mov r5, &test_dst
    mov #test_src, r13
1:
    \insn @r13+, 1000(r13)
    mov &test_dst, r12
.endm

; Test macros

; checks and setups

.macro no_setup
.endm

do_check_incr:
    mov r2, r11
    cmp r15, r13
    jne 1f
    mov r11, r2
do_check:
    cmp r7, r2
    jne 1f
    cmp r6, r12
    jne 1f
    br #pass
1:
    mov r14, r12
    br #fail

.macro setup exp, sr
    mov #\exp, r6
    mov #\sr, r7
.endm

.macro set_carry
    setc
.endm

.macro clear_carry
    clrc
.endm

.macro setupc exp, sr, carry
    setup \exp, \sr
.if \carry
    setc
.else
    clrc
.endif
.endm

.macro setupb exp_direct, exp_indirect, sr
    mov #\sr, r7
    mov #\exp_direct, r8
    mov #\exp_indirect, r9
.endm

.macro check msg, direct, incr
    mov #\msg, r14
.if \incr
    mov #test_src + \incr, r15
    call #do_check_incr
.else
    call #do_check
.endif
.endm

.macro checkb msg, direct, incr
.if \direct
    mov r8, r6
.else
    mov r9, r6
.endif
    check \msg, \direct, \incr
.endm

; The test macros themselves

.macro test_op1_mode mode, insn, check, setup, direct, incr, exec_args:vararg
.section .rodata
9:
    .asciz "test_\insn\()_\mode\()_\@"

.text
test_\insn\()_\mode\()_\@:
    \setup
    exec_\mode \insn \exec_args
    \check 9b, \direct, \incr
.endm

.macro test_op1_modes insn, arg, check, setup=no_setup, incr=2, base=0x0123
    mov #\arg, r4

    test_op1_mode direct, \insn, \check, \setup, 1, 0
    test_op1_mode indexed, \insn, \check, \setup, 0, 0, \base
    test_op1_mode indirect, \insn, \check, \setup, 0, 0
    test_op1_mode autoincrement, \insn, \check, \setup, 0, \incr
    test_op1_mode symbolic, \insn, \check, \setup, 0, 0
    test_op1_mode absolute, \insn, \check, \setup, 0, 0
.endm

.macro test_op1_modes_full insn, arg, check, setup=no_setup, incr=2, base=0x0123
    test_op1_mode immediate, \insn, \check, \setup, 0, 0, \arg
    test_op1_modes \insn, \arg, \check, \setup, \incr, \base
.endm

.macro test_op2_mode mode, insn, check, setup, incr, direct, exec_args:vararg
.section .rodata
9:
    .asciz "test_\insn\()_\mode\()_\@"

.text
test_\insn\()_\mode\()_\@:
    \setup
    exec_\mode \insn \exec_args
    \check 9b, \incr, \direct
.endm

.macro test_op2_modes insn, arg1, arg2, check, setup=no_setup, incr=2, base1=0x0123, base2=0x4567
    mov #\arg1, r4
    mov #\arg2, r5

    test_op2_mode direct_direct, \insn, \check, \setup, 1, 0
    test_op2_mode indexed_direct, \insn, \check, \setup, 1, 0, \base1
    test_op2_mode indirect_direct, \insn, \check, \setup, 1, 0
    test_op2_mode autoincrement_direct, \insn, \check, \setup, 1, \incr
    test_op2_mode symbolic_direct, \insn, \check, \setup, 1, 0
    test_op2_mode immediate_direct, \insn, \check, \setup, 1, 0, \arg1
    test_op2_mode absolute_direct, \insn, \check, \setup, 1, 0
    test_op2_mode direct_indexed, \insn, \check, \setup, 0, 0, \base1
    test_op2_mode indexed_indexed, \insn, \check, \setup, 0, 0, \base1, \base2
    test_op2_mode indirect_indexed, \insn, \check, \setup, 0, 0, \base1
    test_op2_mode autoincrement_indexed, \insn, \check, \setup, 0, \incr, \base1
    test_op2_mode symbolic_indexed, \insn, \check, \setup, 0, 0, \base1
    test_op2_mode immediate_indexed, \insn, \check, \setup, 0, 0, \arg1, \base1
    test_op2_mode absolute_indexed, \insn, \check, \setup, 0, 0, \base1
    test_op2_mode direct_symbolic, \insn, \check, \setup, 0, 0
    test_op2_mode indexed_symbolic, \insn, \check, \setup, 0, 0, \base1
    test_op2_mode indirect_symbolic, \insn, \check, \setup, 0, 0
    test_op2_mode autoincrement_symbolic, \insn, \check, \setup, 0, \incr
    test_op2_mode symbolic_symbolic, \insn, \check, \setup, 0, 0
    test_op2_mode immediate_symbolic, \insn, \check, \setup, 0, 0, \arg1
    test_op2_mode absolute_symbolic, \insn, \check, \setup, 0, 0
    test_op2_mode direct_absolute, \insn, \check, \setup, 0, 0
    test_op2_mode indexed_absolute, \insn, \check, \setup, 0, 0, \base1
    test_op2_mode indirect_absolute, \insn, \check, \setup, 0, 0
    test_op2_mode autoincrement_absolute, \insn, \check, \setup, 0, \incr
    test_op2_mode symbolic_absolute, \insn, \check, \setup, 0, 0
    test_op2_mode immediate_absolute, \insn, \check, \setup, 0, 0, \arg1
    test_op2_mode absolute_absolute, \insn, \check, \setup, 0, 0
    test_op2_mode direct_indexed_same, \insn, \check, \setup, 0, 0, \arg1
    test_op2_mode indexed_direct_same, \insn, \check, \setup, 1, 0, \arg2
    test_op2_mode indexed_indexed_same, \insn, \check, \setup, 0, 0, \base1
    test_op2_mode indirect_indexed_same, \insn, \check, \setup, 0, 0
    test_op2_mode autoincrement_indexed_same, \insn, \check, \setup, 0, \incr, \incr
.endm

.macro test_op1_single insn, arg, check, setup=no_setup, setup_args:vararg
.section .rodata
9:
    .asciz "test_\insn\()_\@ (\arg)"

.text
test_\insn\()_\@:
    \setup \setup_args
    mov #\arg, r12
    \insn r12
    \check 9b, 1, 0
.endm

.macro test_op2_single insn, arg1, arg2, check, setup=no_setup, setup_args:vararg
.section .rodata
9:
    .asciz "test_\insn\()_\@ (\arg1, \arg2)"

.text
test_\insn\()_\@:
    \setup \setup_args
    mov #\arg2, r12
    \insn #\arg1, r12
    \check 9b, 1, 0
.endm
