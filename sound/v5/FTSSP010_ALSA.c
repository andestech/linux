// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Andes Technology Corporation
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <soc/andes/dmad.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/asound.h>
#include <linux/i2c.h>
#include <sound/control.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <asm/irq.h>
#include "FTSSP010_UDA1345TS.h"
#include <linux/uaccess.h>

struct alc5630_data;

#if (!defined(CONFIG_PLATFORM_AHBDMA))
#warning needs ahb dma to wrok
#endif

/* ---------------------------------------------------------------------------
 * Define the debug level of FTSSP_DEBUG
 */
#define FTSSP_DEBUG         0
#define FTSSP_DEBUG_VERBOSE 0
#define FTSSP_PROC_FS       0

#undef VVDBG
#if (FTSSP_DEBUG_VERBOSE)
#define VVDBG(vvar...)
#else
#define VVDBG(vvar...)
#endif

#undef ERR
#define ERR(vvar...)	pr_err(vvar)

#undef INFO
#define INFO(vvar...)	pr_err(vvar)

#if (FTSSP_DEBUG)
#undef DBG
#define DBG(vvar...)	pr_err(vvar)
#else
#define DBG(vvar...)
#endif

#if (FTSSP_DEBUG_VERBOSE)
#undef VDBG
#define VDBG(vvar...)	pr_err(vvar)
#else
#define VDBG(vvar...)
#endif

/* ---------------------------------------------------------------------------
 * Preserved size of memory space for audio DMA ring
 */
#define FTSSP_HW_DMA_SIZE		(512 * 1024)

/* Buffer sizes reported to ALSA layer - AC97 mode */

/* ring size, exported to application */
#define AC97_HW_BUFFER_BYTES_MAX	(42 * 1024)
/* should not exceed AC97_HW_PERIOD_BYTES_MAX */
#define AC97_HW_PERIOD_BYTES_MIN	(2 * 1024)
/* AC97_HW_PERIOD_BYTES_MAX * AC97_HW_PERIODS_MAX <= AC97_HW_BUFFER_SIZE */
#define AC97_HW_PERIOD_BYTES_MAX	(8 * 1024)
/* 3 <= AC97_HW_PERIODS_MIN <= AC97_HW_PERIODS_MAX */
#define AC97_HW_PERIODS_MIN		3
/* AC97_HW_PERIOD_BYTES_MAX * AC97_HW_PERIODS_MAX <= AC97_HW_BUFFER_SIZE */
#define AC97_HW_PERIODS_MAX		5

/* Driver internal dma buffer size, x2 for S16_LE(16-bits) to AC97 (20-bits),
 * x6 for sampling rate converion from minimum 8k to AC97 48k.
 *
 * Note that AC97 mode cannot do playback and recording simultaneouly. So we
 * use up all FTSSP_HW_DMA_SIZE of memory.
 */
#define AC97_HW_DMA_SIZE		(AC97_HW_BUFFER_BYTES_MAX * 2 * 6)

/* Buffer sizes reported to ALSA layer - I2S mode */

/* ring size, exported to application */
#define I2S_HW_BUFFER_BYTES_MAX		(256 * 1024)
/* should not exceed I2S_HW_PERIOD_BYTES_MAX */
#define I2S_HW_PERIOD_BYTES_MIN		(2 * 1024)
/* I2S_HW_PERIOD_BYTES_MAX * I2S_HW_PERIODS_MAX <= I2S_HW_BUFFER_SIZE */
#define I2S_HW_PERIOD_BYTES_MAX		(32 * 1024)
/* 3 <= I2S_HW_PERIODS_MIN <= I2S_HW_PERIODS_MAX */
#define I2S_HW_PERIODS_MIN		3
/* I2S_HW_PERIOD_BYTES_MAX * I2S_HW_PERIODS_MAX <= I2S_HW_BUFFER_SIZE */
#define I2S_HW_PERIODS_MAX		8

/* Page-in size for playback and capture each.  Note that I2S mode can do
 * playback and recording simultaneouly, so this size should be less than or
 * equal to FTSSP_HW_DMA_SIZE/2
 */
#define I2S_HW_DMA_SIZE			(I2S_HW_BUFFER_BYTES_MAX)

/* ---------------------------------------------------------------------------
 * Audio formats
 */
#define AC97_CODEC_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE)
#define AC97_CODEC_SAMPLE_RATES		(SNDRV_PCM_RATE_48000 | \
					 SNDRV_PCM_RATE_44100 | \
					 SNDRV_PCM_RATE_32000 | \
					 SNDRV_PCM_RATE_16000 | \
					 SNDRV_PCM_RATE_8000)

#define AC97_CODEC_SAMPLE_RATE_MIN	(8000)
#define AC97_CODEC_SAMPLE_RATE_MAX	(48000)

#define I2S_CODEC_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE)
#define I2S_CODEC_SAMPLE_RATES		(SNDRV_PCM_RATE_48000 | \
					 SNDRV_PCM_RATE_44100 | \
					 SNDRV_PCM_RATE_32000 | \
					 SNDRV_PCM_RATE_22050 | \
					 SNDRV_PCM_RATE_16000 | \
					 SNDRV_PCM_RATE_11025 | \
					 SNDRV_PCM_RATE_8000)
#define I2S_CODEC_SAMPLE_RATE_MIN	(8000)
#define I2S_CODEC_SAMPLE_RATE_MAX	(48000)


/* ---------------------------------------------------------------------------
 * Configuration
 */
#if (CONFIG_PROC_FS == 0)
#undef FTSSP_PROC_FS
#define FTSSP_PROC_FS 0
#else
#if (FTSSP_PROC_FS)
#include <sound/info.h>
#endif  /* FTSSP_PROC_FS */
#endif  /* CONFIG_PROC_FS */

#define FTSSP_CARD_ID		"ftssp010"
#define FTSSP_DRIVER_NAME	"ftssp"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Faraday Technology Corp.");
MODULE_DESCRIPTION("FTSSP010 Linux 2.6 Driver");

static int cardno;
/* Driver mode */
#ifdef CONFIG_SND_FTSSP010_AC97
static int ac97 = 1;
#else
static int ac97;
#endif

// ----------------------------------------------
module_param(cardno, int, 0);
MODULE_PARM_DESC(cardno, "FTSSP No.");

module_param(ac97, int, 0);
MODULE_PARM_DESC(ac97, "AC97 mode");
// ----------------------------------------------

/* ---------------------------------------------------------------------------
 * Structures
 */

