#include "u.h"
#include "../port/tos32.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "io.h"
#include "fns.h"

#include "init.h"
#include <pool.h>

enum {
	/* space for syscall args, return PC, top-of-stack struct */
	Ustkheadroom	= sizeof(Sargs) + sizeof(uintptr) + sizeof(Tos32),
};

Mach*	machaddr[MAXMACH];
Conf	conf;

/*
 * Where configuration info is left for the loaded programme.
 */
#define BOOTARGS	((char*)CONFADDR)
#define	BOOTARGSLEN	(MACHADDR-CONFADDR)
#define	MAXCONF		64
#define MAXCONFLINE	160

void	(*pl011init)(void);

/*
 * Option arguments from the command line.
 * oargv[0] is the boot file.
 */
static int oargc;
static char* oargv[20];
static char oargb[128];
static int oargblen;

static uintptr sp;		/* XXX - must go - user stack of init proc */

/* store plan9.ini contents here at least until we stash them in #ec */
static char confname[MAXCONF][KNAMELEN];
static char confval[MAXCONF][MAXCONFLINE];
static int nconf;

static int
findconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return i;
	return -1;
}

char*
getconf(char *name)
{
	int i;

	i = findconf(name);
	if(i >= 0)
		return confval[i];
	return nil;
}

void
addconf(char *name, char *val)
{
	int i;

	i = findconf(name);
	if(i < 0){
		if(val == nil || nconf >= MAXCONF)
			return;
		i = nconf++;
		strecpy(confname[i], confname[i]+sizeof(confname[i]), name);
	}
	strecpy(confval[i], confval[i]+sizeof(confval[i]), val);
}

static void
plan9iniinit(char *s, int cmdline)
{
	char *toks[MAXCONF];
	int i, c, n;
	char *v;

	if((c = *s) < ' ' || c >= 0x80)
		return;
	if(cmdline)
		n = tokenize(s, toks, MAXCONF);
	else
		n = getfields(s, toks, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(toks[i][0] == '#')
			continue;
		v = strchr(toks[i], '=');
		if(v == nil)
			continue;
		*v++ = '\0';
		addconf(toks[i], v);
	}
}

/*
 * Flattened device tree walk
 */
typedef struct Dtnode Dtnode;
struct Dtnode {
	Dtnode	*parent;
	char	*name;
};
static struct {
	char	*stringtab;
	void	(*property)(Dtnode, char*, uchar*,  int);
} dt;

uchar *
dtwalk(uchar *p, Dtnode n)
{
	Dtnode nn;
	uint len, nameoff;

	nn.parent = &n;
	//print("walk %s %#p", n.name, p);
	//for(int i = 0; i < 16; i++) print(" %2.2ux", p[i]);
	//print("\n");
	for(;;){
		switch(nhgetl(p)){
		case 9:	/* end tree */
			return p;
		case 1:	/* begin node */
			nn.name = (char*)p + 4;
			p = (uchar*)nn.name + strlen(nn.name) + 1;
			p = (uchar*)(((uintptr)p + 3) & ~3);
			p = dtwalk(p, nn);
			if(p == nil)
				return nil;
			break;
		case 2: /* end node */
			return p + 4;
		case 3: /* property */
			len = nhgetl(p + 4);
			nameoff = nhgetl(p + 8);
			(*dt.property)(n, &dt.stringtab[nameoff], p + 12, len);
			p += 12 + len;
			p = (uchar*)(((uintptr)p + 3) & ~3);
			break;
		case 4:	/* nop */
			p += 4;
			break;
		default:	/* syntax error */
			return nil;
		}
	}
}

int
devicetree(void *a, void (*f)(Dtnode, char*, uchar*,  int))
{
	u32int *hdr;
	Dtnode root;

	hdr = a;
	if(nhgetl(hdr) != 0xd00dfeed){
		print("no devicetree at %#p: %ux\n", hdr, nhgetl(hdr));
		return -1;
	}
	dt.stringtab = (char*)a + nhgetl(&hdr[3]);
	dt.property = f;
	root.parent = &root;
	root.name = "/";
	if(dtwalk((uchar*)a + nhgetl(&hdr[2]), root) == nil){
		print("bad syntax in devicetree\n");
		return -1;
	}
	return 0;
}

