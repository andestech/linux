// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation
 */

#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include "timer-of.h"

/* ID and Revision Register */
#define ID_REV           0x0
#define ATCPIT_ID        0x03031
#define ATCPIT_ID_MSK    0xFFFFF000
#define ATCPIT_ID_SFT    12

/* Configuration Register */
#define CFG              0x10
#define NUM_PIT_CH_MSK   0x7

/* Interrupt Enable Register */
#define INT_EN           0x14
#define CH_INT_EN(c, t)  (t << (c * 4))

/* Interrupt Status Register */
#define INT_STA          0x18
#define CH_INT_STA(c, t) (t << (c * 4))

/* Channel Enable Register */
#define CH_EN_REG        0x1C
#define CH_EN_TIME(c, t) (t << (c * 4))

/* Channel Control Register */
#define CH_CTRL(ch)      (0x20 + (ch * 0x10))
#define CH_CTRL_CLK_SFT  3
#define CH_CTRL_MODE_MSK 0x7
#define CH_CTRL_CLK_MSK  0x8
#define TIMER0           1
#define TIMER1           2
#define TIMER2           4
#define TIMER3           8

/* Channel clock source, bit 3, 0:External clock, 1:APB clock */
#define PIT_CLK_SRC_PCLK 1
#define PIT_CLK_SRC_EXT  0

/* Channel mode, bit 0~2 */
#define TIMER_32         0x1
#define TIMER_16         0x2
#define TIMER_8          0x3

/* Channel 0, 1 Reload Register */
#define CH_REL(ch)       (0x24 + (ch * 0x10))

/* Channel 0, 1 Counter Register */
#define CH_CNT(ch)       (0x28 + (ch * 0x10))

#define to_atcpit_data_clksrc(x) \
	container_of(x, struct atcpit_data, clksrc)

#define to_atcpit_data_clkevt(x) \
	container_of(x, struct atcpit_data, clkevt)

struct atcpit_data {
	struct clock_event_device clkevt;
	struct clocksource        clksrc;
	void __iomem              *base;
	struct clk                *src_clk;
	u8                        pit_num_ch;
	u8                        clock_src_ch;
	u8                        clock_evt_ch;
	u8                        pit_clk_src;
	u8                        irq;
};

static inline u8 atcpit_check_pit_id(void __iomem *base)
{
	return ((readl(base + ID_REV) & ATCPIT_ID_MSK) >> ATCPIT_ID_SFT) == ATCPIT_ID;
}

static inline u8 atcpit_get_num_ch(void __iomem *base)
{
	return readl(base + CFG) & NUM_PIT_CH_MSK;
}

static inline void atcpit_ch_reload(void __iomem *base, u8 ch, u32 reload)
{
	writel(reload, base + CH_REL(ch));
}

static void atcpit_ch_crtl(void __iomem *base, u8 ch, u8 ch_clksrc, u8 ch_mode)
{
	u32 ch_ctrl;

	ch_ctrl = readl(base + CH_CTRL(ch));
	ch_ctrl &= ~(CH_CTRL_MODE_MSK | CH_CTRL_CLK_MSK);
	ch_ctrl |= (ch_clksrc << CH_CTRL_CLK_SFT) | ch_mode;
	writel(ch_ctrl, base + CH_CTRL(ch));
}

static void atcpit_ch_en(void __iomem *base, u8 ch, u8 timer_id, u8 en)
{
	u32 pit_en;

	pit_en = readl(base + CH_EN_REG);
	if (en)
		pit_en |= CH_EN_TIME(ch, timer_id);
	else
		pit_en &= ~(CH_EN_TIME(ch, timer_id));

	writel(pit_en, base + CH_EN_REG);
}

static void atcpit_ch_int_en(void __iomem *base, u8 ch, u8 timer_id, u8 en)
{
	u32 pit_int_en;

	pit_int_en = readl(base + INT_EN);

	if (en)
		pit_int_en |= CH_INT_EN(ch, timer_id);
	else
		pit_int_en &= ~(CH_INT_EN(ch, timer_id));

	writel(pit_int_en, base + INT_EN);
}

