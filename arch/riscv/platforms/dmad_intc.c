/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Andes Technology Corporation
 *
 */

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/dmad.h>

#ifdef CONFIG_PLATFORM_AHBDMA
extern int dmad_probe_irq_source_ahb(void);
void AHBDMA_irq_rounter(struct irq_desc *desc)
{
	int ahb_irq;
	struct irq_desc *ahb_desc;

	raw_spin_lock(&desc->lock);
	ahb_irq = dmad_probe_irq_source_ahb();

	if (ahb_irq >= 0) {
		ahb_irq += DMA_IRQ0;
		ahb_desc = irq_to_desc(ahb_irq);
		ahb_desc->irq_data.irq = ahb_irq;
		raw_spin_unlock(&desc->lock);
		ahb_desc->handle_irq(ahb_desc);
		raw_spin_lock(&desc->lock);
	}
	desc->irq_data.chip->irq_unmask(&desc->irq_data);
	desc->irq_data.chip->irq_eoi(&desc->irq_data);
	raw_spin_unlock(&desc->lock);
}

int intc_ftdmac020_init_irq(int irq)
{
	int i;
	int ret;
	/* Register all IRQ */
	for (i = DMA_IRQ0;
	     i < DMA_IRQ0 + DMA_IRQ_COUNT; i++) {
		// level trigger
		ret = irq_set_chip(i, &dummy_irq_chip);
		irq_set_handler(i, handle_simple_irq);
	}
	irq_set_chained_handler(irq, AHBDMA_irq_rounter);
	return 0;
}
#endif /* CONFIG_PLATFORM_AHBDMA */
