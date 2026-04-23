/*
 * Adapted from ../teg2/trap.c
 *
 * arm mpcore generic interrupt controller (gic) v1
 * traps, exceptions, interrupts, system calls.
 *
 * there are two pieces: the interrupt distributor and the cpu interface.
 *
 * memset or memmove on any of the distributor registers generates an
 * exception like this one:
 *	panic: external abort 0x28 pc 0xc048bf68 addr 0x50041800
 *
 * we use l1 and l2 cache ops to force vectors to be visible everywhere.
 *
 * apparently irqs 0—15 (SGIs) are always enabled.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#include "ureg.h"

#define IRQLOCAL(irq)	((irq) - IRQlocal + 16)
#define IRQGLOBAL(irq)	((irq) + 64 + 32)

#define ISSGI(irq)	((uint)(irq) < Nppi)

enum {
	Debug = 0,

	Intrdist = 0x7fff9000,
	Intrcpu = 0x7fffa000,

	Nvec = 8,		/* # of vectors at start of lexception.s */
	Bi2long = BI2BY * sizeof(long),
	Nirqs = 1024,
	Nsgi =	16,		/* software-generated (inter-processor) intrs */
	Nppi =	32,		/* sgis + other private peripheral intrs */
};

typedef struct Intrcpuregs Intrcpuregs;
typedef struct Intrdistregs Intrdistregs;

/*
 * almost this entire register set is buggered.
 * the distributor is supposed to be per-system, not per-cpu,
 * yet some registers are banked per-cpu, as marked.
 */
struct Intrdistregs {			/* distributor */
	ulong	ctl;
	ulong	ctlrtype;
	ulong	distid;
	uchar	_pad0[0x80 - 0xc];

	/* botch: *[0] are banked per-cpu from here */
	/* bit maps */
	ulong	grp[32];		/* in group 1 (non-secure) */
	ulong	setena[32];		/* forward to cpu interfaces */
	ulong	clrena[32];
	ulong	setpend[32];
	ulong	clrpend[32];
	ulong	setact[32];		/* active? */
	ulong	clract[32];
	/* botch: *[0] are banked per-cpu until here */

	uchar	pri[1020];	/* botch: pri[0] — pri[7] are banked per-cpu */
	ulong	_rsrvd1;
	/* botch: targ[0] through targ[7] are banked per-cpu and RO */
	uchar	targ[1020];	/* byte bit maps: cpu targets indexed by intr */
	ulong	_rsrvd2;
	/* botch: cfg[1] is banked per-cpu */
	ulong	cfg[64];		/* bit pairs: edge? 1-N? */
	ulong	_pad1[64];
	ulong	nsac[64];		/* bit pairs (v2 only) */

	/* software-generated intrs (a.k.a. sgi) */
	ulong	swgen;			/* intr targets */
	uchar	_pad2[0xf10 - 0xf04];
	uchar	clrsgipend[16];		/* bit map (v2 only) */
	uchar	setsgipend[16];		/* bit map (v2 only) */
};

enum {
	/* ctl bits */
	Forw2cpuif =	1,

	/* ctlrtype bits */
	Cpunoshft =	5,
	Cpunomask =	MASK(3),
	Intrlines =	MASK(5),

	/* cfg bits */
	Level =		0<<1,
	Edge =		1<<1,		/* edge-, not level-sensitive */
	Toall =		0<<0,
	To1 =		1<<0,		/* vs. to all */

	/* swgen bits */
	Totargets =	0,
	Tonotme =	1<<24,
	Tome =		2<<24,
};

/* each cpu sees its own registers at the same base address ((ARMLOCAL+Intrcpu)) */
struct Intrcpuregs {
	ulong	ctl;
	ulong	primask;

	ulong	binpt;			/* group pri vs subpri split */
	ulong	ack;
	ulong	end;
	ulong	runpri;
	ulong	hipripend;

	/* aliased regs (secure, for group 1) */
	ulong	alibinpt;
	ulong	aliack;			/* (v2 only) */
	ulong	aliend;			/* (v2 only) */
	ulong	alihipripend;		/* (v2 only) */

	uchar	_pad0[0xd0 - 0x2c];
	ulong	actpri[4];		/* (v2 only) */
	ulong	nsactpri[4];		/* (v2 only) */

	uchar	_pad0[0xfc - 0xf0];
	ulong	ifid;			/* ro */

	uchar	_pad0[0x1000 - 0x100];
	ulong	deact;			/* wo (v2 only) */
};

enum {
	/* ctl bits */
	Enable =	1,
	Eoinodeact =	1<<9,		/* (v2 only) */

