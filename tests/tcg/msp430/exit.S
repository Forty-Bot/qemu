; SPDX-License-Identifier: GPL-2.0-or-later
; Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>

.global _exit
_exit:
.global C$$EXIT
C$$EXIT:
    nop
1:
    jmp 1b
