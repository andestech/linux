/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Andes Technology Corporation
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/sizes.h>
#include <asm/types.h>
#include <asm/io.h>
#include <asm/dmad.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/math64.h>
#include <asm/div64.h>

resource_size_t	dmac_base;

static inline addr_t REG_READ(unsigned long r)
{
	return readl((volatile void __iomem *) (unsigned long) r);
}

static inline void REG_WRITE(addr_t d, unsigned long r)
{
	writel(d, (volatile void __iomem *) (unsigned long) r);
}

#if (defined(CONFIG_PLATFORM_AHBDMA))
#define DMAD_AHB_MAX_CHANNELS           DMAC_MAX_CHANNELS

#define DMAD_DRB_POOL_SIZE              32	/* 128 */


static inline addr_t din(unsigned long r)
{
	return REG_READ(r);
}

static inline void dout(addr_t d, unsigned long r)
{
	REG_WRITE(d,r);
}

/* reg/io supplementals */

static void setbl(addr_t bit, unsigned long reg)
{
	REG_WRITE(REG_READ(reg) | (addr_t) ((addr_t) 1 << bit), reg);
}

static inline void clrbl(addr_t bit, unsigned long reg)
{
	REG_WRITE(REG_READ(reg) & (~((addr_t) ((addr_t) 1 << bit))), reg);
}

static inline addr_t getbl(addr_t bit, unsigned long reg)
{
 return REG_READ(reg) & (addr_t) ((addr_t) 1 << bit);
}
/******************************************************************************/

enum DMAD_DRQ_FLAGS {
	DMAD_DRQ_STATE_READY = 0x00000001,	/* channel allocation status */
	DMAD_DRQ_STATE_ABORT = 0x00000002,	/* abort drb alloc block-wait */
	DMAD_DRQ_DIR_A1_TO_A0 = 0x00000004,	/* Transfer direction */
};

#define DMAD_DRQ_DIR_MASK  DMAD_DRQ_DIR_A1_TO_A0

/* DMA request queue, one instance per channel */
typedef struct dmad_drq {
	u32 state;		/* enum DMAD_DRQ_STATE */

	unsigned long channel_base;	/* register base address */
	unsigned long enable_port;	/* enable register */
	unsigned long src_port;	/* source address register */
	unsigned long dst_port;	/* dest address register */
	unsigned long cyc_port;	/* size(cycle) register */

	u32 flags;		/* enum DMAD_CHREQ_FLAGS */

	spinlock_t drb_pool_lock;
	dmad_drb *drb_pool;	/* drb pool */

	unsigned long fre_head;		/* free list head */
	unsigned long fre_tail;		/* free list tail */

	unsigned long rdy_head;		/* ready list head */
	unsigned long rdy_tail;		/* ready list tail */

	unsigned long sbt_head;		/* submitted list head */
	unsigned long sbt_tail;		/* submitted list tail */

	u32 data_width;		/* dma transfer data width */

	struct completion drb_alloc_sync;

	/* client supplied callback function, executed in interrupt context
	 * client private data to be passed to data argument of completion_cb().
	 */
	void (*completion_cb) (int channel, u16 status, void *data);
	void *completion_data;

	/* ring-mode fields are valid for DMAD_FLAGS_RING_MODE */
	dma_addr_t ring_base;	/* ring buffer base address */
	int ring_size;	/* size (of data width) */
	unsigned long ring_port;	/* for setup/fetch hw_ptr */
	dmad_drb *ring_drb;

	addr_t dev_addr;	/* device data port */

	int periods;		/* interrupts periods */
	int period_size;	/* of dma data with */
	dma_addr_t period_bytes;	/* Period size, in bytes */

	/* ring_size - period_size * periods */
	dma_addr_t remnant_size;

	dma_addr_t sw_ptr;	/* sw pointer */
	int sw_p_idx;		/* current ring_ptr */
	dma_addr_t sw_p_off;	/* offset to period base */

} dmad_drq;

static inline void dmad_enable_channel(dmad_drq * drq)
{
	setbl(CHEN, drq->enable_port);
}

static inline void dmad_disable_channel(dmad_drq * drq)
{
	clrbl(CHEN, drq->enable_port);
}

static inline addr_t dmad_is_channel_enabled(dmad_drq * drq)
{
	return (addr_t) getbl(CHEN, drq->enable_port);
}

/* system irq number (per channel, ahb) */
static const unsigned int ahb_irqs[DMAD_AHB_MAX_CHANNELS] = {
	DMA_IRQ0,
	DMA_IRQ1,
	DMA_IRQ2,
	DMA_IRQ3,
	DMA_IRQ4,
	DMA_IRQ5,
	DMA_IRQ6,
	DMA_IRQ7,
};

/* Driver data structure, one instance per system */
typedef struct DMAD_DATA_STRUCT {
	/* Driver data initialization flag */

	/* DMA queue pool access control object */
	spinlock_t drq_pool_lock;

	/* DMA queue base address, to ease alloc/free flow */
	dmad_drq *drq_pool;
	/* DMA queue for AHB DMA channels */
	dmad_drq *ahb_drq_pool;
	void *plat;
} DMAD_DATA;

/* Driver data structure instance, one instance per system */

static DMAD_DATA dmad __attribute__ ((aligned(8))) = {

	.drq_pool_lock = __SPIN_LOCK_UNLOCKED(dmad.drq_pool_lock),
	.drq_pool = 0,
	.ahb_drq_pool = 0,
	.plat = 0,
};

/**
 * dmad_next_drb - static function
 * @drb_pool : [in] The raw DRB pool of a DMA channel
 * @node     : [in] The node number to lookup its next node
 * @drb      : [out] The drb next to the "node" node number
 *
 * Lookup next DRB of the specified node number. "drb" is null if reaches end
 * of the list.
 */
static inline void dmad_next_drb(dmad_drb * drb_pool, u32 node, dmad_drb ** drb)
{
	if (likely(drb_pool[node].next != 0))
		*drb = &drb_pool[drb_pool[node].next];
	else
		*drb = 0;
}

/**
 * dmad_prev_drb - static function
 * @drb_pool : [in] The raw DRB pool of a DMA channel
 * @node     : [in] The node number to lookup its previous node
 * @drb      : [out] The drb previous to the "node" node number
 *
 * Lookup previous DRB of the specified node number. "drb" is null if reaches
 * head-end of the list.
 */
static inline void dmad_prev_drb(dmad_drb * drb_pool, u32 node, dmad_drb ** drb)
{
	if (unlikely(drb_pool[node].prev != 0))
		*drb = &drb_pool[drb_pool[node].prev];
	else
		*drb = 0;
}

/**
 * dmad_detach_node - static function
 * @drb_pool : [in] The raw DRB pool of a DMA channel
 * @head     : [in/out] Reference to the head node number
 * @tail     : [in/out] Reference to the tail node number
 * @node     : [in] The node to be dettached from the queue
 *
 * Detached a DRB specified by the node number from the queue.  The head and
 * tail records will be updated accordingly.
 */
static inline void dmad_detach_node(dmad_drb * drb_pool,
				    unsigned long * head, unsigned long * tail, u32 node)
{
	if (likely(drb_pool[node].prev != 0)) {
		/* prev->next = this->next (= 0, if this is a tail) */
		drb_pool[drb_pool[node].prev].next = drb_pool[node].next;
	} else {
		/* this node is head, move head to next node
		 * (= 0, if this is the only one node) */
		*head = drb_pool[node].next;
	}

	if (unlikely(drb_pool[node].next != 0)) {
		/* next->prev = this->prev (= 0, if this is a head) */
		drb_pool[drb_pool[node].next].prev = drb_pool[node].prev;
	} else {
		/* this node is tail, move tail to previous node
		 * (= 0, if this is the only one node) */
		*tail = drb_pool[node].prev;
	}

	drb_pool[node].prev = drb_pool[node].next = 0;
}

/**
 * dmad_detach_head - static function
 * @drb_pool : [in] The raw DRB pool of a DMA channel
 * @head     : [in/out] Reference to the head node number
 * @tail     : [in/out] Reference to the tail node number
 * @drb      : [out] The detached head node; null if the queue is empty
 *
 * Detached a DRB from the head of the queue.  The head and tail records will
 * be updated accordingly.
 */
static inline void dmad_detach_head(dmad_drb * drb_pool,
				    unsigned long * head, unsigned long * tail, dmad_drb ** drb)
{
	if (unlikely(*head == 0)) {
		*drb = NULL;
		return;
	}

	*drb = &drb_pool[*head];

	if (likely((*drb)->next != 0)) {
		/* next->prev = this->prev (= 0, if this is a head) */
		drb_pool[(*drb)->next].prev = 0;

		/* prev->next = this->next (do nothing, if this is a head) */

		/* head = this->next */
		*head = (*drb)->next;
	} else {
		/* head = tail = 0 */
		*head = 0;
		*tail = 0;
	}

	/* this->prev = this->next = 0 (do nothing, if save code size) */
	(*drb)->prev = (*drb)->next = 0;
}

/**
 * dmad_get_head - static function
 * @drb_pool : [in] The raw DRB pool of a DMA channel
 * @head     : [in/out] Reference to the head node number
 * @tail     : [in/out] Reference to the tail node number
 * @drb      : [out] The head node; null if the queue is empty
 *
 * Get a DRB from the head of the queue.  The head and tail records remain
 * unchanged.
 */
static inline void dmad_get_head(dmad_drb * drb_pool, const unsigned long * head,
				 const unsigned long * tail, dmad_drb ** drb)
{
	if (unlikely(*head == 0)) {
		*drb = NULL;
		return;
	}

	*drb = &drb_pool[*head];
}

/**
 * dmad_detach_tail - static function
 * @drb_pool : [in] The raw DRB pool of a DMA channel
 * @head     : [in/out] Reference to the head node number
 * @tail     : [in/out] Reference to the tail node number
 * @drb      : [out] The tail node; null if the queue is empty
 *
 * Detached a DRB from the head of the queue.  The head and tail records will
 * be updated accordingly.
 */
static inline void dmad_detach_tail(dmad_drb * drb_pool,
				    unsigned long * head, unsigned long * tail, dmad_drb ** drb)
{
	if (unlikely(*tail == 0)) {
		*drb = NULL;
		return;
	}

	*drb = &drb_pool[*tail];

	if (likely((*drb)->prev != 0)) {
		/* prev->next = this->next (= 0, if this is a tail) */
		drb_pool[(*drb)->prev].next = 0;

		/* next->prev = this->prev (do nothing, if this is a tail) */

		/* tail = this->prev */
		*tail = (*drb)->prev;
	} else {
		/* head = tail = 0 */
		*head = 0;
		*tail = 0;
	}

	/* this->next = this->prev = 0 (do nothing, if save code size) */
	(*drb)->prev = (*drb)->next = 0;
}

/**
 * dmad_get_tail - static function
 * @drb_pool : [in] The raw DRB pool of a DMA channel
 * @head     : [in/out] Reference to the head node number
 * @tail     : [in/out] Reference to the tail node number
 * @drb      : [out] The tail node; null if the queue is empty
 *
 * Get a DRB from the tail of the queue.  The head and tail records remain
 * unchanged.
 */
