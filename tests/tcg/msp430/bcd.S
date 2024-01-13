; SPDX-License-Identifier: GPL-2.0-or-later
; Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
; A collection of base-conversion routines for printf

.macro lentry name
.section .text.\name
.type \name STT_FUNC
\name:
.endm

.macro entry name
.global \name
lentry \name
.endm

.macro lconst name
.section .rodata.\name
.type \name STT_OBJECT
\name:
.endm

.macro const name
.global \name
lconst \name
.endm

.macro ldata name
.section .data.\name
.type \name STT_OBJECT
\name:
.endm

.macro data name
.global \name
ldata \name
.endm

.macro end name
.size \name, . - \name
.endm

; itod() - Convert an int to BCD
; unsigned long itod(unsigned int u)
; @u: int to convert
;
; This (and the following function) converts from base-2 to base-10 using method
; 1b from Knuth's TAoCP vol. 2 section 4.4.A. That is, we calculate
;
; 2**15 * u_15 + 2**14 * u_14 + ... + 2 * u_1 + u_0
;
; in base-10 using Horner's method.
;
; Return: @u converted to (packed) BCD
entry itod
    ; r11 - @u
    ; r12:r13 - return value
    ; r15 - loop counter
    mov r12, r11
    clr r12
    clr r13
    mov #16, r15
1:
    rla r11
    dadd r12, r12
    dadd r13, r13
    dec r15
    jnz 1b
    ret
end itod

; ltod() - Convert a long to BCD
; unsigned long long ltod(unsigned long u)
; @u: long to convert
;
; Return: @u converted to (packed) BCD
entry ltod
    ; r11 - lower half of @n
    ; r12:r14 return value
    ; r15: loop counter
    push r12
    mov r13, r12
    call #itod
    pop r11
    clr r14
    mov #16, r15
1:
    rla r11
    dadd r12, r12
    dadd r13, r13
    dadd r14, r14
    dec r15
    jnz 1b
    ret
end ltod

; lltod() - Convert a long long to BCD
; unsigned long long ltod(unsigned long u)
; @u: long to convert
;
; Return: @u converted to (packed) BCD
entry lltod
    ; r11 - lower half of @n
    ; r12:r14 return value
    ; r15: loop counter
    push r12
    mov r13, r12
    call #ltod
    pop r10
    clr r14
    mov #32, r15
1:
    rla r11
    dadd r12, r12
    dadd r13, r13
    dadd r14, r14
    dec r15
    jnz 1b
    ret
end lltod

; dtoa() - Convert BCD to ASCII
; char *dtoa(unsigned int d, char buf[4])
; @d: BCD number to convert to ASCII
; @buf: buffer to store the result in
;
; Convert @d to ASCII, including leading zeros. No nul-terminator is added.
;
; Return: @buf in r13
lentry _dtoa
    ; r12 - Even digits in @d
    ; r13 - @buf
    ; r14 - Odd digits in @d
    mov r12, r14
    and #0x0f0f, r12
    and #0xf0f0, r14
    add #0x3030, r12
    rrc r14 ; No carry from the above addition
    rrc r14
    rrc r14
    rrc r14
    add #0x3030, r14

    ; If @d = 0x5678, we now have r12 = 0x3638 and r14 = 0x3537
    mov.b r12, 3(r13)
    mov.b r14, 2(r13)
    swpb r12
    swpb r14
    mov.b r12, 1(r13)
    mov.b r14, 0(r13)
    mov r13, r12
    ret
end _dtoa

; dtoa10() - Convert a BCD-string to a base-10 ASCII string
; char *dtoa10(size_t n, char buf[n * 4 + 1], unsigned int d, ...)
; @n: Number of words in the BCD-string
; @buf: Buffer to store the result in of size @n * 4 + 1
; @d: Most-significant digit in the BCD-string
; @...: Lesser-significance digits in the BCD-string
;
; Convert a BCD-string beginning with @d and continuing with the variadic
; arguments to a base-10 ASCII string. The BCD digits should be right-aligned
; within the BCD-string, such that all numbers in @... contain four digits, and
; only @d may contain fewer than four digits.
;
; Return: The number converted to a string, located within @buf
entry dtoa10
    ; r9 - loop counter
    ; r10 - &@d
    ; r12 - current BCD word
    ; r13 - current pointer into @buf
    push r10
    push r9
    push r12
    mov r12, r9
    mov r1, r10
    add #8, r10
