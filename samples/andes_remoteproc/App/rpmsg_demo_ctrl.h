/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TEMP_H__
#define __TEMP_H__

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/rpmsg.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>

#define RPMSG_BUS_SYS   "/sys/bus/rpmsg"
#define RPMSG_CLASS_SYS "/sys/class/rpmsg"
#define RPMSG_DEV       "virtio0.rpmsg_ctrl.0.0"
#define SHUTDOWN_MSG    0xEF56A55A

int rpmsg_create_ept(int rpfd, struct rpmsg_endpoint_info *eptinfo);
char *get_rpmsg_ept_dev_name(const char *rpmsg_char_name,
			     const char *ept_name,
			     char *ept_dev_name);
int get_rpmsg_chrdev_fd(const char *rpmsg_dev_name,
			char *rpmsg_ctrl_name);
void set_dev_node(char *path, char *dev_path);

#endif /* __TEMP_H__ */