static inline void dmad_get_tail(dmad_drb * drb_pool,
				 unsigned long * head, unsigned long * tail, dmad_drb ** drb)
{
	if (unlikely(*tail == 0)) {
		*drb = NULL;
		return;
	}

	*drb = &drb_pool[*tail];
}

/**
 * dmad_attach_head - static function
 * @drb_pool : [in] The raw DRB pool of a DMA channel
 * @head     : [in/out] Reference to the head node number
 * @tail     : [in/out] Reference to the tail node number
 * @node     : [in] The node to be attached
 *
 * Attach a DRB node to the head of the queue.  The head and tail records will
 * be updated accordingly.
 */
static inline void dmad_attach_head(dmad_drb * drb_pool,
				    unsigned long * head, unsigned long * tail, u32 node)
{
	if (likely(*head != 0)) {
		/* head->prev = this */
		drb_pool[*head].prev = node;

		/* this->next = head */
		drb_pool[node].next = *head;
		/* this->prev = 0 */
		drb_pool[node].prev = 0;

		/* head = node */
		*head = node;
	} else {
		/* head = tail = node */
		*head = *tail = node;
		drb_pool[node].prev = drb_pool[node].next = 0;
	}
}

/**
 * dmad_attach_head - static function
 * @drb_pool : [in] The raw DRB pool of a DMA channel
 * @head     : [in/out] Reference to the head node number
 * @tail     : [in/out] Reference to the tail node number
 * @node     : [in] The node to be attached
 *
 * Attach a DRB node to the tail of the queue.  The head and tail records will
 * be updated accordingly.
 */
static inline void dmad_attach_tail(dmad_drb * drb_pool,
				    unsigned long * head, unsigned long * tail, u32 node)
{
	if (likely(*tail != 0)) {
		/* tail->next = this */
		drb_pool[*tail].next = node;

		/* this->prev = tail */
		drb_pool[node].prev = *tail;
		/* this->next = 0 */
		drb_pool[node].next = 0;

		/* tail = node */
		*tail = node;
	} else {
		/* head = tail = node */
		*head = *tail = node;
		drb_pool[node].prev = drb_pool[node].next = 0;
	}
}

#ifdef CONFIG_PLATFORM_AHBDMA

/**
 * dmad_ahb_isr - AHB DMA interrupt service routine
 *
 * @irq    : [in] The irq number
 * @dev_id : [in] The identifier to identify the asserted channel
 *
 * This is the ISR that services all AHB DMA channels.
 */
static irqreturn_t dmad_ahb_isr(int irq, void *dev_id)
{
	dmad_drq *drq;
	dmad_drb *drb, *drb_iter;
	u32 channel = ((unsigned long) dev_id) - 1;
	u8 tc_int = 0;
	u8 err_int = 0;
	u8 abt_int = 0;
	u8 cpl_events = 1;

	dmad_dbg("%s() >> channel(%d)\n", __func__, channel);

	if (channel >= DMAD_AHB_MAX_CHANNELS) {
		dmad_err("%s() invlaid channel number: %d!\n",
			 __func__, channel);
		return IRQ_HANDLED;
	}

	/* Fetch channel's DRQ struct (DMA Request Queue) */
	drq = (dmad_drq *) & dmad.ahb_drq_pool[channel];

	/* Check DMA status register to get channel number */
	if (likely(getbl(channel+TC_OFFSET, (unsigned long)INT_STA))) {

		/* Mark as TC int */
		tc_int = 1;

		/* DMAC INT TC status clear */
		setbl(channel+TC_OFFSET, (unsigned long)INT_STA);

	} else if (getbl(channel+ERR_OFFSET, (unsigned long)INT_STA)) {

		/* Mark as ERR int */
		err_int = 1;

		/* DMAC INT ERR status clear */
		setbl(channel+ERR_OFFSET, (unsigned long)INT_STA);

	} else if (getbl(channel+ABT_OFFSET, (unsigned long)INT_STA)) {

		/* Mark as ABT int */
		abt_int = 1;

		/* DMAC INT ABT status clear */
		setbl(channel+INT_STA, (unsigned long)INT_STA);

	} else {

		dmad_err("%s() possible false-fired ahb dma int,"
			 "channel %d status-reg: status(0x%08x)\n",
			 __func__, channel,din((unsigned long)INT_STA));

		/* Stop DMA channel (make sure the channel will be stopped) */
		clrbl(CH_EN, drq->enable_port);
		return IRQ_HANDLED;
	}

	/* DMAC
	 * Stop DMA channel temporarily */
	dmad_disable_channel(drq);

	spin_lock(&drq->drb_pool_lock);

	/* Lookup/detach latest submitted DRB (DMA Request Block) from
	 * the DRQ (DMA Request Queue), so ISR could kick off next DRB */
	dmad_detach_head(drq->drb_pool, &drq->sbt_head, &drq->sbt_tail, &drb);
	if (drb == NULL) {
		spin_unlock(&drq->drb_pool_lock);
		/* submitted list could be empty if client cancel all requests
		 * of the channel. */
		return IRQ_HANDLED;
	}

	/* release blocking of drb-allocation, if any ... */
	if (unlikely((drq->fre_head == 0) &&
		     (drq->flags & DMAD_FLAGS_SLEEP_BLOCK))) {
		complete_all(&drq->drb_alloc_sync);
	}

	/* Process DRBs according to interrupt reason */
	if (tc_int) {

		dmad_dbg("dma finish\n");

		dmad_dbg("finish drb(%d 0x%08x) addr0(0x%08x) "
			 "addr1(0x%08x) size(0x%08x)\n",
			 drb->node, (u32) drb, drb->src_addr,
			 drb->dst_addr, drb->req_cycle);

		if (drb->req_cycle == 0)
			cpl_events = 0;

		// Mark DRB state as completed
		drb->state = DMAD_DRB_STATE_COMPLETED;
		if (cpl_events && drb->sync)
			complete_all(drb->sync);

		dmad_attach_tail(drq->drb_pool, &drq->fre_head,
				 &drq->fre_tail, drb->node);

		// Check whether there are pending requests in the DRQ
		if (drq->sbt_head != 0) {

			// Lookup next DRB (DMA Request Block)
			drb_iter = &drq->drb_pool[drq->sbt_head];

			dmad_dbg("exec drb(%d 0x%08x) addr0(0x%08x) "
				 "addr1(0x%08x) size(0x%08x)\n",
				 drb_iter->node, (u32) drb_iter,
				 drb_iter->src_addr, drb_iter->dst_addr,
				 drb_iter->req_cycle);

			// Kick-off DMA for next DRB
			// - Source and destination address
			if (drq->flags & DMAD_DRQ_DIR_A1_TO_A0) {
				dout(drb_iter->addr1, (unsigned long)drq->src_port);
				dout(drb_iter->addr0, (unsigned long)drq->dst_port);
			} else {
				dout(drb_iter->addr0, (unsigned long)drq->src_port);
				dout(drb_iter->addr1, (unsigned long)drq->dst_port);
			}

			/* - Transfer size (in units of source width) */
			dout(drb_iter->req_cycle, (unsigned long)drq->cyc_port);

			/* Kick off next request */
			dmad_enable_channel(drq);

			drb_iter->state = DMAD_DRB_STATE_EXECUTED;

		} else {
			/* No pending requests, keep the DMA channel stopped */
		}

	} else {

		dmad_err("%s() ahb dma channel %d error!\n", __func__, channel);

		/* Zero out src, dst, and size */
		dout(0, (unsigned long)drq->src_port);
		dout(0, (unsigned long)drq->dst_port);
		dout(0, (unsigned long)drq->cyc_port);

		/* Remove all pending requests in the queue */
		drb_iter = drb;
		while (drb_iter) {

			dmad_err("abort drb ");

			if (drb_iter->req_cycle == 0)
				cpl_events = 0;

			/* Mark DRB state as abort */
			drb_iter->state = DMAD_DRB_STATE_ABORT;

			if (cpl_events && drb_iter->sync)
				complete_all(drb_iter->sync);

			dmad_attach_tail(drq->drb_pool, &drq->fre_head,
					 &drq->fre_tail, drb_iter->node);

			/* Detach next submitted DRB (DMA Request Block)
			 * from the DRQ (DMA Request Queue) */
			dmad_detach_head(drq->drb_pool, &drq->sbt_head,
					 &drq->sbt_tail, &drb_iter);
		}
	}

	spin_unlock(&drq->drb_pool_lock);

	/* dispatch interrupt-context level callbacks */
	if (cpl_events && drq->completion_cb) {
		/* signal DMA driver that new node is available */
		drq->completion_cb(channel, tc_int, drq->completion_data);
	}

	dmad_dbg("%s() <<\n", __func__);

	return IRQ_HANDLED;
}

/**
 * dmad_ahb_config_dir - prepare command reg according to tx direction
 * @ch_req       : [in] Reference to the DMA request descriptor structure
 * @channel_cmds : [out] Reference to array of command words to be prepared with
 * @return       : none
 *
 * Prepare command registers according to transfer direction ...
 *   channel_cmd[0]  DMAC_CSR
 *   channel_cmd[1]  DMAC_CFG
 *
 * This function only serves as local helper.  No protection wrappers.
 */
static void dmad_ahb_config_dir(dmad_chreq * ch_req, unsigned long * channel_cmds)
{
	dmad_drq *drq = (dmad_drq *) ch_req->drq;
	dmad_ahb_chreq *ahb_req = (dmad_ahb_chreq *) (&ch_req->ahb_req);
	channel_control ch_ctl;
	dmad_dbg("%s() channel_cmds(0x%08x)\n",__func__, channel_cmds[0]);
	channel_cmds[0] &= ~(u32)(SRCWIDTH_MASK|SRCADDRCTRL_MASK|
		DSTWIDTH_MASK|DSTADDRCTRL_MASK|
		SRC_HS|DST_HS|SRCREQSEL_MASK|DSTREQSEL_MASK);

	if (ahb_req->tx_dir == 0) {
		dmad_dbg("%s() addr0 --> addr1\n", __func__);
		memcpy((u8 *)&ch_ctl.sWidth,(u8 *)&ahb_req->addr0_width,12);
		memcpy((u8 *)&ch_ctl.dWidth,(u8 *)&ahb_req->addr1_width,12);
		drq->flags &= ~(addr_t) DMAD_DRQ_DIR_A1_TO_A0;
	}else{
		dmad_dbg("%s() addr0 <-- addr1\n", __func__);
		memcpy((u8 *)&ch_ctl.sWidth,(u8 *)&ahb_req->addr1_width,12);
		memcpy((u8 *)&ch_ctl.dWidth,(u8 *)&ahb_req->addr0_width,12);
		drq->flags |= (addr_t) DMAD_DRQ_DIR_A1_TO_A0;
	}
	channel_cmds[0] |=(((ch_ctl.sWidth << SRCWIDTH) &SRCWIDTH_MASK) |
		((ch_ctl.sCtrl << SRCADDRCTRL) &SRCADDRCTRL_MASK) |
		((ch_ctl.dWidth << DSTWIDTH) &DSTWIDTH_MASK) |
		((ch_ctl.dCtrl << DSTADDRCTRL) &DSTADDRCTRL_MASK));
	drq->data_width = ch_ctl.sWidth;
	if (likely(ahb_req->hw_handshake != 0)) {
		if (ch_ctl.sReqn != DMAC_REQN_NONE)
		{
			channel_cmds[0] |= (SRC_HS |
				((ch_ctl.sReqn <<SRCREQSEL)&SRCREQSEL_MASK));
		}
		if (ch_ctl.dReqn != DMAC_REQN_NONE)
		{
			channel_cmds[0] |= (DST_HS |
				((ch_ctl.dReqn <<DSTREQSEL)&DSTREQSEL_MASK));
		}
	}
	dmad_dbg("%s() channel_cmds(0x%08x)\n",
		 __func__, channel_cmds[0]);
}

