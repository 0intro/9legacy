#ifndef __POLL_H
#define __POLL_H
#pragma lib "/$M/lib/ape/libap.a"

#ifndef	FD_SETSIZE
#define	FD_SETSIZE	96
#endif

struct pollfd {
	int fd;			/* file descriptor */
	short events;	/* events to look for */
	short revents;	/* events returned */
};

typedef unsigned long nfds_t;

#define	POLLIN		0x001
#define	POLLPRI		0x002
#define	POLLOUT		0x004
#define	POLLERR		0x008
#define	POLLHUP		0x010
#define	POLLNVAL	0x020

#define	POLLRDNORM	0x040
#define	POLLRDBAND	0x080
#define	POLLWRNORM	POLLOUT
#define	POLLWRBAND	0x100

#define	INFTIM	-1

#ifdef __cplusplus
extern "C" {
#endif

extern int poll(struct pollfd fds[], nfds_t nfds, int timeout);

#ifdef __cplusplus
}
#endif

#endif
