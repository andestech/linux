// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Andes ATCDMAC300
 *
 * Copyright (C) 2021 Andes Technology Corporation
 *
 */
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/of_address.h>
#include <linux/dma-map-ops.h>
#include "dmaengine.h"
#include "atcdmac300g.h"

#define V5_DMA_BUSWIDTHS\
	(BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED) |\
	BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |\
	BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |\
	BIT(DMA_SLAVE_BUSWIDTH_4_BYTES))

static unsigned int init_nr_desc_per_channel = 64;
module_param(init_nr_desc_per_channel, uint, 0644);
MODULE_PARM_DESC(init_nr_desc_per_channel,
		 "initial descriptors per channel (default: 64)");

static inline void v5_en_channel(struct v5_dma_chan *v5chan)
{
	setbl(CHEN, v5chan->ch_regs+CH_CTL_OFF);
}

static inline void v5_dis_channel(struct v5_dma_chan *v5chan)
{
	clrbl(CHEN, v5chan->ch_regs+CH_CTL_OFF);
}

static inline void v5_abort_channel(struct v5_dma_chan *v5chan)
{
	setbl(v5chan->chan_id, v5chan->ch_regs + CH_ABT);
}

static dma_cookie_t v5_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct v5_desc		*desc = txd_to_v5_desc(tx);
	struct v5_dma_chan	*v5chan = to_v5_dma_chan(tx->chan);
	dma_cookie_t		cookie;
	unsigned long		flags;

	spin_lock_irqsave(&v5chan->lock, flags);
	cookie = dma_cookie_assign(tx);
	list_add_tail(&desc->desc_node, &v5chan->queue);
	spin_unlock_irqrestore(&v5chan->lock, flags);

	return cookie;
}

static struct v5_desc *v5_first_active(struct v5_dma_chan *v5chan)
{
	return list_first_entry(&v5chan->active_list,
				struct v5_desc, desc_node);
}

static struct v5_desc *v5_first_queued(struct v5_dma_chan *v5chan)
{
	return list_first_entry(&v5chan->queue,
				struct v5_desc, desc_node);
}

/**
 * v5_alloc_descriptor - allocate and return an initialized descriptor
 * @chan: the channel to allocate descriptors for
 * @gfp_flags: GFP allocation flags
 */
static struct v5_desc *v5_alloc_descriptor(struct dma_chan *chan,
					    gfp_t gfp_flags)
{
	struct v5_desc	*desc = NULL;
	struct v5_dma	*v5dma = to_v5_dma(chan->device);
	dma_addr_t phys;

	desc = dma_pool_zalloc(v5dma->dma_desc_pool, gfp_flags, &phys);
	if (desc) {
		INIT_LIST_HEAD(&desc->tx_list);
		dma_async_tx_descriptor_init(&desc->txd, chan);
		/* txd.flags will be overwritten in prep functions */
		desc->txd.flags = DMA_CTRL_ACK;
		desc->txd.tx_submit = v5_tx_submit;
		if (v5dma->io_regs)
			desc->txd.phys = phys | IOCP_MASK;
		else
			desc->txd.phys = phys;
	}

	return desc;
}

/**
 * v5_desc_get - get an unused descriptor from free_list
 * @v5chan: channel we want a new descriptor for
 */
static struct v5_desc *v5_desc_get(struct v5_dma_chan *v5chan)
{
	struct v5_desc *desc, *_desc;
	struct v5_desc *ret = NULL;
	unsigned long flags;
	unsigned int i = 0;

	spin_lock_irqsave(&v5chan->lock, flags);
	list_for_each_entry_safe(desc, _desc, &v5chan->free_list, desc_node) {
		i++;
		if (async_tx_test_ack(&desc->txd)) {
			list_del(&desc->desc_node);
			ret = desc;
			break;
		}
		dev_dbg(chan2dev(&v5chan->chan_common),
				"desc %p not ACKed\n", desc);
	}
	spin_unlock_irqrestore(&v5chan->lock, flags);
	dev_vdbg(chan2dev(&v5chan->chan_common),
		"scanned %u descriptors on freelist\n", i);
	if (!ret) {
		ret = v5_alloc_descriptor(&v5chan->chan_common, GFP_ATOMIC);
		if (ret) {
			spin_lock_irqsave(&v5chan->lock, flags);
			v5chan->descs_allocated++;
			spin_unlock_irqrestore(&v5chan->lock, flags);
		} else {
			dev_err(chan2dev(&v5chan->chan_common),
					"not enough descriptors available\n");
		}
	}

	return ret;
}

/**
 * v5_desc_put - move a descriptor, including any children, to the free list
 * @v5chan: channel we work on
 * @desc: descriptor, at the head of a chain, to move to free list
 */
static void v5_desc_put(struct v5_dma_chan *v5chan, struct v5_desc *desc)
{
	if (desc) {
		struct v5_desc *child;
		unsigned long flags;

		spin_lock_irqsave(&v5chan->lock, flags);
		list_for_each_entry(child, &desc->tx_list, desc_node)
			dev_vdbg(chan2dev(&v5chan->chan_common),
					"moving child desc %p to freelist\n",
					child);
		list_splice_init(&desc->tx_list, &v5chan->free_list);
		dev_vdbg(chan2dev(&v5chan->chan_common),
			 "moving desc %p to freelist\n", desc);
		list_add(&desc->desc_node, &v5chan->free_list);
		spin_unlock_irqrestore(&v5chan->lock, flags);
	}
}

/*
 * v5_desc_chain - build chain adding a descriptor
 * @first: address of first descriptor of the chain
 * @prev: address of previous descriptor of the chain
 * @desc: descriptor to queue
 */