	/* (ali) ack/end/hipriend/deact bits */
	Intrmask =	MASK(10),
	Cpuidshift =	10,
	Cpuidmask =	MASK(3),

	/* ifid bits */
	Archversshift =	16,
	Archversmask =	MASK(4),
};

/* secondary interrupt controller */

#define RP1INTC	(VIRTIO + 0x80108000ull)

enum {
	StatLo	= 0x108/4,
	StatHi	= 0x10C/4,
	Set		= 0x808/4,
	Clr		= 0xC08/4,
		Ienable		= 1<<0,
		Iack		= 1<<1,
		Iackenable	= 1<<3,
};

typedef struct Vctl Vctl;
typedef struct Vctl {
	Vctl*	next;		/* handlers on this vector */
	char	*name;		/* of driver, xallocated */
	void	(*f)(Ureg*, void*);	/* handler to call */
	void*	a;		/* argument to call it with */
} Vctl;

static Lock vctllock;
static Vctl* vctl[Nirqs];

/*
 *   Layout at virtual address 0.
 */
typedef struct Vpage0 {
	void	(*vectors[Nvec])(void);
	u32int	vtable[Nvec];
} Vpage0;

enum
{
	Ntimevec = 20		/* number of time buckets for each intr */
};
ulong intrtimes[Nirqs][Ntimevec];

int irqtooearly = 0;

static ulong shadena[32];	/* copy of enable bits, saved by intcmaskall */
static Lock distlock;

extern int notify(Ureg*);

static void dumpstackwithureg(Ureg *ureg);
static int doirq(Ureg*, int);

void
printrs(int base, ulong word)
{
	int bit;

	for (bit = 0; word; bit++, word >>= 1)
		if (word & 1)
			iprint(" %d", base + bit);
}

void
dumpintrs(char *what, ulong *bits)
{
	int i, first, some;
	ulong word;
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	first = 1;
	some = 0;
	USED(idp);
	for (i = 0; i < nelem(idp->setpend); i++) {
		word = bits[i];
		if (word) {
			if (first) {
				first = 0;
				iprint("%s", what);
			}
			some = 1;
			printrs(i * Bi2long, word);
		}
	}
	if (!some)
		iprint("%s none", what);
	iprint("\n");
}

void
dumpintrpend(void)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	iprint("\ncpu%d gic regs:\n", m->machno);
	dumpintrs("group 1", idp->grp);
	dumpintrs("enabled", idp->setena);
	dumpintrs("pending", idp->setpend);
	dumpintrs("active ", idp->setact);
}

/*
 *  keep histogram of interrupt service times
 */
void
intrtime(Mach*, int vno)
{
	ulong diff;
	ulong x;

	x = perfticks();
	diff = x - m->perf.intrts;
	m->perf.intrts = x;

	m->perf.inintr += diff;
	if(up == nil && m->perf.inidle > diff)
		m->perf.inidle -= diff;

	if (m->cpumhz == 0)
		return;			/* don't divide by zero */
	diff /= m->cpumhz*100;		/* quantum = 100µsec */
	if(diff >= Ntimevec)
		diff = Ntimevec-1;
	if ((uint)vno >= Nirqs)
		vno = Nirqs-1;
	intrtimes[vno][diff]++;
}

static ulong
intack(Intrcpuregs *icp)
{
	return icp->ack & Intrmask;
}

static void
intdismiss(Intrcpuregs *icp, ulong ack)
{
	icp->end = ack;
	coherence();
}

static int
irqinuse(uint irq)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	return idp->setena[irq / Bi2long] & (1 << (irq % Bi2long));
}

void
intcunmask(uint irq)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	ilock(&distlock);
	idp->setena[irq / Bi2long] = 1 << (irq % Bi2long);
	iunlock(&distlock);
}

void
intcmask(uint irq)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	ilock(&distlock);
	idp->clrena[irq / Bi2long] = 1 << (irq % Bi2long);
	iunlock(&distlock);
}

static void
intcmaskall(Intrdistregs *idp)		/* mask all intrs for all cpus */
{
	int i;

	for (i = 0; i < nelem(idp->setena); i++)
		shadena[i] = idp->setena[i];
	for (i = 0; i < nelem(idp->clrena); i++)
		idp->clrena[i] = ~0;
	coherence();
}

static void
intcunmaskall(Intrdistregs *idp)	/* unused */
{
	int i;

	for (i = 0; i < nelem(idp->setena); i++)
		idp->setena[i] = shadena[i];
	coherence();
}

