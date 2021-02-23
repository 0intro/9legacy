#define _SUSV2_SOURCE
#define _C99_SNPRINTF_EXTENSION
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

const char*
inet_ntop(int af, const void *src, char *dst, int size)
{
	char map[16/2], buf[32], *c;
	int d, j, n, run, srun, maxrun, maxsrun;
	uint8_t *u;
	uint32_t i;
	struct in_addr *ia;
	struct in6_addr *ia6;

	switch(af){
	default:
		errno = EAFNOSUPPORT;
		return nil;
	case AF_INET:
		ia = (struct in_addr*)src;
		i = ia->s_addr;
		i = ntohl(i);
		n = snprintf(dst, size, "%d.%d.%d.%d",
			i>>24, i>>16 & 0xff, i>>8 & 0xff, i & 0xff);
		if(n == -1){
			errno = ENOSPC;
			return nil;
		}
		return dst;
	case AF_INET6:
		ia6 = (struct in6_addr*)src;
		u = ia6->s6_addr;

		srun = run = 0;
		maxrun = maxsrun = 0;
		for(i = 0; i < 16; i += 2){
			if((u[i]|u[i+1]) == 0){
				map[i/2] = 1;
				if(run == 0)
					srun = i/2;
				run++;
			}else{
				if(run > 0 && run > maxrun){
					maxrun = run;
					maxsrun = srun;
				}
				srun = run = 0;
			}
		}
		if(run > 0 && run > maxrun){
			maxrun = run;
			maxsrun = srun;
		}

		/* buf must be bigger than biggest address, else -1 return gets us */
		memset(buf, 0, sizeof buf);
		j = 0;
		c = ":";
		for(i = 0; i < 8;){
			if(map[i] && i == maxsrun){
				j += snprintf(buf+j, sizeof buf-j, "%s:", c);
				c = "";
				i += maxrun;
			}else{
				d = u[i*2]<<8 | u[i*2+1];
				c = i<7? ":": "";
				j += snprintf(buf+j, sizeof buf-j, "%x%s", d, c);
				c = "";
				i++;
			}
		}
		if(strlen(buf)+1 > size){
			errno = ENOSPC;
			return nil;
		}
		memcpy(dst, buf, strlen(buf)+1);
		return dst;
	}
}
