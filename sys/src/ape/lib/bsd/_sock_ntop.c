/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

#include "priv.h"

void
_sock_ntop(int af, const void *addr, char *ip, int nip, int *port)
{
	struct sockaddr_in *a;
	struct sockaddr_in6 *a6;

	switch(af){
	default:
		abort();
	case AF_INET:
		a = (struct sockaddr_in*)addr;
		if(port != nil)
			*port = ntohs(a->sin_port);
		if(ip != nil)
			inet_ntop(af, &a->sin_addr, ip, nip);
		break;
	case AF_INET6:
		a6 = (struct sockaddr_in6*)addr;
		if(port != nil)
			*port = ntohs(a6->sin6_port);
		if(ip != nil)
			inet_ntop(af, &a6->sin6_addr, ip, nip);
		break;
	}
}