/**
 * dmad_ahb_init - initialize a ahb dma channel
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @return : 0 if success, non-zero if any error
 *
 * Register AHB DMA ISR and performs hw initialization for the given DMA
 * channel.
 */
static int dmad_ahb_init(dmad_chreq * ch_req)
{
	int err = 0;
	dmad_drq *drq = (dmad_drq *) ch_req->drq;
	dmad_ahb_chreq *ahb_req = (dmad_ahb_chreq *) (&ch_req->ahb_req);
	u32 channel = (u32) ch_req->channel;

	unsigned long channel_base = drq->channel_base;
	addr_t channel_cmds[1];
	unsigned long lock_flags;
	dmad_dbg("%s()\n", __func__);
	/* register interrupt handler */
	err = request_irq(ahb_irqs[channel], dmad_ahb_isr, 0,
			  "AHB_DMA", (void *)(unsigned long)(channel + 1));
	if (unlikely(err != 0)) {
		dmad_err("unable to request IRQ %d for AHB DMA "
			 "(error %d)\n", ahb_irqs[channel], err);
		free_irq(ahb_irqs[channel], (void *)(unsigned long)(channel + 1));
		return err;
	}
	spin_lock_irqsave(&dmad.drq_pool_lock, lock_flags);

	/* - INT TC/ERR/ABT status clear */
	setbl(channel+TC_OFFSET, (unsigned long)INT_STA);
	setbl(channel+ABT_OFFSET, (unsigned long)INT_STA);
	setbl(channel+ERR_OFFSET, (unsigned long)INT_STA);

	/* - SYNC */
	if (ahb_req->sync != (getbl(REQSYNC, CFG)>>REQSYNC))
	{
		printk("sync configuration error !\n");
		return EINVAL;
	}
	if(ahb_req->priority > PRIORITY_HIGH)
		ahb_req->priority = PRIORITY_HIGH;

	channel_cmds[0] = (ahb_req->priority << PRIORITY_SHIFT);
	channel_cmds[0] |= (ahb_req->burst_size << SBURST_SIZE_SHIFT) &
	    SBURST_SIZE_MASK;

	if (0 ==
	    (ch_req->flags & (DMAD_FLAGS_RING_MODE | DMAD_FLAGS_BIDIRECTION)))
		ahb_req->tx_dir = 0;

	dmad_ahb_config_dir(ch_req, (unsigned long *)channel_cmds);
	dout(channel_cmds[0], (unsigned long)drq->enable_port);

	/* SRCADR and DESADR */
	dout(0, (unsigned long) drq->src_port);
	dout(0, (unsigned long) drq->dst_port);
	/* CYC (transfer size) */
	dout(0, (unsigned long) drq->cyc_port);
	/* LLP */
	dout(0, (unsigned long)channel_base + CH_LLP_LOW_OFF);

	/* TOT_SIZE - not now */
	spin_unlock_irqrestore(&dmad.drq_pool_lock, lock_flags);

	return err;
}

#endif /* CONFIG_PLATFORM_AHBDMA */
/**
 * dmad_channel_init - initialize given dma channel
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @return : 0 if success, non-zero if any error
 *
 * This function serves as the abstraction layer of dmad_ahb_init()
 * and dmad_apb_init() functions.
 */
static int dmad_channel_init(dmad_chreq * ch_req)
{
	int err = 0;

	dmad_dbg("%s()\n", __func__);

	if (unlikely(ch_req == NULL))
		return -EFAULT;

	if (unlikely(ch_req->drq == NULL))
		return -EBADR;

	/* Initialize DMA controller */
	if (ch_req->controller == DMAD_DMAC_AHB_CORE)
		err = dmad_ahb_init(ch_req);
	return err;
}

static inline void dmad_reset_channel(dmad_drq * drq)
{
	/* disable dma controller */
	dmad_disable_channel(drq);

	/* Source and destination address */
	dout(0, (unsigned long)drq->src_port);
	dout(0, (unsigned long)drq->dst_port);

	/* Transfer size (in units of source width) */
	dout(0, (unsigned long)drq->cyc_port);
}

/**
 * dmad_channel_reset - reset given dma channel
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @return : 0 if success, non-zero if any error
 *
 * This function serves as the abstraction layer of dmad_ahb_reset()
 * and dmad_apb_reset() functions.
 */
static int dmad_channel_reset(dmad_chreq * ch_req)
{
	u32 channel = (u32) ch_req->channel;
	unsigned long lock_flags;
	int err = 0;

	dmad_dbg("%s()\n", __func__);

	if (unlikely(ch_req == NULL))
		return -EFAULT;

	if (unlikely(ch_req->drq == NULL))
		return -EBADR;

	spin_lock_irqsave(&((dmad_drq *) ch_req->drq)->drb_pool_lock,
			  lock_flags);

	/* stop DMA channel */
	dmad_reset_channel((dmad_drq *) ch_req->drq);

	spin_unlock_irqrestore(&((dmad_drq *) ch_req->drq)->drb_pool_lock,
			       lock_flags);

	/* unregister interrupt handler */
	if (ch_req->controller == DMAD_DMAC_AHB_CORE)
		free_irq(ahb_irqs[channel], (void *)(unsigned long)(channel + 1));

	return err;
}

/**
 * dmad_channel_alloc - allocates and initialize a dma channel
 * @ch_req : [in/out] Reference to the DMA request descriptor structure
 * @return : 0 if success, non-zero if any error
 *
 * This function allocates a DMA channel according to client's request
 * parameters.  ISR and HW state will also be initialized accordingly.
 */
int dmad_channel_alloc(dmad_chreq * ch_req)
{
	dmad_drq *drq_iter = NULL;
	dmad_drb *drb_iter;
	int err = 0;
	u32 i = 0;
	dmad_dbg("%s()\n", __func__);

	if (ch_req == NULL) {
		printk(KERN_ERR "%s() invalid argument!\n", __func__);
		return -EFAULT;
	}

	spin_lock(&dmad.drq_pool_lock);

	/* locate an available DMA channel */
	if (ch_req->controller == DMAD_DMAC_AHB_CORE) {

		drq_iter = dmad.ahb_drq_pool;

		if ((ch_req->ahb_req.src_reqn != DMAC_REQN_NONE) ||
		    (ch_req->ahb_req.dst_reqn != DMAC_REQN_NONE)) {
			/* [2007-12-03] It looks current board have problem to
			 * do dma traffic for APB devices on DMAC channel 0/1.
			 * Redirect all APB devices to start from channel 2.
			 */

			/* [todo] include USB controller ? */
			drq_iter = &dmad.ahb_drq_pool[2];
			for (i = 2; i < DMAD_AHB_MAX_CHANNELS; ++i, ++drq_iter) {
				if (!(drq_iter->state & DMAD_DRQ_STATE_READY))
					break;
			}
		} else {
			/* channel for other devices is free to allocate */
			for (i = 0; i < DMAD_AHB_MAX_CHANNELS; ++i, ++drq_iter) {
				if (!(drq_iter->state & DMAD_DRQ_STATE_READY))
					break;
			}
		}
		if (unlikely(i == DMAD_AHB_MAX_CHANNELS)) {
			spin_unlock(&dmad.drq_pool_lock);
			dmad_err("out of available channels (AHB DMAC)!\n");
			return -ENOSPC;
		}

		dmad_dbg("allocated channel: %d (AHB DMAC)\n", i);
	}

	if (drq_iter == NULL) {
		spin_unlock(&dmad.drq_pool_lock);
		printk(KERN_ERR "%s() invalid argument!\n", __func__);
		return -EFAULT;
	}

	spin_unlock(&dmad.drq_pool_lock);
	memset(drq_iter, 0, sizeof(dmad_drq));

	/* Initialize DMA channel's DRB pool as list of free DRBs */
	drq_iter->drb_pool =
	    kmalloc(DMAD_DRB_POOL_SIZE * sizeof(dmad_drb), GFP_ATOMIC);

	if (drq_iter->drb_pool == NULL) {
		printk(KERN_ERR "%s() failed to allocate drb pool!\n",
		       __func__);
		return -ENOMEM;
	}

	/* Allocate the DMA channel */
	drq_iter->state = DMAD_DRQ_STATE_READY;
	drq_iter->flags = ch_req->flags;

	/* Initialize synchronization object for DMA queue access control */
	spin_lock_init(&drq_iter->drb_pool_lock);

	/* Initialize synchronization object for free drb notification */
	init_completion(&drq_iter->drb_alloc_sync);

	/* Record the channel number in client's struct */
	ch_req->channel = i;
	/* Record the channel's queue handle in client's struct */
	ch_req->drq = drq_iter;

	if (ch_req->controller == DMAD_DMAC_AHB_CORE) {
		drq_iter->channel_base = (unsigned long) DMAC_BASE_CH(i);
		drq_iter->enable_port = (unsigned long) CH_CTL(i);
		drq_iter->src_port = (unsigned long) CH_SRC_L(i);
		drq_iter->dst_port = (unsigned long) CH_DST_L(i);
		drq_iter->cyc_port = (unsigned long) CH_SIZE(i);
	}
	/* drb-0 is an invalid node - for node validation */
	drb_iter = &drq_iter->drb_pool[0];
	drb_iter->prev = 0;
	drb_iter->next = 0;
	drb_iter->node = 0;
	++drb_iter;

	/* init other drbs - link in order */
	for (i = 1; i < DMAD_DRB_POOL_SIZE; ++i, ++drb_iter) {
		drb_iter->prev = i - 1;
		drb_iter->next = i + 1;
		drb_iter->node = i;
	}
	drq_iter->drb_pool[DMAD_DRB_POOL_SIZE - 1].next = 0;

	/* Initialize channel's DRB free-list, ready-list, and submitted-list */
	drq_iter->fre_head = 1;
	drq_iter->fre_tail = DMAD_DRB_POOL_SIZE - 1;
	drq_iter->rdy_head = drq_iter->rdy_tail = 0;
	drq_iter->sbt_head = drq_iter->sbt_tail = 0;

	/* initialize ring buffer mode resources */
	if (ch_req->flags & DMAD_FLAGS_RING_MODE) {

		int remnant = (int)ch_req->ring_size -
		    (int)ch_req->periods * (int)ch_req->period_size;
		if (remnant == 0) {
			drq_iter->periods = ch_req->periods;
		} else if (remnant > 0) {
			drq_iter->periods = ch_req->periods;	// + 1;
		} else {
			dmad_err("%s() Error - buffer_size < "
				 "periods * period_size!\n", __func__);
			err = -EFAULT;
			goto _err_exit;
		}

		drq_iter->ring_size = ch_req->ring_size;
		drq_iter->period_size = ch_req->period_size;
		drq_iter->remnant_size = (dma_addr_t) remnant;

		drq_iter->ring_base = (dma_addr_t) ch_req->ring_base;
		drq_iter->dev_addr = (dma_addr_t) ch_req->dev_addr;

		if (ch_req->controller == DMAD_DMAC_AHB_CORE) {
			if ((ch_req->ahb_req.ring_ctrl == DMAC_CSR_AD_DEC) ||
			    (ch_req->ahb_req.dev_ctrl == DMAC_CSR_AD_DEC)) {
				dmad_err("%s() Error - decremental"
					 " addressing DMA is not supported in"
					 " ring mode currently!\n", __func__);
				err = -EFAULT;
				goto _err_exit;
			}

			if (ch_req->ahb_req.ring_ctrl == DMAC_CSR_AD_FIX) {
				dmad_err("%s() Error - ring address control is "
					 "fixed in ring DMA mode!\n", __func__);
				err = -EFAULT;
				goto _err_exit;
			}

			drq_iter->period_bytes =
			    DMAC_CYCLE_TO_BYTES(ch_req->period_size,
						ch_req->ahb_req.ring_width);

			/* 0 - addr0 to addr1; 1 - addr1 to addr0 */
			if (ch_req->ahb_req.tx_dir == 0)
				drq_iter->ring_port =(unsigned long) drq_iter->src_port;
			else
				drq_iter->ring_port =(unsigned long) drq_iter->dst_port;

		}

		dmad_dbg("%s() ring: base(0x%08x) port(0x%08x) periods(0x%08x)"
			 " period_size(0x%08x) period_bytes(0x%08x)"
			 " remnant_size(0x%08x)\n",
			 __func__, drq_iter->ring_base, drq_iter->ring_port,
			 drq_iter->periods, drq_iter->period_size,
			 drq_iter->period_bytes, drq_iter->remnant_size);
	}

	drq_iter->completion_cb = ch_req->completion_cb;
	drq_iter->completion_data = ch_req->completion_data;

	/* Initialize the channel && register isr */
	err = dmad_channel_init(ch_req);

_err_exit:

	if (err != 0) {
		spin_lock(&dmad.drq_pool_lock);

		kfree(drq_iter->drb_pool);
		memset(drq_iter, 0, sizeof(dmad_drq));

		ch_req->channel = -1;
		ch_req->drq = (void *)0;

		spin_unlock(&dmad.drq_pool_lock);

		dmad_err("Failed to initialize APB DMA! "
			 "Channel allocation aborted!\n");
	}

	return err;
}