void
dtproperty(Dtnode n, char *prop, uchar *value, int len)
{
	extern uchar ether0mac[];

	if(!strcmp(n.name, "chosen") && !strcmp(prop, "bootargs"))
		plan9iniinit((char*)value, 1);
	else if(!strncmp(n.name, "ethernet@", 9) &&
	  !strcmp(n.parent->name, "rp1") &&
	  strstr(prop, "local-mac-address") && len == 6)
		memmove(ether0mac, value, 6);
	else if(!strncmp(n.name, "pcie@12", 7) &&
	  !strcmp(prop, "ranges") &&
	  len >= 48)
		soc.pcispace = (uvlong)nhgetl(value+40) << 32;
	else if(!strcmp(n.name, "memory@0") && !strcmp(prop, "reg"))
		conf.mem[0].limit = nhgetl(value + 8);
	else if(!strcmp(n.name, "clk_emmc2") && !strcmp(prop, "clock-frequency"))
		soc.emmc2freq = nhgetl(value);
	else if(!strncmp(n.name, "framebuffer@", 12) &&
	  !strcmp(n.parent->name, "chosen"))
		fbsetup(prop, value, len);
	else if(!strcmp(n.name, "pinctrl@7d504100") && !strcmp(prop, "compatible")){
		if(strstr((char*)value, "bcm2712d") != nil)
			soc.dstepping = 1;
	}
}

uvlong	memsize = 0x30000000;

void
confinit()
{
	int i, userpcnt;
	ulong kpages;
	uintptr pa;
	char *p;

	if((p = getconf("service")) != nil){
		if(strcmp(p, "cpu") == 0)
			cpuserver = 1;
		else if(strcmp(p,"terminal") == 0)
			cpuserver = 0;
	}
	if((p = getconf("*maxmem")) != nil){
		memsize = strtoull(p, 0, 0) - PHYSDRAM;
		if (memsize < 16*MB)		/* sanity */
			memsize = 16*MB;
	}

	getramsize(&conf.mem[0]);
	if(conf.mem[0].limit == 0){
		conf.mem[0].base = PHYSDRAM;
		conf.mem[0].limit = PHYSDRAM + memsize;
	}
	/*
	 * pi4 extra memory (beyond video ram) indicated by board id
	 */
	switch(getboardrev()&0xF00000){
	case 0xA00000:
		break;
	case 0xB00000:
		conf.mem[1].base = 1*GiB;
		conf.mem[1].limit = 2*GiB;
		break;
	case 0xC00000:
		conf.mem[1].base = 1*GiB;
		conf.mem[1].limit = 0xFF000000;
		break;
	default:
	case 0xD00000:
		conf.mem[1].base = 1*GiB;
		conf.mem[1].limit =  0xFF000000;
		conf.himem.base = 4LL*GiB;
		conf.himem.limit = 8LL*GiB;
		break;
	case 0xE00000:
		conf.mem[1].base = 1*GiB;
		conf.mem[1].limit = 0xFF000000;
		conf.himem.base = 4LL*GiB;
		conf.himem.limit = 16LL*GiB;
		break;
	}
	if(p != nil){
		for(i = 0; i < nelem(conf.mem); i++){
			if(memsize < conf.mem[i].base)
				conf.mem[i].limit = conf.mem[i].base;
			else if(memsize < conf.mem[i].limit)
				conf.mem[i].limit = memsize;
		}
		if(memsize < conf.himem.base)
			conf.himem.limit = conf.himem.base;
		else if(memsize < conf.himem.limit)
			conf.himem.limit = memsize;
	}
	if(conf.himem.limit > 0)
		soc.dramsize = conf.himem.limit;
	else
		soc.dramsize = conf.mem[1].limit;

	if(p = getconf("*kernelpercent"))
		userpcnt = 100 - strtol(p, 0, 0);
	else
		userpcnt = 0;

	conf.npage = 0;
	pa = PADDR(PGROUND(PTR2UINT(end)));
	pa += BY2PG;

	/*
	 *  we assume that the kernel is at the beginning of one of the
	 *  contiguous chunks of memory and fits therein.
	 */
	for(i=0; i<nelem(conf.mem); i++){
		/* take kernel out of allocatable space */
		if(pa > conf.mem[i].base && pa < conf.mem[i].limit)
			conf.mem[i].base = pa;

		conf.mem[i].npage = (conf.mem[i].limit - conf.mem[i].base)/BY2PG;
		conf.npage += conf.mem[i].npage;
	}
	conf.himem.npage = (conf.himem.limit - conf.himem.base)/BY2PG;

	if(userpcnt < 10 || userpcnt > 99)
		userpcnt = 90;
	conf.upages = (conf.npage*userpcnt)/100;
	if(conf.npage - conf.upages > 800*MiB/BY2PG)
		conf.upages = conf.npage - 800*MiB/BY2PG;
	conf.ialloc = ((conf.npage-conf.upages)/2)*BY2PG;

	/* set up other configuration parameters */
	conf.nproc = 100 + (conf.npage/(MB/BY2PG))*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 2000)
		conf.nproc = 2000;
	conf.nswap = conf.npage*3;
	conf.nswppo = 4096;
	conf.nimage = 200;

	conf.copymode = 1;		/* copy on reference, not copy on write */

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for
	 * (probably ~300KB).
	 */
	kpages = conf.npage - conf.upages;
	kpages *= BY2PG;
	kpages -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc)
		+ conf.nimage*sizeof(Image)
		+ conf.nswap
		+ conf.nswppo*sizeof(Page*);
	mainmem->maxsize = kpages;
	if(!cpuserver)
		/*
		 * give terminals lots of image memory, too; the dynamic
		 * allocation will balance the load properly, hopefully.
		 * be careful with 32-bit overflow.
		 */
		imagmem->maxsize = kpages;
}

