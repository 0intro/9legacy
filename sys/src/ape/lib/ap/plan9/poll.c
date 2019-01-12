#include <stdio.h>
#include <sys/time.h>
#include <sys/select.h>
#include <poll.h>

int
poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	int n;
	fd_set rfds, wfds, efds;
	struct timeval w, *wp;
	struct pollfd *p, *ep;

	n = -1;
	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_ZERO(&efds);

	ep = fds + nfds;
	for(p = fds; p < ep; p++){
		if(p->fd < 0)
			continue;
		if((p->events&(POLLIN|POLLPRI|POLLOUT)) == 0)
			continue;
		if(p->events&POLLIN)
			FD_SET(p->fd, &rfds);
		if(p->events&POLLOUT)
			FD_SET(p->fd, &wfds);
		if(p->events&(POLLPRI))
			FD_SET(p->fd, &efds);
		if(p->fd > n)
			n = p->fd;
	}
	wp = NULL;
	if(timeout >= 0){
		w.tv_sec = timeout/1000;
		w.tv_usec = (timeout%1000)*1000;
		wp = &w;
	}
	n = select(n+1, &rfds, &wfds, &efds, wp);
	if(n < 0)
		return -1;

	/* POLLHUP means the socket is no longer connected. (FIN) */
	/* POLLERR means the socket got an asynchronous error. (RST) */

	for(p = fds; p < ep; p++){
		p->revents = 0;
		if(FD_ISSET(p->fd, &rfds))
			p->revents |= POLLIN;
		if(FD_ISSET(p->fd, &wfds))
			p->revents |= POLLOUT;
		if(FD_ISSET(p->fd, &efds))
			p->revents |= POLLPRI|POLLHUP;
	}
	return n;
}
