// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Andes Technology Corporation.
 */
#define pr_fmt(fmt) "atcsmu: " fmt
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/soc/andes/smu.h>

static struct atcsmu atcsmu = { 0 };

void __iomem *atcsmu_get_address(void)
{
	struct atcsmu *smu = &atcsmu;
	return smu->base;
}
EXPORT_SYMBOL(atcsmu_get_address);

/* SMU per hart scratch reg could be used to store sleep type */
void atcsmu_set_sleep_type(unsigned long sleep_type)
{
	struct atcsmu *smu = &atcsmu;
	unsigned int cpu, hart;

	for_each_online_cpu(cpu) {
		hart = cpuid_to_hartid_map(cpu);

		writel(sleep_type, (void *)(smu->base + PCSm_SCRATCH_OFF(hart)));
	}
}

void atcsmu_set_wake(unsigned long wake_event)
{
	struct atcsmu *smu = &atcsmu;
	unsigned int cpu, hart;

	for_each_online_cpu(cpu) {
		hart = cpuid_to_hartid_map(cpu);

		writel(wake_event, (void *)(smu->base + PCSm_WE_OFF(hart)));
	}
}

static int atcsmu_probe(struct platform_device *pdev)
{
	struct atcsmu *smu = &atcsmu;
	unsigned int cpu;

	smu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(smu->base))
		return PTR_ERR(smu->base);

	for_each_possible_cpu(cpu)
		writel(0x0, (void *)(smu->base + PCSm_WE_OFF(cpu)));

	pr_info("ATCSMU driver probed\n");

	return 0;
}

static int __exit atcsmu_remove(struct platform_device *pdev)
{
	pr_info("ATCSMU driver removed\n");
	return 0;
}

static const struct of_device_id atcsmu_of_id_table[] = {
	{ .compatible = "andestech,atcsmu" },
	{}
};
MODULE_DEVICE_TABLE(of, atcsmu_of_id_table);

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
