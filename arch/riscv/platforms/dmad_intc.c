/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Andes Technology Corporation
 *
 */

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/dmad.h>

#ifdef CONFIG_PLATFORM_AHBDMA
extern int dmad_probe_irq_source_ahb(void);

/*
 * Generic dummy implementation which can be used for
 * real dumb interrupt sources
 */
struct irq_chip atcdmac_irq_chip = {
	.name		= "Andes DMAC",
};

struct ftdmac020_info {
	int			parent_irq;
	struct irq_domain	*irq_domain;
};

struct ftdmac020_info *ftdmac020;

static int ftdmac020_irq_map(struct irq_domain *domain, unsigned int virq,
			       irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(virq, &atcdmac_irq_chip, handle_simple_irq);
	irq_set_chip_data(virq, domain->host_data);

	return 0;
}

static void ftdmac020_irq_unmap(struct irq_domain *d, unsigned int virq)
{
	irq_set_chip_and_handler(virq, NULL, NULL);
	irq_set_chip_data(virq, NULL);
}

static const struct irq_domain_ops ftdmac020_irq_ops = {
	.map    = ftdmac020_irq_map,
	.unmap  = ftdmac020_irq_unmap,
};


/*
 * The atcdmac300 provides a single hardware interrupt for all of the dmad
 * channel, so we use a self-defined interrupt chip to translate this single interrupt
 * into multiple interrupts, each associated with a single dma channel.
 */
static void AHBDMA_irq_rounter(struct irq_desc *desc)
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

int ftdmac020_find_irq(int hwirq){
	int virq;

	virq = irq_find_mapping(ftdmac020->irq_domain, hwirq);
	printk("[ftdmac020_irq_mapping]: virq=%d, hwirq=%d,\n",virq,hwirq);
	if (!virq)
		return -EINVAL;

	return virq;
}

int ftdmac020_init(struct device_node *node, int irq)
{
	int ret=0;

	ftdmac020 = kzalloc(sizeof(struct ftdmac020_info), GFP_KERNEL);

	ftdmac020->parent_irq=irq;

	ftdmac020->irq_domain = __irq_domain_add(of_node_to_fwnode(node), DMA_IRQ_COUNT, DMA_IRQ0+DMA_IRQ_COUNT,
					 ~0, &ftdmac020_irq_ops, ftdmac020);
	if (!ftdmac020->irq_domain) {
		printk("ftdmac020: Failed to create irqdomain\n");
		return -EINVAL;
	}

	ret = irq_create_strict_mappings(ftdmac020->irq_domain, DMA_IRQ0, DMA_IRQ0, DMA_IRQ_COUNT);
	if(unlikely(ret < 0)){
		printk("ftdmac020: Failed to create irq_create_strict_mappings()\n");
		return -EINVAL;
	}

	ftdmac020->irq_domain->name = "ftdmac020-domain";
	irq_set_chained_handler_and_data(ftdmac020->parent_irq,
					 AHBDMA_irq_rounter, ftdmac020);

	return 0;
}

#endif /* CONFIG_PLATFORM_AHBDMA */