/* private data for card */
typedef struct {
	struct platform_device	*pdev;
	struct snd_card *card;
	struct snd_pcm  *pcm;
	struct snd_pcm_substream *substream_tx;
	struct snd_pcm_substream *substream_rx;
#if (FTSSP_PROC_FS)
	struct snd_info_entry *info_buf_max;
	struct snd_info_entry *info_period_min;
	struct snd_info_entry *info_period_max;
	struct snd_info_entry *info_periods_min;
	struct snd_info_entry *info_periods_max;
#endif
} ftssp_chip;

/* dma request descriptors */
dmad_chreq dma_chreq_tx = {
	.channel = -1,
	.drq     = NULL,
};

dmad_chreq dma_chreq_rx = {
	.channel = -1,
	.drq     = NULL,
};

/* Holds ALSA card instance pointers */
struct snd_card *ftssp_cards[SSP_FTSSP010_COUNT];

/* snd_pcm_hardware */
static struct snd_pcm_hardware snd_ftssp_pcm_hw = {
	.info               = SNDRV_PCM_INFO_INTERLEAVED,
	.formats            = I2S_CODEC_FORMATS,
	.rates              = I2S_CODEC_SAMPLE_RATES,
	.rate_min           = I2S_CODEC_SAMPLE_RATE_MIN,
	.rate_max           = I2S_CODEC_SAMPLE_RATE_MAX,
	.channels_min       = 1,
	.channels_max       = 2,
	.buffer_bytes_max   = I2S_HW_BUFFER_BYTES_MAX,
	.period_bytes_min   = I2S_HW_PERIOD_BYTES_MIN,
	.period_bytes_max   = I2S_HW_PERIOD_BYTES_MAX,
	.periods_min        = I2S_HW_PERIODS_MIN,
	.periods_max        = I2S_HW_PERIODS_MAX,
};

/* private data for a substream (playback or capture) */
/* function pointer for set up AHBDMA for this substream */
typedef void (*start_t)(int cardno, unsigned int use_dma);
typedef void (*pmu_set_clocking_t)(unsigned int);
typedef void (*ftssp010_config_t)(int cardno, unsigned int is_stereo,
				  unsigned int speed, int use8bit);

typedef struct {
	u32                busy;
	spinlock_t         dma_lock;
	unsigned long	dma_area_va;

	int                dma_width;
	unsigned int       tx_period;
	unsigned int       rx_period;

	start_t            start;
	pmu_set_clocking_t pmu_set_clocking;
	ftssp010_config_t  hw_config;
} ftssp_substream;

static ftssp_substream ftssp010_substreams[2] = {
	/* Playback substream */
	{
		.busy = 0,
		.start = ftssp010_start_tx,
		.pmu_set_clocking = pmu_set_i2s_clocking,
		.hw_config = ftssp010_config_tx,
	},
	/* Capture substream */
	{
		.busy = 0,
		.start = ftssp010_start_rx,
		.pmu_set_clocking = pmu_set_i2s_clocking,
		.hw_config = ftssp010_config_rx,
	}
};

/* (AC97 only) Convert 16 bits PCM data in user buffer to/from 20 bits PCM data
 * (32 bits actaully in dma buffer) for AC97 codec.
 */
static int snd_ftssp_playback_copy(struct snd_pcm_substream *substream,
	int channel, snd_pcm_uframes_t pos, void *usr_buf,
	snd_pcm_uframes_t count)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	ftssp_substream *ftssp010_substream =
		(ftssp_substream *) runtime->private_data;

	u32 *dma_va = NULL;
	u16 *usr_va = usr_buf;
	int copy_words;
	int pcm_data;
	dmad_chreq *dma_chreq;
	u32 sw_ptr;

	/* convert to frames */
	pos = bytes_to_frames(substream->runtime, pos);
	count = bytes_to_frames(substream->runtime, count);

	/* frames_to_bytes(runtime, pos + count) * 2(bytes/per pcm) /
	 * 4(bytes per dma unit)
	 */
	sw_ptr = (u32)frames_to_bytes(runtime, pos + count) >> 1;

	switch (runtime->rate) {
	case 8000:
		sw_ptr *= 6;
		dma_va = (unsigned int *)(ftssp010_substream->dma_area_va +
				frames_to_bytes(runtime, pos * 6) * 2);

		VVDBG("%s: pos(0x%08x) count(0x%08x) next_pos(0x%08x)\n",
			__func__, (u32)pos, (u32)count, (u32)(pos + count));
		VVDBG("%s: va base(0x%08x) range (0x%08x ~ 0x%08x)\n",
			__func__, (u32)ftssp010_substream->dma_area_va,
			(u32)dma_va,
			(u32)dma_va +
			(u32)2 * frames_to_bytes(runtime, count * 6));

		if (runtime->channels == 1) {
			while (count--) {
				pcm_data = 0;
				get_user(pcm_data, usr_va++);
				dma_va[0] = (pcm_data & 0xffff) << 4;
				dma_va[1] = dma_va[2] = dma_va[3] =
				dma_va[4] = dma_va[5] = dma_va[0];
				//memcpy(&dma_va[1], &dma_va[0], 5 * 4 * 1);
				dma_va += 6;
			}
		} else {  // assume 2 channels
			while (count--) {
				pcm_data = 0;
				get_user(pcm_data, usr_va++);
				dma_va[0] = (pcm_data & 0xffff) << 4;

				pcm_data = 0;
				get_user(pcm_data, usr_va++);
				dma_va[1] = (pcm_data & 0xffff) << 4;

				dma_va[2] = dma_va[4] = dma_va[6] =
				dma_va[8] = dma_va[10] = dma_va[0];
				dma_va[3] = dma_va[5] = dma_va[7] =
				dma_va[9] = dma_va[11] = dma_va[0];
				//memcpy(&dma_va[2], &dma_va[0], 5 * 4 * 2);
				dma_va += 12;
			}
		}
		break;

	case 16000:
		sw_ptr *= 3;

		dma_va = (unsigned int *)(ftssp010_substream->dma_area_va +
				frames_to_bytes(runtime, pos * 3) * 2);

		VVDBG("%s: pos(0x%08x) count(0x%08x) next_pos(0x%08x)\n",
			__func__, (u32)pos, (u32)count, (u32)(pos + count));
		VVDBG("%s: va base(0x%08x) range (0x%08x ~ 0x%08x)\n",
			__func__, (u32)ftssp010_substream->dma_area_va,
			(u32)dma_va,
			(u32)dma_va +
			(u32)2 * frames_to_bytes(runtime, count * 3));

		if (runtime->channels == 1) {
			while (count--) {
				pcm_data = 0;
				get_user(pcm_data, usr_va++);
				dma_va[0] = (pcm_data & 0xffff) << 4;
				dma_va[1] = dma_va[2] = dma_va[0];
				//memcpy(&dma_va[1], &dma_va[0], 2 * 4 * 1);
				dma_va += 3;
			}
		} else {  // assume 2 channels
			while (count--) {
				pcm_data = 0;
				get_user(pcm_data, usr_va++);
				dma_va[0] = (pcm_data & 0xffff) << 4;

				pcm_data = 0;
				get_user(pcm_data, usr_va++);
				dma_va[1] = (pcm_data & 0xffff) << 4;

				dma_va[2] = dma_va[4] = dma_va[0];
				dma_va[3] = dma_va[5] = dma_va[1];
				//memcpy(&dma_va[2], &dma_va[0], 2 * 4 * 2);
				dma_va += 6;
			}
		}
		break;

	case 48000:
	default:
		dma_va = (unsigned int *)(ftssp010_substream->dma_area_va +
				frames_to_bytes(runtime, pos) * 2);
		copy_words = 2 * frames_to_bytes(runtime, count) / sizeof(u32);

		VVDBG("%s: pos(0x%08x) count(0x%08x) next_pos(0x%08x)\n",
			__func__, (u32)pos, (u32)count, (u32)(pos + count));
		VVDBG("%s: va base(0x%08x) range (0x%08x ~ 0x%08x)\n",
			__func__, (u32)ftssp010_substream->dma_area_va,
			(u32)dma_va,
			(u32)dma_va + (u32)copy_words*4);

		while (copy_words--) {
			get_user(pcm_data, usr_va++);
			*dma_va++ = (pcm_data & 0xffff) << 4;
		}
		break;
	}

	dma_chreq = &dma_chreq_tx;

	if (dmad_update_ring_sw_ptr(dma_chreq, sw_ptr,
		(runtime->status->state == SNDRV_PCM_STATE_RUNNING) ? 1:0) != 0) {
		ERR("%s: failed to update sw-pointer!\n", __func__);
		return -ENODEV;
	}

	return 0;
}

