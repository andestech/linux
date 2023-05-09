// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Andes Technology Corporation
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <soc/andes/dmad.h>
#include <linux/dma-mapping.h>
#include "FTSSP010_UDA1345TS.h"

/* control0 register */
#define NACK		0x40000000
/* control2 register value */
#define SSP_ACCRST                  0x20    /* AC-Link Cold Reset Enable */
#define SSP_ACWRST                  0x10    /* AC-Link Warm Reset Enable */
#define SSP_TXFCLR                  0x8     /* TX FIFO Clear */
#define SSP_RXFCLR                  0x4     /* RX FIFO Clear */
#define SSP_TXDOE                   0x2     /* TX Data Output Enable */
#define SSP_SSPEN                   0x1     /* SSP Enable */
#define SSP_TFVE                    0x1f000 /* TX FIFO Valid Entries */
#define SSP_RFVE                    0x1f0   /* RX FIFO Valid Entries */
#define SSP_FIFO_THOD               0xc400
/* status register */
#define WBUSY			    0x08000000
#define ACTXDATA		    0x01000000
#define SSP_BUSY                    0x4

u32 __iomem *ssp2_vbase;
resource_size_t	ssp2_pbase;

#ifdef CONFIG_PLAT_AG101P
#define PMU_PDLLCR1		(pmu_base+0x34)
#define PMU_MFPSR		(pmu_base+0x28)
#define PMU_I2SAC97_REQACKCFG	(pmu_base+0xbc)
#define PMU_C4			(pmu_base+0xc4)
#endif
#define SSPCLK_TO_SCLKDIV(sspclk_div2, bps)	((sspclk_div2)/(bps)-1)

// Each client has this additional data
struct alc5630_data {
	struct i2c_client *client;
	struct delayed_work     work;
	unsigned long gpio2_value;
	struct mutex mtx;
};

static int i2s_alc5630_read(unsigned int raddr, char *data, struct i2c_client *client)
{
#ifndef CONFIG_SND_FTSSP010_AC97
	struct i2c_adapter *adap =  client->adapter;
	int ret;
#endif
	struct i2c_msg msg;
	int i2c_value;

	//Reading ALC5630 register
	msg.addr = raddr;
	msg.flags = (client->flags & I2C_M_TEN) | I2C_M_RD;
	msg.len = 1;
	msg.buf = (char *)data;

	//ret = i2c_transfer(adap, &msg, 1);
#ifndef CONFIG_SND_FTSSP010_AC97
	ret = i2c_transfer(adap, &msg, 1);

	if (ret != 0) {
		pr_err("i2c read failed\n");
		return -1;
	}
#endif

	i2c_value = (data[0]&0xff) << 8 | (data[1]&0xff);
	return i2c_value;
}

static void i2s_alc5630_write(unsigned int raddr, unsigned int data, struct i2c_client *client)
{
#ifndef CONFIG_SND_FTSSP010_AC97
	struct i2c_adapter *adap =  client->adapter;
	int ret;
#endif
	struct i2c_msg msg;
	char buf[3];

	//Writing ALC5630 register
	msg.addr = raddr;
	msg.flags = (client->flags & I2C_M_TEN) | ~I2C_M_RD;
	msg.len = 1;
	buf[0] = (data >> 8) & 0xff;
	buf[1] =  data & 0xff;
	msg.buf = (char *)buf;
	//ret = i2c_transfer(adap, &msg, 1);
#ifndef CONFIG_SND_FTSSP010_AC97
	ret = i2c_transfer(adap, &msg, 1);
	if (ret != 0)
		pr_err("i2c write failed\n");

#endif
}

static void i2s_al5630_slave_stereo_mode(struct i2c_client *client)
{
	i2s_alc5630_write(0x34, 0x8000, client); // codec slave mode
	i2s_alc5630_write(0x0c, 0x1010, client);
	i2s_alc5630_write(0x10, 0xee03, client);
	i2s_alc5630_write(0x1c, 0x0748, client);
	i2s_alc5630_write(0x62, 0x0000, client);
}

//End ADD by river 2011.01.26


