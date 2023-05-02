/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Andes Technology Corporation
 *
 */

#ifndef __NDS_DMAD_ATF_INC__
#define __NDS_DMAD_ATF_INC__

/*****************************************************************************
 * Configuration section
 *****************************************************************************/
/* Debug trace enable switch */
#define DMAD_ERROR_TRACE            1       /* message for fatal errors */
#define DMAD_DEBUG_TRACE            0       /* message for debug trace */
typedef u32 addr_t;

/* Device base address */

extern resource_size_t	dmac_base;
#define DMAC_BASE			(dmac_base)

/* ID and Revision Register */
#define ID_REV				(DMAC_BASE + 0x00)
/* DMAC Configuration Register*/
#define CFG				(DMAC_BASE + 0x10)
#define REQSYNC				30
#define CTL				(DMAC_BASE + 0x20)
#define CH_ABT				(DMAC_BASE + 0x24)
/* Interrupt Status Register */
#define INT_STA				(DMAC_BASE + 0x30)
#define TC_OFFSET			16
#define ABT_OFFSET			8
#define ERR_OFFSET			0


#define CH_EN				(DMAC_BASE + 0x34)



#define DMAC_CH_OFFSET			0x40
#define CH_CTL_OFF			0x0
#define CH_SIZE_OFF			0x4
#define CH_SRC_LOW_OFF			0x8
#define CH_SRC_HIGH_OFF			0xc
#define CH_DST_LOW_OFF			0x10
#define CH_DST_HIGH_OFF			0x14
#define CH_LLP_LOW_OFF			0x18
#define CH_LLP_HIGH_OFF			0x1c


#define DMAC_C0_BASE			(DMAC_BASE + DMAC_CH_OFFSET)
#define DMAC_MAX_CHANNELS		8
#define DMAC_BASE_CH(n)			(DMAC_C0_BASE + n*0x20)

/***** Channel n Control Register ******/
#define CH_CTL(n)			(DMAC_BASE_CH(n)+CH_CTL_OFF)
#define PRIORITY_SHIFT			29
#define PRIORITY_LOW			0
#define PRIORITY_HIGH			1
#define DMAC_CSR_CHPRI_0		PRIORITY_LOW
#define DMAC_CSR_CHPRI_1		PRIORITY_LOW
#define DMAC_CSR_CHPRI_2		PRIORITY_HIGH
#define DMAC_CSR_CHPRI_3		PRIORITY_HIGH


#define SBURST_SIZE_SHIFT		24
#define SBURST_SIZE_MASK		(0xf<<24)
#define DMAC_CSR_SIZE_1			0x0
#define DMAC_CSR_SIZE_2			0x1
#define DMAC_CSR_SIZE_4			0x2
#define DMAC_CSR_SIZE_8			0x3
#define DMAC_CSR_SIZE_16		0x4
#define DMAC_CSR_SIZE_32		0x5
#define DMAC_CSR_SIZE_64		0x6
#define DMAC_CSR_SIZE_128		0x7
#define DMAC_CSR_SIZE_256		0x8
#define DMAC_CSR_SIZE_512		0x9
#define DMAC_CSR_SIZE_1024		0xa
/* Source transfer width */
#define SRCWIDTH			21
#define SRCWIDTH_MASK			(0x7<<SRCWIDTH)
#define WIDTH_1				0x0
#define WIDTH_2				0x1
#define WIDTH_4				0x2
#define WIDTH_8				0x3
#define WIDTH_16			0x4
#define WIDTH_32			0x5
#define DMAC_CSR_WIDTH_8	      WIDTH_1
#define DMAC_CSR_WIDTH_16	      WIDTH_2
#define DMAC_CSR_WIDTH_32	      WIDTH_4


/* Destination transfer width */
#define DSTWIDTH			18
#define DSTWIDTH_MASK			(0x7<<DSTWIDTH)
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
/* Source DMA request select */
#define SRCREQSEL			8
#define SRCREQSEL_MASK			(0xf<<SRCREQSEL)
/* Destination DMA request select */
#define DSTREQSEL			4
#define DSTREQSEL_MASK			(0xf<<DSTREQSEL)


/* Channel abort interrupt mask */
#define INTABTMASK			(3)
/* Channel error interrupt mask */
#define INTERRMASK			(2)
/* Channel terminal count interrupt mask */
#define INTTCMASK			(1)
/* Channel Enable */
#define CHEN				(0)