/* enable scheduling of this cpu */
void
machon(uint cpu)
{
	ulong cpubit;

	cpubit = 1 << cpu;
	lock(&active);
	if ((active.machs & cpubit) == 0) {	/* currently off? */
		conf.nmach++;
		active.machs |= cpubit;
	}
	unlock(&active);
}

/* disable scheduling of this cpu */
void
machoff(uint cpu)
{
	ulong cpubit;

	cpubit = 1 << cpu;
	lock(&active);
	if (active.machs & cpubit) {		/* currently on? */
		conf.nmach--;
		active.machs &= ~cpubit;
	}
	unlock(&active);
}

void
machinit(void)
{
	Mach *m0;

	m->ticks = 1;
	m->perf.period = 1;
	m0 = MACHP(0);
	if (m->machno != 0) {
		/* synchronise with cpu 0 */
		m->ticks = m0->ticks;
	}
}

void
mach0init(void)
{
	conf.nmach = 0;

	m->machno = 0;
	machaddr[m->machno] = m;

	machinit();
	active.exiting = 0;

	up = nil;
}

void
launchinit(int ncpus)
{
	int mach;
	Mach *mm;
	PTE *l1;

	if(ncpus > MAXMACH)
		ncpus = MAXMACH;
	for(mach = 1; mach < ncpus; mach++){
		machaddr[mach] = mm = mallocalign(MACHSIZE, MACHSIZE, 0, 0);
		l1 = mallocalign(L1SIZE, BY2PG, 0, 0);
		if(mm == nil || l1 == nil)
			panic("launchinit");
		memset(mm, 0, MACHSIZE);
		mm->machno = mach;

		mmuinit(PADDR(l1));  /* clone cpu0's l1 table */
		cachedwbse(l1, L1SIZE);
		mm->mmul1 = l1;
		cachedwbse(mm, MACHSIZE);
	}
	cachedwbse(machaddr, sizeof machaddr);
	if((mach = startcpus(ncpus)) < ncpus)
			print("only %d cpu%s started\n", mach, mach == 1? "" : "s");
}

