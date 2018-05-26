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

/*
 * In order to keep resource consumption at bay the sending side is expected to
 * set the confirm_rx flag on some packets and never have more than some
 * limited number (MAX_DEPTH) of packets outstanding.
 *
 * With a recommended low watermark of 5 and a limit of 10 a maximum of 2 * H -
 * L packets may be in transit at any point in time.
 *
 * The following test register a remote node and send 1000 packets to ports in
 * the range [100, 110) and count the maximum number of packets between
 * confirm_rx.
 *
 * As the limit is sender-defined and only given a recomended value of 10 we
 * compare with the arbitrary limit of 20 here.
 */

#define TEST_SIZE	1000

#define MAX_DEPTH	20

#define REMOTE_NODE	100

static int run_receiver(struct qrtr_node *node)
{
	struct qrtr_hdr_v1 hdr;
	struct iovec iov[2];
	struct timeval tv;
	unsigned depth[10] = {};
	unsigned count[10] = {};
	unsigned max_depth = 0;
	char buf[8192];
	fd_set rset;
	int tun_fd = node->fd;
	ssize_t n;
	int bucket;
	int i;

	iov[0].iov_base = &hdr;
	iov[0].iov_len = sizeof(hdr);

	iov[1].iov_base = buf;
	iov[1].iov_len = sizeof(buf);

	for (;;) {
		FD_ZERO(&rset);
		FD_SET(tun_fd, &rset);

		tv.tv_sec = 5;
		tv.tv_usec = 0;

		n = select(tun_fd + 1, &rset, NULL, NULL, &tv);
		if (n < 0)
			err(1, "[remote] select failed");

		if (!n)
			break;

		if (!FD_ISSET(tun_fd, &rset))
			continue;

		n = readv(tun_fd, iov, 2);
		if (n < (int)sizeof(hdr))
			err(1, "[remote] failed to read");

		if (hdr.type == QRTR_TYPE_DATA) {
			bucket = hdr.dst_port_id % 10;

			count[bucket]++;

			depth[bucket] = MAX(count[bucket], depth[bucket]);

			if (hdr.confirm_rx) {
				printf("confirm_rx set\n");
				count[bucket] = 0;
			}
		}
	}

	printf("buckets:");
	for (i = 0; i < 10; i++) {
		printf(" %d", depth[i]);
		max_depth = MAX(max_depth, depth[i]);
	}
	printf("\n");

	printf("max depth: %d\n", max_depth);

	return max_depth < MAX_DEPTH ? 0 : 1;
}

static int run_transmitter(void)
{
	struct sockaddr_qrtr sq = { AF_QIPCRTR, REMOTE_NODE };
	const char ping[] = "ping";
	unsigned sent = 0;
	ssize_t n;
	int status;
	int sock;
	int i;

	sock = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (sock < 0)
		err(1, "creating AF_QIPCRTR socket failed");

	for (i = 0; i < TEST_SIZE; i++) {
		sq.sq_port = 100 + rand() % 10;

		n = sendto(sock, ping, 4, 0, (void *)&sq, sizeof(sq));
		if (n < 0)
			err(1, "failed to send ping to %d", sq.sq_node);

		sent++;
	}

	printf("sent %d messages\n", sent);

	wait(&status);

	return WEXITSTATUS(status);
}

int main(int argc, char **argv)
{
	struct qrtr_node *node;
	int tun_fd;
	int pid;
	int ret;

	tun_fd = open("/dev/qrtr-tun", O_RDWR);
	if (tun_fd < 0)
		err(1, "failed to open qrtr-tun");

	node = qrtr_node_new(REMOTE_NODE, tun_fd);

	ret = qrtr_node_hello(node);
	if (ret < 0)
		err(1, "failed to hello");

	pid = fork();
	switch (pid) {
	case -1:
		err(1, "fork failed");
	case 0:
		return run_receiver(node);
	default:
		close(tun_fd);
		return run_transmitter();
	}
}
