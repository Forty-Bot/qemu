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

.data
.balign 2
saved_sp:
    .skip 2

.text
.global main
main:
    mov #1, r12
    call #plan

    mov r1, &saved_sp
    mov #0x1110, r1
    mov #0x0000, r2
    mov #0x4444, r4
    mov #0x5555, r5
    mov #0x6666, r6
    mov #0x7777, r7
    mov #0x8888, r8
    mov #0x9999, r9
    mov #0xaaaa, r10
    mov #0xbbbb, r11
    mov #0xcccc, r12
    mov #0xdddd, r13
    mov #0xeeee, r14
    mov #0xffff, r15

    ; two-operand instructions
    ; done with arithmetic to avoid getting the assembler confused
.irp base, 0x4000, 0x8000, 0xc000
    i = 0
.rept 0x400
    .dc \base | (i << 4) | 3
    i = i + 1
.endr
.endr

    ; one-operand instructions
.irp val, 0, 1, 2, 4, 8, -1
    rrc #\val
    rrc.b #\val
    rra #\val
    rra.b #\val
    swpb #\val
    sxt #\val
.endr

    cmp #0x0000, r2  { jne 2f
    cmp #0x1110, r1  { jne 2f
    cmp #0x4444, r4  { jne 2f
    cmp #0x5555, r5  { jne 2f
    cmp #0x6666, r6  { jne 2f
    cmp #0x7777, r7  { jne 2f
    cmp #0x8888, r8  { jne 2f
    cmp #0x9999, r9  { jne 2f
    cmp #0xaaaa, r10 { jne 2f
    cmp #0xbbbb, r11 { jne 2f
    cmp #0xcccc, r12 { jne 2f
    cmp #0xdddd, r13 { jne 2f
    cmp #0xeeee, r14 { jne 2f
    cmp #0xffff, r15 { jne 2f

    mov &saved_sp, r1
    call #pass
    jmp 3f

.section .rodata
1:
    .asciz "test_nop"

.text
2:
    mov &saved_sp, r1
    mov 1b, r12
    call #fail

3:
    mov #0, r15
    call #exit
