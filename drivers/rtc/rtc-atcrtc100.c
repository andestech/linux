// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008-2024 Andes Technology Corporation
 * Andes RTC Support
 *
 * Copyright (C) 2006, 2007, 2008  Paul Mundt
 * Copyright (C) 2006  Jamie Lenehan
 * Copyright (C) 2008  Angelo Castello
 * Copyright (C) 2008  Roy Lee
 *
 * Based on the old arch/sh/kernel/cpu/rtc.c by:
 *
 *  Copyright (C) 2000  Philipp Rumpf <prumpf@tux.org>
 *  Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/uaccess.h>

#define DRV_NAME		 "atcrtc100"
#define RTC_READ32(offset)	 readl(rtc->regbase + offset)
#define RTC_WRITE32(val, offset) writel(val, rtc->regbase + offset)

#define RTC_ID			0x00	/* ID and Revision */
#define ID_OFF			12
#define ID_MSK			(0xfffff << ID_OFF)
#define ATCRTC100ID		(0x03011 << ID_OFF)
#define RTC_RSV			0x4	/* Reserve */
#define RTC_CNT			0x10	/* Counter */
#define RTC_ALM			0x14	/* Alarm */
#define DAY_OFF			17
#define DAY_MSK			(0x7fff)
#define HOUR_OFF		12
#define HOUR_MSK		(0x1f)
#define MIN_OFF			6
#define MIN_MSK			(0x3f)
#define SEC_OFF			0
#define SEC_MSK			(0x3f)
#define RTC_SECOND(x)		\
			((x >> SEC_OFF) & SEC_MSK) /* RTC sec */
#define RTC_MINUTE(x)		\
			((x >> MIN_OFF) & MIN_MSK) /* RTC min */
#define RTC_HOUR(x)		\
			((x >> HOUR_OFF) & HOUR_MSK) /* RTC hour */
#define RTC_DAYS(x)		\
			((x >> DAY_OFF) & DAY_MSK) /* RTC day */
#define RTC_ALM_SECOND		\
			((RTC_READ32(RTC_ALM) >> SEC_OFF) & SEC_MSK) /* RTC alarm sec */
#define RTC_ALM_MINUTE		\
			((RTC_READ32(RTC_ALM) >> MIN_OFF) & MIN_MSK) /* RTC alarm min */
#define RTC_ALM_HOUR		\
			((RTC_READ32(RTC_ALM) >> HOUR_OFF) & HOUR_MSK) /* RTC alarm hour */
#define RTC_CR			0x18	/* Control */
#define RTC_EN			(0x1UL << 0)
#define ALARM_WAKEUP		(0x1UL << 1)
#define ALARM_INT		(0x1UL << 2)
#define DAY_INT			(0x1UL << 3)
#define HOUR_INT		(0x1UL << 4)
#define MIN_INT			(0x1UL << 5)
#define SEC_INT			(0x1UL << 6)
#define HSEC_INT		(0x1UL << 7)
#define RTC_STA			0x1c	/* Status */
#define WRITE_DONE		(0x1UL << 16)

#define ATCRTC_TIME_TO_SEC(D, H, M, S)	(D * 86400LL + H * 3600 + M * 60 + S)

/*
 * The maximum day counter available for the ATCRTC100 hardware is 15 bits
 * long and can count up to 32767 days. This means that the ATCRTC100 hardware
 * can count up to about 89 years, so we set range_min to 2000 and range_max
 * to the end of the year 2089.
 */
#define ATCRTC_RTC_TIMESTAMP_END_2089	3786911999LL  /* 2089-12-31 23:59:59 */
#define ATCRTC_RTC_TIMESTAMP_BEGIN_2000 RTC_TIMESTAMP_BEGIN_2000

/*
 * WARNING: This variable is only intended to pass the LTP test. The Andes
 * internal implementation of the RTC on FPGA cannot count more than 32 days
 * because the day counter is 5 bits long.
 * The real RTC hardware should support a sufficiently large counter, and this
 * variable is automatically set to 0 if the day counter is sufficient to
 * represent the date.
 */
time64_t total_offset_sec;

