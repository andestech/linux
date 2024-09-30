// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Andes ATCWDT200 watchdog timer.
 *
 * Copyright (C) 2025 Andes Technology Corporation
 */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/minmax.h>
#include <linux/moduleparam.h>
#include <linux/math64.h>

#define DRV_NAME	"atcwdt200"

/* ID and Revision Register */
#define WDT_IDREV	0x00
#define ID_OFF		12
#define ID_MSK		GENMASK(31, 12)
#define ID		(0x03002 << ID_OFF)

/* Control Register */
#define REG_WDT_CFG	0x10
#define RST_TIME_OFF	8
#define RST_TIME_MSK	GENMASK(10, 8)
#define INT_TIME_OFF	4
#define INT_TIME_MSK	GENMASK(7, 4)
#define RST_EN		(1 << 3)
#define INT_EN		(1 << 2)
#define CLK_PCLK	(1 << 1)
#define WDT_EN		(1 << 0)

/* Restart Register */
#define REG_WDT_RST	0x14
#define WDT_RST_MAGIC	0xCAFE

/* Write Enable Register */
#define REG_WDT_WE	0x18
#define WDT_WP_MAGIC	0x5AA5

/* Status Register */
#define REG_WDT_STA	0x1C
#define INT_EXPIRED	(1 << 0)

/* The default timeout value in seconds */
#define WDT_TIMEOUT	16

/* Define the array size for each timer type */
#define TMR_SZ_RST	8
#define TMR_SZ_INT_16	8
#define TMR_SZ_INT_32	16

/*
 * The conditional block modifies the REG_WDT_CFG register:
 * - If WATCHDOG_DEBUG == 1, only the WDT_EN bit is set, enabling the
 *                           watchdog timer.
 * - If WATCHDOG_DEBUG == 0, both RST_EN and WDT_EN bits are set, enabling
 *                           the watchdog timer and the reset mechanism.
 */
#define WATCHDOG_DEBUG	0

enum timer_type {
	TMR_RST,
	TMR_INT_16,
	TMR_INT_32,
	TMR_UNKNOWN,
};

static u32 timeout = WDT_TIMEOUT;
static bool nowayout = WATCHDOG_NOWAYOUT;

module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds (default="
		 __MODULE_STRING(WDT_TIMEOUT) ")");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct atcwdt_drv {
	struct watchdog_device	wdt_dev;
	struct regmap		*regmap;
	spinlock_t		lock;
	u32			clk_freq;
	u8			clk_src;
	u8			int_timer_type;
};

static const struct regmap_config atcwdt_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = REG_WDT_STA,
	.cache_type = REGCACHE_NONE,
};

static const struct watchdog_info atcwdt_info = {
	.identity = DRV_NAME,
	.options = WDIOF_SETTIMEOUT
		   | WDIOF_KEEPALIVEPING
		   | WDIOF_MAGICCLOSE,
};

/**
 * atcwdt_get_index - Get the interval value for the specified timer type
 * @index: The index of the interval in the array
 * @timer_type: The type of timer, which can be TMR_RST, TMR_INT_16, or TMR_INT_32.
 *
 * This function retrieves the interval value based on the timer type and
 * ensures the index stays within the valid range for the given timer type.
 * For TMR_RST:
    - The maximum array size is 8 (index range: 0-7).
 * For TMR_INT_16:
 *  - The maximum array size is 8 (index range: 0-7).
 * For TMR_INT_32:
 *  - The maximum array size is 16 (index range: 0-15).
 *
 * If the index exceeds the maximum array size, the function will return
 * the last element of the respective array.
 */
static inline u8 atcwdt_get_index(u8 index, enum timer_type timer_type)
{
	const u8 rst_timer_interval[TMR_SZ_RST] = {7, 8, 9, 10, 11, 12, 13, 14};
	const u8 int_timer_interval[TMR_SZ_INT_32] = {
		6, 8, 10, 11, 12, 13, 14, 15, 17, 19, 21, 23, 25, 27, 29, 31};
	u8 array_index;

	if (timer_type == TMR_RST) {
		array_index = min(index, TMR_SZ_RST - 1);
		return rst_timer_interval[array_index];
	}

	if (timer_type == TMR_INT_32)
		array_index = min(index, TMR_SZ_INT_32 - 1);
	else
		array_index = min(index, TMR_SZ_INT_16 - 1);

	return int_timer_interval[array_index];
}

