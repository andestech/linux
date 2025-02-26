// SPDX-License-Identifier: GPL-2.0
/*
 * non-coherent cache operations for Andes Platform CPUs.
 *
 * Copyright (C) 2023 Renesas Electronics Corp.
 */

#include <linux/cacheflush.h>
#include <linux/cacheinfo.h>
#include <linux/dma-direction.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>

#include <asm/dma-noncoherent.h>
#include <linux/soc/andes/csr.h>

/* L2 cache registers */
#define ANDES_L2C_REG_CTL_OFFSET		0x8

#define ANDES_L2C_REG_C0_CMD_OFFSET		0x40
#define ANDES_L2C_REG_C0_ACC_OFFSET		0x48
#define ANDES_L2C_REG_C0_STATUS_OFFSET		0x80

/* D-cache operation */
#define ANDES_CCTL_L1D_VA_INVAL			0 /* Invalidate an L1 cache entry */
#define ANDES_CCTL_L1D_VA_WB			1 /* Write-back an L1 cache entry */

/* L2 CCTL status */
#define ANDES_CCTL_L2_STATUS_IDLE		0

/* L2 CCTL status cores mask */
#define ANDES_CCTL_L2_STATUS_C0_MASK		0xf

/* L2 cache operation */
#define ANDES_CCTL_L2_PA_INVAL			0x8 /* Invalidate an L2 cache entry */
#define ANDES_CCTL_L2_PA_WB			0x9 /* Write-back an L2 cache entry */

#define ANDES_L2C_REG_CN_CMD_OFFSET(n)	\
	(ANDES_L2C_REG_C0_CMD_OFFSET + ((n) * ANDES_L2C_REG_PER_CORE_OFFSET))
#define ANDES_L2C_REG_CN_ACC_OFFSET(n)	\
	(ANDES_L2C_REG_C0_ACC_OFFSET + ((n) * ANDES_L2C_REG_PER_CORE_OFFSET))
#define ANDES_CCTL_L2_STATUS_CN_MASK(n)	\
	(ANDES_CCTL_L2_STATUS_C0_MASK << ((n) * ANDES_CCTL_L2_STATUS_PER_CORE_OFFSET))

#define ANDES_CCTL_REG_UCCTLBEGINADDR_NUM	0x80b
#define ANDES_CCTL_REG_UCCTLCOMMAND_NUM		0x80c

static u32 ANDES_L2C_REG_PER_CORE_OFFSET;
static u32 ANDES_CCTL_L2_STATUS_PER_CORE_OFFSET;
static u32 ANDES_L2C_REG_STATUS_OFFSET;

struct andes_priv {
	void __iomem *l2c_base;
	u32 andes_cache_line_size;
};

static struct andes_priv andes_priv;

/* L2 Cache IRQ handler and print error messages */
static uint32_t get_l2c_async_err(void)
{
	return readl((void *)(andes_priv.l2c_base + L2C_REG_ASYNC_ERR_OFFSET));
}

static uint32_t get_l2c_err(void)
{
	return readl((void *)(andes_priv.l2c_base + L2C_REG_ERR_OFFSET));
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
	writel(0x0, andes_priv.l2c_base + L2C_REG_ASYNC_ERR_OFFSET);
	writel(0x0, andes_priv.l2c_base + L2C_REG_ERR_OFFSET);

	l2c_print_err(async_err_reg, err_reg);
	return IRQ_HANDLED;
}

/* L2 Cache operations */
static inline uint32_t andes_cpu_l2c_get_cctl_status(int mhartid)
{
	return readl_relaxed(andes_priv.l2c_base + ANDES_L2C_REG_C0_STATUS_OFFSET +
			     mhartid * ANDES_L2C_REG_STATUS_OFFSET);
}

static void cpu_l2c_cctl(phys_addr_t pa, void __iomem *base,
			 int mhartid, unsigned long ops)
{
#ifdef CONFIG_64BIT
	writeq_relaxed(pa, (base + ANDES_L2C_REG_CN_ACC_OFFSET(mhartid)));
#else
	/*
	 * Considering RV32 potential to use over 4G memory,
	 * the physical address is split into upper and lower 32 bits
	 * and stored in a 64-bits PA-type L2C CCTL access line register.
	 */
	writel_relaxed((pa & 0xFFFFFFFF),
		       (base + ANDES_L2C_REG_CN_ACC_OFFSET(mhartid)));
	writel_relaxed((pa >> 32),
		       (base + ANDES_L2C_REG_CN_ACC_OFFSET(mhartid) + 0x4));
#endif /* !CONFIG_64BIT */

	writel_relaxed(ops, base + ANDES_L2C_REG_CN_CMD_OFFSET(mhartid));
	while ((andes_cpu_l2c_get_cctl_status(mhartid) &
		ANDES_CCTL_L2_STATUS_CN_MASK(mhartid)) !=
		ANDES_CCTL_L2_STATUS_IDLE)
		;
}

static void andes_cpu_cache_operation(unsigned long start, unsigned long end,
				       unsigned int l1_op, unsigned int l2_op)
{
	unsigned long line_size = andes_priv.andes_cache_line_size;
	void __iomem *base = andes_priv.l2c_base;
	unsigned long pa;
	int mhartid = 0;

	if (IS_ENABLED(CONFIG_SMP))
		mhartid = get_cpu();

	if (likely(base)) {
		while (end > start) {
			csr_write(ANDES_CCTL_REG_UCCTLBEGINADDR_NUM, start);
			csr_write(ANDES_CCTL_REG_UCCTLCOMMAND_NUM, l1_op);

			pa = virt_to_phys((void *)start);
			cpu_l2c_cctl(pa, base, mhartid, l2_op);

			start += line_size;
		}
	} else {
		while (end > start) {
			csr_write(ANDES_CCTL_REG_UCCTLBEGINADDR_NUM, start);
			csr_write(ANDES_CCTL_REG_UCCTLCOMMAND_NUM, l1_op);

			start += line_size;
		}
	}

	if (IS_ENABLED(CONFIG_SMP))
		put_cpu();
}

