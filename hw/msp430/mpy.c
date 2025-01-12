/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 Sean Anderson <seanga2@gmail.com>
 */

#include "qemu/osdep.h"
#include "hw/msp430/mpy.h"
#include "hw/registerfields.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

REG16(MPY,    0x0)
REG16(MPYS,   0x2)
REG16(MAC,    0x4)
REG16(MACS,   0x6)
REG16(OP2,    0x8)
REG16(RESLO,  0xa)
REG16(RESHI,  0xc)
REG16(SUMEXT, 0xe)

REG16(MPY32L,  0x10)
REG16(MPY32H,  0x12)
REG16(MPYS32L, 0x14)
REG16(MPYS32H, 0x16)
REG16(MAC32L,  0x18)
REG16(MAC32H,  0x1a)
REG16(MACS32L, 0x1c)
REG16(MACS32H, 0x1e)
REG16(OP2L,    0x20)
REG16(OP2H,    0x22)
REG16(RES0,    0x24)
REG16(RES1,    0x26)
REG16(RES2,    0x28)
REG16(RES3,    0x2a)
REG16(CTL0,    0x2c)
    FIELD(CTL0, OP2_32, 7, 1)
    FIELD(CTL0, OP1_32, 6, 1)
    FIELD(CTL0, M, 4, 2)
    FIELD(CTL0, SAT, 3, 1)
    FIELD(CTL0, FRAC, 2, 1)
    FIELD(CTL0, C, 0, 1)

#define R_CTL0_OP_32_MASK (R_CTL0_OP1_32_MASK | R_CTL0_OP2_32_MASK)

#define R_CTL0_M_MPY    0
#define R_CTL0_M_MPYS   1
#define R_CTL0_M_MAC    2
#define R_CTL0_M_MACS   3

typedef struct {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    bool is32;
} MSP430MPYClass;

DECLARE_CLASS_CHECKERS(MSP430MPYClass, MSP430_MPY, TYPE_MSP430_MPY)

static int mpy_shift(MSP430MPYState *mpy)
{
    return mpy->ctl0 & R_CTL0_OP_32_MASK ? 63 : 31;
}

static uint64_t mpy_underflow(MSP430MPYState *mpy, int shift)
{
    return (mpy->res & ~0ULL << (shift + 1)) | BIT_ULL(shift);
}

static uint64_t mpy_overflow(MSP430MPYState *mpy, int shift)
{
    return (mpy->res & ~0ULL << (shift + 1)) | (BIT_ULL(shift) - 1);
}

static uint64_t mpy_saturate(MSP430MPYState *mpy)
{
    int shift = mpy_shift(mpy);
    bool msb_set = mpy->res & BIT(shift);

    if (mpy->ctl0 & R_CTL0_C_MASK) {
        if (!msb_set) {
            return mpy_underflow(mpy, shift);
        }
    } else if (msb_set) {
        return mpy_overflow(mpy, shift);
    }

    if (mpy->ctl0 & R_CTL0_FRAC_MASK) {
        if (msb_set) {
            if (mpy->res & BIT(shift - 1)) {
                return mpy_underflow(mpy, shift);
            }
        } else if (!(mpy->res & BIT(shift - 1))) {
            return mpy_overflow(mpy, shift);
        }
    }

    return mpy->res;
}

static uint64_t mpy_read(void *opaque, hwaddr addr, unsigned size)
{
    MSP430MPYState *mpy = opaque;
    uint64_t res = mpy->ctl0 & R_CTL0_SAT_MASK ? mpy_saturate(mpy) : mpy->res;

    if (mpy->ctl0 & R_CTL0_FRAC_MASK) {
        res <<= 1;
    }

    switch (addr >> 1) {
    case R_MPY ... R_MACS:
        return extract32(mpy->op1, (addr & 1) * 8, size * 8);
    case R_MPY32L ... R_MACS32H:
        return extract32(mpy->op1, (addr & 3) * 8, size * 8);
    case R_OP2:
        return extract32(mpy->op2, (addr & 1) * 8, size * 8);
    case R_OP2L:
    case R_OP2H:
        return extract32(mpy->op2, (addr & 3) * 8, size * 8);
    case R_RESLO:
    case R_RESHI:
        return extract64(res, (addr - A_RESLO) * 8, size * 8);
    case R_RES0 ... R_RES3:
        return extract64(res, (addr - A_RES0) * 8, size * 8);
    case R_SUMEXT: {
        uint16_t sumext;

        if (FIELD_EX16(mpy->ctl0, CTL0, M) & R_CTL0_M_MPYS) {
            sumext = (mpy->res & BIT_ULL(mpy_shift(mpy))) ? 0xffff : 0;
        } else {
            sumext = mpy->ctl0 & R_CTL0_C_MASK;
        }

        return extract16(sumext, (addr & 1) * 8, size * 8);
    }
    case R_CTL0:
        if (addr & 1) {
            return 0;
        }
        return mpy->ctl0;
    }

    g_assert_not_reached();
    return UINT64_MAX;
}

