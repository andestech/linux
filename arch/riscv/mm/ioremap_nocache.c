// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) Copyright 1995 1996 Linus Torvalds
 * (C) Copyright 2012 Regents of the University of California
 */
#include <linux/io.h>

#include <asm/pgtable.h>
void __iomem *ioremap_nocache(phys_addr_t offset, size_t size)
{
	void __iomem *ret;
	pgprot_t pgprot = pgprot_noncached(PAGE_KERNEL);
	ret = ioremap_prot(offset, size, pgprot.pgprot);
	return ret;
}
EXPORT_SYMBOL(ioremap_nocache);