static void
optionsinit(char* s)
{
	strecpy(oargb, oargb+sizeof(oargb), s);

	oargblen = strlen(oargb);
	oargc = tokenize(oargb, oargv, nelem(oargv)-1);
	oargv[oargc] = nil;
}

void
main(uvlong arg)
{
	devicetree((void*)arg, dtproperty);
	m = (Mach*)MACHADDR;
	mach0init();
	m->mmul1 = (PTE*)L1;
	machon(0);

	optionsinit("/boot/boot boot");
	quotefmtinstall();

	confinit();
	xinit();
	xsummary();
	/* set clock rate to arm_freq from config.txt (default pi1:700Mhz pi2:900MHz) */
	setclkrate(ClkArm, 0);
	screeninit();
	if(pl011init != nil)
		(*pl011init)();
	print("\nPlan 9 from Bell Labs\n");
	print("vcore reports memory = 0x%llux\n", conf.mem[0].limit);
	trapinit();
	clockinit();
	printinit();
	timersinit();
	if(conf.monitor)
		swcursorinit();
	cpuidprint();
	if(0)print("clocks: CPU %lud core %lud UART %lud EMMC %lud\n",
		getclkrate(ClkArm), getclkrate(ClkCore), getclkrate(ClkUart), getclkrate(ClkEmmc));
	archreset();
	procinit0();
	initseg();
	links();
	chandevreset();			/* most devices are discovered here */
	pageinit();
	lpapageinit();
	swapinit();
	userinit();
	launchinit(getncpus());
	mmuinit1();
	schedinit();
	assert(0);			/* shouldn't have returned */
	spllo();
}

/*
 *  starting place for first process
 */
void
init0(void)
{
	int i;
	Chan *c;
	char buf[2*KNAMELEN];

	up->nerrlab = 0;
	coherence();
	spllo();

	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	up->slash = namec("#/", Atodir, 0, 0);
	pathclose(up->slash->path);
	up->slash->path = newpath("/");
	up->dot = cclone(up->slash);

	chandevinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "%s %s", "ARM", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "arm", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		//snprint(buf, sizeof(buf), "-a %s", getethermac());
		//ksetenv("etherargs", buf, 0);

		/* convert plan9.ini variables to #e and #ec */
		for(i = 0; i < nconf; i++) {
			ksetenv(confname[i], confval[i], 0);
			ksetenv(confname[i], confval[i], 1);
		}
		if(getconf("pitft")){
			c = namec("#P/pitft", Aopen, OWRITE, 0);
			if(!waserror()){
				devtab[c->type]->write(c, "init", 4, 0);
				poperror();
			}
			cclose(c);
		}
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	touser(sp);
	assert(0);			/* shouldn't have returned */
}

static void
bootargs(uintptr base)
{
	int i;
	ulong ssize;
	char **av, *p;

	/*
	 * Push the boot args onto the stack.
	 * The initial value of the user stack must be such
	 * that the total used is larger than the maximum size
	 * of the argument list checked in syscall.
	 */
	i = oargblen+1;
	p = UINT2PTR(STACKALIGN(base + BY2PG - Ustkheadroom - i));
	memmove(p, oargb, i);

	/*
	 * Now push the argv pointers.
	 * The code jumped to by touser in lproc.s expects arguments
	 *	main(char* argv0, ...)
	 * and calls
	 * 	startboot("/boot/boot", &argv0)
	 * not the usual (int argc, char* argv[])
	 */
	av = (char**)(p - (oargc+1)*sizeof(ulong));
	av = (char**)STACKALIGN(PTR2UINT(av));
	ssize = base + BY2PG - PTR2UINT(av);
	for(i = 0; i < oargc; i++){
		*(ulong*)av = PTR2UINT((oargv[i] - oargb) + (p - base) + (USTKTOP - BY2PG));
		av = (char**)(PTR2UINT(av) + sizeof(ulong));
	}
	*(ulong*)av = 0;
	sp = USTKTOP - ssize;
}

