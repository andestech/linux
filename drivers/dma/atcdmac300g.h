/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Header file for the Andestech DMA Controller driver
 *
 * Copyright (C) 2021 Andestech Corporation
 */
#ifndef ATCDMAC300G_H
#define	ATCDMAC300G_H

#include <linux/platform_data/dma-v5.h>

typedef u32 addr_t;

static inline void setbl(addr_t bit, void __iomem *reg)
{
	writel(readl(reg) | (addr_t) bit, reg);
}

static inline void clrbl(addr_t bit, void __iomem *reg)
{
	writel(readl(reg) & (~((addr_t) bit)), reg);
}

#define DMAC_BASE			(0)

/* ID and Revision Register */
#define ID_REV				(DMAC_BASE + 0x00)
/* DMAC Configuration Register*/
#define CFG				(DMAC_BASE + 0x10)
#define CH_NUM				GENMASK(3, 0)
#define DATA_WIDTH			GENMASK(25, 24)
#define DATA_WIDTH_OFF			24
#define REQSYNC				30
#define CTL				(DMAC_BASE + 0x20)
#define CH_ABT				(DMAC_BASE + 0x24)
/* Interrupt Status Register */
#define INT_STA				(DMAC_BASE + 0x30)
#define TC_OFFSET			16
#define ABT_OFFSET			8
#define ERR_OFFSET			0
#define	V5_DMA_TC(x)			(0x1 << (TC_OFFSET + (x)))
#define	V5_DMA_ABT(x)			(0x1 << (ABT_OFFSET + (x)))
#define	V5_DMA_ERR(x)			(0x1 << (ERR_OFFSET + (x)))
/* Channel Enable Register */
#define CH_EN				(DMAC_BASE + 0x34)
/* Channel Base Register */
#define DMAC_CH_OFFSET			0x40
#define CH_CTL_OFF			0x0
#define CH_SIZE_OFF			0x4
#define CH_SRC_LOW_OFF			0x8
#define CH_SRC_HIGH_OFF			0xc
#define CH_DST_LOW_OFF			0x10
#define CH_DST_HIGH_OFF			0x14
#define CH_LLP_LOW_OFF			0x18
#define CH_LLP_HIGH_OFF			0x1c
/* Channel x base addr */
#define	ch_regs(x)	(DMAC_CH_OFFSET + (x) * 0x20)


/* Channel n Control Register */
#define PRIORITY_SHIFT			29
#define PRIORITY_LOW			0
#define PRIORITY_HIGH			1
#define DMAC_CSR_CHPRI_0		PRIORITY_LOW
#define DMAC_CSR_CHPRI_1		PRIORITY_LOW
#define DMAC_CSR_CHPRI_2		PRIORITY_HIGH
#define DMAC_CSR_CHPRI_3		PRIORITY_HIGH

/* Maximum Buffer Transfer Size */
#define	ATC_BTSIZE_MAX		0xFFFFUL
/* Buffer Transfer Size */
#define	ATC_BTSIZE(x)		(ATC_BTSIZE_MAX & (x))
/* Source Chunk Transfer Size */
#define	ATC_SCSIZE_MASK		(0x7 << 16)
#define	ATC_SCSIZE(x)		(ATC_SCSIZE_MASK & ((x) << 16))
#define SBURST_SIZE_SHIFT	24
#define SBURST_SIZE_MASK	(0xf<<SBURST_SIZE_SHIFT)
#define	SBSIZE(x)		(SBURST_SIZE_MASK & ((x) << SBURST_SIZE_SHIFT))
#define DMAC_CSR_SIZE_1			(1 << 0)
#define DMAC_CSR_SIZE_2			(1 << 1)
#define DMAC_CSR_SIZE_4			(1 << 2)
#define DMAC_CSR_SIZE_8			(1 << 3)
#define DMAC_CSR_SIZE_16		(1 << 4)
#define DMAC_CSR_SIZE_32		(1 << 5)
#define DMAC_CSR_SIZE_64		(1 << 6)
#define DMAC_CSR_SIZE_128		(1 << 7)
#define DMAC_CSR_SIZE_256		(1 << 8)
#define DMAC_CSR_SIZE_512		(1 << 9)
#define DMAC_CSR_SIZE_1024		(1 << 10)
/* Source transfer width */
#define SRCWIDTH			21
#define SRCWIDTH_MASK			(0x7<<SRCWIDTH)
#define	SRC_WIDTH(x)			(SRCWIDTH_MASK & ((x) << SRCWIDTH))
#define WIDTH_1				0x0
#define WIDTH_2				0x1
#define WIDTH_4				0x2
#define WIDTH_8				0x3
#define WIDTH_16			0x4
#define WIDTH_32			0x5
#define DMAC_CSR_WIDTH_8		WIDTH_1
#define DMAC_CSR_WIDTH_16		WIDTH_2
#define DMAC_CSR_WIDTH_32		WIDTH_4
/* Destination transfer width */
#define DSTWIDTH			18
#define DSTWIDTH_MASK			(0x7<<DSTWIDTH)
#define	DST_WIDTH(x)			(DSTWIDTH_MASK & ((x) << DSTWIDTH))
/* Source DMA handshake mode */
#define SRCMODE				17
#define HANDSHAKE			1
#define SRC_HS				(HANDSHAKE<<SRCMODE)
/* Destination DMA handshake mode */
#define DSTMODE				16
#define DST_HS				(HANDSHAKE<<DSTMODE)
/* Source address control */
#define SRCADDRCTRL			14
#define SRCADDRCTRL_MASK		(0x3<<SRCADDRCTRL)
#define ADDR_INC			0x0
#define ADDR_DEC			0x1
#define ADDR_FIX			0x2
#define DMAC_CSR_AD_INC			ADDR_INC
#define DMAC_CSR_AD_DEC			ADDR_DEC
#define DMAC_CSR_AD_FIX			ADDR_FIX
/* Destination address control */
#define DSTADDRCTRL			12
#define DSTADDRCTRL_MASK		(0x3<<DSTADDRCTRL)
#define	SRC_ADDR_MODE_INCR	(0x0 << SRCADDRCTRL)
#define	SRC_ADDR_MODE_DECR	(0x1 << SRCADDRCTRL)
#define	SRC_ADDR_MODE_FIXED	(0x2 << SRCADDRCTRL)

