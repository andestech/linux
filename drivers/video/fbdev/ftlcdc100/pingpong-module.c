// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#include <linux/io.h>

void *fmem_alloc(size_t size, dma_addr_t *dma_handle, bool has_dma_coherent)
{
	struct page *page;
	void *cpu_addr = NULL;

	size = PAGE_ALIGN(size);

	page = alloc_pages(GFP_DMA, get_order(size));
	if (!(page))
		goto no_page;

	*dma_handle = page_to_phys(page);

	if (!has_dma_coherent)
		cpu_addr = ioremap_wc(*dma_handle, size);
	else
		cpu_addr = ioremap(*dma_handle, size);

	if (cpu_addr) {
		do {
			SetPageReserved(page);
			page++;
		} while (size -= PAGE_SIZE);
	} else {
		__free_pages(page, get_order(size));
	}

no_page:
	return cpu_addr;
}

void fmem_free(size_t size, void *cpu_addr, dma_addr_t handle)
{
	struct page *page = pfn_to_page(handle >> PAGE_SHIFT);

	iounmap(cpu_addr);
	size = PAGE_ALIGN(size);

	do {
		ClearPageReserved(page);
		__free_page(page);
		page++;

	} while (size -= PAGE_SIZE);
}

static int faradayfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct faradayfb_info *fbi = info->par;
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
	int ret = -EINVAL;

	DEBUG(0, 1, "Enter\n");

	if (off < info->fix.smem_len) {

		off += fbi->screen_dma & PAGE_MASK;
		vma->vm_pgoff = off >> PAGE_SHIFT;
		if (!info->device->dma_coherent)
			vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

		ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				vma->vm_end - vma->vm_start, vma->vm_page_prot);
	} else {
		DEBUG(1, 1, "buffer mapping error !!\n");
	}

	DEBUG(0, 1, "Leave\n");

	return ret;
}

/*
 * Allocates the DRAM memory for the frame buffer.  This buffer is
 * remapped into a non-cached, non-buffered, memory region to
 * allow palette and pixel writes to occur without flushing the
 * cache.  Once this area is remapped, all virtual memory
 * access to the video memory should occur at the new region.
 */
static int __init faradayfb_map_video_memory(struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;

	bool dma_coherent = info->device->dma_coherent;

	/* The extra PAGE size is used for the palette. */
	fbi->map_size = PAGE_ALIGN(info->fix.smem_len) + PAGE_SIZE;
	fbi->map_cpu = fmem_alloc(fbi->map_size, &fbi->map_dma, dma_coherent);

	if (fbi->map_cpu) {

		memset(fbi->map_cpu, 0x1d, fbi->map_size);
		info->screen_base = fbi->map_cpu + PAGE_SIZE;
		fbi->screen_dma = fbi->map_dma + PAGE_SIZE;
		info->fix.smem_start = fbi->screen_dma;
	}

	return fbi->map_cpu ? 0 : -ENOMEM;
}

static inline void faradayfb_clean_screen(struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;
	int size = info->var.xres * info->var.yres;

	if (fbi->smode & FFB_MODE_YUV422) {
		memset(fbi->map_cpu, 16, size * info->var.bits_per_pixel / 8);
	} else if (fbi->smode & FFB_MODE_YUV420) {

		memset(fbi->map_cpu, 16, size);
		memset(fbi->map_cpu + PAGE_SIZE + ((size + 0xffff) & 0xffff0000), 128, size / 4);
		memset(fbi->map_cpu + PAGE_SIZE + ((size + 0xffff) & 0xffff0000) * 5 / 4, 128, size / 4);
	}
}

static inline void faradayfb_unmap_video_memory(struct fb_info *info)
{
	struct faradayfb_info *fbi = info->par;

	fmem_free(fbi->map_size, fbi->map_cpu, fbi->map_dma);
}

#define FRAME_SIZE_RGB(xres, yres, mbpp)	((xres) * (yres) * (mbpp) / 8)
#define FRAME_SIZE_YUV422(xres, yres, mbpp)	(((xres) * (yres) * (mbpp) / 8) * 2)
#define FRAME_SIZE_YUV420(xres, yres, mbpp)	(((((xres) * (yres) * (mbpp) / 8) + 0xffff) & 0xffff0000) * 3 / 2)

unsigned int nextPowerOf2(unsigned int n)
{
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n++;
	return n;
}

