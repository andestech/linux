// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/cacheinfo.h>
#include <linux/sizes.h>
#include <linux/smp.h>
#include <linux/irqflags.h>
#include <linux/io.h>
#include <linux/of_irq.h>
#include <asm/csr.h>
#include <asm/sbi.h>
#include <soc/andes/csr.h>
#include <soc/andes/sbi.h>
#include <soc/andes/dma.h>

#define MAX_CACHE_LINE_SIZE 256
#define EVSEL_MASK	0xff
#define SEL_PER_CTL	8
#define SEL_OFF(id)	(8 * (id % 8))

void __iomem *l2c_base;

u32 L2C_REG_PER_CORE_OFFSET;
u32 CCTL_L2_STATUS_PER_CORE_OFFSET;
u32 L2C_REG_STATUS_OFFSET;

DEFINE_PER_CPU(struct andesv5_cache_info, cpu_cache_info) = {
	.init_done = 0,
	.dcache_line_size = SZ_32
};

static void fill_cpu_cache_info(struct andesv5_cache_info *cpu_ci)
{
	int ncpu = get_cpu();
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(ncpu);
	struct cacheinfo *this_leaf = this_cpu_ci->info_list;
	unsigned int i = 0;

	for (; i < this_cpu_ci->num_leaves; i++, this_leaf++) {
		if (this_leaf->type == CACHE_TYPE_DATA) {
			cpu_ci->dcache_line_size =
				this_leaf->coherency_line_size;
		}
	}
	cpu_ci->init_done = true;
	put_cpu();
}

static inline int get_cache_line_size(void)
{
	int ncpu = get_cpu();
	struct andesv5_cache_info *cpu_ci = &per_cpu(cpu_cache_info, ncpu);

	if (unlikely(cpu_ci->init_done == false))
		fill_cpu_cache_info(cpu_ci);
	put_cpu();
	return cpu_ci->dcache_line_size;
}

static uint32_t cpu_l2c_get_cctl_status(void)
{
	int mhartid = get_cpu();

	put_cpu();
	return readl((void *)(l2c_base + L2C_REG_C0_STATUS_OFFSET +
		     mhartid * L2C_REG_STATUS_OFFSET));
}

static void cpu_l2c_cctl(unsigned long long pa, int mhartid, unsigned long ops)
{
#ifdef CONFIG_64BIT
	writeq(pa,
	       (void *)(l2c_base +
			L2C_REG_CN_ACC_OFFSET(mhartid)));
#else
	/*
	 * Considering RV32 potential to use over 4G memory,
	 * the physical address is split into upper and lower 32 bits
	 * and stored in a 64-bits PA-type L2C CCTL access line register.
	 */
	writel((unsigned long)(pa & 0xFFFFFFFF),
	       (void *)(l2c_base +
			L2C_REG_CN_ACC_OFFSET(mhartid)));

	writel((unsigned long)(pa >> 32),
	       (void *)(l2c_base +
			L2C_REG_CN_ACC_OFFSET(mhartid)) + 0x4);
#endif /* !CONFIG_64BIT */

	writel(ops,
	       (void *)(l2c_base +
			L2C_REG_CN_CMD_OFFSET(mhartid)));

	while ((cpu_l2c_get_cctl_status() &
		CCTL_L2_STATUS_CN_MASK(mhartid)) !=
		CCTL_L2_STATUS_IDLE)
		;
}

void cpu_dcache_wb_range(unsigned long start, unsigned long end, int line_size,
			 struct page *page)
{
	int mhartid = get_cpu();
	unsigned long long pa = page_to_phys(page);

	if (start & (~PAGE_MASK))
		pa += start & ~PAGE_MASK;

	while (end > start) {
		custom_csr_write(CCTL_REG_UCCTLBEGINADDR_NUM, start);
		custom_csr_write(CCTL_REG_UCCTLCOMMAND_NUM, CCTL_L1D_VA_WB);

		if (likely(l2c_base))
			cpu_l2c_cctl(pa, mhartid, CCTL_L2_PA_WB);

		start += line_size;
		pa += line_size;
	}
	put_cpu();
}