static uint32_t mpy_deposit(MSP430MPYClass *mpc, uint32_t op, hwaddr addr,
                            uint64_t val, unsigned size, uint16_t m, int shift)
{
    if (size == 1) {
        if (addr & 1) {
            return deposit32(op, shift + 8, 8, val);
        }

        if (m & R_CTL0_M_MPYS) {
            val = (int16_t)(int8_t)val;
        }
    }

    return deposit32(op, shift, 16, val);
}

static void mpy_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MSP430MPYState *mpy = opaque;
    MSP430MPYClass *mpc = MSP430_MPY_GET_CLASS(mpy);
    uint16_t m, new_m;

    m = FIELD_EX16(mpy->ctl0, CTL0, M);
    if (addr < A_MPY32L) {
        new_m = (addr >> 1) & 3;
    } else {
        new_m = (addr >> 2) & 3;
    }

    switch (addr >> 1) {
    case R_MPY:
    case R_MPYS:
    case R_MAC:
    case R_MACS:
        mpy->op1 &= 0xffff;
        /* fallthrough */
    case R_MPY32L:
    case R_MPYS32L:
    case R_MAC32L:
    case R_MACS32L:
        mpy->op1 = mpy_deposit(mpc, mpy->op1, addr, val, size, new_m, 0);
        mpy->ctl0 &= ~R_CTL0_OP1_32_MASK;
        mpy->ctl0 = FIELD_DP16(mpy->ctl0, CTL0, M, new_m);
        return;
    case R_MPYS32H:
    case R_MACS32H:
    case R_MAC32H:
    case R_MPY32H:
        mpy->op1 = mpy_deposit(mpc, mpy->op1, addr, val, size, new_m, 16);
        mpy->ctl0 |= R_CTL0_OP1_32_MASK;
        return;
    case R_OP2:
        mpy->op2 &= 0xffff;
        mpy->op2 = mpy_deposit(mpc, mpy->op2, addr, val, size, m, 0);
        mpy->ctl0 &= ~R_CTL0_OP2_32_MASK;
        break;
    case R_OP2L:
        mpy->op2 = mpy_deposit(mpc, mpy->op2, addr, val, size, m, 0);
        mpy->ctl0 |= R_CTL0_OP2_32_MASK;
        mpy->expecting_op2h = true;
        return;
    case R_OP2H:
        if (!mpy->expecting_op2h) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "msp430_mpy: ignoring unexpected write to OP2H\n");
            return;
        }

        mpy->op2 = mpy_deposit(mpc, mpy->op2, addr, val, size, m, 16);
        mpy->expecting_op2h = false;
        break;
    case R_RESLO:
    case R_RESHI:
        mpy->res &= 0xffffffff;
        mpy->res = deposit64(mpy->res, (addr - A_RESLO) * 8, size * 8, val);
        return;
    case R_RES0:
    case R_RES1:
    case R_RES2:
    case R_RES3:
        mpy->res = deposit64(mpy->res, (addr - A_RES0) * 8, size * 8, val);
        return;
    case R_SUMEXT:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "msp430_mpy: write to read-only register SUMEXT\n");
        return;
    case R_CTL0:
        if (!addr & 1) {
            mpy->ctl0 = val;
        }
        return;
    default:
        g_assert_not_reached();
    }

    if ((m & R_CTL0_M_MAC) && (mpy->ctl0 & R_CTL0_SAT_MASK)) {
        mpy->res = mpy_saturate(mpy);
    }

    if (mpy->ctl0 & R_CTL0_OP_32_MASK) {
        uint64_t res;

        if (m & R_CTL0_M_MPYS) {
            res = (int64_t)(int32_t)mpy->op1 * (int32_t)mpy->op2;
        } else {
            res = (uint64_t)mpy->op1 * mpy->op2;
        }

        if (m & R_CTL0_M_MAC) {
            if (uadd64_overflow(res, mpy->res, &mpy->res)) {
                mpy->ctl0 |= R_CTL0_C_MASK;
            } else {
                mpy->ctl0 &= ~R_CTL0_C_MASK;
            }
        } else {
            mpy->res = res;
            mpy->ctl0 &= ~R_CTL0_C_MASK;
        }
    } else {
        uint64_t res;

        if (m & R_CTL0_M_MPYS) {
            res = (int64_t)((int32_t)(int16_t)mpy->op1 * (int16_t)mpy->op2);
        } else {
            res = (uint32_t)(uint16_t)mpy->op1 * (uint16_t)mpy->op2;
        }

        if (m & R_CTL0_M_MAC) {
            uint32_t reslo;
            bool carry = uadd32_overflow(res, mpy->res, &reslo);

            res = (mpy->res >> 32) + (res >> 32) + carry;
            res <<= 32;
            res |= reslo;
            if (carry) {
                mpy->ctl0 |= R_CTL0_C_MASK;
            } else {
                mpy->ctl0 &= ~R_CTL0_C_MASK;
            }
        } else {
            mpy->ctl0 &= ~R_CTL0_C_MASK;
        }

        mpy->res = res;
    }
}