static inline u32 faradayfb_cal_frame_buf_size(struct faradayfb_info *fbi)
{
	u32 size_rgb	= FRAME_SIZE_RGB(fbi->xres, fbi->yres, fbi->max_bpp);
	u32 size_yuv422	= FRAME_SIZE_YUV422(fbi->xres, fbi->yres, 8);
	u32 size_yuv420	= FRAME_SIZE_YUV420(fbi->xres, fbi->yres, 8);
	u32 buf_size;
	u32 buf_size_power_of_2;

	/* PMA needs to allocate power of 2 memory size and the size should include the palette*/
	buf_size = max(size_rgb, max(size_yuv422, size_yuv420));
	buf_size_power_of_2 = nextPowerOf2(buf_size);
	if ((buf_size_power_of_2 - buf_size) < PAGE_SIZE)
		buf_size_power_of_2 = nextPowerOf2(buf_size + PAGE_SIZE);

	return buf_size_power_of_2;
}

#ifdef CONFIG_FTLCD_OSD
/************************************
 *	OSD
 ***********************************/
#define FOSD_SETPOS		0x46e1
#define FOSD_SETDIM		0x46e2
#define FOSD_SETSCAL		0x46e3
#define FLCD_SET_TRANSPARENT	0x46e4
#define FLCD_SET_STRING		0x46e5
#define FOSD_ON			0x46e6
#define FOSD_OFF		0x46e7
#define FRREG			0x46e8
#define FWREG			0x46e9

#define dig_alpha	((16 * 10) + (16 * 26) + (16 * 3))

struct fosd_string {

	unsigned int Str_row;
	unsigned int display_mode;
	unsigned int fg_color;
	unsigned int bg_color;
	unsigned char Str_OSD[30];
};

struct fosd_data {

	unsigned int HPos;
	unsigned int VPos;
	unsigned int HDim;
	unsigned int VDim;
	unsigned int transparent_level;
	unsigned int HScal;
	unsigned int VScal;
	struct fosd_string Str_Data[10];
};