EXPORT_SYMBOL_GPL(dmad_channel_alloc);

/**
 * dmad_channel_free - release a dma channel
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @return : 0 if success, non-zero if any error
 *
 * This function releases a DMA channel.  The channel is available for future
 * allocation after the invokation.
 */
int dmad_channel_free(dmad_chreq * ch_req)
{
	dmad_drq *drq;

	dmad_dbg("%s()\n", __func__);

	if (unlikely(ch_req == NULL)) {
		dmad_err("null ch_req!\n");
		return -EFAULT;
	}

	drq = (dmad_drq *) ch_req->drq;

	if (unlikely(drq == NULL)) {
		dmad_err("null ch_req->drq!\n");
		return -EBADR;
	}
	if (unlikely((ch_req->channel < 0) ||
		     ((drq->state & DMAD_DRQ_STATE_READY) == 0))) {
		dmad_err("try to free a free channel!\n");
		return -EBADR;
	}

	/* Stop/abort channel I/O
	 * (forced to shutdown and should be protected against isr)
	 */
	dmad_drain_requests(ch_req, 1);
	dmad_channel_reset(ch_req);

	dmad_dbg("freed channel: %d\n", ch_req->channel);

	spin_lock(&dmad.drq_pool_lock);

	kfree(drq->drb_pool);
	memset(drq, 0, sizeof(dmad_drq));

	ch_req->drq = 0;
	ch_req->channel = (u32) - 1;

	spin_unlock(&dmad.drq_pool_lock);

	return 0;
}

EXPORT_SYMBOL_GPL(dmad_channel_free);

/**
 * dmad_channel_enable - enable/disable a dma channel
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @enable : [in] 1 to enable the channel, 0 to disable
 * @return : 0 if success, non-zero if any error
 *
 * Enable or disable the given DMA channel.
 */
int dmad_channel_enable(const dmad_chreq * ch_req, u8 enable)
{
	dmad_drq *drq;
	unsigned long lock_flags;

	dmad_dbg("%s()\n", __func__);

	if (unlikely(ch_req == NULL))
		return -EFAULT;

	drq = (dmad_drq *) ch_req->drq;

	if (unlikely(drq == NULL))
		return -EBADR;

	spin_lock_irqsave(&drq->drb_pool_lock, lock_flags);

	/* Enable/disable DMA channel */
	if (enable)
		dmad_enable_channel(drq);
	else
		dmad_disable_channel(drq);

	spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);

	return 0;
}

EXPORT_SYMBOL_GPL(dmad_channel_enable);

/**
 * dmad_config_channel_dir - config dma channel transfer direction
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @dir    : [in] DMAD_DRQ_DIR_A0_TO_A1 or DMAD_DRQ_DIR_A1_TO_A0
 * @return : 0 if success, non-zero if any error
 *
 * Reconfigure the channel transfer direction.  This function works only if
 * the channel was allocated with the DMAD_FLAGS_BIDIRECTION flags.  Note
 * that bi-direction mode and ring mode are mutual-exclusive from user's
 * perspective.
 */
int dmad_config_channel_dir(dmad_chreq * ch_req, u8 dir)
{
	dmad_drq *drq;
	addr_t channel_cmds[1];
	unsigned long lock_flags;
	u8 cur_dir;

	if (unlikely(ch_req == NULL))
		return -EFAULT;

	drq = (dmad_drq *) ch_req->drq;

	if (unlikely(drq == NULL))
		return -EBADR;

	if (unlikely(!(ch_req->flags & DMAD_FLAGS_BIDIRECTION))) {
		dmad_err("%s() Channel is not configured as"
			 " bidirectional!\n", __func__);
		return -EFAULT;
	}

	cur_dir = drq->flags & DMAD_DRQ_DIR_MASK;
	if (dir == cur_dir) {
		dmad_dbg("%s() cur_dir(%d) == dir(%d) skip reprogramming hw.\n",
			 __func__, cur_dir, dir);
		return 0;
	}

	spin_lock_irqsave(&drq->drb_pool_lock, lock_flags);

	if (unlikely((drq->sbt_head != 0) /*||dmad_is_channel_enabled(drq) */ )) {
		spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);
		dmad_err("%s() Cannot change direction while the "
			 "channel has pending requests!\n", __func__);
		return -EFAULT;
	}

	if (ch_req->controller == DMAD_DMAC_AHB_CORE) {
		channel_cmds[0] = din((unsigned long)drq->enable_port);
		ch_req->ahb_req.tx_dir = dir;
		dmad_ahb_config_dir(ch_req, (unsigned long *)channel_cmds);
		dout(channel_cmds[0], (unsigned long)drq->enable_port);
	}

	spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);

	return 0;
}

EXPORT_SYMBOL_GPL(dmad_config_channel_dir);

/**
 * dmad_max_size_per_drb - return maximum transfer size per drb
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @return : The maximum transfer size per drb, in bytes.
 *
 * Calculate the maximum transfer size per drb according to the setting of
 * data width during channel initialization.
 *
 * Return size is aligned to 4-byte boundary; this ensures the alignment
 * requirement of dma starting address if the function was used in a loop to
 * separate a large size dma transfer.
 */
u32 dmad_max_size_per_drb(dmad_chreq * ch_req)
{
	addr_t size = 0;
	addr_t data_width = (addr_t) ((dmad_drq *) ch_req->drq)->data_width;

	if (ch_req->controller == DMAD_DMAC_AHB_CORE) {
		size = DMAC_CYCLE_TO_BYTES(DMAC_TOT_SIZE_MASK & ((addr_t) ~ 3),
					   data_width);
	}

	dmad_dbg("%s() - 0x%08x bytes\n", __func__, size);

	return size;
}

EXPORT_SYMBOL_GPL(dmad_max_size_per_drb);

/**
 * dmad_bytes_to_cycles - calculate drb transfer size, in cycles
 * @ch_req    : [in] Reference to the DMA request descriptor structure
 * @byte_size : [in] The DMA transfer size to be converted, in bytes
 * @return    : The drb transfer size, in cycles.
 *
 * Calculate the drb transfer cycle according to the setting of channel data
 * width and burst setting.
 *
 * AHB DMA : unit is number of "data width".
 * APB DMA : unit is number of "data width * burst size"
 *
 * APB Note: According to specification, decrement addressing seems to regard
 *           the burst size setting.  For code efficiency,
 *           dmad_make_req_cycles() does not take care of this case and might
 *           produce wrong result.
 */
u32 dmad_bytes_to_cycles(dmad_chreq * ch_req, u32 byte_size)
{
	addr_t cycle = 0;
	addr_t data_width = (addr_t) ((dmad_drq *) ch_req->drq)->data_width;

	if (ch_req->controller == DMAD_DMAC_AHB_CORE) {
		cycle = DMAC_BYTES_TO_CYCLE(byte_size, data_width);
	}

	dmad_dbg("%s() - 0x%08x bytes --> 0x%08x cycles\n",
		 __func__, byte_size, cycle);
	return cycle;
}

EXPORT_SYMBOL_GPL(dmad_bytes_to_cycles);

/**
 * dmad_alloc_drb_internal - allocate a dma-request-block of a dma channel
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @drb    : [out] Reference to a drb pointer to receive the allocated drb
 * @return : 0 if success, non-zero if any error
 *
 * Allocates a DRB (DMA request block) of the given DMA channel.  DRB is a
 * single dma request which will be pushed into the submission queue of the
 * given DMA channel.  This is a lightweight internal version of
 * dmad_alloc_drb() majorly for use in ring mode.  Critical access to the
 * drb pool should be protected before entering this function.
 */
static inline int dmad_alloc_drb_internal(dmad_drq * drq, dmad_drb ** drb)
{
	/* Initialize drb ptr in case of fail allocation */
	*drb = NULL;

	if (unlikely(drq->fre_head == 0)) {
		return -EAGAIN;
	}

	dmad_detach_head(drq->drb_pool, &drq->fre_head, &drq->fre_tail, drb);

	dmad_attach_tail(drq->drb_pool,
			 &drq->rdy_head, &drq->rdy_tail, (*drb)->node);

	(*drb)->state = DMAD_DRB_STATE_READY;
	(*drb)->sync = 0;

	dmad_dbg("%s() drb(%d 0x%08x)\n", __func__, (*drb)->node, (u32) (*drb));

	return 0;
}