/**
 * atcwdt_get_clock_period - Calculate the closest clock period based on a given tick count
 * @tick: The target tick count to match
 * @timer_type: The type of timer, which can be TMR_RST, TMR_INT_16, or TMR_INT_32.
 * @index: Pointer to store the index of the selected parameter
 *
 * This function calculates the closest clock period to the given tick count
 * by iterating through the timer parameters and selecting the one that
 * minimizes the difference between the target tick count and the calculated
 * clock period. The function determines the index of the closest parameter
 * and returns the difference between the target tick count and the selected
 * clock period.
 *
 * Return: The difference between the target tick count and the selected
 * clock period.
 */
static s64 atcwdt_get_clock_period(s64 tick,
				   enum timer_type timer_type,
				   u8 *index)
{
	s64 result;
	u8 size;
	s8 i;

	if (timer_type == TMR_RST)
		size = TMR_SZ_RST;
	else if (timer_type == TMR_INT_32)
		size = TMR_SZ_INT_32;
	else
		size = TMR_SZ_INT_16;

	*index = size - 1;
	for (i = 0; i < size; i++) {
		result = tick - (1LL << atcwdt_get_index(i, timer_type));

		if (result <= 1) {
			*index = i;
			break;
		}
	}

	return result;
}

/**
 * atcwdt_get_timeout_params - Calculate optimal parameters for Watchdog Timer
 * @drv_data: Pointer to the Watchdog driver data structure
 * @timeout: Desired timeout value (in seconds)
 * @int_timer_params: Pointer to store the calculated interrupt timer parameter index
 * @rst_timer_params: Pointer to store the calculated reset timer parameter index
 *
 * This function calculates the optimal parameter combination for the
 * interrupt timer and reset timer of the Watchdog Timer to achieve a
 * timeout value closest to, but not less than the specified timeout.
 *
 * Algorithm:
 * 1. The parameters for both the interrupt timer and reset timer are
 *    predefined as a series of options represented as powers of 2.
 * 2. The function first determines the interrupt timer's parameter index
 *    that provides a time closest to and not exceeding the desired timeout.
 * 3. Based on the selected interrupt timer, it calculates the required
 *    reset timer parameter to ensure the total timeout matches the target.
 *
 * Return: The calculated parameter indices are stored in the provided pointers.
 */
static void atcwdt_get_timeout_params(struct atcwdt_drv *drv_data,
			       u32 timeout,
			       u8 *int_timer_params,
			       u8 *rst_timer_params)
{
	s64 rest_time_ms;
	s64 result;
	u8 above;
	u8 below;
	u8 rst_index;
	u8 int_index;

	result = atcwdt_get_clock_period((s64)timeout * drv_data->clk_freq,
					 drv_data->int_timer_type,
					 &above);
	if (result == 0 || above == 0) {
		*int_timer_params = above;
		*rst_timer_params = 0;
		return;
	}
	below = above - 1;

	int_index = atcwdt_get_index(below, drv_data->int_timer_type);
	rest_time_ms = timeout * 1000LL
		       - div64_s64(1000LL << int_index, drv_data->clk_freq);

	result = atcwdt_get_clock_period(rest_time_ms * drv_data->clk_freq,
					 TMR_RST,
					 &rst_index);

	if (result > 1) {
		*int_timer_params = above;
		*rst_timer_params = 0;
	} else {
		*int_timer_params = below;
		*rst_timer_params = rst_index;
	}
}

/**
 * atcwdt_get_int_timer_type - Get the supported interrupt timer type.
 * @drv_data: Pointer to the watchdog driver data structure.
 *
 * This function tests the writable bits in the IntTime field of the control
 * register to determine the interrupt timer type supported by the hardware.
 *
 * Note: This function must only be called when the ATCWDT200 watchdog is
 * disabled. If the watchdog is enabled, this function returns TMR_UNKNOWN.
 *
 * Returns: The interrupt timer type supported by the hardware.
 */
static enum timer_type atcwdt_get_int_timer_type(struct atcwdt_drv *drv_data)
{
	u32 val;
	enum timer_type int_timer_type;