/* Drive PMU to generate I2S main clocking signal. Also configures PMU to set correct DMA REQ/ACK pair */
void pmu_set_i2s_clocking(unsigned int speed)
{
	unsigned int pmu_pdllcr1; /* PLL/DLL Control Register 1 */
	/* Configure PMU to generate I2S main clock */
	#ifdef CONFIG_PLAT_AG101
	pmu_pdllcr1 = ioread32(PMU_PDLLCR1)&0xfff0ffff; /* Bit 19-16 are relevent */
	#endif

	switch (speed) {
	case 8000:
		pmu_pdllcr1 |= 0x00000000; /* 2.048MHz x2 */
		break;
	case 11025:
		pmu_pdllcr1 |= 0x00010000; /* 2.8224MHz x2 */
		break;
	case 16000:
		pmu_pdllcr1 |= 0x00020000; /* 4.096MHz x2 */
		break;
	case 22050:
		pmu_pdllcr1 |= 0x00030000; /* 5.6448MHz x2 */
		break;
	case 32000:
		pmu_pdllcr1 |= 0x00040000; /* 8.192MHz x2 */
		break;
	case 44100:
		pmu_pdllcr1 |= 0x00050000; /* 11.2896Mhz x2 */
		break;
	case 48000:
		pmu_pdllcr1 |= 0x00060000; /* 12.2880MHz x2 */
		break;
	default:
		pr_err("%s: Unknown i2s speed %d\n", __func__, speed);
	};

	#ifdef CONFIG_PLAT_AG101
	iowrite32(pmu_pdllcr1, PMU_PDLLCR1);
	/* Configure PMU to select I2S output (instead of AC97) */
	iowrite32(ioread32(PMU_MFPSR)&(~(1<<3)), PMU_MFPSR); /* clear bit 3 of MFPSR*/
	#endif
}

/* Drive PMU to generate AC97 main clocking signal. Also configures PMU to set correct DMA REQ/ACK pair */
void pmu_set_ac97_clocking(unsigned int speed)
{
	/* do nothing here */
}

/* Programs PMU to set I2S/AC97 DMA Channel, ch=0-7 */
void pmu_set_i2s_dma_channel(unsigned int ch)
{
	#ifdef CONFIG_PLAT_AG101
	ch &= 0x7;
	//iowrite32((ioread32(PMU_I2SAC97_REQACKCFG)&(~0x7))|ch, PMU_I2SAC97_REQACKCFG);
	iowrite32(0xa, PMU_I2SAC97_REQACKCFG);
	iowrite32(0xb, PMU_C4);
	#endif
}

static struct i2c_client *g_i2c_client;
void ftssp010_set_int_control(int cardno, unsigned int val)
{
	iowrite32(val, FTSSP010_INT_CONTROL(cardno));
}

unsigned int ftssp010_get_int_status(int cardno)
{
	return ioread32(FTSSP010_INT_STATUS(cardno));
}

int ftssp010_get_status(int cardno)
{
	return ioread32(FTSSP010_STATUS(cardno));
}

int ftssp010_tx_fifo_not_full(int cardno)
{
	return (ioread32(FTSSP010_STATUS(cardno)) & 0x2) == 0x2;
}

int ftssp010_tx_fifo_vaild_entries(int cardno)
{
	return (ioread32(FTSSP010_STATUS(cardno))>>12) & 0x1f;
}

#include "FTSSP010_W83972D.h"

// AC97 codec tags
#define TAG_COMMAND	0xe000
#define TAG_DATA	0x9800 /* Slot 3/4 */
#define TAG_DATA_MONO	0x9000 /* Slot 3 */
//#define TAG_DATA_LINE_IN	0x9000	/* Slot 3 */

void ftssp010_ac97_write_codec(unsigned int reg, unsigned int data)
{
	int cnt = 0x1000000;

	while ((ioread32(FTSSP010_STATUS(0)) & ACTXDATA) && cnt--)
		;

	if (!cnt)
		pr_err("wait transfer buffer timeout\n");

	iowrite32(0x0,  FTSSP010_INT_CONTROL(0));/*Disable interrupts & DMA req */
	iowrite32(ioread32(FTSSP010_CONTROL2(0)) | (SSP_TXFCLR|SSP_RXFCLR), FTSSP010_CONTROL2(0));
	iowrite32(TAG_COMMAND, FTSSP010_ACLINK_SLOT_VALID(0));
	iowrite32(((reg<<16)|data), FTSSP010_ACLINK_CMD(0));
	iowrite32(ioread32(FTSSP010_CONTROL2(0)) | (SSP_SSPEN|SSP_TXDOE), FTSSP010_CONTROL2(0));
	cnt = 0x1000000;
	while ((ioread32(FTSSP010_STATUS(0))&WBUSY) && cnt--)
		;

	if (!cnt)
		pr_err("write ac97 timeout\n");
	iowrite32(ioread32(FTSSP010_CONTROL2(0)) & (~(SSP_SSPEN|SSP_TXDOE)), FTSSP010_CONTROL2(0));
}

