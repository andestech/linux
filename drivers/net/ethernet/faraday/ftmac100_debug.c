// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2009 Andes Technology Corporation
 *  Copyright (C) 2019 Andes Technology Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>

#define INPUTLEN 32

/* 0: DISABLE
 * 1: ENABLE
 */
unsigned int FTMAC100_DEBUG;

/* 0: DISABLE
 * 1: INCR4
 * 2: INCR8
 * 4: INCR16
 */
unsigned int FTMAC100_INCR;

#define ENTRY_NUMBER 2

struct entry_struct {
	char *name;
	int perm;
	const struct proc_ops *p_ops;
};

static struct proc_dir_entry *proc_ftmac100_debug;

#define DEBUG(enable, tagged, ...)                               \
	do {                                                     \
		if (enable) {                                    \
			if (tagged)                              \
				printk("[ %30s() ] ", __func__); \
			printk(__VA_ARGS__);                     \
		}                                                \
	} while (0)

static int debug;
module_param(debug, int, 0);

static ssize_t ftmac100_proc_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	int ret = 0;
	char buf[128] = {0};

	if (!strncmp(file->f_path.dentry->d_name.name, "debug", 5)) {
		ret = sprintf(buf, "FTMAC100 debug info: %s\n",
			      (FTMAC100_DEBUG) ? "Enabled" : "Disabled");
	} else if (!strncmp(file->f_path.dentry->d_name.name, "incr", 4)) {
		switch (FTMAC100_INCR) {
		case 0:
			ret = sprintf(buf, "FTMAC100 INCR: %s\n", "Disabled");
			break;
		case 1:
			ret = sprintf(buf, "FTMAC100 INCR: %d (INCR4)\n",
				      FTMAC100_INCR);
			break;
		case 2:
			ret = sprintf(buf, "FTMAC100 INCR: %d (INCR8)\n",
				      FTMAC100_INCR);
			break;
		case 4:
			ret = sprintf(buf, "FTMAC100 INCR: %d (INCR16)\n",
				      FTMAC100_INCR);
			break;
		}
	} else
		return -EFAULT;

	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static ssize_t ftmac100_proc_write(struct file *file, const char __user *buffer,
				   size_t count, loff_t *ppos)
{
	unsigned long en;
	char inbuf[INPUTLEN];

	if (count > INPUTLEN - 1)
		count = INPUTLEN - 1;

	if (copy_from_user(inbuf, buffer, count))
		return -EFAULT;

	inbuf[count] = '\0';

	if (!sscanf(inbuf, "%lu", &en))
		return -EFAULT;

	if (!strncmp(file->f_path.dentry->d_name.name, "debug", 5)) {
		FTMAC100_DEBUG = en;
	} else if (!strncmp(file->f_path.dentry->d_name.name, "incr", 4)) {
		switch (en) {
		case 0:
		case 1:
		case 2:
		case 4:
			FTMAC100_INCR = en;
			printk("Please restart eth0 interface to apply INCR\n");
			printk("    ~# ifconfig eth0 down\n");
			printk("    ~# udhcpc\n");
			break;
		default:
			printk("INCR value must be [0/1/2/4]\n");
			printk("    0: INCR disabled\n");
			printk("    1: INCR4\n");
			printk("    2: INCR8\n");
			printk("    4: INCR16\n");
		}
	} else {
		return -EFAULT;
	}

	return count;
}

static const struct proc_ops en_fops = {
	.proc_open = simple_open,
	.proc_read = ftmac100_proc_read,
	.proc_write = ftmac100_proc_write,
};

static void create_seq_entry(struct entry_struct *e, mode_t mode,
			     struct proc_dir_entry *parent)
{
	struct proc_dir_entry *entry =
		proc_create(e->name, mode, parent, e->p_ops);

	if (!entry)
		printk(KERN_ERR "invalid %s register.\n", e->name);
}

static void install_proc_table(struct entry_struct *table)
{
	int i;

	for (i = 0; i < ENTRY_NUMBER; table++, i++)
		create_seq_entry(table, table->perm, proc_ftmac100_debug);
}

static void remove_proc_table(struct entry_struct *table)
{
	int i;

	for (i = 0; i < ENTRY_NUMBER; table++, i++)
		remove_proc_entry(table->name, proc_ftmac100_debug);
}

struct entry_struct proc_table_ftmac100_debug[ENTRY_NUMBER] = {
	{"debug", 0644, &en_fops},
	{"incr", 0644, &en_fops},
};

static int __init init_ftmac100_debug(void)
{
	FTMAC100_DEBUG = 0;
	FTMAC100_INCR = 4;
	debug = 0;
	DEBUG(debug, 1, "ftmac100_debug module registered\n");

	proc_ftmac100_debug = proc_mkdir("ftmac100_debug", NULL);
	if (!proc_ftmac100_debug)
		return -ENOMEM;

	install_proc_table(proc_table_ftmac100_debug);

	return 0;
}

static void __exit cleanup_ftmac100_debug(void)
{
	remove_proc_table(proc_table_ftmac100_debug);
	remove_proc_entry("ftmac100_debug", NULL);

	DEBUG(debug, 1, "ftmac100_debug module unregistered\n");
}

module_init(init_ftmac100_debug);
module_exit(cleanup_ftmac100_debug);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ftmac100_debug Module");
