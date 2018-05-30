#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
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
 * Test that the kernel will handle breaking out of an indefinite wait for
 * resume-tx when the remote goes away.
 */

#define TEST_SIZE	100

#define REMOTE_NODE	100
#define REMOTE_PORT	100

static int run_receiver(struct qrtr_node *node)
{
	struct pollfd pfd;
	int ret;

	pfd.fd = node->fd;
	pfd.revents = 0;
	pfd.events = POLLIN | POLLERR;

	ret = poll(&pfd, 1, 10000);
	if (ret < 0)
		err(1, "poll failed");
	else if (ret == 0)
		errx(1, "poll timed out");

	/*
	 * Messages are pending, wait for a few more before killing the remote.
	 */
	sleep(2);

	close(node->fd);

	return 0;
}

static void sigalrm_handler(int signo)
{
	errx(1, "test timed out, probably stuck waiting for resume tx");
}

static int run_transmitter(void)
{
	struct sockaddr_qrtr sq = { AF_QIPCRTR, REMOTE_NODE, REMOTE_PORT };
	const char ping[] = "ping";
	unsigned sent = 0;
	ssize_t n;
	int status;
	int sock;
	int i;

	sock = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (sock < 0)
		err(1, "creating AF_QIPCRTR socket failed");

	signal(SIGALRM, sigalrm_handler);

	alarm(10);

	for (i = 0; i < TEST_SIZE; i++) {
		n = sendto(sock, ping, 4, 0, (void *)&sq, sizeof(sq));
		if (n < 0 && errno == EPIPE)
			break;
		if (n < 0)
			err(1, "failed to send ping to %d", sq.sq_node);
		
		sent++;
	}

	if (i == TEST_SIZE)
		errx(1, "sending didn't fail");

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

