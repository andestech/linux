// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#include <linux/module.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include "faradayfb.h"
#include "lcd-info.c"
#include "pingpong-module.c"

#define REG32(a)        (*(unsigned int *)(a))
static resource_size_t	lcd_base;

static u32 faradayfb_pseudo_palette[32];
static inline void faradayfb_lcd_power(struct fb_info *info, int on)
{
	struct faradayfb_info *fbi = info->par;
	LCD_Register *plcd = (LCD_Register *)fbi->io_base;

	if (on)
		fbi->control |= (1UL << 11);
	else
		fbi->control &= ~(1UL << 11);

	plcd->Control = fbi->control;
}

static void faradayfb_setup_gpio(struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;
	LCD_Register *plcd = (LCD_Register *)fbi->io_base;

	plcd->GPIO = 0x010000;
}

static void faradayfb_enable_controller(struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;
	LCD_Register *plcd = (LCD_Register *)fbi->io_base;

	plcd->Timing0 = fbi->time0;
	plcd->Timing1 = fbi->time1;
	plcd->Timing2 = fbi->time2;
	plcd->Control = fbi->control & ~0x01;
	plcd->UPBase  = fbi->screen_dma | fbi->frame420_size;
	plcd->Control |= 0x01;
	DEBUG(0, 1, "Time0   = 0x%08x\n", plcd->Timing0);
	DEBUG(0, 1, "Time1   = 0x%08x\n", plcd->Timing1);
	DEBUG(0, 1, "Time2   = 0x%08x\n", plcd->Timing2);
	DEBUG(0, 1, "Control = 0x%08x\n", plcd->Control);
	DEBUG(0, 1, "UPBase  = 0x%08x\n", plcd->UPBase);
}

static void faradayfb_disable_controller(struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;
	LCD_Register *plcd = (LCD_Register *)fbi->io_base;
	DECLARE_WAITQUEUE(wait, current);

	set_current_state(TASK_UNINTERRUPTIBLE);
	add_wait_queue(&fbi->ctrlr_wait, &wait);
	fbi->control &= ~0x0001;
	plcd->Control = fbi->control;
	schedule_timeout(20 * HZ / 1000);
	remove_wait_queue(&fbi->ctrlr_wait, &wait);
}

static void faradayfb_enable_int(struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;
	LCD_Register *plcd = (LCD_Register *)fbi->io_base;

	plcd->INTREnable = fbi->int_mask;
}

static void faradayfb_disable_int(struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;
	LCD_Register *plcd = (LCD_Register *)fbi->io_base;

	fbi->int_mask    = plcd->INTREnable;
	plcd->INTREnable = 0;
	plcd->Status     = 0x1e;
}

static struct faradayfb_rgb def_rgb_8 = {

	.red    = { .offset = 0, .length = 4 },
	.green  = { .offset = 0, .length = 4 },
	.blue   = { .offset = 0, .length = 4 },
	.transp = { .offset = 0, .length = 0 },
};

static struct faradayfb_rgb def_rgb_16 = {

	.red    = { .offset = 11, .length = 5, .msb_right = 0 },
	.green  = { .offset = 5,  .length = 6, .msb_right = 0 },
	.blue   = { .offset = 0,  .length = 5, .msb_right = 0 },
	.transp = { .offset = 15, .length = 0, .msb_right = 0 },
};

static struct faradayfb_rgb def_rgb_24 = {

	.red    = { .offset = 16, .length = 8, .msb_right = 0 },
	.green  = { .offset = 8,  .length = 8, .msb_right = 0 },
	.blue   = { .offset = 0,  .length = 8, .msb_right = 0 },
	.transp = { .offset = 0,  .length = 0, .msb_right = 0 },
};

static inline void faradayfb_schedule_work(struct fb_info *info, unsigned int state)
{
	struct faradayfb_info *fbi = info->par;
	unsigned long flags;

	local_irq_save(flags);

	/*
	 * We need to handle two requests being made at the same time.
	 * There are two important cases:
	 *  1. When we are changing VT (C_REENABLE) while unblanking (C_ENABLE)
	 *     We must perform the unblanking, which will do our REENABLE for us.
	 *  2. When we are blanking, but immediately unblank before we have
	 *     blanked.  We do the "REENABLE" thing here as well, just to be sure.
	 */
	if (fbi->task_state == C_ENABLE && state == C_REENABLE)
		state = (unsigned int) -1;

	if (fbi->task_state == C_DISABLE && state == C_ENABLE)
		state = C_REENABLE;

	if (state != (unsigned int) -1) {

		fbi->task_state = state;
		schedule_work(&fbi->task);
	}

	local_irq_restore(flags);
}

