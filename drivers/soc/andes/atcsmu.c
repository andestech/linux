// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Andes Technology Corporation.
 */
#define pr_fmt(fmt) "atcsmu: " fmt
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/soc/andes/smu.h>
#include <linux/stringify.h>

static struct atcsmu atcsmu = { 0 };
static DEFINE_SPINLOCK(ae350_clk_lock);

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

static const char *ae350_clk_enum_to_str(enum ae350_clk clk)
{
	switch (clk) {
	ENUM_TO_STR(ROOT);
	ENUM_TO_STR(CLK_32K);
	ENUM_TO_STR(SPI);
	ENUM_TO_STR(UART);
	ENUM_TO_STR(CORE);
	ENUM_TO_STR(AXI);
	ENUM_TO_STR(AHB);
	ENUM_TO_STR(APB);
	ENUM_TO_STR(AHB_GATE);
	ENUM_TO_STR(APB_GATE);
	default:
		return "UNKNOWN";
	}
}

/*
 * SMU can adjust AHB/APB clock ratios. To know the clock ratios, we have to
 * read the SMU clock ratio register(0x24).
 *
 *			SMU clock ratio register(0x24)
 * ------------------------------------------------------------------
 * HPCLKSEL   | [3:1] | HCLK(AHB) and PCLK(APB) clock ratio select
 *			------------------------------------------
 *			| Select | core : aclk : hclk : pclk
 *			|   0    |   1  :   1  :   1  :   1
 *			|   1    |   1  :   1  :   1  :  1/2
 *			|   2    |   1  :   1  :   1  :  1/4
 *			|   3    |   1  :   1  :  1/2 :  1/2
 *			|   4    |   1  :   1  :  1/2 :  1/4
 *			|  5-7   |   Reserved
 */
static struct clk_hw *ae350_clk_hw_register_fixed_factor(
			struct device *dev, const char *name,
			const char *parent_name, unsigned long flags,
			void __iomem *reg)
{
	unsigned int div = 1;
	unsigned int select = readl(reg) & SMU_CLK_RATIO_MASK;

	if (!strncmp(name, "ahb_clk", 7)) {
		if (select == 3 || select == 4)
			div = 2;
	} else if (!strncmp(name, "apb_clk", 7)) {
		if (select == 1 || select == 3)
			div = 2;
		else if (select == 2 || select == 4)
			div = 4;
	}

	return clk_hw_register_fixed_factor(dev, name, parent_name, flags, 1,
					    div);
}

static int atcsmu_probe(struct platform_device *pdev)
{
	struct atcsmu *smu = &atcsmu;
	unsigned int cpu;
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *ae350_clk_data;
	struct clk_hw *hw;

	smu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(smu->base))
		return PTR_ERR(smu->base);

	for_each_possible_cpu(cpu)
		writel(0x0, (void *)(smu->base + PCSm_WE_OFF(cpu)));

	ae350_clk_data = devm_kzalloc(dev,
				      struct_size(ae350_clk_data, hws, MAXCLKS),
				      GFP_KERNEL);
	if (!ae350_clk_data)
		return -ENOMEM;

	ae350_clk_data->num = MAXCLKS;

	/* Register fixed rate clocks */
	ae350_clk_data->hws[ROOT] =
		clk_hw_register_fixed_rate(dev, "root_clk", NULL, 0,
					   ROOT_CLK_RATE);
	ae350_clk_data->hws[CLK_32K] =
		clk_hw_register_fixed_rate(dev, "ext_clk", NULL, 0,
					   OSC_CLK_32K);
	ae350_clk_data->hws[SPI] =
		clk_hw_register_fixed_rate(dev, "spi_clk", NULL, 0,
					   SPI_CLK_RATE);
	ae350_clk_data->hws[UART] =
		clk_hw_register_fixed_rate(dev, "uart_clk", NULL, 0,
					   UART_CLK_RATE);

	/* Register gated clocks */
	ae350_clk_data->hws[CORE] =
		clk_hw_register_gate(dev, "core_clk", "root_clk",
				     0, smu->base + SMU_CLK_ENABLE,
				     SMU_CLK_ENABLE_CORE, 0, &ae350_clk_lock);
	ae350_clk_data->hws[AXI] =
		clk_hw_register_gate(dev, "axi_clk", "root_clk",
				     0, smu->base + SMU_CLK_ENABLE,
				     SMU_CLK_ENABLE_AXI, 0, &ae350_clk_lock);
	ae350_clk_data->hws[AHB] =
		ae350_clk_hw_register_fixed_factor(dev, "ahb_clk",
						   "root_clk", 0,
						   smu->base + SMU_CLK_RATIO);
	ae350_clk_data->hws[APB] =
		ae350_clk_hw_register_fixed_factor(dev, "apb_clk",
						   "root_clk", 0,
						   smu->base + SMU_CLK_RATIO);
	ae350_clk_data->hws[AHB_GATE] =
		clk_hw_register_gate(dev, "ahb_gate_clk", "ahb_clk",
				     0, smu->base + SMU_CLK_ENABLE,
				     SMU_CLK_ENABLE_AHB, 0, &ae350_clk_lock);
	ae350_clk_data->hws[APB_GATE] =
		clk_hw_register_gate(dev, "apb_gate_clk", "apb_clk",
				     0, smu->base + SMU_CLK_ENABLE,
				     SMU_CLK_ENABLE_APB, 0, &ae350_clk_lock);

	for (int i = 0; i < MAXCLKS; i++) {
		hw = ae350_clk_data->hws[i];
		if (IS_ERR(hw)) {
			dev_err_probe(dev, IS_ERR(hw),
				      "failed to register %s clock\n",
				      ae350_clk_enum_to_str(i));
			return PTR_ERR(hw);
		}
	}

	pr_info("ATCSMU driver probed\n");
	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   ae350_clk_data);
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
