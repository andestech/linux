/*
 *  linux/driver/mmc/ftsdc010.h - Andestech MMC/SD driver
 *  Andestech FTSDC010 Device Driver
 *
 *  Andestech (C) 2005 Faraday Corp. (http://www.Andestech.com)
 *
 *  All Rights Reserved
 */
#ifndef _FTSDC010_H_
#define _FTSDC010_H_

#define DELAY_FOR_DMA_READ

#ifdef SD_DEBUG
	#define P_DEBUG(fmt, args...) printk(KERN_ALERT "SD: " fmt, ## args)
#else
	#define P_DEBUG(a...)
#endif
#define P_DEBUGG(a...)

/* used for dma timeout */
#define SDC_TIMEOUT_BASE			(HZ/2)	// Unit is 500 ms

/* used for pio retry times */
#define SDC_PIO_RETRY				0x300000

/* sd controller register */
#define SDC_CMD_REG				0x00000000
#define SDC_ARGU_REG				0x00000004
#define SDC_RESPONSE0_REG			0x00000008
#define SDC_RESPONSE1_REG			0x0000000C
#define SDC_RESPONSE2_REG			0x00000010
#define SDC_RESPONSE3_REG			0x00000014
#define SDC_RSP_CMD_REG				0x00000018
#define SDC_DATA_CTRL_REG			0x0000001C
#define SDC_DATA_TIMER_REG			0x00000020
#define SDC_DATA_LEN_REG			0x00000024
#define SDC_STATUS_REG				0x00000028
#define SDC_CLEAR_REG				0x0000002C
#define SDC_INT_MASK_REG			0x00000030
#define SDC_POWER_CTRL_REG			0x00000034
#define SDC_CLOCK_CTRL_REG			0x00000038
#define SDC_BUS_WIDTH_REG			0x0000003C
#define SDC_DATA_WINDOW_REG			0x00000040

#ifdef A320D_BUILDIN_SDC
#define SDC_FEATURE_REG				0x00000044
#define SDC_REVISION_REG			0x00000048
#else
#define SDC_MMC_INT_RSP_REG			0x00000044
#define SDC_GP_OUTPUT_REG			0x00000048
#define SDC_FEATURE_REG				0x0000009C
#define SDC_REVISION_REG			0x000000A0
#endif

#define SDC_SDIO_CTRL1_REG			0x0000006C
#define SDC_SDIO_CTRL2_REG			0x00000070
#define SDC_SDIO_STATUS_REG			0x00000074

/* bit mapping of command register */
#define SDC_CMD_REG_INDEX			0x0000003F
#define SDC_CMD_REG_NEED_RSP			0x00000040
#define SDC_CMD_REG_LONG_RSP			0x00000080
#define SDC_CMD_REG_APP_CMD			0x00000100
#define SDC_CMD_REG_CMD_EN			0x00000200
#define SDC_CMD_REG_SDC_RST			0x00000400
#define SDC_CMD_MMC_INT_STOP			0x00000800

/* bit mapping of response command register */
#define SDC_RSP_CMD_REG_INDEX			0x0000003F
#define SDC_RSP_CMD_REG_APP			0x00000040

/* bit mapping of data control register */
#define SDC_DATA_CTRL_REG_BLK_SIZE		0x0000000F
#define SDC_DATA_CTRL_REG_DATA_WRITE		0x00000010
#define SDC_DATA_CTRL_REG_DMA_EN		0x00000020
#define SDC_DATA_CTRL_REG_DATA_EN		0x00000040
#define SDC_DATA_CTRL_REG_FIFOTH		0x00000080
#define SDC_DATA_CTRL_REG_DMA_TYPE		0x00000300
#define SDC_DATA_CTRL_REG_FIFO_RST		0x00000400
#define SDC_CPRM_DATA_CHANGE_ENDIAN_EN		0x00000800
#define SDC_CPRM_DATA_SWAP_HL_EN		0x00001000

#define SDC_DMA_TYPE_1				0x00000000
#define SDC_DMA_TYPE_4				0x00000100
#define SDC_DMA_TYPE_8				0x00000200

