#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "qrtr.h"
#include "util.h"

/*
 * Register two nodes on a single endpoint, simulating a remote with multiple
 * CPUs, and send a simple message to each one.
 */

struct qrtr_hdr_v1 {
	__le32 version;
	__le32 type;
	__le32 src_node_id;
	__le32 src_port_id;
	__le32 confirm_rx;
	__le32 size;
	__le32 dst_node_id;
	__le32 dst_port_id;
} __packed;

struct qrtr_node {
	int node_id;

	int fd;
};

static ssize_t send_ctrl_message(struct qrtr_node *node, int type, const void *data, size_t len)
{
	struct qrtr_hdr_v1 hdr = {};
	struct iovec iov[2];

	hdr.version = 1;
	hdr.type = type;
	hdr.src_node_id = node->node_id;
	hdr.src_port_id = QRTR_PORT_CTRL;
	hdr.confirm_rx = 0;
	hdr.size = len;
	hdr.dst_node_id = QRTR_NODE_BCAST;
	hdr.dst_port_id = QRTR_PORT_CTRL;

	iov[0].iov_base = &hdr;
	iov[0].iov_len = sizeof(hdr);

	iov[1].iov_base = (void *)data;
	iov[1].iov_len = len;

	return writev(node->fd, iov, 2);
}

static ssize_t qrtr_node_hello(struct qrtr_node *node)
{
	struct qrtr_ctrl_pkt pkt = {};

	pkt.cmd = QRTR_TYPE_HELLO;

	return send_ctrl_message(node, pkt.cmd, &pkt, sizeof(pkt));
}

struct qrtr_node *qrtr_node_new(int node_id, int fd)
{
	struct qrtr_node *node;

	node = calloc(1, sizeof(*node));
	node->node_id = node_id;
	node->fd = fd;

	return node;
}

static ssize_t send_ping(int sock, int node)
{
	struct sockaddr_qrtr sq = { AF_QIPCRTR, node, 1 };
	char ping[] = "ping";
	ssize_t n;

	n = sendto(sock, ping, 4, 0, (void *)&sq, sizeof(sq));
	if (n < 0)
		warn("failed to send ping to %d", node);

	return 0;
}

/* Send hello message 1 */
/* Receive hello message 1 */
/* Send hello message 2 */
/* Receive hello message 2 */
/* Send ping to node 1 */
/* Receive ping 1 */
/* Send ping to node 2 */
/* Receive ping 2 */
enum {
	STEP_SEND_HELLO_1,
	STEP_RECEIVE_HELLO_1,
	STEP_SEND_HELLO_2,
	STEP_RECEIVE_HELLO_2,
	STEP_SEND_PING_1,
	STEP_RECEIVE_PING_1,
	STEP_SEND_PING_2,
	STEP_RECEIVE_PING_2,
	STEP_DONE,
};

static int test_passes;
static int test_fails;

static void pass(const char *msg)
{
	fprintf(stderr, "[PASS]: %s\n", msg);

	test_passes++;
}

static void fail(const char *msg)
{
	fprintf(stderr, "[FAIL]: %s\n", msg);

	test_fails++;
}

int main(int argc, char **argv)
{
	struct qrtr_node *nodes[2];
	struct qrtr_hdr_v1 hdr;
	struct timeval tv;
	struct iovec iov[2];
	fd_set rset;
	int tun_fd;
	ssize_t n;
	char buf[8192];
	int sock;
	int step = STEP_SEND_HELLO_1;

	sock = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (sock < 0)
		err(1, "creating AF_QIPCRTR socket failed");

	tun_fd = open("/dev/qrtr-tun", O_RDWR);
	if (tun_fd < 0)
		err(1, "failed to open qrtr-tun");

	nodes[0] = qrtr_node_new(100, tun_fd);
	nodes[1] = qrtr_node_new(101, tun_fd);

	iov[0].iov_base = &hdr;
	iov[0].iov_len = sizeof(hdr);

	iov[1].iov_base = buf;
	iov[1].iov_len = sizeof(buf);

	while (step != STEP_DONE) {
		FD_ZERO(&rset);
		FD_SET(tun_fd, &rset);

		tv.tv_sec = 5;
		tv.tv_usec = 0;

		switch (step) {
		case STEP_SEND_HELLO_1:
			qrtr_node_hello(nodes[0]);
			step++;
			break;
		case STEP_SEND_HELLO_2:
			qrtr_node_hello(nodes[1]);
			step++;
			break;
		case STEP_SEND_PING_1:
			send_ping(sock, nodes[0]->node_id);
			step++;
			break;
		case STEP_SEND_PING_2:
			send_ping(sock, nodes[1]->node_id);
			step++;
			break;
		}

		n = select(tun_fd + 1, &rset, NULL, NULL, &tv);
		if (n == 0) {
			switch (step) {
			case STEP_RECEIVE_HELLO_1:
				fail("timeout receiving hello 1");
				step++;
				break;
			case STEP_RECEIVE_HELLO_2:
				fail("timeout receiving hello 2");
				step++;
				break;
			case STEP_RECEIVE_PING_1:
				fail("timeout receiving ping 1");
				step++;
				break;
			case STEP_RECEIVE_PING_2:
				fail("timeout receiving ping 2");
				step++;
				break;
			}

			continue;
		}

		n = readv(tun_fd, iov, 2);
		if (n < 0)
			err(1, "failed to read");

		switch (hdr.type) {
		case QRTR_TYPE_HELLO:
			switch (step) {
			case STEP_RECEIVE_HELLO_1:
				pass("received hello 1");
				step++;
				break;
			case STEP_RECEIVE_HELLO_2:
				pass("received hello 2");
				step++;
				break;
			}
			break;
		case QRTR_TYPE_DATA:
			switch (step) {
			case STEP_RECEIVE_PING_1:
				if (hdr.dst_node_id == nodes[0]->node_id) {
					pass("received ping 1");
					step++;
				}
				break;
			case STEP_RECEIVE_PING_2:
				if (hdr.dst_node_id == nodes[1]->node_id) {
					pass("received ping 2");
					step++;
				}
				break;
			}
			break;
		}
	}

	return !!test_fails;
}