static void v5_desc_chain(struct v5_desc **first, struct v5_desc **prev,
			   struct v5_desc *desc)
{
	if (!(*first)) {
		*first = desc;
		desc->at = &desc->tx_list;
	} else {
		if ((*first)->cyclic == false) {
			(*prev)->lli.llPointerl = lower_32_bits(desc->txd.phys);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
			(*prev)->lli.llPointerh = upper_32_bits(desc->txd.phys);
#endif
		}

		/* insert the descriptor to the ring */
		list_add_tail(&desc->desc_node, &(*first)->tx_list);
	}
	*prev = desc;

	desc->lli.llPointerh = 0;
	desc->lli.llPointerl = 0;
}

/**
 * v5_dostart - starts the DMA engine for real
 * @v5chan: the channel we want to start
 * @first: first descriptor in the list we want to begin with
 */
static void v5_dostart(struct v5_dma_chan *v5chan, struct v5_desc *first)
{
	if (v5_chan_is_enabled(v5chan)) {
		dev_err(chan2dev(&v5chan->chan_common),
			"BUG: Attempted to start non-idle channel\n");
		dev_err(chan2dev(&v5chan->chan_common),
			"  channel: 0x%x 0x%x 0x%x 0x%x 0x%x\n",
			v5_channel_readl(v5chan, CH_CTL_OFF),
			v5_channel_readl(v5chan, CH_SIZE_OFF),
			v5_channel_readl(v5chan, CH_SRC_LOW_OFF),
			v5_channel_readl(v5chan, CH_DST_LOW_OFF),
			v5_channel_readl(v5chan, CH_LLP_LOW_OFF));
		return;
	}
	vdbg_dump_regs(v5chan);
	v5_channel_writel(v5chan, CH_CTL_OFF, first->lli.ctrl);
	v5_channel_writel(v5chan, CH_SIZE_OFF, first->lli.tranSize);
	v5_channel_writel(v5chan, CH_SRC_LOW_OFF, first->lli.srcAddrl);
	v5_channel_writel(v5chan, CH_DST_LOW_OFF, first->lli.dstAddrl);
	v5_channel_writel(v5chan, CH_LLP_LOW_OFF, first->lli.llPointerl);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	v5_channel_writel(v5chan, CH_SRC_HIGH_OFF, first->lli.srcAddrh);
	v5_channel_writel(v5chan, CH_DST_HIGH_OFF, first->lli.dstAddrh);
	v5_channel_writel(v5chan, CH_LLP_HIGH_OFF, first->lli.llPointerh);
#endif
	v5_en_channel(v5chan);
}

/**
 * v5_chain_complete - finish work for one transaction chain
 * @v5chan: channel we work on
 * @desc: descriptor at the head of the chain we want do complete
 * @result: dma result
 */
static void v5_chain_complete(struct v5_dma_chan *v5chan,
	struct v5_desc *desc, enum dmaengine_tx_result result)
{
	struct dma_async_tx_descriptor	*txd = &desc->txd;
	struct dmaengine_result res;

	res.result = result;
	dev_vdbg(chan2dev(&v5chan->chan_common),
		"descriptor %u complete\n", txd->cookie);
	dma_cookie_complete(txd);
	/* move children to free_list */
	list_splice_init(&desc->tx_list, &v5chan->free_list);
	/* move myself to free_list */
	list_move(&desc->desc_node, &v5chan->free_list);
	dma_descriptor_unmap(txd);
	dmaengine_desc_get_callback_invoke(txd, &res);
	dma_run_dependencies(txd);
}

/**
 * v5_complete_all - finish work for all transactions
 * @v5chan: channel to complete transactions for
 */
static void v5_complete_all(struct v5_dma_chan *v5chan)
{
	struct v5_desc *desc, *_desc;
	LIST_HEAD(list);

	dev_vdbg(chan2dev(&v5chan->chan_common), "complete all\n");
	if (!list_empty(&v5chan->queue))
		v5_dostart(v5chan, v5_first_queued(v5chan));
	/* empty active_list now it is completed */
	list_splice_init(&v5chan->active_list, &list);
	/* empty queue list by moving descriptors (if any) to active_list */
	list_splice_init(&v5chan->queue, &v5chan->active_list);

	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		v5_chain_complete(v5chan, desc, DMA_TRANS_NOERROR);
}

/**
 * v5_advance_work - at the end of a transaction, move forward
 * @v5chan: channel where the transaction ended
 */
static void v5_advance_work(struct v5_dma_chan *v5chan)
{
	struct v5_desc *v5desc = NULL;

	dev_vdbg(chan2dev(&v5chan->chan_common), "advance_work\n");
	clear_bit(V5_IS_TC, &v5chan->status);
	if (v5_chan_is_enabled(v5chan))
		return;

	v5desc = v5_first_active(v5chan);
	if (!v5desc->cyclic) {
		if (list_empty(&v5chan->active_list) ||
		    list_is_singular(&v5chan->active_list)) {
			v5_complete_all(v5chan);
		} else {
			v5_chain_complete(v5chan, v5_first_active(v5chan),
				DMA_TRANS_NOERROR);
			/* advance work */
			v5_dostart(v5chan, v5_first_active(v5chan));
		}
	} else {
		/* For cyclic mode */
		struct dmaengine_result res;

		if (!list_empty(&v5chan->active_list)) {
			res.result = DMA_TRANS_NOERROR;

			if (v5desc->num_sg == 1) {
				/* Just repeat itself */
				v5_dostart(v5chan, v5_first_active(v5chan));
			} else {
				struct v5_desc *next_tx;
				unsigned long flags;

				/* do next SG and set the at pointer */
				v5desc->at = v5desc->at->next;

				spin_lock_irqsave(&v5chan->lock, flags);
				if ((uintptr_t)v5desc->at == (uintptr_t)&v5desc->tx_list) {
					next_tx = list_entry(v5desc->at, struct v5_desc,
						tx_list);
				} else {
					next_tx = list_entry(v5desc->at, struct v5_desc,
						desc_node);
				}
				spin_unlock_irqrestore(&v5chan->lock, flags);

				v5_dostart(v5chan, next_tx);

				dmaengine_desc_get_callback_invoke(&v5desc->txd, &res);
			}
		} else {
			dev_vdbg(chan2dev(&v5chan->chan_common),
				"active list is empty\n");
		}
	}
}

