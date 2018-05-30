#ifndef __QRTR_TEST_H__
#define __QRTR_TEST_H__

#include "qrtr.h"

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

struct qrtr_node *qrtr_node_new(int node_id, int fd);
ssize_t qrtr_node_hello(struct qrtr_node *node);
void qrtr_resume_tx(struct qrtr_node *node, int local_node, int local_port, int remote_node, int remote_port);
ssize_t send_data(struct qrtr_node *node, int port, struct sockaddr_qrtr *dest, const void *data, size_t len, int confirm_rx);

#endif
