// SPDX-License-Identifier: GPL-2.0-only
#include "proxy_app.h"

//#define LPRINTF(format, ...) printf(format, ##__VA_ARGS__)
#define LPRINTF(format, ...)

#define RPC_BUFF_SIZE 512
#define PROXY_ENDPOINT 127

/* Initialization message ID */
#define RPMG_INIT_MSG	"init_msg"

struct _proxy_data {
	int active;
	int rpmsg_proxy_fd;
	struct _sys_rpc *rpc;
	struct _sys_rpc *rpc_response;
	char *firmware_path;
};

static struct _proxy_data *proxy;
char sbuf[512];
int remote_id;

int handle_open(struct _sys_rpc *rpc)
{
	int fd;
	ssize_t bytes_written;

	/* Open remote fd */

	fd = open(rpc->sys_call_args.data, rpc->sys_call_args.int_field1,
			rpc->sys_call_args.int_field2);

	/* Construct rpc response */
	proxy->rpc_response->id = OPEN_SYSCALL_ID;
	proxy->rpc_response->sys_call_args.int_field1 = fd;
	proxy->rpc_response->sys_call_args.int_field2 = 0; /*not used*/
	proxy->rpc_response->sys_call_args.data_len = 0; /*not used*/

	/* Transmit rpc response */
	bytes_written = write(proxy->rpmsg_proxy_fd, proxy->rpc_response,
				sizeof(struct _sys_rpc));

	return (bytes_written != sizeof(struct _sys_rpc)) ? -1 : 0;
}

int handle_close(struct _sys_rpc *rpc)
{
	int retval;
	ssize_t bytes_written;

	/* Close remote fd */
	retval = close(rpc->sys_call_args.int_field1);

	/* Construct rpc response */
	proxy->rpc_response->id = CLOSE_SYSCALL_ID;
	proxy->rpc_response->sys_call_args.int_field1 = retval;
	proxy->rpc_response->sys_call_args.int_field2 = 0; /*not used*/
	proxy->rpc_response->sys_call_args.data_len = 0; /*not used*/

	/* Transmit rpc response */
	bytes_written = write(proxy->rpmsg_proxy_fd, proxy->rpc_response,
				sizeof(struct _sys_rpc));

	return (bytes_written != sizeof(struct _sys_rpc)) ? -1 : 0;
}

int handle_read(struct _sys_rpc *rpc)
{
	ssize_t bytes_read, bytes_written;
	size_t  payload_size;
	char *buff = proxy->rpc_response->sys_call_args.data;

	if (rpc->sys_call_args.int_field1 == 0)
		/*
		 * * Perform read from fd for large size since this is a
		 * STD/I request
		 */
		bytes_read = read(rpc->sys_call_args.int_field1, buff, 512);
	else
		/* Perform read from fd */
		bytes_read = read(rpc->sys_call_args.int_field1, buff,
					rpc->sys_call_args.int_field2);

	/* Construct rpc response */
	proxy->rpc_response->id = READ_SYSCALL_ID;
	proxy->rpc_response->sys_call_args.int_field1 = bytes_read;
	proxy->rpc_response->sys_call_args.int_field2 = 0; /* not used */
	proxy->rpc_response->sys_call_args.data_len = bytes_read;

	payload_size = sizeof(struct _sys_rpc) +
			((bytes_read > 0) ? bytes_read : 0);

	/* Transmit rpc response */
	LPRINTF("%s: %d, %d\n", __func__, proxy->rpc_response->id,
			proxy->rpc_response->sys_call_args.int_field1);
	bytes_written = write(proxy->rpmsg_proxy_fd, proxy->rpc_response,
					payload_size);

	return (bytes_written != payload_size) ? -1 : 0;
}

int handle_write(struct _sys_rpc *rpc)
{
	ssize_t bytes_written;

	/* Write to remote fd */
	bytes_written = write(rpc->sys_call_args.int_field1,
				rpc->sys_call_args.data,
				rpc->sys_call_args.int_field2);

	/* Construct rpc response */
	proxy->rpc_response->id = WRITE_SYSCALL_ID;
	proxy->rpc_response->sys_call_args.int_field1 = bytes_written;
	proxy->rpc_response->sys_call_args.int_field2 = 0; /*not used*/
	proxy->rpc_response->sys_call_args.data_len = 0; /*not used*/

	/* Transmit rpc response */
	bytes_written = write(proxy->rpmsg_proxy_fd, proxy->rpc_response,
				sizeof(struct _sys_rpc));

	return (bytes_written != sizeof(struct _sys_rpc)) ? -1 : 0;
}

