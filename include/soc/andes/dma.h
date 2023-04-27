/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#ifndef __SOC_ANDES_DMA_H
#define __SOC_ANDES_DMA_H

#include <linux/types.h>
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/highmem.h>
#include <linux/cacheflush.h>
#include <asm/sbi.h>

/*
 * struct andesv5_cache_info
 * The member of this struct is dupilcated to some content of struct cacheinfo
 * to reduce the latence of searching dcache inforamtion in andesv5/cache.c.
 * At current only dcache-line-size is needed. when the content of
 * andesv5_cache_info has been initilized by function fill_cpu_cache_info(),
 * member init_done is set as true
 */
struct andesv5_cache_info {
	bool init_done;
	int dcache_line_size;
};

struct range_info {
	unsigned long start;
	unsigned long end;
	struct page *page;
};

void cpu_dma_inval_range(void *info);
void cpu_dma_wb_range(void *info);

static inline void dma_flush_page(struct page *page, size_t size)
{
	unsigned long k_d_vaddr;
	struct range_info ri;

	/*
	 * Invalidate any data that might be lurking in the
	 * kernel direct-mapped region for device DMA.
	 */
	k_d_vaddr = (unsigned long)page_address(page);
	memset((void *)k_d_vaddr, 0, size);
	ri.start = k_d_vaddr;
	ri.end = k_d_vaddr + size;
	ri.page = page;
	cpu_dma_wb_range((void *)&ri);
	cpu_dma_inval_range((void *)&ri);
}

static inline void andes_cache_op(phys_addr_t paddr, size_t size,
		void (*fn)(void *ri))
{
	struct page *page = pfn_to_page(paddr >> PAGE_SHIFT);
	unsigned offset = paddr & ~PAGE_MASK;
	size_t left = size;
	unsigned long start;
	struct range_info ri;

	do {
		size_t len = left;

		if (PageHighMem(page)) {
			void *addr;

			if (offset + len > PAGE_SIZE) {
				if (offset >= PAGE_SIZE) {
					page += offset >> PAGE_SHIFT;
					offset &= ~PAGE_MASK;
				}
				len = PAGE_SIZE - offset;
			}

			addr = kmap_atomic(page);
			start = (unsigned long)(addr + offset);
			ri.start = start;
			ri.end = start + len;
			ri.page = page;
			fn((struct range_info *)&ri);
			kunmap_atomic(addr);
		} else {
			start = (unsigned long)phys_to_virt(paddr);
			ri.start = start;
			ri.end = start + size;
			ri.page = page;
			fn((struct range_info *)&ri);
		}
		offset = 0;
		page++;
		left -= len;
	} while (left);
}

extern void *arch_dma_set_uncached(void *addr, size_t size);
extern void arch_dma_clear_uncached(void *addr, size_t size);

#endif /* !__SOC_ANDES_DMA_H */