/**
 * v5_handle_error - handle errors reported by DMA controller
 * @v5chan: channel where error occurs
 */
static void v5_handle_error(struct v5_dma_chan *v5chan)
{
	struct v5_desc *bad_desc;

	clear_bit(V5_IS_ERR, &v5chan->status);
	/*
	 * The descriptor currently at the head of the active list is
	 * broked. Since we don't have any way to report errors, we'll
	 * just have to scream loudly and try to carry on.
	 */
	bad_desc = v5_first_active(v5chan);
	list_del_init(&bad_desc->desc_node);

	/*
	 * As we are stopped, take advantage
	 * to push queued descriptors in active_list
	 */
	list_splice_init(&v5chan->queue, v5chan->active_list.prev);

	/* Try to restart the controller */
	if (!list_empty(&v5chan->active_list))
		v5_dostart(v5chan, v5_first_active(v5chan));

	/*
	 * KERN_CRITICAL may seem harsh, but since this only happens
	 * when someone submits a bad physical address in a
	 * descriptor, we should consider ourselves lucky that the
	 * controller flagged an error instead of scribbling over
	 * random memory locations.
	 */
	dev_crit(chan2dev(&v5chan->chan_common),
			"Bad descriptor submitted for DMA!\n");
	dev_crit(chan2dev(&v5chan->chan_common),
			"  cookie: %d\n", bad_desc->txd.cookie);
	/* Report the descriptor that dma abort */
	v5_chain_complete(v5chan, bad_desc, DMA_TRANS_ABORTED);
}

static void v5_tasklet(unsigned long data)
{
	struct v5_dma_chan *v5chan = (struct v5_dma_chan *)data;

	if (test_and_clear_bit(V5_IS_ERR, &v5chan->status))
		v5_handle_error(v5chan);
	else
		v5_advance_work(v5chan);
}

static irqreturn_t v5_dma_interrupt(int irq, void *dev_id)
{
	struct v5_dma		*v5dma = (struct v5_dma *)dev_id;
	struct v5_dma_chan	*v5chan;
	int			i;
	u32			status;
	int			ret = IRQ_NONE;

	do {
		status = v5_dma_readl(v5dma, INT_STA);

		dev_vdbg(v5dma->dma_common.dev,
			"int: sta = 0x%08x\n", status);
		if (status == 0)
			break;

		v5_dma_writel(v5dma, INT_STA, status);

		for (i = 0; i < v5dma->dma_common.chancnt; i++) {
			v5chan = &v5dma->chan[i];
			if (status & (V5_DMA_TC(i)))
				set_bit(V5_IS_TC, &v5chan->status);

			if (status & (V5_DMA_ABT(i) | V5_DMA_ERR(i)))
				set_bit(V5_IS_ERR, &v5chan->status);

			if (v5chan->status) {
				tasklet_schedule(&v5chan->tasklet);
				ret = IRQ_HANDLED;
			}
		}
	} while (status);

	return ret;
}

static void v5_issue_pending(struct dma_chan *chan)
{
	struct v5_dma_chan	*v5chan = to_v5_dma_chan(chan);
	unsigned long		flags;

	spin_lock_irqsave(&v5chan->lock, flags);
	if (list_empty(&v5chan->active_list))
		list_move(v5chan->queue.next, &v5chan->active_list);
	v5_dostart(v5chan, v5_first_active(v5chan));
	spin_unlock_irqrestore(&v5chan->lock, flags);
}

static inline unsigned int xfer_width(struct v5_dma *v5dma,
	dma_addr_t src, dma_addr_t dst, size_t len)
{
	unsigned int width;

	if (!((src | dst  | len) & 15))
		width = 4;
	else if (!((src | dst | len) & 7))
		width = 3;
	else if (!((src | dst | len) & 3))
		width = 2;
	else if (!((src | dst | len) & 1))
		width = 1;
	else
		width = 0;

	width = width > v5dma->data_width ?
		v5dma->data_width : width;

	return width;
}

/**
 * v5_prep_dma_memcpy - prepare a memcpy operation
 * @chan: the channel to prepare operation on
 * @dest: operation virtual destination address
 * @src: operation virtual source address
 * @len: operation length
 * @flags: tx descriptor status flags
 */
static struct dma_async_tx_descriptor *
v5_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dst, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct v5_dma_chan	*v5chan = to_v5_dma_chan(chan);
	struct v5_desc		*desc;
	unsigned int		src_width;
	unsigned int		dst_width;
	u32			src_maxburst;
	u32			ctrl;

	struct v5_dma	*v5dma = to_v5_dma(chan->device);

	desc = v5_desc_get(v5chan);
	if (!desc)
		goto err_desc_get;

	if (v5dma->io_regs) {
		dst |= IOCP_MASK;
		src |= IOCP_MASK;
	}
	src_maxburst = DMAC_CSR_SIZE_1024;
	src_width = dst_width = xfer_width(v5dma, src, dst, len);
	convert_burst(&src_maxburst);
	ctrl = v5_channel_readl(v5chan, CH_CTL_OFF);
	ctrl &= ~(SRCWIDTH_MASK|SRCADDRCTRL_MASK|DSTWIDTH_MASK|
		DSTADDRCTRL_MASK|SRC_HS|DST_HS|SRCREQSEL_MASK|DSTREQSEL_MASK);
	ctrl =   SBSIZE(src_maxburst);
	ctrl |=  SRC_WIDTH(src_width);
	ctrl |=  DST_WIDTH(dst_width);
	ctrl |=  (SRC_ADDR_MODE_INCR | DST_ADDR_MODE_INCR);
	desc->lli.srcAddrl = lower_32_bits(src);
	desc->lli.dstAddrl = lower_32_bits(dst);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	desc->lli.srcAddrh = upper_32_bits(src);
	desc->lli.dstAddrh = upper_32_bits(dst);