/***** Channel n Transfer Size Register ******/
#define CH_SIZE(n)			(DMAC_BASE_CH(n)+CH_SIZE_OFF)
/* total transfer size from source */
#define DMAC_TOT_SIZE_MASK          0xffffffff




#define CH_SRC_L(n)			(DMAC_BASE_CH(n)+CH_SRC_LOW_OFF)
#define CH_SRC_H(n)			(DMAC_BASE_CH(n)+CH_SRC_HIGH_OFF)
#define CH_DST_L(n)			(DMAC_BASE_CH(n)+CH_DST_LOW_OFF)
#define CH_DST_H(n)			(DMAC_BASE_CH(n)+CH_DST_HIGH_OFF)
#define CH_LLP_L(n)			(DMAC_BASE_CH(n)+CH_LLP_LOW_OFF)
#define CH_LLP_H(n)			(DMAC_BASE_CH(n)+CH_LLP_HIGH_OFF)


typedef struct channel_control {
	u32	sWidth;
	u32	sCtrl;
	u32	sReqn;
	u32	dWidth;
	u32	dCtrl;
	u32	dReqn;
} channel_control;






/* DMA channel 0 registers (32-bit width) */
#define DMAC_C0_CSR                 (DMAC_C0_BASE + DMAC_CSR_OFFSET)
#define DMAC_C0_CFG                 (DMAC_C0_BASE + DMAC_CFG_OFFSET)
#define DMAC_C0_SRC_ADDR            (DMAC_C0_BASE + DMAC_SRC_ADDR_OFFSET)
#define DMAC_C0_DST_ADDR            (DMAC_C0_BASE + DMAC_DST_ADDR_OFFSET)
#define DMAC_C0_LLP                 (DMAC_C0_BASE + DMAC_LLP_OFFSET)
#define DMAC_C0_SIZE                (DMAC_C0_BASE + DMAC_SIZE_OFFSET)


#ifdef CONFIG_PLATFORM_AHBDMA
#define DMAC_CYCLE_TO_BYTES(cycle, width) ((cycle) << (width))
#define DMAC_BYTES_TO_CYCLE(bytes, width) ((bytes) >> (width))
#else
#define DMAC_CYCLE_TO_BYTES(cycle, width) 0
#define DMAC_BYTES_TO_CYCLE(bytes, width) 0
#endif  /* CONFIG_PLATFORM_AHBDMA */


/* Assignment of DMA hardware handshake ID */
#define DMAC_REQN_SPITX			0
#define DMAC_REQN_SPIRX			1
#ifdef CONFIG_PLAT_AE350
#define DMAC_REQN_I2SAC97TX		14
#define DMAC_REQN_I2SAC97RX		15
#else

#define DMAC_REQN_I2SAC97TX		2
#define DMAC_REQN_I2SAC97RX		3
#endif
#define DMAC_REQN_UART1TX		4
#define DMAC_REQN_UART1RX		5
#define DMAC_REQN_UART2TX		6
#define DMAC_REQN_UART2RX		7
#define DMAC_REQN_I2C			8
#define DMAC_REQN_SDC			9
#define DMAC_REQN_NONE			16


enum DMAD_DMAC_CORE {
	DMAD_DMAC_AHB_CORE,
	DMAD_DMAC_APB_CORE
};

enum DMAD_CHREG_FLAGS {
	DMAD_FLAGS_NON_BLOCK    = 0x00000000,
	DMAD_FLAGS_SLEEP_BLOCK  = 0x00000001,
	DMAD_FLAGS_SPIN_BLOCK   = 0x00000002,
	DMAD_FLAGS_RING_MODE    = 0x00000008,   /* ring submission mode */
	DMAD_FLAGS_BIDIRECTION  = 0x00000010,   /* indicates both tx and rx */
};

enum DMAD_CHDIR {
	DMAD_DIR_A0_TO_A1       = 0,
	DMAD_DIR_A1_TO_A0       = 1,
};

/* AHB Channel Request
 *
 * Notes for developers:
 *   These should be channel-only properties. Controller-specific properties
 *   should be separated as other driver structure or driver buildin-hardcode.
 *   If controller properties are embeded in this union, request for a channel
 *   may unexpectedly override the controller setting of the request of other
 *   channels.
 */
