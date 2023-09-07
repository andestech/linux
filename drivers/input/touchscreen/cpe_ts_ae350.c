// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes technology Corporation
 */

#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/irqnr.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <asm/irq.h>
#include "cpe_ts_ae350.h"

#define ads_dbg(enabled, tagged, ...)					\
	do {								\
		if (enabled) {						\
			if (tagged)					\
				pr_err("[ %30s() ] ", __func__);        \
			pr_err(__VA_ARGS__);				\
		}							\
	} while (0)

#define TS_POLL_DELAY        (1 * 1000000)          /* ns delay before the first sample */
#define TS_POLL_PERIOD       (delay * 1000000)      /* ns delay between samples */
#define	read_reg(addr)       ioread32(addr)
#define	write_reg(val, addr) iowrite32(val, addr)

static int debug;
static int delay = 25;
module_param(debug, int, 0);
module_param(delay, int, 0);

struct ads7846 {
	void __iomem *regs;
	struct input_dev *input;
	char phys[32];
	struct hrtimer timer;
	int irq;
	spinlock_t lock;
	bool disabled;
};

struct ts_event {
	int x;
	int y;
	int z1, z2;
	int Rt;
};

struct ads7846 touchscreen;

#define ADS_START           (0x1UL << 7)
#define ADS_A2A1A0_d_y      (0x1UL << 4)	/* differential */
#define ADS_A2A1A0_d_z1     (0x3UL << 4)	/* differential */
#define ADS_A2A1A0_d_z2     (0x4UL << 4)	/* differential */
#define ADS_A2A1A0_d_x      (0x5UL << 4)	/* differential */
#define ADS_12_BIT          (0x0UL << 3)
#define ADS_SER             (0x1UL << 2)	/* non-differential */
#define ADS_DFR             (0x0UL << 2)	/* differential */
#define ADS_PD10_PDOWN      (0x0UL << 0)	/* lowpower mode + penirq */
#define ADS_PD10_ADC_ON     (0x1UL << 0)	/* ADC on */
#define ADS_PD10_REF_ON     (0x2UL << 0)	/* vREF on + penirq */
#define ADS_PD10_ALL_ON     (0x3UL << 0)	/* ADC + vREF on */
#define MAX_12BIT           ((0x1UL << 12) - 1)

#define READ_X              (ADS_A2A1A0_d_x  | ADS_12_BIT | ADS_DFR)
#define READ_Y              (ADS_A2A1A0_d_y  | ADS_12_BIT | ADS_DFR)
#define READ_Z1             (ADS_A2A1A0_d_z1 | ADS_12_BIT | ADS_DFR)
#define READ_Z2             (ADS_A2A1A0_d_z2 | ADS_12_BIT | ADS_DFR)

static int
read_val(struct ads7846 *ts, unsigned long cmd)
{
	unsigned long data = 0;

	ads_dbg(0, 1, "Queuing data: 0x%08lx\n", cmd << 16);

	while (!(read_reg(ts->regs + STATUS) & TX_EMPTY))
		;

	write_reg((ADS_START | cmd) << 16, ts->regs + DATA);
	write_reg(1, ts->regs + CMD);

	while ((read_reg(ts->regs + STATUS) & RX_EMPTY))
		;

	data = (read_reg(ts->regs + DATA) >> 3) & 0xFFF;
	ads_dbg(0, 1, "CMD <%02lx> data: 0x%08lx( %ld)\n", cmd, data, data);

	return data;
}

static int pendown(struct ads7846 *ts)
{
	return read_val(ts, READ_Z1) > 40;
}

static void report(struct ads7846 *ts, struct ts_event *e)
{
	e->x = read_val(ts, READ_X);
	e->y = read_val(ts, READ_Y);
	e->z1 = read_val(ts, READ_Z1);
	e->z2 = read_val(ts, READ_Z2);
	ads_dbg(debug, 1, "x: %4d, y: %4d, z1: %4d, z2: %4d\n", e->x, e->y, e->z1, e->z2);
}

#define FILTER_LIMIT 35
static enum hrtimer_restart ads7846_timer(struct hrtimer *handle)
{
	struct ads7846 *ts = container_of(handle, struct ads7846, timer);
	struct ts_event e;
	static int xp, yp;

	if (ts->disabled)
		return HRTIMER_NORESTART;

	if (!pendown(ts)) {
		ads_dbg(debug, 1, "Release\n");
		input_report_key(ts->input, BTN_TOUCH, 0);
		input_report_abs(ts->input, ABS_PRESSURE, 0);
		input_sync(ts->input);
		enable_irq(ts->irq);
		return HRTIMER_NORESTART;
	}
	report(ts, &e);
#ifdef CONFIG_TOUCHSCREEN_CPE_TS_DEJITTER_AE350
	if (abs(xp - e.x) > FILTER_LIMIT || abs(yp - e.y) > FILTER_LIMIT) {
#endif
		input_report_key(ts->input, BTN_TOUCH, 1);
		input_report_abs(ts->input, ABS_X, e.x);
		input_report_abs(ts->input, ABS_Y, e.y);
		input_report_abs(ts->input, ABS_PRESSURE, 50);
		xp = e.x;
		yp = e.y;
#ifdef CONFIG_TOUCHSCREEN_CPE_TS_DEJITTER_AE350
	}
#endif
	input_sync(ts->input);
	hrtimer_start(&ts->timer, ktime_set(0, TS_POLL_PERIOD), HRTIMER_MODE_REL);
	ads_dbg(0, 1, "Leave\n");
	return HRTIMER_NORESTART;
}


