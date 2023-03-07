// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */
#include <linux/module.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/pgtable.h>
#include <soc/andes/ppma.h>

void __iomem *ioremap_wc(phys_addr_t phys_addr, size_t size)
{
	void __iomem *ret;
	pgprot_t prot;

	prot = pgprot_writecombine(PAGE_KERNEL);

	ret = ioremap_prot(phys_addr, size, pgprot_val(prot));

	if (ret)
		andes_set_ppma(phys_addr, ret, size);

	return ret;
}
EXPORT_SYMBOL(ioremap_wc);

bool iounmap_allowed(void *addr)
{
	andes_free_ppma(addr);
	return true;
}
EXPORT_SYMBOL(iounmap_allowed);