typedef struct dmad_ahb_chreq {
	/* channel property */
	u32 sync;                       /* (in)  different clock domain */
	u32 priority;                   /* (in)  DMAC_CSR_CHPRI_xxx */
	u32 hw_handshake;               /* (in)  hardware handshaking on/off */
	u32 burst_size;                 /* (in)  DMAC_CSR_SIZE_xxx */

	/* source property */
	union {
		u32 src_width;          /* (in)  DMAC_CSR_WIDTH_xxx */
		u32 addr0_width;        /* (in)  bi-direction mode alias */
		u32 ring_width;         /* (in)  ring-mode alias */
	};
	union {
		u32 src_ctrl;           /* (in)  DMAC_CSR_AD_xxx */
		u32 addr0_ctrl;         /* (in)  bi-direction mode alias */
		u32 ring_ctrl;          /* (in)  ring-mode alias */
	};
	union {
		u32 src_reqn;           /* (in)  DMAC_REQN_xxx */
		u32 addr0_reqn;         /* (in)  bi-direction mode alias */
		u32 ring_reqn;          /* (in)  ring-mode alias */
	};

	/* destination property */
	union {
		u32 dst_width;          /* (in)  DMAC_CSR_WIDTH_xxx */
		u32 addr1_width;        /* (in)  bi-direction mode alias */
		u32 dev_width;          /* (in)  ring-mode alias */
	};
	union {
		u32 dst_ctrl;           /* (in)  DMAC_CSR_AD_xxx */
		u32 addr1_ctrl;         /* (in)  bi-direction mode alias */
		u32 dev_ctrl;           /* (in)  ring-mode alias */
	};
	union {
		u32 dst_reqn;           /* (in)  DMAC_REQN_xxx */
		u32 addr1_reqn;         /* (in)  bi-direction mode alias */
		u32 dev_reqn;           /* (in)  ring-mode alias */
	};

	/* (in)  transfer direction, valid only if following flags were set ...
	 *         DMAD_FLAGS_BIDIRECTION or
	 *         DMAD_FLAGS_RING_MODE
	 *       value:
	 *         0 (addr0 -> addr1, or ring-buff to device)
	 *         1 (addr0 <- addr1, or device to ring-buff)
	 */
	u32 tx_dir;

} dmad_ahb_chreq;

/* APB Channel Request
 *
 * Notes for developers:
 *   These should be channel-only properties. Controller-specific properties
 *   should be separated as other driver structure or driver buildin-hardcode.
 *   If controller properties are embeded in this union, request for a channel
 *   may unexpectedly override the controller setting of the request of other
 *   channels.
 */
typedef struct dmad_apb_chreq {
	/* controller property (removed! should not exist in this struct) */

	/* channel property */
	u32 burst_mode;                 /* (in)  Burst mode (0/1) */
	u32 data_width;                 /* (in)  APBBR_DATAWIDTH_xxx */

	/* source property */
	union {
		u32 src_ctrl;           /* (in)  APBBR_ADDRINC_xxx */
		u32 addr0_ctrl;         /* (in)  bi-direction mode alias */
		u32 ring_ctrl;          /* (in)  ring-mode alias */
	};
	union {
		u32 src_reqn;           /* (in)  APBBR_REQN_xxx */
		u32 addr0_reqn;         /* (in)  bi-direction mode alias */
		u32 ring_reqn;          /* (in)  ring-mode alias */
	};

	/* destination property */
	union {
		u32 dst_ctrl;           /* (in)  APBBR_ADDRINC_xxx */
		u32 addr1_ctrl;         /* (in)  bi-direction mode alias */
		u32 dev_ctrl;           /* (in)  ring-mode alias */
	};
	union {
		u32 dst_reqn;           /* (in)  APBBR_REQN_xxx */
		u32 addr1_reqn;         /* (in)  bi-direction mode alias */
		u32 dev_reqn;           /* (in)  ring-mode alias */
	};

	/* (in)  transfer direction, valid only if following flags were set ...
	 *         DMAD_FLAGS_BIDIRECTION or
	 *         DMAD_FLAGS_RING_MODE
	 *       value:
	 *         0 (addr0 -> addr1, or ring-buff to device)
	 *         1 (addr0 <- addr1, or device to ring-buff)
	 */
	u32 tx_dir;

} dmad_apb_chreq;