/* bit mapping of status register */
#define SDC_STATUS_REG_RSP_CRC_FAIL		0x00000001
#define SDC_STATUS_REG_DATA_CRC_FAIL		0x00000002
#define SDC_STATUS_REG_RSP_TIMEOUT		0x00000004
#define SDC_STATUS_REG_DATA_TIMEOUT		0x00000008
#define SDC_STATUS_REG_RSP_CRC_OK		0x00000010
#define SDC_STATUS_REG_DATA_CRC_OK		0x00000020
#define SDC_STATUS_REG_CMD_SEND			0x00000040
#define SDC_STATUS_REG_DATA_END			0x00000080
#define SDC_STATUS_REG_FIFO_UNDERRUN		0x00000100
#define SDC_STATUS_REG_FIFO_OVERRUN		0x00000200
#define SDC_STATUS_REG_CARD_CHANGE		0x00000400
#define SDC_STATUS_REG_CARD_DETECT		0x00000800
#define SDC_STATUS_REG_CARD_LOCK		0x00001000
#define SDC_STATUS_REG_CP_READY			0x00002000
#define SDC_STATUS_REG_CP_BUF_READY		0x00004000
#define SDC_STATUS_REG_PLAIN_TEXT_READY		0x00008000
#define SDC_STATUS_REG_SDIO_INTR	    	0x00010000

/* bit mapping of clear register */
#define SDC_CLEAR_REG_RSP_CRC_FAIL		0x00000001
#define SDC_CLEAR_REG_DATA_CRC_FAIL		0x00000002
#define SDC_CLEAR_REG_RSP_TIMEOUT		0x00000004
#define SDC_CLEAR_REG_DATA_TIMEOUT		0x00000008
#define SDC_CLEAR_REG_RSP_CRC_OK		0x00000010
#define SDC_CLEAR_REG_DATA_CRC_OK		0x00000020
#define SDC_CLEAR_REG_CMD_SEND			0x00000040
#define SDC_CLEAR_REG_DATA_END			0x00000080
#define SDC_CLEAR_REG_CARD_CHANGE		0x00000400
#define SDC_CLEAR_REG_SDIO_INTR			0x00010000

/* bit mapping of int_mask register */
#define SDC_INT_MASK_REG_RSP_CRC_FAIL		0x00000001
#define SDC_INT_MASK_REG_DATA_CRC_FAIL		0x00000002
#define SDC_INT_MASK_REG_RSP_TIMEOUT		0x00000004
#define SDC_INT_MASK_REG_DATA_TIMEOUT		0x00000008
#define SDC_INT_MASK_REG_RSP_CRC_OK		0x00000010
#define SDC_INT_MASK_REG_DATA_CRC_OK		0x00000020
#define SDC_INT_MASK_REG_CMD_SEND		0x00000040
#define SDC_INT_MASK_REG_DATA_END		0x00000080
#define SDC_INT_MASK_REG_FIFO_UNDERRUN		0x00000100
#define SDC_INT_MASK_REG_FIFO_OVERRUN		0x00000200
#define SDC_INT_MASK_REG_CARD_CHANGE		0x00000400
#define SDC_INT_MASK_REG_CARD_LOCK		0x00001000
#define SDC_INT_MASK_REG_CP_READY		0x00002000
#define SDC_INT_MASK_REG_CP_BUF_READY		0x00004000
#define SDC_INT_MASK_REG_PLAIN_TEXT_READY	0x00008000
#define SDC_INT_MASK_REG_SDIO_INTR	    	0x00010000


#define SDC_CARD_INSERT				0x0
#define SDC_CARD_REMOVE				SDC_STATUS_REG_CARD_DETECT

/* bit mapping of power control register */
#define SDC_POWER_CTRL_REG_POWER_ON		0x00000010
#define SDC_POWER_CTRL_REG_POWER_BITS		0x0000000F

/* bit mapping of clock control register */
#define SDC_CLOCK_CTRL_REG_CLK_DIV		0x0000007F
#define SDC_CLOCK_CTRL_REG_CARD_TYPE		0x00000080
#define SDC_CLOCK_CTRL_REG_CLK_DIS		0x00000100