static int faradayfb_setpalettereg(unsigned int regno, unsigned int red,
		unsigned int green, unsigned int blue, unsigned int trans,
		struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;

	if (regno < fbi->palette_size) {

		fbi->palette_cpu[regno] = ((red >> 0) & (0x1fUL << 11)) |
			((green >> 5) & (0x3F << 5)) |
			((blue >> 11) & (0x1f << 0));

		return 0;
	}

	return 1;
}

static int faradayfb_setcolreg(unsigned int regno, unsigned int red, unsigned int green,
		unsigned int blue, unsigned int trans, struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;
	int ret = 1;
	/*
	 * If inverse mode was selected, invert all the colours
	 * rather than the register number.  The register number
	 * is what you poke into the framebuffer to produce the
	 * colour you requested.
	 */
	if (fbi->cmap_inverse) {

		red   = 0xffff - red;
		green = 0xffff - green;
		blue  = 0xffff - blue;
	}

	/*
	 * If greyscale is true, then we convert the RGB value
	 * to greyscale no mater what visual we are using.
	 */
	if (info->var.grayscale)
		red = green = blue = (19595 * red + 38470 * green + 7471 * blue) >> 16;

	switch (info->fix.visual) {

	case FB_VISUAL_TRUECOLOR:
		/*
		 * 12 or 16-bit True Colour.  We encode the RGB value
		 * according to the RGB bitfield information.
		 */
		if (regno < 16) {
			u32 col;

			red	>>= (16 - info->var.red.length);
			green	>>= (16 - info->var.green.length);
			blue	>>= (16 - info->var.blue.length);
			col = (red << info->var.red.offset) |
				(green << info->var.green.offset) |
				(blue << info->var.blue.offset);

			switch (info->var.bits_per_pixel) {

				/* is the following code correct?? */
			case 16:
			case 32:
				((u32 *)(info->pseudo_palette))[regno] = col;
				ret = 0;
				break;
			}
		}
		break;

	case FB_VISUAL_STATIC_PSEUDOCOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		if (fbi->smode == FFB_MODE_RGB)
			ret = faradayfb_setpalettereg(regno, red, green, blue, trans, info);
		break;
	}

	return ret;
}

/*
 * Round up in the following order: bits_per_pixel, xres,
 * yres, xres_virtual, yres_virtual, xoffset, yoffset, grayscale,
 * bitfields, horizontal timing, vertical timing.
 */
static int faradayfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;
	int rgbidx;

	var->xres = min_t(unsigned int, var->xres, MIN_XRES);
	var->yres = min_t(unsigned int, var->yres, MIN_YRES);
	var->xres = max(var->xres, fbi->xres);
	var->yres = max(var->yres, fbi->yres);
	var->xres_virtual = max(var->xres_virtual, var->xres);
	var->yres_virtual = max(var->yres_virtual, var->yres);

	DEBUG(0, 1, "var->bits_per_pixel = %d\n", var->bits_per_pixel);

	switch (var->bits_per_pixel) {

	case 1:
	case 2:
	case 4:
	case 8:
		rgbidx = RGB_8;
		break;

	case 16:
		rgbidx = RGB_16;
		break;

	case 32:
		rgbidx = RGB_24;
		break;

	default:
		return -EINVAL;
	}

	/*
	 * Copy the RGB parameters for this display
	 * from the machine specific parameters.
	 */
	var->red	= fbi->rgb[rgbidx]->red;
	var->green	= fbi->rgb[rgbidx]->green;
	var->blue	= fbi->rgb[rgbidx]->blue;
	var->transp	= fbi->rgb[rgbidx]->transp;

	DEBUG(0, 1, "RGBT length = %d:%d:%d:%d\n",
			var->red.length, var->green.length, var->blue.length, var->transp.length);

	DEBUG(0, 1, "RGBT offset = %d:%d:%d:%d\n",
			var->red.offset, var->green.offset, var->blue.offset, var->transp.offset);

	DEBUG(0, 1, "Leave\n");

	return 0;
}

/*
 * Configures LCD Controller based on entries in var parameter.  Settings are
 * only written to the controller if changes were made.
 */
