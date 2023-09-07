/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Andes technology Corporation
 */

#ifndef SPI_ANDES_H
#define SPI_ANDES_H

#define XILINX_SPI_NAME "andes-spi"

struct at_ts_platdata {
	void __iomem *regs;
	unsigned int clk;
	unsigned int sclk;
	unsigned int x_max;
	unsigned int y_max;
};

/* ID and Revision Register */
#define ID_REV		0x0
#define ID_OFF		12
#define ID_MSK		(0xfffff<<12)

#define ID_ATCSPI200	0x02002

/* SPI Transfer Format Register */
#define FORMAT		0x10
#define DATA_LEN_OFF	8
#define DATA_LEN_MSK	(0x1f<<8)

/* SPI Transfer Control Register */
#define TCONTROL	0x20
#define TRANS_MODE_OFF	24
#define TRANS_MODE_MSK	(0xf<<TRANS_MODE_OFF)
#define WRITE_THEN_READ	(0x3<<TRANS_MODE_OFF)
#define WRTRANCNT_OFF	12
#define WRTRANCNT_MSK	(0x1ff<<WRTRANCNT_OFF)
#define RDTRANCNT_OFF	0
#define RDTRANCNT_MSK	(0x1ff<<RDTRANCNT_OFF)



/* SPI Command Register */
#define CMD		0x24

/* SPI Data Register */
#define DATA		0x2c

/* SPI Control Register */
#define CONTROL		0x30

/* SPI Status Register */
#define STATUS		0x34
#define TX_FULL		(1<<23)
#define TX_EMPTY	(1<<22)
#define RX_EMPTY	(1<<14)
#define ACTIVE		(1<<0)


/* SPI Interface Timing Register */
#define TIMING		0x40
#define SCLK_DIV_MSK	(0xff<<0)


/*
 * Register definitions as per "OPB Serial Peripheral Interface ( SPI) ( v1.00e)
 * Product Specification", DS464
 */
#define XSPI_CR_OFFSET		0x62	/* 16-bit Control Register */

#define XSPI_CR_ENABLE		0x02
#define XSPI_CR_MASTER_MODE	0x04
#define XSPI_CR_CPOL		0x08
#define XSPI_CR_CPHA		0x10
#define XSPI_CR_MODE_MASK	(XSPI_CR_CPHA | XSPI_CR_CPOL)
#define XSPI_CR_TXFIFO_RESET	0x20
#define XSPI_CR_RXFIFO_RESET	0x40
#define XSPI_CR_MANUAL_SSELECT	0x80
#define XSPI_CR_TRANS_INHIBIT	0x100

#define XSPI_SR_OFFSET		0x67	/* 8-bit Status Register */

#define XSPI_SR_RX_EMPTY_MASK	0x01	/* Receive FIFO is empty */
#define XSPI_SR_RX_FULL_MASK	0x02	/* Receive FIFO is full */
#define XSPI_SR_TX_EMPTY_MASK	0x04	/* Transmit FIFO is empty */
#define XSPI_SR_TX_FULL_MASK	0x08	/* Transmit FIFO is full */
#define XSPI_SR_MODE_FAULT_MASK	0x10	/* Mode fault error */

#define XSPI_TXD_OFFSET		0x6b	/* 8-bit Data Transmit Register */
#define XSPI_RXD_OFFSET		0x6f	/* 8-bit Data Receive Register */

#define XSPI_SSR_OFFSET		0x70	/* 32-bit Slave Select Register */

/* Register definitions as per "OPB IPIF ( v3.01c) Product Specification", DS414
 * IPIF registers are 32 bit
 */
#define XIPIF_V123B_DGIER_OFFSET	0x1c	/* IPIF global int enable reg */
#define XIPIF_V123B_GINTR_ENABLE	0x80000000

#define XIPIF_V123B_IISR_OFFSET		0x20	/* IPIF interrupt status reg */
#define XIPIF_V123B_IIER_OFFSET		0x28	/* IPIF interrupt enable reg */

#define XSPI_INTR_MODE_FAULT		0x01	/* Mode fault error */
#define XSPI_INTR_SLAVE_MODE_FAULT	0x02	/* Selected as slave while disabled */
#define XSPI_INTR_TX_EMPTY		0x04	/* TxFIFO is empty */
#define XSPI_INTR_TX_UNDERRUN		0x08	/* TxFIFO was underrun */
#define XSPI_INTR_RX_FULL		0x10	/* RxFIFO is full */
#define XSPI_INTR_RX_OVERRUN		0x20	/* RxFIFO was overrun */

#define XIPIF_V123B_RESETR_OFFSET	0x40	/* IPIF reset register */
#define XIPIF_V123B_RESET_MASK		0x0a	/* the value to write */

#endif /* SPI_ANDES_H */