/* Channel Request Descriptor */
typedef struct dmad_chreq {
	/* common fields */
	u32     controller;                 /* (in)  enum DMAD_DMAC_CORE */
	u32	flags;                      /* (in)  enum DMAD_CHREQ_FLAGS */

	/**********************************************************************
	 * ring mode specific fields (valid only for DMAD_FLAGS_RING_MODE)
	 * note:
	 *  - size fields are in unit of data width
	 *    * for AHB, ring size is limited to 4K * data_width of data if
	 *      hw-LLP is not used
	 *    * for AHB, ring size is limited to 4K * data_width * LLP-count
	 *      hw-if LLP is used
	 *    * for APB, ring size is limited to 16M * data_width of data
	 *  - currently sw ring mode dma supports only fixed or incremental
	 *    src/dst addressing
	 *  - ring_size shoule >= periods * period_size
	 */
	dma_addr_t ring_base;               /* (in)  ring buffer base (pa) */
	dma_addr_t ring_size;               /* (in)  unit of data width */
	dma_addr_t     dev_addr;                /* (in)  device data port address */
	dma_addr_t periods;                 /* (in)  number of ints per ring */
	dma_addr_t period_size;             /* (in)  size per int, data-width */


	/* channel-wise completion callback - called when hw-ptr catches sw-ptr
	 * (i.e., channel stops)
	 *
	 * completion_cb:   (in) client supplied callback function, executed in
	 *                       interrupt context.
	 * completion_data: (in) client private data to be passed to data
	 *                       argument of completion_cb().
	 */
	void (*completion_cb)(int channel, u16 status, void *data);
	void *completion_data;
	/*********************************************************************/

	/* channel allocation output */
	u32     channel;                    /* (out) allocated channel */
	void    *drq;                       /* (out) internal use (DMAD_DRQ *)*/

	/* channel-alloc parameters (channel-wise properties) */
	union {
#ifdef CONFIG_PLATFORM_AHBDMA
		dmad_ahb_chreq ahb_req;     /* (in)  for AHB DMA parameters */
#endif
#ifdef CONFIG_PLATFORM_APBDMA
		dmad_apb_chreq apb_req;     /* (in)  APB Bridge DMA params */
#endif
	};

} dmad_chreq;

/* drb states are mutual exclusive */
enum DMAD_DRB_STATE {
	DMAD_DRB_STATE_FREE             = 0,
	DMAD_DRB_STATE_READY            = 0x00000001,
	DMAD_DRB_STATE_SUBMITTED        = 0x00000002,
	DMAD_DRB_STATE_EXECUTED         = 0x00000004,
	DMAD_DRB_STATE_COMPLETED        = 0x00000008,
	//DMAD_DRB_STATE_ERROR          = 0x00000010,
	DMAD_DRB_STATE_ABORT            = 0x00000020,
};

/* DMA request block
 * todo: replaced link with kernel struct list_head ??
 */
typedef struct dmad_drb {
	u32  prev;                       /* (internal) previous node */
	u32  next;                       /* (internal) next node */
	u32  node;                       /* (internal) this node */

	u32  state;                      /* (out) DRB's current state */

	union {
		dma_addr_t src_addr;     /* (in)  source pa */
		dma_addr_t addr0;        /* (in)  bi-direction mode alias */
	};

	union {
		dma_addr_t dst_addr;     /* (in)  destination pa */
		dma_addr_t addr1;        /* (in)  bi-direction mode alias */
	};

	/* (in) AHB DMA (22 bits): 0 ~ 4M-1, unit is "data width"
	 *      APB DMA (24 bits): 0 ~ 16M-1, unit is "data width * burst size"
	 *      => for safe without mistakes, use dmad_make_req_cycles() to
	 *         compose this value if the addressing mode is incremental
	 *         mode (not working yet for decremental mode).
	 */
	dma_addr_t req_cycle;

	/* (in)  if non-null, this sync object will be signaled upon dma
	 * completion (for blocked-waiting dma completion)
	 */
	struct completion *sync;

} dmad_drb;


/******************************************************************************
 * Debug Trace Mechanism
 */
#if (DMAD_ERROR_TRACE)
#define dmad_err(format, arg...)  pr_err(format, ## arg)
#else
#define dmad_err(format, arg...)
#endif

#if (DMAD_DEBUG_TRACE)
#define dmad_dbg(format, arg...)  pr_err(format, ## arg)
#else
#define dmad_dbg(format, arg...)
#endif

#if (defined(CONFIG_PLATFORM_AHBDMA) || defined(CONFIG_PLATFORM_APBDMA))

/******************************************************************************
 * DMAD Driver Interface
 ******************************************************************************
 */
extern int dmad_channel_alloc(dmad_chreq *ch_req);
extern int dmad_channel_free(dmad_chreq *ch_req);
extern int dmad_channel_enable(const dmad_chreq *ch_req, u8 enable);
extern u32 dmad_max_size_per_drb(dmad_chreq *ch_req);
extern u32 dmad_bytes_to_cycles(dmad_chreq *ch_req, u32 byte_size);