static int snd_ftssp_capture_copy(struct snd_pcm_substream *substream,
	int channel, snd_pcm_uframes_t pos, void *usr_buf,
	snd_pcm_uframes_t count)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	ftssp_substream *ftssp010_substream =
		(ftssp_substream *) runtime->private_data;

	u32 *dma_va = NULL;
	u16 *usr_va = usr_buf;
	/* convert to frames */
	pos = bytes_to_frames(substream->runtime, pos);
	count = bytes_to_frames(substream->runtime, count);

	switch (runtime->rate) {
	case 8000:
		dma_va = (unsigned int *)(ftssp010_substream->dma_area_va +
				 frames_to_bytes(runtime, pos * 6) * 2);

		VVDBG("%s: pos(0x%08x) count(0x%08x) next_pos(0x%08x)\n",
			__func__, (u32)pos, (u32)count, (u32)(pos + count));
		VVDBG("%s: va base(0x%08x) range (0x%08x ~ 0x%08x)\n",
			__func__, (u32)ftssp010_substream->dma_area_va,
			(u32)dma_va,
			(u32)dma_va +
			(u32)2 * frames_to_bytes(runtime, count * 6));

		if (runtime->channels == 1) {
			while (count--) {
				put_user((u16)(dma_va[0] >> 4), usr_va++);
				dma_va += 6;
			}
		} else {
			while (count--) {
				put_user((u16)(dma_va[0] >> 4), usr_va);
				usr_va++;
				put_user((u16)(dma_va[0] >> 4), usr_va);

				/* [hw-limit] only slot-3 has valid data in
				 *   recording mode -- check TAG_DATA_MONO
				 *   defined in "FTSSP010_lib.c".  Mask out
				 *   one channel to avoid hi-freq noise.
				 */
				usr_va += 1;
				dma_va += 12;
			}
		}
		break;

	case 16000:
		dma_va = (unsigned int *)(ftssp010_substream->dma_area_va +
				 frames_to_bytes(runtime, pos * 3) * 2);

		VVDBG("%s: pos(0x%08x) count(0x%08x) next_pos(0x%08x)\n",
			__func__, (u32)pos, (u32)count, (u32)(pos + count));
		VVDBG("%s: va base(0x%08x) range (0x%08x ~ 0x%08x)\n",
			__func__, (u32)ftssp010_substream->dma_area_va,
			(u32)dma_va,
			(u32)dma_va +
			(u32)2 * frames_to_bytes(runtime, count * 3));

		if (runtime->channels == 1) {
			while (count--) {
				put_user((u16)(dma_va[0] >> 4), usr_va++);
				dma_va += 3;
			}
		} else {
			while (count--) {
				put_user((u16)(dma_va[0] >> 4), usr_va);
				usr_va++;
				put_user((u16)(dma_va[0] >> 4), usr_va);

				/* [hw-limit] only slot-3 has valid data in
				 *   recording mode -- check TAG_DATA_MONO
				 *   defined in "FTSSP010_lib.c".  Mask out
				 *   one channel to avoid hi-freq noise.
				 */
				usr_va++;
				dma_va += 6;
			}
		}
		break;

	case 48000:
	default:
		dma_va = (unsigned int *)(ftssp010_substream->dma_area_va +
				frames_to_bytes(runtime, pos) * 2);

		VVDBG("%s: pos(0x%08x) count(0x%08x) next_pos(0x%08x)\n",
			__func__, (u32)pos, (u32)count, (u32)(pos + count));
		VVDBG("%s: va base(0x%08x) range (0x%08x ~ 0x%08x)\n",
			__func__, (u32)ftssp010_substream->dma_area_va,
			(u32)dma_va,
			(u32)dma_va +
			(u32)2 * frames_to_bytes(runtime, count));

		if (runtime->channels == 1) {
			while (count--) {
				put_user((u16)(dma_va[0] >> 4), usr_va++);
				dma_va += 1;
			}
		} else {
			while (count--) {
				put_user((u16)(dma_va[0] >> 4), usr_va);
				usr_va++;
				put_user((u16)(dma_va[0] >> 4), usr_va);

				/* [hw-limit] only slot-3 has valid data in
				 *   recording mode -- check TAG_DATA_MONO
				 *   defined in "FTSSP010_lib.c".  Mask out
				 *   one channel to avoid hi-freq noise.
				 */
				usr_va += 1;
				dma_va += 2;
			}
		}
		break;
	}

	return 0;
}

/**
 * These dma callbacks are called in interrupt context.
 * @data: pointer to the chip-wide structure.
 *        TODO: use stream-specifc data
 */
