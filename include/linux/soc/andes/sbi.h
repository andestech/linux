/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */
#ifndef __LINUX_SOC_ANDES_SBI_H
#define __LINUX_SOC_ANDES_SBI_H

#include <asm/sbi.h>
#include <asm/vendorid_list.h>
#include <linux/soc/andes/ppma.h>

#define ANDES_SBI_EXT_ANDES	0x0900031E

/*
 * Set Andes's internal SBI Call FID start at 64 to avoid the impact of
 * upstream add or remove in sbi_ext_andes_fid{}. If the FID numbers
 * change, our released OpenSBI will lose backward compatible.
 */
#define ANDES_SBI_INTERNAL_FID_START 64

enum sbi_ext_andes_fid {
	SBI_EXT_ANDES_FID0 = 0, /* Reserved for future use */
	SBI_EXT_ANDES_IOCP_SW_WORKAROUND,

	/* Trace */
	SBI_EXT_ANDES_TRIGGER_SET = ANDES_SBI_INTERNAL_FID_START,

	/* CPU freq */
	SBI_EXT_ANDES_POWERBRAKE_READ,
	SBI_EXT_ANDES_POWERBRAKE_WRITE,

	/* Programmable physical memory attributes (PPMA) */
	SBI_EXT_ANDES_PMA_SET,
	SBI_EXT_ANDES_PMA_FREE,
	SBI_EXT_ANDES_PMA_PROBE,

	SBI_EXT_ANDES_DCACHE_EN,

	/* CPU idle (ATCSMU) */
	SBI_EXT_ANDES_SUSPEND_MODE_SET,
	SBI_EXT_ANDES_SUSPEND_MODE_ENTER,
};

/* Programmable physical memory attributes (PPMA) */
void sbi_andes_set_ppma(void *arg);
void sbi_andes_free_ppma(void *addr);
long sbi_andes_probe_ppma(void);

/* PowerBrake */
void sbi_andes_write_powerbrake(unsigned int val);
long sbi_andes_read_powerbrake(void);

#ifdef CONFIG_ARCH_ANDES
/* Trigger module support debug application with gdbserver */
void sbi_andes_set_trigger(unsigned int type, uintptr_t data, int enable);
#else
static inline void sbi_andes_set_trigger(unsigned int type,
					 uintptr_t data,
					 int enable) {}
#endif /* !CONFIG_ARCH_ANDES */

#endif /* !__LINUX_SOC_ANDES_SBI_H */