#endif
	desc->lli.ctrl = ctrl;
	desc->lli.tranSize = (len >> src_width);
	desc->num_sg = 1;
	desc->total_len = len;

	return &desc->txd;

err_desc_get:
	v5_desc_put(v5chan, desc);
	return NULL;
}

/**
 * v5_prep_slave_sg - prepare descriptors for a DMA_SLAVE transaction
 * @chan: DMA channel
 * @sgl: scatterlist to transfer to/from
 * @sg_len: number of entries in @scatterlist
 * @direction: DMA direction
 * @flags: tx descriptor status flags
 * @context: transaction context (ignored)
 */
static struct dma_async_tx_descriptor *
v5_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct v5_dma		*v5dma = to_v5_dma(chan->device);
	struct v5_dma_chan	*v5chan = to_v5_dma_chan(chan);
	struct v5_dma_slave	*v5slave = chan->private;
	struct dma_slave_config	*sconfig = &v5chan->dma_sconfig;
	struct v5_desc		*first = NULL;
	struct v5_desc		*prev = NULL;
	u32			ctrl;
	dma_addr_t		reg;
	unsigned int		reg_width;
	unsigned int		src_reg_width;
	unsigned int		i;
	struct scatterlist	*sg;
	size_t			total_len = 0;

	dev_vdbg(chan2dev(chan), "prep_slave_sg (%d): %s f0x%lx\n",
		sg_len,
	direction == DMA_MEM_TO_DEV ?
		"TO DEVICE" : "FROM DEVICE", flags);
	if (unlikely(!v5slave || !sg_len)) {
		dev_dbg(chan2dev(chan), "prep_slave_sg: sg length is zero!\n");
		return NULL;
	}
	ctrl = v5_channel_readl(v5chan, CH_CTL_OFF);
	ctrl &= ~(SRCWIDTH_MASK|SRCADDRCTRL_MASK|DSTWIDTH_MASK|
		DSTADDRCTRL_MASK|SRC_HS|DST_HS|SRCREQSEL_MASK|DSTREQSEL_MASK);
	ctrl =   SBSIZE(sconfig->src_maxburst);
	src_reg_width = convert_buswidth(sconfig->src_addr_width);
	ctrl |=  SRC_WIDTH(src_reg_width);
	reg_width = convert_buswidth(sconfig->dst_addr_width);
	ctrl |=  DST_WIDTH(reg_width);

	switch (direction) {
	case DMA_MEM_TO_DEV:
		ctrl |=  DST_HS;
		ctrl |=  (SRC_ADDR_MODE_INCR | DST_ADDR_MODE_FIXED);
		ctrl |=  ((v5chan->req_num << DSTREQSEL) & DSTREQSEL_MASK);
		reg = sconfig->dst_addr;
		for_each_sg(sgl, sg, sg_len, i) {
			struct v5_desc	*desc;
			u32		len;
			dma_addr_t		mem;

			desc = v5_desc_get(v5chan);
			if (!desc)
				goto err_desc_get;

			mem = sg_dma_address(sg);

			if (v5dma->io_regs)
				mem |= IOCP_MASK;

			len = sg_dma_len(sg);
			if (unlikely(!len)) {
				dev_dbg(chan2dev(chan),
					"sg(%d) data length is zero\n", i);
				goto err;
			}
			if (unlikely(mem & 3 || len & 3)) {
				dev_dbg(chan2dev(chan),
				"sg(%d) data length is not aligned\n", i);
				goto err_desc_get;
			}
			desc->lli.srcAddrl = lower_32_bits(mem);
			desc->lli.dstAddrl = lower_32_bits(reg);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
			desc->lli.srcAddrh = upper_32_bits(mem);
			desc->lli.dstAddrh = upper_32_bits(reg);
#endif
			desc->lli.ctrl = ctrl;
			desc->lli.tranSize = (len >> src_reg_width);
			v5_desc_chain(&first, &prev, desc);
			total_len += len;
			desc->num_sg = 1;
		}
		break;

	case DMA_DEV_TO_MEM:
		ctrl |=  SRC_HS;
		ctrl |=  (SRC_ADDR_MODE_FIXED | DST_ADDR_MODE_INCR);
		ctrl |=  ((v5chan->req_num << SRCREQSEL) & SRCREQSEL_MASK);
		reg = sconfig->src_addr;
		for_each_sg(sgl, sg, sg_len, i) {
			struct v5_desc	*desc;
			u32		len;
			phys_addr_t	mem;

			desc = v5_desc_get(v5chan);
			if (!desc)
				goto err_desc_get;
			mem = sg_dma_address(sg);

			if (v5dma->io_regs)
				mem |= IOCP_MASK;

			len = sg_dma_len(sg);
			if (unlikely(!len)) {
				dev_dbg(chan2dev(chan),
					"sg(%d) data length is zero\n", i);
				goto err;
			}
			if (unlikely(mem & 3 || len & 3)) {
				dev_dbg(chan2dev(chan),
				"sg(%d) data length is not aligned\n", i);
				goto err;
			}
			desc->lli.srcAddrl = lower_32_bits(reg);
			desc->lli.dstAddrl = lower_32_bits(mem);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
			desc->lli.srcAddrh = upper_32_bits(reg);
			desc->lli.dstAddrh = upper_32_bits(mem);
#endif
			desc->lli.ctrl = ctrl;
			desc->lli.tranSize = (len >> src_reg_width);
			v5_desc_chain(&first, &prev, desc);
			total_len += len;
			desc->num_sg = 1;
		}
		break;
	default:
		return NULL;
	}
	/* First descriptor of the chain embedds additional information */
	first->txd.cookie = -EBUSY;
	first->total_len = total_len;
	first->cyclic = false;

	/* first link descriptor of list is responsible of flags */
	first->txd.flags = flags;

	return &first->txd;