/**
 * dmad_alloc_drb - allocate a dma-request-block of a dma channel
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @drb    : [out] Reference to a drb pointer to receive the allocated drb
 * @return : 0 if success, non-zero if any error
 *
 * Allocates a DRB (DMA request block) of the given DMA channel.  DRB is a
 * single dma request which will be pushed into the submission queue of the
 * given DMA channel.
 */
int dmad_alloc_drb(dmad_chreq * ch_req, dmad_drb ** drb)
{
	dmad_drq *drq;
	unsigned long lock_flags;

	dmad_dbg("%s()\n", __func__);

	if (unlikely(ch_req == NULL)) {
		dmad_err("null ch_req!\n");
		return -EFAULT;
	}

	drq = (dmad_drq *) ch_req->drq;

	if (likely(drq == NULL)) {
		dmad_err("null ch_req->drq!\n");
		return -EBADR;
	}

	spin_lock_irqsave(&drq->drb_pool_lock, lock_flags);

	/* Initialize drb ptr in case of fail allocation */
	*drb = NULL;

	if (unlikely(drq->fre_head == 0)) {

		drq->state &= (u32) ~ DMAD_DRQ_STATE_ABORT;

		spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);

_wait_for_free_drbs:

		/* Wait for free urbs */
		if (drq->flags & DMAD_FLAGS_SLEEP_BLOCK) {

			int timeout =
			    wait_for_completion_interruptible_timeout(&drq->
								      drb_alloc_sync,
								      msecs_to_jiffies
								      (6000));

			/* reset sync object */
			reinit_completion(&drq->drb_alloc_sync);

			if (timeout < 0) {
				dmad_err("%s() wait for"
					 " completion error! (%d)\n",
					 __func__, timeout);
				return timeout;
			}

		} else if (drq->flags & DMAD_FLAGS_SPIN_BLOCK) {

			u32 timeout = 0x00ffffff;

			while ((drq->fre_head == 0) && (--timeout != 0)) {
			}
			if (timeout == 0) {
				dmad_err("%s() polling wait for "
					 "completion timeout!\n", __func__);
				return -EAGAIN;
			}

		} else {
			return -EAGAIN;
		}

		spin_lock_irqsave(&drq->drb_pool_lock, lock_flags);

		/* check whether all the requests of the channel has been
		 * abandoned or not */
		if (unlikely(drq->state & DMAD_DRQ_STATE_ABORT)) {
			dmad_dbg("%s() drb-allocation aborted due"
				 " to cancel-request ...\n", __func__);
			drq->state &= (u32) ~ DMAD_DRQ_STATE_ABORT;
			spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);
			return -ECANCELED;
		}

		/* check again to avoid non-atomic operation between above
		 * two calls */
		if (unlikely(drq->fre_head == 0)) {
			dmad_dbg("%s() lost free drbs ... "
				 "continue waiting ...\n", __func__);
			spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);
			goto _wait_for_free_drbs;
		}
	}

	dmad_detach_head(drq->drb_pool, &drq->fre_head, &drq->fre_tail, drb);

	dmad_attach_tail(drq->drb_pool,
			 &drq->rdy_head, &drq->rdy_tail, (*drb)->node);

	(*drb)->state = DMAD_DRB_STATE_READY;
	(*drb)->sync = 0;

	dmad_dbg("%s() drb(%d 0x%08x)\n", __func__, (*drb)->node, (u32) (*drb));

	drq->state &= (u32) ~ DMAD_DRQ_STATE_ABORT;

	spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);

	return 0;
}

EXPORT_SYMBOL_GPL(dmad_alloc_drb);

/**
 * dmad_free_drb - free a dma-request-block of a dma channel
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @drb    : [in] Reference to a drb to be freed
 * @return : 0 if success, non-zero if any error
 *
 * Frees a DRB (DMA request block) of the given DMA channel.  DRB is a
 * single dma request which will be pushed into the submission queue of the
 * given DMA channel.
 */
int dmad_free_drb(dmad_chreq * ch_req, dmad_drb * drb)
{
	dmad_drq *drq;
	unsigned long lock_flags;

	dmad_dbg("%s()\n", __func__);

	if (unlikely(ch_req == NULL)) {
		dmad_err("null ch_req!\n");
		return -EFAULT;
	}

	drq = (dmad_drq *) ch_req->drq;

	if (unlikely(drq == NULL)) {
		dmad_err("null ch_req->drq!\n");
		return -EBADR;
	}

	spin_lock_irqsave(&drq->drb_pool_lock, lock_flags);

	/****************************************************
	 * Following code requires _safe_exit return path
	 */

	if (unlikely((drq->rdy_head == 0) || (drb->node == 0) ||
		     (drb->state != DMAD_DRB_STATE_READY) ||
		     (drb->node >= DMAD_DRB_POOL_SIZE))) {
		dmad_err("Ready-queue is empty or invalid node!\n");

		spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);
		return -EBADR;
	}

	dmad_detach_node(drq->drb_pool,
			 &drq->rdy_head, &drq->rdy_tail, drb->node);
	dmad_attach_tail(drq->drb_pool,
			 &drq->fre_head, &drq->fre_tail, drb->node);

	drb->state = DMAD_DRB_STATE_FREE;
	drb->sync = 0;

	spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);

	return 0;
}

EXPORT_SYMBOL_GPL(dmad_free_drb);

/**
 * dmad_submit_request_internal - submit a dma-request-block to the dma channel
 * @ch_req     : [in] Reference to the DMA request descriptor structure
 * @drb        : [in] Reference to a drb to be submitted
 * @keep_fired : [in] non-zero to kickoff dma even the channel has stopped due
 *                    to finishing its previous request
 * @return     : 0 if success, non-zero if any error
 *
 * Submit a DRB (DMA request block) of the given DMA channel to submission
 * queue.  DRB is a single dma request which will be pushed into the
 * submission queue of the given DMA channel.  This is a lightweight internal
 * version of dmad_alloc_drb() majorly for use in ring mode.  Critical access to
 * the drb pool should be protected before entering this function.
 */
static inline int dmad_submit_request_internal(dmad_drq * drq, dmad_drb * drb)
{
	if (drb->state == DMAD_DRB_STATE_READY) {
		/* Detach user node from ready list */
		dmad_detach_node(drq->drb_pool,
				 &drq->rdy_head, &drq->rdy_tail, drb->node);

		dmad_attach_tail(drq->drb_pool,
				 &drq->sbt_head, &drq->sbt_tail, drb->node);

		drb->state = DMAD_DRB_STATE_SUBMITTED;

		dmad_dbg("%s() submit drb(%d 0x%08x) addr0(0x%08x) "
			 "addr1(0x%08x) size(0x%08x) state(%d)\n", __func__,
			 drb->node, (u32) drb, drb->src_addr, drb->dst_addr,
			 drb->req_cycle, drb->state);
	} else {
		dmad_dbg("%s() skip drb(%d 0x%08x) addr0(0x%08x) addr1(0x%08x)"
			 " size(0x%08x) state(%d)\n", __func__,
			 drb->node, (u32) drb, drb->src_addr, drb->dst_addr,
			 drb->req_cycle, drb->state);
	}

	return 0;
}

/**
 * dmad_submit_request - submit a dma-request-block to the dma channel
 * @ch_req     : [in] Reference to the DMA request descriptor structure
 * @drb        : [in] Reference to a drb to be submitted
 * @keep_fired : [in] non-zero to kickoff dma even the channel has stopped due
 *                    to finishing its previous request
 * @return     : 0 if success, non-zero if any error
 *
 * Submit a DRB (DMA request block) of the given DMA channel to submission
 * queue.  DRB is a single dma request which will be pushed into the
 * submission queue of the given DMA channel.
 */
int dmad_submit_request(dmad_chreq * ch_req, dmad_drb * drb, u8 keep_fired)
{
	dmad_drq *drq;
	unsigned long lock_flags;
	dmad_dbg("%s()\n", __func__);

	if (unlikely(ch_req == NULL)) {
		dmad_err("null ch_req!\n");
		return -EFAULT;
	}

	drq = (dmad_drq *) ch_req->drq;

	if (unlikely(drq == NULL)) {
		dmad_err("null ch_req->drq!\n");
		return -EBADR;
	}
	spin_lock_irqsave(&drq->drb_pool_lock, lock_flags);

	/******************************************************
	 * Following code require _safe_exit return path
	 */

	if (unlikely((drq->rdy_head == 0) || (drb->node == 0) ||
		     (drb->node >= DMAD_DRB_POOL_SIZE))) {
					printk("node error\n");
		spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);
		return -EBADR;
	}

	/* Detach user node from ready list */
	dmad_detach_node(drq->drb_pool, &drq->rdy_head, &drq->rdy_tail,
			 drb->node);

	/* Queue DRB to the end of the submitted list */
	dmad_dbg("submit drb(%d 0x%08x) addr0(0x%08x) addr1(0x%08x) "
		 "size(0x%08x) sync(0x%08x) fire(%d)\n",
		 drb->node, (u32) drb, drb->src_addr, drb->dst_addr,
		 drb->req_cycle, (u32) drb->sync, keep_fired);

	/* Check if submission is performed to an empty queue */
	if (unlikely(keep_fired && (drq->sbt_head == 0))) {
		/* DMA is not running, so kick off transmission */
		dmad_dbg("kickoff dma engine.\n");

		dmad_attach_tail(drq->drb_pool,
				 &drq->sbt_head, &drq->sbt_tail, drb->node);
		/* Source and destination address */
		if (drq->flags & DMAD_DRQ_DIR_A1_TO_A0) {
			dout(drb->addr1, (unsigned long) drq->src_port);
			dout(drb->addr0, (unsigned long) drq->dst_port);
		} else {
			dout(drb->addr0, (unsigned long) drq->src_port);
			dout(drb->addr1, (unsigned long) drq->dst_port);
		}

		/* Transfer size (in units of source width) */
		dout(drb->req_cycle, (unsigned long) drq->cyc_port);

		/* Enable DMA channel (Kick off transmission when client
		 * enable it's transfer state) */
		dmad_enable_channel(drq);
		drb->state = DMAD_DRB_STATE_EXECUTED;
	} else {
		/* DMA is already running, so only queue DRB to the end of the
		 * list */
		dmad_attach_tail(drq->drb_pool,
				 &drq->sbt_head, &drq->sbt_tail, drb->node);
		drb->state = DMAD_DRB_STATE_SUBMITTED;
	}
	spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);

	return 0;
}

EXPORT_SYMBOL_GPL(dmad_submit_request);