int handle_rpc(struct _sys_rpc *rpc)
{
	int retval;

	/* Handle RPC */
	switch ((int)(rpc->id)) {
	case OPEN_SYSCALL_ID:
	{
		retval = handle_open(rpc);
		break;
	}
	case CLOSE_SYSCALL_ID:
	{
		retval = handle_close(rpc);
		break;
	}
	case READ_SYSCALL_ID:
	{
		retval = handle_read(rpc);
		break;
	}
	case WRITE_SYSCALL_ID:
	{
		retval = handle_write(rpc);
		break;
	}
	case TERM_SYSCALL_ID:
	{
		proxy->active = 0;
		retval = 0;
		break;
	}
	default:
	{
		printf("\r\nMaster>Err:Invalid RPC sys call ID: %d! \r\n", rpc->id);
		retval = -1;
		break;
	}
	}

	return retval;
}

/* write a string to an existing and writtable file */
int file_write(char *path, char *str)
{
	int fd;
	ssize_t bytes_written;
	size_t str_sz;

	fd = open(path, O_WRONLY);
	if (fd == -1) {
		perror("Error");
		return -1;
	}
	str_sz = strlen(str);
	bytes_written = write(fd, str, str_sz);
	if (bytes_written != str_sz) {
		if (bytes_written == -1)
			perror("Error");
		close(fd);
		return -1;
	}

	if (-1 == close(fd)) {
		perror("Error");
		return -1;
	}
	return 0;
}

/* Stop remote CPU and Unload drivers */
void stop_remote(void)
{
	system("modprobe -r rpmsg_char");
	sprintf(sbuf,
		"/sys/class/remoteproc/remoteproc%u/state",
		remote_id);
	(void)file_write(sbuf, "stop");
}

void exit_action_handler(int signum)
{
	proxy->active = 0;
}

void kill_action_handler(int signum)
{
	printf("\r\nMaster>RPC service killed !!\r\n");

	/* Close proxy rpmsg device */
	close(proxy->rpmsg_proxy_fd);

	/* Free up resources */
	free(proxy->rpc);
	free(proxy->rpc_response);
	free(proxy);

	/* Stop remote cpu and unload drivers */
	stop_remote();
}

void display_help_msg(void)
{
	printf("\r\nLinux proxy application.\r\n");
	printf("-v	 Displays proxy application version.\n");
	printf("-c	 Whether to use RPMsg char driver.\n");
	printf("-f	 Accepts path of firmware to load on remote core.\n");
	printf("-r       Which remoteproc instance\n");
	printf("-h	 Displays this help message.\n");
}

