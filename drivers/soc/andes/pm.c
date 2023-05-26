// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */
#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/device.h>
#include <linux/printk.h>

#include <soc/andes/smu.h>

extern void __iomem *plic_regs;
#define PLIC_PEND_OFF 0x1000
#define MAX_DEVICES 1024
#define MAX_USE_REGS (MAX_DEVICES / 32)

int suspend_begin;

#ifndef CONFIG_ANDES_ATCSMU
static void andes_suspend_cpu(void)
{
	int i;
	unsigned int wake;
	u32 __iomem *reg = plic_regs + PLIC_PEND_OFF;

	while (true) {
		__asm__ volatile("wfi");
		for (i = 0; i < MAX_USE_REGS; i++) {
			if (wake_event[i]) {
				wake = readl(reg + i);
				if (wake_event[i] & wake)
					goto wakeup;
			}
		}
	}
wakeup:
	return;
}
#endif

static int andes_pm_enter(suspend_state_t state)
{
	pr_debug("%s:state:%d\n", __func__, state);
	switch (state) {
	case PM_SUSPEND_STANDBY:
#ifdef CONFIG_ANDES_ATCSMU
		atcsmu_suspend2standby();
#else
		andes_suspend_cpu();
#endif
		return 0;
	case PM_SUSPEND_MEM:
#ifdef CONFIG_ANDES_ATCSMU
		atcsmu_suspend2ram();
#else
		andes_suspend_cpu();
#endif
		return 0;
	default:
		return -EINVAL;
	}
}

static int andes_pm_valid(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_ON:
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		return 1;
	default:
		return -EINVAL;
	}
}

static int andes_pm_begin(suspend_state_t state)
{
	suspend_begin = state;
	return 0;
}

static void andes_pm_end(void)
{
	suspend_begin = 0;
}

static const struct platform_suspend_ops andes_pm_ops = {
	.valid	= andes_pm_valid,
	.begin	= andes_pm_begin,
	.enter	= andes_pm_enter,
	.end	= andes_pm_end,
};

static int __init andes_pm_init(void)
{
	suspend_set_ops(&andes_pm_ops);
	return 0;
}
late_initcall(andes_pm_init);
