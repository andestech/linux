// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Andes Technology Corporation
 */

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <soc/andes/dmad.h>

#ifdef CONFIG_PLATFORM_AHBDMA

static void legacy_dma_irq_router(struct irq_desc *desc)
{
	int ahb_irq;
	struct irq_desc *ahb_desc;

	ahb_irq = dmad_probe_irq_source_ahb();

	if (ahb_irq >= 0) {
		ahb_irq = get_irq(ahb_irq);
		ahb_desc = irq_to_desc(ahb_irq);
		ahb_desc->irq_data.irq = ahb_irq;
		ahb_desc->handle_irq(ahb_desc);
	}
	desc->irq_data.chip->irq_unmask(&desc->irq_data);
	desc->irq_data.chip->irq_eoi(&desc->irq_data);
}

int intc_ftdmac020_init_irq(int irq)
{
	int i;
	int ret;
	/* Register all IRQ */
	for (i = DMA_IRQ0; i < DMA_IRQ0 + DMA_IRQ_COUNT; i++) {
		// level trigger
		ret = irq_set_chip(i, &dummy_irq_chip);
		irq_set_handler(i, handle_simple_irq);
	}
	irq_set_chained_handler(irq, legacy_dma_irq_router);
	return 0;
}
#endif /* CONFIG_PLATFORM_AHBDMA */