unsigned int OSD_Font[] = {

	/*	0	*/
	0x00, 0x00, 0x00, 0x3e, 0x63, 0x67, 0x6f, 0x7b,
	0x73, 0x63, 0x63, 0x3e, 0x00, 0x00, 0x00, 0x00,

	/*	1	*/
	0x00, 0x00, 0x00, 0x0c, 0x1c, 0x3c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x3f, 0x00, 0x00, 0x00, 0x00,

	/*	2	*/
	0x00, 0x00, 0x00, 0x3e, 0x63, 0x03, 0x06, 0x0c,
	0x18, 0x30, 0x63, 0x7f, 0x00, 0x00, 0x00, 0x00,

	/*	3	*/
	0x00, 0x00, 0x00, 0x3E, 0x63, 0x03, 0x03, 0x1e,
	0x03, 0x03, 0x63, 0x3E, 0x00, 0x00, 0x00, 0x00,

	/*	4	*/
	0x00, 0x00, 0x00, 0x06, 0x0e, 0x1e, 0x36, 0x66,
	0x7f, 0x06, 0x06, 0x0f, 0x00, 0x00, 0x00, 0x00,

	/*	5	*/
	0x00, 0x00, 0x00, 0x7f, 0x60, 0x60, 0x60, 0x7e,
	0x03, 0x03, 0x63, 0x3e, 0x00, 0x00, 0x00, 0x00,

	/*	6	*/
	0x00, 0x00, 0x00, 0x1c, 0x30, 0x60, 0x60, 0x7e,
	0x63, 0x63, 0x63, 0x3e, 0x00, 0x00, 0x00, 0x00,

	/*	7	*/
	0x00, 0x00, 0x00, 0x7f, 0x63, 0x03, 0x06, 0x0c,
	0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00,

	/*	8	*/
	0x00, 0x00, 0x00, 0x3e, 0x63, 0x63, 0x63, 0x3e,
	0x63, 0x63, 0x63, 0x3e, 0x00, 0x00, 0x00, 0x00,

	/*	9	*/
	0x00, 0x00, 0x00, 0x3e, 0x63, 0x63, 0x63, 0x3f,
	0x03, 0x03, 0x06, 0x3c, 0x00, 0x00, 0x00, 0x00,

	/*	A	*/
	0x00, 0x00, 0x00, 0x08, 0x1c, 0x36, 0x63, 0x63,
	0x7f, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00,

	/*	B	*/
	0x00, 0x00, 0x00, 0x7E, 0x33, 0x33, 0x33, 0x3E,
	0x33, 0x33, 0x33, 0x7E, 0x00, 0x00, 0x00, 0x00,

	/*	C	*/
	0x00, 0x00, 0x00, 0x1E, 0x33, 0x61, 0x60, 0x60,
	0x60, 0x61, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00,

	/*	D	*/
	0x00, 0x00, 0x00, 0x7c, 0x36, 0x33, 0x33, 0x33,
	0x33, 0x33, 0x36, 0x7C, 0x00, 0x00, 0x00, 0x00,

	/*	E	*/
	0x00, 0x00, 0x00, 0x7f, 0x33, 0x31, 0x34, 0x3c,
	0x34, 0x31, 0x33, 0x7f, 0x00, 0x00, 0x00, 0x00,

	/*	F	*/
	0x00, 0x00, 0x00, 0x7f, 0x33, 0x31, 0x34, 0x3c,
	0x34, 0x30, 0x30, 0x78, 0x00, 0x00, 0x00, 0x00,

	/*	G	*/
	0x00, 0x00, 0x00, 0x1E, 0x33, 0x61, 0x60, 0x60,
	0x6F, 0x63, 0x36, 0x7C, 0x00, 0x00, 0x00, 0x00,

	/*	H	*/
	0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x64, 0x7f,
	0x63, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00,

	/*	I	*/
	0x00, 0x00, 0x00, 0x3c, 0x18, 0x18, 0x18, 0x18,
	0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00,

	/*	J	*/
	0x00, 0x00, 0x00, 0x0f, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x66, 0x66, 0x3c, 0x00, 0x00, 0x00, 0x00,

	/*	K	*/
	0x00, 0x00, 0x00, 0x73, 0x33, 0x36, 0x36, 0x3c,
	0x36, 0x36, 0x33, 0x73, 0x00, 0x00, 0x00, 0x00,

	/*	L	*/
	0x00, 0x00, 0x00, 0x78, 0x30, 0x30, 0x30, 0x30,
	0x30, 0x31, 0x33, 0x7f, 0x00, 0x00, 0x00, 0x00,

	/*	M	*/
	0x00, 0x00, 0x00, 0x63, 0x77, 0x7f, 0x6b, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00,

	/*	N	*/
	0x00, 0x00, 0x00, 0x63, 0x73, 0x7b, 0x7f, 0x6f,
	0x67, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00,

	/*	O	*/
	0x00, 0x00, 0x00, 0x1c, 0x36, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x36, 0x1c, 0x00, 0x00, 0x00, 0x00,

	/*	P	*/
	0x00, 0x00, 0x00, 0x7e, 0x33, 0x33, 0x33, 0x3e,
	0x30, 0x30, 0x30, 0x78, 0x00, 0x00, 0x00, 0x00,

	/*	Q	*/
	0x00, 0x00, 0x00, 0x3e, 0x63, 0x63, 0x63, 0x63,
	0x6b, 0x6f, 0x3e, 0x06, 0x07, 0x00, 0x00, 0x00,

	/*	R	*/
	0x00, 0x00, 0x00, 0x7e, 0x33, 0x33, 0x33, 0x3e,
	0x36, 0x33, 0x33, 0x73, 0x00, 0x00, 0x00, 0x00,

	/*	S	*/
	0x00, 0x00, 0x00, 0x3e, 0x63, 0x63, 0x30, 0x1c,
	0x06, 0x63, 0x63, 0x3e, 0x00, 0x00, 0x00, 0x00,

	/*	T	*/
	0x00, 0x00, 0x00, 0xFF, 0x99, 0x18, 0x18, 0x18,
	0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00,

	/*	U	*/
	0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x3e, 0x00, 0x00, 0x00, 0x00,

	/*	V	*/
	0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x36, 0x1c, 0x08, 0x00, 0x00, 0x00, 0x00,

	/*	W	*/
	0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x6b, 0x7f, 0x77, 0x63, 0x00, 0x00, 0x00, 0x00,

	/*	X	*/
	0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x36, 0x1c,
	0x36, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00,

	/*	Y	*/
	0x00, 0x00, 0x00, 0x63, 0x63, 0x63, 0x36, 0x1c,
	0x1c, 0x1c, 0x1c, 0x3e, 0x00, 0x00, 0x00, 0x00,

	/*	Z	*/
	0x00, 0x00, 0x00, 0x7f, 0x63, 0x46, 0x0c, 0x18,
	0x30, 0x61, 0x63, 0x7f, 0x00, 0x00, 0x00, 0x00,

	/*	space	*/
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	/*  =  */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00,
	0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	/*  ,  */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x18, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00,
};