__maybe_unused static void ftssp_dma_callback_tx(int ch, u16 int_status, void *data)
{
	ftssp_chip *chip = (ftssp_chip *)data;

	if (!ac97) {
		/* in i2s mode, no indication to driver for user data length.
		 * For simplicity, just go ahead by one period
		 */

		struct snd_pcm_runtime *runtime = chip->substream_tx->runtime;
		ftssp_substream *ftssp010_substream =
			(ftssp_substream *)runtime->private_data;
		u32 sw_ptr;
		u32 tx_period = ftssp010_substream->tx_period + 1;

		if (tx_period == runtime->periods)
			sw_ptr = runtime->buffer_size;
		else
			sw_ptr = tx_period * runtime->period_size;

		sw_ptr = (u32)frames_to_bytes(runtime, sw_ptr) >> 1;

		if (dmad_update_ring_sw_ptr(&dma_chreq_tx, (u32)sw_ptr, 0))
			ERR("%s: failed to update sw-pointer!\n", __func__);

		ftssp010_substream->tx_period = tx_period % runtime->periods;
	}

	snd_pcm_period_elapsed(chip->substream_tx);
}

__maybe_unused static void ftssp_dma_callback_rx(int ch, u16 int_status, void *data)
{
	ftssp_chip *chip = (ftssp_chip *)data;
	struct snd_pcm_runtime *runtime = chip->substream_rx->runtime;
	ftssp_substream *ftssp010_substream =
		(ftssp_substream *)runtime->private_data;

	u32 sw_ptr;
	u32 rx_period = ftssp010_substream->rx_period + 1;

	if (rx_period == runtime->periods)
		sw_ptr = runtime->buffer_size;
	else
		sw_ptr = rx_period * runtime->period_size;

	if (ac97) {
		switch (runtime->rate) {
		case 8000:
			sw_ptr = sw_ptr * 6;
			break;
		case 16000:
			sw_ptr = sw_ptr * 3;
			break;
		case 48000:
		default:
			break;
		}
	}
	sw_ptr = (u32)frames_to_bytes(runtime, sw_ptr) >> 1;

	if (dmad_update_ring_sw_ptr(&dma_chreq_rx, (u32)sw_ptr, 0) != 0)
		ERR("%s: failed to update sw-pointer!\n", __func__);

	ftssp010_substream->rx_period = rx_period % runtime->periods;

	snd_pcm_period_elapsed(chip->substream_rx);
}

static inline int snd_ftssp_dma_ch_alloc(struct snd_pcm_substream *substream)
{
	dmad_chreq *ch_req __maybe_unused = 0;

#ifdef CONFIG_PLATFORM_AHBDMA
	if (substream->pstr->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ch_req = &dma_chreq_tx;
		ch_req->completion_cb    = ftssp_dma_callback_tx;
		ch_req->ahb_req.tx_dir   = DMAD_DIR_A0_TO_A1;
#if defined(CONFIG_PLAT_AG101P)
		if ((inl(PMU_BASE) & AMERALD_MASK) == AMERALD_PRODUCT_ID)
			ch_req->ahb_req.dev_reqn = DMAC_REQN_I2SAC97TX_AMERALD;
		else
#endif
			/*TX DST handshake mode addr0 --> addr1
			 * tx_dir == 0, addr1 set handshake ID
			 */
			ch_req->ahb_req.dev_reqn = DMAC_REQN_I2SAC97TX;
	} else {
		ch_req = &dma_chreq_rx;
		ch_req->completion_cb    = ftssp_dma_callback_rx;
		ch_req->ahb_req.tx_dir   = DMAD_DIR_A1_TO_A0;
#if defined(CONFIG_PLAT_AG101P)
		if ((inl(PMU_BASE) & AMERALD_MASK) == AMERALD_PRODUCT_ID)
			ch_req->ahb_req.dev_reqn = DMAC_REQN_I2SAC97RX_AMERALD;
		else
#endif
			/*RX SRC handshake mode, addr1 --> addr0
			 *tx_dir == 1, addr1 set handshake ID
			 */
			ch_req->ahb_req.dev_reqn  = DMAC_REQN_I2SAC97RX;
	}

	ch_req->controller           = DMAD_DMAC_AHB_CORE;
	ch_req->flags                = DMAD_FLAGS_RING_MODE;
	ch_req->ring_base            = 0;
	ch_req->dev_addr             = (dma_addr_t)FTSSP010_DATA_PA(cardno);
	ch_req->periods              = 0;
	ch_req->period_size          = 0;

	ch_req->ahb_req.sync         = 1;
	ch_req->ahb_req.priority     = DMAC_CSR_CHPRI_2;
	ch_req->ahb_req.hw_handshake = 1;
	ch_req->ahb_req.burst_size   = DMAC_CSR_SIZE_1;

	if (ac97) {
		ch_req->ahb_req.ring_width   = DMAC_CSR_WIDTH_32;
		ch_req->ahb_req.ring_ctrl    = DMAC_CSR_AD_INC;
		ch_req->ahb_req.ring_reqn    = DMAC_REQN_NONE;
		ch_req->ahb_req.dev_width    = DMAC_CSR_WIDTH_32;
		ch_req->ahb_req.dev_ctrl     = DMAC_CSR_AD_FIX;
	} else {
		ch_req->ahb_req.ring_width   = DMAC_CSR_WIDTH_16;
		ch_req->ahb_req.ring_ctrl    = DMAC_CSR_AD_INC;
		ch_req->ahb_req.ring_reqn    = DMAC_REQN_NONE;
		ch_req->ahb_req.dev_width    = DMAC_CSR_WIDTH_16;
		ch_req->ahb_req.dev_ctrl     = DMAC_CSR_AD_FIX;
	}

	ch_req->completion_data = (void *)snd_pcm_substream_chip(substream);

	if (dmad_channel_alloc(ch_req) != 0) {
		ERR("%s: AHBDMA channel allocation failed\n", __func__);
		goto _err_exit;
	}

	DBG("%s: AHBDMA channel allocated (ch: %d) ring_mode\n",
		__func__, ch_req->channel);

	return 0;

_err_exit:

#endif /* CONFIG_PLATFORM_AHBDMA */

	return -ENODEV;
}

static inline ftssp_substream *ftssp010_substream_new(int stream_id)
{
	ftssp_substream *s = NULL;

	switch (stream_id) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		s = &ftssp010_substreams[0];
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		s = &ftssp010_substreams[1];
		break;
	default:
		ERR("%s: wrong stream type (%d)\n", __func__, stream_id);
		return NULL;
	}

	if (s->busy) {
		ERR("%s: device busy!\n", __func__);
		return NULL;
	}
	s->busy = 1;

	spin_lock_init(&s->dma_lock);

	return s;
}

