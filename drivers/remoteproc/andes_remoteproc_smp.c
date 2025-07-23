// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 Andes Technology Corporation
 *
 * Authors:
 *   Locus Wei-Han Chen <locus84@andestech.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/err.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/remoteproc.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/soc/andes/remoteproc.h>
#include <linux/soc/andes/sbi.h>
#include <linux/soc/andes/smu.h>
#include "remoteproc_internal.h"
#include "remoteproc_elf_helpers.h"
#include <asm/dma-noncoherent.h>
#include <asm/sbi.h>
#include <asm/smp.h>

static void __iomem *smu_base;
static void __iomem *swmbox_base = NULL;
static struct remoteproc_work rpc_work;
static struct workqueue_struct *ae350_vq_wq;
struct remoteproc_check rproc_check = {.check = NULL, .info = NULL};

static void andes_rproc_trace(u32 status)
{
	writel(status, swmbox_base + MP_ST_OFF);
}

static void rproc_handle_event(struct work_struct *ptr)
{
	if (rpc_work.rproc) {
		rproc_vq_interrupt(rpc_work.rproc, 0);
		andes_rproc_trace(RPROC_MP_HANDLE_EVENT_DONE);
	}
}

static void check_for_rproc_sp(void *info)
{
	uint32_t reg_value = 0;

	if (swmbox_base) {
		reg_value = readl(swmbox_base + SBI_OFF);
		if (reg_value) {
			writel(0, swmbox_base + SBI_OFF);
			andes_rproc_trace(RPROC_MP_HANDLE_EVENT);
			queue_work(ae350_vq_wq, &rpc_work.work);
		}
	}
}

static void sbi_plicsw_rproc_enable(unsigned int enable)
{
	sbi_ecall(ANDES_SBI_EXT_ANDES, SBI_EXT_ANDES_RPROC_EN,
		  enable, 0, 0, 0, 0, 0);
}

static void sbi_plicsw_rproc_send_ipi(uint8_t target_hart)
{
	sbi_ecall(ANDES_SBI_EXT_ANDES, SBI_EXT_ANDES_RPROC_SEND_IPI,
		  target_hart, 0, 0, 0, 0, 0);
}

static void andes_rproc_kick(struct rproc *rproc, int vqid)
{
	struct device *dev = rproc->dev.parent;
	struct andes_rproc_pdata *local = rproc->priv;

	dev_dbg(dev, "KICK Firmware to start send messages vqid %d\n", vqid);

	andes_rproc_trace(RPROC_MP_SEND_IPI);
	sbi_plicsw_rproc_send_ipi(local->sp_hartid);
}

static int andes_rproc_start(struct rproc *rproc)
{
	struct andes_rproc_pdata *local = rproc->priv;
	smp_call_func_t cache_op;
	int i;

	andes_rproc_trace(RPROC_MP_START);
	cache_op = (smp_call_func_t)noncoherent_cache_ops.wback_inv_all;

	/* MP set ELF entry point to the SP reset vector. */
	writel(SP_BASE_LO, smu_base + SMU_HART_RESET_VEC_LO(local->sp_hartid));
	if (SP_BASE_HI) {
		writel(SP_BASE_HI,
		       smu_base + SMU_HART_RESET_VEC_HI(local->sp_hartid));
	}

	for_each_online_cpu(i) {
		if (i != smp_processor_id())
			smp_call_function_single(i, cache_op, NULL, true);
	}
	noncoherent_cache_ops.wback_inv_all();
	writel(PCS_RESET, smu_base + PCSm_CTL_OFF(local->sp_hartid));
	writel(MBOX_SET_MSG, local->mbox_msg + MBOX_OFF);

	return 0;
}

static int andes_rproc_stop(struct rproc *rproc)
{
	struct andes_rproc_pdata *local = rproc->priv;
	struct sbiret ret;

	andes_rproc_trace(RPROC_MP_STOP);

	writel(MBOX_SET_MSG, local->mbox_msg + MBOX_OFF);
	ret = sbi_ecall(ANDES_SBI_EXT_ANDES, SBI_EXT_ANDES_RPROC_GET_INIT_FUNC,
			0, 0, 0, 0, 0, 0);
	writel(ret.value, smu_base + SMU_HART_RESET_VEC_LO(local->sp_hartid));
	andes_rproc_kick(rproc, 1);
	writel(PCS_RESET, smu_base + PCSm_CTL_OFF(local->sp_hartid));

	return 0;
}

