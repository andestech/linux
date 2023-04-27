// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

/*
 * HBP : Horizontal Back Porch
 * HFP : Horizontal Front Porch
 * HSPW: Horizontal Sync. Pulse Width
 * PPL : Pixels-per-line = 16(PPL+1)
 */
#define ENC_PARAM_TIME0(HBP, HFP, HSPW, PPL)	\
	((((HBP)         - 1) << 24) |		\
	 (((HFP)         - 1) << 16) |		\
	 (((HSPW)        - 1) << 8) |		\
	 ((((PPL) >> 4) - 1) << 2))

/*
 * HBP : Vertical Back Porch
 * HFP : Vertical Front Porch
 * HSPW: Vertical Sync. Pulse Width
 * LPP : Lines-per-panel = LPP + 1
 */
#define ENC_PARAM_TIME1(VBP, VFP, VSPW, LPP)	\
	 ((VBP << 24) |			\
	 ((VFP) << 16) |			\
	 (((VSPW) - 1) << 10) |			\
	 (((LPP) - 1)))

/*
 * PRA : Pixel Rate Adaptive
 * IOE : Invert Panel Output Enable
 * IPC : Invert Panel Clock (Test Chip Testing)
 * IHS : Invert Horisontal Sync.
 * IVS : Invert Versical Sync.
 * PCD : Panel Clock Divisor
 */
#define ENC_PARAM_TIME2(PRA, IOE, IPC, IHS, IVS, PCD)	\
	(((PRA)     << 15) |				\
	 ((IOE)     << 14) |				\
	 ((IPC)     << 13) |				\
	 ((IHS)     << 12) |				\
	 ((IVS)     << 11) |				\
	 (((PCD) - 1)))

/*
 * Enable YCbCr
 * Enable YCbCr420
 * FIFO threadhold
 * Panel type, 0-6bit, 1-8bit
 * LcdVComp, when to generate interrupt, 1: start of back_porch
 * Power Enable
 * Big Endian Pixel/Byte Ordering
 * BGR
 * TFT
 * LCD bits per pixel
 * Controller Enable
 */

#define ENC_PARAM_CTRL(ENYUV, ENYUV420, FIFOTH, PTYPE, VCOMP, LCD_ON, ENDIAN, BGR, TFT, BPP, LCD_EN) \
	((ENYUV        << 18) |			\
	 (ENYUV420     << 17) |			\
	 (FIFOTH       << 16) |			\
	 (PTYPE        << 15) |			\
	 (VCOMP        << 12) |			\
	 (LCD_ON       << 11) |			\
	 (ENDIAN       <<  9) |			\
	 (BGR          <<  8) |			\
	 (TFT          <<  5) |			\
	 (BPP          <<  1) |			\
	 (LCD_EN))

static struct lcd_param control[] = {

	{
		.value = ENC_PARAM_CTRL(0, 0, 1, 1, 0x3, 1, 0x0, 1, 1, 0x3, 1),
		.flags = FFB_MODE_RGB | FFB_MODE_8BPP,
	},
	{
		.value = ENC_PARAM_CTRL(1, 1, 1, 1, 0x3, 1, 0x0, 0, 1, 0x3, 1),
		.flags = FFB_MODE_YUV420 | FFB_MODE_8BPP,
	},
	{
		.value = ENC_PARAM_CTRL(1, 0, 1, 1, 0x3, 1, 0x0, 0, 1, 0x4, 1),
		.flags = FFB_MODE_YUV422 | FFB_MODE_16BPP,
	},
	{
		.value = ENC_PARAM_CTRL(0, 0, 1, 1, 0x3, 1, 0x0, 1, 1, 0x4, 1),
		.flags = FFB_MODE_RGB | FFB_MODE_16BPP,
	},
	{
		.value = ENC_PARAM_CTRL(0, 0, 1, 1, 0x3, 1, 0x0, 1, 1, 0x5, 1),
		.flags = FFB_MODE_RGB | FFB_MODE_24BPP,
	},
};

#ifdef CONFIG_PANEL_AUA036QN01

static struct lcd_param time0[] = {

	{
		.value = ENC_PARAM_TIME0(7, 6, 1, 320),
		.flags = FFB_MODE_RGB | FFB_MODE_YUV420 | FFB_MODE_YUV422,
	},
};

static struct lcd_param time1[] = {

	{
		.value = ENC_PARAM_TIME1(1, 1, 1, 240),
		.flags = FFB_MODE_RGB | FFB_MODE_YUV420 | FFB_MODE_YUV422,
	},
};

static struct lcd_param time2[] = {