void OSD_On(struct faradayfb_info *fbi)
{
	REG32(fbi->io_base + 0x34) &= 0xfffffffe;
	REG32(fbi->io_base + 0x34) |= 1;
}

void OSD_Off(struct faradayfb_info *fbi)
{
	REG32(fbi->io_base + 0x34) &= 0xfffffffe;
}

void OSD_Pos(struct faradayfb_info *fbi, int HPos, int VPos)
{
	REG32(fbi->io_base + 0x38) = (HPos << 10) | VPos;
}

void OSD_Dim(struct faradayfb_info *fbi, int HDim, int VDim)
{
	REG32(fbi->io_base + 0x34) &= 0x0000001f;
	REG32(fbi->io_base + 0x34) |= ((HDim << 10) | (VDim << 5));
}

void OSD_transparent(struct faradayfb_info *fbi, int level)
{
	REG32(fbi->io_base + 0x40) &= 0xffffff00;
	REG32(fbi->io_base + 0x40) |= (level << 6);
}

void OSD_fg_color(struct faradayfb_info *fbi, int pal0, int pal1, int pal2, int pal3)
{
	REG32(fbi->io_base + 0x3c) = (pal0) | (pal1 << 8) | (pal2 << 16) | (pal3 << 24);
}

void OSD_bg_color(struct faradayfb_info *fbi, int pal1, int pal2, int pal3)
{
	REG32(fbi->io_base + 0x40) &= 0x000000ff;
	REG32(fbi->io_base + 0x40) |= (pal1 << 8) | (pal2 << 16) | (pal3 << 24);
}

void OSD_Scal(struct faradayfb_info *fbi, int HScal, int VScal)
{
	REG32(fbi->io_base + 0x34) &= 0xffffffe1;
	REG32(fbi->io_base + 0x34) |= (HScal << 3) | (VScal << 1);
}

void OSD_putc(struct faradayfb_info *fbi, char c, int position, unsigned int value)
{
	switch (c) {
	case '0' ... '9':
		REG32(fbi->io_base + 0xc000 + position * 4) = ((c - '0') << 4) | value;
		break;

	case 'A' ... 'Z':
		REG32(fbi->io_base + 0xc000 + position * 4) = ((c - 'A' + 10) << 4) | value;
		break;

	case ' ':
		REG32(fbi->io_base + 0xc000 + position * 4) = (('Z' - 'A' + 10 + 1) << 4) | value;
		break;

	case '=':
		REG32(fbi->io_base + 0xc000 + position * 4) = (('Z' - 'A' + 10 + 2) << 4) | value;
		break;

	case ',':
		REG32(fbi->io_base + 0xc000 + position * 4) = (('Z' - 'A' + 10 + 3) << 4) | value;
		break;
	}
}

void OSD_puts(struct faradayfb_info *fbi, char *str, int position, unsigned int value)
{
	int i;

	for (i = 0; i < strlen(str); i++)
		OSD_putc(fbi, *(str + i), position + i, value);
}

void OSD_String(struct faradayfb_info *fbi, int row, int mode, char *str, int fg_color, int bg_color)
{
	int i, j, x, y;
	unsigned int value = fg_color | bg_color;

	/* 10 digit & 26 alphabet & ' ' & '=' & ',' */
	for (i = 0; i < dig_alpha; i++) {

		x = 0;
		y = OSD_Font[i];

		for (j = 0; j < 12; j++) { /* reorder */
			if (y & 1)
				x |= 1;
			y >>= 1;
			x <<= 1;
		}

		x >>= 1;
		REG32(fbi->io_base + 0x8000 + i * 4) = x;
	}

	OSD_puts(fbi, str, row, value);

	if (mode == 2) { /* YCbCr */

		OSD_fg_color(fbi, 0x57, 0x88, 0x3B, 0xFF);
		OSD_bg_color(fbi, 0x57, 0x88, 0x3B);
	} else {
		OSD_fg_color(fbi, 0x07, 0x38, 0xC0, 0xFF);
		OSD_bg_color(fbi, 0x07, 0x38, 0xc0);
	}
}
#endif	/* CONFIG_FTLCD_OSD */

