/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include <msp430.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tap.h"

static volatile uint16_t wdtirq_count = 0;

static __interrupt_vec(WDT_VECTOR) void wdt(void)
{
    wdtirq_count++;
}

void test_wdt_poll(uint16_t ctl, uint16_t timeout)
{
    TACTL |= TACLR;
    IFG1 &= ~WDTIFG;
    WDTCTL = ctl;
    while (!(IFG1 & WDTIFG)) {
        if (TAR > timeout) {
            fail("time out!");
            return;
        }
    }
    pass();
}

int main(void)
{
    /* We don't use it and it doesn't reset with PUC */
    switch (TACCR1++) {
    case 0:
        break;
    case 1:
        next_test = 9;
        goto after_reset;
    case 2:
        next_test = 10;
        goto after_pw;
    default:
        bail();
    }

    plan(10);

    TACTL = TASSEL_2 | MC_2 | TACLR;
    test_wdt_poll(WDT_MDLY_32, 35000);
    test_wdt_poll(WDT_MDLY_8, 9000);
    test_wdt_poll(WDT_MDLY_0_5, 600);
    test_wdt_poll(WDT_MDLY_0_064, 100);

    TACTL |= TACLR;
    IFG1 &= ~WDTIFG;
    WDTCTL = WDT_MDLY_0_064 | WDTHOLD;
    while (TAR < 100);
    ok(!(IFG1 & WDTIFG));

    is(0, wdtirq_count);
    WDTCTL = WDT_MDLY_0_064;
    IE1 |= WDTIE;
    _enable_interrupts();
    /* Do some I/O to force the end of a TCG block */
    TACTL |= TACLR;
    is(1, wdtirq_count);
    ok(!(IFG1 & WDTIFG));

    TACTL |= TACLR;
    WDTCTL = WDT_MRST_0_5;
    while (TAR < 600);
    fail("never reset");
    goto test_pw;

after_reset:
    pass();

test_pw:
    WDTCTL = 0;
    fail("never reset");
    return 0;

after_pw:
    pass();
    return 0;
}