struct atc_rtc {
	void __iomem *regbase;
	struct resource *res;
	unsigned int alarm_irq;
	unsigned int interrupt_irq;
	struct rtc_device *rtc_dev;
	spinlock_t lock; /* Protects this structure */
};

struct atc_rtc rtc_platform_data;

static irqreturn_t rtc_interrupt(int irq, void *dev_id)
{
	struct atc_rtc *rtc = dev_id;

	if (RTC_READ32(RTC_STA) & SEC_INT) {
		RTC_WRITE32(RTC_READ32(RTC_STA) | SEC_INT, RTC_STA);
		rtc_update_irq(rtc->rtc_dev, 1, RTC_UF | RTC_IRQF);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static irqreturn_t rtc_alarm(int irq, void *dev_id)
{
	struct atc_rtc *rtc = dev_id;

	if (RTC_READ32(RTC_STA) & ALARM_INT) {
		RTC_WRITE32(RTC_READ32(RTC_CR) & ~ALARM_INT, RTC_CR);
		RTC_WRITE32(RTC_READ32(RTC_STA) | ALARM_INT, RTC_STA);
		rtc_update_irq(rtc->rtc_dev, 1, RTC_AF | RTC_IRQF);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int atc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct atc_rtc *rtc = dev_get_drvdata(dev);

	spin_lock_irq(&rtc->lock);
	if (enabled)
		RTC_WRITE32(RTC_READ32(RTC_CR) | ALARM_INT, RTC_CR);
	else
		RTC_WRITE32(RTC_READ32(RTC_CR) & ~ALARM_INT, RTC_CR);
	spin_unlock_irq(&rtc->lock);
	return 0;
}

/**
 * This function reads the time from the RTC hardware
 * @rtc: The structure of the atc_rtc.
 *
 * This function is called in an atomic operation, so don't add code
 * to this function that will cause the process to sleep.
 */
static time64_t atc_rtc_read_rtc_time(struct atc_rtc *rtc)
{
	unsigned long rtc_cnt;
	time64_t time;

	/* Check the progress of updating the RTC registers. */
	while ((RTC_READ32(RTC_STA) & WRITE_DONE) != WRITE_DONE)
		continue;

	rtc_cnt = RTC_READ32(RTC_CNT);
	time = ATCRTC_TIME_TO_SEC(RTC_DAYS(rtc_cnt), RTC_HOUR(rtc_cnt),
				  RTC_MINUTE(rtc_cnt), RTC_SECOND(rtc_cnt));

	return time;
}

static int atc_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct atc_rtc *rtc = dev_get_drvdata(dev);
	time64_t time;

	spin_lock_irq(&rtc->lock);
	time = atc_rtc_read_rtc_time(rtc) + total_offset_sec;
	spin_unlock_irq(&rtc->lock);

	rtc_time64_to_tm(time, tm);
	if (rtc_valid_tm(tm) < 0) {
		dev_err(dev, "invalid date\n");
		rtc_time64_to_tm(0, tm);
	}
	return 0;
}

/**
 * This function write the time to the RTC hardware
 * @rtc: The structure of the atc_rtc.
 * @time: The time to set to the RTC
 *
 * This function is called in an atomic operation, so don't add code
 * to this function that will cause the process to sleep.
 */
static void atc_rtc_set_rtc_time(struct atc_rtc *rtc, time64_t time)
{
	time64_t time_rem;
	s32 rem;
	u32 counter;

	counter = ((div_s64_rem(time, 86400, &rem) & DAY_MSK)
		   << DAY_OFF);
	time_rem = rem;
	counter |= ((div_s64_rem(time_rem, 3600, &rem) & HOUR_MSK)
		    << HOUR_OFF);
	time_rem = rem;
	counter |= ((div_s64_rem(time_rem, 60, &rem) & MIN_MSK)
		    << MIN_OFF);
	counter |= ((rem & SEC_MSK) << SEC_OFF);

	RTC_WRITE32(counter, RTC_CNT);
}

static int atc_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct atc_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time tm_offset;
	time64_t rtc_time;
	time64_t sys_time;

	sys_time = rtc_tm_to_time64(tm);

	spin_lock_irq(&rtc->lock);
	atc_rtc_set_rtc_time(rtc, sys_time);

	/* Ensure the size of day counter is sufficient to represent the date. */
	rtc_time = atc_rtc_read_rtc_time(rtc);

	if (rtc_time < sys_time) {
		/*
		 * The day counter is not enough to represent the date, so
		 * we need to take an offset to improve the RTC hardware's ability
		 * to avoid getting the wrong date.
		 */
		dev_err(dev, "The size of day counter is insufficient for date representation, and the date may be incorrect when the system is restarted.\n");
		memcpy(&tm_offset, tm, sizeof(struct rtc_time));
		rtc_time = ATCRTC_TIME_TO_SEC(0, tm_offset.tm_hour, tm_offset.tm_min,
					      tm_offset.tm_sec);
		/*
		 * Only the date (years, months, and days) is stored in the
		 * total_offset_sec, and the time (hours, minutes, and seconds)
		 * is stored directly in the hardware of the RTC.
		 */
		atc_rtc_set_rtc_time(rtc, rtc_time);

		tm_offset.tm_hour = 0;
		tm_offset.tm_min = 0;
		tm_offset.tm_sec = 0;
		total_offset_sec = rtc_tm_to_time64(&tm_offset);
	} else {
		total_offset_sec = 0;
	}

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static int atc_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct atc_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &wkalrm->time;

	tm->tm_sec	= RTC_ALM_SECOND;
	tm->tm_min	= RTC_ALM_MINUTE;
	tm->tm_hour	= RTC_ALM_HOUR;
	wkalrm->enabled = (RTC_READ32(RTC_CR) & ALARM_INT) ? 1 : 0;
	return 0;
}

static int atc_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	u32 alm = 0;
	struct atc_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &wkalrm->time;
	int err = rtc_valid_tm(tm);

	if (err < 0) {
		dev_err(dev, "invalid alarm value\n");
		return err;
	}
	/* Disable alarm interrupt and clear the alarm flag */
	RTC_WRITE32(RTC_READ32(RTC_CR) & ~ALARM_INT, RTC_CR);
	/* Set alarm time */
	alm |= ((tm->tm_sec & SEC_MSK) << SEC_OFF);
	alm |= ((tm->tm_min & MIN_MSK) << MIN_OFF);
	alm |= ((tm->tm_hour & HOUR_MSK) << HOUR_OFF);

	spin_lock_irq(&rtc->lock);
	RTC_WRITE32(alm, RTC_ALM);

	while ((RTC_READ32(RTC_STA) & WRITE_DONE) != WRITE_DONE)
		continue;

	if (wkalrm->enabled)
		RTC_WRITE32(RTC_READ32(RTC_CR) | ALARM_INT, RTC_CR);
	spin_unlock_irq(&rtc->lock);

	return 0;
}

