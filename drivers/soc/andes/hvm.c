// SPDX-License-Identifier: GPL
/*
 * DMA device driver to provide DMA functionality for user space applications.
 *
 * Copyright (C) 2024 Andes Technology Corporation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-direction.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/mm_types.h>
#include <linux/pgtable.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/page.h>

#define DEVICE_NAME			"dma_dev"
#define DMA_DEV_IOC_MAGIC		'd'
#define DMAC_DEV_IOC_DMA_TRANS		_IO(DMA_DEV_IOC_MAGIC, 0)
#define DMAC_DEV_IOC_MEM_REL		_IO(DMA_DEV_IOC_MAGIC, 1)
#define ANDES_DMA_IOC_CHAN_REQUEST	_IO(DMA_DEV_IOC_MAGIC, 2)
#define ANDES_DMA_IOC_CHAN_RELEASE	_IO(DMA_DEV_IOC_MAGIC, 3)

#define DMA_DEV_TIMEOUT_MS		6000
#define DMA_DEV_MAGIC			0xdead4ead

struct dma_dev_trans_para {
	dma_addr_t		dst_phy_addr;
	dma_addr_t		src_phy_addr;
	unsigned long		dst_vir_addr;
	unsigned long		src_vir_addr;
	unsigned int		data_size;
	unsigned int		dma_job_id;
	unsigned char		src_need_cache_op;
	unsigned char		dst_need_cache_op;
	int			chan_id;
};

struct dma_dev_para {
	struct dma_chan		*dma_chan;
	struct list_head	dma_chan_node;
	wait_queue_head_t	dma_wait;
};

struct dma_dev_tx_para {
	struct dma_dev_para	*dma_dev;
	enum dma_status		status;
	dma_cookie_t		cookie;
	unsigned int		magic;
	int			done;
};

struct mem_info {
	struct list_head	mem_list;
	dma_addr_t		phy_addr;
	unsigned long		vir_addr;
	unsigned int		size;
};

struct dma_info {
	struct list_head	dma_list;
	dma_addr_t		src_phy_addr;
	dma_addr_t		dst_phy_addr;
	unsigned int		size;
};

struct file_handle_info {
	struct list_head	file_list;
	struct list_head	mem_alloc;
	struct list_head	dma_chan_list;
	struct dma_dev_para	dma;
	spinlock_t		file_lock;
};

struct driver_info {
	struct list_head	file;
	struct list_head	hvm_mem;
	struct device		*dev;
	spinlock_t		drv_lock;
};

static struct driver_info *drv_info;

static void dma_dev_callback(void *arg)
{
	wait_queue_head_t *dma_wait;
	struct dma_dev_tx_para *tx = (struct dma_dev_tx_para *)arg;

	if (tx->magic == DMA_DEV_MAGIC) {
		dma_wait = &tx->dma_dev->dma_wait;
		tx->done = 1;
		wake_up(dma_wait);
	} else {
		dev_err(drv_info->dev, "A invalid callback found\n");
	}
}

static unsigned long dma_dev_va_to_pa(unsigned long vaddr)
{
	struct page *page = NULL;
	unsigned long paddr;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset(current->mm, vaddr);
	if (!pgd_present(*pgd))
		return 0;

	p4d = p4d_offset(pgd, vaddr);
	if (!p4d_present(*p4d))
		return 0;

	pud = pud_offset(p4d, vaddr);
	if (!pud_present(*pud))
		return 0;

	pmd = pmd_offset(pud, vaddr);
	if (!pmd_present(*pmd))
		return 0;

	pte = pte_offset_kernel(pmd, vaddr);
	if (!pte_present(*pte))
		return 0;

	page = pte_page(*pte);
	paddr = page_to_phys(page) | (vaddr & ~PAGE_MASK);
	return paddr;
}

static long dma_dev_trans(struct dma_dev_trans_para *trans,
			  struct file_handle_info *file_handle)
{
	struct dma_async_tx_descriptor *tx_desc;
	struct dma_dev_tx_para tx;
	struct dma_dev_para *para_next;
	long ret_wait;
	long ret = -EINVAL;

	spin_lock(&file_handle->file_lock);
	list_for_each_entry_safe(tx.dma_dev,
				 para_next,
				 &file_handle->dma_chan_list,
				 dma_chan_node) {
		if (tx.dma_dev->dma_chan->chan_id == trans->chan_id) {
			ret = 0;
			break;
		}
	}
	spin_unlock(&file_handle->file_lock);

	if (ret != 0) {
		dev_err(drv_info->dev, "DMA channel %d not found\n", trans->chan_id);
		goto ERR_EXIT;
	}

	tx_desc = dmaengine_prep_dma_memcpy(tx.dma_dev->dma_chan,
					    trans->dst_phy_addr,
					    trans->src_phy_addr,
					    trans->data_size,
					    DMA_CTRL_ACK
					    | DMA_PREP_INTERRUPT
					    | DMA_PREP_FENCE);
	if (!tx_desc) {
		dev_err(drv_info->dev, "device_prep_dma_memcpy error\n");
		ret = -EIO;
		goto ERR_EXIT;
	}

	tx_desc->callback = dma_dev_callback;
	tx_desc->callback_param = &tx;

	tx.magic = DMA_DEV_MAGIC;
	tx.done = 0;
	tx.cookie = tx_desc->tx_submit(tx_desc);
	if (dma_submit_error(tx.cookie)) {
		dev_err(drv_info->dev, "submit error\n");
		ret = -EIO;
		goto ERR_EXIT;
	}

	dma_async_issue_pending(tx.dma_dev->dma_chan);
	ret_wait = wait_event_timeout(tx.dma_dev->dma_wait, tx.done != 0,
				      msecs_to_jiffies(DMA_DEV_TIMEOUT_MS));

	tx.status = dma_async_is_tx_complete(tx.dma_dev->dma_chan,
					     tx.cookie,
					     NULL,
					     NULL);

	if (tx.done == 0) {
		dev_err(drv_info->dev, "DMA timeout dst_addr:0x%lx dst_addr:0x%lx len:0x%x wait:%ld status:%d chan:%d\n",
			(unsigned long)trans->dst_phy_addr,
			(unsigned long)trans->src_phy_addr,
			trans->data_size,
			ret_wait,
			tx.status,
			tx.dma_dev->dma_chan->chan_id);

		dmaengine_terminate_sync(tx.dma_dev->dma_chan);
		ret = -ETIMEDOUT;
		goto ERR_EXIT;
	}

	if (tx.status != DMA_COMPLETE) {
		dev_err(drv_info->dev, "DMA error dst_addr:0x%lx dst_addr:0x%lx len:0x%x wait:%ld status:%d chan:%d\n",
			(unsigned long)trans->dst_phy_addr,
			(unsigned long)trans->src_phy_addr,
			trans->data_size,
			ret_wait,
			tx.status,
			tx.dma_dev->dma_chan->chan_id);

		dmaengine_terminate_sync(tx.dma_dev->dma_chan);
		ret = -EINVAL;
		goto ERR_EXIT;
	}

ERR_EXIT:
	return ret;
}

static long dma_dev_rel_mem(unsigned long user_va,
			    struct file_handle_info *file_handle)
{
	struct mem_info *mem_info_next;
	struct mem_info *mem_info;
	unsigned long mem_pa;

	if(list_empty(&file_handle->mem_alloc))
		return 0;

	if (user_va != 0)
		mem_pa = dma_dev_va_to_pa(user_va);

	spin_lock(&file_handle->file_lock);
	list_for_each_entry_safe(mem_info, mem_info_next,
				 &file_handle->mem_alloc,
				 mem_list) {
		if (mem_info->phy_addr == mem_pa || user_va == 0) {
			list_del(&mem_info->mem_list);
			kfree((void *)mem_info->vir_addr);
			kfree(mem_info);
		}
	}
	spin_unlock(&file_handle->file_lock);

	return 0;
}

static int dma_dev_check_dma_buf(dma_addr_t pa, unsigned int size)
{
	struct file_handle_info *file_info;
	struct file_handle_info *file_info_next;
	struct mem_info *mem_info;
	struct mem_info *mem_info_next;
	int valid = -EINVAL;

	list_for_each_entry_safe(mem_info,
				 mem_info_next,
				 &drv_info->hvm_mem,
				 mem_list) {
		if (pa >= mem_info->phy_addr &&
		    (pa + size) <= (mem_info->phy_addr + mem_info->size)) {
			return 0;
		}
	}

	spin_lock(&drv_info->drv_lock);
	list_for_each_entry_safe(file_info,
				 file_info_next,
				 &drv_info->file,
				 file_list) {
		spin_lock(&file_info->file_lock);
		list_for_each_entry_safe(mem_info,
					 mem_info_next,
					 &file_info->mem_alloc,
					 mem_list) {
			if (pa >= mem_info->phy_addr &&
			    (pa + size) <= (mem_info->phy_addr + mem_info->size)) {
				spin_unlock(&file_info->file_lock);
				spin_unlock(&drv_info->drv_lock);
				return 0;
			}
		}
		spin_unlock(&file_info->file_lock);
	}
	spin_unlock(&drv_info->drv_lock);

	return valid;
}

static int dma_dev_get_dma_pa(unsigned long va,
			      dma_addr_t *ret_pa,
			      unsigned int size,
			      unsigned char need_cache_op,
			      enum dma_data_direction dir)
{
	dma_addr_t mem_pa;
	int ret = 0;

	if (*ret_pa == 0) {
		mem_pa = dma_dev_va_to_pa(va);
		if (mem_pa == 0) {
			dev_err(drv_info->dev, "Invalid va:0x%lx\n", va);
			ret = -EINVAL;
			goto mem_addr_err;
		}
	} else {
		mem_pa = *ret_pa;
	}

	ret = dma_dev_check_dma_buf(mem_pa, size);
	if (ret != 0) {
		dev_err(drv_info->dev, "Invalid dma buffer va:%p pa:0x%lx\n",
			(void *)va,
			(unsigned long)mem_pa);
		goto mem_addr_err;
	}

	if (need_cache_op) {
		*ret_pa = dma_map_single(drv_info->dev,
					 (void *)phys_to_virt(mem_pa),
					 size,
					 dir);
		ret = dma_mapping_error(drv_info->dev, *ret_pa);
		if (ret) {
			dev_err(drv_info->dev, "dma_map_single failed ret:%d pa:0x%lx va:0x%lx\n",
				ret, (unsigned long)mem_pa, va);
			goto mem_addr_err;
		}
	} else {
		*ret_pa = mem_pa;
	}

mem_addr_err:
	return ret;
}

static void dma_dev_release_buf(bool rel_src, bool rel_dst, struct dma_dev_trans_para *trans)
{
	if (rel_src && trans->src_need_cache_op) {
		dma_unmap_single(drv_info->dev,
				 trans->src_phy_addr,
				 trans->data_size,
				 DMA_TO_DEVICE);
	}

	if (rel_dst && trans->dst_need_cache_op) {
		dma_unmap_single(drv_info->dev,
				 trans->dst_phy_addr,
				 trans->data_size,
				 DMA_FROM_DEVICE);
	}
}

static int dma_dev_prep_buf(struct dma_dev_trans_para *trans)
{
	int ret = 0;

	ret = dma_dev_get_dma_pa(trans->src_vir_addr,
				 &trans->src_phy_addr,
				 trans->data_size,
				 trans->src_need_cache_op,
				 DMA_TO_DEVICE);
	if (ret != 0) {
		dev_err(drv_info->dev, "Invalid src buffer addr %p\n",
			(void *)trans->src_vir_addr);
		return ret;
	}

	ret = dma_dev_get_dma_pa(trans->dst_vir_addr,
				 &trans->dst_phy_addr,
				 trans->data_size,
				 trans->dst_need_cache_op,
				 DMA_FROM_DEVICE);
	if (ret != 0) {
		dev_err(drv_info->dev, "Invalid dst buffer addr %p\n",
			(void *)trans->dst_vir_addr);
		dma_dev_release_buf(1, 0, trans);
		return ret;
	}

	return ret;
}

static int dma_dev_get_hvm_buf(void)
{
	struct mem_info *hvm_mem;
	struct device_node *hvm_np;
	struct resource res;
	int i;

	hvm_np = of_find_compatible_node(NULL, NULL, "andestech,hvm");
	if (!hvm_np) {
		dev_dbg(drv_info->dev, "No HVM node found in the DTB\n");
		return 0;
	}

	i = 0;
	while (of_address_to_resource(hvm_np, i, &res) == 0) {
		hvm_mem = devm_kzalloc(drv_info->dev, sizeof(struct mem_info), GFP_KERNEL);
		if (!hvm_mem) {
			of_node_put(hvm_np);
			return -ENOMEM;
		}
		hvm_mem->phy_addr = res.start;
		hvm_mem->vir_addr = res.start;
		hvm_mem->size = resource_size(&res);
		INIT_LIST_HEAD(&hvm_mem->mem_list);
		list_add_tail(&hvm_mem->mem_list, &drv_info->hvm_mem);
		dev_dbg(drv_info->dev, "HVM addr:0x%lx size:0x%x\n",
			(unsigned long)hvm_mem->phy_addr, hvm_mem->size);
		i++;
	}

	if (i == 0)
		dev_dbg(drv_info->dev, "No HVM resource\n");

	of_node_put(hvm_np);
	return 0;
}

static int dma_dev_channel_request(struct file_handle_info *file_handle)
{
	struct dma_dev_para *para;
	dma_cap_mask_t mask;

	para = kmalloc(sizeof(*para), GFP_KERNEL);

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);
	para->dma_chan = dma_request_channel(mask, NULL, NULL);
	if (!para->dma_chan) {
		dev_err(drv_info->dev, "Failed to request a DMA channel\n");
		return -ENODEV;
	}

	init_waitqueue_head(&para->dma_wait);
	spin_lock(&file_handle->file_lock);
	list_add_tail(&para->dma_chan_node, &file_handle->dma_chan_list);
	spin_unlock(&file_handle->file_lock);

	return para->dma_chan->chan_id;
}

static void dma_dev_channel_release(struct file_handle_info *file_handle,
				    int chan_id)
{
	struct dma_dev_para *para;
	struct dma_dev_para *para_next;
	bool release = false;

	if (list_empty(&file_handle->dma_chan_list))
		return;

	spin_lock(&file_handle->file_lock);
	list_for_each_entry_safe(para, para_next,
				 &file_handle->dma_chan_list,
				 dma_chan_node) {
		if (para->dma_chan->chan_id == chan_id) {
			list_del(&para->dma_chan_node);
			release = true;
			break;
		}
	}
	spin_unlock(&file_handle->file_lock);

	if (release == true) {
		dmaengine_terminate_sync(para->dma_chan);
		dma_release_channel(para->dma_chan);
		kfree(para);
		dev_dbg(drv_info->dev, "DMA channel %d releasd\n", chan_id);
	} else {
		dev_err(drv_info->dev, "DMA channel %d not found\n", chan_id);
	}
}

static long dma_dev_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct dma_dev_trans_para trans_para;
	struct file_handle_info *file_handle;
	unsigned long mem_va;
	long ret = 0;
	int chan_id;

	file_handle = file->private_data;

	switch (cmd) {
	case DMAC_DEV_IOC_DMA_TRANS:
		if (copy_from_user(&trans_para,
				   (void __user *)arg,
				   sizeof(trans_para)))
			return -EFAULT;

		ret = dma_dev_prep_buf(&trans_para);
		if (ret)
			return ret;

		ret = dma_dev_trans(&trans_para, file_handle);

		dma_dev_release_buf(1, 1, &trans_para);
		break;

	case DMAC_DEV_IOC_MEM_REL:
		if (copy_from_user(&mem_va, (void __user *)arg,
				   sizeof(unsigned long)))
			return -EFAULT;
		ret = dma_dev_rel_mem(mem_va, file_handle);
		break;
	case ANDES_DMA_IOC_CHAN_REQUEST:
		chan_id = dma_dev_channel_request(file_handle);
		ret = chan_id;
		break;
	case ANDES_DMA_IOC_CHAN_RELEASE:
		if (copy_from_user(&chan_id, (void __user *)arg,
				   sizeof(chan_id)))
			return -EFAULT;
		dma_dev_channel_release(file_handle, chan_id);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

/*
 * The DMA device driver doesn't include a real device and is constructed
 * on the DMA engine driver, so the value dma_coherent should refer to
 * the DMA controller driver.
 */