err_desc_get:
	dev_err(chan2dev(chan), "not enough descriptors available\n");
err:
	v5_desc_put(v5chan, first);
	return NULL;
}

static struct dma_async_tx_descriptor *
v5_prep_dma_cyclic(struct dma_chan *chan, dma_addr_t buf_addr,
		size_t buf_len, size_t period_len,
		enum dma_transfer_direction direction, unsigned long flags)
{

	struct v5_dma_chan *v5chan = to_v5_dma_chan(chan);
	struct dma_slave_config *sconfig = &v5chan->dma_sconfig;
	struct v5_desc *first = NULL;
	struct v5_desc *prev = NULL;
	u32 ctrl;
	dma_addr_t reg;
	unsigned int reg_width;
	unsigned int i;
	size_t total_len = 0;
	struct v5_dma *v5dma = to_v5_dma(chan->device);
	int period_index;
	u32 src_maxburst;

	src_maxburst = DMAC_CSR_SIZE_1;
	convert_burst(&src_maxburst);
	sconfig->src_maxburst = src_maxburst;

	ctrl = v5_channel_readl(v5chan, CH_CTL_OFF);
	ctrl &= ~(SRCWIDTH_MASK | SRCADDRCTRL_MASK | DSTWIDTH_MASK |
		  DSTADDRCTRL_MASK | SRC_HS | DST_HS | SRCREQSEL_MASK |
		  DSTREQSEL_MASK | INTABTMASK | INTERRMASK | INTTCMASK);
	ctrl =   SBSIZE(sconfig->src_maxburst);
	reg_width = WIDTH_4;
	ctrl |=  SRC_WIDTH(reg_width);
	ctrl |=  DST_WIDTH(convert_buswidth(sconfig->dst_addr_width));

	switch (direction) {
	case DMA_MEM_TO_DEV:
		ctrl |= DST_HS;
		ctrl |= (SRC_ADDR_MODE_INCR | DST_ADDR_MODE_FIXED);
		ctrl |= ((v5chan->req_num << DSTREQSEL) & DSTREQSEL_MASK);
		reg = sconfig->dst_addr;

		for (period_index = 0; period_index < buf_len; period_index += period_len) {
			struct v5_desc *desc;
			u32 len;
			dma_addr_t mem;

			desc = v5_desc_get(v5chan);
			if (!desc)
				goto err_desc_get;

			mem = buf_addr + period_index;

			if ((buf_len - period_index) > period_len)
				len = period_len;
			else
				len = buf_len - period_index;

			if (unlikely(!len)) {
				dev_dbg(chan2dev(chan),
					"sg(%d) data length is zero\n", i);
				goto err;
			}

			if (unlikely(mem & 3 || len & 3)) {
				dev_dbg(chan2dev(chan),
					"sg(%d) data length is not aligned\n", i);
				goto err_desc_get;
			}

			if (v5dma->io_regs)
				mem |= IOCP_MASK;

			desc->lli.srcAddrl = lower_32_bits(mem);
			desc->lli.dstAddrl = lower_32_bits(reg);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
			desc->lli.srcAddrh = upper_32_bits(mem);
			desc->lli.dstAddrh = upper_32_bits(reg);
#endif
			desc->lli.ctrl = ctrl;
			desc->lli.tranSize = (len >> reg_width);

			desc->cyclic = true;
			desc->num_sg = (buf_len + period_len - 1) / period_len;
			desc->len = len;
			desc->total_len = buf_len - period_index;
			v5_desc_chain(&first, &prev, desc);
			total_len += len;
		}
		break;

	case DMA_DEV_TO_MEM:
		ctrl |= SRC_HS;
		ctrl |= (SRC_ADDR_MODE_FIXED | DST_ADDR_MODE_INCR);
		ctrl |= ((v5chan->req_num << SRCREQSEL) & SRCREQSEL_MASK);
		reg = sconfig->src_addr;


		for (period_index = 0; period_index < buf_len;
		     period_index += period_len) {
			struct v5_desc *desc;
			u32 len;
			phys_addr_t mem;

			desc = v5_desc_get(v5chan);
			if (!desc)
				goto err_desc_get;

			mem = buf_addr + period_index;

			if (v5dma->io_regs)
				mem |= IOCP_MASK;

			if ((buf_len - period_index) > period_len)
				len = period_len;
			else
				len = buf_len - period_index;

			if (unlikely(!len)) {
				dev_dbg(chan2dev(chan),
					"sg(%d) data length is zero\n", i);
				goto err;
			}

			if (unlikely(mem & 3 || len & 3)) {
				dev_dbg(chan2dev(chan),
					"sg(%d) data length is not aligned\n", i);
				goto err;
			}

			desc->lli.srcAddrl = lower_32_bits(reg);
			desc->lli.dstAddrl = lower_32_bits(mem);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
			desc->lli.srcAddrh = upper_32_bits(reg);
			desc->lli.dstAddrh = upper_32_bits(mem);
#endif
			desc->lli.ctrl = ctrl;
			desc->lli.tranSize = (len >> reg_width);

			desc->cyclic = true;
			desc->num_sg = (buf_len + period_len - 1) / period_len;
			desc->len = len;
			desc->total_len = buf_len - period_index;
			v5_desc_chain(&first, &prev, desc);
			total_len += len;
		}
		break;
	default:
		return NULL;
	}

	first->txd.flags = flags;

	return &first->txd;

err_desc_get:
	dev_err(chan2dev(chan), "Not enough descriptors available\n");
err:
	v5_desc_put(v5chan, first);


	return NULL;
}

static int v5_config(struct dma_chan *chan,
		      struct dma_slave_config *sconfig)
{
	struct v5_dma_chan	*v5chan = to_v5_dma_chan(chan);

