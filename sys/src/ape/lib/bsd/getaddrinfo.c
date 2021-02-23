#define _SUSV2_SOURCE
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <inttypes.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

static const struct addrinfo defhints = {
.ai_flags		= AI_ALL,
.ai_family	= PF_UNSPEC,
.ai_socktype	= 0,
.ai_protocol	= 0,
};

#define nil	((void*)0)
#define nelem(a)	(sizeof(a)/sizeof((a)[0]))

static int
chkfamily(int f)
{
	switch(f){
	default:
		return -1;
	case PF_UNSPEC:
	case PF_INET:
	case PF_INET6:
		return 0;
	}
}

static struct {
	char	*s;
	int	proto;
	int	type;
} sockttab[] = {
	"tcp",		IPPROTO_TCP,		SOCK_STREAM,
	"il",		IPPROTO_RAW,		SOCK_STREAM,
	"udp",		IPPROTO_UDP,		SOCK_DGRAM,
	"icmp",		IPPROTO_ICMP,		SOCK_RAW,
	"icmpv6",	IPPROTO_ICMP,		SOCK_RAW,
};

typedef struct Storage Storage;
struct Storage {
	struct addrinfo	ai;
	struct sockaddr	sa;
	char		buf[64];
};

#define Strround(s)	((strlen(s)+1)+7 & ~7)

static int
copyout(Storage *storage, int ns, int totsz, struct addrinfo *hint, struct addrinfo **res)
{
	char *p;
	unsigned char *m;
	int i;
	Storage *s;
	struct addrinfo *ai;
	struct sockaddr *sa;

	m = malloc(totsz);
	if(m == nil)
		return EAI_MEMORY;
	memset(m, 0, totsz);
	*res = (void*)m;

	for(i = 0; i < ns; i++){
		s = storage + i;
		sa = &s->sa;

		ai = (struct addrinfo*)m;
		m += sizeof *ai;
		ai->ai_addr = (struct sockaddr*)m;
		m += s->ai.ai_addrlen;

		ai->ai_addrlen = s->ai.ai_addrlen;
		memmove(ai->ai_addr, sa, s->ai.ai_addrlen);
		ai->ai_family = s->ai.ai_family;
		ai->ai_flags = hint->ai_flags;
		ai->ai_socktype = s->ai.ai_socktype;
		ai->ai_protocol = s->ai.ai_protocol;

		if(hint->ai_flags & AI_CANONNAME){
			ai->ai_canonname =(char*)m;
			p = s->buf;
			m += Strround(p);
			memcpy(ai->ai_canonname, p, strlen(p));
		}

		if(i+1 < ns)
			ai->ai_next = (struct addrinfo*)m;
	}
	return 0;
}

static int
canon(int fd, Storage *a, int ns, int *totsz, struct addrinfo *hint)
{
	char buf[128], *f[15], *p;
	int i, j, n, best, diff;
	Storage *s;

	for(i = 0; i < ns; i++){
		s = a+i;

		lseek(fd, 0, 0);
		if(s->buf[0] != 0 && (hint->ai_flags & AI_PASSIVE) == 0)
			snprintf(buf, sizeof buf, "!ipinfo ip=%s sys dom", s->buf);
		else
			snprintf(buf, sizeof buf, "!ipinfo sys dom");
		if(write(fd, buf, strlen(buf)) == -1)
			return EAI_FAIL;
		lseek(fd, 0, 0);
		n = read(fd, buf, sizeof buf-1);
		if(n <= 0)
			continue;
		buf[n] = 0;

	//	n = tokenize(buf, f, nelem(f));
		p = buf;
		best = -1;
		for(j = 0; j < n; j++){
			f[j] = p;
			p = strchr(f[j], ' ');
			if(p != nil)
				*p++ = 0;

			if(strncmp(f[j], "dom=", 4) == 0)
				break;
			if(best == 0)
			if(strncmp(f[j], "sys=", 4) == 0)
				best = i;

			if(p == nil)
				break;
		}
		if(j == n && best != -1)
			j = best;
		while((read(fd, buf, sizeof buf)) > 0)
			;

		if(j != n){
			p = strchr(f[j], '=')+1;
			diff = Strround(s->buf) - Strround(p);
			memset(s->buf, 0, sizeof s->buf);
			memcpy(s->buf, p, strlen(p));
			*totsz -= diff;
		}
	}
	return 0;
}

