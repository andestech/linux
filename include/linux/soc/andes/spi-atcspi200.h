// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#ifndef __LINUX_SOC_ANDES_ATCSPI_H
#define __LINUX_SOC_ANDES_ATCSPI_H

#define SPI_XFER_BEGIN		(1 << 0)
#define SPI_XFER_END		(1 << 1)
#define SPI_XFER_DATA		(1 << 2)
#define SPI_XFER_ONCE		(SPI_XFER_BEGIN | SPI_XFER_END)
#define SPI_XFER_SHIFT		0

#define SPI_NAME		"atcspi200"
#define SPI_MAX_HZ		50000000
#define MAX_TRANSFER_LEN	512
#define CHUNK_SIZE		1
#define SPI_TIMEOUT		0x100000
#define NSPI_MAX_CS_NUM		1
#define DATA_LENGTH(x)		((x - 1) << 8)
#define ADDR_LENGTH(x)		((x - 1) << 16)

/* SPI Transfer Control Register */
#define ATCSPI200_TRANSFMT_OFFSET		24
#define ATCSPI200_TRANSFMT_MASK			(0x0F << ATCSPI200_TRANSFMT_OFFSET)
#define ATCSPI200_TRANSMODE_WR_SYNC		(0 << ATCSPI200_TRANSFMT_OFFSET)
#define ATCSPI200_TRANSMODE_W_ONLY		(1 << ATCSPI200_TRANSFMT_OFFSET)
#define ATCSPI200_TRANSMODE_R_ONLY		(2 << ATCSPI200_TRANSFMT_OFFSET)
#define ATCSPI200_TRANSMODE_WR			(3 << ATCSPI200_TRANSFMT_OFFSET)
#define ATCSPI200_TRANSMODE_NONEDATA		(7 << ATCSPI200_TRANSFMT_OFFSET)
#define ATCSPI200_TRANSMODE_DMYREAD		(9 << ATCSPI200_TRANSFMT_OFFSET)
#define ATCSPI200_TRANCNT_MASK			0x1FF
#define ATCSPI200_TRANSCTRL_WRTRANCNT_OFFSET	12
#define ATCSPI200_TRANSCTRL_WRTRANCNT_MASK	(ATCSPI200_TRANCNT_MASK << ATCSPI200_TRANSCTRL_WRTRANCNT_OFFSET)
#define ATCSPI200_TRANSCTRL_RDTRANCNT_OFFSET	0
#define ATCSPI200_TRANSCTRL_RDTRANCNT_MASK	(ATCSPI200_TRANCNT_MASK << ATCSPI200_TRANSCTRL_RDTRANCNT_OFFSET)
#define RDTRAN_CNT(x)				((x == 0) ? 0 : (x - 1) << 0)
#define DUMMY_CNT(x)				((x == 0) ? 0 : (x - 1) << 9)
#define WRTRAN_CNT(x)				((x == 0) ? 0 : (x - 1) << 12)
#define TOKEN_VAl(x)				((x == 0) ? 0 : (1 << 11))
#define TOKEN_EN(x)				((x == 0) ? 0 : (1 << 21))
#define DUAL_QUAD(x)				((x == 0) ? 0 : (x << 22))
#define TRANSMODE(x)				((x == 0) ? 0 : (x << 24))
#define ADDR_FMT(x)				((x == 0) ? 0 : (1 << 28))
#define ADDR_EN(x)				((x == 0) ? 0 : (1 << 29))
#define CMD_EN(x)				((x == 0) ? 0 : (1 << 30))

/* SPI Control Register */
#define ATCSPI200_CTRL_TXFIFORST_MASK		(1 << 2)
#define ATCSPI200_CTRL_RXFIFORST_MASK		(1 << 1)
#define ATCSPI200_CTRL_SPIRST_MASK		(1 << 0)

/* SPI Transfer Format Register */
#define ATCSPI200_TRANSFMT_CPHA_MASK		(1UL << 0)
#define ATCSPI200_TRANSFMT_CPOL_MASK		(1UL << 1)
#define ATCSPI200_TRANSFMT_DATA_LEN_OFFSET	(8)
#define ATCSPI200_TRANSFMT_DATA_LEN_MASK	(0x1F << ATCSPI200_TRANSFMT_DATA_LEN_OFFSET)
#define ATCSPI200_TRANSFMT_ADDR_LEN_OFFSET	(16)
#define ATCSPI200_TRANSFMT_ADDR_LEN_MASK	(0x3 << ATCSPI200_TRANSFMT_ADDR_LEN_OFFSET)

/* SPI Status Register */
#define ATCSPI200_STATUS_TXEMPTY_OFFSET		(1 << 22)
#define ATCSPI200_STATUS_TXFULL_OFFSET		(1 << 23)
#define ATCSPI200_STATUS_RXEMPTY_OFFSET		(1 << 14)
#define ATCSPI200_STATUS_TXNUM_LOWER_OFFSET	(16)
#define ATCSPI200_STATUS_TXNUM_LOWER_MASK	(0x1F << ATCSPI200_STATUS_TXNUM_LOWER_OFFSET)
#define ATCSPI200_STATUS_RXNUM_LOWER_OFFSET	(8)
#define ATCSPI200_STATUS_RXNUM_LOWER_MASK	(0x1F << ATCSPI200_STATUS_RXNUM_LOWER_OFFSET)
#define ATCSPI200_STATUS_SPIACTIVE_MASK		(1 << 0)

/* SPI Interface timing Setting */
#define ATCSPI200_TIMING_SCLK_DIV_MASK		0xFF

/* ATCSPI200 registers */
#define SPI_IDREV		0x00	// ID and Revision Register
#define SPI_TRANSFMT		0x10	// SPI Transfer Format Register
#define SPI_DIRECTIO		0x14	// SPI Direct IO Control Register
#define SPI_TRANSCTRL		0x20	// SPI Transfer Control Register
#define SPI_CMD			0x24	// SPI Command Register
#define SPI_ADDR		0x28	// SPI Address Register
#define SPI_DATA		0x2C	// SPI Data Register
#define SPI_CTRL		0x30	// SPI Control Register
#define SPI_STATUS		0x34	// SPI Status Register
#define SPI_INTR_EN		0x38	// SPI Interrupt Enable Register
#define SPI_INTR_ST		0x3C	// SPI Interrupt Status Registe
#define SPI_TIMING		0x40	// SPI Interface timing Register

/* SPI transfer mode */
#define REGULAR_MODE		0x1
#define DUAL_MODE		0x2
#define QUAD_MODE		0x4

struct ts_buf {
	u8 cmd;
	__be16 data;
} __packed;

static bool ts_enable;

struct atcspi200_spi {
	void __iomem	*regs;
	struct clk	*clk;
	size_t		trans_len;
	size_t		data_len;
	size_t		cmd_len;
	u32		clk_rate;
	u8		cmd_buf[16];
	u8		*din;
	u8		*dout;
	struct ts_buf	*tx_buf;
	struct ts_buf	*rx_buf;
	unsigned int	addr;
	unsigned int	max_transfer_length;
	unsigned int	freq;
	unsigned int	mode;
	unsigned int	mtiming;
	int		timeout;
	spinlock_t	lock;
	struct mutex	mutex_lock;
};

#endif /* !__LINUX_SOC_ANDES_ATCSPI_H */