static int andes_reserved_mem_alloc(struct rproc *rproc,
				    struct rproc_mem_entry *mem)
{
	void *va;
	struct andes_rproc_pdata *local = rproc->priv;

	if ((mem->dma >= local->rmem_base) &&
		((mem->dma + mem->len) < (local->rmem_base + local->rmem_size))) {
		va = (local->revdmem_base + mem->dma - local->rmem_base);
	}
	mem->va = va;

	return 0;
}

static int andes_parse_reserved_mems(struct rproc *rproc)
{
	int i, num_mems;
	int index = 0;
	struct device *dev = rproc->dev.parent;
	struct device_node *mem_nodes = dev->of_node;
	struct andes_rproc_pdata *local = rproc->priv;
	struct rproc_mem_entry *mem;

	num_mems = of_count_phandle_with_args(mem_nodes,
					      "memory-region", NULL);
	if (num_mems <= 0) {
		dev_err(dev, "need reserved-memory to shared memory\n");
		return -EINVAL;
	}

	dev_dbg(dev, "%s number of mems: %d \n", __func__, num_mems);
	andes_rproc_trace(RPROC_MP_REVD_MEM);

	for (i = 0; i < num_mems; i++) {
		struct device_node *dt_node;
		struct reserved_mem *rmem;

		dt_node = of_parse_phandle(mem_nodes, "memory-region", i);
		rmem = of_reserved_mem_lookup(dt_node);
		if (!rmem) {
			dev_err(dev, "unable to acquire memory-region\n");
			return -EINVAL;
		}

		mem = rproc_of_resm_mem_entry_init(dev, index, rmem->size,
						   rmem->base, dt_node->name);
		mem->dma = (dma_addr_t)rmem->base;
		if (!mem) {
			dev_err(dev,
				"unable to initialize memory-region %s \n",
				dt_node->name);
			return -ENOMEM;
		}
		andes_reserved_mem_alloc(rproc, mem);
		rmem->priv = mem;
		rproc_add_carveout(rproc, mem);
		index++;
	}

	memset(local->revdmem_base, 0, local->rmem_size);

	return 0;
}

static int andes_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	int ret;

	andes_rproc_trace(RPROC_MP_PAESE_FW);

	ret = andes_parse_reserved_mems(rproc);
	if (ret)
		return ret;

	ret = rproc_elf_load_rsc_table(rproc, fw);
	if (ret == -EINVAL) {
		dev_info(&rproc->dev, "Resource table not found.\n");
		ret = 0;
	}

	return ret;
}

static int andes_rproc_elf_load_segments(struct rproc *rproc, const struct firmware *fw)
{
	struct device *dev = &rproc->dev;
	const void *ehdr, *phdr;
	int i, ret = 0;
	u16 phnum;
	const u8 *elf_data = fw->data;
	u8 class = fw_elf_get_class(fw);
	u32 elf_phdr_get_size = elf_size_of_phdr(class);

	ehdr = elf_data;
	phnum = elf_hdr_get_e_phnum(class, ehdr);
	phdr = elf_data + elf_hdr_get_e_phoff(class, ehdr);

	andes_rproc_trace(RPROC_MP_LOAD_ELF);

	/* go through the available ELF segments */
	for (i = 0; i < phnum; i++, phdr += elf_phdr_get_size) {
		u64 da = elf_phdr_get_p_vaddr(class, phdr);
		u64 memsz = elf_phdr_get_p_memsz(class, phdr);
		u64 filesz = elf_phdr_get_p_filesz(class, phdr);
		u64 offset = elf_phdr_get_p_offset(class, phdr);
		u32 type = elf_phdr_get_p_type(class, phdr);
		void *ptr;

		if (type != PT_LOAD)
			continue;

		dev_dbg(dev, "phdr: type %d da 0x%llx memsz 0x%llx filesz 0x%llx\n",
			type, da, memsz, filesz);

		if (filesz > memsz) {
			dev_err(dev, "bad phdr filesz 0x%llx memsz 0x%llx\n",
				filesz, memsz);
			ret = -EINVAL;
			break;
		}

		if (offset + filesz > fw->size) {
			dev_err(dev, "truncated fw: need 0x%llx avail 0x%zx\n",
				offset + filesz, fw->size);
			ret = -EINVAL;
			break;
		}

		if (!rproc_u64_fit_in_size_t(memsz)) {
			dev_err(dev, "size (%llx) does not fit in size_t type\n",
				memsz);
			ret = -EOVERFLOW;
			break;
		}
		/* grab the kernel address for this device address */
		ptr = rproc_da_to_va(rproc, da, memsz, NULL);
		if (!ptr) {
			dev_err(dev, "bad phdr da 0x%llx mem 0x%llx\n", da,
				memsz);
			ret = -EINVAL;
			break;
		}

		/* put the segment where the remote processor expects it */
		if (filesz)
			memcpy(ptr, elf_data + offset, filesz);

		/*
		 * Zero out remaining memory for this segment.
		 *
		 * This isn't strictly required since dma_alloc_coherent already
		 * did this for us. albeit harmless, we may consider removing
		 * this.
		 */
		if (memsz > filesz)
			memset(ptr + filesz, 0, memsz - filesz);
	}

	return ret;
}