/* Write-back L1 and L2 cache entry */
static inline void andes_cpu_dcache_wb_range(unsigned long start, unsigned long end)
{
	andes_cpu_cache_operation(start, end, ANDES_CCTL_L1D_VA_WB,
				   ANDES_CCTL_L2_PA_WB);
}

/* Invalidate the L1 and L2 cache entry */
static inline void andes_cpu_dcache_inval_range(unsigned long start, unsigned long end)
{
	andes_cpu_cache_operation(start, end, ANDES_CCTL_L1D_VA_INVAL,
				   ANDES_CCTL_L2_PA_INVAL);
}

static void andes_dma_cache_inv(phys_addr_t paddr, size_t size)
{
	unsigned long start = (unsigned long)phys_to_virt(paddr);
	unsigned long end = start + size;
	unsigned long line_size;
	unsigned long flags;

	if (unlikely(start == end))
		return;

	line_size = andes_priv.andes_cache_line_size;
	start = start & (~(line_size - 1));
	end = ((end + line_size - 1) & (~(line_size - 1)));

	local_irq_save(flags);
	andes_cpu_dcache_inval_range(start, end);
	local_irq_restore(flags);
}

static void andes_dma_cache_wback(phys_addr_t paddr, size_t size)
{
	unsigned long start = (unsigned long)phys_to_virt(paddr);
	unsigned long end = start + size;
	unsigned long line_size;
	unsigned long flags;

	line_size = andes_priv.andes_cache_line_size;
	start = start & (~(line_size - 1));
	end = ((end + line_size - 1) & (~(line_size - 1)));

	local_irq_save(flags);
	andes_cpu_dcache_wb_range(start, end);
	local_irq_restore(flags);
}

static void andes_dma_cache_wback_inv(phys_addr_t paddr, size_t size)
{
	andes_dma_cache_wback(paddr, size);
	andes_dma_cache_inv(paddr, size);
}

static int andes_get_l2_line_size(struct device_node *np)
{
	int ret;

	ret = of_property_read_u32(np, "cache-line-size", &andes_priv.andes_cache_line_size);
	if (ret) {
		pr_err("Failed to get cache-line-size.\n");
		return ret;
	}

	return 0;
}

static const struct riscv_nonstd_cache_ops andes_cmo_ops __initconst = {
	.wback = &andes_dma_cache_wback,
	.inv = &andes_dma_cache_inv,
	.wback_inv = &andes_dma_cache_wback_inv,
};

static const struct of_device_id andes_cache_ids[] = {
	{ .compatible = "andestech,ax45mp-cache" },
	{ .compatible = "cache" },
	{ /* sentinel */ }
};

static int __init andes_cache_init(void)
{
	struct device_node *np;
	struct resource res;
	int ret, error;
	u32 irq;
	u32 l2c_cfg;

	/*
	 * Initialize l2c_base and cache_line_size to provide
	 * default settings in the absence of an l2c layout.
	 */
	andes_priv.l2c_base = 0;

	np = of_cpu_device_node_get(0);
	if (!of_device_is_available(np))
		return -ENODEV;

	ret = of_property_read_u32(np, "d-cache-line-size",
				   &andes_priv.andes_cache_line_size);
	if (ret) {
		pr_err("Failed to get d-cache-line-size.\n");
		return ret;
	}

	/*
	 * If there is no IOCP and Zicbom on the Andes CPU,
	 * riscv_cbom_block_size must be 1.
	 */
	if (riscv_cbom_block_size != 1)
		return 0;

	riscv_noncoherent_register_cache_ops(&andes_cmo_ops);

	np = of_find_matching_node(NULL, andes_cache_ids);
	if (!of_device_is_available(np))
		return -ENODEV;

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		return ret;

	andes_priv.l2c_base = ioremap(res.start, resource_size(&res));
	if (!andes_priv.l2c_base)
		return -ENOMEM;
	l2c_cfg = *(u32 *)andes_priv.l2c_base;

	/* Default to offset of V0 memory map */
	ANDES_L2C_REG_PER_CORE_OFFSET = 0x10;
	ANDES_CCTL_L2_STATUS_PER_CORE_OFFSET = 0x4;
	ANDES_L2C_REG_STATUS_OFFSET = 0;

	if (l2c_cfg & L2C_CFG_MAP_MASK) {
		ANDES_L2C_REG_PER_CORE_OFFSET = 0x1000;
		ANDES_CCTL_L2_STATUS_PER_CORE_OFFSET = 0;
		ANDES_L2C_REG_STATUS_OFFSET = 0x1000;
	}

	ret = andes_get_l2_line_size(np);
	if (ret) {
		iounmap(andes_priv.l2c_base);
		return ret;
	}

	/* l2c cache irq */
	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_err("Failed to get L2C irq number\n");
		iounmap(andes_priv.l2c_base);
		return irq;
	}

	/*
	 * The l2c_irq handler references andes_priv.l2c_base.
	 * Therefore, we should register the IRQ handler only
	 * after andes_priv.l2c_base has been set.
	 */
	error = request_irq(irq, l2c_irq, 0, "L2C", NULL);
	if (error) {
		pr_err("Failed to register L2C irq\n");
		iounmap(andes_priv.l2c_base);
		return error;
	}

	return 0;
}
late_initcall(andes_cache_init);