static int snd_ftssp_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int stream_id = substream->pstr->stream;

	VDBG("%s, %s\n", __func__,
		(substream->pstr->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		"playback" : "capture");

	/* Both playback and capture share a hardware description */
	runtime->hw = snd_ftssp_pcm_hw;

	/* Allocate & Initialize stream-specific data */
	runtime->private_data = ftssp010_substream_new(stream_id);

	if (runtime->private_data)
		return snd_ftssp_dma_ch_alloc(substream);
	else
		return -EBUSY;
}

static int snd_ftssp_pcm_close(struct snd_pcm_substream *substream)
{
	int stream_id = substream->pstr->stream;
	ftssp_substream *ftssp010_substream =
		(ftssp_substream *)substream->runtime->private_data;

	VDBG("%s, %s\n", __func__,
		(substream->pstr->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		"playback" : "capture");

	if (stream_id == SNDRV_PCM_STREAM_PLAYBACK)
		dmad_channel_free(&dma_chreq_tx);
	else
		dmad_channel_free(&dma_chreq_rx);

	ftssp010_substream->busy = 0;
	return 0;
}

static int snd_ftssp_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *hw_params)
{
	VDBG("%s, %s\n", __func__,
		(substream->pstr->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		"playback" : "capture");

	if (ac97)
		return snd_pcm_lib_malloc_pages(substream, AC97_HW_DMA_SIZE);
	else
		return snd_pcm_lib_malloc_pages(substream, I2S_HW_DMA_SIZE);
}

static int snd_ftssp_pcm_hw_free(struct snd_pcm_substream *substream)
{
	VDBG("%s, %s\n", __func__,
		(substream->pstr->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		"playback" : "capture");

	if (substream->pstr->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dmad_drain_requests(&dma_chreq_tx, 1);
	else
		dmad_drain_requests(&dma_chreq_rx, 1);

	return snd_pcm_lib_free_pages(substream);
}

/* Prepare FTSSP010 AHBDMA for playback & capture */
static int snd_ftssp_pcm_prepare(struct snd_pcm_substream *substream)
{
	ftssp_chip *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	ftssp_substream *ftssp010_substream =
		(ftssp_substream *)runtime->private_data;

	int stream_id = substream->pstr->stream;
	dmad_chreq *dma_chreq;
	unsigned int period_size, buffer_size;

	if (stream_id == SNDRV_PCM_STREAM_PLAYBACK)
		dma_chreq = &dma_chreq_tx;
	else
		dma_chreq = &dma_chreq_rx;

	period_size = frames_to_bytes(runtime, runtime->period_size);
	buffer_size = frames_to_bytes(runtime, runtime->buffer_size);

	if (runtime->format != SNDRV_PCM_FORMAT_S16_LE)
		return -ENODEV;

	if (ac97) {
		switch (runtime->rate) {
		case 8000:
			period_size *= 12;
			buffer_size *= 12;
			break;
		case 16000:
			period_size *= 6;
			buffer_size *= 6;
			break;
		case 48000:
		default:
			period_size *= 2;
			buffer_size *= 2;
			break;
		}

		ftssp010_substream->dma_width = 4;
	} else {
		ftssp010_substream->dma_width = 2;
	}

	dmad_drain_requests(dma_chreq, 1);

	dma_chreq->ring_base   = (dma_addr_t)runtime->dma_addr;
	dma_chreq->periods     = (dma_addr_t)runtime->periods;
	if (ac97) {
		dma_chreq->period_size = (dma_addr_t)(period_size >> 2);
		dma_chreq->ring_size   = (dma_addr_t)(buffer_size >> 2);
	} else {
		dma_chreq->period_size = (dma_addr_t)(period_size >> 1);
		dma_chreq->ring_size   = (dma_addr_t)(buffer_size >> 1);
	}
	dmad_update_ring(dma_chreq);

	/* Set PMU, FTSSP010, and DMA */
	spin_lock(&ftssp010_substream->dma_lock);

	/* keep DMA buffer VA for copy() callback */
	// todo: support playback/capture simultaneously
	ftssp010_substream->dma_area_va = (unsigned long)runtime->dma_area;

	if (ac97) {
		ftssp010_substream->pmu_set_clocking(48000);
		ftssp010_substream->hw_config(cardno,
			runtime->channels > 1 ? 1 : 0, /* 1: stereo, 0: mono */
			runtime->rate, ftssp010_substream->dma_width);
	} else {
		ftssp010_substream->pmu_set_clocking(runtime->rate);
		ftssp010_substream->hw_config(cardno,
			runtime->channels > 1 ? 1 : 0, /* 1: stereo, 0: mono */
			runtime->rate, ftssp010_substream->dma_width);
	}

	if (stream_id == SNDRV_PCM_STREAM_PLAYBACK) {
		ftssp010_substream->tx_period = 0;
		chip->substream_tx = substream;
	} else {
		ftssp010_substream->rx_period = 0;
		chip->substream_rx = substream;
	}

	spin_unlock(&ftssp010_substream->dma_lock);

	return 0;
}

static inline int snd_ftssp_start_play(ftssp_substream *ftssp010_substream,
	struct snd_pcm_runtime *runtime)
{
	int err = 0;

	if (ac97) {
		/* in ac97 mode, user data was fed to dma buffer through
		 * driver-provided copy callback
		 */
		err = dmad_kickoff_requests(&dma_chreq_tx);
		if (err != 0) {
			ERR("%s: failed to kickoff dma!\n", __func__);
			return err;
		}
	} else {
		/* in i2s mode, no indication to driver for user data length
		 * (except start threshold). For simplicity at start, just go
		 * ahead by one cycle
		 */

		u32 sw_ptr =
			(u32)frames_to_bytes(runtime, runtime->buffer_size) >> 1;

		err = dmad_update_ring_sw_ptr(&dma_chreq_tx, sw_ptr, 0);
		if (err != 0) {
			ERR("%s: failed to update sw-pointer!\n", __func__);
			return err;
		}

		err = dmad_kickoff_requests(&dma_chreq_tx);
		if (err != 0) {
			ERR("%s: failed to kickoff dma!\n", __func__);
			return err;
		}
	}
	ftssp010_substream->start(cardno, 1);

	return 0;
}

static inline int snd_ftssp_start_record(ftssp_substream *ftssp010_substream,
	struct snd_pcm_runtime *runtime)
{
	int err = 0;
	u32 sw_ptr = (u32)frames_to_bytes(runtime, runtime->buffer_size);

	if (ac97) {
		switch (runtime->rate) {
		case 8000:
			sw_ptr = (sw_ptr * 3);
			break;
		case 16000:
			sw_ptr = (sw_ptr * 3) >> 1;
			break;
		case 48000:
		default:
			sw_ptr = sw_ptr >> 1;
			break;
		}
	} else {
		sw_ptr = sw_ptr >> 1;
	}

	err = dmad_update_ring_sw_ptr(&dma_chreq_rx, sw_ptr, 0);
	if (err != 0) {
		ERR("%s: failed to update sw-pointer!\n", __func__);
		return err;
	}

	err = dmad_kickoff_requests(&dma_chreq_rx);
	if (err != 0) {
		ERR("%s: failed to kickoff dma!\n", __func__);
		return err;
	}

	ftssp010_substream->start(cardno, 1);

	return 0;
}

/* Triggers AHBDMA for playback & capture */
static int snd_ftssp_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	ftssp_substream *ftssp010_substream =
		(ftssp_substream *)substream->runtime->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err = 0;
	int stream_id = substream->pstr->stream;

	/* note local interrupts are already disabled in the midlevel code */
	spin_lock(&ftssp010_substream->dma_lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:

		VDBG("%s: SNDRV_PCM_TRIGGER_START state(0x%08x)\n",
			__func__, (u32)runtime->status->state);

		if (stream_id == SNDRV_PCM_STREAM_PLAYBACK) {
			err = snd_ftssp_start_play(ftssp010_substream, runtime);
		} else {
			err = snd_ftssp_start_record(ftssp010_substream,
				runtime);
		}
		break;

	case SNDRV_PCM_TRIGGER_STOP:

		VDBG("%s: SNDRV_PCM_TRIGGER_STOP state(0x%08x)\n",
			__func__, (u32)substream->runtime->status->state);

		if (stream_id == SNDRV_PCM_STREAM_PLAYBACK) {
			ftssp010_stop_tx(cardno);
			dmad_drain_requests(&dma_chreq_tx, 1);
		} else {
			ftssp010_stop_rx(cardno);
			dmad_drain_requests(&dma_chreq_rx, 1);
		}
		break;
	default:
		err = -EINVAL;
		break;
	}

	spin_unlock(&ftssp010_substream->dma_lock);
	return err;
}

// pcm middle-layer call this function within irq (snd_pcm_period_elapsed) or
// with local irq disabled (snd_pcm_lib_write1)
static snd_pcm_uframes_t snd_ftssp_pcm_pointer(
	struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	u32 hw_ptr;
	snd_pcm_uframes_t ret;
	int stream_id = substream->pstr->stream;


	/* Fetch DMA pointer, with spin lock */
	//spin_lock_irqsave(&ftssp010_substream->dma_lock, flags);

	if (stream_id == SNDRV_PCM_STREAM_PLAYBACK)
		hw_ptr = dmad_probe_ring_hw_ptr(&dma_chreq_tx);
	else
		hw_ptr = dmad_probe_ring_hw_ptr(&dma_chreq_rx);

	//spin_unlock_irqrestore(&ftssp010_substream->dma_lock, flags);

	if (ac97) {
		ret = bytes_to_frames(runtime, hw_ptr << 1);

		switch (runtime->rate) {
		case 8000:
			ret = ret / 6;
			break;
		case 16000:
			ret = ret / 3;
			break;
		case 48000:
		default:
			break;
		}
	} else {
		ret = bytes_to_frames(runtime, hw_ptr << 1);
	}


	VVDBG("%s: hw_ptr(0x%08x) ret(0x%08x)\n",
		(stream_id == SNDRV_PCM_STREAM_PLAYBACK) ? "p" : "c",
		(u32)hw_ptr, (u32)ret);

	/* ALSA requires return value 0 <= ret < buffer_size */
	if (ret >= runtime->buffer_size)
		return 0;
	return ret;
}

/* For FTSSP010 driver, operations are shared among playback & capture */
static struct snd_pcm_ops snd_ftssp_playback_ops = {
	.open      = snd_ftssp_pcm_open,
	.close     = snd_ftssp_pcm_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = snd_ftssp_pcm_hw_params,
	.hw_free   = snd_ftssp_pcm_hw_free,
	.prepare   = snd_ftssp_pcm_prepare,
	.trigger   = snd_ftssp_pcm_trigger,
	.pointer   = snd_ftssp_pcm_pointer,
};

static struct snd_pcm_ops snd_ftssp_capture_ops = {
	.open      = snd_ftssp_pcm_open,
	.close     = snd_ftssp_pcm_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = snd_ftssp_pcm_hw_params,
	.hw_free   = snd_ftssp_pcm_hw_free,
	.prepare   = snd_ftssp_pcm_prepare,
	.trigger   = snd_ftssp_pcm_trigger,
	.pointer   = snd_ftssp_pcm_pointer,
};

/* ALSA PCM constructor */
static int snd_ftssp_new_pcm(ftssp_chip *chip)
{
	struct snd_pcm *pcm;
	int err;

	/* PCM device #0 with 1 playback and 1 capture */
	err = snd_pcm_new(chip->card, "ftssp_pcm", 0, 1, 1, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = chip;
	strcpy(pcm->name, "ftssp_pcm device");
	chip->pcm = pcm;

	/* set operators for playback and capture*/
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
		&snd_ftssp_playback_ops);

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
		&snd_ftssp_capture_ops);

	/* Pre-allocate buffer, as suggested by ALSA driver document */
	// todo: support playback/capture simultaneously
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
	&chip->pdev->dev, FTSSP_HW_DMA_SIZE, FTSSP_HW_DMA_SIZE);

	/* Force half-duplex (on A320D, or AC97 mode) */
	if (ac97)
		pcm->info_flags |= SNDRV_PCM_INFO_HALF_DUPLEX;

	return 0;
}

