/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __FARADAYFB_H
#define __FARADAYFB_H

#define FARADAYFB_MODULE_NAME  "faradayfb"
#define DEBUG(enabled, tagged, ...)		\
	do {		\
		if (enabled) {					\
			if (tagged)		\
				pr_err("[ %30s() ] ", __func__);		\
			pr_err(__VA_ARGS__);		\
		}		\
	} while (0)

#if defined(CONFIG_FFB_MODE_RGB)
#  define DEFAULT_COLOR  FFB_MODE_RGB
#elif defined(CONFIG_FFB_MODE_YUV422)
#  define DEFAULT_COLOR  FFB_MODE_YUV422
#elif defined(CONFIG_FFB_MODE_YUV420)
#  define DEFAULT_COLOR  FFB_MODE_YUV420
#endif

#if defined(CONFIG_FFB_MODE_8BPP)
#  define DEFAULT_BPP  FFB_MODE_8BPP
#elif defined(CONFIG_FFB_MODE_16BPP)
#  define DEFAULT_BPP  FFB_MODE_16BPP
#elif defined(CONFIG_FFB_MODE_24BPP)
#  define DEFAULT_BPP  FFB_MODE_24BPP
#endif

#define FFB_DEFAULT_MODE        (DEFAULT_COLOR | DEFAULT_BPP)

struct lcd_param {

	unsigned long value;
	unsigned long flags;
};

inline struct lcd_param *get_lcd_time(struct lcd_param *array, unsigned long num_array, unsigned long type)
{
	int i;

	for (i = 0; i < num_array; i++) {

		struct lcd_param *r = &array[i];

		if (r->flags & type & 0xff)
			return r;
	}

	return NULL;
}

inline struct lcd_param *get_lcd_ctrl(struct lcd_param *array, unsigned long num_array, unsigned long type)
{
	int i;

	for (i = 0; i < num_array; i++) {

		struct lcd_param *r = &array[i];

		if ((r->flags & type & 0xff) && (r->flags & type & 0xff00))
			return r;
	}

	return NULL;
}

enum { RGB_8, RGB_16, RGB_24, NR_RGB};

struct faradayfb_rgb {

	struct fb_bitfield	red;
	struct fb_bitfield	green;
	struct fb_bitfield	blue;
	struct fb_bitfield	transp;
};

/* This structure describes the machine which we are running on.  */
struct faradayfb_mach_info {

	unsigned long		num_time0;
	struct lcd_param *time0;

	unsigned long		num_time1;
	struct lcd_param *time1;

	unsigned long		num_time2;
	struct lcd_param *time2;

	unsigned long		num_control;
	struct lcd_param *control;

	unsigned long		pixclock;

	unsigned long		xres;
	unsigned long		yres;

	unsigned int		max_bpp;
	unsigned int		sync;

	unsigned int		cmap_greyscale:1,
				cmap_inverse:1,
				cmap_static:1,
				unused:29;
};

struct faradayfb_info {

	struct fb_info		*info;
	struct faradayfb_rgb	*rgb[NR_RGB];

	unsigned int		xres;
	unsigned int		yres;
	unsigned int		max_bpp;

	/*
	 * These are the addresses we mapped
	 * the framebuffer memory region to.
	 */
	dma_addr_t		map_dma;
	unsigned char *map_cpu;
	unsigned int		map_size;

	unsigned char *screen_cpu;
	dma_addr_t		screen_dma;
	u32           *palette_cpu;
	dma_addr_t		palette_dma;
	unsigned int		palette_size;

	unsigned int		cmap_inverse:1,
				cmap_static:1,
				unused:30;

	unsigned long		time0;
	unsigned long		time1;
	unsigned long		time2;
	unsigned long		control;
	unsigned long		int_mask;
	unsigned long		io_base;

	unsigned int		state;
	unsigned int		task_state;
	struct semaphore	ctrlr_sem;
	wait_queue_head_t	ctrlr_wait;
	struct work_struct	task;

	unsigned long		smode;
	unsigned long		frame420_size;
};

/*
 * These are the actions for set_ctrlr_state
 */
#define C_DISABLE		0
#define C_ENABLE		1
#define C_DISABLE_CLKCHANGE	2
#define C_ENABLE_CLKCHANGE	3
#define C_REENABLE		4
#define C_DISABLE_PM		5
#define C_ENABLE_PM		6
#define C_STARTUP		7

#define FARADAY_LCDTIME0_GET_HBP(x) ((((x) >> 24) & 0xFF) + 1)
#define FARADAY_LCDTIME0_GET_HFP(x) ((((x) >> 16) & 0xFF) + 1)
#define FARADAY_LCDTIME0_GET_HW(x)  ((((x) >>  8) & 0xFF) + 1)
#define FARADAY_LCDTIME1_GET_VBP(x) ((((x) >> 24) & 0xFF))
#define FARADAY_LCDTIME1_GET_VFP(x) ((((x) >> 16) & 0xFF))
#define FARADAY_LCDTIME1_GET_VW(x)  ((((x) >>  8) & 0xFF) + 1)

#define FARADAY_LCDTIME0_HBP(x)     ((((x) - 1) & 0xFF) << 24)
#define FARADAY_LCDTIME0_HFP(x)     ((((x) - 1) & 0xFF) << 16)
#define FARADAY_LCDTIME0_HW(x)      ((((x) - 1) & 0xFF) <<  8)
#define FARADAY_LCDTIME0_PL(x)      (((((x) >> 4) - 1) & 0x3F) <<  2)
#define FARADAY_LCDTIME1_VBP(x)     ((((x)) & 0xFF) << 24)
#define FARADAY_LCDTIME1_VFP(x)     ((((x)) & 0xFF) << 16)
#define FARADAY_LCDTIME1_VW(x)      ((((x) - 1) & 0xFF) <<  8)
#define FARADAY_LCDTIME1_LF(x)      ((((x) - 1) & 0x3FF))

#define FFB_MODE_RGB	0x00000001
#define FFB_MODE_YUV420	0x00000002
#define FFB_MODE_YUV422	0x00000004

#define FFB_MODE_8BPP	0x00000100
#define FFB_MODE_16BPP	0x00000200
#define FFB_MODE_24BPP	0x00000400

/* Minimum X and Y resolutions */
#define MIN_XRES	64
#define MIN_YRES	64

typedef struct LCDTag {

	u32 Timing0;			/* 0x00        */
	u32 Timing1;			/* 0x04        */
	u32 Timing2;			/* 0x08        */
	u32 Timing3;			/* 0x0C        */
	u32 UPBase;			/* 0x10        */
	u32 LPBase;			/* 0x14        */
	u32 INTREnable;			/* 0x18        */
	u32 Control;			/* 0x1C        */
	u32 Status;			/* 0x20        */
	u32 Interrupt;			/* 0x24        */
	u32 UPCurr;			/* 0x28        */
	u32 LPCurr;			/* 0x2C        */
	u32 Reserved1[5];		/* 0x30~0x40   */
	u32 GPIO;			/* 0x44        */
	u32 Reserved2[0x74 - 6];	/* 0x48~0x1FC  */

	u32 Palette[0x80];		/* 0x200~0x3FC */
	u32 TestReg[0x100];		/* 0x400~0x7FC */

} LCD_Register;

#endif /* __FARADAYFB_H */

