/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Andes Technology Corporation
 *
 */

#ifndef __NDS_DMAD_INC__
#define __NDS_DMAD_INC__

#include <asm/atcdmac300.h>

#ifdef CONFIG_PLATFORM_AHBDMA
int intc_ftdmac020_init_irq(int irq);
#endif

extern resource_size_t		ahb_base;
extern resource_size_t		pmu_base;

#define AMERALD_PRODUCT_ID	0x41471000
#define AMERALD_MASK		0xFFFFF000

/* DMAC */
#define DMAC_FTDMAC020_IRQ_COUNT	8
#define DMAC_FTDMAC020_IRQ0		64
#define DMAC_FTDMAC020_IRQ1		65
#define DMAC_FTDMAC020_IRQ2		66
#define DMAC_FTDMAC020_IRQ3		67
#define DMAC_FTDMAC020_IRQ4		68
#define DMAC_FTDMAC020_IRQ5		69
#define DMAC_FTDMAC020_IRQ6		70
#define DMAC_FTDMAC020_IRQ7		71

/* APBBRG */
#define APBBRG_FTAPBBRG020S_IRQ_COUNT	4
#define APBBRG_FTAPBBRG020S_IRQ0	72
#define APBBRG_FTAPBBRG020S_IRQ1	73
#define APBBRG_FTAPBBRG020S_IRQ2	74
#define APBBRG_FTAPBBRG020S_IRQ3	75


/* Dma irq */
#define DMA_IRQ_COUNT	DMAC_FTDMAC020_IRQ_COUNT
#define DMA_IRQ0	DMAC_FTDMAC020_IRQ0
#define DMA_IRQ1	DMAC_FTDMAC020_IRQ1
#define DMA_IRQ2	DMAC_FTDMAC020_IRQ2
#define DMA_IRQ3	DMAC_FTDMAC020_IRQ3
#define DMA_IRQ4	DMAC_FTDMAC020_IRQ4
#define DMA_IRQ5	DMAC_FTDMAC020_IRQ5
#define DMA_IRQ6	DMAC_FTDMAC020_IRQ6
#define DMA_IRQ7	DMAC_FTDMAC020_IRQ7


struct at_dma_platform_data {
	unsigned int	nr_channels;
	bool		is_private;
#define CHAN_ALLOCATION_ASCENDING	0	/* zero to seven */
#define CHAN_ALLOCATION_DESCENDING	1	/* seven to zero */
	unsigned char	chan_allocation_order;
#define CHAN_PRIORITY_ASCENDING		0	/* chan0 highest */
#define CHAN_PRIORITY_DESCENDING	1	/* chan7 highest */
	unsigned char	chan_priority;
	unsigned short	block_size;
	unsigned char	nr_masters;
	unsigned char	data_width[4];
	struct resource	*io;
	void __iomem	*dmac_regs;
	void __iomem	*pmu_regs;
	void __iomem	*apb_regs;
};

#endif  /* __NDS_DMAD_INC__ */