#if (FTSSP_PROC_FS)
static void snd_ftssp_buf_max_read(struct snd_info_entry *entry,
	struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "%d\n", snd_ftssp_pcm_hw.buffer_bytes_max);
}

static void snd_ftssp_buf_max_write(struct snd_info_entry *entry,
	struct snd_info_buffer *buffer)
{
	char tmp[128];
	char *ptr_e;
	u32 val;

	if (buffer->size == 0)
		return;

	memset(tmp, 0, 128);
	snd_info_get_str(tmp, buffer->buffer, 127);

	val = simple_strtoul(tmp, &ptr_e, 10);
	if (*ptr_e == 'k')
		val *= 1024;
	else if (*ptr_e == 'm')
		val *= 1024 * 1024;

	if (ac97) {
		if (val > AC97_HW_BUFFER_BYTES_MAX)
			val = AC97_HW_BUFFER_BYTES_MAX;
	} else {
		if (val > I2S_HW_BUFFER_BYTES_MAX)
			val = I2S_HW_BUFFER_BYTES_MAX;
	}

	snd_ftssp_pcm_hw.buffer_bytes_max = (size_t)val;
}

static void snd_ftssp_period_min_read(struct snd_info_entry *entry,
	struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "%d\n", snd_ftssp_pcm_hw.period_bytes_min);
}