static int dma_dev_set_coherent(struct device *dev)
{
	struct device_node *dmac_np;
	struct platform_device *pdev;

	dmac_np = of_find_node_by_name(NULL, "dma");
	if (!dmac_np) {
		dev_err(drv_info->dev, "No DMAC node found, so DMA device driver initialisation failed.\n");
		return -ENODEV;
	}

	pdev = of_find_device_by_node(dmac_np);
	if (!pdev) {
		dev_err(drv_info->dev, "Invalid DMAC platform device\n");
		return -ENODEV;
	}

	dev->dma_coherent = pdev->dev.dma_coherent;

	return 0;
}

static int dma_dev_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct file_handle_info *file_handle;
	struct mem_info *mem_info;
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
	int ret = 0;
	unsigned int size;

	file_handle = file->private_data;
	size = vma->vm_end - vma->vm_start;
	mem_info = kmalloc(sizeof(struct mem_info), GFP_KERNEL);
	if (mem_info == NULL) {
		return -ENOMEM;
	}

	mem_info->vir_addr = (unsigned long)kzalloc(size, GFP_KERNEL);
	if (mem_info->vir_addr == 0) {
		ret = -ENOMEM;
		goto mem_alloc_err;
	}

	mem_info->phy_addr = virt_to_phys((void *)mem_info->vir_addr);
	mem_info->size = size;

	spin_lock(&file_handle->file_lock);
	list_add_tail(&mem_info->mem_list, &file_handle->mem_alloc);
	spin_unlock(&file_handle->file_lock);

	off += mem_info->phy_addr;
	vma->vm_pgoff = off >> PAGE_SHIFT;
	ret = remap_pfn_range(vma,
			      vma->vm_start,
			      vma->vm_pgoff,
			      size,
			      vma->vm_page_prot);

	return ret;

