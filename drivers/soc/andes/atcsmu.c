// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/suspend.h>

#include <asm/tlbflush.h>
#include <asm/sbi.h>
#include <asm/csr.h>
#include <soc/andes/andes.h>
#include <soc/andes/sbi.h>
#include <soc/andes/smu.h>

// define in drivers/soc/andes/pm.c
extern int suspend_begin;

struct atcsmu atcsmu;
EXPORT_SYMBOL(atcsmu);

void sbi_andes_set_suspend_mode(unsigned int suspend_mode)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_SET_SUSPEND_MODE, suspend_mode,
		  0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_andes_set_suspend_mode);

void sbi_andes_enter_suspend_mode(int main_core, unsigned int wake_event)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_ENTER_SUSPEND_MODE, main_core,
		  wake_event, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_andes_enter_suspend_mode);

#ifdef CONFIG_ANDES_ATCSMU
/* main hart */
void atcsmu_suspend2ram(void)
{
	sbi_andes_set_suspend_mode(DEEP_SLEEP_MODE);
	sbi_andes_enter_suspend_mode(true, *wake_event);
}

/* main hart */
void atcsmu_suspend2standby(void)
{
	sbi_andes_set_suspend_mode(LIGHT_SLEEP_MODE);
	sbi_andes_enter_suspend_mode(true, *wake_event);
}

/* other harts or hotplugging hart */
/* arch/riscv/kernel/cpu-hotplug.c: arch_cpu_idle_dead */
void atcsmu_set_suspend_mode(void)
{
	if (suspend_begin == PM_SUSPEND_MEM)
		sbi_andes_set_suspend_mode(DEEP_SLEEP_MODE);
	else if (suspend_begin == PM_SUSPEND_STANDBY)
		sbi_andes_set_suspend_mode(LIGHT_SLEEP_MODE);
	else
		sbi_andes_set_suspend_mode(CPUHOTPLUG_DEEP_SLEEP_MODE);
}
#endif

static int atcsmu_probe(struct platform_device *pdev)
{
	struct atcsmu *smu = &atcsmu;
	int ret = -ENOENT;
	int pcs = 0;

	spin_lock_init(&smu->lock);

	smu->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!smu->res)
		goto err_exit;

	ret = -EBUSY;

	smu->res = request_mem_region(smu->res->start,
				      smu->res->end - smu->res->start + 1,
				      pdev->name);
	if (!smu->res)
		goto err_exit;

	ret = -EINVAL;

	smu->base =
		ioremap(smu->res->start, smu->res->end - smu->res->start + 1);
	if (!smu->base)
		goto err_ioremap;

	for (pcs = 0; pcs < MAX_PCS_SLOT; pcs++)
		writel(0xffdfffff, (void *)(smu->base + PCSN_WE_OFF(pcs)));

	return 0;
err_ioremap:
	release_resource(smu->res);
err_exit:
	return ret;
}

static int __exit atcsmu_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id atcsmu_of_id_table[] = {
	{ .compatible = "andestech,atcsmu" },
	{}
};
MODULE_DEVICE_TABLE(of, atcsmu_of_id_table);
#endif

static struct platform_driver atcsmu_driver = {
	.probe = atcsmu_probe,
	.remove = __exit_p(atcsmu_remove),
	.driver = {
		.name = "atcsmu",
		.of_match_table = of_match_ptr(atcsmu_of_id_table),
	},
};

static int __init atcsmu_init(void)
{
	int ret = platform_driver_register(&atcsmu_driver);

	return ret;
}
subsys_initcall(atcsmu_init);