/*
 *  create the first process
 */
void
userinit(void)
{
	Proc *p;
	Segment *s;
	KMap *k;
	Page *pg;

	/* no processes yet */
	up = nil;

	p = newproc();
	p->compat32 = 1;
	p->pgrp = newpgrp();
	p->egrp = smalloc(sizeof(Egrp));
	p->egrp->ref = 1;
	p->fgrp = dupfgrp(nil);
	p->rgrp = newrgrp();
	p->procmode = 0640;

	kstrdup(&eve, "");
	kstrdup(&p->text, "*init*");
	kstrdup(&p->user, eve);

	/*
	 * Kernel Stack
	 */
	p->sched.pc = PTR2UINT(init0);
	p->sched.sp = PTR2UINT(p->kstack+KSTACK-sizeof(up->s.args)-sizeof(uintptr));
	p->sched.sp = STACKALIGN(p->sched.sp);

	/*
	 * User Stack
	 *
	 * Technically, newpage can't be called here because it
	 * should only be called when in a user context as it may
	 * try to sleep if there are no pages available, but that
	 * shouldn't be the case here.
	 */
	s = newseg(SG_STACK, USTKTOP-USTKSIZE, USTKSIZE/BY2PG);
	s->flushme++;
	p->seg[SSEG] = s;
	pg = newpage(1, 0, USTKTOP-BY2PG);
	segpage(s, pg);
	k = kmap(pg);
	bootargs(VA(k));
	kunmap(k);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO_COMPAT32, 1);
	p->seg[TSEG] = s;
	pg = newpage(1, 0, UTZERO_COMPAT32);
	memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));
	segpage(s, pg);
	k = kmap(s->map[0]->pages[0]);
	memmove(UINT2PTR(VA(k)), initcode, sizeof initcode);
	kunmap(k);

	ready(p);
}

static void
shutdown(int ispanic)
{
	int ms, once;

	lock(&active);
	if(ispanic)
		active.ispanic = ispanic;
	else if(m->machno == 0 && (active.machs & (1<<m->machno)) == 0)
		active.ispanic = 0;
	once = active.machs & (1<<m->machno);
	active.machs &= ~(1<<m->machno);
	active.exiting = 1;
	unlock(&active);

	if(once) {
		delay(m->machno*100);		/* stagger them */
		iprint("cpu%d: exiting\n", m->machno);
	}
	spllo();
	if (m->machno == 0)
		ms = 5*1000;
	else
		ms = 2*1000;
	for(; ms > 0; ms -= TK2MS(2)){
		delay(TK2MS(2));
		if(active.machs == 0 && consactive() == 0)
			break;
	}
	if(active.ispanic){
		if(!cpuserver)
			for(;;)
				;
		if(getconf("*debug"))
			delay(5*60*1000);
		else
			delay(10000);
	}
}

void
exit(int code)
{
	shutdown(code);
	splfhi();
	if(m->machno == 0)
		archreboot();
	else
		for(;;){}
}

/*
 * stub for ../omap/devether.c
 */
int
isaconfig(char *class, int ctlrno, ISAConf *isa)
{
	char cc[32], *p;
	int i;

	if(strcmp(class, "ether") != 0)
		return 0;
	snprint(cc, sizeof cc, "%s%d", class, ctlrno);
	p = getconf(cc);
	if(p == nil)
		return (ctlrno == 0);
	isa->type = "";
	isa->nopt = tokenize(p, isa->opt, NISAOPT);
	for(i = 0; i < isa->nopt; i++){
		p = isa->opt[i];
		if(cistrncmp(p, "type=", 5) == 0)
			isa->type = p + 5;
	}
	return 1;
}

void
rerrstr(char*, uint)
{}