static void snd_ftssp_period_min_write(struct snd_info_entry *entry,
	struct snd_info_buffer *buffer)
{
	char tmp[128];
	char *ptr_e;
	u32 val;

	if (buffer->size == 0)
		return;

	memset(tmp, 0, 128);
	snd_info_get_str(tmp, buffer->buffer, 127);

	val = simple_strtoul(tmp, &ptr_e, 10);
	if (*ptr_e == 'k')
		val *= 1024;
	else if (*ptr_e == 'm')
		val *= 1024 * 1024;

	snd_ftssp_pcm_hw.period_bytes_min = (size_t)val;

	if ((val * snd_ftssp_pcm_hw.periods_max) >
	    snd_ftssp_pcm_hw.buffer_bytes_max) {
		INFO("\nWarning: period_bytes(%d) * periods(%d) exceeds hw_buffer_size(%d).\n",
			snd_ftssp_pcm_hw.period_bytes_min,
			snd_ftssp_pcm_hw.periods_max,
			snd_ftssp_pcm_hw.buffer_bytes_max);
		INFO("         Unexpected access violation may occur!\n");
	}
}

static void snd_ftssp_period_max_read(struct snd_info_entry *entry,
	struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "%d\n", snd_ftssp_pcm_hw.period_bytes_max);
}

static void snd_ftssp_period_max_write(struct snd_info_entry *entry,
	struct snd_info_buffer *buffer)
{
	char tmp[128];
	char *ptr_e;
	u32 val;

	if (buffer->size == 0)
		return;

	memset(tmp, 0, 128);
	snd_info_get_str(tmp, buffer->buffer, 127);

	val = simple_strtoul(tmp, &ptr_e, 10);
	if (*ptr_e == 'k')
		val *= 1024;
	else if (*ptr_e == 'm')
		val *= 1024 * 1024;

	snd_ftssp_pcm_hw.period_bytes_max = (size_t)val;

	if ((val * snd_ftssp_pcm_hw.periods_max) >
	    snd_ftssp_pcm_hw.buffer_bytes_max) {
		INFO("\nWarning: period_bytes(%d) * periods(%d) exceeds hw_buffer_size(%d).\n",
			snd_ftssp_pcm_hw.period_bytes_max,
			snd_ftssp_pcm_hw.periods_max,
			snd_ftssp_pcm_hw.buffer_bytes_max);
		INFO("         Unexpected access violation may occur!\n");
	}
}

static void snd_ftssp_periods_min_read(struct snd_info_entry *entry,
	struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "%d\n", snd_ftssp_pcm_hw.periods_min);
}

static void snd_ftssp_periods_min_write(struct snd_info_entry *entry,
	struct snd_info_buffer *buffer)
{
	char tmp[128];
	char *ptr_e;
	u32 val;

	if (buffer->size == 0)
		return;

	memset(tmp, 0, 128);
	snd_info_get_str(tmp, buffer->buffer, 127);

	val = simple_strtoul(tmp, &ptr_e, 10);
	if (*ptr_e == 'k')
		val *= 1024;
	else if (*ptr_e == 'm')
		val *= 1024 * 1024;

	snd_ftssp_pcm_hw.periods_min = (size_t)val;

	if ((val * snd_ftssp_pcm_hw.period_bytes_max) >
	    snd_ftssp_pcm_hw.buffer_bytes_max) {
		INFO("\nWarning: period_bytes(%d) * periods(%d) exceeds hw_buffer_size(%d).\n",
			snd_ftssp_pcm_hw.period_bytes_max,
			snd_ftssp_pcm_hw.periods_min,
			snd_ftssp_pcm_hw.buffer_bytes_max);
		INFO("         Unexpected access violation may occur!\n");
	}
}

static void snd_ftssp_periods_max_read(struct snd_info_entry *entry,
	struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "%d\n", snd_ftssp_pcm_hw.periods_max);
}

static void snd_ftssp_periods_max_write(struct snd_info_entry *entry,
	struct snd_info_buffer *buffer)
{
	char tmp[128];
	char *ptr_e;
	u32 val;

	if (buffer->size == 0)
		return;

	memset(tmp, 0, 128);
	snd_info_get_str(tmp, buffer->buffer, 127);

	val = simple_strtoul(tmp, &ptr_e, 10);
	if (*ptr_e == 'k')
		val *= 1024;
	else if (*ptr_e == 'm')
		val *= 1024 * 1024;

	snd_ftssp_pcm_hw.periods_max = (size_t)val;

	if ((val * snd_ftssp_pcm_hw.period_bytes_max) >
	    snd_ftssp_pcm_hw.buffer_bytes_max) {
		INFO("\nWarning: period_bytes(%d) * periods(%d) exceeds hw_buffer_size(%d).\n",
			snd_ftssp_pcm_hw.period_bytes_max,
			snd_ftssp_pcm_hw.periods_max,
			snd_ftssp_pcm_hw.buffer_bytes_max);
		INFO("         Unexpected access violation may occur!\n");
	}
}
#endif  //FTSSP_PROC_FS

static inline void ftssp_ac97_init(void)
{
	/* Change codec-dependent callbacks to AC97 */
	ftssp010_substreams[0].pmu_set_clocking = pmu_set_ac97_clocking;
	ftssp010_substreams[0].hw_config        = ftssp010_config_ac97_play;
	ftssp010_substreams[1].pmu_set_clocking = pmu_set_ac97_clocking;
	ftssp010_substreams[1].hw_config        = ftssp010_config_ac97_rec;

	snd_ftssp_playback_ops.copy_user = snd_ftssp_playback_copy;
	snd_ftssp_playback_ops.copy_kernel = snd_ftssp_playback_copy;
	snd_ftssp_capture_ops.copy_user  = snd_ftssp_capture_copy;
	snd_ftssp_capture_ops.copy_kernel  = snd_ftssp_capture_copy;

	snd_ftssp_pcm_hw.rates          = AC97_CODEC_SAMPLE_RATES;
	snd_ftssp_pcm_hw.rate_min       = AC97_CODEC_SAMPLE_RATE_MIN;
	snd_ftssp_pcm_hw.rate_max       = AC97_CODEC_SAMPLE_RATE_MAX;
	snd_ftssp_pcm_hw.formats        = AC97_CODEC_FORMATS;
	snd_ftssp_pcm_hw.buffer_bytes_max = AC97_HW_BUFFER_BYTES_MAX;
	snd_ftssp_pcm_hw.period_bytes_min = AC97_HW_PERIOD_BYTES_MIN;
	snd_ftssp_pcm_hw.period_bytes_max = AC97_HW_PERIOD_BYTES_MAX;
	snd_ftssp_pcm_hw.periods_min      = AC97_HW_PERIODS_MIN;
	snd_ftssp_pcm_hw.periods_max      = AC97_HW_PERIODS_MAX;
}