static int faradayfb_activate_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;
	LCD_Register *plcd = (LCD_Register *)fbi->io_base;

	DEBUG(0, 1, "var: xres=%d hslen=%d lm=%d rm=%d\n", var->xres, var->hsync_len, var->left_margin, var->right_margin);
	DEBUG(0, 1, "var: yres=%d vslen=%d um=%d bm=%d\n", var->yres, var->vsync_len, var->upper_margin, var->lower_margin);

	fbi->time0 = FARADAY_LCDTIME0_HFP(var->left_margin)
		| FARADAY_LCDTIME0_HBP(var->right_margin)
		| FARADAY_LCDTIME0_HW(var->hsync_len)
		| FARADAY_LCDTIME0_PL(var->xres);

	fbi->time1 = FARADAY_LCDTIME1_VBP(var->upper_margin)
		| FARADAY_LCDTIME1_VFP(var->lower_margin)
		| FARADAY_LCDTIME1_VW(var->vsync_len)
		| FARADAY_LCDTIME1_LF(var->yres);

	if ((plcd->Timing0 != fbi->time0)
			|| (plcd->Timing1 != fbi->time1)
			|| (plcd->Timing2 != fbi->time2)
			|| (plcd->Control != fbi->control)) {

		faradayfb_schedule_work(info, C_REENABLE);
	}

	return 0;
}

/* Set the user defined part of the display for the specified console */
static int faradayfb_set_par(struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;
	struct fb_var_screeninfo *var = &info->var;
	unsigned long palette_mem_size;

	if (var->bits_per_pixel == 16 || var->bits_per_pixel == 32)
		info->fix.visual = FB_VISUAL_TRUECOLOR;
	else if (!fbi->cmap_static)
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	else {
		/*
		 * Some people have weird ideas about wanting static
		 * pseudocolor maps.  I suspect their user space
		 * applications are broken.
		 */
		info->fix.visual = FB_VISUAL_STATIC_PSEUDOCOLOR;
	}

	info->fix.line_length = var->xres_virtual * var->bits_per_pixel / 8;

	fbi->palette_size = var->bits_per_pixel == 8 ? 256 : 16;

	palette_mem_size = fbi->palette_size * sizeof(u32);

	DEBUG(0, 1, "info->fix.line_length = %d\n", info->fix.line_length);
	DEBUG(0, 1, "palette_mem_size = 0x%08lx\n", (unsigned long) palette_mem_size);

	fbi->palette_cpu = (u32 *)(fbi->map_cpu + PAGE_SIZE - palette_mem_size);

	fbi->palette_dma = fbi->map_dma + PAGE_SIZE - palette_mem_size;

	/* Set (any) board control register to handle new color depth */
	faradayfb_activate_var(var, info);

	DEBUG(0, 1, "Leave\n");

	return 0;
}

static irqreturn_t faradayfb_handle_irq(int irq, void *dev_id)
{
	struct fb_info *info = (struct fb_info *)dev_id;
	struct faradayfb_info *fbi = info->par;
	LCD_Register *plcd = (LCD_Register *)fbi->io_base;
	u32 status = plcd->Interrupt;

	pr_err("%s, irq %d\n", __func__, irq);

	DEBUG(0, 1, "status 0x%x\n", status);

	return IRQ_HANDLED;
}

/*
 * This function must be called from task context only, since it will
 * sleep when disabling the LCD controller, or if we get two contending
 * processes trying to alter state.
 */