	{
		.value = ENC_PARAM_TIME2(0, 0, 1, 1, 1, 0x7),
		.flags = FFB_MODE_RGB | FFB_MODE_YUV420 | FFB_MODE_YUV422,
	},
};

static struct faradayfb_mach_info ffb_mach_info = {

	.pixclock      = 171521,
	.xres          = 320,
	.yres          = 240,
	.max_bpp       = 24,
	.sync          = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.num_time0     = ARRAY_SIZE(time0),
	.time0         = time0,
	.num_time1     = ARRAY_SIZE(time1),
	.time1         = time1,
	.num_time2     = ARRAY_SIZE(time2),
	.time2         = time2,
	.num_control   = ARRAY_SIZE(control),
	.control       = control,
};
#endif

#ifdef CONFIG_PANEL_AUA070VW04
static struct lcd_param time0[] = {

	{
		.value = ENC_PARAM_TIME0(88, 40, 128, 800),
		.flags = FFB_MODE_RGB | FFB_MODE_YUV420 | FFB_MODE_YUV422,
	},
};

static struct lcd_param time1[] = {

	{
		.value = ENC_PARAM_TIME1(21, 1, 3, 480),
		.flags = FFB_MODE_RGB | FFB_MODE_YUV420 | FFB_MODE_YUV422,
	},
};

static struct lcd_param time2[] = {

	{
//		.value = ENC_PARAM_TIME2(0, 1, 1, 1, 1, 0x5),
		.value = ENC_PARAM_TIME2(1, 0, 0, 1, 1, 0x5),
		.flags = FFB_MODE_RGB | FFB_MODE_YUV420 | FFB_MODE_YUV422,
	},
};

static struct faradayfb_mach_info ffb_mach_info = {

	.pixclock      = 171521,
	.xres          = 800,
	.yres          = 480,
	.max_bpp       = 24,
	.sync          = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.num_time0     = ARRAY_SIZE(time0),
	.time0         = time0,
	.num_time1     = ARRAY_SIZE(time1),
	.time1         = time1,
	.num_time2     = ARRAY_SIZE(time2),
	.time2         = time2,
	.num_control   = ARRAY_SIZE(control),
	.control       = control,
};

#endif

#ifdef CONFIG_PANEL_LW500AC9601
static struct lcd_param time0[] = {
	{
		.value = ENC_PARAM_TIME0(88, 40, 128, 800),
		.flags = FFB_MODE_RGB | FFB_MODE_YUV420 | FFB_MODE_YUV422,
	},
};

static struct lcd_param time1[] = {
	{
		.value = ENC_PARAM_TIME1(33, 10, 2, 480),
		.flags = FFB_MODE_RGB | FFB_MODE_YUV420 | FFB_MODE_YUV422,
	},
};

static struct lcd_param time2[] = {
	{
		.value = ENC_PARAM_TIME2(1, 0, 1, 1, 1, 0x3),
		.flags = FFB_MODE_RGB | FFB_MODE_YUV420 | FFB_MODE_YUV422,
	},
};

static struct faradayfb_mach_info ffb_mach_info = {
	.pixclock      = 15000000,
	.xres          = 800,
	.yres          = 480,
	.max_bpp       = 24,
	.sync          = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.num_time0     = ARRAY_SIZE(time0),
	.time0         = time0,
	.num_time1     = ARRAY_SIZE(time1),
	.time1         = time1,
	.num_time2     = ARRAY_SIZE(time2),
	.time2         = time2,
	.num_control   = ARRAY_SIZE(control),
	.control       = control,
};
#endif

#ifdef CONFIG_PANEL_CH7013A
static struct lcd_param time0[] = {

	{
		.value = ENC_PARAM_TIME0(88, 40, 128, 800),
		.flags = FFB_MODE_RGB,
	},
};

static struct lcd_param time1[] = {
	{
		.value = ENC_PARAM_TIME1(23, 1, 4, 600),
		.flags = FFB_MODE_RGB,
	},
};
static struct lcd_param time2[] = {

	{
		.value = 0x00003801,  ENC_PARAM_TIME2(0, 0, 1, 1, 1, 0x2),
		.flags = FFB_MODE_RGB,
	},
};

static struct faradayfb_mach_info ffb_mach_info = {

	.pixclock      = 35500000,
	.xres		   = 800,
	.yres		   = 600,
	.max_bpp	   = 24,

	.sync          = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.num_time0     = ARRAY_SIZE(time0),
	.time0         = time0,
	.num_time1     = ARRAY_SIZE(time1),
	.time1         = time1,
	.num_time2     = ARRAY_SIZE(time2),
	.time2         = time2,
	.num_control   = ARRAY_SIZE(control),
	.control       = control,
};

#endif /* CONFIG_CH7013A */