void cpu_dcache_inval_range(unsigned long start, unsigned long end,
			    int line_size, struct page *page)
{
	int mhartid = get_cpu();
	unsigned long long pa = page_to_phys(page);

	if (start & (~PAGE_MASK))
		pa += start & ~PAGE_MASK;

	while (end > start) {
		custom_csr_write(CCTL_REG_UCCTLBEGINADDR_NUM, start);
		custom_csr_write(CCTL_REG_UCCTLCOMMAND_NUM, CCTL_L1D_VA_INVAL);

		if (likely(l2c_base))
			cpu_l2c_cctl(pa, mhartid, CCTL_L2_PA_INVAL);

		start += line_size;
		pa += line_size;
	}
	put_cpu();
}
void cpu_dma_inval_range(void *info)
{
	unsigned long flags;
	unsigned long line_size = get_cache_line_size();
	struct range_info *ri = info;
	unsigned long start = ri->start;
	unsigned long end = ri->end;
	unsigned long old_start = start;
	unsigned long old_end = end;
	char cache_buf[2][MAX_CACHE_LINE_SIZE] = {0};

	if (unlikely(start == end))
		return;

	start = start & (~(line_size - 1));
	end = ((end + line_size - 1) & (~(line_size - 1)));

	local_irq_save(flags);
	if (unlikely(start != old_start))
		memcpy(&cache_buf[0][0], (void *)start, line_size);

	if (unlikely(end != old_end)) {
		memcpy(&cache_buf[1][0], (void *)(old_end & (~(line_size - 1))),
		       line_size);
	}
	cpu_dcache_inval_range(start, end, line_size, ri->page);
	if (unlikely(start != old_start)) {
		memcpy((void *)start, &cache_buf[0][0],
		       (old_start & (line_size - 1)));
	}
	if (unlikely(end != old_end)) {
		memcpy((void *)(old_end + 1),
		       &cache_buf[1][(old_end & (line_size - 1)) + 1],
		       end - old_end - 1);
	}
	local_irq_restore(flags);
}
EXPORT_SYMBOL(cpu_dma_inval_range);

void cpu_dma_wb_range(void *info)
{
	unsigned long flags;
	unsigned long line_size = get_cache_line_size();
	struct range_info *ri = info;
	unsigned long start = ri->start;
	unsigned long end = ri->end;

	local_irq_save(flags);
	start = start & (~(line_size - 1));
	cpu_dcache_wb_range(start, end, line_size, ri->page);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(cpu_dma_wb_range);

static uint32_t get_l2c_async_err(void)
{
	return readl((void *)(l2c_base + L2C_REG_ASYNC_ERR_OFFSET));
}

static uint32_t get_l2c_err(void)
{
	return readl((void *)(l2c_base + L2C_REG_ERR_OFFSET));
}

static void l2c_print_err(u32 async_err_reg, u32 err_reg)
{
	if (async_err_reg) {
		u32 err_type = err_reg & L2C_ERR_TYPE_MASK;
		bool more_err = err_reg & L2C_ERR_MORERR_MASK;

		if (more_err)
			pr_err_ratelimited("More errors occur due to L2 cache. Below is the first error type\n");

		switch (err_type) {
		case L2C_RAM_ERROR:
			pr_err_ratelimited("L2C RAM error: CCTL operation encounters uncorrectable RAM errors\n");
			break;
		case L2C_RELEASE_ERROR:
			pr_err_ratelimited("L2C release error: D-cache writes back a line that is not in L2-cache\n");
			break;
		case L2C_PROBE_ERROR:
			pr_err_ratelimited("L2C probe error: CCTL operation probes D-cache when D-cache coherency is disabled\n");
			break;
		case L2C_BUS_ERROR:
			pr_err_ratelimited("L2C bus error: CCTL operation or writing back a line to L3 has bus errors\n");
			break;
		default:
			pr_err_ratelimited("L2C unknown error\n");
			break;
		}
	} else {
		pr_err_ratelimited("L2C synchronous error\n");
	}
}

static irqreturn_t l2c_irq(int irq, void *dev_id)
{
	u32 async_err_reg = get_l2c_async_err();
	u32 err_reg = get_l2c_err();

	/* Clear the error status */
	writel(0x0, l2c_base + L2C_REG_ASYNC_ERR_OFFSET);
	writel(0x0, l2c_base + L2C_REG_ERR_OFFSET);

	l2c_print_err(async_err_reg, err_reg);
	return IRQ_HANDLED;
}

int __init l2c_init(void)
{
	struct device_node *node;
	u32 l2c_cfg = 0, irq;
	int error;

	node = of_find_compatible_node(NULL, NULL, "cache");

	l2c_base = of_iomap(node, 0);
	if (l2c_base)
		l2c_cfg = *(u32 *)l2c_base;

	/* default to offset of V0 memory map */
	L2C_REG_PER_CORE_OFFSET = 0x10;
	CCTL_L2_STATUS_PER_CORE_OFFSET = 0x4;
	L2C_REG_STATUS_OFFSET = 0;

	if (l2c_cfg & V5_L2C_CFG_MAP_MASK) {
		L2C_REG_PER_CORE_OFFSET = 0x1000;
		CCTL_L2_STATUS_PER_CORE_OFFSET = 0;
		L2C_REG_STATUS_OFFSET = 0x1000;
	}

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0) {
		pr_err("Failed to get L2C irq number\n");
		return irq;
	}

	error = request_irq(irq, l2c_irq, 0, "L2C", NULL);
	if (error) {
		pr_err("Failed to register L2C irq\n");
		return error;
	}

	return 0;
}
arch_initcall(l2c_init)