static void set_ctrlr_state(struct fb_info *info, unsigned int state)
{
	struct faradayfb_info *fbi = info->par;
	unsigned int old_state;

	down(&fbi->ctrlr_sem);

	old_state = fbi->state;

	/* Hack around fbcon initialisation.  */
	if (old_state == C_STARTUP && state == C_REENABLE)
		state = C_ENABLE;

	switch (state) {
	case C_DISABLE_PM:
		/* Disable controller */
		if (old_state != C_DISABLE) {

			fbi->state = state;
			faradayfb_disable_int(info);
			faradayfb_lcd_power(info, 0);
			faradayfb_disable_controller(info);
		}
		break;

	case C_DISABLE:
		/* Disable controller */
		if (old_state != C_DISABLE) {

			fbi->state = state;
			faradayfb_disable_int(info);
			faradayfb_lcd_power(info, 0);
			//				faradayfb_disable_controller(info);
		}
		break;

	case C_REENABLE:
		/*
		 * Re-enable the controller only if it was already
		 * enabled.  This is so we reprogram the control
		 * registers.
		 */
		if (old_state == C_ENABLE) {

			faradayfb_disable_int(info);
			faradayfb_lcd_power(info, 0);
			faradayfb_disable_controller(info);

			faradayfb_setup_gpio(info);
			faradayfb_lcd_power(info, 1);
			faradayfb_enable_controller(info);
			faradayfb_enable_int(info);
		}
		break;

	case C_ENABLE_PM:
		/*
		 * Re-enable the controller after PM.  This is not
		 * perfect - think about the case where we were doing
		 * a clock change, and we suspended half-way through.
		 */
		if (old_state != C_DISABLE_PM) {
			fbi->state = C_ENABLE;
			faradayfb_setup_gpio(info);
			faradayfb_lcd_power(info, 1);
			faradayfb_enable_controller(info);
			faradayfb_enable_int(info);
		}
		break;
		/* fall through */

	case C_ENABLE:
		/*
		 * Power up the LCD screen, enable controller, and
		 * turn on the backlight.
		 */
		if (old_state != C_ENABLE) {

			fbi->state = C_ENABLE;
			faradayfb_setup_gpio(info);
			faradayfb_lcd_power(info, 1);
			faradayfb_enable_controller(info);
			faradayfb_enable_int(info);
		}
		break;
	}
	up(&fbi->ctrlr_sem);
}

/*
 * Blank the display by setting all palette values to zero.  Note, the
 * 12 and 16 bpp modes don't really use the palette, so this will not
 * blank the display in all modes.
 */
static int faradayfb_blank(int blank, struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;
	int i;

	switch (blank) {
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_NORMAL:

		if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR ||
				info->fix.visual == FB_VISUAL_STATIC_PSEUDOCOLOR) {

			for (i = 0; i < fbi->palette_size; i++)
				faradayfb_setpalettereg(i, 0, 0, 0, 0, info);
		}

		faradayfb_schedule_work(info, C_DISABLE);

		break;

	case FB_BLANK_UNBLANK:

		if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR ||
				info->fix.visual == FB_VISUAL_STATIC_PSEUDOCOLOR)
			fb_set_cmap(&info->cmap, info);

		faradayfb_schedule_work(info, C_ENABLE);

		break;
	}

	return 0;
}

/*
 * Our LCD controller task (which is called when we blank or unblank)
 * via keventd.
 */
static void faradayfb_task(struct work_struct *dummy)
{
	struct faradayfb_info *fbi = container_of(dummy, struct faradayfb_info, task);
	struct fb_info *info = fbi->info;
	unsigned int state;

	state = xchg(&fbi->task_state, -1);
	set_ctrlr_state(info, state);
}

/* Fake monspecs to fill in fbinfo structure */
static struct fb_monspecs monspecs = {

	.hfmin	= 30000,
	.hfmax	= 70000,
	.vfmin	= 50,
	.vfmax	= 65,
};

static int ffb_get_mach_param(struct faradayfb_mach_info *inf, struct fb_info *info, unsigned long smode)
{
	struct faradayfb_info *fbi = info->par;
	struct lcd_param *tparam;
	unsigned long bpp_mode;

	tparam = get_lcd_time(inf->time0, inf->num_time0, smode);
	if (!(tparam))
		return -1;

	fbi->time0 = tparam->value;
	tparam = get_lcd_time(inf->time1, inf->num_time1, smode);
	if (!(tparam))
		return -1;

	fbi->time1 = tparam->value;
	tparam = get_lcd_time(inf->time2, inf->num_time2, smode);
	if (!(tparam))
		return -1;

	fbi->time2 = tparam->value;

	switch (info->var.bits_per_pixel) {
	case 1:
	case 2:
	case 4:
	case 8:
		bpp_mode = FFB_MODE_8BPP;
		break;

	case 16:
		bpp_mode = FFB_MODE_16BPP;
		break;

	case 32:
		bpp_mode = FFB_MODE_24BPP;
		break;

	default:
		DEBUG(1, 1, "Unsupported BPP, set default BPP to 16\n");
		bpp_mode = FFB_MODE_16BPP;
		break;
	}

	tparam = get_lcd_ctrl(inf->control, inf->num_control, smode | bpp_mode);

	if (!(tparam))
		return -1;

	fbi->control = tparam->value;

	fbi->xres = inf->xres;
	fbi->yres = inf->yres;

	return 0;
}