static ulong
permintrs(Intrdistregs *idp, int base, int r)
{
	ulong perms;

	idp->clrena[r] = ~0;		/* disable all */
	coherence();
	perms = idp->clrena[r];
	if (perms) {
		iprint("perm intrs:");
		printrs(base, perms);
		iprint("\n");
	}
	return perms;
}

static void
intrcfg(Intrdistregs *idp)
{
	int i, cpumask;
	ulong pat;

	/* set up all interrupts as level-sensitive, to one cpu (0) */
	pat = 0;
	for (i = 0; i < Bi2long; i += 2)
		pat |= (Level | To1) << i;

	if (m->machno == 0) {			/* system-wide & cpu0 cfg */
		for (i = 0; i < nelem(idp->grp); i++)
			idp->grp[i] = 0;		/* secure */
		for (i = 0; i < nelem(idp->pri); i++)
			idp->pri[i] = 0;		/* highest priority */
		/* set up all interrupts as level-sensitive, to one cpu (0) */
		for (i = 0; i < nelem(idp->cfg); i++)
			idp->cfg[i] = pat;
		/* first Nppi are read-only for SGIs and PPIs */
		cpumask = 1<<0;				/* just cpu 0 */
		for (i = Nppi; i < sizeof idp->targ; i++)
			idp->targ[i] = cpumask;
		coherence();

		intcmaskall(idp);
		for (i = 0; i < nelem(idp->clrena); i++) {
			// permintrs(idp, i * Bi2long, i);
			idp->clrpend[i] = idp->clract[i] = idp->clrena[i] = ~0;
		}
	} else {				/* per-cpu config */
		idp->grp[0] = 0;		/* secure */
		for (i = 0; i < 8; i++)
			idp->pri[i] = 0;	/* highest priority */
		/* idp->targ[0 through Nppi-1] are supposed to be read-only */
		for (i = 0; i < Nppi; i++)
			idp->targ[i] = 1<<m->machno;
		idp->cfg[1] = pat;
		coherence();

		// permintrs(idp, i * Bi2long, i);
		idp->clrpend[0] = idp->clract[0] = idp->clrena[0] = ~0;
		/* on cpu1, irq Extpmuirq (118) is always pending here */
	}
	coherence();
}

void
intrto(int cpu, int irq)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	/* first Nppi are read-only for SGIs and the like */
	ilock(&distlock);
	idp->targ[irq] = 1 << cpu;
	iunlock(&distlock);
}

void
intrsto(int cpu)			/* unused */
{
	int i;
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	/* first Nppi are read-only for SGIs and the like */
	for (i = Nppi; i < sizeof idp->targ; i++)
		intrto(cpu, i);
	USED(idp);
}

void
intrcpu(int cpu)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);

	ilock(&distlock);
	idp->swgen = Totargets | 1 << (cpu + 16) | m->machno;
	iunlock(&distlock);
}

/*
 * secondary interrupt controller on RP1 southbridge
 */
void
rp1unmask(int irq)
{
	ulong *regs = (ulong*)RP1INTC;

	regs[Clr + irq] = Iackenable;
	regs[Set + irq] = Ienable;
}

void
pciehostintr(Ureg *ureg, void*)
{
	ulong *regs = (ulong*)RP1INTC;
	int irq;
	uvlong ack;
	static int first;

	ack = regs[StatLo] | ((vlong)regs[StatHi])<<32;
	//if(ack == 0 && ++first < 16)
	//	iprint("pciehost ack==0\n");
	for(irq = 0; ack; irq++, ack >>= 1){
		if(ack&01){
			if(!doirq(ureg, IRQRP1 + irq)){
				//regs[Set + irq] = Iack;
				coherence();
				//iprint("pciehost unexpected interrupt %d, disabling\n", irq);
				regs[Clr + irq] = Ienable;	/* ? */
			}else{
				/* not if edge triggered, ie usb */
				//regs[Set + irq] = Iack;
			}
		}
	}
}

extern void setvbar(void*);
extern void _vectors(void);

/*
 *  set up for exceptions
 */
void
trapinit(void)
{
	Intrdistregs *idp = (Intrdistregs *)(ARMLOCAL+Intrdist);
	Intrcpuregs *icp = (Intrcpuregs *)(ARMLOCAL+Intrcpu);

	setvbar(_vectors);
	assert((idp->distid & MASK(12)) == 0x43b);	/* made by arm */
	assert((icp->ifid   & MASK(12)) == 0x43b);	/* made by arm */

	ilock(&distlock);
	idp->ctl = 0;
	icp->ctl = 0;
	coherence();

	intrcfg(idp);			/* some per-cpu cfg here */

	icp->ctl = Enable;
	icp->primask = (uchar)~0;	/* let all priorities through */
	coherence();

	idp->ctl = Forw2cpuif;
	iunlock(&distlock);
}

