// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test application that data integraty of inter processor
 * communication from linux userspace to a remote software
 * context. The application sends chunks of data to the
 * remote processor. The remote side echoes the data back
 * to application which then validates the data returned.
 */

#include "rpmsg_demo_ctrl.h"

#define RPMSG_GET_KFIFO_SIZE 1
#define RPMSG_GET_AVAIL_DATA_SIZE 2
#define RPMSG_GET_FREE_SPACE 3
#define RPMSG_HEADER_LEN 16
#define MAX_RPMSG_BUFF_SIZE (512 - RPMSG_HEADER_LEN)

#define PAYLOAD_MIN_SIZE 1
#define PAYLOAD_MAX_SIZE (MAX_RPMSG_BUFF_SIZE - 24)
#define NUM_PAYLOADS	 (PAYLOAD_MAX_SIZE/PAYLOAD_MIN_SIZE)

static int charfd = -1, fd = -1, err_cnt;

struct _payload {
	unsigned long num;
	unsigned long size;
	char data[];
};

struct _payload *i_payload;
struct _payload *r_payload;

void send_shutdown(int fd)
{
	union {
		unsigned int n[8];
		struct _payload sdown;
	} umsg = {
		.n = {
			SHUTDOWN_MSG, SHUTDOWN_MSG, SHUTDOWN_MSG, SHUTDOWN_MSG,
			SHUTDOWN_MSG, SHUTDOWN_MSG, SHUTDOWN_MSG, SHUTDOWN_MSG,
		}
	};

	umsg.sdown.size = sizeof(umsg);
	if (write(fd, &umsg, sizeof(umsg)) < 0)
		perror("write SHUTDOWN_MSG\n");
}

int main(int argc, char *argv[])
{
	int ret, i, j;
	int size, bytes_rcvd, bytes_sent;
	int opt;
	char *rpmsg_dev = RPMSG_DEV;
	int ntimes = 1;
	char fpath[256];
	char rpmsg_char_name[16];
	struct rpmsg_endpoint_info eptinfo;
	char ept_dev_name[16];
	char ept_dev_path[32];

	while ((opt = getopt(argc, argv, "d:n:")) != -1) {
		switch (opt) {
		case 'd':
			rpmsg_dev = optarg;
			break;
		case 'n':
			ntimes = atoi(optarg);
			break;
		default:
			printf("getopt return unsupported option: -%c\n", opt);
			break;
		}
	}

	printf("\r\nEcho test start \r\n");
	printf("\r\nOpen rpmsg dev %s! \r\n", rpmsg_dev);
	sprintf(fpath, "%s/devices/%s", RPMSG_BUS_SYS, rpmsg_dev);
	if (access(fpath, F_OK)) {
		fprintf(stderr, "Not able to access rpmsg device %s, %s\n",
			fpath, strerror(errno));
		return -EINVAL;
	}

	charfd = get_rpmsg_chrdev_fd(rpmsg_dev, rpmsg_char_name);
	if (charfd < 0)
		return charfd;

	strcpy(eptinfo.name, "rpmsg-openamp-demo-channel");
	eptinfo.src = 0;
	eptinfo.dst = 0x400;
	ret = rpmsg_create_ept(charfd, &eptinfo);
	if (ret) {
		printf("failed to create RPMsg endpoint.\n");
		return -EINVAL;
	}
	if (!get_rpmsg_ept_dev_name(rpmsg_char_name, eptinfo.name,
				    ept_dev_name))
		return -EINVAL;
	sprintf(ept_dev_path, "/dev/%s", ept_dev_name);
	fd = open(ept_dev_path, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		perror("Failed to open rpmsg device.");
		close(charfd);
		return -1;
	}

	i_payload = (struct _payload *)malloc(2 * sizeof(unsigned long) + PAYLOAD_MAX_SIZE);
	r_payload = (struct _payload *)malloc(2 * sizeof(unsigned long) + PAYLOAD_MAX_SIZE);

	if (i_payload == 0 || r_payload == 0) {
		printf("ERROR: Failed to allocate memory for payload.\n");
		return -1;
	}

	for (j = 0; j < ntimes; j++) {
		printf("\r\n **********************************");
		printf("****\r\n");
		printf("\r\n  Echo Test Round %d \r\n", j);
		printf("\r\n **********************************");
		printf("****\r\n");
		for (i = 0, size = PAYLOAD_MIN_SIZE; i < NUM_PAYLOADS;
		i++, size++) {
			int k;

			i_payload->num = i;
			i_payload->size = size;

			/* Mark the data buffer. */
			memset(&(i_payload->data[0]), 0xA5, size);

			printf("\r\n sending payload number");
			printf(" %ld of size %ld\r\n", i_payload->num,
			(2 * sizeof(unsigned long)) + size);

			bytes_sent = write(fd, i_payload,
			(2 * sizeof(unsigned long)) + size);

			if (bytes_sent <= 0) {
				printf("\r\n Error sending data");
				printf(" .. \r\n");
				break;
			}
			printf("echo test: sent : %d\n", bytes_sent);

			r_payload->num = 0;
			bytes_rcvd = read(fd, r_payload,
					(2 * sizeof(unsigned long)) + PAYLOAD_MAX_SIZE);
			while (bytes_rcvd <= 0) {
				usleep(10000);
				bytes_rcvd = read(fd, r_payload,
					(2 * sizeof(unsigned long)) + PAYLOAD_MAX_SIZE);
			}
			printf(" received payload number ");
			printf("%ld of size %d\r\n", r_payload->num, bytes_rcvd);

			/* Validate data buffer integrity. */
			for (k = 0; k < r_payload->size; k++) {

				if (r_payload->data[k] != 0xA5) {
					printf(" \r\n Data corruption");
					printf(" at index %d \r\n", k);
					err_cnt++;
					break;
				}
			}
			bytes_rcvd = read(fd, r_payload,
			(2 * sizeof(unsigned long)) + PAYLOAD_MAX_SIZE);

		}
		printf("\r\n **********************************");
		printf("****\r\n");
		printf("\r\n Echo Test Round %d Test Results: Error count = %d\r\n",
		j, err_cnt);
		printf("\r\n **********************************");
		printf("****\r\n");
	}

	/* send_shutdown(fd); */

	free(i_payload);
	free(r_payload);

	close(fd);
	if (charfd >= 0)
		close(charfd);
	return 0;
}