mem_alloc_err:
	kfree(mem_info);
	return ret;
}

static int dma_dev_open(struct inode *inode, struct file *filp)
{
	struct file_handle_info *file_handle;

	file_handle = kzalloc(sizeof(struct file_handle_info), GFP_KERNEL);
	if (!file_handle)
		return -ENOMEM;

	INIT_LIST_HEAD(&file_handle->file_list);
	INIT_LIST_HEAD(&file_handle->mem_alloc);
	INIT_LIST_HEAD(&file_handle->dma_chan_list);
	spin_lock_init(&file_handle->file_lock);

	spin_lock(&drv_info->drv_lock);
	list_add_tail(&file_handle->file_list, &drv_info->file);
	spin_unlock(&drv_info->drv_lock);

	filp->private_data = file_handle;

	return 0;
}

static int dma_dev_release(struct inode *inode, struct file *file)
{
	struct file_handle_info *file_handle;
	int ret;

	file_handle = file->private_data;

	spin_lock(&drv_info->drv_lock);
	list_del(&file_handle->file_list);
	spin_unlock(&drv_info->drv_lock);

	ret = dma_dev_rel_mem(0, file_handle);

	kfree(file_handle);
	return ret;
}

static const struct file_operations dma_dev_fops = {
	.owner = THIS_MODULE,
	.open = dma_dev_open,
	.release = dma_dev_release,
	.unlocked_ioctl = dma_dev_ioctl,
	.mmap = dma_dev_mmap
};