const static struct rtc_class_ops rtc_ops = {
	.alarm_irq_enable = atc_alarm_irq_enable,
	.read_time	= atc_rtc_read_time,
	.set_time	= atc_rtc_set_time,
	.read_alarm	= atc_rtc_read_alarm,
	.set_alarm	= atc_rtc_set_alarm,
};

static int atc_rtc_probe(struct platform_device *pdev)
{
	int (*read_fixup)(void __iomem *addr, unsigned int val,
		unsigned int shift_bits);
	struct atc_rtc *rtc = &rtc_platform_data;
	int ret = -ENOENT;

	spin_lock_init(&rtc->lock);

	rtc->alarm_irq = platform_get_irq(pdev, 1);
	if (rtc->alarm_irq < 0)
		goto err_exit;

	rtc->interrupt_irq = platform_get_irq(pdev, 0);
	if (rtc->interrupt_irq < 0)
		goto err_exit;

	rtc->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!rtc->res)
		goto err_exit;

	ret = -EBUSY;

	rtc->res = request_mem_region(rtc->res->start,
				      rtc->res->end - rtc->res->start + 1,
				      pdev->name);
	if (!rtc->res)
		goto err_request_region;

	ret = -EINVAL;

	rtc->regbase = (void __iomem *)ioremap(rtc->res->start,
					       rtc->res->end - rtc->res->start + 1);
	if (!rtc->regbase)
		goto err_ioremap1;

	read_fixup = symbol_get(readl_fixup);
	/* Check ID and Revision register 0x030110 */
	ret = read_fixup(rtc->regbase, 0x030110, 8);
	symbol_put(readl_fixup);
	if (!ret) {
		dev_err(&pdev->dev,
			"Failed to read the ID register, ATCRTC100 is not supported.\n");
		return -ENOENT;
	}

	if ((RTC_READ32(RTC_ID) & ID_MSK) != ATCRTC100ID)
		return -ENOENT;

	platform_set_drvdata(pdev, rtc);

	if (of_property_read_bool(pdev->dev.of_node, "wakeup-source"))
		device_init_wakeup(&pdev->dev, true);

	rtc->rtc_dev = devm_rtc_allocate_device(&pdev->dev);
	rtc->rtc_dev->ops = &rtc_ops;
	rtc->rtc_dev->range_min = ATCRTC_RTC_TIMESTAMP_BEGIN_2000;
	rtc->rtc_dev->range_max = ATCRTC_RTC_TIMESTAMP_END_2089;

	if (IS_ERR(rtc->rtc_dev)) {
		ret = PTR_ERR(rtc->rtc_dev);
		goto err_unmap;
	}

	rtc->rtc_dev->max_user_freq = 256;
	rtc->rtc_dev->irq_freq = 1;

	ret = devm_rtc_register_device(rtc->rtc_dev);
	if (ret)
		goto err_unmap;

	ret = request_irq(rtc->alarm_irq, rtc_alarm, 0,
			  "RTC Alarm : atcrtc100", rtc);
	if (ret)
		goto err_exit;

	ret = request_irq(rtc->interrupt_irq, rtc_interrupt,
			  0, "RTC Interrupt : atcrtc100", rtc);
	if (ret)
		goto err_interrupt_irq;

	RTC_WRITE32(RTC_READ32(RTC_CR) | RTC_EN, RTC_CR);

	return 0;

