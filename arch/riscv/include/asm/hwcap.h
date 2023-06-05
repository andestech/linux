/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copied from arch/arm64/include/asm/hwcap.h
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */
#ifndef _ASM_RISCV_HWCAP_H
#define _ASM_RISCV_HWCAP_H

#include <asm/errno.h>
#include <linux/bits.h>
#include <uapi/asm/hwcap.h>

#ifndef __ASSEMBLY__
#include <linux/jump_label.h>

enum {
	CAP_HWCAP = 1,
};

extern unsigned long elf_hwcap;

#define RISCV_ISA_EXT_a		('a' - 'a')
#define RISCV_ISA_EXT_c		('c' - 'a')
#define RISCV_ISA_EXT_d		('d' - 'a')
#define RISCV_ISA_EXT_f		('f' - 'a')
#define RISCV_ISA_EXT_h		('h' - 'a')
#define RISCV_ISA_EXT_i		('i' - 'a')
#define RISCV_ISA_EXT_m		('m' - 'a')
#define RISCV_ISA_EXT_s		('s' - 'a')
#define RISCV_ISA_EXT_u		('u' - 'a')
#define RISCV_ISA_EXT_v		('v' - 'a')

/*
 * These macros represent the logical IDs of each multi-letter RISC-V ISA
 * extension and are used in the ISA bitmap. The logical IDs start from
 * RISCV_ISA_EXT_BASE, which allows the 0-25 range to be reserved for single
 * letter extensions. The maximum, RISCV_ISA_EXT_MAX, is defined in order
 * to allocate the bitmap and may be increased when necessary.
 *
 * New extensions should just be added to the bottom, rather than added
 * alphabetically, in order to avoid unnecessary shuffling.
 */
#define RISCV_ISA_EXT_BASE		26

#define RISCV_ISA_EXT_SSCOFPMF		26
#define RISCV_ISA_EXT_SSTC		27
#define RISCV_ISA_EXT_SVINVAL		28
#define RISCV_ISA_EXT_SVPBMT		29
#define RISCV_ISA_EXT_ZBB		30
#define RISCV_ISA_EXT_ZICBOM		31
#define RISCV_ISA_EXT_ZIHINTPAUSE	32
#define RISCV_ISA_EXT_SVNAPOT		33
#define ANDES_ISA_EXT_DSP		63

#define RISCV_ISA_EXT_MAX		64
#define RISCV_ISA_EXT_NAME_LEN_MAX	32

/*
 * This enum represents the logical ID for each RISC-V ISA extension static
 * keys. We can use static key to optimize code path if some ISA extensions
 * are available.
 * Entries are sorted alphabetically.
 */
enum riscv_isa_ext_key {
	RISCV_ISA_EXT_KEY_FPU,		/* For 'F' and 'D' */
	RISCV_ISA_EXT_KEY_VECTOR,
	RISCV_ISA_EXT_KEY_ZIHINTPAUSE,
	RISCV_ISA_EXT_KEY_SVINVAL,
	RISCV_ISA_EXT_KEY_SVNAPOT,
	ANDES_ISA_EXT_KEY_DSP,
	RISCV_ISA_EXT_KEY_MAX,
};

unsigned long riscv_get_elf_hwcap(void);

struct riscv_isa_ext_data {
	/* Name of the extension displayed to userspace via /proc/cpuinfo */
	char uprop[RISCV_ISA_EXT_NAME_LEN_MAX];
	/* The logical ISA extension ID */
	unsigned int isa_ext_id;
};

extern struct static_key_false riscv_isa_ext_keys[RISCV_ISA_EXT_KEY_MAX];

static __always_inline int riscv_isa_ext2key(int num)
{
	switch (num) {
	case RISCV_ISA_EXT_f:
		return RISCV_ISA_EXT_KEY_FPU;
	case RISCV_ISA_EXT_d:
		return RISCV_ISA_EXT_KEY_FPU;
	case RISCV_ISA_EXT_v:
		return RISCV_ISA_EXT_KEY_VECTOR;
	case RISCV_ISA_EXT_ZIHINTPAUSE:
		return RISCV_ISA_EXT_KEY_ZIHINTPAUSE;
	case RISCV_ISA_EXT_SVINVAL:
		return RISCV_ISA_EXT_KEY_SVINVAL;
	case RISCV_ISA_EXT_SVNAPOT:
		return RISCV_ISA_EXT_KEY_SVNAPOT;
	case ANDES_ISA_EXT_DSP:
		return ANDES_ISA_EXT_KEY_DSP;
	default:
		return -EINVAL;
	}
}

unsigned long riscv_isa_extension_base(const unsigned long *isa_bitmap);

#define riscv_isa_extension_mask(ext) BIT_MASK(RISCV_ISA_EXT_##ext)

bool __riscv_isa_extension_available(const unsigned long *isa_bitmap, int bit);
#define riscv_isa_extension_available(isa_bitmap, ext)	\
	__riscv_isa_extension_available(isa_bitmap, RISCV_ISA_EXT_##ext)

#endif

#endif /* _ASM_RISCV_HWCAP_H */
