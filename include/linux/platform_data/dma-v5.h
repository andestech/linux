/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Header file for the Andestech DMA Controller driver
 *
 * Copyright (C) 2021 Andestech Corporation
 */
#ifndef V5_DMA_H
#define V5_DMA_H

#include <linux/dmaengine.h>

struct v5_dma_platform_data {
	unsigned int	nr_channels;
	dma_cap_mask_t  cap_mask;
};

struct v5_dma_slave {
	struct device		*dma_dev;
};

#endif /* V5_DMA_H */