static int smp_add_rproc_check(void *ptr, void *info)
{
	if (!rproc_check.check) {
		rproc_check.check = ptr;
		rproc_check.info = info;
		return 0;
	}
	return -EINVAL;
}

static int smp_del_rproc_check(void)
{
	if (rproc_check.check != NULL) {
		rproc_check.check = NULL;
		rproc_check.info = NULL;
		return 0;
	}
	return -EINVAL;
}

static struct rproc_ops andes_rproc_ops = {
	.start		       = andes_rproc_start,
	.stop		       = andes_rproc_stop,
	.kick		       = andes_rproc_kick,
	.parse_fw	       = andes_parse_fw,
	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
	.load                  = andes_rproc_elf_load_segments,
	.sanity_check          = rproc_elf_sanity_check,
	.get_boot_addr         = rproc_elf_get_boot_addr,
};

static int andes_remoteproc_smp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev->of_node;
	struct rproc *rproc;
	struct andes_rproc_pdata *local;
	int ret = 0;

	rproc = rproc_alloc(dev, dev_node->name, &andes_rproc_ops,
			    NULL, sizeof(*local));
	if (!rproc) {
		dev_err(&pdev->dev, "rproc allocation failed\n");
		return -ENOMEM;
	}

	smu_base = atcsmu_get_address();
	if (!smu_base) {
		dev_err(&pdev->dev, "smu_base is NULL or 0x0\n");
		goto error;
	}

	rproc->auto_boot = false;
	local = rproc->priv;
	local->rproc = rproc;
	local->dev = dev;

	platform_set_drvdata(pdev, rproc);

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		goto error;
	}

	ret = rproc_add(local->rproc);
	if (ret) {
		dev_err(&pdev->dev, "rproc registration failed\n");
		goto error;
	}

	/* plicsw base address and length */

	of_property_read_u64(dev_node, "rmem_base", &local->rmem_base);
	of_property_read_u64(dev_node, "rmem_size", &local->rmem_size);
	of_property_read_u32(dev_node, "sp_hartid", &local->sp_hartid);

	dev_info(&pdev->dev, "rmem_base:%llx rmem_size:%llx\n",
		 local->rmem_base, local->rmem_size);

	local->revdmem_base = memremap(local->rmem_base, local->rmem_size, MEMREMAP_WB);
	if (!local->revdmem_base)
		goto error;

	if (MBOX_MSG >= local->rmem_base) {
		local->mbox_msg = local->revdmem_base + MBOX_MSG - local->rmem_base;
	}

	swmbox_base = local->mbox_msg;
	memset(swmbox_base, 0x0, sizeof(struct swmsg_box));

	andes_rproc_trace(RPROC_MP_SBI_ENABLE);
	sbi_plicsw_rproc_enable(REMOTEPROC_EN);

	if (smp_add_rproc_check(&check_for_rproc_sp, rproc))
		goto error;

	INIT_WORK(&rpc_work.work, rproc_handle_event);
	rpc_work.rproc = rproc;

	ae350_vq_wq = alloc_workqueue("ae350_vq_wq", WQ_HIGHPRI, 0);
	if (!ae350_vq_wq) {
		dev_err(&pdev->dev, "ae350_vq_wq init failed\n");
		goto error;
	}

	return 0;

error:
	if (local->revdmem_base)
		iounmap(local->revdmem_base);

	local->mbox_msg = NULL;
	swmbox_base = NULL;
	rproc_free(rproc);
	sbi_plicsw_rproc_enable(REMOTEPROC_DIS);
	return ret;
}