/*	Configure FTSSP010 to a given sampling rate and channel number
 *	for AC97 mode in playback mode
 */
int init_hw(unsigned int cardno, unsigned int ac97, struct i2c_client *client)
{
	int cnt = 0x1000000;

	g_i2c_client = client;

	if (ac97) {
		// Clear FFMT
		iowrite32(ioread32(FTSSP010_CONTROL0(cardno)) & 0xfff, FTSSP010_CONTROL0(cardno));
		// AC_link
		iowrite32(ioread32(FTSSP010_CONTROL0(cardno)) | 0x4000, FTSSP010_CONTROL0(cardno));

		iowrite32(0xc400,  FTSSP010_INT_CONTROL(cardno));
		if ((ioread32(FTSSP010_INT_CONTROL(cardno))) != 0xc400)
			return -EIO;

		iowrite32(0x20, FTSSP010_CONTROL2(cardno));  /* Cold Reset AC-Link */
		while (ioread32(FTSSP010_CONTROL2(cardno)) && cnt--)
			;
		if (!cnt)
			return -EIO;

	} else {
		#ifdef CONFIG_PLAT_AG101
		iowrite32(ioread32(PMU_MFPSR)&(~(1<<3)), PMU_MFPSR); /* clear bit 3 of MFPSR*/
		iowrite32(0xa, PMU_I2SAC97_REQACKCFG);
		iowrite32(0xb, PMU_C4);
		#endif

		i2s_al5630_slave_stereo_mode(client);
		iowrite32(0x311c, FTSSP010_CONTROL0(cardno));	/* I2S Master */
		iowrite32(0, FTSSP010_CONTROL1(cardno));	        /* I2S Master */
		iowrite32(0xc400, FTSSP010_INT_CONTROL(cardno));	/* I2S Master */
		iowrite32(0x40, FTSSP010_CONTROL2(cardno)); /* Reset AC-Link */
	}
	return 0;
}
static void _ftssp010_config_ac97(int cardno, unsigned int is_stereo, unsigned int speed, int is_rec)
{
	int cnt = 0x1000000;
	/* Codec initialization */
	iowrite32(ioread32(FTSSP010_CONTROL0(cardno))|NACK, FTSSP010_CONTROL0(cardno));
	ftssp010_ac97_write_codec(W83972D_RESET, 0);

	if (is_rec) {	/* Recording */
		/* Mute output */
		ftssp010_ac97_write_codec(W83972D_STEREO_OUTPUT_CONTROL, 0x8000);
		/* Mute PCM */
		//ftssp010_ac97_write_codec(W83972D_PCM_OUTPUT_CONTROL, 0x8000);

		/* Register 0x10, Line-In/Mic Gain */
		//ftssp010_ac97_write_codec(W83972D_AUX_INPUT_CONTROL, 0x808);
		/* FIXME: REC from line-in only */

		/* Register 0x1A, Record Select=StereoMix */
		ftssp010_ac97_write_codec(W83972D_RECORD_SELECT, 0x0000 /*404*/);
		ftssp010_ac97_write_codec(W83972D_MIC_VOLUME, 0x0040);
		/* Register 0x1C, Record Gain=0db */
		ftssp010_ac97_write_codec(W83972D_RECORD_GAIN_MIC, 0x0808);
		ftssp010_ac97_write_codec(W83972D_PCM_OUTPUT_CONTROL, 0x8000);
		ftssp010_ac97_write_codec(W83972D_RECORD_GAIN, 0x0808);
	} else {	/* Playback */
		/* Register 0x10, Mute Line-In/Mic Gain */
		ftssp010_ac97_write_codec(W83972D_LINE_IN_VOLUME, 0x8000);
		ftssp010_ac97_write_codec(W83972D_MIC_VOLUME, 0x8000);

		/* Register 0x1A, Mute Record Gains */
		ftssp010_ac97_write_codec(W83972D_RECORD_GAIN, 0x8000);
		ftssp010_ac97_write_codec(W83972D_RECORD_GAIN_MIC, 0x8000);

		/* Output */
		ftssp010_ac97_write_codec(W83972D_STEREO_OUTPUT_CONTROL, 0);
		ftssp010_ac97_write_codec(W83972D_PCM_OUTPUT_CONTROL, 0x808);
	}

	ftssp010_ac97_write_codec(W83972D_EXT_AUDIO_CONTROL, 0x1);
	ftssp010_ac97_write_codec(W83972D_DAC_SAMPLE_RATE_CONTROL, speed);

	iowrite32(ioread32(FTSSP010_CONTROL0(cardno))&~NACK, FTSSP010_CONTROL0(cardno));

	/* Start data transfer */
//	if(is_rec) {
//		iowrite32(TAG_DATA_LINE_IN, FTSSP010_ACLINK_SLOT_VALID(cardno));
//	} else {
		if (is_stereo)
			iowrite32(TAG_DATA, FTSSP010_ACLINK_SLOT_VALID(cardno));
		else
			iowrite32(TAG_DATA_MONO, FTSSP010_ACLINK_SLOT_VALID(cardno));
//	}
	while ((ioread32(FTSSP010_INT_STATUS(cardno)) & 0x3) && cnt--)
		;
}

