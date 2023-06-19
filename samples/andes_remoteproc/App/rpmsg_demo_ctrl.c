// SPDX-License-Identifier: GPL-2.0-only
#include "rpmsg_demo_ctrl.h"

int rpmsg_create_ept(int rpfd, struct rpmsg_endpoint_info *eptinfo)
{
	int ret;

	ret = ioctl(rpfd, RPMSG_CREATE_EPT_IOCTL, eptinfo);
	if (ret)
		perror("Failed to create endpoint.\n");
	return ret;
}

char *get_rpmsg_ept_dev_name(const char *rpmsg_char_name,
			     const char *ept_name,
			     char *ept_dev_name)
{
	char sys_rpmsg_ept_name_path[64], svc_name[64];
	char dpath[256], fpath[256];
	int i, ept_name_len;

	FILE *fp;

	for (i = 0; i < 128; i++) {
		sprintf(sys_rpmsg_ept_name_path, "%s/%s/rpmsg%d/name",
			RPMSG_CLASS_SYS, rpmsg_char_name, i);
		printf("checking %s\n", sys_rpmsg_ept_name_path);
		if (access(sys_rpmsg_ept_name_path, F_OK) < 0)
			continue;
		fp = fopen(sys_rpmsg_ept_name_path, "r");
		if (!fp) {
			printf("failed to open %s\n", sys_rpmsg_ept_name_path);
			break;
		}
		fgets(svc_name, sizeof(svc_name), fp);
		fclose(fp);
		printf("svc_name: %s", svc_name);
		ept_name_len = strlen(ept_name);
		if (ept_name_len > sizeof(svc_name))
			ept_name_len = sizeof(svc_name);
		if (!strncmp(svc_name, ept_name, ept_name_len)) {
			sprintf(ept_dev_name, "rpmsg%d", i);

			/*
			 * Make file node under the /dev
			 */
			sprintf(dpath, "%s/%s/dev",
					RPMSG_CLASS_SYS, ept_dev_name);
			sprintf(fpath, "/dev/%s", ept_dev_name);
			set_dev_node(dpath, fpath);

			return ept_dev_name;
		}
	}

	printf("Not able to RPMsg endpoint file for %s:%s.\n",
	       rpmsg_char_name, ept_name);
	return NULL;
}

int get_rpmsg_chrdev_fd(const char *rpmsg_dev_name,
			char *rpmsg_ctrl_name)
{
	char dpath[512], fpath[512];
	char *rpmsg_ctrl_prefix = "rpmsg_ctrl";
	DIR *dir;
	struct dirent *ent;
	int fd;

	sprintf(dpath, "%s/devices/%s/rpmsg", RPMSG_BUS_SYS, rpmsg_dev_name);
	dir = opendir(dpath);
	if (dir == NULL) {
		fprintf(stderr, "Failed to open dir %s\n", dpath);
		return -EINVAL;
	}
	while ((ent = readdir(dir)) != NULL) {
		if (!strncmp(ent->d_name, rpmsg_ctrl_prefix,
			    strlen(rpmsg_ctrl_prefix))) {
			printf("Opening file %s.\n", ent->d_name);
			sprintf(fpath, "/dev/%s", ent->d_name);

			/*
			 * Make file node under the /dev
			 */
			sprintf(dpath, "%s/devices/%s/rpmsg/%s/dev",
				RPMSG_BUS_SYS, rpmsg_dev_name, ent->d_name);
			set_dev_node(dpath, fpath);

			fd = open(fpath, O_RDWR | O_NONBLOCK);
			if (fd < 0) {
				fprintf(stderr,
					"Failed to open rpmsg char dev %s,%s\n",
					fpath, strerror(errno));
				closedir(dir);
				return fd;
			}
			sprintf(rpmsg_ctrl_name, "%s", ent->d_name);
			closedir(dir);
			return fd;
		}
	}

	fprintf(stderr, "No rpmsg char dev file is found\n");
	return -EINVAL;
}

void set_dev_node(char *path, char *dev_path)
{
	FILE *fp;
	char dev[12];
	char *major, *minor;
	int major_num, minor_num, ret;

	fp = fopen(path, "r");
	if (!fp)
		printf("Failed to open %s\n", path);

	fgets(dev, sizeof(dev), fp);
	fclose(fp);

	major = strtok(dev, ":");
	minor = strtok(NULL, ":");

	major_num = atoi(major);
	minor_num = atoi(minor);
	printf("dev_path = %s, major = %d, minor = %d\n",
		   dev_path, major_num, minor_num);

	ret = mknod(dev_path, S_IFCHR, makedev(major_num, minor_num));

	if (ret)
		printf("mknod No.%d\n", ret);
}
