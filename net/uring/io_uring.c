#include <arpa/inet.h> // debugging
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <liburing.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>

// TODO: use fixed buffers? https://unixism.net/loti/tutorial/fixed_buffers.html

typedef struct io_uring go_uring;
typedef struct msghdr go_msghdr;
typedef struct iovec go_iovec;
typedef struct sockaddr_in go_sockaddr_in;

// Wait for a completion to be available, fetch the data
static int receive_into(struct io_uring *ring) {
    struct io_uring_cqe *cqe;
again:;

    int ret = io_uring_wait_cqe(ring, &cqe);
    if (ret == -EINTR) {
        goto again;
    }
    // TODO: Delete perror, fprintf, etc.
    // Encode in return value or similar.
    if (ret < 0) {
        perror("io_uring_wait_cqe");
        return ret;
    }
    if (cqe->res < 0) {
        fprintf(stderr, "recvmsg failed: %d.\n", cqe->res);
        return cqe->res;
    }
    struct msghdr *mhdr = io_uring_cqe_get_data(cqe);
    if (mhdr == NULL) {
        fprintf(stderr, "received nop\n");
        return -1;
    }
    int n = cqe->res;
    io_uring_cqe_seen(ring, cqe);
    return n;
}

static uint32_t ip(struct sockaddr_in *sa) {
    return ntohl(sa->sin_addr.s_addr);
}

static uint16_t port(struct sockaddr_in *sa) {
    return ntohs(sa->sin_port);
}

// submit a recvmsg request via liburing
// TODO: What recvfrom support arrives, maybe use that instead?
static int submit_recvmsg_request(int sock, struct io_uring *ring, struct msghdr *mhdr, struct iovec *iov, struct sockaddr_in *sender, char *buf, int buflen) {
    iov->iov_base = buf;
    iov->iov_len = buflen;

    mhdr->msg_iov = iov;
    mhdr->msg_iovlen = 1;

    mhdr->msg_name = sender;
    mhdr->msg_namelen = sizeof(struct sockaddr_in);

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_recvmsg(sqe, sock, mhdr, 0);
    io_uring_sqe_set_data(sqe, mhdr);
    io_uring_submit(ring);

    return 0;
}

static void submit_nop_request(struct io_uring *ring) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	io_uring_prep_nop(sqe);
    io_uring_submit(ring);
}