	spin_lock(&drv_data->lock);
	regmap_read(drv_data->regmap, REG_WDT_CFG, &val);
	if (val & WDT_EN) {
		spin_unlock(&drv_data->lock);
		return TMR_UNKNOWN;
	}

	/*
	 * Configures the IntTime field with the maximum mask value
	 * (INT_TIME_MSK), reads its value from the control register to
	 * identify the maximum writable bits.
	 */
	regmap_write(drv_data->regmap, REG_WDT_WE, WDT_WP_MAGIC);
	regmap_write(drv_data->regmap, REG_WDT_CFG, INT_TIME_MSK);
	regmap_read(drv_data->regmap, REG_WDT_CFG, &val);
	spin_unlock(&drv_data->lock);

	val = (val & INT_TIME_MSK) >> INT_TIME_OFF;
	switch (val) {
	case 7:
		int_timer_type = TMR_INT_16;
		break;
	case 15:
		int_timer_type = TMR_INT_32;
		break;
	default:
		int_timer_type = TMR_UNKNOWN;
	}

	return int_timer_type;
}

static s32 atcwdt_ping(struct watchdog_device *wdt_dev)
{
	struct atcwdt_drv *drv_data = watchdog_get_drvdata(wdt_dev);

	spin_lock(&drv_data->lock);
	regmap_write(drv_data->regmap, REG_WDT_WE, WDT_WP_MAGIC);
	regmap_write(drv_data->regmap, REG_WDT_RST, WDT_RST_MAGIC);
	regmap_update_bits(drv_data->regmap, REG_WDT_STA, INT_EXPIRED,
			   INT_EXPIRED);
	spin_unlock(&drv_data->lock);

	return 0;
}

static s32 atcwdt_set_timeout(struct watchdog_device *wdt_dev, u32 timeout)
{
	struct atcwdt_drv *drv_data = watchdog_get_drvdata(wdt_dev);
	u8  rst_val;
	u8  int_val;

	wdt_dev->timeout = timeout;
	atcwdt_get_timeout_params(drv_data, timeout, &int_val, &rst_val);

	spin_lock(&drv_data->lock);
	regmap_write(drv_data->regmap, REG_WDT_WE, WDT_WP_MAGIC);
	regmap_update_bits(drv_data->regmap, REG_WDT_CFG,
			   RST_TIME_MSK | INT_TIME_MSK | CLK_PCLK,
			   ((rst_val << RST_TIME_OFF) & RST_TIME_MSK)
			   | ((int_val << INT_TIME_OFF) & INT_TIME_MSK)
			   | (drv_data->clk_src & CLK_PCLK));
	spin_unlock(&drv_data->lock);
	atcwdt_ping(wdt_dev);

	return 0;
}

static s32 atcwdt_start(struct watchdog_device *wdt_dev)
{
	struct atcwdt_drv *drv_data = watchdog_get_drvdata(wdt_dev);

	atcwdt_set_timeout(wdt_dev, wdt_dev->timeout);

	spin_lock(&drv_data->lock);
	regmap_write(drv_data->regmap, REG_WDT_WE, WDT_WP_MAGIC);
#if WATCHDOG_DEBUG
	regmap_update_bits(drv_data->regmap, REG_WDT_CFG, RST_EN | WDT_EN,
			   WDT_EN);
#else
	regmap_update_bits(drv_data->regmap, REG_WDT_CFG, RST_EN | WDT_EN,
			   RST_EN | WDT_EN);
#endif
	spin_unlock(&drv_data->lock);

	return 0;
}

static s32 atcwdt_stop(struct watchdog_device *wdt_dev)
{
	struct atcwdt_drv *drv_data = watchdog_get_drvdata(wdt_dev);

	spin_lock(&drv_data->lock);
	regmap_write(drv_data->regmap, REG_WDT_WE, WDT_WP_MAGIC);
	regmap_update_bits(drv_data->regmap, REG_WDT_CFG, RST_EN | WDT_EN, 0);
	spin_unlock(&drv_data->lock);

	return 0;
}

static const struct watchdog_ops atcwdt_ops = {
	.owner = THIS_MODULE,
	.start = atcwdt_start,
	.stop = atcwdt_stop,
	.ping = atcwdt_ping,
	.set_timeout = atcwdt_set_timeout,
};

