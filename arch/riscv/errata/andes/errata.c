// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/bug.h>
#include <asm/patch.h>
#include <asm/vendorid_list.h>
#include <asm/errata_list.h>

static u32 __init_or_module andes_errata_probe(unsigned long archid,
					       unsigned long impid)
{
	u32 cpu_req_errata = 0;

	return cpu_req_errata;
}

void __init_or_module andes_errata_patch_func(struct alt_entry *begin,
					      struct alt_entry *end,
					      unsigned long archid,
					      unsigned long impid,
					      unsigned int stage)
{
	struct alt_entry *alt;
	u32 cpu_req_errata = andes_errata_probe(archid, impid);
	u32 tmp;

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
		return;

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