void ftssp010_config_ac97_play(int cardno, unsigned int is_stereo, unsigned int speed, int use8bit)
{
	_ftssp010_config_ac97(cardno, is_stereo, speed, 0);
}

void ftssp010_config_ac97_rec(int cardno, unsigned int is_stereo, unsigned int speed, int use8bit)
{
	_ftssp010_config_ac97(cardno, is_stereo, speed, 1);
}

/*
 *	Configure FTSSP010 to a given sampling rate and channel number
 *	for I2S mode
 */
void ftssp010_config(int cardno, unsigned int is_stereo, unsigned int speed, int width, int is_rec)
{
	int use8bit = (width == 1 ? 1 : 0);
	unsigned int opm, bps = 2 * (use8bit ? 8 : 16);	/* bits per 1 second audio data. */
	unsigned int fpclkdiv = 0;
	struct alc5630_data *alc5630;
	char data[3];

	opm = is_stereo ? FTSSP010_CONTROL0_OPM_STEREO : FTSSP010_CONTROL0_OPM_MONO;
	iowrite32(0x3100 | opm, FTSSP010_CONTROL0(cardno));	/* I2S Master */

	/* configures CONTROL1 to use suitable clock divider.
	 * the I2S clock is generated from PMU.
	 */
	bps *= speed;
	switch (speed) {
	case 8000:	/* SCLK : 256KHZ */
		i2s_alc5630_write(0x44, 0x3ea0, g_i2c_client);
		i2s_alc5630_write(0x60, 0x3174, g_i2c_client);
		i2s_alc5630_write(0x62, 0x1010, g_i2c_client);
		fpclkdiv = 0xBB;
		break;
	case 11025:	/* SCLK : 352.8KHZ */
		i2s_alc5630_write(0x44, 0x3ea0, g_i2c_client);
		i2s_alc5630_write(0x60, 0x3174, g_i2c_client);
		i2s_alc5630_write(0x62, 0x1010, g_i2c_client);
		fpclkdiv = 0x88;
		break;
	case 16000:	/* SCLK : 512KHZ */
		i2s_alc5630_write(0x44, 0x3ea0, g_i2c_client);
		i2s_alc5630_write(0x60, 0x3174, g_i2c_client);
		i2s_alc5630_write(0x62, 0x1010, g_i2c_client);
		fpclkdiv = 0x5f;
		break;
	case 22050:	/* SCLK : 705.6KHZ */
		i2s_alc5630_write(0x44, 0x3ea0, g_i2c_client);
		i2s_alc5630_write(0x60, 0x3174, g_i2c_client);
		i2s_alc5630_write(0x62, 0x1010, g_i2c_client);
		fpclkdiv = 0x45;
		break;
	case 24000:	/* SCLK : 768KHZ */
		i2s_alc5630_write(0x44, 0x3ea0, g_i2c_client);
		i2s_alc5630_write(0x60, 0x3174, g_i2c_client);
		i2s_alc5630_write(0x62, 0x1010, g_i2c_client);
		fpclkdiv = 0x3e;
		break;
	case 32000:	/* SCLK : 1024KHZ */
		i2s_alc5630_write(0x44, 0x3ea0, g_i2c_client);
		i2s_alc5630_write(0x60, 0x3174, g_i2c_client);
		i2s_alc5630_write(0x62, 0x1010, g_i2c_client);
		fpclkdiv = 0x2f;
		break;
	case 44100:	/* SCLK : 1.4112 MHZ */ /* 96 MHZ */
		i2s_alc5630_write(0x44, 0x3ea0, g_i2c_client);
		i2s_alc5630_write(0x60, 0x3174, g_i2c_client);
		i2s_alc5630_write(0x62, 0x1010, g_i2c_client);
		fpclkdiv = 0x22;
		break;
	case 48000: /* SCLK : 1.536 MHZ */
		i2s_alc5630_write(0x44, 0x3ea0, g_i2c_client);
		i2s_alc5630_write(0x60, 0x3174, g_i2c_client);
		i2s_alc5630_write(0x62, 0x1010, g_i2c_client);
		fpclkdiv = 0x1f;
		break;
	default:
		pr_err("%s: unsupported speed %d\n", __func__, speed);
		return;
	};

	if (!use8bit)
		iowrite32(0xf0000|fpclkdiv, FTSSP010_CONTROL1(cardno));	/*  16bits */
	else
		iowrite32(0x70000|fpclkdiv, FTSSP010_CONTROL1(cardno));	/*  8bits */

	if (is_rec)
		iowrite32(ioread32(FTSSP010_INT_CONTROL(cardno))&(~0x0f15),  FTSSP010_INT_CONTROL(cardno));		/* Disable all interrupts */
	else
		iowrite32(ioread32(FTSSP010_INT_CONTROL(cardno))&(~0xf02a),  FTSSP010_INT_CONTROL(cardno));		/* Disable all interrupts */

	iowrite32(0xc, FTSSP010_CONTROL2(cardno));	/* clear FIFOs */
	alc5630 = i2c_get_clientdata(g_i2c_client);

	if (is_rec) {
		pr_err("%s for I2S mode in record.\n", __func__);
		i2s_alc5630_write(0x0e, 0x0808, g_i2c_client);
		i2s_alc5630_write(0x10, 0xee03, g_i2c_client);
		i2s_alc5630_write(0x22, 0x0500, g_i2c_client);
		i2s_alc5630_write(0x1c, 0x0748, g_i2c_client);
		i2s_alc5630_write(0x14, 0x1f1f, g_i2c_client);
		i2s_alc5630_write(0x12, 0xdfdf, g_i2c_client);
		i2s_alc5630_write(0x26, 0x000f, g_i2c_client);
		i2s_alc5630_write(0x3a, 0xffff, g_i2c_client);
		i2s_alc5630_write(0x3c, 0xffff, g_i2c_client);
		i2s_alc5630_write(0x3e, 0x80cf, g_i2c_client);
		i2s_alc5630_write(0x44, 0x3ea0, g_i2c_client);
		i2s_alc5630_write(0x42, 0x2000, g_i2c_client);
		i2s_alc5630_write(0x40, 0x8c0a, g_i2c_client);
		i2s_alc5630_write(0x02, 0x8080, g_i2c_client);
		i2s_alc5630_write(0x04, 0x0000, g_i2c_client);
	} else {
		i2s_alc5630_write(0x0e, 0x0808, g_i2c_client);
		i2s_alc5630_write(0x12, 0xcbcb, g_i2c_client);
		i2s_alc5630_write(0x14, 0x7f7f, g_i2c_client);
		i2s_alc5630_write(0x22, 0x0000, g_i2c_client);
		i2s_alc5630_write(0x3e, 0x8000, g_i2c_client);
		i2s_alc5630_write(0x40, 0x0c0a, g_i2c_client);
		i2s_alc5630_write(0x42, 0x0000, g_i2c_client);
		i2s_alc5630_write(0x26, 0x0000, g_i2c_client);
		i2s_alc5630_write(0x3c, 0x2000, g_i2c_client);
		i2s_alc5630_write(0x3a, 0x0002, g_i2c_client);
		i2s_alc5630_write(0x3c, 0xa330, g_i2c_client);
		i2s_alc5630_write(0x3a, 0xc843, g_i2c_client);
		i2s_alc5630_write(0x3A, i2s_alc5630_read(0x3A, data, g_i2c_client)|0x0002, g_i2c_client);
		i2s_alc5630_write(0x04, i2s_alc5630_read(0x04, data, g_i2c_client)|0x8080, g_i2c_client);
		i2s_alc5630_write(0x3A, i2s_alc5630_read(0x3A, data, g_i2c_client)|0x0040, g_i2c_client);
		i2s_alc5630_write(0x3c, i2s_alc5630_read(0x3C, data, g_i2c_client)|0x2000, g_i2c_client);
		i2s_alc5630_write(0x3E, i2s_alc5630_read(0x3E, data, g_i2c_client)|0xfC00, g_i2c_client);
		i2s_alc5630_write(0x5E, i2s_alc5630_read(0x5E, data, g_i2c_client)|0x0100, g_i2c_client);
		i2s_alc5630_write(0x3A, i2s_alc5630_read(0x3A, data, g_i2c_client)|0x0200, g_i2c_client);
		i2s_alc5630_write(0x3A, i2s_alc5630_read(0x3A, data, g_i2c_client)|0x0100, g_i2c_client);
		i2s_alc5630_write(0x5E, i2s_alc5630_read(0x5E, data, g_i2c_client) & 0xfeff, g_i2c_client);
		i2s_alc5630_write(0x1c, 0x0748, g_i2c_client);
		i2s_alc5630_write(0x26, 0x000f, g_i2c_client);

		if (alc5630->gpio2_value == 0x0)
			i2s_alc5630_write(0x3A, (i2s_alc5630_read(0x3A, data, g_i2c_client) & 0xFBFF)|0x0040, g_i2c_client);
		else
			i2s_alc5630_write(0x3A, i2s_alc5630_read(0x3A, data, g_i2c_client)|0x0440, g_i2c_client);

		i2s_alc5630_write(0x5E, i2s_alc5630_read(0x5E, data, g_i2c_client)|0x0020, g_i2c_client);
		i2s_alc5630_write(0x5E, i2s_alc5630_read(0x5E, data, g_i2c_client)|0x00c0, g_i2c_client);
		i2s_alc5630_write(0x04, i2s_alc5630_read(0x04, data, g_i2c_client) & 0x7f7f, g_i2c_client);
		if (alc5630->gpio2_value == 0x0)
			i2s_alc5630_write(0x02, 0x5F5F, g_i2c_client);
		else
			i2s_alc5630_write(0x02, 0x0000, g_i2c_client);
	}
}

