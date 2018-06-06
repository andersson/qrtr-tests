#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <err.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "qrtr.h"
#include "qrtr-test.h"
#include "util.h"

#define BIT(x) (1 << (x))

/*
 * Verify that registered services are announced to newly connected remotes.
 *
 * Register N services
 * Boot remote
 * Check that the N services was announced
 * Stop the remote
 * Repeat
 */

#define REMOTE_NODE	100

#define SERVICE_COUNT	100
#define TEST_ROUNDS	3

static int register_service(int idx)
{
	struct sockaddr_qrtr sq;
	struct qrtr_ctrl_pkt pkt = {};
	socklen_t sl = sizeof(sq);
	ssize_t n;
	int sock;
	int ret;

	pkt.cmd = QRTR_TYPE_NEW_SERVER;
	pkt.server.service = 1337;
	pkt.server.instance = idx + 1;

	sock = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (sock < 0)
		err(1, "creating AF_QIPCRTR socket failed");

	ret = getsockname(sock, (void *)&sq, &sl);
	if (ret < 0)
		err(1, "getsockname failed");

	sq.sq_port = QRTR_PORT_CTRL;

	n = sendto(sock, &pkt, sizeof(pkt), 0, (void *)&sq, sizeof(sq));
	if (n < 0)
		err(1, "fail to register service");

	return 0;
}

static int test_announcement(void)
{
	struct qrtr_ctrl_pkt *ctrl;
	struct qrtr_hdr_v1 hdr;
	struct qrtr_node *node;
	struct iovec iov[2];
	struct timeval tv = {5, 0};
	unsigned received = 0;
	int result;
	fd_set rset;
	ssize_t n;
	int tun_fd;
	char buf[128];
	int ret;
	uint32_t seen[(SERVICE_COUNT + 31) / 32] = {};
	int bit;
	int i;

	iov[0].iov_base = &hdr;
	iov[0].iov_len = sizeof(hdr);

	iov[1].iov_base = buf;
	iov[1].iov_len = sizeof(buf);

	tun_fd = open("/dev/qrtr-tun", O_RDWR);
	if (tun_fd < 0)
		err(1, "failed to open qrtr-tun");

	node = qrtr_node_new(REMOTE_NODE, tun_fd);

	ret = qrtr_node_hello(node);
	if (ret < 0)
		err(1, "failed to hello");

	for (;;) {
		FD_ZERO(&rset);
		FD_SET(tun_fd, &rset);

		usleep(1000);

		n = select(tun_fd + 1, &rset, NULL, NULL, &tv);
		if (n < 0)
			err(1, "select failed");
		if (!n)
			break;

		if (FD_ISSET(tun_fd, &rset)) {
			n = readv(tun_fd, iov, 2);
			if (n < (int)sizeof(hdr))
				err(1, "failed to read");

			if (hdr.type != QRTR_TYPE_NEW_SERVER)
			       continue;

			if (n < (int)sizeof(hdr) + hdr.size)
				errx(1, "truncated control packet");

			ctrl = (struct qrtr_ctrl_pkt *)buf;
			if (ctrl->cmd != QRTR_TYPE_NEW_SERVER)
				err(1, "new server message is a %d message", ctrl->cmd);

			if (ctrl->server.service == 1337) {
				bit = ctrl->server.instance - 1;

				if (seen[bit / 32] & BIT(bit % 32))
					warnx("Already been notified about instance %d", ctrl->server.instance);

				seen[bit / 32] |= BIT(bit % 32);

				received++;
			}
		}
	}

	close(tun_fd);

	warnx("received %d of %d service announcements", received, SERVICE_COUNT);

	result = 0;

	for (i = 0; i < SERVICE_COUNT; i++) {
		if (!(seen[i / 32] & BIT(i % 32))) {
			warnx("Expected instance %d to be announced", i + 1);
			result = 1;
		}
	}

	return result;
}

int main(int argc, char **argv)
{
	int result;
	int i;

	for (i = 0; i < SERVICE_COUNT; i++)
		register_service(i);

	for (i = 0; i < TEST_ROUNDS; i++) {
		result = test_announcement();
		if (result)
			return result;
	}

	return 0;
}