	dev_vdbg(chan2dev(chan), "%s\n", __func__);
	/* Check if it is chan is configured for slave transfers */
	if (!chan->private)
		return -EINVAL;

	memcpy(&v5chan->dma_sconfig, sconfig, sizeof(*sconfig));
	convert_burst(&v5chan->dma_sconfig.src_maxburst);
	convert_burst(&v5chan->dma_sconfig.dst_maxburst);

	return 0;
}


static int v5_terminate_all(struct dma_chan *chan)
{
	struct v5_dma_chan	*v5chan = to_v5_dma_chan(chan);
	LIST_HEAD(list);

	dev_vdbg(chan2dev(chan), "%s\n", __func__);
	/*
	 * This is only called when something went wrong elsewhere, so
	 * we don't really care about the data. Just disable the
	 * channel.
	 */

	/* disabling channel: must also remove suspend state */
	v5_abort_channel(v5chan);
	v5_dis_channel(v5chan);
	/* confirm that this channel is disabled */
	while (v5_chan_is_enabled(v5chan))
		cpu_relax();
	/* active_list entries will end up before queued entries */
	list_splice_init(&v5chan->queue, &v5chan->free_list);
	list_splice_init(&v5chan->active_list, &v5chan->free_list);

	clear_bit(V5_IS_PAUSED, &v5chan->status);

	return 0;
}

/**
 * v5_tx_status - poll for transaction completion
 * @chan: DMA channel
 * @cookie: transaction identifier to check status of
 * @txstate: if not %NULL updated with transaction state
 */
static enum dma_status
v5_tx_status(struct dma_chan *chan,
		dma_cookie_t cookie,
		struct dma_tx_state *txstate)
{
	struct v5_dma_chan *v5chan = to_v5_dma_chan(chan);
	struct v5_desc *v5desc = NULL;
	enum dma_status ret;
	size_t residue = 0;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	/*
	 * There's no point calculating the residue if there's
	 * no txstate to store the value.
	 */
	if (!txstate)
		return ret;

	v5desc = v5_first_active(v5chan);
	if (v5desc) {
		if (v5desc->num_sg == 1) {
			residue = v5desc->total_len;
		} else {
			struct v5_desc *v5desc_cur = NULL;

			if ((uintptr_t)v5desc->at == (uintptr_t)&v5desc->tx_list)
				v5desc_cur = v5desc;
			else
				v5desc_cur = list_entry(v5desc->at, struct v5_desc, desc_node);

			residue = v5desc_cur->total_len;
		}
	}
	dma_set_residue(txstate, residue);
	return ret;
}

/**
 * v5_alloc_chan_resources - allocate resources for DMA channel
 * @chan: allocate descriptor resources for this channel
 * @client: current client requesting the channel be ready for requests
 *
 * return - the number of allocated descriptors
 */
static int v5_alloc_chan_resources(struct dma_chan *chan)
{
	struct v5_dma_chan	*v5chan = to_v5_dma_chan(chan);
	struct v5_dma		*v5dma = to_v5_dma(chan->device);
	struct v5_desc		*desc;
	unsigned long		flags;
	int			i;
	LIST_HEAD(tmp_list);

	dev_vdbg(chan2dev(chan), "alloc_chan_resources\n");

	/* ASSERT:  channel is idle */
	if (v5_chan_is_enabled(v5chan)) {
		dev_dbg(chan2dev(chan), "DMA channel not idle ?\n");
		return -EIO;
	}

	/*
	 * have we already been set up?
	 * reconfigure channel but no need to reallocate descriptors
	 */
	if (!list_empty(&v5chan->free_list))
		return v5chan->descs_allocated;

	/* Allocate initial pool of descriptors */
	for (i = 0; i < init_nr_desc_per_channel; i++) {
		desc = v5_alloc_descriptor(chan, GFP_KERNEL);
		if (!desc) {
			dev_err(v5dma->dma_common.dev,
				"Only %d initial descriptors\n", i);
			break;
		}
		list_add_tail(&desc->desc_node, &tmp_list);
	}

	spin_lock_irqsave(&v5chan->lock, flags);
	v5chan->descs_allocated = i;
	list_splice(&tmp_list, &v5chan->free_list);
	dma_cookie_init(chan);
	spin_unlock_irqrestore(&v5chan->lock, flags);

	dev_dbg(chan2dev(chan),
		"alloc_chan_resources: allocated %d descriptors\n",
		v5chan->descs_allocated);

	return v5chan->descs_allocated;
}

/**
 * v5_free_chan_resources - free all channel resources
 * @chan: DMA channel
 */
static void v5_free_chan_resources(struct dma_chan *chan)
{
	struct v5_dma_chan	*v5chan = to_v5_dma_chan(chan);
	struct v5_dma		*v5dma = to_v5_dma(chan->device);
	struct v5_desc		*desc, *_desc;
	LIST_HEAD(list);

	dev_dbg(chan2dev(chan), "free_chan_resources: (descs allocated=%u)\n",
		v5chan->descs_allocated);

	/* ASSERT:  channel is idle */
	WARN_ON(!list_empty(&v5chan->active_list));
	WARN_ON(!list_empty(&v5chan->queue));
	WARN_ON(v5_chan_is_enabled(v5chan));

	list_for_each_entry_safe(desc, _desc, &v5chan->free_list, desc_node) {
		dev_vdbg(chan2dev(chan), "  freeing descriptor %p\n", desc);
		list_del(&desc->desc_node);
		/* free link descriptor */
		dma_pool_free(v5dma->dma_desc_pool, desc, desc->txd.phys);
	}
	list_splice_init(&v5chan->free_list, &list);
	v5chan->descs_allocated = 0;
	v5chan->status = 0;
	kfree(chan->private);
	chan->private = NULL;
	dev_vdbg(chan2dev(chan), "free_chan_resources: done\n");
}