static struct miscdevice dma_dev_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &dma_dev_fops
};

static int dma_dev_init(void)
{
	int ret;

	ret = misc_register(&dma_dev_misc);
	if (ret) {
		pr_err("dma_dev: misc_register failed");
		return ret;
	}

	drv_info = devm_kzalloc(dma_dev_misc.this_device,
				sizeof(struct driver_info),
				GFP_KERNEL);
	if (!drv_info) {
		ret = -ENOMEM;
		goto misc_deregister;
	}

	drv_info->dev = dma_dev_misc.this_device;
	dma_dev_misc.this_device->dma_mask = devm_kzalloc(drv_info->dev,
							  sizeof(u64),
							  GFP_KERNEL);
	if (!dma_dev_misc.this_device->dma_mask) {
		ret = -ENOMEM;
		goto misc_deregister;
	}

	ret = dma_dev_set_coherent(drv_info->dev);
	if (ret)
		goto misc_deregister;

	if (!dma_set_mask_and_coherent(drv_info->dev, DMA_BIT_MASK(64))) {
		dev_dbg(drv_info->dev, "DMA to 64-bit address\n");
	} else {
		ret = dma_set_mask_and_coherent(drv_info->dev,
						DMA_BIT_MASK(32));
		if (ret) {
			dev_err(drv_info->dev, "DMA configuration failed\n");
			goto misc_deregister;
		}
	}

	INIT_LIST_HEAD(&drv_info->file);
	INIT_LIST_HEAD(&drv_info->hvm_mem);
	spin_lock_init(&drv_info->drv_lock);

	ret = dma_dev_get_hvm_buf();
	if (ret)
		goto misc_deregister;

	return ret;

misc_deregister:
	misc_deregister(&dma_dev_misc);
	return ret;
}

static void __exit dma_dev_exit(void)
{
	struct file_handle_info *file_info, *file_info_next;

	spin_lock(&drv_info->drv_lock);
	list_for_each_entry_safe(file_info, file_info_next,
				 &drv_info->file, file_list) {
		dma_dev_rel_mem(0, file_info);
		kfree(file_info);
	}
	spin_unlock(&drv_info->drv_lock);

	misc_deregister(&dma_dev_misc);
	kfree(drv_info);
}

module_init(dma_dev_init);
module_exit(dma_dev_exit);

MODULE_DESCRIPTION("Andes DMA device driver");
MODULE_LICENSE("GPL");
