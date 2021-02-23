#ifndef __NETDB_H__
#define __NETDB_H__

#ifndef _BSD_EXTENSION
    This header file is an extension to ANSI/POSIX
#endif

#pragma lib "/$M/lib/ape/libbsd.a"

/*-
 * Copyright (c) 1980, 1983, 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that: (1) source distributions retain this entire copyright
 * notice and comment, and (2) distributions including binaries display
 * the following acknowledgement:  ``This product includes software
 * developed by the University of California, Berkeley and its contributors''
 * in the documentation or other materials provided with the distribution
 * and in all advertising materials mentioning features or use of this
 * software. Neither the name of the University nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	@(#)netdb.h	5.11 (Berkeley) 5/21/90
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Structures returned by network data base library.  All addresses are
 * supplied in host order, and returned in network order (suitable for
 * use in system calls).
 */
struct	hostent {
	char	*h_name;	/* official name of host */
	char	**h_aliases;	/* alias list */
	int	h_addrtype;	/* host address type */
	int	h_length;	/* length of address */
	char	**h_addr_list;	/* list of addresses from name server */
#define	h_addr	h_addr_list[0]	/* address, for backward compatiblity */
};

/*
 * Assumption here is that a network number
 * fits in 32 bits -- probably a poor one.
 */
struct	netent {
	char		*n_name;	/* official name of net */
	char		**n_aliases;	/* alias list */
	int		n_addrtype;	/* net address type */
	unsigned long	n_net;		/* network # */
};

struct	servent {
	char	*s_name;	/* official service name */
	char	**s_aliases;	/* alias list */
	int	s_port;		/* port # */
	char	*s_proto;	/* protocol to use */
};

struct	protoent {
	char	*p_name;	/* official protocol name */
	char	**p_aliases;	/* alias list */
	int	p_proto;	/* protocol # */
};

/* from 4.0 RPCSRC */
struct rpcent {
	char	*r_name;	/* name of server for this rpc program */
	char	**r_aliases;	/* alias list */
	int	r_number;	/* rpc program number */
};

extern struct hostent	*gethostbyname(const char *),
			*gethostbyaddr(const void *, int, int),
			*gethostent(void);
extern struct netent	*getnetbyname(const char *),
			*getnetbyaddr(long, int),
			*getnetent(void);
extern struct servent	*getservbyname(const char *, const char *),
			*getservbyport(int, const char *),
			*getservent(void);
extern struct protoent	*getprotobyname(const char *),
			*getprotobynumber(int),
			*getprotoent(void);
extern struct rpcent	*getrpcbyname(const char *), 
			*getrpcbynumber(int), 
			*getrpcent(void);
extern void sethostent(int),  endhostent(void),
	    setnetent(int),   endnetent(void),
	    setservent(int),  endservent(void),
	    setprotoent(int), endprotoent(void),
	    setrpcent(int),   endrpcent(void);

/*
 * Error return codes from gethostbyname() and gethostbyaddr()
 * (left in extern int h_errno).
 */
extern int h_errno;
extern void herror(const char *);
extern char *hstrerror(int);

#define	HOST_NOT_FOUND	1 /* Authoritative Answer Host not found */
#define	TRY_AGAIN	2 /* Non-Authoritive Host not found, or SERVERFAIL */
#define	NO_RECOVERY	3 /* Non recoverable errors, FORMERR, REFUSED, NOTIMP */
#define	NO_DATA		4 /* Valid name, no data record of requested type */
#define	NO_ADDRESS	NO_DATA		/* no address, look for MX record */

#define __HOST_SVC_NOT_AVAIL 99		/* libc internal use only */

enum {
	AI_PASSIVE		= 0x01,
	AI_CANONNAME	= 0x02,
	AI_NUMERICHOST	= 0x04,		/* ignored */
	AI_V4MAPPED		= 0x08,
	AI_ALL			= 0x10,		/* ignored */
	AI_ADDRCONFIG	= 0x20,		/* ignored */
	AI_NUMERICSERV	= 0x400,
};

#define	AI_PASSIVE		AI_PASSIVE
#define	AI_CANONNAME	AI_CANONNAME
#define	AI_NUMERICHOST	AI_NUMERICHOST
#define	AI_V4MAPPED		AI_V4MAPPED
#define	AI_ALL			AI_ALL
#define	AI_ADDRCONFIG	AI_ADDRCONFIG
#define	AI_NUMERICSERV	AI_NUMERICSERV

enum {
	EAI_BADFLAGS	= -1,
	EAI_NONAME		= -2,
	EAI_AGAIN		= -3,
	EAI_FAIL		= -4,
	EAI_FAMILY		= -6,
	EAI_SOCKTYPE		= -7,
	EAI_SERVICE		= -8,
	EAI_MEMORY		= -10,
	EAI_SYSTEM		= -11,
	EAI_OVERFLOW	= -12,
};
#define	EAI_BADFLAGS	EAI_BADFLAGS
#define	EAI_NONAME		EAI_NONAME
#define	EAI_AGAIN		EAI_AGAIN
#define	EAI_FAIL		EAI_FAIL
#define	EAI_FAMILY		EAI_FAMILY
#define	EAI_SOCKTYPE		EAI_SOCKTYPE
#define	EAI_SERVICE		EAI_SERVICE
#define	EAI_MEMORY		EAI_MEMORY
#define	EAI_SYSTEM		EAI_SYSTEM
#define	EAI_OVERFLOW	EAI_OVERFLOW

enum {
	NI_NUMERICHOST	= 1<<0,
	NI_NUMERICSERV	= 1<<1,
	NI_NOFQDN		= 1<<2,
	NI_NAMEREQD		= 1<<3,
	NI_DGRAM		= 1<<4,
};
#define	NI_NUMERICHOST	NI_NUMERICHOST
#define	NI_NUMERICSERV	NI_NUMERICSERV
#define	NI_NOFQDN		NI_NOFQDN
#define	NI_NAMEREQD		NI_NAMEREQD
#define	NI_DGRAM		NI_DGRAM

struct addrinfo {
	int		ai_flags;
	int		ai_family;
	int		ai_socktype;
	int		ai_protocol;
	int		ai_addrlen;
	struct sockaddr	*ai_addr;
	char		*ai_canonname;
	struct addrinfo	*ai_next;
};

/* _SUSV2_SOURCE? */
int		getaddrinfo(const char*, const char*, const struct addrinfo*, struct addrinfo**);
void		freeaddrinfo(struct addrinfo *);
int		getnameinfo(const struct sockaddr *, int, char*, long, char*, long, int);
const char*	gai_strerror(int);

#ifdef __cplusplus
}
#endif

#endif /* !__NETDB_H__ */