static const MemoryRegionOps mpy_ops = {
    .write = mpy_write,
    .read  = mpy_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static void mpy_reset(MSP430MPYState *mpy)
{
        mpy->ctl0 &= ~0x0e;
        mpy->expecting_op2h = false;
}

static void mpy_reset_hold(Object *obj, ResetType type)
{
    MSP430MPYState *mpy = MSP430_MPY(obj);

    if (type != RESET_TYPE_GUEST) {
        mpy->res = g_random_int();
        mpy->op1 = g_random_int();
        mpy->op2 = g_random_int();
        mpy->ctl0 = g_random_int();
    }
    mpy_reset(mpy);
}

static void mpy_puc(void *opaque, int irq, int level)
{
    MSP430MPYState *mpy = opaque;

    if (level) {
        mpy_reset(mpy);
    }
}

static void mpy_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MSP430MPYState *mpy = MSP430_MPY(obj);
    MSP430MPYClass *mpc = MSP430_MPY_GET_CLASS(mpy);

    memory_region_init_io(&mpy->memory, OBJECT(mpy), &mpy_ops, mpy,
                          "msp430-mpy", mpc->is32 ? 0x3e : 0x10);
    sysbus_init_mmio(d, &mpy->memory);

    qdev_init_gpio_in_named(DEVICE(d), mpy_puc, "puc", 1);
}

static const VMStateDescription vmstate_mpy = {
    .name = "msp430-mpy",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(res, MSP430MPYState),
        VMSTATE_UINT32(op1, MSP430MPYState),
        VMSTATE_UINT32(op2, MSP430MPYState),
        VMSTATE_UINT8(ctl0, MSP430MPYState),
        VMSTATE_BOOL(expecting_op2h, MSP430MPYState),
        VMSTATE_END_OF_LIST()
    }
};

static void mpy_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "MSP430 16-bit hardware multiplier";
    dc->vmsd = &vmstate_mpy;
    rc->phases.hold = mpy_reset_hold;
}

static void mpy32_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MSP430MPYClass *mpc = MSP430_MPY_CLASS(oc);

    dc->desc = "MSP430 32-bit hardware multiplier";
    mpc->is32 = true;
}

static const TypeInfo mpy_types[] = {
    {
        .parent = TYPE_SYS_BUS_DEVICE,
        .name = TYPE_MSP430_MPY,
        .instance_size = sizeof(MSP430MPYState),
        .instance_init = mpy_init,
        .class_init = mpy_class_init,
    },
    {
        .parent = TYPE_MSP430_MPY,
        .name = TYPE_MSP430_MPY32,
        .class_init = mpy32_class_init,
    },
};

DEFINE_TYPES(mpy_types);