#ifdef CONFIG_OF
static bool v5_dma_filter(struct dma_chan *chan, void *slave)
{
	struct v5_dma_slave *atslave = slave;

	if (atslave->dma_dev == chan->device->dev) {
		chan->private = atslave;

		return true;
	} else
		return false;
}
static struct dma_chan *v5_dma_xlate(struct of_phandle_args *dma_spec,
				     struct of_dma *of_dma)
{
	struct dma_chan *chan;

	struct v5_dma_chan *v5chan;
	struct v5_dma_slave *v5slave;
	dma_cap_mask_t mask;
	struct platform_device *dmac_pdev;

	dmac_pdev = of_find_device_by_node(dma_spec->np);
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	v5slave = kzalloc(sizeof(*v5slave), GFP_KERNEL);
	if (!v5slave)
		return NULL;

	v5slave->dma_dev = &dmac_pdev->dev;
	chan = dma_request_channel(mask, v5_dma_filter, v5slave);
	if (!chan)
		return NULL;

	v5chan = to_v5_dma_chan(chan);
	v5chan->req_num = dma_spec->args[0] & 0xff;

	return chan;
}
#else
static struct dma_chan *v5_dma_xlate(struct of_phandle_args *dma_spec,
				     struct of_dma *of_dma)
{
	return NULL;
}
#endif

static struct v5_dma_platform_data v5dma_config = {
	.nr_channels = 8,
};

#if defined(CONFIG_OF)
static const struct of_device_id v5_dma_dt_ids[] = {
	{
		.compatible = "andestech,atcdmac300g",
		.data = &v5dma_config,
	}, {
		/* sentinel */
	}
};

MODULE_DEVICE_TABLE(of, v5_dma_dt_ids);
#endif

static const struct platform_device_id v5dma_devtypes[] = {
	{
		.name = "v5dma",
		.driver_data = (unsigned long) &v5dma_config,
	}, {
		/* sentinel */
	}
};

static inline const struct v5_dma_platform_data * __init v5_dma_get_driver_data(
						struct platform_device *pdev)
{
	if (pdev->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_node(v5_dma_dt_ids, pdev->dev.of_node);
		if (match == NULL)
			return NULL;
		return match->data;
	}
	return (struct v5_dma_platform_data *)
			platform_get_device_id(pdev)->driver_data;
}

/**
 * v5_dma_off - disable DMA controller
 */
static void v5_dma_off(struct v5_dma *v5dma)
{
	/* reset the DMA */
	setbl(CHEN, v5dma->regs+CTL);
	/* disable all channels */
	clrbl(CHEN, v5dma->regs+CTL);
	/* confirm that all channels are disabled */
	while (v5_dma_readl(v5dma, CH_EN) & v5dma->all_chan_mask)
		cpu_relax();
}