static irqreturn_t ads7846_irq(int irq, void *handle)
{
	struct ads7846 *ts = handle;

	if (ts->disabled)
		return IRQ_HANDLED;

	if (!pendown(ts))
		return IRQ_HANDLED;

	disable_irq_nosync(irq);
	hrtimer_start(&ts->timer, ktime_set(0, TS_POLL_DELAY), HRTIMER_MODE_REL);

	return IRQ_HANDLED;
}

static int xspi_init_hw(struct at_ts_platdata *pdata)
{
	int id = read_reg(pdata->regs + ID_REV);
	unsigned int reg_val;

	if ((id & ID_MSK) != (ID_ATCSPI200 << ID_OFF)) {
		ads_dbg(1, 0, "ADS7846 Touchscreen controller initialized failed:\n"
			"\tcannot detect ANdes SPI Controller\n");
		return -ENXIO;
	}

	reg_val = read_reg(pdata->regs + TIMING);
	write_reg(reg_val | ((pdata->clk / (pdata->sclk << 1)) - 1),
		  pdata->regs + TIMING);

	reg_val = read_reg(pdata->regs + FORMAT);
	write_reg(reg_val & DATA_LEN_MSK, (pdata->regs + FORMAT));

	reg_val = read_reg(pdata->regs + FORMAT);
	write_reg(reg_val | (23 << DATA_LEN_OFF), (pdata->regs + FORMAT));

	return 0;
}

static int __exit ads7846_remove(struct platform_device *pdev)
{
	struct ads7846 *ts = platform_get_drvdata(pdev);

	disable_irq(ts->irq);
	free_irq(ts->irq, ts);
	input_unregister_device(ts->input);

	return 0;
}

#ifdef CONFIG_PM
static int ads7846_suspend(struct platform_device *pdev, pm_message_t message)
{
	struct ads7846 *ts = platform_get_drvdata(pdev);

	spin_lock_irq(&ts->lock);
	ts->disabled = true;
	disable_irq(ts->irq);
	spin_unlock_irq(&ts->lock);

	return 0;
}

static int ads7846_resume(struct platform_device *pdev)
{
	struct ads7846 *ts = platform_get_drvdata(pdev);

	spin_lock_irq(&ts->lock);
	enable_irq(ts->irq);
	ts->disabled = false;
	spin_unlock_irq(&ts->lock);

	return 0;
}
#endif

static const struct of_device_id ads7846_dt_ids[] = {
	{
		.compatible = "andestech,atcts",
	},
	{},
};
MODULE_DEVICE_TABLE(of, c_dt_ids);

#ifdef CONFIG_OF
static struct at_ts_platdata *ts_parse_dt(struct device *dev)
{
	struct at_ts_platdata *pdata;
	struct device_node *np = dev->of_node;

	if (!np)
		return ERR_PTR(-ENOENT);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	if (of_property_read_u32(np, "spi-max-frequency", &pdata->sclk)) {
		dev_err(dev, "failed to get spi-max-frequency property\n");
		return ERR_PTR(-EINVAL);
	}

	if (of_property_read_u32(np, "clock-frequency", &pdata->clk)) {
		dev_err(dev, "failed to get clock-frequency property\n");
		return ERR_PTR(-EINVAL);
	}

	if (of_property_read_u32(np, "x-size", &pdata->x_max)) {
		dev_err(dev, "failed to get x-size property\n");
		return ERR_PTR(-EINVAL);
	}

	if (of_property_read_u32(np, "y-size", &pdata->y_max)) {
		dev_err(dev, "failed to get y-size property\n");
		return ERR_PTR(-EINVAL);
	}

	return pdata;
}
#else
static struct at_ts_platdata *ts_parse_dt(struct device *dev)
{
	return ERR_PTR(-EINVAL);
}
#endif

static int ads7846_probe(struct platform_device *pdev)
{
	struct ads7846 *ts = &touchscreen;
	struct input_dev *input_dev;
	int err = 0;
	struct resource *r;
	struct at_ts_platdata *pdata;

	pdata = ts_parse_dt(&pdev->dev);
	pdev->dev.platform_data = (void *)pdata;
	platform_set_drvdata(pdev, ts);
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ts->regs = devm_ioremap_resource(&pdev->dev, r);
	pdata->regs = ts->regs;

	err = xspi_init_hw(pdata);
	if (err)
		goto err_unmap;

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}
	ts->input = input_dev;
	hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ts->timer.function = ads7846_timer;
	input_dev->name = "ADS7846 Touchscreen";
	input_dev->phys = ts->phys;
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, MAX_12BIT, 0, 0);
	ts->irq = platform_get_irq(pdev, 0);

	if (request_irq(ts->irq, ads7846_irq, IRQF_TRIGGER_RISING, "touch screen", ts))
		goto err_free_mem;

	err = input_register_device(input_dev);
	if (err)
		goto err_free_irq;

	spin_lock_init(&ts->lock);

	return 0;

err_free_irq:
	free_irq(ts->irq, ts);
err_free_mem:
	input_free_device(input_dev);
err_unmap:
	iounmap(ts->regs);

	return err;
}

static struct platform_driver ads7846_driver = {
	.driver = {
		.name = "ads7846",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ads7846_dt_ids),
	},
	.remove = __exit_p(ads7846_remove),

#ifdef CONFIG_PM
	.suspend = ads7846_suspend,
	.resume = ads7846_resume,
#endif
};

module_platform_driver_probe(ads7846_driver, ads7846_probe);
MODULE_DESCRIPTION("ADS7846 TouchScreen Driver");
MODULE_LICENSE("GPL");