/**
 * dmad_withdraw_request - cancel a submitted dma-request-block
 * @ch_req     : [in] Reference to the DMA request descriptor structure
 * @drb        : [in] Reference to a drb to be submitted
 * @keep_fired : [in] non-zero to kickoff dma even the channel has stopped due
 *                    to finishing its previous request
 * @return     : 0 if success, non-zero if any error
 *
 * Cancel a submitted DRB (DMA request block) of the given DMA channel in its
 * submission queue.  DRB is a single dma request which will be pushed into the
 * submission queue of the given DMA channel. Cancellation fails if the DRB has
 * already been kicked off.
 */
int dmad_withdraw_request(dmad_chreq * ch_req, dmad_drb * drb)
{
	dmad_drq *drq = 0;
	unsigned long lock_flags;

	dmad_dbg("%s()\n", __func__);

	if (unlikely(ch_req == NULL)) {
		dmad_err("null ch_req!\n");
		return -EFAULT;
	}

	drq = (dmad_drq *) ch_req->drq;

	if (unlikely(drq == NULL)) {
		dmad_err("null ch_req->drq!\n");
		return -EBADR;
	}

	if (unlikely(drq->sbt_head == 0))
		return -EBADR;

	if (unlikely((drb->node == 0) || (drb->node >= DMAD_DRB_POOL_SIZE)))
		return -EBADR;

	spin_lock_irqsave(&drq->drb_pool_lock, lock_flags);

	if (unlikely((drq->sbt_head == 0) || (drb->node == 0) ||
		     (drb->state != DMAD_DRB_STATE_SUBMITTED) ||
		     (drb->node >= DMAD_DRB_POOL_SIZE))) {
		dmad_err("Submitted-queue is empty or invalid node!\n");

		spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);
		return -EBADR;
	}

	dmad_dbg("cancel drb(%d 0x%08x) addr0(0x%08x) addr1(0x%08x) "
		 "size(0x%08x) state(%d)\n",
		 drb->node, (u32) drb, drb->src_addr, drb->dst_addr,
		 drb->req_cycle, drb->state);

	if (unlikely(drb->state == DMAD_DRB_STATE_EXECUTED)) {
		dmad_dbg("Already running drb cannot be stopped currently!\n");

		spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);
		return 0;/*-EBADR; */
	}

	dmad_detach_node(drq->drb_pool,
			 &drq->rdy_head, &drq->rdy_tail, drb->node);
	dmad_attach_tail(drq->drb_pool,
			 &drq->fre_head, &drq->fre_tail, drb->node);

	drb->state = DMAD_DRB_STATE_FREE;

	if (drb->sync)
		complete_all(drb->sync);
	drb->sync = 0;

	spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);

	return 0;
}

EXPORT_SYMBOL_GPL(dmad_withdraw_request);

/**
 * dmad_kickoff_requests_internal - kickoff hw DMA transmission
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @return : 0 if success, non-zero if any error
 *
 * Kickoff hw DMA transmission of the given DMA channel.  This function is
 * valid for both ring & non-ring mode.  This is a lightweight internal version
 * of dmad_kickoff_requests() majorly for use in ring mode.  Critical access to
 * the drb pool should be protected before entering this function.
 */
static inline int dmad_kickoff_requests_internal(dmad_drq * drq)
{
	dmad_drb *drb;

	dmad_get_head(drq->drb_pool, &drq->sbt_head, &drq->sbt_tail, &drb);

	if (!drb) {
		dmad_err("%s() null drb!\n", __func__);
		return -EBADR;
	}

	dmad_dbg("%s() drb(%d 0x%08x) addr0(0x%08x) addr1(0x%08x) "
		 "size(0x%08x) state(%d)\n", __func__,
		 drb->node, (u32) drb, drb->src_addr, drb->dst_addr,
		 drb->req_cycle, drb->state);

	if (drb->state == DMAD_DRB_STATE_SUBMITTED) {
		/* Transfer size (in units of source width) */
		dout(drb->req_cycle, (unsigned long) drq->cyc_port);

		/* Source and destination address */
		if (drq->flags & DMAD_DRQ_DIR_A1_TO_A0) {
			dout(drb->addr1, (unsigned long) drq->src_port);
			dout(drb->addr0, (unsigned long) drq->dst_port);
		} else {
			dout(drb->addr0, (unsigned long) drq->src_port);
			dout(drb->addr1, (unsigned long) drq->dst_port);
		}

		drb->state = DMAD_DRB_STATE_EXECUTED;
	}

	/* Enable DMA channel */
	if (!dmad_is_channel_enabled(drq)) {
		dmad_enable_channel(drq);
	}

	return 0;
}

/**
 * dmad_kickoff_requests - kickoff hw DMA transmission of the given DMA channel
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @return : 0 if success, non-zero if any error
 *
 * Kickoff hw DMA transmission of the given DMA channel.  This function is
 * valid for both ring & non-ring mode.
 */
int dmad_kickoff_requests(dmad_chreq * ch_req)
{
	dmad_drq *drq = 0;
	dmad_drb *drb = 0;
	unsigned long lock_flags;
	dma_addr_t req_cycle;

	dmad_dbg("%s()\n", __func__);

	if (unlikely(ch_req == NULL)) {
		dmad_err("null ch_req!\n");
		return -EFAULT;
	}

	drq = (dmad_drq *) ch_req->drq;

	if (unlikely(drq == NULL)) {
		dmad_err("null ch_req->drq!\n");
		return -EBADR;
	}

	spin_lock_irqsave(&drq->drb_pool_lock, lock_flags);

	dmad_get_head(drq->drb_pool, &drq->sbt_head, &drq->sbt_tail, &drb);

	dmad_dbg("drq(0x%08x) channel_base(0x%08x)\n",
		 (u32) drq, drq->channel_base);
	dmad_dbg("kick off drb(%d 0x%08x) addr0(0x%08x) addr1(0x%08x) "
		 "size(0x%08x) state(%d) a1_to_a0(%d)\n",
		 (u32) drb->node, (u32) drb, drb->addr0, drb->addr1,
		 drb->req_cycle, drb->state,
		 drq->flags & DMAD_DRQ_DIR_A1_TO_A0);

	/* do nothing if no drbs are in the submission queue */
	if (unlikely((drb == 0) || (drb->state != DMAD_DRB_STATE_SUBMITTED))) {
		dmad_dbg("%s() invalid drb(%d 0x%08x) or drb-state(%d)!\n",
			 __func__,
			 drb->node, (u32) drb, drb ? drb->state : 0xffffffff);
		spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);
		return 0;
	}

	req_cycle = drb->req_cycle;

	if (unlikely(req_cycle == 0)) {
		dmad_dbg("%s() zero transfer size!\n", __func__);
		goto _safe_exit;
	}

	/* Transfer size (in units of source width) */
	dout(req_cycle, (unsigned long) drq->cyc_port);

	/* Source and destination address */
	if (drq->flags & DMAD_DRQ_DIR_A1_TO_A0) {
		dout(drb->addr1, (unsigned long) drq->src_port);
		dout(drb->addr0, (unsigned long) drq->dst_port);
	} else {
		dout(drb->addr0, (unsigned long) drq->src_port);
		dout(drb->addr1, (unsigned long) drq->dst_port);
	}

	drb->state = DMAD_DRB_STATE_EXECUTED;

	/* Enable DMA channel */
	dmad_enable_channel(drq);

_safe_exit:
	spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);

	return 0;
}

EXPORT_SYMBOL_GPL(dmad_kickoff_requests);

/**
 * dmad_probe_hw_ptr_src - probe DMA source hw-address of the given channel
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @return : physical address of current HW source pointer
 *
 * Probe DMA source hw-address of the given channel.
 */
dma_addr_t dmad_probe_hw_ptr_src(dmad_chreq * ch_req)
{
	return (dma_addr_t) din(((dmad_drq *) ch_req->drq)->src_port);
}

EXPORT_SYMBOL_GPL(dmad_probe_hw_ptr_src);

/**
 * dmad_probe_hw_ptr_dst - probe DMA destination hw-address of the given channel
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @return : physical address of current HW destination pointer
 *
 * Probe DMA destination hw-address of the given channel.
 */
dma_addr_t dmad_probe_hw_ptr_dst(dmad_chreq * ch_req)
{
	return (dma_addr_t) din(((dmad_drq *) ch_req->drq)->dst_port);
}

EXPORT_SYMBOL_GPL(dmad_probe_hw_ptr_dst);

/**
 * dmad_update_ring - update DMA ring buffer base && size of the given channel
 * @ch_req : [in] Reference to the DMA request descriptor structure
 * @size   : [in] The new ring buffer size, in unit of data width (cycles)
 * @return : 0 if success, non-zero if any error
 *
 * Update DMA ring buffer size of the given channel.  This function is valid
 * only if the channel is initialized as ring buffer mode.
 */
int dmad_update_ring(dmad_chreq * ch_req)
{
	unsigned long lock_flags;
	dmad_drq *drq = (dmad_drq *) ch_req->drq;
	int remnant;

	if (unlikely(dmad_is_channel_enabled(drq))) {
		dmad_err("%s() Error - dma channel should be "
			 "disabled before updating ring size!\n", __func__);
		return -EFAULT;
	}

	spin_lock_irqsave(&drq->drb_pool_lock, lock_flags);

	/* todo: range checking */

	remnant = (int)ch_req->ring_size -
	    (int)ch_req->periods * (int)ch_req->period_size;
	if (remnant == 0) {
		drq->periods = ch_req->periods;
	} else if (remnant > 0) {
		drq->periods = ch_req->periods;	// + 1;
	} else {
		dmad_err("%s() Error - buffer_size < "
			 "periods * period_size!\n", __func__);
		spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);
		return -EFAULT;
	}

	drq->ring_base = ch_req->ring_base;
	drq->ring_size = ch_req->ring_size;
	drq->period_size = ch_req->period_size;
	drq->remnant_size = (dma_addr_t) remnant;

	if (ch_req->controller == DMAD_DMAC_AHB_CORE) {
		drq->period_bytes =
		    DMAC_CYCLE_TO_BYTES(drq->period_size, drq->data_width);
	}

	spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);

	dmad_dbg("%s() ring: base(0x%08x) port(0x%08x) periods(0x%08x) "
		 "period_size(0x%08x) period_bytes(0x%08x) "
		 "remnant_size(0x%08x)\n",
		 __func__, drq->ring_base, drq->ring_port,
		 drq->periods, drq->period_size, drq->period_bytes,
		 drq->remnant_size);

	return 0;
}

EXPORT_SYMBOL_GPL(dmad_update_ring);

/**
 * dmad_update_ring_sw_ptr - update DMA ring buffer sw-pointer
 * @ch_req     : [in] Reference to the DMA request descriptor structure
 * @sw_ptr     : [in] The new sw-pointer for the hw-pointer to chase of
 * @keep_fired : [in] non-zero to kickoff dma even the channel has stopped due
 *                    to finishing its previous request
 * @return     : 0 if success, non-zero if any error
 *
 * Update DMA ring buffer sw-pointer of the given channel on the fly.  This
 * function is valid only if the channel is initialized as ring buffer mode.
 * Uint of sw_ptr is in number of dma data width.
 */
