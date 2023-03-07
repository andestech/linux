/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */
#ifndef __SOC_ANDES_SBI_H
#define __SOC_ANDES_SBI_H

#include <asm/sbi.h>
#include <asm/vendorid_list.h>
#include <soc/andes/ppma.h>

#define SBI_EXT_ANDES (SBI_EXT_VENDOR_START | ANDES_VENDOR_ID)

enum sbi_ext_andes_fid {

	/* CCTL */
	SBI_EXT_ANDES_GET_MCACHE_CTL_STATUS = 0,
	SBI_EXT_ANDES_GET_MMISC_CTL_STATUS,
	SBI_EXT_ANDES_SET_MCACHE_CTL,
	SBI_EXT_ANDES_SET_MMISC_CTL,
	SBI_EXT_ANDES_ICACHE_OP,
	SBI_EXT_ANDES_DCACHE_OP,
	SBI_EXT_ANDES_L1CACHE_I_PREFETCH,
	SBI_EXT_ANDES_L1CACHE_D_PREFETCH,
	SBI_EXT_ANDES_NON_BLOCKING_LOAD_STORE,
	SBI_EXT_ANDES_WRITE_AROUND,

	/* Trace */
	SBI_EXT_ANDES_TRIGGER,
	SBI_EXT_ANDES_SET_PFM,

	/* CPU freq */
	SBI_EXT_ANDES_READ_POWERBRAKE,
	SBI_EXT_ANDES_WRITE_POWERBRAKE,

	/* CPU idle (ATCSMU) */
	SBI_EXT_ANDES_SUSPEND_PREPARE,
	SBI_EXT_ANDES_SUSPEND_MEM,
	SBI_EXT_ANDES_SET_SUSPEND_MODE,
	SBI_EXT_ANDES_ENTER_SUSPEND_MODE,

	/* Reset (ATCSMU) */
	SBI_EXT_ANDES_RESTART,
	SBI_EXT_ANDES_SET_RESET_VEC,

	/* Programmable physical memory attributes (PPMA) */
	SBI_EXT_ANDES_SET_PPMA,
	SBI_EXT_ANDES_FREE_PPMA,
	SBI_EXT_ANDES_PROBE_PPMA,

	SBI_EXT_ANDES_DCACHE_WBINVAL_ALL,
};

/* Programmable physical memory attributes (PPMA) */
void sbi_andes_set_ppma(void *arg);
void sbi_andes_free_ppma(unsigned long addr);
long sbi_andes_probe_ppma(void);

#endif /* !__SOC_ANDES_SBI_H */
