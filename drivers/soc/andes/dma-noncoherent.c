// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Andes Technology Corporation
 */

#include <soc/andes/dma.h>
#include <linux/io.h>

void *arch_dma_set_uncached(void *addr, size_t size)
{
	phys_addr_t phys;

	phys = page_to_phys(virt_to_page(addr));
	addr = ioremap_wc(phys, size);
	return addr;
}
EXPORT_SYMBOL(arch_dma_set_uncached);

void arch_dma_clear_uncached(void *addr, size_t size)
{
	iounmap(addr);
}
EXPORT_SYMBOL(arch_dma_clear_uncached);
