/*
 * Copyright (C) 2017 SiFive
 *   Wesley Terpstra <wesley@sifive.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 */

#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>
#include <linux/dma-noncoherent.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <asm/andesv5/proc.h>
#include <asm/sbi.h>

static void dma_flush_page(struct page *page, size_t size)
{
	unsigned long k_d_vaddr;
	struct range_info ri;
	int ret = -1;

	/*
	 * Invalidate any data that might be lurking in the
	 * kernel direct-mapped region for device DMA.
	 */
	k_d_vaddr = (unsigned long)page_address(page);
	memset((void *)k_d_vaddr, 0, size);
	ri.start = k_d_vaddr;
	ri.end = k_d_vaddr + size;
	ri.page = page;
#ifdef CONFIG_IPI_CACHE_OP
	ret = sbi_remote_cache_flush_range(NULL, page_to_phys(page), size);
#endif
	if (ret)
		cpu_dma_wb_range((void *)&ri);
	ret = -1;
#ifdef CONFIG_IPI_CACHE_OP
	ret = sbi_remote_cache_inval_range(NULL, page_to_phys(page), size);
#endif
	if (ret)
		cpu_dma_inval_range((void *)&ri);
}


static inline void cache_op(phys_addr_t paddr, size_t size,
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

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
			      enum dma_data_direction dir)
{
	int ret = -1;

	switch (dir) {
	case DMA_FROM_DEVICE:
    // Bug 18339
#ifdef CONFIG_IPI_CACHE_OP
		ret = sbi_remote_cache_inval_range(NULL, paddr, size);
#endif
		if (ret)
			cache_op(paddr, size, cpu_dma_inval_range);
		break;
	case DMA_TO_DEVICE:
	case DMA_BIDIRECTIONAL:
#ifdef CONFIG_IPI_CACHE_OP
		ret = sbi_remote_cache_flush_range(NULL, paddr, size);
#endif
		if (ret)
			cache_op(paddr, size, cpu_dma_wb_range);
		break;
	default:
		BUG();
	}
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
			   enum dma_data_direction dir)
{
	int ret = -1;

	switch (dir) {
	case DMA_TO_DEVICE:
		break;
	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:
    // for pre-fetch
#ifdef CONFIG_IPI_CACHE_OP
		ret = sbi_remote_cache_inval_range(NULL, paddr, size);
#endif
		if (ret)
			cache_op(paddr, size, cpu_dma_inval_range);
		break;
	default:
		BUG();
	}
}

void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
               gfp_t gfp, unsigned long attrs)
{
       void* kvaddr, *coherent_kvaddr;
       size = PAGE_ALIGN(size);

       kvaddr = dma_direct_alloc_pages(dev, size, handle, gfp, attrs);
       if (!kvaddr)
               goto no_mem;
       coherent_kvaddr = ioremap_nocache(dma_to_phys(dev, *handle), size);
       if (!coherent_kvaddr)
               goto no_map;

       dma_flush_page(virt_to_page(kvaddr),size);
       return coherent_kvaddr;
no_map:
       dma_direct_free_pages(dev, size, kvaddr, *handle, attrs);
no_mem:
       return NULL;
}

void arch_dma_free(struct device *dev, size_t size, void *vaddr,
               dma_addr_t handle, unsigned long attrs)
{
       void *kvaddr = phys_to_virt(dma_to_phys(dev, handle));

       size = PAGE_ALIGN(size);
       iounmap(vaddr);
       dma_direct_free_pages(dev, size, kvaddr, handle, attrs);

       return;
}