static int faradayfb_set_var(struct fb_info *info, struct faradayfb_mach_info *inf, unsigned long type)
{
	struct faradayfb_info *fbi = info->par;
	unsigned long t_smode = 0;

	if (!type)
		t_smode = fbi->smode;
	else
		t_smode = type;

	if (ffb_get_mach_param(inf, info, t_smode)) {

		DEBUG(1, 1, "Not support mode(%lx)\n", t_smode);
		return -1;
	}

	info->var.xres		= fbi->xres;
	info->var.xres_virtual	= fbi->xres;
	info->var.yres		= fbi->yres;
	info->var.yres_virtual	= fbi->yres;

	info->var.upper_margin	= FARADAY_LCDTIME1_GET_VBP(fbi->time1);
	info->var.lower_margin	= FARADAY_LCDTIME1_GET_VFP(fbi->time1);
	info->var.vsync_len	= FARADAY_LCDTIME1_GET_VW(fbi->time1);
	info->var.left_margin	= FARADAY_LCDTIME0_GET_HFP(fbi->time0);
	info->var.right_margin	= FARADAY_LCDTIME0_GET_HBP(fbi->time0);
	info->var.hsync_len	= FARADAY_LCDTIME0_GET_HW(fbi->time0);

	fbi->int_mask = 0;

	if (t_smode & FFB_MODE_YUV420)
		fbi->frame420_size	= (((info->var.xres * info->var.yres + 0xffff) & 0xffff0000) >> 16) << 2;
	else
		fbi->frame420_size	= 0;

	return 0;
}

static const struct fb_ops faradayfb_ops = {

	.owner		= THIS_MODULE,
	.fb_check_var	= faradayfb_check_var,
	.fb_set_par	= faradayfb_set_par,
	.fb_setcolreg	= faradayfb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_blank	= faradayfb_blank,
	.fb_mmap	= faradayfb_mmap,
	.fb_ioctl	= faradayfb_ioctl,
};

static struct fb_info *faradayfb_init_fbinfo(struct device *dev)
{
	struct faradayfb_mach_info *inf;
	struct fb_info *info;
	struct faradayfb_info *fbi;

	info = framebuffer_alloc(sizeof(struct faradayfb_info), dev);
	if (!(info))
		return NULL;

	fbi = info->par;
	fbi->info = info;
	fbi->io_base		= lcd_base;
	strcpy(info->fix.id, FARADAYFB_MODULE_NAME);

	info->fix.type		= FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux	= 0;
	info->fix.xpanstep	= 0;
	info->fix.ypanstep	= 0;
	info->fix.ywrapstep	= 0;
	info->fix.accel		= FB_ACCEL_NONE;

	info->var.nonstd	= 0;
	info->var.activate	= FB_ACTIVATE_NOW;
	info->var.height	= -1;
	info->var.width		= -1;
	info->var.accel_flags	= 0;
	info->var.vmode		= FB_VMODE_NONINTERLACED;

	info->fbops		= &faradayfb_ops;
	info->flags		= FBINFO_DEFAULT;
	info->monspecs		= monspecs;
	info->pseudo_palette	= &faradayfb_pseudo_palette;

	fbi->rgb[RGB_8]		= &def_rgb_8;
	fbi->rgb[RGB_16]	= &def_rgb_16;
	fbi->rgb[RGB_24]	= &def_rgb_24;

	inf = (struct faradayfb_mach_info *)dev->platform_data;

	/*
	 * People just don't seem to get this.  We don't support
	 * anything but correct entries now, so panic if someone
	 * does something stupid.
	 */

	fbi->xres			= inf->xres;
	fbi->yres			= inf->yres;
	fbi->max_bpp			= 32;
#if defined(CONFIG_FFB_MODE_8BPP)
	info->var.bits_per_pixel	= min((u32)8, inf->max_bpp);
#elif defined(CONFIG_FFB_MODE_16BPP)
	info->var.bits_per_pixel	= min((u32)16, inf->max_bpp);
#elif defined(CONFIG_FFB_MODE_24BPP)
	info->var.bits_per_pixel	= min((u32)32, inf->max_bpp);
	info->var.bits_per_pixel	= 32;
#endif

