#define _SUSV2_SOURCE
#define _C99_SNPRINTF_EXTENSION
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <inttypes.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define nil ((void*)0)

static int
getport(char *trans, int port, char *srv, int srvlen)
{
	char buf[128], match[5], *p, *q;
	int fd, r, n;

	r = EAI_FAIL;
	fd = open("/net/cs", O_RDWR);
	if(fd < 0)
		return r;
	snprintf(buf, sizeof buf, "!port=%d %s=*", port, trans);
	snprintf(match, sizeof match, "%s=", trans);
	if(write(fd, buf, strlen(buf)) == -1){
		close(fd);
		return r;
	}

	lseek(fd, 0, 0);
	n = read(fd, buf, sizeof buf - 1);
	if(n > 0){
		buf[n] = 0;
		for(p = buf; p != nil; p = q){
			q = strchr(buf, ' ');
			if(q != nil)
				*q++ = 0;
			if(strncmp(p, match, 4) == 0){
				r = snprintf(srv, srvlen, "%s", p+4);
				break;
			}
		}
	}
	close(fd);
	return r;
}

static int
getname(int af, void *addr, char *host, long hostlen, int flags)
{
	char ipbuf[128], buf[128], *p, *q;
	int fd, r, n;

	r = EAI_FAIL;
	if(inet_ntop(af, addr, ipbuf, sizeof ipbuf) == nil)
		return EAI_SYSTEM;
	fd = open("/net/cs", O_RDWR);
	if(fd < 0)
		return r;
	snprintf(buf, sizeof buf, "!ip=%s dom=*", ipbuf);
	if(write(fd, buf, strlen(buf)) == -1){
		close(fd);
		return r;
	}

	lseek(fd, 0, 0);
	n = read(fd, buf, sizeof buf - 1);
	if(n > 0){
		buf[n] = 0;
		for(p = buf; p != nil; p = q){
			q = strchr(buf, ' ');
			if(q != nil)
				*q++ = 0;
			if(strncmp(p, "dom=", 4) == 0){
				r = snprintf(ipbuf, sizeof ipbuf, "%s", p+4);
				break;
			}
		}
	}
	close(fd);

	if(r >= 0){
		if(flags & NI_NOFQDN){
			p = strchr(ipbuf, '.');
			if(p != nil)
				*p = 0;
		}
		r = EAI_OVERFLOW;
		if(snprintf(host, hostlen, "%s", ipbuf) >= 0)
			r = 0;
	}
	return r;
}

static int
afhinfo(int af, void *addr, int port, char *host, long hostlen, char *srv, long srvlen, int flags)
{
	char *trans;
	int r;

	trans = "tcp";
	if(flags & NI_DGRAM)
		trans = "udp";

	if(flags & NI_NUMERICSERV || getport(trans, port, srv, srvlen) < 0)
		snprintf(srv, srvlen, "%d", port);

	/* require name even if not returning it */
	if(flags & NI_NAMEREQD){
		r = getname(af, addr, host, hostlen, flags);
		if(r < 0)
			return r;
	}
	if(flags & NI_NUMERICHOST){
		if(inet_ntop(af, addr, host, hostlen) == nil)
			return EAI_SYSTEM;
		return 0;
	}
	if(getname(af, addr, host, hostlen, flags) == 0)
		return 0;
	return 0;
}

int
getnameinfo(const struct sockaddr *sa, int len, char *host, long hostlen, char *srv, long srvlen, int flags)
{
	char fakehost[64], fakesrv[16];
	struct sockaddr_in *in;
	struct sockaddr_in6 *in6;

	if(host != nil && hostlen != 0)
		*host = 0;
	else{
		host = fakehost;
		hostlen = sizeof fakehost;
	}
	if(srv != nil && hostlen != 0)
		*srv = 0;
	else{
		srv = fakesrv;
		srvlen = sizeof fakesrv;
	}

	switch(sa->sa_family){
	default:
		return EAI_SOCKTYPE;
	case AF_INET:
		if(len < sizeof *in)
			return EAI_OVERFLOW;
		in = (struct sockaddr_in*)sa;
		return afhinfo(AF_INET, &in->sin_addr,  ntohs(in->sin_port),
			host, hostlen, srv, srvlen, flags);
	case AF_INET6:
		if(len < sizeof *in6)
			return EAI_OVERFLOW;
		in6 = (struct sockaddr_in6*)sa;
		return afhinfo(AF_INET6, &in6->sin6_addr,  ntohs(in6->sin6_port),
			host, hostlen, srv, srvlen, flags);
	}
}