static void atcpit_ch_clear_int(void __iomem *base, u8 ch, u8 timer_id)
{
	writel(CH_INT_STA(ch, timer_id), base + INT_STA);
}

static inline void atcpit_clkevt_time_setup(struct atcpit_data *pit_data, unsigned long delay)
{
	atcpit_ch_reload(pit_data->base, pit_data->clock_evt_ch, delay);
}

static inline void atcpit_clkevt_time_start(struct atcpit_data *pit_data)
{
	atcpit_ch_en(pit_data->base, pit_data->clock_evt_ch, TIMER0, 1);
}

static inline void atcpit_clkevt_time_stop(struct atcpit_data *pit_data)
{
	atcpit_ch_en(pit_data->base, pit_data->clock_evt_ch, TIMER0, 0);
	atcpit_ch_clear_int(pit_data->base, pit_data->clock_evt_ch, TIMER0);
}

static int atcpit_clkevt_set_periodic(struct clock_event_device *evt)
{
	struct atcpit_data *pit_data;
	struct timer_of *to = to_timer_of(evt);

	pit_data = to_atcpit_data_clkevt(evt);

	atcpit_clkevt_time_stop(pit_data);
	atcpit_clkevt_time_setup(pit_data, timer_of_period(to));
	atcpit_clkevt_time_start(pit_data);

	return 0;
}

static int atcpit_clkevt_shutdown(struct clock_event_device *evt)
{
	struct atcpit_data *pit_data;

	pit_data = to_atcpit_data_clkevt(evt);
	atcpit_clkevt_time_stop(pit_data);

	return 0;
}

static irqreturn_t atcpit_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;
	struct atcpit_data *pit_data = to_atcpit_data_clkevt(evt);

	atcpit_ch_clear_int(pit_data->base, pit_data->clock_evt_ch, TIMER0);

	if (evt->event_handler)
		evt->event_handler(evt);

	return IRQ_HANDLED;
}

static u64 atcpit_clksrc_read(struct clocksource *clksrc)
{
	struct atcpit_data *atcpit;

	atcpit = to_atcpit_data_clksrc(clksrc);

	return ~readl(atcpit->base + CH_CNT(atcpit->clock_src_ch));
}

static int __init atcpit_clockevent_init(struct device_node *node, struct atcpit_data *pit_data)
{
	unsigned long pit_clk_rate;
	int           ret = 0;

	pit_clk_rate = clk_get_rate(pit_data->src_clk);
	if (!pit_clk_rate) {
		pr_err("Invalid clock rate\n");
		return -EINVAL;
	}

	pit_data->clkevt.name = "atcpit_timer";
	pit_data->clkevt.features = CLOCK_EVT_FEAT_PERIODIC;
	pit_data->clkevt.set_state_shutdown = atcpit_clkevt_shutdown;
	pit_data->clkevt.set_state_periodic = atcpit_clkevt_set_periodic;
	pit_data->clkevt.tick_resume = atcpit_clkevt_shutdown;
	pit_data->clkevt.rating = 200;
	pit_data->irq = pit_data->irq;
	pit_data->clkevt.cpumask = cpu_possible_mask;

	atcpit_ch_crtl(pit_data->base, pit_data->clock_evt_ch, pit_data->pit_clk_src, TIMER_32);
	atcpit_ch_clear_int(pit_data->base, pit_data->clock_evt_ch, TIMER0);
	atcpit_ch_int_en(pit_data->base, pit_data->clock_evt_ch, TIMER0, 1);
	ret = request_irq(pit_data->irq, atcpit_timer_interrupt, IRQF_TIMER | IRQF_IRQPOLL,
			  "atcpit_timer", &pit_data->clkevt);
	if (ret) {
		pr_err("Unable to register interrupt for atcpit_timer\n");
		goto ERR_EXIT;
	}

	clockevents_config_and_register(&pit_data->clkevt, pit_clk_rate, 0xf, 0xfffffffe);

ERR_EXIT:
	return ret;
}