struct andesIO {

	unsigned long Regaddr;
	unsigned long Regoffset;
	unsigned long Regvalue;
};

static int faradayfb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
#ifdef CONFIG_FTLCD_OSD
	struct faradayfb_info	*fbi = info->par;
	int			i;
	struct fosd_data	fosd;

	struct andesIO		IOAccess;
	unsigned long			*Regaccess;
#endif

	DEBUG(0, 1, "Enter\n");

	switch (cmd) {
#ifdef CONFIG_FTLCD_OSD
	case FRREG:

		if (copy_from_user(&IOAccess, (struct andesIO *)arg, sizeof(struct andesIO))) {

			ret = -EFAULT;
			break;
		}

		Regaccess = (unsigned long *)(((IOAccess.Regaddr >> 4) | 0xF0000000UL) + IOAccess.Regoffset);

		IOAccess.Regvalue = *(Regaccess);

		if (copy_to_user((struct andesIO *)arg, &IOAccess, sizeof(struct andesIO))) {
			ret = -EFAULT;
			break;
		}

		break;

	case FWREG:

		if (copy_from_user(&IOAccess, (struct andesIO *)arg, sizeof(struct andesIO))) {
			ret = -EFAULT;
			break;
		}

		Regaccess = (unsigned long *)(((IOAccess.Regaddr >> 4) | 0xF0000000UL) + IOAccess.Regoffset);
		*(Regaccess) = IOAccess.Regvalue;

		break;

	case FOSD_ON:

		DEBUG(1, 1, "FOSD_ON:\n");
		OSD_On(fbi);
		break;

	case FOSD_OFF:

		DEBUG(1, 1, "FOSD_OFF:\n");
		OSD_Off(fbi);
		break;

	case FOSD_SETPOS:

		DEBUG(1, 1, "FOSD_SETPOS:\n");

		if (copy_from_user(&fosd, (unsigned int *)arg, 2 * sizeof(unsigned int))) {

			ret = -EFAULT;
			break;
		}

		OSD_Pos(fbi, fosd.HPos, fosd.VPos);
		DEBUG(1, 1, "OSD_Pos = %d %d\n", fosd.HPos, fosd.VPos);
		break;

	case FOSD_SETDIM:

		DEBUG(1, 1, "FOSD_SETDIM:\n");

		if (copy_from_user(&fosd, (unsigned int *)arg, 4 * sizeof(unsigned int))) {

			ret = -EFAULT;
			break;
		}

		OSD_Dim(fbi, fosd.HDim, fosd.VDim);
		DEBUG(1, 1, "OSD_Dim = %d %d\n", fosd.HDim, fosd.VDim);
		break;

	case FOSD_SETSCAL:

		DEBUG(1, 1, "FOSD_SETSCAL:\n");

		if (copy_from_user(&fosd, (unsigned int *)arg, 7 * sizeof(unsigned int))) {
			ret = -EFAULT;
			break;
		}

		OSD_Scal(fbi, fosd.HScal, fosd.VScal);
		break;

	case FLCD_SET_TRANSPARENT:

		DEBUG(1, 1, "FLCD_SET_TRANSPARENT:\n");

		if (copy_from_user(&fosd, (unsigned int *)arg, 5 * sizeof(unsigned int))) {

			ret = -EFAULT;
			break;
		}

		OSD_transparent(fbi, fosd.transparent_level);
		DEBUG(1, 1, "OSD_transparent = %d\n", fosd.transparent_level);
		break;

	case FLCD_SET_STRING:

		DEBUG(1, 1, "FLCD_SET_STRING:\n");

		if (copy_from_user(&fosd, (unsigned int *)arg, sizeof(struct fosd_data))) {

			ret = -EFAULT;
			break;
		}

		for (i = 0; i < fosd.VDim; i++)

			OSD_String(fbi, fosd.Str_Data[i].Str_row,
					fosd.Str_Data[i].display_mode,
					fosd.Str_Data[i].Str_OSD,
					fosd.Str_Data[i].fg_color,
					fosd.Str_Data[i].bg_color);
		break;
#endif  /* CONFIG_FTLCD_OSD */

	default:

		DEBUG(1, 1, "IOCTL CMD(%08u) no define!\n", cmd);
		ret = -EFAULT;
		break;
	}

	DEBUG(0, 1, "Leave\n");
	return ret;
}