static int andes_remoteproc_smp_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct andes_rproc_pdata *local = rproc->priv;

	dev_info(&pdev->dev, "%s\n", __func__);
	andes_rproc_trace(RPROC_MP_REMOVE);

	destroy_workqueue(ae350_vq_wq);

	if (atomic_read(&rproc->power) > 0)
		rproc_shutdown(rproc);

	if (local->revdmem_base)
		iounmap(local->revdmem_base);

	local->mbox_msg = NULL;
	swmbox_base = NULL;

	of_reserved_mem_device_release(&pdev->dev);
	rproc_del(rproc);
	rproc_free(rproc);

	sbi_plicsw_rproc_enable(REMOTEPROC_DIS);
	smp_del_rproc_check();

	return 0;
}

/* Match table for OF platform binding */
static const struct of_device_id andes_remoteproc_smp_match[] = {
	{ .compatible = "andestech,andes_remoteproc_smp", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, andes_remoteproc_smp_match);

static struct platform_driver andes_remoteproc_smp_driver = {
	.probe = andes_remoteproc_smp_probe,
	.remove = andes_remoteproc_smp_remove,
	.driver = {
		.name = "andes_remoteproc_smp",
		.of_match_table = andes_remoteproc_smp_match,
	},
};
module_platform_driver(andes_remoteproc_smp_driver);

static struct dma_coherent_mem *andes_dma_init(phys_addr_t phys_addr,
					       dma_addr_t device_addr,
					       size_t size,
					       bool use_dma_pfn_offset)
{
	struct dma_coherent_mem *dma_mem;
	int pages = size >> PAGE_SHIFT;

	if (!size)
		return ERR_PTR(-EINVAL);

	dma_mem = kzalloc(sizeof(struct dma_coherent_mem), GFP_KERNEL);
	if (!dma_mem)
		goto out_unmap_membase;
	dma_mem->bitmap = bitmap_zalloc(pages, GFP_KERNEL);
	if (!dma_mem->bitmap)
		goto out_free_dma_mem;

	dma_mem->device_base = device_addr;
	dma_mem->pfn_base = PFN_DOWN(phys_addr);
	dma_mem->size = pages;
	dma_mem->use_dev_dma_pfn_offset = use_dma_pfn_offset;
	spin_lock_init(&dma_mem->spinlock);

	return dma_mem;

out_free_dma_mem:
	kfree(dma_mem);
out_unmap_membase:
	pr_err("Reserved memory: failed to init DMA memory pool at %pa, size %zd MiB\n",
		&phys_addr, size / SZ_1M);
	return ERR_PTR(-ENOMEM);
}

static int andes_dma_assign(struct device *dev,
			    struct dma_coherent_mem *mem)
{
	if (!dev)
		return -ENODEV;

	if (dev->dma_mem)
		return -EBUSY;

	dev->dma_mem = mem;
	return 0;
}

static int andes_device_init(struct reserved_mem *rmem, struct device *dev)
{
	struct rproc_mem_entry *rproc_mem = rmem->priv;
	static struct dma_coherent_mem *mem = NULL;

	if (!mem) {
		mem = andes_dma_init(rmem->base, rmem->base,
				     rmem->size, false);
		if (IS_ERR(mem))
			return PTR_ERR(mem);
		mem->virt_base = rproc_mem->va;
	}
	rmem->priv = mem;

	/* Warn if the device potentially can't use the reserved memory */
	if (mem->device_base + rmem->size - 1 >
	    min_not_zero(dev->coherent_dma_mask, dev->bus_dma_limit))
		dev_warn(dev, "reserved memory is beyond device's set DMA address range\n");

	andes_dma_assign(dev, mem);
	return 0;
}

static void andes_device_release(struct reserved_mem *rmem,
				 struct device *dev)
{
	if (dev)
		dev->dma_mem = NULL;
}

static const struct reserved_mem_ops andes_vdev_buffer_ops = {
	.device_init = andes_device_init,
	.device_release = andes_device_release,
};

static int andes_vdev_buffer_init(struct reserved_mem *rmem)
{
	rmem->ops = &andes_vdev_buffer_ops;

	return 0;
}

RESERVEDMEM_OF_DECLARE(andes_vdev_buffer, "andes_vdev_buffer",
		       andes_vdev_buffer_init);

MODULE_DESCRIPTION("Andes remote processor control driver on SMP system");
MODULE_LICENSE("GPL v2");
