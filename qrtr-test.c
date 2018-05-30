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
#include "qrtr-test.h"
#include "util.h"

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

ssize_t qrtr_node_hello(struct qrtr_node *node)
{
	struct qrtr_ctrl_pkt pkt = {};

	pkt.cmd = QRTR_TYPE_HELLO;

	return send_ctrl_message(node, pkt.cmd, &pkt, sizeof(pkt));
}

void qrtr_resume_tx(struct qrtr_node *node, int local_node, int local_port, int remote_node, int remote_port)
{
	struct qrtr_ctrl_pkt pkt = {};
	struct qrtr_hdr_v1 hdr = {};
	struct iovec iov[2];

	hdr.version = 1;
	hdr.type = QRTR_TYPE_RESUME_TX;
	hdr.src_node_id = local_node;
	hdr.src_port_id = local_port;
	hdr.confirm_rx = 0;
	hdr.size = sizeof(pkt);
	hdr.dst_node_id = remote_node;
	hdr.dst_port_id = remote_port;

	iov[0].iov_base = &hdr;
	iov[0].iov_len = sizeof(hdr);

	pkt.cmd = QRTR_TYPE_RESUME_TX;
	pkt.client.node = hdr.src_node_id;
	pkt.client.port = hdr.src_port_id;

	iov[1].iov_base = &pkt;
	iov[1].iov_len = sizeof(pkt);

	return writev(node->fd, iov, 2);
}

struct qrtr_node *qrtr_node_new(int node_id, int fd)
{
	struct qrtr_node *node;

	node = calloc(1, sizeof(*node));
	node->node_id = node_id;
	node->fd = fd;

	return node;
}

ssize_t send_data(struct qrtr_node *node, int port, struct sockaddr_qrtr *dest, const void *data, size_t len, int confirm_rx)
{
	struct qrtr_hdr_v1 hdr = {};
	struct iovec iov[2];

	hdr.version = 1;
	hdr.type = QRTR_TYPE_DATA;
	hdr.src_node_id = node->node_id;
	hdr.src_port_id = port;
	hdr.confirm_rx = !!confirm_rx;
	hdr.size = len;
	hdr.dst_node_id = dest->sq_node;
	hdr.dst_port_id = dest->sq_port;

	iov[0].iov_base = &hdr;
	iov[0].iov_len = sizeof(hdr);

	iov[1].iov_base = (void *)data;
	iov[1].iov_len = len;

	return writev(node->fd, iov, 2);
}

