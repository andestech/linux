// SPDX-License-Identifier: GPL-2.0-only
/*
 * Erratas to be applied for Andes CPU cores
 *
 *  Copyright (C) 2023 Renesas Electronics Corporation.
 *
 * Author: Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>
 */

#include <linux/memory.h>
#include <linux/module.h>

#include <asm/alternative.h>
#include <asm/cacheflush.h>
#include <asm/errata_list.h>
#include <asm/patch.h>
#include <asm/processor.h>
#include <asm/vendorid_list.h>
#include <asm/vendor_extensions.h>
#include <linux/soc/andes/sbi.h>
#include <linux/soc/andes/andes.h>
#include <linux/soc/andes/ppma.h>

#define ANDES_AX45MP_MARCHID		0x8000000000008a45UL
#define ANDES_AX45MP_MIMPID		0x500UL

DEFINE_STATIC_KEY_FALSE(andes_legacy_mmu_key);

phys_addr_t andes_pfn_msb;
EXPORT_SYMBOL(andes_pfn_msb);

bool andes_legacy_mmu;
EXPORT_SYMBOL(andes_legacy_mmu);

struct errata_info_t {
	char name[32];
	bool (*check_func)(unsigned int stage,
			   unsigned long arch_id,
			   unsigned long impid);
};

static long ax45mp_iocp_sw_workaround(void)
{
	struct sbiret ret;

	/*
	 * SBI_EXT_ANDES_IOCP_SW_WORKAROUND SBI EXT checks if the IOCP is missing and
	 * cache is controllable only then CMO will be applied to the platform.
	 */
	ret = sbi_ecall(ANDES_SBI_EXT_ANDES, SBI_EXT_ANDES_IOCP_SW_WORKAROUND,
			0, 0, 0, 0, 0, 0);

	return ret.error ? 0 : ret.value;
}

static bool errata_probe_iocp(unsigned int stage,
			      unsigned long arch_id,
			      unsigned long impid)
{
	static bool done;

	if (!IS_ENABLED(CONFIG_ERRATA_ANDES_CMO))
		return 0;

	if (done)
		return done;

	done = true;

	if (arch_id != ANDES_AX45MP_MARCHID || impid != ANDES_AX45MP_MIMPID)
		return 0;

	if (!ax45mp_iocp_sw_workaround())
		return 0;

	/* Set this just to make core cbo code happy */
	riscv_cbom_block_size = 1;
	riscv_noncoherent_supported();
	return done;
}

static bool errata_legacy_mmu_check_func(unsigned int stage,
					 unsigned long arch_id,
					 unsigned long impid)
{
	/* legacy MMU only exists in 2X-series CPU.*/
	andes_legacy_mmu = (((arch_id & 0xF0) >> 4) == 0x2) ? true : false;
	if (andes_legacy_mmu && ((arch_id & 0xF) == 0x5))
		static_branch_enable(&andes_legacy_mmu_key);
	return andes_legacy_mmu;
}

static bool errata_support_uncache(unsigned int stage,
				   unsigned long arch_id,
				   unsigned long impid)
{
	/*
	 * Check RISCV_ALTERNATIVES_EARLY_BOOT stage ensures
	 * andes_pfn_msb is modified only once during kernel bootup.
	 */
	if (stage != RISCV_ALTERNATIVES_EARLY_BOOT)
		return false;

	andes_pfn_msb = 0;

	if (!IS_ENABLED(CONFIG_ERRATA_ANDES_CMO))
		return 0;

	if (riscv_isa_extension_available(NULL, SVPBMT))
		return true;

	/* Set this just to make core cbo code happy */
	riscv_cbom_block_size = 1;
	riscv_noncoherent_supported();

	if (andes_probe_ppma())
		return true;

	csr_write(satp, SATP_PPN);
	andes_pfn_msb = (csr_read(satp) + 1) >> 1;

	return true;
}

static struct errata_info_t errata_list[ERRATA_ANDES_NUMBER] = {
	{	.name = "probe_iocp",
		.check_func = errata_probe_iocp
	},
	{
		.name = "legacy_mmu",
		.check_func = errata_legacy_mmu_check_func
	},
	{
		.name = "support_uncache",
		.check_func = errata_support_uncache
	},
};

static u32 __init_or_module andes_errata_probe(unsigned int stage,
					       unsigned long archid,
					       unsigned long impid)
{
	u32 cpu_req_errata = 0;
	int idx;

	for (idx = 1; idx < ERRATA_ANDES_NUMBER; idx++)
		if (errata_list[idx].check_func(stage, archid, impid))
			cpu_req_errata |= (1U << idx);

	return cpu_req_errata;
}

void __init_or_module andes_errata_patch_func(struct alt_entry *begin, struct alt_entry *end,
					      unsigned long archid, unsigned long impid,
					      unsigned int stage)
{
	BUILD_BUG_ON(ERRATA_ANDES_NUMBER >= RISCV_VENDOR_EXT_ALTERNATIVES_BASE);

	struct alt_entry *alt;
	u32 cpu_req_errata;
	u32 tmp = 0;

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT) {
		if (IS_ENABLED(CONFIG_ARCH_R9A07G043))
			errata_probe_iocp(stage, archid, impid);
		else
			errata_support_uncache(stage, archid, impid);
		return;
	}

	cpu_req_errata = andes_errata_probe(stage, archid, impid);

	for (alt = begin; alt < end; alt++) {
		if (alt->vendor_id != ANDES_VENDOR_ID)
			continue;
		if (alt->patch_id >= ERRATA_ANDES_NUMBER)
			continue;

		tmp = (1U << alt->patch_id);
		if (cpu_req_errata & tmp) {
			mutex_lock(&text_mutex);
			patch_text_nosync(ALT_OLD_PTR(alt), ALT_ALT_PTR(alt),
					  alt->alt_len);
			mutex_unlock(&text_mutex);
		}
	}
}