static int __init v5_dma_probe(struct platform_device *pdev)
{
	struct resource		*io;
	struct v5_dma		*v5dma;
	size_t			size;
	int			irq;
	int			err;
	int			i;
	const struct v5_dma_platform_data *plat_dat;
	const __be32 *prop;
	int len;

	dma_cap_set(DMA_SLAVE, v5dma_config.cap_mask);
	dma_cap_set(DMA_MEMCPY, v5dma_config.cap_mask);
	dma_cap_set(DMA_CYCLIC, v5dma_config.cap_mask);

	/* get DMA parameters from controller type */
	plat_dat = v5_dma_get_driver_data(pdev);
	if (!plat_dat)
		return -ENODEV;

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	size = sizeof(struct v5_dma);
	size += plat_dat->nr_channels * sizeof(struct v5_dma_chan);
	v5dma = kzalloc(size, GFP_KERNEL);
	if (!v5dma)
		return -ENOMEM;

	v5dma->io_regs = 0;
	if (dev_is_dma_coherent(&pdev->dev)) {
		u64 taddr;

		prop = of_get_property(pdev->dev.of_node, "iocp-address", &len);
		if (prop) {
			taddr = of_translate_address(pdev->dev.of_node, prop);
			v5dma->io_regs = ioremap(taddr, 0x10);
		}
	}
	/* discover transaction capabilities */
	v5dma->dma_common.cap_mask = plat_dat->cap_mask;
	v5dma->all_chan_mask = (1 << plat_dat->nr_channels) - 1;
	size = resource_size(io);
	if (!request_mem_region(io->start, size, pdev->dev.driver->name)) {
		err = -EBUSY;
		goto err_kfree;
	}

	v5dma->regs = ioremap(io->start, size);
	if (!v5dma->regs) {
		err = -ENOMEM;
		goto err_release_r;
	}
	v5dma->ctl = v5_dma_readl(v5dma, CFG);
	v5dma->ch = (v5dma->ctl & CH_NUM);
	v5dma->data_width = ((v5dma->ctl & DATA_WIDTH)>>DATA_WIDTH_OFF) + 2;
	v5dma->ch = (plat_dat->nr_channels > v5dma->ch) ?
		v5dma->ch : plat_dat->nr_channels;

	v5_dma_off(v5dma);
	err = request_irq(irq, v5_dma_interrupt, 0, "v5_dmac", v5dma);
	if (err)
		goto err_irq;

	platform_set_drvdata(pdev, v5dma);
	/* create a pool of consistent memory blocks for hardware descriptors */
	v5dma->dma_desc_pool = dma_pool_create("v5_desc_pool",
			&pdev->dev, sizeof(struct v5_desc),
			64 /* 8 bytes aligned */, 4096);

	if (!v5dma->dma_desc_pool) {
		dev_err(&pdev->dev, "No memory for descriptors dma pool\n");
		err = -ENOMEM;
		goto err_desc_pool_create;
	}
	/* initialize channels related values */
	INIT_LIST_HEAD(&v5dma->dma_common.channels);
	for (i = 0; i < v5dma->ch; i++) {
		struct v5_dma_chan	*v5chan = &v5dma->chan[i];

		v5chan->chan_common.device = &v5dma->dma_common;
		dma_cookie_init(&v5chan->chan_common);
		list_add_tail(&v5chan->chan_common.device_node,
				&v5dma->dma_common.channels);
		v5chan->ch_regs = v5dma->regs + ch_regs(i);
		spin_lock_init(&v5chan->lock);
		v5chan->mask = 1 << i;
		INIT_LIST_HEAD(&v5chan->active_list);
		INIT_LIST_HEAD(&v5chan->queue);
		INIT_LIST_HEAD(&v5chan->free_list);
		tasklet_init(&v5chan->tasklet, v5_tasklet,
				(unsigned long)v5chan);
		v5_enable_chan_irq(v5dma, i);
		v5chan->device = v5dma;
		v5chan->chan_id = i;
	}
	/* set base routines */
	v5dma->dma_common.device_alloc_chan_resources = v5_alloc_chan_resources;
	v5dma->dma_common.device_free_chan_resources = v5_free_chan_resources;
	v5dma->dma_common.device_tx_status = v5_tx_status;
	v5dma->dma_common.device_issue_pending = v5_issue_pending;
	v5dma->dma_common.dev = &pdev->dev;
	if (dma_has_cap(DMA_MEMCPY, v5dma->dma_common.cap_mask))
		v5dma->dma_common.device_prep_dma_memcpy = v5_prep_dma_memcpy;

	if (dma_has_cap(DMA_CYCLIC, v5dma->dma_common.cap_mask)) {
		v5dma->dma_common.device_prep_dma_cyclic = v5_prep_dma_cyclic;
	}

	if (dma_has_cap(DMA_SLAVE, v5dma->dma_common.cap_mask)) {
		v5dma->dma_common.device_prep_slave_sg = v5_prep_slave_sg;
		v5dma->dma_common.device_config = v5_config;
		v5dma->dma_common.device_terminate_all = v5_terminate_all;
		v5dma->dma_common.src_addr_widths = V5_DMA_BUSWIDTHS;
		v5dma->dma_common.dst_addr_widths = V5_DMA_BUSWIDTHS;
		v5dma->dma_common.directions = BIT(DMA_DEV_TO_MEM)
			| BIT(DMA_MEM_TO_DEV);
		v5dma->dma_common.residue_granularity =
			DMA_RESIDUE_GRANULARITY_BURST;
	}
	dev_info(&pdev->dev, "Atcdmac300 DMA Controller (%s,%s), %d channels\n",
	  dma_has_cap(DMA_MEMCPY, v5dma->dma_common.cap_mask) ? "cpy" : "",
	  dma_has_cap(DMA_SLAVE, v5dma->dma_common.cap_mask)  ? "slave" : "",
	  v5dma->ch);
	dma_async_device_register(&v5dma->dma_common);

	if (v5dma->io_regs) {
		v5_dma_soc_writel(v5dma, CACHE_CTRL, IOCP_CACHE_DMAC0_AW |
			IOCP_CACHE_DMAC0_AR | IOCP_CACHE_DMAC1_AW |
			IOCP_CACHE_DMAC1_AW);
	}
	/*
	 * Do not return an error if the dmac node is not present in order to
	 * not break the existing way of requesting channel with
	 * dma_request_channel().
	 */
	if (pdev->dev.of_node) {
		err = of_dma_controller_register(pdev->dev.of_node,
						 v5_dma_xlate, v5dma);
		if (err) {
			dev_err(&pdev->dev, "could not register of_dma_controller\n");
			goto err_of_dma_controller_register;
		}
	}

	return 0;

err_of_dma_controller_register:
	dma_async_device_unregister(&v5dma->dma_common);
	dma_pool_destroy(v5dma->dma_desc_pool);
err_desc_pool_create:
	free_irq(platform_get_irq(pdev, 0), v5dma);
err_irq:
	iounmap(v5dma->regs);
	v5dma->regs = NULL;
err_release_r:
	release_mem_region(io->start, size);
err_kfree:
	kfree(v5dma);
	return err;
}

static int v5_dma_remove(struct platform_device *pdev)
{
	struct v5_dma		*v5dma = platform_get_drvdata(pdev);
	struct dma_chan		*chan, *_chan;
	struct resource		*io;

	v5_dma_off(v5dma);
	if (pdev->dev.of_node)
		of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&v5dma->dma_common);
	dma_pool_destroy(v5dma->dma_desc_pool);
	free_irq(platform_get_irq(pdev, 0), v5dma);

	list_for_each_entry_safe(chan, _chan, &v5dma->dma_common.channels,
			device_node) {
		struct v5_dma_chan	*v5chan = to_v5_dma_chan(chan);

		/* Disable interrupts */
		v5_disable_chan_irq(v5dma, chan->chan_id);
		tasklet_kill(&v5chan->tasklet);
		list_del(&chan->device_node);
	}

	iounmap(v5dma->regs);
	v5dma->regs = NULL;
	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(io->start, resource_size(io));
	kfree(v5dma);

	return 0;
}

static void v5_dma_shutdown(struct platform_device *pdev)
{
	v5_dma_off(platform_get_drvdata(pdev));
}

static struct platform_driver v5_dma_driver = {
	.remove		= v5_dma_remove,
	.shutdown	= v5_dma_shutdown,
	.id_table	= v5dma_devtypes,
	.driver = {
		.name	= "v5dmac",
		.of_match_table	= of_match_ptr(v5_dma_dt_ids),
	},
};

static int __init v5_dma_init(void)
{
	return platform_driver_probe(&v5_dma_driver, v5_dma_probe);
}

subsys_initcall(v5_dma_init);

static void __exit v5_dma_exit(void)
{
	platform_driver_unregister(&v5_dma_driver);
}
module_exit(v5_dma_exit);
MODULE_DESCRIPTION("Andestech ATCDMAC300 Controller driver");
MODULE_AUTHOR("Rick Chen <rick@andestech.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:v5dmac");