/*
 *  enable an irq interrupt
 *  note that the same private interrupt may be enabled on multiple cpus
 */
void
irqenable(int irq, void (*f)(Ureg*, void*), void* a)
{
	Vctl *v;
	int ena;
	static char name[] = "anon";

	if(f == nil)
		return;

	/* permute irq numbers for pi5 */
	ena = 1;
	if(irq >= IRQRP1)
		ena = 2;
	else if(irq >= IRQlocal)
		irq = IRQLOCAL(irq);
	else
		irq = IRQGLOBAL(irq);
	if(irq >= nelem(vctl))
		panic("irqenable irq %d", irq);

	if (irqtooearly) {
		iprint("irqenable for %d %s called too early\n", irq, name);
		return;
	}

	/*
	 * if in use, could be a private interrupt on a secondary cpu,
	 * or a shared irq number (eg emmc and sdhc)
	 */
	if(!ISSGI(irq) || vctl[irq] == nil) {
		v = malloc(sizeof(Vctl));
		if (v == nil)
			panic("irqenable: malloc Vctl");
		v->f = f;
		v->a = a;
		v->name = malloc(strlen(name)+1);
		if (v->name == nil)
			panic("irqenable: malloc name");
		strcpy(v->name, name);

		lock(&vctllock);
		v->next = vctl[irq];
		if (v->next == nil)
			vctl[irq] = v;
		else if (!ISSGI(irq)) {
			/* shared irq number */
			vctl[irq] = v;
			ena = 0;
		} else {
			/* allocation race: someone else did it first */
			free(v->name);
			free(v);
		}
		unlock(&vctllock);
	}
	switch(ena) {
	case 1:		/* GIC */
		intdismiss((Intrcpuregs *)(ARMLOCAL+Intrcpu), irq);
		intcunmask(irq);
		break;
	case 2:		/* RP1 */
		if(vctl[IRQGLOBAL(IRQpciehost)] == nil)
			irqenable(IRQpciehost, pciehostintr, nil);
		rp1unmask(irq - IRQRP1);
		break;
	}
}

/*
 *  called by trap to handle access faults
 */
static void
faultarm(Ureg *ureg, uintptr va, int user, int read)
{
	int n, insyscall;

	if(up == nil) {
		//dumpstackwithureg(ureg);
		iprint("pc %#p link %#p sp %#p r2 %#p\n", ureg->pc, ureg->link, ureg->sp, ureg->r2);
		panic("faultarm: cpu%d: nil up, user %d %sing %#p at %#p",
			m->machno, user, (read? "read": "writ"), va, ureg->pc);
	}
	if(up->nlocks.ref)
		iprint("faultarm: user %d pc %#p addr %#p: nlocks %ld\n", user, ureg->pc, va, up->nlocks.ref);
	insyscall = up->insyscall;
	up->insyscall = 1;

	n = fault(va, read);		/* goes spllo */
	splhi();
	if(n < 0){
		char buf[ERRMAX];

		if(!user){
			//dumpstackwithureg(ureg);
			uintptr *badsp = (uintptr*)ureg->sp;
			panic("fault: cpu%d: kernel %sing %#p at %#p link %#p [%#p %#p %#p %#p %#p]",
				m->machno, read? "read": "writ", va, ureg->pc, ureg->link, badsp[0], badsp[1], badsp[2], badsp[3], badsp[4]);
		}
		/* don't dump registers; programs suicide all the time */
		snprint(buf, sizeof buf, "sys: trap: fault %s va=%#p",
			read? "read": "write", va);
		postnote(up, 1, buf, NDebug);
	}
	up->insyscall = insyscall;
}

