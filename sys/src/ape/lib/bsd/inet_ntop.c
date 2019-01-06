#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

enum {
	IPaddrlen = 16
};

#define CLASS(x)	(x[0]>>6)

static unsigned char v4prefix[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0xff, 0xff,
	0, 0, 0, 0
};

const char *
inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
	char buf[5*8];
	static char *ifmt = "%d.%d.%d.%d";
	unsigned char *p;
	unsigned short s;
	int i, j, n, eln, eli;

	switch(af){
	case AF_INET:
		p = (unsigned char *)src;
	v4:
		n = snprintf(buf, sizeof buf, ifmt, p[0], p[1], p[2], p[3]);
		if(n >= size){
			errno = ENOSPC;
			return NULL;
		}
		return strcpy(dst, buf);
	case AF_INET6:
		p = (unsigned char *)src;
		if(memcmp(p, v4prefix, 12) == 0){
			p += 12;
			goto v4;
		}

		/* find longest elision */
		eln = eli = -1;
		for(i = 0; i < 16; i += 2){
			for(j = i; i < 16; j += 2)
				if(p[j] != 0 || p[j+1] != 0)
					break;
			if(j > i && j - i > eln){
				eli = i;
				eln = j - i;
			}
		}

		/* print with possible elision */
		n = 0;
		for(i = 0; i < 16; i += 2){
			if(i == eli){
				n += sprintf(buf+n, "::");
				i += eln;
				if(i >= 16)
					break;
			}else if(i != 0)
				n += sprintf(buf+n, ":");
			s = (p[i]<<8) + p[i+1];
			n += sprintf(buf+n, "%x", s);
		}
		return strcpy(dst, buf);
	default:
		errno = EAFNOSUPPORT;
		return NULL;
	}
}

static char *
v4parseip(unsigned char *to, char *from)
{
	int i;
	char *p;

	p = from;
	for(i = 0; i < 4 && *p; i++){
		to[i] = strtoul(p, &p, 0);
		if(*p == '.')
			p++;
	}
	switch(CLASS(to)){
	case 0: /* class A */
	case 1:
		if(i == 3){
			to[3] = to[2];
			to[2] = to[1];
			to[1] = 0;
		}else if(i == 2){
			to[3] = to[1];
			to[1] = 0;
		}
		break;
	case 2: /* class B */
		if(i == 3){
			to[3] = to[2];
			to[2] = 0;
		}
		break;
	}
	return p;
}

static int
ipcharok(int c)
{
	return c == '.' || c == ':' || (isascii(c) && isxdigit(c));
}

static int
delimchar(int c)
{
	if(c == '\0')
		return 1;
	if(c == '.' || c == ':' || (isascii(c) && isalnum(c)))
		return 0;
	return 1;
}

static char *
v6parseip(unsigned char *to, char *from)
{
	int i, elipsis, v4;
	unsigned long x;
	char *p, *op;

	elipsis = 0;
	v4 = 1;
	memset(to, 0, IPaddrlen);
	p = from;
	for(i = 0; i < IPaddrlen && ipcharok(*p); i += 2){
		op = p;
		x = strtoul(p, &p, 16);
		if((*p == '.' && i <= IPaddrlen-4) || (*p == '\0' && i == 0)){
			/* ends with v4 */
			p = v4parseip(to+i, op);
			i += 4;
			break;
		}
		/* v6: at most 4 hex digits, followed by colon or delim */
		if(x != (unsigned short)x || (*p != ':' && !delimchar(*p))){
			memset(to, 0, IPaddrlen);
			return NULL; /* parse error */
		}
		to[i] = x>>8;
		to[i+1] = x;
		if(*p == ':'){
			v4 = 0;
			if(*++p == ':'){	/* :: is elided zero short(s) */
				if(elipsis){
					memset(to, 0, IPaddrlen);
					return NULL; /* second :: */
				}
				elipsis = i+2;
				p++;
			}
		}else if(p == op)	/* stroul made no progress? */
			break;
	}
	if(p == from || !delimchar(*p)){
		memset(to, 0, IPaddrlen);
		return NULL;	/* parse error */
	}
	if(i < IPaddrlen){
		memmove(&to[elipsis+IPaddrlen-1], &to[elipsis], i-elipsis);
		memset(&to[elipsis], 0, IPaddrlen-i);
	}
	if(v4)
		to[10] = to[11] = 0xff;
	return p;
}

int
inet_pton(int af, const char *src, void *dst)
{
	char *p;
	switch(af){
	case AF_INET:
		p = v4parseip(dst, src);
	case AF_INET6:
		p = v6parseip(dst, src);
	default:
		errno = EAFNOSUPPORT;
		return -1;
	}
	if(p == NULL)
		return 0;
	return 1;
}
