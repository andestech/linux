// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/bug.h>
#include <asm/patch.h>
#include <asm/alternative.h>
#include <asm/vendorid_list.h>
#include <asm/errata_list.h>
#include <soc/andes/andes.h>

bool andes_legacy_mmu;
EXPORT_SYMBOL(andes_legacy_mmu);

phys_addr_t andes_pa_msb;
EXPORT_SYMBOL(andes_pa_msb);

struct errata_info_t {
	char name[ERRATA_STRING_LENGTH_MAX];
	bool (*check_func)(unsigned long arch_id,
			   unsigned long impid,
			   unsigned int stage);
};

static bool errata_msb_check_func(unsigned long arch_id,
				  unsigned long impid,
				  unsigned int stage)
{
	if (stage != RISCV_ALTERNATIVES_EARLY_BOOT)
		return false;

	andes_pa_msb = 0;

	if (riscv_isa_extension_available(NULL, SVPBMT))
		return false;

	csr_write(satp, SATP_PPN);
	andes_pa_msb = (csr_read(satp) + 1) >> 1;
	return true;
}

static bool errata_legacy_mmu_check_func(unsigned long arch_id,
					 unsigned long impid,
					 unsigned int stage)
{
	/* legacy MMU only exists in 2x-series CPU. */
	andes_legacy_mmu = (((arch_id & 0xF0) >> 4) == 0x2) ? true : false;
	return andes_legacy_mmu;
}

static struct errata_info_t errata_list[ERRATA_ANDES_NUMBER] = {
	{
		.name = "legacy_mmu",
		.check_func = errata_legacy_mmu_check_func
	},
	{
		.name = "pa_msb",
		.check_func = errata_msb_check_func
	},
};

static u32 __init_or_module andes_errata_probe(unsigned long archid,
					       unsigned long impid,
					       unsigned int stage)
{
	u32 cpu_req_errata = 0;
	int idx;

	for (idx = 0; idx < ERRATA_ANDES_NUMBER; idx++)
		if (errata_list[idx].check_func(archid, impid, stage))
			cpu_req_errata |= (1U << idx);

	return cpu_req_errata;
}

void __init_or_module andes_errata_patch_func(struct alt_entry *begin,
					      struct alt_entry *end,
					      unsigned long archid,
					      unsigned long impid,
					      unsigned int stage)
{
	struct alt_entry *alt;
	u32 cpu_req_errata;
	u32 tmp = 0;

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT) {
		cpu_req_errata = errata_msb_check_func(archid, impid, stage);
		return;
	}

	cpu_req_errata = andes_errata_probe(archid, impid, stage);

	for (alt = begin; alt < end; alt++) {
		if (alt->vendor_id != ANDES_VENDOR_ID)
			continue;
		if (alt->errata_id >= ERRATA_ANDES_NUMBER)
			continue;

		tmp = (1U << alt->errata_id);
		if (cpu_req_errata & tmp)
			patch_text_nosync(alt->old_ptr, alt->alt_ptr, alt->alt_len);
	}
}
