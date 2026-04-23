/*
 * bcm2712 (raspberry pi 5 architecture-specific stuff
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"
#include "ureg.h"
#include "ureg32.h"

#include "../port/netif.h"
#include "etherif.h"

typedef struct Mbox Mbox;
typedef struct Mboxes Mboxes;

#define	POWERREGS	(VIRTIO+0x7d200000)

Soc soc = {
	.pcispace	= 0x1C00000000ull,
};

enum {
	Wdogfreq	= 65536,
	Wdogtime	= 10,	/* seconds, ≤ 15 */
};

/*
 * Power management / watchdog registers
 */
enum {
	Rstc		= 0x1c>>2,
		Password	= 0x5A<<24,
		CfgMask		= 0x03<<4,
		CfgReset	= 0x02<<4,
	Rsts		= 0x20>>2,
	Wdog		= 0x24>>2,
};

static Lock startlock[MAXMACH];

void
archreset(void)
{
	fpon();
}

void
archreboot(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Wdog] = Password | 1;
	r[Rstc] = Password | (r[Rstc] & ~CfgMask) | CfgReset;
	coherence();
	for(;;)
		;
}

void
wdogfeed(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Wdog] = Password | (Wdogtime * Wdogfreq);
	r[Rstc] = Password | (r[Rstc] & ~CfgMask) | CfgReset;
}

void
wdogoff(void)
{
	u32int *r;

	r = (u32int*)POWERREGS;
	r[Rstc] = Password | (r[Rstc] & ~CfgMask);
}


char *
cputype2name(char *buf, int size)
{
	u32int r;
	uint part;
	char *p;

	r = cpidget();			/* main id register */
	assert((r >> 24) == 'A');
	part = (r >> 4) & MASK(12);
	switch(part){
	case 0xc07:
		p = seprint(buf, buf + size, "Cortex-A7");
		break;
	case 0xd03:
		p = seprint(buf, buf + size, "Cortex-A53");
		break;
	case 0xd08:
		p = seprint(buf, buf + size, "Cortex-A72");
		break;
	case 0xd0b:
		p = seprint(buf, buf + size, "Cortex-A76");
		break;
	default:
		p = seprint(buf, buf + size, "Unknown-%#x", part);
		break;
	}
	seprint(p, buf + size, " r%ldp%ld",
		(r >> 20) & MASK(4), r & MASK(4));
	return buf;
}

void
cpuidprint(void)
{
	char name[64];

	cputype2name(name, sizeof name);
	delay(50);				/* let uart catch up */
	print("cpu%d: %dMHz ARM %s\n", m->machno, m->cpumhz, name);
}

int
getncpus(void)
{
	int n, max;
	char *p;

	n = 4;
	if(n > MAXMACH)
		n = MAXMACH;
	p = getconf("*ncpu");
	if(p && (max = atoi(p)) > 0 && n > max)
		n = max;
	return n;
}

int
startcpus(uint ncpu)
{
	int i, timeout;
	extern uintptr startcpu(int);

	for(i = 1; i < ncpu; i++)
		lock(&startlock[i]);
	for(i = 1; i < ncpu; i++)
		if(canlock(&startlock[i]))
			print("wtf? lock%d\n", i);
	cachedwbse(startlock, sizeof startlock);
	for(i = 1; i < ncpu; i++){
		startcpu(i);
		timeout = 100000000;
		while(!canlock(&startlock[i]))
			if(--timeout == 0)
				return i;
		unlock(&startlock[i]);
	}
	return ncpu;
}

void
rdbstart(void)
{
	wdogoff();
	rdb();
}

uchar ether0mac[Eaddrlen];

int
archether(unsigned ctlrno, Ether *ether)
{
	switch(ctlrno){
	case 0:
		ether->type = "gem";
		memmove(ether->ea, ether0mac, Eaddrlen);
		break;
	case 1:
		ether->type = "4330";
		break;
	default:
		return 0;
	}
	ether->ctlrno = ctlrno;
	ether->irq = -1;
	ether->nopt = 0;
	ether->maxmtu = 9014;
	return 1;
}

int
cmpswap(long *addr, long old, long new)
{
	return cas((ulong*)addr, old, new);
}

void
cpustart(int cpu)
{
	void machon(int);

	up = nil;
	machinit();
	trapinit();
	clockinit();
	mmuinit1();
	timersinit();
	cpuidprint();
	archreset();
	machon(m->machno);
	unlock(&startlock[cpu]);
	schedinit();
	panic("schedinit returned");
}

static void
_ureg64to32(void *a, Ureg *src)
{
	int i;
	Ureg32 *dst;

	dst = a;
	for(i = 0; i < 15; i++)
		((u32int*)&dst->r0)[i] = ((u64int*)&src->r0)[i];
	dst->type = src->type;
	dst->psr = src->psr;
	dst->pc = src->pc;
}

static void
_ureg32to64(Ureg *dst, void *a)
{
	int i;
	Ureg32 *src;

	src = a;
	for(i = 0; i < 15; i++)
		((u64int*)&dst->r0)[i] = ((u32int*)&src->r0)[i];
	dst->type = src->type;
	dst->psr = src->psr;
	dst->pc = src->pc;
}

void
archbcm5link(void)
{
	ureg64to32 = _ureg64to32;
	ureg32to64 = _ureg32to64;
	ureg32size = sizeof(Ureg32);
	addclock0link(wdogfeed, HZ);
}