static int __init atcpit_clocksource_init(struct device_node *node, struct atcpit_data *pit_data)
{
	unsigned long pit_clk_rate;
	int           ret = 0;

	pit_clk_rate = clk_get_rate(pit_data->src_clk);
	if (!pit_clk_rate) {
		pr_err("Invalid clock rate\n");
		return -EINVAL;
	}

	pit_data->clksrc.mask = CLOCKSOURCE_MASK(32);
	pit_data->clksrc.name = "atcpit_clocksource";
	pit_data->clksrc.rating = 200;
	pit_data->clksrc.read = atcpit_clksrc_read;
	pit_data->clksrc.flags = CLOCK_SOURCE_IS_CONTINUOUS;

	atcpit_ch_crtl(pit_data->base, pit_data->clock_src_ch, pit_data->pit_clk_src, TIMER_32);
	atcpit_ch_reload(pit_data->base, pit_data->clock_src_ch, 0xFFFFFFFF);
	atcpit_ch_en(pit_data->base, pit_data->clock_src_ch, TIMER0, 1);

	ret = clocksource_register_hz(&pit_data->clksrc, pit_clk_rate);

	if (ret)
		pr_err("Failed to register clocksource\n");

	return ret;
}

static int __init atcpit_timer_init(struct device_node *node)
{
	int (*read_fixup)(void __iomem *addr, unsigned int val, unsigned int shift_bits);
	struct atcpit_data *pit_data;
	int                ret = 0;
	u32                val;

	pit_data = kzalloc(sizeof(*pit_data), GFP_KERNEL);
	if (!pit_data)
		return -ENOMEM;

	pit_data->clock_evt_ch = 0xFF;
	pit_data->clock_src_ch = 0xFF;

	pit_data->base = of_iomap(node, 0);
	if (!pit_data->base) {
		pr_err("Invalid io addr\n");
		ret = -ENXIO;
		goto ERR_EXIT;
	}

	/* Check ID register */
	read_fixup = symbol_get(readl_fixup);
	if (read_fixup != NULL) {
		ret = read_fixup(pit_data->base, ATCPIT_ID, 12);
		symbol_put(readl_fixup);
	} else {
		ret = atcpit_check_pit_id(pit_data->base);
	}
	if (!ret) {
		pr_err("Invalid ID register, ATCPIT is not supported\n");
		ret = -ENXIO;
		goto ERR_EXIT;
	}

	pit_data->pit_num_ch = atcpit_get_num_ch(pit_data->base);

	pit_data->src_clk = of_clk_get(node, 0);
	if (IS_ERR(pit_data->src_clk)) {
		pr_err("Invalid src_clk\n");
		ret = PTR_ERR(pit_data->src_clk);
		goto ERR_EXIT;
	}

	pit_data->irq = irq_of_parse_and_map(node, 0);
	if (!pit_data->irq) {
		pr_err("Invalid irq\n");
		ret = -EINVAL;
		goto ERR_EXIT;
	}

	ret = of_property_read_u32(node, "pit_clk_src", &val);
	if (ret || val > PIT_CLK_SRC_PCLK) {
		pr_err("Invalid pit clock source\n");
		goto ERR_EXIT;
	}
	pit_data->pit_clk_src = val;

	ret = of_property_read_u32(node, "clock_src_ch", &val);
	if (!ret) {
		if (val < pit_data->pit_num_ch) {
			pit_data->clock_src_ch = val;
			ret = atcpit_clocksource_init(node, pit_data);
			if (ret)
				goto ERR_EXIT;
		} else {
			pr_err("Invalid clock_src_ch:%d\n", val);
		}
	}

	ret = of_property_read_u32(node, "clock_evt_ch", &val);
	if (!ret) {
		if (val < pit_data->pit_num_ch) {
			pit_data->clock_evt_ch = val;
			ret = atcpit_clockevent_init(node, pit_data);
			if (ret)
				goto ERR_EXIT;
		} else {
			pr_err("Invalid clock_evt_ch:%d\n", val);
		}
	}

	return ret;

ERR_EXIT:
	kfree(pit_data);
	return ret;
}

TIMER_OF_DECLARE(andes_atcpit, "andestech,atcpit100", atcpit_timer_init);