extern int dmad_kickoff_requests(dmad_chreq *ch_req);
extern int dmad_drain_requests(dmad_chreq *ch_req, u8 shutdown);

/* for performance reason, these two functions are platform-specific */
#ifdef CONFIG_PLATFORM_AHBDMA
extern int dmad_probe_irq_source_ahb(void);
#endif
#ifdef CONFIG_PLATFORM_APBDMA
extern int dmad_probe_irq_source_apb(void);
#endif

/* note: hw_ptr here is phyical address of dma source or destination */
extern dma_addr_t dmad_probe_hw_ptr_src(dmad_chreq *ch_req);
extern dma_addr_t dmad_probe_hw_ptr_dst(dmad_chreq *ch_req);

/*****************************************************************************
 * routines only valid in discrete (non-ring) mode
 */
extern int dmad_config_channel_dir(dmad_chreq *ch_req, u8 dir);
extern int dmad_alloc_drb(dmad_chreq *ch_req, dmad_drb **drb);
extern int dmad_free_drb(dmad_chreq *ch_req, dmad_drb *drb);
extern int dmad_submit_request(dmad_chreq *ch_req,
			       dmad_drb *drb, u8 keep_fired);
extern int dmad_withdraw_request(dmad_chreq *ch_req, dmad_drb *drb);
/****************************************************************************/

/*****************************************************************************
 * routines only valid in ring mode
 * note: sw_ptr and hw_ptr are values offset from the ring buffer base
 *       unit of sw_ptr is data-width
 *       unit of hw_ptr returned is byte
 */
extern int dmad_update_ring(dmad_chreq *ch_req);
extern int dmad_update_ring_sw_ptr(dmad_chreq *ch_req,
				   dma_addr_t sw_ptr, u8 keep_fired);
extern dma_addr_t dmad_probe_ring_hw_ptr(dmad_chreq *ch_req);
/****************************************************************************/

#else  /* CONFIG_PLATFORM_AHBDMA || CONFIG_PLATFORM_APBDMA */

static inline int dmad_channel_alloc(dmad_chreq *ch_req) { return -EFAULT; }
static inline int dmad_channel_free(dmad_chreq *ch_req) { return -EFAULT; }
static inline int dmad_channel_enable(const dmad_chreq *ch_req, u8 enable)
	{ return -EFAULT; }
static inline u32 dmad_max_size_per_drb(dmad_chreq *ch_req) { return 0; }
static inline u32 dmad_bytes_to_cycles(dmad_chreq *ch_req, u32 byte_size)
	{ return 0; }
static inline int dmad_kickoff_requests(dmad_chreq *ch_req) { return -EFAULT; }
static inline int dmad_drain_requests(dmad_chreq *ch_req, u8 shutdown)
	{ return -EFAULT; }
static inline int dmad_probe_irq_source_ahb(void) { return -EFAULT; }
static inline int dmad_probe_irq_source_apb(void) { return -EFAULT; }
static inline dma_addr_t dmad_probe_hw_ptr_src(dmad_chreq *ch_req)
	{ return (dma_addr_t)NULL; }
static inline dma_addr_t dmad_probe_hw_ptr_dst(dmad_chreq *ch_req)
	{ return (dma_addr_t)NULL; }
static inline int dmad_config_channel_dir(dmad_chreq *ch_req, u8 dir)
	{ return -EFAULT; }
static inline int dmad_alloc_drb(dmad_chreq *ch_req, dmad_drb **drb)
	{ return -EFAULT; }
static inline int dmad_free_drb(dmad_chreq *ch_req, dmad_drb *drb)
	{ return -EFAULT; }
static inline int dmad_submit_request(dmad_chreq *ch_req,
		dmad_drb *drb, u8 keep_fired) { return -EFAULT; }
static inline int dmad_withdraw_request(dmad_chreq *ch_req, dmad_drb *drb)
	{ return -EFAULT; }
static inline int dmad_update_ring(dmad_chreq *ch_req)
	{ return -EFAULT; }
static inline int dmad_update_ring_sw_ptr(dmad_chreq *ch_req,
	dma_addr_t sw_ptr, u8 keep_fired) { return -EFAULT; }
static inline dma_addr_t dmad_probe_ring_hw_ptr(dmad_chreq *ch_req)
	{ return (dma_addr_t)NULL; }

#endif  /* CONFIG_PLATFORM_AHBDMA || CONFIG_PLATFORM_APBDMA */

#endif  /* __NDS_DMAD_ATF_INC__ */

