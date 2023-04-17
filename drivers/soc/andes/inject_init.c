// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Andes Technology Corporation.
 */

#include <linux/io.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#ifdef CONFIG_PROC_FS

// FIXME: make this FPGA-depedent
#define INJECT_START    0x100000
#define INJECT_SIZE     (0x200000 - INJECT_START)

static void *inject;

static int inject_show(struct seq_file *m, void *v)
{
	int i, pos;
	unsigned char *p = (unsigned char *)inject;

	if (inject != NULL && p[0] == 0) {
		/* do nothing */
	} else if (inject != NULL) {
		for (i = 0, pos = 0; i < (INJECT_SIZE / PAGE_SIZE); i++, pos += PAGE_SIZE)
			seq_write(m, (void *)&p[pos], PAGE_SIZE);
	} else {
		seq_printf(m, "#!/bin/sh\necho not found: ioremap failed.\n");
	}
	return 0;
}

static int inject_open(struct inode *inode, struct file *file)
{
	return single_open(file, inject_show, NULL);
}

static const struct proc_ops inject_fops = {
	.proc_open	= inject_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int __init inject_init(void)
{
	unsigned char *p;

	inject = (void *)ioremap((phys_addr_t)INJECT_START, INJECT_SIZE);
	p = (unsigned char *)inject;

	if (p[0] != '#' && p[0] != 0x79)
		p[0] = 0;

	proc_create("inject", 0444, NULL, &inject_fops);
	return 0;
}
arch_initcall(inject_init);
#endif /* CONFIG_PROC_FS */
