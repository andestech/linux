// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Andes Technology Corporation.
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/uaccess.h>

#define GPIO_DATA_OUT			0x24
#define GPIO_DATA_IN			0x20
#define PIN_DIR				0x28
#define PIN_PULL_ENABLE			0x40
#define PIN_PULL_TYPE			0x44
#define INT_ENABLE			0x50
#define INT_STATE			0x64
#define INT_MODE(ch)			(0x54 + ((ch / 8) << 2))
#define HIGH_LEVEL			2
#define LOW_LEVEL			3
#define NEGATIVE_EDGE			5
#define POSITIVE_EDGE			6
#define DUAL_EDGE			7
#define DEBOUNCE_ENABLE			0x70

#define ATCGPIO100_VIRTUAL_IRQ_BASE	0
#define DEFAULT_PIN_NUMBER		16

struct atcgpio_priv {
	struct gpio_chip gc;
	struct irq_domain *domain;
	void __iomem *base;
	unsigned long virq_base;
	spinlock_t lock;
	unsigned int irq_parent;
};

#define GPIO_READL(offset, base)	\
	readl((void __iomem *)base + (offset))

#define GPIO_WRITEL(val, offset, base)	\
	writel((val), (void __iomem *)base + (offset))

static inline struct atcgpio_priv *atcgpio_irq_data_get_data(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);

	return gpiochip_get_data(chip);
}

static int atcirq_to_gpio(unsigned long irq)
{
	return irq - (ATCGPIO100_VIRTUAL_IRQ_BASE ? gpio_to_irq(0) : 0);
}

static int atcgpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct atcgpio_priv *priv;
	unsigned long flags;
	u32 val;

	priv = gpiochip_get_data(gc);
	spin_lock_irqsave(&priv->lock, flags);
	val = GPIO_READL(GPIO_DATA_IN, priv->base);
	spin_unlock_irqrestore(&priv->lock, flags);

	return (val >> gpio & 1);
}

