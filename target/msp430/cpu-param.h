/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 Sean Anderson <seanga2@gmail.com>
 */

#ifndef MSP430_CPU_PARAM_H
#define MSP430_CPU_PARAM_H

#define TARGET_LONG_BITS 32
#define TARGET_PAGE_BITS 9

/* QEMU can't handle any smaller address space */
#define TARGET_PHYS_ADDR_SPACE_BITS 24
#define TARGET_VIRT_ADDR_SPACE_BITS 24

#endif /* MSP430_CPU_PARAM_H */
