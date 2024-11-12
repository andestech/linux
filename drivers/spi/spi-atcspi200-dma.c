// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Andes Technology Corporation.
 */

#include <linux/dmaengine.h>
#include <linux/spi/spi-mem.h>
#include <linux/soc/andes/spi-atcspi200.h>
#define DMA_DEV_TIMEOUT_MS	1000

static int atcspi200_spi_dma_init(struct device *dev, struct atcspi200_spi *spi)
{
	int ret;
	struct dma_slave_caps tx, rx;

	if (IS_ENABLED(CONFIG_ATCSPI200_DMA_RX)) {
		spi->rxchan = dma_request_chan(dev, "spi_rx");
		if (IS_ERR(spi->rxchan)) {
			ret = PTR_ERR(spi->rxchan);
			spi->rxchan = NULL;
			return ret;
		}
		spi->controller->dma_rx = spi->rxchan;
		ret = dma_get_slave_caps(spi->rxchan, &rx);
		if (ret)
			return ret;
	}
	if (IS_ENABLED(CONFIG_ATCSPI200_DMA_TX)) {
		spi->txchan = dma_request_chan(dev, "spi_tx");
		if (IS_ERR(spi->txchan)) {
			ret = PTR_ERR(spi->txchan);
			spi->txchan = NULL;
			return ret;
		}
		spi->controller->dma_tx = spi->txchan;
		ret = dma_get_slave_caps(spi->txchan, &tx);
		if (ret)
			return ret;
	}

	init_completion(&spi->dma_completion);

	return 0;
}

static void atcspi200_spi_dma_exit(struct atcspi200_spi *spi)
{
	if (spi->txchan) {
		dmaengine_terminate_sync(spi->txchan);
		dma_release_channel(spi->txchan);
	}

	if (spi->rxchan) {
		dmaengine_terminate_sync(spi->rxchan);
		dma_release_channel(spi->rxchan);
	}
}

static inline int spi_dma_config_rx(struct atcspi200_spi *spi)
{
	struct dma_slave_config rxconf = { 0 };

	rxconf.direction = DMA_DEV_TO_MEM;
	rxconf.src_addr = spi->dma_addr;
	rxconf.dst_maxburst = 2;
	rxconf.src_maxburst = 2;
#ifdef	CONFIG_SPI_ATCSPI200_DATA_MERGE
	rxconf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	rxconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
#else
	rxconf.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	rxconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
#endif

	return dmaengine_slave_config(spi->rxchan, &rxconf);
}

static inline int spi_dma_config_tx(struct atcspi200_spi *spi)
{
	struct dma_slave_config txconf = { 0 };

	txconf.direction = DMA_MEM_TO_DEV;
	txconf.dst_addr = spi->dma_addr;
	txconf.dst_maxburst = 2;
	txconf.src_maxburst = 2;
#ifdef	CONFIG_SPI_ATCSPI200_DATA_MERGE
	txconf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	txconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
#else
	txconf.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	txconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
#endif

	return dmaengine_slave_config(spi->txchan, &txconf);
}

static void atcspi200_spi_dma_callback(void *arg)
{
	struct completion *dma_completion = arg;

	complete(dma_completion);
}

static int atcspi200_spi_dma_setup(struct atcspi200_spi *spi, const struct spi_mem_op *op)
{
	int ret;
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *dma_ch;
	enum dma_transfer_direction dma_dir;
	struct sg_table sgt;
	dma_cookie_t cookie;

	/* Setup DMA channels */
	if (op->data.dir == SPI_MEM_DATA_IN) {
		ret = spi_dma_config_rx(spi);
		if (ret)
			return ret;
		dma_dir = DMA_DEV_TO_MEM;
		dma_ch = spi->rxchan;
	} else {
		spi_dma_config_tx(spi);
		if (ret)
			return ret;
		dma_dir = DMA_MEM_TO_DEV;
		dma_ch = spi->txchan;
	}

	ret = spi_controller_dma_map_mem_op_data(spi->controller, op, &sgt);
	if (ret)
		return ret;

	desc = dmaengine_prep_slave_sg(dma_ch, sgt.sgl, sgt.nents,
				       dma_dir, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		ret = -ENOMEM;
		goto exit_unmap;
	}

	reinit_completion(&spi->dma_completion);
	desc->callback = atcspi200_spi_dma_callback;
	desc->callback_param = &spi->dma_completion;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret)
		goto exit_unmap;

	return 0;

exit_unmap:
	spi_controller_dma_unmap_mem_op_data(spi->controller, op, &sgt);
	return ret;
}

static int atcspi200_spi_dma_transfer(struct atcspi200_spi *spi, const struct spi_mem_op *op)
{
	struct dma_chan *dma_ch;
	int ret, timeout = DMA_DEV_TIMEOUT_MS;

	if (op->data.dir == SPI_MEM_DATA_IN)
		dma_ch = spi->rxchan;
	else
		dma_ch = spi->txchan;

	dma_async_issue_pending(dma_ch);
	timeout = msecs_to_jiffies(op->data.nbytes * DMA_DEV_TIMEOUT_MS);
	if (!wait_for_completion_timeout(&spi->dma_completion, timeout)) {
		ret = -ETIMEDOUT;
		return ret;
	}

	return 0;
}

static const struct atcspi200_dma_ops atcspi200_dma_ops = {
	.dma_init = atcspi200_spi_dma_init,
	.dma_exit = atcspi200_spi_dma_exit,
	.dma_setup = atcspi200_spi_dma_setup,
	.dma_transfer = atcspi200_spi_dma_transfer,
};

void atcspi200_spi_dma_ops_setup(struct atcspi200_spi *spi)
{
	if (IS_ENABLED(CONFIG_SPI_ATCSPI200_DMA) &&
	    (IS_ENABLED(CONFIG_ATCSPI200_DMA_RX) || IS_ENABLED(CONFIG_ATCSPI200_DMA_TX)))
		spi->dma_ops = &atcspi200_dma_ops;
}