	info->var.pixclock		= inf->pixclock;
	info->var.sync			= inf->sync;
	info->var.grayscale		= inf->cmap_greyscale;
	fbi->cmap_inverse		= inf->cmap_inverse;
	fbi->cmap_static		= inf->cmap_static;
	fbi->smode			= FFB_DEFAULT_MODE;
	if (faradayfb_set_var(info, inf, 0))
		goto err;
	fbi->state			= C_STARTUP;
	fbi->task_state			= (unsigned char) -1;

	/* The size of the palette should not be included in info->fix.smem_len. */
	info->fix.smem_len		= faradayfb_cal_frame_buf_size(fbi) - PAGE_SIZE;
	init_waitqueue_head(&fbi->ctrlr_wait);
	INIT_WORK(&fbi->task, faradayfb_task);
	sema_init(&fbi->ctrlr_sem, 1);
	return info;
err:
	kfree(fbi);
	kfree(info);

	return NULL;
}

static int faradayfb_probe(struct platform_device *pdev)
{
	struct fb_info *info = NULL;
	struct faradayfb_info *fbi;
	int irq = -EINVAL;
	int ret = -ENODEV;
	struct resource	*io;

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (io == NULL) {
		dev_err(&pdev->dev, "Failed to get memory resource\n");
		goto err_exit;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev, "Failed to get irq\n");
		goto err_exit;
	}

	lcd_base = (resource_size_t)(unsigned long)devm_ioremap_resource(&pdev->dev, io);
	pdev->dev.platform_data	= &ffb_mach_info;

	info = faradayfb_init_fbinfo(&pdev->dev);
	if (info == NULL) {
		dev_err(&pdev->dev, "Failed to init info\n");
		goto err_exit;
	}

	fbi = info->par;

	/* Initialize video memory */
	ret = faradayfb_map_video_memory(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to map video memory\n");
		goto err_free_mem;
	}

	ret = -EINVAL;
	if (request_irq(irq, faradayfb_handle_irq, 0, "LCD", info)) {
		dev_err(&pdev->dev, "Failed to request irq\n");
		goto err_free_map;
	}

	/*
	 * This makes sure that our colour bitfield
	 * descriptors are correctly initialised.
	 */
	faradayfb_check_var(&info->var, info);
	faradayfb_set_par(info);
	dev_set_drvdata(&pdev->dev, info);
	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register framebuffer\n");
		goto err_free_irq;
	}
	faradayfb_clean_screen(info);

	return 0;

err_free_irq:
	dev_set_drvdata(&pdev->dev, NULL);
	free_irq(irq, info);
err_free_map:
	faradayfb_unmap_video_memory(info);
err_free_mem:
	kfree(info->par);
	kfree(info);
err_exit:

	return ret;
}

/* Called when the device is being detached from the driver */
static int faradayfb_remove(struct platform_device *pdev)
{
	struct fb_info  *info = platform_get_drvdata(pdev);
	int irq;

	set_ctrlr_state(info, C_DISABLE);

	irq = platform_get_irq(pdev, 0);
	free_irq(irq, info);

	unregister_framebuffer(info);
	faradayfb_unmap_video_memory(info);
	dev_set_drvdata(&pdev->dev, NULL);
	kfree(info->par);
	kfree(info);

	return 0;
}

#ifdef CONFIG_PM
static int faradayfb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct fb_info  *info;

	info = platform_get_drvdata(pdev);

	set_ctrlr_state(info, C_DISABLE_PM);

	return 0;
}

static int faradayfb_resume(struct platform_device *pdev)
{
	struct fb_info  *info;

	info = platform_get_drvdata(pdev);

	set_ctrlr_state(info, C_ENABLE_PM);

	return 0;
}
#endif /* CONFIG_PM */

static const struct of_device_id atflcd_of_match[] = {
	{ .compatible = "andestech,atflcdc100", },
	{},
};

MODULE_DEVICE_TABLE(of, atflcd_of_match);
static struct platform_driver atflcd_of_driver = {
	.probe = faradayfb_probe,
	.remove = faradayfb_remove,
	.driver = {
		.name = "faraday-lcd",
		.owner = THIS_MODULE,
		.of_match_table = atflcd_of_match,
	},
#ifdef CONFIG_PM
	.suspend = faradayfb_suspend,
	.resume = faradayfb_resume,
#endif /* CONFIG_PM */
};

module_platform_driver(atflcd_of_driver);
MODULE_DESCRIPTION("Faraday LCD driver");
MODULE_AUTHOR("Francis Huang <francish@faraday-tech.com>");
MODULE_LICENSE("GPL");