1:
    mov @r10+, r12
    call #_dtoa
    add #4, r13
    dec r9
    jnz 1b
    mov.b #0, 0(r13) ; nul-terminator

    ; r12 - @buf
    ; r13 - maximum leading zeros to remove
    mov r13, r12
    pop r13
    rla r13
    rla r13
    sub r13, r12
    dec r13
    pop r9
    pop r10
    ; Explicitly reference strnffnz so we can fall through
    .reloc ., R_MSP430_NONE, strnffnz
end dtoa10

; strnffnz() - Find the first non-zero character in a string
; char *strnffnz(char *s, unsigned int n)
; @s: String to search
; @n: Length of @s; must be non-zero
;
; Find the first non-zero character in @s. This may be used to remove leading
; zeros from a number. By example,
;
; strnffnz("0012", 1) = "012"
; strnffnz("0012", 2) = "12"
; strnffnz("0000", 3) = "0"
;
; Return: The first non-zero character of @s, or one character past the end of @s if
; all characters are zero.
entry strnffnz
    ; r12 - @s
    ; r13 - @n
    cmp.b #0x30, 0(r12)
    jne 1f
    inc r12
    dec r13
    jnz strnffnz
1:
    ret
end strnffnz

; itoa10() - Convert an int to a base-10 ASCII string
; char *itoa10(unsigned int n, char buf[6])
; @n: int to convert
; @buf: Buffer to store the result in
;
; Return: @n converted to a string, located within @buf
entry itoa10
    push r10
    mov r13, r10
    call #itod
    push r12
    push r13
    mov #2, r12
    mov r10, r13
    call #dtoa10
    add #4, r1
    pop r10
    ret
end itoa10

; ltoa10() - Convert a long to a base-10 ASCII string
; char *ltoa10(unsigned long n, char buf[13])
; @n: long to convert
; @buf: Buffer to store the result in
;
; Return: @n converted to a string, located within @buf
entry ltoa10
    push r10
    mov r14, r10
    call #ltod
    push r12
    push r13
    push r14
    mov #3, r12
    mov r10, r13
    call #dtoa10
    mov 6(r1), r10
    add #8, r1
    ret
end ltoa10

const digits
    .ascii "0123456789abcdef"
end digits

; _itoa16() - Convert a number to hexadecimal
; char *dtoa(unsigned int d, char buf[4])
; @d: number to convert to hexadecimal
; @buf: buffer to store the result in
;
; Convert @d to hexadecimal, including leading zeros. No nul-terminator is
; added.
;
; Return: @buf in r13
lentry _itoa16
    ; r11 - Digit to write
    ; r12 - Even digits in @d
    ; r13 - @buf
    ; r14 - Odd digits in @d
    mov r12, r14
    and #0x0f0f, r12
    and #0xf0f0, r14
    clrc
    rrc r14
    rrc r14
    rrc r14
    rrc r14

    ; If @d = 0x89ab, we now have r12 = 0x090b and r14 = 0x080a
    mov.b r12, r11
    mov.b digits(r11), 3(r13)
    mov.b r14, r11
    mov.b digits(r11), 2(r13)
    swpb r12
    swpb r14
    mov.b r12, r11
    mov.b digits(r11), 1(r13)
    mov.b r14, r11
    mov.b digits(r11), 0(r13)
    ret
end _itoa16

; itoa16() - Convert an int to a base-16 ASCII string
; char *itoa16(unsigned int n, char buf[5], int hex_adj)
; @n: int to convert
; @buf: Buffer to store the result in
;
; Return: @n converted to a string, located within @buf
entry itoa16
    call #_itoa16
    mov.b #0, 4(r13) ; nul-terminator
    mov r13, r12
    mov #3, r13
    jump #strnffnz
end itoa16

; ltoa16() - Convert a long to a base-16 ASCII string
; char *ltoa16(unsigned long n, char buf[9], int hex_adj)
; @n: int to convert
; @buf: Buffer to store the result in
;
; Return: @n converted to a string, located within @buf
entry ltoa16
    push r10
    mov r12, r10
    mov r13, r12
    mov r14, r13
    call #_itoa16
    mov r10, r12
    add #4, r13
    call #_itoa16
    mov.b #0, 4(r13) ; nul-terminator
    mov r13, r12
    sub #4, r12
    mov #7, r13
    pop r10
    jump #strnffnz
end ltoa16
