// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Andes Technology Corporation.
 */
#include <asm/sbi.h>
#include <asm/suspend.h>
#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/device.h>
#include <linux/printk.h>
#include <linux/soc/andes/smu.h>

extern int sbi_system_suspend(unsigned long, unsigned long, unsigned long);

static int andes_pm_enter(suspend_state_t state)
{
	atcsmu_set_wake(*andes_wake_event);

	if (state == PM_SUSPEND_STANDBY)
		return cpu_suspend(SBI_SUSP_AE350_LIGHT_SLEEP, sbi_system_suspend);
	else if (state == PM_SUSPEND_MEM)
		return cpu_suspend(SBI_SUSP_AE350_DEEP_SLEEP, sbi_system_suspend);
	else
		return -EINVAL;
}

static int andes_pm_valid(suspend_state_t state)
{
	return ((state == PM_SUSPEND_MEM) || (state == PM_SUSPEND_STANDBY));
}

static int andes_pm_begin(suspend_state_t state)
{
	if (state == PM_SUSPEND_STANDBY)
		atcsmu_set_sleep_type(SBI_SUSP_AE350_LIGHT_SLEEP);
	else if (state == PM_SUSPEND_MEM)
		atcsmu_set_sleep_type(SBI_SUSP_AE350_DEEP_SLEEP);
	else
		return -EINVAL;

	return 0;
}

static void andes_pm_end(void)
{
	atcsmu_set_wake(0);
	atcsmu_set_sleep_type(0);
}

static const struct platform_suspend_ops andes_pm_ops = {
	.valid	= andes_pm_valid,
	.enter	= andes_pm_enter,
	.begin	= andes_pm_begin,
	.end	= andes_pm_end,
};

static int __init andes_pm_init(void)
{
	if (IS_ENABLED(CONFIG_SUSPEND)){
		pr_info("AE350: Andes Power Management\n");
		suspend_set_ops(&andes_pm_ops);
	}
	return 0;
}
late_initcall(andes_pm_init);