int main(int argc, char *argv[])
{
	struct sigaction exit_action;
	struct sigaction kill_action;
	int bytes_rcvd;
	int opt = 0, ret = 0;
	char *rpmsg_dev = RPMSG_DEV;
	char rpmsg_char_name[16];
	int rpmsg_char_fd = -1, ept_fd = -1;
	struct rpmsg_endpoint_info eptinfo;
	char ept_dev_name[16];
	char ept_dev_path[32];

	/* Initialize signalling infrastructure */
	memset(&exit_action, 0, sizeof(struct sigaction));
	memset(&kill_action, 0, sizeof(struct sigaction));
	exit_action.sa_handler = exit_action_handler;
	kill_action.sa_handler = kill_action_handler;
	sigaction(SIGTERM, &exit_action, NULL);
	sigaction(SIGINT, &exit_action, NULL);
	sigaction(SIGKILL, &kill_action, NULL);
	sigaction(SIGHUP, &kill_action, NULL);

	while ((opt = getopt(argc, argv, "vh:r:")) != -1) {
		switch (opt) {
		case 'r':
			remote_id = atoi(optarg);
			if (remote_id != 0 && remote_id != 1) {
				display_help_msg();
				return -1;
			}
			break;
		case 'v':
			printf("\r\nLinux proxy application version 1.1\r\n");
			return 0;
		case 'h':
			display_help_msg();
			return 0;
		default:
			printf("getopt return unsupported option: -%c\n", opt);
			break;
		}
	}

	rpmsg_char_fd = get_rpmsg_chrdev_fd(rpmsg_dev, rpmsg_char_name);
	if (rpmsg_char_fd < 0) {
		ret = rpmsg_char_fd;
		goto error0;
	}

	/* Create endpoint from rpmsg char driver */
	strcpy(eptinfo.name, "rpmsg-openamp-demo-channel");
	eptinfo.src = 0;
	eptinfo.dst = 0x400;
	ret = rpmsg_create_ept(rpmsg_char_fd, &eptinfo);
	if (ret) {
		printf("failed to create RPMsg endpoint.\n");
		goto error0;
	}
	if (!get_rpmsg_ept_dev_name(rpmsg_char_name, eptinfo.name,
				    ept_dev_name)) {
		ret = -EINVAL;
		goto error0;
	}
	sprintf(ept_dev_path, "/dev/%s", ept_dev_name);
	ept_fd = open(ept_dev_path, O_RDWR | O_NONBLOCK);
	if (ept_fd < 0) {
		perror("Failed to open rpmsg device.");
		ret = ept_fd;
		goto error0;
	}
	/* Allocate memory for proxy data structure */
	proxy = malloc(sizeof(struct _proxy_data));
	if (proxy == NULL) {
		fprintf(stderr, "\r\nMaster>Failed to allocate memory.\r\n");
		ret = -ENOMEM;
		goto error0;
	}
	proxy->rpmsg_proxy_fd = ept_fd;
	proxy->active = 1;

	/* Allocate memory for rpc payloads */
	proxy->rpc = malloc(RPC_BUFF_SIZE);
	proxy->rpc_response = malloc(RPC_BUFF_SIZE);
	if (proxy->rpc == NULL || proxy->rpc_response == NULL) {
		fprintf(stderr, "\r\nMaster>Failed to allocate memory.\r\n");
		ret = -ENOMEM;
		goto error0;
	}

	/* RPC service starts */
	printf("\r\nMaster>RPC service started !!\r\n");
	/* Send init message to remote.
	 * This is required otherwise, remote doesn't know the host
	 * RPMsg endpoint
	 */
	ret = write(proxy->rpmsg_proxy_fd, RPMG_INIT_MSG,
		    sizeof(RPMG_INIT_MSG));
	if (ret < 0) {
		printf("\r\nMaster>Failed to send init message.\r\n");
		goto error0;
	}

	/* Block on read for rpc requests from remote context */
	do {
		bytes_rcvd = read(proxy->rpmsg_proxy_fd, proxy->rpc,
				  RPC_BUFF_SIZE);
		if (bytes_rcvd < 0 && errno != EAGAIN) {
			perror("Failed to read ept");
			break;
		}
		/* Handle rpc */
		if (bytes_rcvd > 0 && handle_rpc(proxy->rpc)) {
			printf("\nMaster>Err:Handling remote procedure call!\n");
			printf("\nrpc id %d\n", proxy->rpc->id);
			printf("\nrpc int field1 %d\n",
				proxy->rpc->sys_call_args.int_field1);
			printf("\nrpc int field2 %d\n",
				proxy->rpc->sys_call_args.int_field2);
			break;
		}
	} while (proxy->active);

	printf("\r\nMaster>RPC service exiting !!\r\n");
	ret = 0;

	/* Close proxy rpmsg device */
	close(proxy->rpmsg_proxy_fd);

	/* Wait for other end to cleanup
	 * Otherwise, virtio_rpmsg_bus can post msg with no recipient
	 * warning as it can receive NS destroy after the rpmsg char
	 * module is removed.
	 */
	sleep(1);

	/* Free up resources */
	free(proxy->rpc);
	free(proxy->rpc_response);

error0:
	if (ept_fd >= 0)
		close(ept_fd);
	if (rpmsg_char_fd >= 0)
		close(rpmsg_char_fd);
	free(proxy);

	stop_remote();

	return ret;
}