static int ftssp_alsa_init(struct platform_device *pdev)
{
	ftssp_chip *chip;
	int err;

	if (init_hw(cardno, ac97, NULL))
		return -EIO;

	if (ac97)
		ftssp_ac97_init();

	DBG("%s: FTSSP010 #%d (Physical Addr=0x%08X), mode: %s\n",
		__func__,
		cardno, SSP_FTSSP010_pa_base[cardno],
		ac97 ? "ac97" : "i2s");

	err = snd_card_new(&pdev->dev, SNDRV_DEFAULT_IDX1, FTSSP_CARD_ID,
			THIS_MODULE, sizeof(ftssp_chip), &ftssp_cards[cardno]);

	if (err < 0)
		return err;

	if (ac97) {
		sprintf(ftssp_cards[cardno]->driver, FTSSP_DRIVER_NAME);
		sprintf(ftssp_cards[cardno]->shortname,
			FTSSP_DRIVER_NAME "_ac97");
		sprintf(ftssp_cards[cardno]->longname,
			FTSSP_DRIVER_NAME "_ac97 controller");
	} else {
		sprintf(ftssp_cards[cardno]->driver, FTSSP_DRIVER_NAME);
		sprintf(ftssp_cards[cardno]->shortname,
			FTSSP_DRIVER_NAME "_i2s");
		sprintf(ftssp_cards[cardno]->longname,
			FTSSP_DRIVER_NAME "_i2s controller");
	}

	// PCM
	chip = (ftssp_chip *)(ftssp_cards[cardno]->private_data);
	chip->pdev = pdev;
	chip->card = ftssp_cards[cardno];

	err = snd_ftssp_new_pcm(chip);
	if (err) {
		ERR("%s, Can't new PCM devices\n", __func__);
		return -ENODEV;
	}

#if (FTSSP_PROC_FS)
	// new a proc entries subordinate to card->proc_root for debugging
	// /proc/card#/buf_max
	snd_card_proc_new(chip->card, "buf_max", &chip->info_buf_max);
	if (chip->info_buf_max) {
		chip->info_buf_max->c.text.read = snd_ftssp_buf_max_read;
		chip->info_buf_max->c.text.write = snd_ftssp_buf_max_write;
	}
	// /proc/card#/period_min
	snd_card_proc_new(chip->card, "period_size_min",
		&chip->info_period_min);
	if (chip->info_period_min) {
		chip->info_period_min->c.text.read = snd_ftssp_period_min_read;
		chip->info_period_min->c.text.write =
			snd_ftssp_period_min_write;
	}
	// /proc/card#/period_max
	snd_card_proc_new(chip->card, "period_size_max",
		&chip->info_period_max);
	if (chip->info_period_max) {
		chip->info_period_max->c.text.read = snd_ftssp_period_max_read;
		chip->info_period_max->c.text.write =
			snd_ftssp_period_max_write;
	}
	// /proc/card#/periods_min
	snd_card_proc_new(chip->card, "periods_min", &chip->info_periods_min);
	if (chip->info_periods_min) {
		chip->info_periods_min->c.text.read =
			snd_ftssp_periods_min_read;
		chip->info_periods_min->c.text.write =
			snd_ftssp_periods_min_write;
	}
	// /proc/card#/periods_max
	snd_card_proc_new(chip->card, "periods_max", &chip->info_periods_max);
	if (chip->info_periods_max) {
		chip->info_periods_max->c.text.read =
			snd_ftssp_periods_max_read;
		chip->info_periods_max->c.text.write =
			snd_ftssp_periods_max_write;
	}
#endif

	// Register the card to ALSA
	err = snd_card_register(chip->card);
	if (err == 0)
		INFO("%s card registered!\n", FTSSP_CARD_ID);

	return err;
}

static int ftssp_alsa_i2c_i2s_exit(void)
{
	DBG("%s, cleaning up\n", __func__);

	#ifndef CONFIG_SND_FTSSP010_AC97
		i2c_del_driver(&alc5630_driver);
	#endif

	dmad_channel_free(&dma_chreq_tx);
	dmad_channel_free(&dma_chreq_rx);
	snd_card_free(ftssp_cards[cardno]);

	return 0;
}


static int atf_ac97_probe(struct platform_device *pdev)
{
	int (*read_fixup)(void __iomem *addr, unsigned int val,
		unsigned int shift_bits);
	struct resource *r, *mem = NULL;
	size_t mem_size;
	int ret;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ssp2_pbase = r->start;
	mem_size = resource_size(r);
	mem = request_mem_region(r->start, mem_size, pdev->name);
	if (mem == NULL) {
		ERR("%s: request_mem_region failed\n", __func__);
		return -ENXIO;
	}

	ssp2_vbase = (u32 *)ioremap(mem->start, mem_size);

	/* Check SSP revision register */
	read_fixup = symbol_get(readl_fixup);
	ret = read_fixup((void __iomem *)(unsigned long)ssp2_vbase + 0x40,  0x00011506, 0);
	symbol_put(readl_fixup);
	if (!ret) {
		ERR("%s: fail to read revision reg, bitmap no support ftssp\n", __func__);
		return -ENXIO;
	}

	return ftssp_alsa_init(pdev);
}

static int atf_ac97_remove(struct platform_device *pdev)
{
	return ftssp_alsa_i2c_i2s_exit();
}

static const struct of_device_id atf_ac97_of_match[] = {
	{ .compatible = "andestech,atfac97", },
	{},
};

static struct platform_driver atf_ac97_driver = {
	.driver = {
		.name = "atfssp",
		.owner = THIS_MODULE,
		.of_match_table = atf_ac97_of_match,
	},
	.probe = atf_ac97_probe,
	.remove = atf_ac97_remove,
};
module_platform_driver(atf_ac97_driver);
