typedef struct Netaddr	Netaddr;
typedef struct Netfile	Netfile;
typedef struct Netif	Netif;

enum
{
	Nmaxaddr=	64,
	Nmhash=		31,
	Niqlim=		128,
	Noqlim=		4096,

	Ncloneqid=	1,
	Naddrqid,
	N2ndqid,
	N3rdqid,
	Ndataqid,
	Nctlqid,
	Nstatqid,
	N3statqid,
	Ntypeqid,
	Nifstatqid,
	Nmtuqid,
	Nmaxmtuqid,
};

/*
 *  Macros to manage Qid's used for multiplexed devices
 */
#define NETTYPE(x)	(((ulong)x)&0x1f)
#define NETID(x)	((((ulong)x))>>5)
#define NETQID(i,t)	((((ulong)i)<<5)|(t))

/*
 *  one per multiplexed connection
 */
struct Netfile
{
	QLock;

	int	inuse;
	ulong	mode;
	char	owner[KNAMELEN];

	int	type;			/* multiplexor type */
	int	prom;			/* promiscuous mode */
	int	scan;			/* base station scanning interval */
	int	bridge;			/* bridge mode */
	int	vlan;			/* treat 802.1Q frames as inner frame type*/
	int	headersonly;		/* headers only - no data */
	uchar	maddr[8];		/* bitmask of multicast addresses requested */
	int	nmaddr;			/* number of multicast addresses */
	int	fat;			/* frame at a time */

	uint	inoverflows;		/* software input overflows */

	Queue*	iq;			/* input */
};

/*
 *  a network address
 */
struct Netaddr
{
	Netaddr	*next;			/* allocation chain */
	Netaddr	*hnext;
	uchar	addr[Nmaxaddr];
	int	ref;
};

/*
 *  a network interface
 */
struct Netif
{
	QLock;

	int	inited;

	/* multiplexing */
	char	name[KNAMELEN];		/* for top level directory */
	int	nfile;			/* max number of Netfiles */
	Netfile	**f;

	/* about net */
	int	limit;			/* flow control */
	int	alen;			/* address length */
	int	mbps;			/* megabits per sec */
	int	link;			/* link status */
	int	minmtu;
	int 	maxmtu;
	int	mtu;
	uchar	addr[Nmaxaddr];
	uchar	bcast[Nmaxaddr];
	Netaddr	*maddr;			/* known multicast addresses */
	int	nmaddr;			/* number of known multicast addresses */
	Netaddr *mhash[Nmhash];		/* hash table of multicast addresses */
	int	prom;			/* number of promiscuous opens */
	int	scan;			/* number of base station scanners */
	int	all;			/* number of -1 multiplexors */

	Queue*	oq;			/* output */

	/* statistics */
	uint	misses;
	uvlong	inpackets;
	uvlong	outpackets;
	uint	crcs;			/* input crc errors */
	uint	oerrs;			/* output errors */
	uint	frames;			/* framing errors */
	uint	overflows;		/* packet overflows */
	uint	buffs;			/* buffering errors */
	uint	inoverflows;	/* software overflow on input */
	uint	outoverflows;	/* software overflow on output */
	uint	loopbacks;	/* loopback packets processed */

	/* routines for touching the hardware */
	void	*arg;
	void	(*promiscuous)(void*, int);
	void	(*multicast)(void*, uchar*, int);
	int	(*hwmtu)(void*, int);	/* get/set mtu */
	void	(*scanbs)(void*, uint);	/* scan for base stations */
};

void	netifinit(Netif*, char*, int, ulong);
Walkqid*	netifwalk(Netif*, Chan*, Chan*, char **, int);
Chan*	netifopen(Netif*, Chan*, int);
void	netifclose(Netif*, Chan*);
long	netifread(Netif*, Chan*, void*, long, vlong);
Block*	netifbread(Netif*, Chan*, long, vlong);
long	netifwrite(Netif*, Chan*, void*, long);
long	netifwstat(Netif*, Chan*, uchar*, long);
long	netifstat(Netif*, Chan*, uchar*, long);
int	activemulti(Netif*, uchar*, int);
void	netifbypass(Netif*, Chan*, void (*)(void*, Block*), void*);