void
trap(Ureg* ureg)
{
	int ec, user, read, rem;
	uintptr va;

	if(islo())
		panic("trap entered with interrupts enabled");
	if(up != nil)
		rem = ((char*)ureg)-up->kstack;
	else
		rem = ((char*)ureg)-((char*)m+sizeof(Mach));
	if(rem < 1024) {
		panic("trap: %d stack bytes left, up %#p ureg %#p at pc %#p",
			rem, up, ureg, ureg->pc);
	}

	user = (ureg->psr & 0xf) == 0;
	read = 1;
	ec = ureg->type>>26;	/* exception class in ESR_EL1 sysreg */
	switch(ec){
	case 0x00:		/* illegal instruction or unknown reason? */
		if(!user)
			panic("illegal instruction in kernel (%8.8lux) PC=%#p link=%#p", *(ulong*)ureg->pc, ureg->pc, ureg->link);
		else{
			postnote(up, 1, "sys: illegal instruction", NDebug);
		}
		break;
	case 0x07:		/* floating point */
	case 0x2C:		/* floating exception from aarch62 */
		if(!user)
			panic("illegal instruction in kernel PC=%#p link=%#p", ureg->pc, ureg->link);
		else if(fpuemu(ureg) == 0){
			postnote(up, 1, "sys: fp instruction error", NDebug);
		}
		break;
	case 0x11:		/* aarch32 syscall */
	case 0x15:		/* aarch64 syscall */
		syscall(ureg);
		return;		/* sic */
	case 0x22:		/* PC alignment fault */
		print("alignment fault PC=%#p SP=%#p R13=%#p PSR=%#p", ureg->pc, ureg->sp, ureg->r13, ureg->psr);
		if(user)
			postnote(up, 1, "sys: PC alignment fault", NDebug);
		else
			panic("kernel PC alignment fault PC=%#p", ureg->pc);
		break;
	case 0x24:		/* EL0 data abort */
		read = (ureg->type & 1<<6) == 0;
	case 0x20:		/* EL0 instruction abort */
		user = 1;
		goto fault;
	case 0x25:		/* EL1 data abort */
		read = (ureg->type & 1<<6) == 0;
	case 0x21:		/* EL1 instruction abort */
	fault:
		va = farget();
		faultarm(ureg, va, user, read);
		break;
	case 0x38:		/* BKPT from aarch32 */
	case 0x3C:		/* BKPT from aarch64 */
		if(user)
			postnote(up, 1, "sys: breakpoint", NDebug);
		else
			panic("kernel bkpt PC=%#p", ureg->pc);
		break;
	default:
		iprint("!trap PC=%#p code=%x\n", ureg->pc, ec);
		for(;;) ;
	}

	splhi();
	if(user){
		if(up->procctl || up->nnote)
			notify(ureg);
		kexit(ureg);
	}
}

/*
 *  called from exception vector to handle interrupts.
 *  if a clock interrupt, maybe reschedule.
 */
void
irq(Ureg* ureg)
{
	int clockintr, ack, user, rem;
	uint irqno;
	Intrcpuregs *icp = (Intrcpuregs *)(ARMLOCAL+Intrcpu);

	m->perf.intrts = perfticks();
	if(islo())
		panic("irq entered with interrupts enabled");
	if(up != nil)
		rem = ((char*)ureg)-up->kstack;
	else
		rem = ((char*)ureg)-((char*)m+sizeof(Mach));
	if(rem < 1024) {
		panic("trap: %d stack bytes left, up %#p ureg %#p at pc %#p",
			rem, up, ureg, ureg->pc);
	}
	user = (ureg->psr & 0xf) == 0;
	clockintr = 0;
	m->intr++;
  for(;;){
	ack = intack(icp);
	irqno = ack & Intrmask;

	if(irqno == 0 || irqno == 1023)
		break;
	if(irqno == IRQGLOBAL(IRQclock) || irqno == IRQLOCAL(IRQcntpns))
		clockintr = 1;

	if(!doirq(ureg, irqno)){
		if (irqno >= 1022){
			iprint("cpu%d: ignoring spurious interrupt\n", m->machno);
			break;
		}else {
			intcmask(irqno);
			iprint("cpu%d: unexpected interrupt %d, now masked\n",
				m->machno, irqno);
		}
	}

	intdismiss(icp, ack);
	intrtime(m, irqno);
  }
	
	splhi();

	/* delaysched set because we held a lock or because our quantum ended */
	if(up && up->delaysched && clockintr){
		sched();		/* can cause more traps */
		splhi();
	}

	if(user){
		if(up->procctl || up->nnote)
			notify(ureg);
		kexit(ureg);
	}
}

static int
doirq(Ureg *ureg, int irqno)
{
	int handled;
	Vctl *v;

	handled = 0;
	for(v = vctl[irqno]; v != nil; v = v->next)
		if (v->f) {
			if (islo())
				panic("trap: pl0 before trap handler for %s",
					v->name);
			coherence();
			v->f(ureg, v->a);
			coherence();
			if (islo()){
				iprint("trap: %s lowered pl", v->name);
				splhi();		/* in case v->f lowered pl */
			}
			handled++;
		}
	return handled;
}

void
fiq(Ureg* ureg)
{
	iprint("!fiq PC=%#p type=%#p\n", ureg->pc, ureg->type);
}

void
serror(Ureg* ureg)
{
	iprint("!serror %s PC=%#p type=%#p\n", up? up->text : "?", ureg->pc, ureg->type);
}

void
dumpstack(void)
{}
