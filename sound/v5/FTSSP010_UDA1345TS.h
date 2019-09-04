/* FTSSP010 - UDA1345TS supporting library header */
/*
 *
 *      $log$
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>

/* Programming sequence:
 *	Suppose your playback format is 44.1KHz, 16 bit stereo
 *	PIO mode:
 *		pmu_set_i2s_clocking(44100);
 *		ftssp010_config(1, 44100, 0);
 *		ftssp010_start_tx(0);
 *		while(ftssp010_tx_fifo_not_full()) {
 *			Poke_your_PCM_data_to_FTSSP_data_port
 *
 *	DMA mode:
 *		pmu_set_i2s_clocking(44100);
 *		ftssp010_config(1, 44100);
 *		<setup DMA controller, acquire DMA channel>
 *		pmu_set_i2s_dma_channel(ch);
 *		ftssp010_start_tx(1);
 *		<wait DMA to complete..>
 *		ftssp010_stop_tx();
 */

extern  resource_size_t	ssp2_pbase;
extern  resource_size_t	ssp2_vbase;

#define SSP_FTSSP010_COUNT	1
#define PMU_BASE			(pmu_base)
#define FTSSP010_DATA(x)		(ssp2_vbase+0x18)
#define FTSSP010_DATA_PA(x)		(ssp2_pbase+0x18)

/* Initialize FTSSP010 to output to UDA1345TS via I2S */
#define FTSSP010_CONTROL0(x)		(ssp2_vbase+0x0)
#define FTSSP010_CONTROL0_OPM_STEREO	0xC
#define FTSSP010_CONTROL0_OPM_MONO	0x8

#define FTSSP010_CONTROL1(x)		(ssp2_vbase+0x4)
#define FTSSP010_CONTROL2(x)		(ssp2_vbase+0x8)

#define FTSSP010_INT_CONTROL(x)		(ssp2_vbase+0x10)
#define FTSSP010_STATUS(x)		(ssp2_vbase+0xC)
#define FTSSP010_INT_STATUS(x)		(ssp2_vbase+0x14)
#define FTSSP010_DATA(x)		(ssp2_vbase+0x18)
#define FTSSP010_INFO(x)		(ssp2_vbase+0x1C)
#define FTSSP010_ACLINK_SLOT_VALID(x)	(ssp2_vbase+0x20)
#define FTSSP010_ACLINK_CMD(x)		(ssp2_vbase+0x28)
#define FTSSP010_ACLINK_CMD_DATA(x)	(ssp2_vbase+0x2c)


/* Drive PMU to generate I2S main clocking signal. Also configures PMU to set correct DMA REQ/ACK pair */
extern void pmu_set_i2s_clocking(unsigned int speed);
/* Programs PMU to set I2S/AC97 DMA Channel, ch=0-7 */
extern void pmu_set_i2s_dma_channel(unsigned ch);

/* Drive PMU to generate AC97 main clocking signal. Also configures PMU to set correct DMA REQ/ACK pair */
extern void pmu_set_ac97_clocking(unsigned int speed);

/* Returns FTSSP010 status */
extern void ftssp010_set_int_control(int cardno, unsigned val);
extern int ftssp010_get_status(int cardno);
extern unsigned ftssp010_get_int_status(int cardno);
/* Polls FIFO full register */
extern int  ftssp010_tx_fifo_not_full(int cardno);
/* Configure FTSSP010 to a given sampling rate and channel number */
extern void ftssp010_config_tx(int cardno, unsigned is_stereo, unsigned speed, int use8bit);
extern void ftssp010_config_rx(int cardno, unsigned is_stereo, unsigned speed, int use8bit);

/* Configure FTSSP010 to a given sampling rate and channel number */
extern void ftssp010_config_ac97_play(int cardno, unsigned is_stereo, unsigned speed, int use8bit);

extern void ftssp010_config_ac97_rec(int cardno, unsigned is_stereo, unsigned speed, int use8bit);

/* Initialize FTSSP010 to output to UDA1345TS via I2S */
extern void ftssp010_start_tx(int cardno, unsigned use_dma);
extern void ftssp010_start_rx(int cardno, unsigned use_dma);
extern void ftssp010_stop_tx(int cardno);
extern void ftssp010_stop_rx(int cardno);