/* card type */
#define SDC_CARD_TYPE_SD			SDC_CLOCK_REG_CARD_TYPE
#define SDC_CARD_TYPE_MMC			0x0

/* bit mapping of bus width register */
#define SDC_BUS_WIDTH_REG_SINGLE_BUS		0x00000001
#define SDC_BUS_WIDTH_REG_WIDE_8_BUS		0x00000002
#define SDC_BUS_WIDTH_REG_WIDE_4_BUS		0x00000004
#define SDC_BUS_WIDTH_REG_WIDE_BUS_SUPPORT	0x00000018
#define SDC_BUS_WIDTH_REG_CARD_DETECT		0x00000020

#define SDC_WIDE_4_BUS_SUPPORT			0x00000008
#define SDC_WIDE_8_BUS_SUPPORT			0x00000010

/* bit mapping of feature register */
#define SDC_FEATURE_REG_FIFO_DEPTH		0x000000FF
#define SDC_FEATURE_REG_CPRM_FUNCTION		0x00000100

/* bit mapping of sdio control register */
#define SDC_SDIO_CTRL1_REG_SDIO_BLK_NO		0xFFFF8000
#define SDC_SDIO_CTRL1_REG_SDIO_ENABLE		0x00004000
#define SDC_SDIO_CTRL1_REG_READ_WAIT_ENABLE	0x00002000
#define SDC_SDIO_CTRL1_REG_SDIO_BLK_MODE	0x00001000
#define SDC_SDIO_CTRL1_REG_SDIO_BLK_SIZE	0x00000FFF

/* bit mapping of sdio status register */
#define SDC_SDIO_SDIO_STATUS_REG_FIFO_REMAIN_NO	0x00FE0000
#define SDC_SDIO_SDIO_STATUS_REG_SDIO_BLK_CNT	0x0001FFFF

enum ftsdc_waitfor {
	COMPLETION_NONE,
	COMPLETION_FINALIZE,
	COMPLETION_CMDSENT,
	COMPLETION_RSPFIN,
	COMPLETION_XFER_PROGRESS,
};

struct ftsdc_host {
	struct platform_device	*pdev;
	struct mmc_host		*mmc;
	struct resource		*mem;
	struct clk		*clk;
	void __iomem		*base;
	int			irq;

	unsigned int		real_rate;
	bool			irq_enabled;
	unsigned int		fifo_len;	/* bytes */
	unsigned int		last_opcode;	/* keep last successful cmd to judge application specific command */

	struct mmc_request	*mrq;
	int			cmd_is_stop;

	spinlock_t		complete_lock;
	enum ftsdc_waitfor	complete_what;

	struct completion	dma_complete;
	dmad_chreq		*dma_req;
	bool			dodma;
	bool			dma_finish;


	u32			buf_sgptr;	/* keep next scallterlist buffer index */
	u32			buf_bytes;	/* keep current total scallterlist buffer length */
	u32			buf_count;	/* keep real data size rw from sd */
	u32			*buf_ptr;	/* keep current scallterlist buffer address */
#define XFER_NONE 0
#define XFER_READ 1
#define XFER_WRITE 2
	u32			buf_active;	/* keep current transfer mode */

	int			bus_width;

	char 			dbgmsg_cmd[301];
	char 			dbgmsg_dat[301];
	char			*status;

	unsigned int		ccnt, dcnt;
	struct tasklet_struct	pio_tasklet;
	struct work_struct work;

#ifdef CONFIG_DEBUG_FS
	struct dentry		*debug_root;
	struct dentry		*debug_state;
	struct dentry		*debug_regs;
#endif
};

struct ftsdc_mmc_config {
	/* get_cd()/get_wp() may sleep */
	int	(*get_cd)(int module);
	int	(*get_ro)(int module);

	void	(*set_power)(int module, bool on);

	/* wires == 0 is equivalent to wires == 4 (4-bit parallel) */
	u8	wires;

	u32     max_freq;

	/* any additional host capabilities: OR'd in to mmc->f_caps */
	u32     caps;

	/* Number of sg segments */
	u8	nr_sg;
};

#endif
