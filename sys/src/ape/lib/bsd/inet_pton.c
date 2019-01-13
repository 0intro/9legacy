#define _SUSV2_SOURCE
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define nil ((void*)0)
typedef unsigned char uchar;

int classnetbytes[] = {1, 1, 2, 3, };

static char*
parseip4(uchar *u, char *s)
{
	char *v;
	int i, d, b, n;

	for(i = 0; i < 4; i++){
		v = s;
		d = strtoul(s, &s, 0);
		if(d > 255)
			return nil;
		u[i] = d;
		if(*s == '.')
			s++;
		else if(s == v)
			break;
	}
	if(i < 4 && i > (b = classnetbytes[u[0]>>6])){
		n = i - b;
		memmove(u+4-n, u+b, n);
		memset(u+b, 0, 4-(b+n));
		i = 4;
	}
	if(i != 4)
		return nil;
	return s;
}

static char*
parseip6(uchar *u, char *s)
{
	char *v;
	int i, d, cc, is4;

	is4 = 1;
	cc = -1;
	for(i = 0; i < 16;){
		v = s;
		d = strtoul(s, &s, 16);
		switch(*s){
		case '.':
			if(i + 4 > 16)
				return nil;
			s = parseip4(u+i, v);
			if(s == nil)
				return nil;
			i += 4;
			break;
		case ':':
			is4 = 0;
			s++;
		default:
			if(d > 0xffff)
				return nil;
			u[i++] = d>>8;
			u[i++] = d;
			if(*s == ':'){
				if(cc != -1)
					return nil;
				cc = i;
				s++;
			}
			if(s == v)
				i -= 2;
			break;
		}
		if(s == v)
			break;
	}
	if(is4 && i == 4){
		memmove(u+12, u, 4);
		memset(u, 0, 4);
		memset(u+10, 0xff, 2);
	}
	else if(cc != -1 && i < 16){
		memmove(u+cc+(16-i), u+cc, i-cc);
		memset(u+cc, 0, 16-i);
	}
	else if(i != 16)
		return nil;
	return s;
}

int
inet_pton(int af, const char *src, void *dst)
{
	uchar u[16];
	struct in_addr *ia;
	struct in6_addr *ia6;

	memset(u, 0, sizeof u);
	switch(af){
	default:
		errno = EAFNOSUPPORT;
		return -1;
	case AF_INET:
		if(parseip4(u, src) == nil)
			return 0;
		ia = (struct in_addr*)dst;
		memmove(&ia->s_addr, u, 4);
		return 1;
	case AF_INET6:
		if(parseip6(u, src) == nil)
			return 0;
		ia6 = (struct in6_addr*)dst;
		memmove(ia6->s6_addr, u, 16);
		return 1;
	}
}