#define	DST_ADDR_MODE_INCR	(0x0 << DSTADDRCTRL)
#define	DST_ADDR_MODE_DECR	(0x1 << DSTADDRCTRL)
#define	DST_ADDR_MODE_FIXED	(0x2 << DSTADDRCTRL)

/* Source DMA request select */
#define SRCREQSEL			8
#define SRCREQSEL_MASK			(0xf<<SRCREQSEL)
/* Destination DMA request select */
#define DSTREQSEL			4
#define DSTREQSEL_MASK			(0xf<<DSTREQSEL)


/* Channel abort interrupt mask */
#define INTABTMASK			BIT(3)
/* Channel error interrupt mask */
#define INTERRMASK			BIT(2)
/* Channel terminal count interrupt mask */
#define INTTCMASK			BIT(1)
/* Channel Enable */
#define CHEN				BIT(0)

/* Channel n Transfer Size Register */
/* total transfer size from source */
#define DMAC_TOT_SIZE_MASK          0xffffffff
struct v5_lli {
	u32	ctrl;
	u32	tranSize;
	u32	srcAddrl;
	u32	srcAddrh;
	u32	dstAddrl;
	u32	dstAddrh;
	u32	llPointerl;
	u32	llPointerh;
};

/**
 * struct v5_desc - software descriptor
 * @v5_lli: hardware lli structure
 * @txd: support for the async_tx api
 * @desc_node: node on the channed descriptors list
 * @len: descriptor byte count
 * @total_len: total transaction byte count
 */
struct v5_desc {
	/* FIRST values the hardware uses */
	struct v5_lli			lli;
	/* THEN values for driver housekeeping */
	struct list_head		tx_list;
	struct dma_async_tx_descriptor	txd;
	struct list_head		desc_node;
	size_t				len;
	size_t				total_len;
	/* Interleaved data */
	size_t				boundary;
	size_t				dst_hole;
	size_t				src_hole;
};

static inline struct v5_desc *
txd_to_v5_desc(struct dma_async_tx_descriptor *txd)
{
	return container_of(txd, struct v5_desc, txd);
}

/**
 * v5_status - information bits stored in channel status flag
 *
 * Manipulated with atomic operations.
 */
enum v5_status {
	V5_IS_TC	= 0,
	V5_IS_ERR	= 1,
	V5_IS_PAUSED	= 2,
	V5_IS_CYCLIC	= 24,
};

/**
 * struct v5_dma_chan - internal representation of an channel
 * @chan_common: common dmaengine channel object members
 * @device: parent device
 * @ch_regs: memory mapped register base
 * @mask: channel index in a mask
 * @req_num: request number
 * @status: transmit status information from irq/prep* functions
 *                to tasklet (use atomic operations)
 * @tasklet: bottom half to finish transaction work
 *             the cyclic list on suspend/resume cycle
 * @dma_sconfig: configuration for slave transfers, passed via
 * .device_config
 * @lock: serializes enqueue/dequeue operations to descriptors lists
 * @active_list: list of descriptors dmaengine is being running on
 * @queue: list of descriptors ready to be submitted to engine
 * @free_list: list of descriptors usable by the channel
 * @descs_allocated: records the actual size of the descriptor pool
 */