static s32 atcwdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct atcwdt_drv *drv_data;
	void __iomem *reg_base;
	u32 dtb_value;
	s32 ret;
	u8 rst_index;
	u8 int_index;

	drv_data = devm_kzalloc(dev, sizeof(struct atcwdt_drv), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;
	platform_set_drvdata(pdev, drv_data);

	spin_lock_init(&drv_data->lock);

	reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg_base)) {
		dev_err(dev, "Failed to ioremap I/O resource\n");
		return PTR_ERR(reg_base);
	}

	drv_data->regmap = devm_regmap_init_mmio(dev, reg_base,
						 &atcwdt_regmap_config);
	if (IS_ERR(drv_data->regmap)) {
		dev_err(dev, "Failed to create regmap for I/O\n");
		return PTR_ERR(drv_data->regmap);
	}

	ret = device_property_read_u32(dev, "clock-source", &dtb_value);
	if (ret || dtb_value == 0)
		drv_data->clk_src = 0;
	else
		drv_data->clk_src = CLK_PCLK;

	drv_data->int_timer_type = atcwdt_get_int_timer_type(drv_data);
	if (drv_data->int_timer_type == TMR_UNKNOWN) {
		dev_err(dev, "Failed to detect interrupt timer type\n");
		return -ENODEV;
	}

	ret = device_property_read_u32(dev, "clock-frequency",
				       &drv_data->clk_freq);
	if (ret) {
		dev_err(dev, "No clock-frequency property in DTB\n");
		return ret;
	}

	drv_data->wdt_dev.parent = dev;
	drv_data->wdt_dev.info = &atcwdt_info;
	drv_data->wdt_dev.ops = &atcwdt_ops;
	drv_data->wdt_dev.timeout = WDT_TIMEOUT;
	drv_data->wdt_dev.min_timeout = 1;

	/*
	 * If the first parameter exceeds the array size for the specified
	 * timer_type, atcwdt_get_index returns the last element of the array.
	 * Passing 0xFF as the first parameter retrieves the maximum time
	 * interval for the timer.
	 */
	int_index = atcwdt_get_index(0xFF, drv_data->int_timer_type);
	rst_index = atcwdt_get_index(0xFF, TMR_RST);
	drv_data->wdt_dev.max_timeout = ((1U << rst_index) + (1U << int_index))
					/ drv_data->clk_freq;

	watchdog_set_nowayout(&drv_data->wdt_dev, nowayout);
	watchdog_set_drvdata(&drv_data->wdt_dev, drv_data);
	watchdog_init_timeout(&drv_data->wdt_dev, timeout, dev);

	dev_dbg(dev, "initialized. timeout=%d sec nowayout=%d\n",
		drv_data->wdt_dev.timeout, nowayout);

	ret = devm_watchdog_register_device(dev, &drv_data->wdt_dev);

	return ret;
}

static s32 atcwdt_suspend(struct device *dev)
{
	struct atcwdt_drv *drv_data = dev_get_drvdata(dev);

	if (watchdog_active(&drv_data->wdt_dev))
		atcwdt_stop(&drv_data->wdt_dev);

	return 0;
}

static s32 atcwdt_resume(struct device *dev)
{
	struct atcwdt_drv *drv_data = dev_get_drvdata(dev);

	if (watchdog_active(&drv_data->wdt_dev)) {
		atcwdt_start(&drv_data->wdt_dev);
		atcwdt_ping(&drv_data->wdt_dev);
	}

	return 0;
}

static const struct of_device_id atcwdt_match[] = {
	{ .compatible = "andestech,atcwdt200" },
	{},
};
MODULE_DEVICE_TABLE(of, atcwdt_match);

static DEFINE_SIMPLE_DEV_PM_OPS(atcwdt_pm_ops, atcwdt_suspend, atcwdt_resume);

static struct platform_driver atcwdt_driver = {
	.probe		= atcwdt_probe,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = atcwdt_match,
	},
};

module_platform_driver(atcwdt_driver);

MODULE_DESCRIPTION("Andes ATCWDT200 driver");
MODULE_AUTHOR("cl634@andestech.com");
MODULE_LICENSE("GPL");