static void atcgpio_set(struct gpio_chip *gc, unsigned int gpio, int data)
{
	unsigned long flags;
	struct atcgpio_priv *priv;
	unsigned long val;

	priv = gpiochip_get_data(gc);
	spin_lock_irqsave(&priv->lock, flags);
	if (data)
		val = GPIO_READL(GPIO_DATA_OUT, priv->base) | (0x1UL << gpio);
	else
		val = GPIO_READL(GPIO_DATA_OUT, priv->base) & ~(0x1UL << gpio);
	GPIO_WRITEL(val, GPIO_DATA_OUT, priv->base);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int atcgpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	unsigned long val;
	unsigned long flags;
	struct atcgpio_priv *priv;

	priv = gpiochip_get_data(gc);
	spin_lock_irqsave(&priv->lock, flags);
	val = GPIO_READL(PIN_DIR, priv->base) & ~(0x1UL << gpio);
	GPIO_WRITEL(val, PIN_DIR, priv->base);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int atcgpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int data)
{
	unsigned long val;
	unsigned long flags;
	struct atcgpio_priv *priv;

	priv = gpiochip_get_data(gc);
	spin_lock_irqsave(&priv->lock, flags);
	val = GPIO_READL(PIN_DIR, priv->base) | (0x1UL << gpio);
	GPIO_WRITEL(val, PIN_DIR, priv->base);
	gc->set(gc, gpio, data);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static void atcgpio_irq_ack(struct irq_data *data)
{
	unsigned long flags;
	struct atcgpio_priv *priv;
	unsigned long irq;

	priv = atcgpio_irq_data_get_data(data);
	irq = priv->virq_base ? data->irq : data->hwirq;
	spin_lock_irqsave(&priv->lock, flags);
	GPIO_WRITEL(0x1UL << atcirq_to_gpio(irq), INT_STATE, priv->base);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void atcgpio_irq_unmask(struct irq_data *data)
{
	unsigned long val;
	unsigned long flags;
	struct atcgpio_priv *priv;
	unsigned long irq;

	priv = atcgpio_irq_data_get_data(data);
	irq = priv->virq_base ? data->irq : data->hwirq;
	spin_lock_irqsave(&priv->lock, flags);
	val = GPIO_READL(INT_ENABLE, priv->base) | (0x1UL << atcirq_to_gpio(irq));
	GPIO_WRITEL(val, INT_ENABLE, priv->base);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void atcgpio_irq_mask(struct irq_data *data)
{
	unsigned long val;
	unsigned long flags;
	struct atcgpio_priv *priv;
	unsigned long irq;

	priv = atcgpio_irq_data_get_data(data);
	irq = priv->virq_base ? data->irq : data->hwirq;
	spin_lock_irqsave(&priv->lock, flags);
	val = GPIO_READL(INT_ENABLE, priv->base) & ~(0x1UL << atcirq_to_gpio(irq));
	GPIO_WRITEL(val, INT_ENABLE, priv->base);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int atcgpio_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct atcgpio_priv *priv  = atcgpio_irq_data_get_data(data);
	unsigned int irq = priv->virq_base ? data->irq : data->hwirq;
	unsigned int gpio = atcirq_to_gpio(irq);
	unsigned int ch = (gpio % 8);
	unsigned int mode_off = (ch << 2);
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	val = GPIO_READL(INT_MODE(gpio), priv->base);
	val &= ~(7 << mode_off);
	if (flow_type & IRQF_TRIGGER_RISING && flow_type & IRQF_TRIGGER_FALLING)
		GPIO_WRITEL(val | DUAL_EDGE<<mode_off, INT_MODE(gpio), priv->base);
	else if (flow_type & IRQF_TRIGGER_FALLING)
		GPIO_WRITEL(val | NEGATIVE_EDGE<<mode_off, INT_MODE(gpio), priv->base);
	else if (flow_type & IRQF_TRIGGER_RISING)
		GPIO_WRITEL(val | POSITIVE_EDGE<<mode_off, INT_MODE(gpio), priv->base);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static void gpio_irq_router(struct irq_desc *desc)
{
	unsigned long status;
	struct atcgpio_priv *priv;
	int i = 0;

	priv = irq_desc_get_handler_data(desc);
	status = GPIO_READL(INT_STATE, priv->base);
	status &= ~((1 << 22) | (1 << 25) | (1 << 26));
	while (status) {
		if (status & 0x1UL)
			generic_handle_irq(gpio_to_irq(i));
		status >>= 1;
		i++;
	}
	desc->irq_data.chip->irq_eoi(&desc->irq_data);
}

static const struct irq_chip atcgpio_irqchip = {
	.name = "ATCGPIO100_irq",
	.irq_ack = atcgpio_irq_ack,
	.irq_mask = atcgpio_irq_mask,
	.irq_unmask = atcgpio_irq_unmask,
	.irq_set_type = atcgpio_irq_set_type,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int atcgpio100_gpio_probe(struct platform_device *pdev)
{
	int (*read_fixup)(void __iomem *addr, unsigned int val,
			  unsigned int shift_bits);
	struct resource *res;
	int ret;
	struct atcgpio_priv *priv;
	struct gpio_chip *gc;
	struct gpio_irq_chip *girq;
	struct device_node *node;
	u32 ngpios;

	priv = devm_kzalloc(&pdev->dev,	sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to request atcgpio100 resource\n");
		return -ENXIO;
	}
	platform_set_drvdata(pdev, priv);
	ret = platform_irq_count(pdev);
	priv->irq_parent = platform_get_irq(pdev, 0);
	if (priv->irq_parent < 0) {
		dev_err(&pdev->dev, "failed to request atcgpio100 irq\n");
		return -ENXIO;
	}
	priv->virq_base = ATCGPIO100_VIRTUAL_IRQ_BASE;
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	spin_lock_init(&priv->lock);
	if (IS_ERR((void *)priv->base))
		return PTR_ERR((void *)priv->base);

	/* Check ID register */
	read_fixup = symbol_get(readl_fixup);
	ret = read_fixup(priv->base, 0x020310, 8);
	symbol_put(readl_fixup);
	if (!ret) {
		dev_err(&pdev->dev,
			"Failed to read ID register, ATCGPIO100 is not supported.\n");
		return -ENXIO;
	}
	
	node = pdev->dev.of_node;
	if (!node)
		return -EINVAL;
	
	ret = of_property_read_u32(node, "ngpios", &ngpios);
	if (ret)
		ngpios = DEFAULT_PIN_NUMBER;

	/* disable interrupt */
	GPIO_WRITEL(0x00000000UL, INT_ENABLE, priv->base);
	/* clear interrupt */
	GPIO_WRITEL(0x0000FFFFUL, INT_STATE, priv->base);
	/* enable de-bouncing */
	GPIO_WRITEL(0x0000FFFFUL, DEBOUNCE_ENABLE, priv->base);
	/* enable interrupt */
	GPIO_WRITEL(0x0000FFFFUL, INT_ENABLE, priv->base);
	gc = &priv->gc;
	gc->owner = THIS_MODULE;
	gc->parent = &pdev->dev;
	gc->label = "atcgpio100";
	gc->base = 0;
	gc->ngpio = ngpios;
	gc->direction_output = atcgpio_dir_out;
	gc->direction_input = atcgpio_dir_in;
	gc->set = atcgpio_set;
	gc->get = atcgpio_get;
	girq = &priv->gc.irq;
	gpio_irq_chip_set_chip(girq, &atcgpio_irqchip);
	girq->parent_handler = gpio_irq_router;
	girq->num_parents = 1;
	girq->first = priv->virq_base;
	girq->parents = devm_kcalloc(&pdev->dev, girq->num_parents,
				     sizeof(*girq->parents), GFP_KERNEL);
	girq->parents[0] = priv->irq_parent;
	if (!girq->parents)
		return -ENOMEM;

	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_level_irq;
	ret = devm_gpiochip_add_data(&pdev->dev, &priv->gc, priv);
	if (ret) {
		dev_err(&pdev->dev, "gpiochip_add error %d\n", ret);
		return ret;
	}
	pr_info("ATCGPIO100 module inserted\n");

	return 0;
}

static int atcgpio100_gpio_remove(struct platform_device *pdev)
{
	struct atcgpio_priv *priv = platform_get_drvdata(pdev);

	/* disable interrupt */
	GPIO_WRITEL(0x00000000UL, INT_ENABLE, priv->base);
	gpiochip_remove(&priv->gc);
	pr_info("GPIO module removed\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id atcgpio100_gpio_match[] = {
	{
		.compatible = "andestech,atcgpio100",
	},
	{ },
};
#endif

static struct platform_driver atcgpio100_gpio_driver = {
	.probe		= atcgpio100_gpio_probe,
	.driver		= {
		.name	= "atcgpio100_gpio",
		.of_match_table = of_match_ptr(atcgpio100_gpio_match),
	},
	.remove		= atcgpio100_gpio_remove,
};

module_platform_driver(atcgpio100_gpio_driver);
MODULE_DESCRIPTION("ATCGPIO100");
MODULE_LICENSE("GPL");