void ftssp010_config_tx(int cardno, unsigned int is_stereo, unsigned int speed, int width)
{
	return ftssp010_config(cardno, is_stereo, speed, width, 0);
}

void ftssp010_config_rx(int cardno, unsigned int is_stereo, unsigned int speed, int width)
{
	return ftssp010_config(cardno, is_stereo, speed, width, 1);
}

/* Configures FTSSP010 to start TX. If use_dma being nonzero,
 * FTSSP010 will use hardware handshake for DMA
 */
void ftssp010_start_tx(int cardno, unsigned int use_dma)
{
	unsigned int bogus = 0x800 * 3;

	if (use_dma) {
		/* Enable H/W DMA Request and set TX DMA threshold to 12*/
		iowrite32(ioread32(FTSSP010_INT_CONTROL(cardno)) | 0xc422, FTSSP010_INT_CONTROL(cardno));
	}

	iowrite32(ioread32(FTSSP010_CONTROL2(cardno)) | (SSP_SSPEN|SSP_TXDOE), FTSSP010_CONTROL2(cardno));
	if (!use_dma) {
		while (bogus > 0) {
			while (!ftssp010_tx_fifo_not_full(cardno))
				udelay(50);

			iowrite32(0, FTSSP010_DATA(cardno));
			bogus--;
		}
	}
}

/* Configures FTSSP010 to start RX. If use_dma being nonzero,
 * FTSSP010 will use hardware handshake for DMA
 */
void ftssp010_start_rx(int cardno, unsigned int use_dma)
{
	if (use_dma) {
		/* Enable H/W DMA Request and set RX DMA threshold to 2 */
		iowrite32(ioread32(FTSSP010_INT_CONTROL(cardno)) | 0xc411, FTSSP010_INT_CONTROL(cardno));
	}

	iowrite32(ioread32(FTSSP010_CONTROL2(cardno)) | 0x3, FTSSP010_CONTROL2(cardno));
}

void ftssp010_stop_tx(int cardno)
{
	iowrite32(ioread32(FTSSP010_INT_CONTROL(cardno)) & (~0x22), FTSSP010_INT_CONTROL(cardno));
	iowrite32(ioread32(FTSSP010_CONTROL2(0)) & (~(SSP_SSPEN|SSP_TXDOE)), FTSSP010_CONTROL2(0));
}

void ftssp010_stop_rx(int cardno)
{
	iowrite32(ioread32(FTSSP010_INT_CONTROL(cardno)) & (~0x11), FTSSP010_INT_CONTROL(cardno));
}

