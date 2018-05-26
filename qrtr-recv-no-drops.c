#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <err.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "qrtr.h"
#include "qrtr-test.h"
#include "util.h"

#define TEST_SIZE	1000

#define FLOW_H		10
#define FLOW_L		5

static void run_remote(struct sockaddr_qrtr local_sq)
{
	struct qrtr_hdr_v1 hdr;
	struct qrtr_node *node;
	unsigned transmitted = 0;
	struct timeval tv;
	struct iovec iov[2];
	fd_set rset;
	ssize_t n;
	int tun_fd;
	char buf[8192];
	int count = 0;

	tun_fd = open("/dev/qrtr-tun", O_RDWR);
	if (tun_fd < 0)
		err(1, "failed to open qrtr-tun");

	node = qrtr_node_new(100, tun_fd);

	qrtr_node_hello(node);

	printf("[remote] test socket at %d:%d\n", local_sq.sq_node, local_sq.sq_port);

	iov[0].iov_base = &hdr;
	iov[0].iov_len = sizeof(hdr);

	iov[1].iov_base = buf;
	iov[1].iov_len = sizeof(buf);

	for (;;) {
		const char ping[] = "ping";
		FD_ZERO(&rset);
		FD_SET(tun_fd, &rset);

		if (count >= FLOW_H || transmitted == TEST_SIZE) {
			tv.tv_sec = 5;
			tv.tv_usec = 0;
		} else {
			tv.tv_sec = 0;
			tv.tv_usec = 0;
		}

		n = select(tun_fd + 1, &rset, NULL, NULL, &tv);
		if (n < 0)
			err(1, "[remote] select failed");

		if (!n && transmitted == TEST_SIZE)
			break;

		if (!n && count >= FLOW_H)
			err(1, "[remote] no resume tx received");

		if (FD_ISSET(tun_fd, &rset)) {
			n = readv(tun_fd, iov, 2);
			if (n < (int)sizeof(hdr))
				err(1, "[remote] failed to read");

			if (hdr.type == QRTR_TYPE_RESUME_TX)
				count = 0;
		} else {
			n = send_data(node, 1000, &local_sq, ping, 4, count == FLOW_L);
			if (n < 0)
				warn("[remote] send data failed\n");

			transmitted++;
			count++;
		}
	}

	printf("[remote] sent %d\n", transmitted);
}

int main(int argc, char **argv)
{
	struct sockaddr_qrtr sq;
	socklen_t sl = sizeof(sq);
	unsigned received = 0;
	struct pollfd pfd;
	char buf[128];
	ssize_t n;
	int sock;
	int ret;

	sock = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (sock < 0)
		err(1, "creating AF_QIPCRTR socket failed");

	sq.sq_family = AF_QIPCRTR;
	sq.sq_node = 1;
	sq.sq_port = 0;
	ret = bind(sock, (void *)&sq, sizeof(sq));
	if (ret < 0)
		err(1, "bind failed");

	ret = getsockname(sock, (void *)&sq, &sl);
	if (ret < 0)
		err(1, "getsockname failed");

	ret = fork();
	if (ret < 0)
		err(1, "fork failed");

	if (!ret) {
		close(sock);
		run_remote(sq);
		exit(0);
	}

	for (;;) {
		pfd.fd = sock;
		pfd.revents = 0;
		pfd.events = POLLIN | POLLERR;

		ret = poll(&pfd, 1, 1000);
		if (ret < 0)
			err(1, "poll failed");
		if (!ret)
			break;

		sl = sizeof(sq);
		n = recvfrom(sock, buf, sizeof(buf), 0, (void *)&sq, &sl);
		if (n < 0)
			warn("failed receive message");

		received++;
		usleep(1000);
	}

	wait(NULL);

	printf("received %d of %d messages\n", received, TEST_SIZE);

	return received == TEST_SIZE ? 0 : 1;
}