int dmad_update_ring_sw_ptr(dmad_chreq * ch_req,
			    dma_addr_t sw_ptr, u8 keep_fired)
{
	dmad_drq *drq;
	unsigned long lock_flags;
	dma_addr_t hw_off = 0, ring_ptr;
	dma_addr_t sw_p_off, ring_p_off, period_bytes;
	dma_addr_t remnant_size;
	int period_size;
	int sw_p_idx, ring_p_idx, period, periods;
	dmad_drb *drb = NULL;

	/*if (ch_req == NULL) { */
	/*    dmad_dbg("%s() null ch_req!\n", __func__); */
	/*    return -EFAULT; */
	/*} */

	drq = (dmad_drq *) ch_req->drq;

	/*if (drq == NULL) { */
	/*    dmad_dbg("%s() null ch_req->drq!\n", __func__); */
	/*    return -EBADR; */
	/*} */

	if (unlikely(sw_ptr > drq->ring_size)) {
//		dmad_err("%s() Invalid ring buffer sw-pointer ");
		return -EBADR;
	}

	spin_lock_irqsave(&drq->drb_pool_lock, lock_flags);

	periods = drq->periods;
	period_size = drq->period_size;
	period_bytes = drq->period_bytes;
	remnant_size = drq->remnant_size;

	ring_ptr = drq->sw_ptr;
	ring_p_idx = drq->sw_p_idx;
	ring_p_off = drq->sw_p_off;

	sw_p_idx = div_u64(sw_ptr, period_size);
	__iter_div_u64_rem(sw_ptr, period_size, &sw_p_off);

	if (remnant_size && (sw_p_idx == periods)) {
		--sw_p_idx;
		sw_p_off += period_size;
	}

	dmad_dbg("%s() ring_ptr(0x%08x) ring_p_idx(0x%08x) "
		 "ring_p_off(0x%08x)\n",
		 __func__, ring_ptr, ring_p_idx, ring_p_off);
	dmad_dbg("%s() sw_ptr(0x%08x) sw_p_idx(0x%08x) sw_p_off(0x%08x)\n",
		 __func__, sw_ptr, sw_p_idx, sw_p_off);

	if (drq->ring_drb &&
	    (drq->ring_drb->state & (DMAD_DRB_STATE_READY |
				     DMAD_DRB_STATE_SUBMITTED |
				     DMAD_DRB_STATE_EXECUTED))) {
		drb = drq->ring_drb;
	} else {
		/* alloc new drb if there is none yet at ring_ptr */
		if (0 != dmad_alloc_drb_internal(drq, &drb)) {
			dmad_err("%s() drb allocation failed!\n", __func__);
			spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);
			return -ENOSPC;
		}
		drb->addr0 = ((dma_addr_t) ring_p_idx * period_bytes) +
		    drq->ring_base;
		drb->addr1 = drq->dev_addr;
		drb->req_cycle = 0;	// redundent, though, no harm to performance

		dmad_dbg("init_drb(%d 0x%08x) addr0(0x%08x) addr1(0x%08x) "
			 "size(0x%08x) state(%d)\n",
			 (u32) drb->node, (u32) drb, drb->src_addr,
			 drb->dst_addr, drb->req_cycle, drb->state);

		drq->ring_drb = drb;
	}

	/* Following code-path has been optimized.  The design flow is expanded
	 * below for reference.
	 *
	 *   if (sw_ptr >= ring_ptr)
	 *       if (sw_p_idx == ring_p_idx)
	 *           ring_drb::req_cycle <- sw_p_off
	 *           if (ring_drb::state == executed)
	 *               hw_cycle <- sw_p_idx
	 *           fi
	 *       else
	 *           ring_drb::req_cycle <- period_size
	 *           if (ring_drb::state == executed)
	 *               hw_cycle <- period_size
	 *           fi
	 *           for (i = ring_p_idx+1 ~ sw_p_idx-1)
	 *               new_drb::ring_addr <- i * period_bytes + ring_base
	 *               new_drb::req_cycle <- period_size
	 *           rof
	 *           sw_drb::ring_addr <- sw_p_idx * period_bytes + ring_base
	 *           sw_drb::req_cycle <- sw_p_off
	 *   else
	 *       // sw_ptr < ring_ptr
	 *       ring_drb::req_cycle <- period_size
	 *       if (ring_drb::state == executed)
	 *           hw_cycle <- period_size
	 *       fi
	 *       for (i = ring_p_idx+1 ~ idx_max)
	 *           new_drb::ring_addr <- i * period_bytes + ring_base
	 *           new_drb::req_cycle <- period_size
	 *       rof
	 *       for (i = 0 ~ sw_p_idx-1)
	 *           new_drb::ring_addr <- i * period_bytes + ring_base
	 *           new_drb::req_cycle <- period_size
	 *       rof
	 *       sw_drb::ring_addr <- sw_p_idx * period_bytes + ring_base
	 *       sw_drb::req_cycle <- sw_p_off
	 *   fi
	 */
	if ((sw_ptr >= ring_ptr) && (sw_p_idx == ring_p_idx) && (sw_p_off != 0)) {

		dmad_dbg("update ring drb\n");

		/* update drb size at ring_ptr */
		drb->req_cycle = sw_p_off;

		dmad_dbg("ring_drb(%d 0x%08x) addr0(0x%08x) addr1(0x%08x) "
			 "size(0x%08x) state(%d)\n",
			 (u32) drb->node, (u32) drb, drb->addr0, drb->addr1,
			 drb->req_cycle, drb->state);

		/* update hw dma size of this drb if it has been sent to the
		 * controller */
		if (drb->state == DMAD_DRB_STATE_EXECUTED) {
			dmad_disable_channel(drq);

			if (ch_req->controller == DMAD_DMAC_AHB_CORE)
				hw_off = DMAC_BYTES_TO_CYCLE((addr_t)
							     din((addr_t) drq->
								 ring_port) -
							     (addr_t) drb->
							     addr0,
							     drq->data_width);

			dmad_dbg("hw_off(0x%08x) sw_p_off(0x%08x)\n",
				 (u32) hw_off, (u32) sw_p_off);

			if (sw_p_off < hw_off)
				dmad_err("%s() underrun! sw_p_off(0x%08x) <"
					 " hw_off(0x%08x)\n", __func__,
					 (u32) sw_p_off, (u32) hw_off);
			else
				dout(sw_p_off - hw_off, (unsigned long)drq->cyc_port);

			dmad_enable_channel(drq);

		} else {
			dmad_submit_request_internal(drq, drb);
		}

	} else {

		dmad_dbg("fulfill ring drb - sw_ptr(0x%08x) ring_ptr(0x%08x)\n",
			 (u32) sw_ptr, (u32) ring_ptr);

		/* fulfill last drb at ring_ptr */
		if (ring_p_idx == (periods - 1))
			drb->req_cycle = period_size + remnant_size;
		else
			drb->req_cycle = period_size;

		dmad_dbg("ring_drb(%d 0x%08x) addr0(0x%08x) addr1(0x%08x) "
			 "size(0x%08x) state(%d)\n",
			 (u32) drb->node, (u32) drb, drb->addr0, drb->addr1,
			 drb->req_cycle, drb->state);

		if (drb->state == DMAD_DRB_STATE_EXECUTED) {
			dmad_disable_channel(drq);

			if (ch_req->controller == DMAD_DMAC_AHB_CORE)
				hw_off = DMAC_BYTES_TO_CYCLE((addr_t)
							     din((addr_t) drq->
								 ring_port) -
							     (addr_t) drb->
							     addr0,
							     drq->data_width);

			dmad_dbg("hw_off(0x%08x) period_size(0x%08x)\n",
				 (u32) hw_off, (u32) period_size);

			if (ring_p_idx == (periods - 1)) {
				if (period_size < hw_off)
					dmad_err("%s() illegal! "
						 "period_size(0x%08x) + "
						 "remnant_size(0x%08x) < "
						 "hw_off(0x%08x)\n", __func__,
						 (u32) period_size,
						 (u32) remnant_size,
						 (u32) hw_off);
				else
					dout(period_size + remnant_size -
					     hw_off, (unsigned long)drq->cyc_port);
			} else {
				if (period_size < hw_off)
					dmad_err("%s() illegal! "
						 "period_size(0x%08x) < "
						 "hw_off(0x%08x)\n", __func__,
						 (u32) period_size,
						 (u32) hw_off);
				else
					dout(period_size - hw_off,
					     (unsigned long)drq->cyc_port);
			}

			dmad_enable_channel(drq);

		} else {
			dmad_submit_request_internal(drq, drb);
		}

		++ring_p_idx;

		/* adjust sw_ptr period index ahead by one ring cycle */
		//if (sw_ptr < ring_ptr) {
		if (sw_p_idx < ring_p_idx) {
			sw_p_idx += periods;
		}

		/* allocate in-between (ring_ptr+1 to sw_ptr-1)
		 * full-cycle drbs */
		for (period = ring_p_idx; period < sw_p_idx; ++period) {
			if (0 != dmad_alloc_drb_internal(drq, &drb)) {
				dmad_err("%s() drb allocation failed!\n",
					 __func__);
				spin_unlock_irqrestore(&drq->drb_pool_lock,
						       lock_flags);
				return -ENOSPC;
			}

			drb->addr0 = (dma_addr_t) (period % periods) *
			    period_bytes + drq->ring_base;
			drb->addr1 = drq->dev_addr;

			if (period == (periods - 1)) {
				drb->req_cycle = period_size + remnant_size;
			} else {
				drb->req_cycle = period_size;
			}

			dmad_dbg("inbtw_drb(%d 0x%08x) addr0(0x%08x) "
				 "addr1(0x%08x) size(0x%08x) state(%d)\n",
				 (u32) drb->node, (u32) drb, drb->addr0,
				 drb->addr1, drb->req_cycle, drb->state);

			dmad_submit_request_internal(drq, drb);
		}

		/* allocate drb right at sw_ptr */
		if (0 != dmad_alloc_drb_internal(drq, &drb)) {
			dmad_err("%s() drb allocation failed!\n", __func__);
			spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);
			return -ENOSPC;
		}
		drb->addr0 = (dma_addr_t) (sw_p_idx % periods) *
		    period_bytes + drq->ring_base;
		drb->addr1 = drq->dev_addr;
		drb->req_cycle = sw_p_off;

		dmad_dbg("swptr_drb(%d 0x%08x) addr0(0x%08x) addr1(0x%08x) "
			 "size(0x%08x) state(%d)\n",
			 (u32) drb->node, (u32) drb, drb->addr0, drb->addr1,
			 drb->req_cycle, drb->state);

		drq->ring_drb = drb;

		if (sw_p_off > 0)
			dmad_submit_request_internal(drq, drb);
	}

	__iter_div_u64_rem(sw_ptr, drq->ring_size, &drq->sw_ptr);
	drq->sw_p_idx = sw_p_idx % periods;
	drq->sw_p_off = sw_p_off;

	if (likely(keep_fired)) {
		dmad_kickoff_requests_internal(drq);
	}

	spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);

	return 0;
}

EXPORT_SYMBOL_GPL(dmad_update_ring_sw_ptr);