err_unmap:
	iounmap(rtc->regbase);
err_ioremap1:
	release_resource(rtc->res);
err_request_region:
	free_irq(rtc->interrupt_irq, rtc);
err_interrupt_irq:
	free_irq(rtc->alarm_irq, rtc);
err_exit:
	return ret;
}

static int atc_rtc_remove(struct platform_device *pdev)
{
	struct atc_rtc *rtc = platform_get_drvdata(pdev);

	/*
	 * Because generic rtc will not execute rtc_device_release()
	 * when call rtc_device_unregister(),
	 * rtc id will increase when unloading a rtc driver.
	 * This can work around to recycle rtc id.
	 * But if kernel fix this issue, it shell be removed away.
	 */
	rtc->rtc_dev->dev.release(&rtc->rtc_dev->dev);
	RTC_WRITE32(RTC_READ32(RTC_CR) & ~(RTC_EN | SEC_INT | ALARM_INT), RTC_CR);
	free_irq(rtc->alarm_irq, rtc);
	free_irq(rtc->interrupt_irq, rtc);
	iounmap(rtc->regbase);
	release_resource(rtc->res);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int atc_rtc_resume(struct device *dev)
{
	struct atc_rtc *rtc_dd = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(rtc_dd->alarm_irq);

	return 0;
}

static int atc_rtc_suspend(struct device *dev)
{
	struct atc_rtc *rtc_dd = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(rtc_dd->alarm_irq);

	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(atc_rtc_pm_ops, atc_rtc_suspend, atc_rtc_resume);

#ifdef CONFIG_OF
static const struct of_device_id atc_rtc_dt_match[] = {
	{.compatible = "andestech,atcrtc100" },
	{},
};
MODULE_DEVICE_TABLE(of, atc_rtc_dt_match);
#endif

static struct platform_driver atc_rtc_platform_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(atc_rtc_dt_match),
		.pm = &atc_rtc_pm_ops,
	},
	.probe		= atc_rtc_probe,
	.remove		= atc_rtc_remove,
};

module_platform_driver(atc_rtc_platform_driver);
MODULE_DESCRIPTION("Andes ATCRTC100 driver");
MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>, "
	      "Jamie Lenehan <lenehan@twibble.org>, "
	      "Angelo Castello <angelo.castello@st.com>, "
	      "Nick Hu <nickhu@andestech.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