struct v5_dma_chan {
	struct dma_chan		chan_common;
	struct v5_dma		*device;
	void __iomem		*ch_regs;
	u8			mask;
	u8			req_num;
	unsigned long		status;
	struct tasklet_struct	tasklet;
	struct dma_slave_config dma_sconfig;
	spinlock_t		lock;
	/* these other elements are all protected by lock */
	struct list_head	active_list;
	struct list_head	queue;
	struct list_head	free_list;
	unsigned int		descs_allocated;
};

#define	v5_channel_readl(v5chan, name) \
	__raw_readl((v5chan)->ch_regs + name)

#define	v5_channel_writel(v5chan, name, val) \
	__raw_writel((val), (v5chan)->ch_regs + name)

static inline struct v5_dma_chan *to_v5_dma_chan(struct dma_chan *dchan)
{
	return container_of(dchan, struct v5_dma_chan, chan_common);
}

/*
 * Note fls(0) = 0, fls(1) = 1, fls(0x8) = 4.
 */
static inline void convert_burst(u32 *maxburst)
{
	*maxburst = fls(*maxburst) - 1;
}


/*
 * Fix sconfig's bus width
 * 1 byte -> 0, 2 bytes -> 1, 4 bytes -> 2.
 */
static inline u8 convert_buswidth(enum dma_slave_buswidth addr_width)
{
	switch (addr_width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		return 0;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		return 1;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		return 2;
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		return 3;
	case DMA_SLAVE_BUSWIDTH_16_BYTES:
		return 4;
	case DMA_SLAVE_BUSWIDTH_32_BYTES:
		return 5;
	default:
		/* fallback */
		return 0;
	}
}

struct v5_dma {
	struct dma_device	dma_common;
	void __iomem		*regs;
	u32			ctl;
	u8			data_width;
	u8			ch;
	u8			all_chan_mask;
	struct dma_pool		*dma_desc_pool;
	struct v5_dma_chan	chan[0];
};

#define	v5_dma_readl(v5_dma, name) \
	__raw_readl((v5_dma)->regs + name)
#define	v5_dma_writel(v5_dma, name, val) \
	__raw_writel((val), (v5_dma)->regs + name)

static inline struct v5_dma *to_v5_dma(struct dma_device *ddev)
{
	return container_of(ddev, struct v5_dma, dma_common);
}

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}

#if defined(VERBOSE_DEBUG)
static void vdbg_dump_regs(struct v5_dma_chan *v5chan)
{
	struct v5_dma	*v5_dma = to_v5_dma(v5chan->chan_common.device);

	dev_err(chan2dev(&v5chan->chan_common),
		"  channel %d : sta = 0x%x, en = 0x%x\n",
		v5chan->chan_common.chan_id,
		v5_dma_readl(v5_dma, INT_STA),
		v5_dma_readl(v5_dma, CH_EN));
	dev_err(chan2dev(&v5chan->chan_common),
		"  channel: s0x%x d0x%x ctrl0x%x:0x%x cfg0x%x l0x%x\n",
		v5_channel_readl(v5chan, CH_CTL_OFF),
		v5_channel_readl(v5chan, CH_SIZE_OFF));
}
#else
static void vdbg_dump_regs(struct v5_dma_chan *v5chan) {}
#endif

static void v5_setup_irq(struct v5_dma *v5dma, int chan_id, int on)
{

	struct v5_dma_chan	*v5chan = &v5dma->chan[chan_id];
	u32 chctl;

	chctl = v5_channel_readl(v5chan, CH_CTL_OFF);
	if (on)
		chctl &= ~(INTABTMASK|INTERRMASK|INTTCMASK);
	else
		chctl |= (INTABTMASK|INTERRMASK|INTTCMASK);
	v5_channel_writel(v5chan, CH_CTL_OFF, chctl);
}

static void v5_enable_chan_irq(struct v5_dma *v5dma, int chan_id)
{
	v5_setup_irq(v5dma, chan_id, 1);
}

static void v5_disable_chan_irq(struct v5_dma *v5dma, int chan_id)
{
	v5_setup_irq(v5dma, chan_id, 0);
}

/**
 * v5_chan_is_enabled - test if given channel is enabled
 * @v5chan: channel we want to test status
 */
static inline int v5_chan_is_enabled(struct v5_dma_chan *v5chan)
{
	struct v5_dma	*v5dma = to_v5_dma(v5chan->chan_common.device);

	return !!(v5_dma_readl(v5dma, CH_EN) & v5chan->mask);
}

/**
 * v5_chan_is_paused - test channel pause/resume status
 * @v5chan: channel we want to test status
 */
static inline int v5_chan_is_paused(struct v5_dma_chan *v5chan)
{
	return 0;
}
#endif /* ATCDMAC300G_H */