/**
 * dmad_probe_ring_hw_ptr - probe DMA ring buffer position of the given channel
 * @ch_req     : [in] Reference to the DMA request descriptor structure
 * @return     : Ring buffer position of current HW ring buffer pointer
 *
 * Probe DMA ring buffer position of the given channel.  The position is
 * relative to the ring buffer base.  This function is valid only if the
 * channel is initialized as ring buffer mode.
 */
dma_addr_t dmad_probe_ring_hw_ptr(dmad_chreq * ch_req)
{
	dmad_drq *drq = (dmad_drq *) ch_req->drq;
	dma_addr_t cycles =
	    (dma_addr_t) din(drq->ring_port) - (dma_addr_t) drq->ring_base;

	if (ch_req->controller == DMAD_DMAC_AHB_CORE)
		cycles = DMAC_BYTES_TO_CYCLE(cycles, drq->data_width);

	return cycles;
}

EXPORT_SYMBOL_GPL(dmad_probe_ring_hw_ptr);

/**
 * dmad_channel_drain - cancel DMA transmission of the given DMA channel
 * @controller : [in] One of the enum value of DMAD_DMAC_CORE
 * @drq        : [in] Reference to the DMA queue structure (dmad_drq)
 * @shutdown   : [in] Non-zero to force a immediate channel shutdown
 * @return     : 0 if success, non-zero if any error
 *
 * Stop the DMA transmission and cancel all submitted requests of the given
 * DMA channel.  This function drains a single channel and is the internal
 * implementation of the interface routine dmad_drain_requests() and the
 * module_exit function.
 */
static int dmad_channel_drain(u32 controller, dmad_drq * drq, u8 shutdown)
{
	dmad_drb *drb = 0;
	unsigned long lock_flags;

	if (unlikely(drq == NULL)) {
		dmad_err("null ch_req->drq!\n");
		return -EBADR;
	}

	spin_lock_irqsave(&drq->drb_pool_lock, lock_flags);

	/* Stop DMA channel if forced to shutdown immediately */
	if (shutdown) {
		/* disable dma controller */
		dmad_reset_channel(drq);

		/* todo: more settings to stop DMA controller ?? */

		/*if (drb->state == DMAD_DRB_STATE_EXECUTED) { */
		/*} */
	}

	/* Detach DRBs in submit queue */
	dmad_detach_head(drq->drb_pool, &drq->sbt_head, &drq->sbt_tail, &drb);

	while (drb) {
		dmad_dbg("cancel sbt drb(%d 0x%08x) addr0(0x%08x) "
			 "addr1(0x%08x) size(0x%08x) state(%d)\n",
			 drb->node, (u32) drb, drb->src_addr, drb->dst_addr,
			 drb->req_cycle, (u32) drb->state);

		/* Mark DRB state as abort */
		drb->state = DMAD_DRB_STATE_ABORT;

		if (drb->sync)
			complete_all(drb->sync);

		dmad_attach_tail(drq->drb_pool, &drq->fre_head, &drq->fre_tail,
				 drb->node);

		dmad_detach_head(drq->drb_pool, &drq->sbt_head, &drq->sbt_tail,
				 &drb);
	}

	/* Detach DRBs in ready queue */
	dmad_detach_head(drq->drb_pool, &drq->rdy_head, &drq->rdy_tail, &drb);

	while (drb) {
		dmad_dbg("cancel rdy drb(%d 0x%08x) addr0(0x%08x) "
			 "addr1(0x%08x) size(0x%08x) state(%d)\n",
			 drb->node, (u32) drb, drb->src_addr, drb->dst_addr,
			 drb->req_cycle, (u32) drb->state);

		/* Mark DRB state as abort */
		drb->state = DMAD_DRB_STATE_ABORT;

		dmad_attach_tail(drq->drb_pool, &drq->fre_head, &drq->fre_tail,
				 drb->node);

		/* Detach next submitted DRB (DMA Request Block) from the
		 * DRQ (DMA Request Queue) */
		dmad_detach_head(drq->drb_pool, &drq->rdy_head, &drq->rdy_tail,
				 &drb);
	}

	drq->state |= DMAD_DRQ_STATE_ABORT;

	drq->ring_drb = NULL;
	drq->sw_ptr = 0;
	drq->sw_p_idx = 0;
	drq->sw_p_off = 0;

	spin_unlock_irqrestore(&drq->drb_pool_lock, lock_flags);

	if ( /*(drq->fre_head == 0) && */ (drq->flags & DMAD_FLAGS_SLEEP_BLOCK)) {
		complete_all(&drq->drb_alloc_sync);
	}

	return 0;
}

/**
 * dmad_cancel_requests - cancel DMA transmission of the given DMA channel
 * @ch_req   : [in] Reference to the DMA request descriptor structure
 * @shutdown : [in] Non-zero to force a immediate channel shutdown
 * @return   : 0 if success, non-zero if any error
 *
 * Stop the DMA transmission and cancel all submitted requests of the given
 * DMA channel.
 */
int dmad_drain_requests(dmad_chreq * ch_req, u8 shutdown)
{
	dmad_dbg("%s()\n", __func__);

	if (ch_req == NULL) {
		dmad_err("null ch_req!\n");
		return -EFAULT;
	}

	return dmad_channel_drain(ch_req->controller, ch_req->drq, shutdown);
}

EXPORT_SYMBOL_GPL(dmad_drain_requests);

/**
 * dmad_probe_irq_source - probe DMA channel who asserts the shared sw-irq line
 * @return : The channel number which asserts the shared sw-irq line
 *
 * Probe DMA channel who asserts the shared sw-irq line.
 */
int dmad_probe_irq_source_ahb(void)
{
	int channel;		/* interrupt channel number */

	/* todo: spin_lock */

	/* - Check DMA status register to get channel number */
	for (channel = 0; channel < DMAD_AHB_MAX_CHANNELS; ++channel) {
		if (getbl(channel+TC_OFFSET, (unsigned long)INT_STA))
			return channel;
	}

	/* Perform DMA error checking if no valid channel was found who
	 * assert the finish signal. */
	for (channel = 0; channel < DMAD_AHB_MAX_CHANNELS; ++channel) {
		if (getbl(channel+ERR_OFFSET, (unsigned long)INT_STA))
			return channel;
		if (getbl(channel+ABT_OFFSET, (unsigned long)INT_STA))
			return channel;
	}

	/* todo: spin_unlock */

	return -EFAULT;
}

EXPORT_SYMBOL_GPL(dmad_probe_irq_source_ahb);


/**
 * dmad_module_init - dma module-init function
 * @return : 0 if success, non-zero if any error
 */
int dmad_module_init(void)
{
	int err = 0;
	dmad_dbg("%s() >>\n", __func__);

	/* clear device struct since the module may be load/unload many times */
	memset(&dmad, 0, sizeof(dmad)-4);
	dmad.drq_pool =
	    kmalloc(DMAD_AHB_MAX_CHANNELS * sizeof(dmad_drq), GFP_KERNEL);
	if (dmad.drq_pool == NULL) {
		dmad_err("%s() failed to allocate drb pool!\n", __func__);
		return -ENOMEM;
	}
	memset(dmad.drq_pool, 0,DMAD_AHB_MAX_CHANNELS*sizeof(dmad_drq));
	spin_lock_init(&dmad.drq_pool_lock);
	dmad.ahb_drq_pool = dmad.drq_pool;
	dmad_dbg("DMA module init result: (%d)\n", err);
	dmad_dbg("  AHB channels: %d\n;"
		 "DRBs per channel: %d\n",
		 DMAC_MAX_CHANNELS, DMAD_DRB_POOL_SIZE);

	dmad_dbg("%s() return code (%d) <<\n", __func__, err);
	return err;
}

/**
 * dmad_module_init - dma module clean up function
 */
int __exit dmad_module_exit(struct platform_device *pdev)
{
	dmad_drq *drq;
	u32 channel;
	struct at_dma_platform_data *pdata;
	pdata = dev_get_platdata(&pdev->dev);

	dmad_dbg("%s() >>\n", __func__);

	spin_lock(&dmad.drq_pool_lock);

	/* cancel existing requests and unregister interrupt handler */
	for (channel = 0; channel < DMAD_AHB_MAX_CHANNELS; ++channel) {

		/* shutdown dma requests */
		drq = (dmad_drq *) & dmad.ahb_drq_pool[channel];

		if ((drq->state & DMAD_DRQ_STATE_READY) != 0)
			dmad_channel_drain(DMAD_DMAC_AHB_CORE, drq, 1);

		/* free registered irq handlers */
		free_irq(ahb_irqs[channel], (void *)(unsigned long)(channel + 1));
	}

	spin_unlock(&dmad.drq_pool_lock);

	if (dmad.drq_pool)
		kfree(dmad.drq_pool);
	memset(&dmad, 0, sizeof(dmad));

	/* release I/O space */
	release_region((uintptr_t)pdata->dmac_regs, resource_size(pdata->io));
	dmad_dbg("DMA module unloaded!\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id atcdma100_of_id_table[] = {
	{ .compatible = "andestech,atcdmac300" },
	{}
};
MODULE_DEVICE_TABLE(of, atcdma100_of_id_table);

static struct at_dma_platform_data *
at_dma_parse_dt(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct at_dma_platform_data *pdata;

	if (!np) {
		dev_err(&pdev->dev, "Missing DT data\n");
		return NULL;
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (of_property_read_u32(np, "dma-channels", &pdata->nr_channels))
		return NULL;

	return pdata;
}
#else
static inline struct at_dma_platform_data *
at_dma_parse_dt(struct platform_device *pdev)
{
	return NULL;
}
#endif

static int atcdma_probe(struct platform_device *pdev)
{
	struct at_dma_platform_data *pdata;
	struct resource 	*io=0;
	struct resource *mem = NULL;
	int			irq;

	pdata = dev_get_platdata(&pdev->dev);
	dmad.plat = pdev;

	if (!pdata)
		pdata = at_dma_parse_dt(pdev);
	pdev->dev.platform_data = pdata;
	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pdata->io = io;
	mem = request_mem_region(io->start, resource_size(io), pdev->name);

	if (!mem){
		printk("failed to get io memory region resouce.\n");
		return -EINVAL;
	}

	pdata->dmac_regs = (void __iomem *) ioremap(mem->start, resource_size(io));
	dmac_base = (uintptr_t)pdata->dmac_regs;

	irq = platform_get_irq(pdev, 0);

	if (irq < 0)
		return irq;

	intc_ftdmac020_init_irq(irq);

	return dmad_module_init();

}

static int __exit atcdma_remove(struct platform_device *pdev)
{
	return dmad_module_exit(pdev);
}


static struct platform_driver atcdma100_driver = {
	.probe		= atcdma_probe,
	.remove		= __exit_p(atcdma_remove),
	.driver = {
		.name	= "atcdmac100",
		.of_match_table = of_match_ptr(atcdma100_of_id_table),
	},
};

static int __init atcdma_init(void)
{
	return platform_driver_register(&atcdma100_driver);
}
subsys_initcall(atcdma_init);

#endif /* CONFIG_PLATFORM_AHBDMA */