static int
docsquery(char *nettype, char *host, char *service, struct addrinfo *hint, struct addrinfo **res)
{
	char buf[128], *p, *q, *r, *net;
	int i, fd, rv, n, ns, af, sz, totsz;
	struct sockaddr_in *in;
	struct sockaddr_in6 *in6;
	Storage storage[6];

	fd = open("/net/cs", O_RDWR);
	if(fd < 0)
		return EAI_FAIL;

	snprintf(buf, sizeof buf, "%s!%s!%s", nettype, host, service);
	if(write(fd, buf, strlen(buf)) == -1){
		close(fd);
		return EAI_FAIL;
	}

	lseek(fd, 0, 0);
	totsz = 0;
	for(ns = 0; ns < nelem(storage);){
		n = read(fd, buf, sizeof buf - 1);
		if(n < 1)
			break;
		buf[n] = 0;
		if((p = strchr(buf, ' ')) == nil)
			continue;
		*p++ = 0;

		if(strstr(buf, "!fasttimeout") != nil)
			continue;
		if((q = strchr(p, '!')) == nil)
			q = "";
		else
			*q++ = 0;

		if(strcmp(host, "*") == 0){
			q = p;
			if(hint->ai_family == AF_INET6)
				p = "::";
			else
				p = "0.0.0.0";
		}
		if(strlen(p) >= sizeof storage[0].buf)
			continue;

		/* wrong for passive, except for icmp6 */
		af = AF_INET;
		if(strchr(p, ':') != nil)
			af = AF_INET6;

		if(hint->ai_family != AF_UNSPEC && af != hint->ai_family)
			continue;

		sz = 0;
		memset(&storage[ns], 0, sizeof(storage[ns]));
		if(hint->ai_flags & AI_CANONNAME){
			memcpy(storage[ns].buf, p, strlen(p));
			sz += Strround(p);
		}

		storage[ns].ai.ai_socktype = SOCK_DGRAM;
		net = "tcp";
		r = strrchr(buf, '/');
		if(r != nil && strcmp(r, "/clone") == 0){
			*r = 0;
			r = strrchr(buf, '/');
			if(r != nil)
				net = r+1;
		}
		for(i = 0; i < nelem(sockttab); i++)
			if(strcmp(sockttab[i].s, net) == 0)
			if(sockttab[i].proto != IPPROTO_RAW || hint->ai_protocol == IPPROTO_RAW){
				storage[ns].ai.ai_socktype = sockttab[i].type;
				storage[ns].ai.ai_protocol = sockttab[i].proto;
				break;
			}
		if(i == nelem(sockttab))
			continue;

		storage[ns].ai.ai_family = af;
		switch(af){
		case AF_INET:
			in = (struct sockaddr_in*)&storage[ns].sa;
			in->sin_family = af;
			in->sin_addr.s_addr = inet_addr(p);
			in->sin_port = ntohs(atoi(q));
			storage[ns].ai.ai_addrlen = sizeof *in;
			sz += sizeof *in;
			break;
		case AF_INET6:
			storage[ns].ai.ai_family = af;
			in6 = (struct sockaddr_in6*)&storage[ns].sa;
			in6->sin6_family = af;
			inet_pton(af, p, &in6->sin6_addr);
			in6->sin6_port = ntohs(atoi(q));
			storage[ns].ai.ai_addrlen = sizeof *in6;
			sz += sizeof *in6;
			break;
		}

		totsz += sz + sizeof(struct addrinfo);
		ns++;

		/* hacky way to get udp */
		if(strcmp(nettype, "net") == 0 && hint->ai_protocol == 0)
		if(ns < nelem(storage)){
			storage[ns] = storage[ns-1];
			storage[ns].ai.ai_protocol = IPPROTO_UDP;
			totsz += sz + sizeof(struct addrinfo);
			ns++;
		}
	}

	rv = 0;
	if(hint->ai_flags & AI_CANONNAME)
		rv = canon(fd, storage, ns, &totsz, hint);
	close(fd);

	if(rv != 0)
		return rv;
	if(ns == 0)
		return EAI_NONAME;
	return copyout(storage, ns, totsz, hint, res);
}

int
getaddrinfo(const char *node, const char *service, const struct addrinfo *hint0, struct addrinfo **res)
{
	char *nettype, *p;
	int i;
	struct addrinfo hint;

	*res = nil;

	if(node == nil && service == nil)
		return EAI_BADFLAGS;
	if(hint0 == nil)
		hint = defhints;
	else
		hint = *hint0;

	if(hint.ai_flags & AI_NUMERICSERV){
		if(service == nil)
			return EAI_BADFLAGS;
		strtoul(service, &p, 0);
		if(*p != 0)
			return EAI_NONAME;
	}

	if(chkfamily(hint.ai_family) == -1)
		return EAI_FAMILY;
	if(node != nil)
		hint.ai_flags &= ~AI_PASSIVE;

	/* not passive and no host → loopback */
	if(node == nil && (hint.ai_flags & AI_PASSIVE) == 0){
		switch(hint.ai_family){
		case PF_UNSPEC:
			hint.ai_family = AF_INET;
		case PF_INET:
			node = "127.1";
			break;
		case PF_INET6:
			node = "::1";
			break;
		}
	}
	else if (node == nil)
		node = "*";

	nettype = "net";
	switch(hint.ai_socktype){
		for(i = 0; i < nelem(sockttab); i++)
			if(sockttab[i].type == hint.ai_socktype)
				nettype = sockttab[i].s;
	}
	if(strcmp(nettype, "net") != 0 && hint.ai_protocol != 0)
		return EAI_BADFLAGS;
	if(hint.ai_protocol != 0){
		for(i = 0; i < nelem(sockttab); i++)
			if(sockttab[i].proto == hint.ai_protocol)
				nettype = sockttab[i].s;
	}
	if(hint.ai_family == PF_INET6 && strcmp(nettype, "icmp") == 0)
		nettype = "icmpv6";

	if(node == nil || *node == 0)
		node = "*";
	if(service == nil || *service == 0)
		service = "0";

	return docsquery(nettype, node, service, &hint, res);
}

void
freeaddrinfo(struct addrinfo *ai)
{
	free(ai);
}
