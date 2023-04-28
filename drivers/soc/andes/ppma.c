// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#include <linux/export.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <asm/page.h>
#include <soc/andes/sbi.h>

struct ppma_arg_t ppma_arg;

long andes_probe_ppma(void)
{
	return sbi_andes_probe_ppma();
}
EXPORT_SYMBOL(andes_probe_ppma);

void andes_set_ppma(phys_addr_t phys_addr, void *va_addr, size_t size)
{
	int cpu_num = num_online_cpus();
	int id = smp_processor_id();
	int i, err;
	bool within_memory = pfn_valid(phys_addr >> PAGE_SHIFT);

	/*
	 * PPMA/Non-cacheability setting only
	 * when the given PA is within main memory range,
	 * and the given size is power of 2 (NAPOT).
	 */
	if (!within_memory)
		return;

	if ((size & (size - 1)) != 0) {
		printk("The value of size is not power of 2\n");
		BUG();
	}

	/*
	 * FIXME: this is not protected.
	 */
	ppma_arg.phys_addr = phys_addr;
	ppma_arg.va_addr = (unsigned long)va_addr;
	ppma_arg.size = size;

	/*
	 * Send IPI
	 * FIXME: we need online CPU mask, not the number
	 */
	for (i = 0; i < cpu_num; i++) {
		if (i == id)
			continue;

		err = smp_call_function_single(i, sbi_andes_set_ppma,
						(void *)&ppma_arg, true);

		if (err) {
			pr_err("Core %d fails to set ppma\nError Code: %d\n",
				i, err);
		}
	}

	sbi_andes_set_ppma(&ppma_arg);
}
EXPORT_SYMBOL(andes_set_ppma);

void andes_free_ppma(void *addr)
{
	int cpu_num = num_online_cpus();
	int id = smp_processor_id();
	int i, err;

	/*
	 * Send IPI
	 * FIXME: we need online CPU mask, not the number
	 */
	for (i = 0; i < cpu_num; i++) {
		if (i == id)
			continue;

		err = smp_call_function_single(i, sbi_andes_free_ppma,
						(void *)addr, true);
		if (err) {
			pr_err("Core %d fails to free ppma\n"
				"Error Code: %d\n", i, err);
		}
	}

	sbi_andes_free_ppma(addr);

}
EXPORT_SYMBOL(andes_free_ppma);
