#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

typedef struct Mprof Mprof;
typedef struct Mevents Mevents;
typedef struct Mevent Mevent;

enum
{
	Qdir,
	Qctl,
	Qevent,
	Qprof,
	Qsum,
};

static
Dirtab memdir[] =
{
	".",			{Qdir, 0, QTDIR},	0,	DMDIR|0555,
	"memctl",		{Qctl},	0,			0664,
	"memevent",	{Qevent},	0,			0444,
	"memprof",	{Qprof},	0,			0444,
	"memsum",	{Qsum},	0,			0444,
};

enum
{
	Nevent = 10000,
	BucketLg2=	15,	/* for 512k kernel, allows allocation every 16 bytes */
	Nbucket=	1<<BucketLg2,
	BucketMask=	Nbucket-1,
	Noverflow=	100,	/* number of entries for duplicates */
	Profreclen=	3*4,	/* tag, na, busy */
	Evreclen=		4*4,	/* 0, tag, koff, size */

	MaxInt=	0x7FFFFFFF,
};

/*
 * allocation profile
 */
struct Mprof{
	ulong	tag;		/* usually low-order 32 bits of pc */
	int	na;			/* active allocations */
	int	busy;		/* bytes currently allocated */
	uint	ovfl;			/* !=0, next index on overflow */
};

static struct
{
	Mprof	bucket[Nbucket+Noverflow+1];	/* last entry as catchall */
	int	novfl;
	Lock	ovlk;
} memprof;

/*
 * allocation events
 */
struct Mevent
{
	ulong	tag;		/* usually low-order 32 bits of pc */
	ulong	koff;		/* base-KZERO */
	int	size;		/* > 0, alloc; < 0, free */
};

static struct Mevents
{
	Lock;
	Ref;
	Rendez	r;
	Mevent	events[Nevent];
	uint	rd;
	uint	wr;
	int	want;
	ulong	lost;
} memevents;

static	Ref	monitoring;

extern	void setmemprof(void (*)(void*, ulong, usize, int));	/* qmalloc.c */

static void
aadd(int *addr, int delta)
{
	int value;

	do
		value = *addr;
	while(!CASW(addr, value, value+delta));
}

static int
isnonempty(void *v)
{
	Mevents *evs;

	evs = v;
	return evs->rd != evs->wr;
}

static int
isnotfull(Mevents *evs)
{
	return (evs->wr - evs->rd) < Nevent;
}

static void
addmemevent(void *a, ulong tag, usize nb, int w)
{
	Mevents *evs;
	Mevent e;
	int empty;

	e.tag = tag;
	e.koff = (uintptr)a - KZERO;
	if(nb > MaxInt)
		nb = MaxInt;
	e.size = w < 0? -nb: nb;

	evs = &memevents;
	ilock(evs);
	if(isnotfull(evs)){
		empty = evs->rd == evs->wr;
		evs->events[evs->wr++] = e;
	}else{
		evs->lost++;
		empty = 0;
	}
	iunlock(evs);
	if(empty)
		wakeup(&evs->r);
}

static void
mprofmonitor(void *a, ulong tag, usize nb, int w)
{
	Mprof *p;
	uint n;

	if(memevents.ref != 0)
		addmemevent(a, tag, nb, w);
	n = ((tag-(KTZERO&0xFFFFFFFF))/(512*KiB/Nbucket))&BucketMask;
	if(n > Nbucket)
		n = Nbucket-1;
	for(;;){
		p = &memprof.bucket[n];
		if(p->tag == tag || p->tag == ~0)
			break;
		n = p->ovfl;
		if(n == 0){
			if(w < 0)
				return;
			ilock(&memprof.ovlk);
			if(p->tag != 0 && p->tag != tag){
				n = p->ovfl;
				if(n != 0){
					iunlock(&memprof.ovlk);
					/* follow the overflow chain */
					continue;
				}
				/* need an overflow entry */
				n = memprof.novfl;
				if(n < Noverflow)
					memprof.novfl++;
				else
					tag = ~0;
				n += Nbucket;
				p->ovfl = n;
				p = &memprof.bucket[n];
			}
			p->tag = tag;
			iunlock(&memprof.ovlk);
			break;
		}
	}
	if(w < 0){
		aadd(&p->na, -1);
		aadd(&p->busy, -nb);
	}else{
		aadd(&p->na, 1);
		aadd(&p->busy, nb);
	}
}

static void
mput4(uchar *m, ulong v)
{
	m[0] = v>>24;
	m[1] = v>>16;
	m[2] = v>>8;
	m[3] = v;
}

static void
memprofinit(void)
{
	incref(&monitoring);
	setmemprof(mprofmonitor);
}

static Chan*
memattach(char *spec)
{
	return devattach('%', spec);
}

static Walkqid*
memwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, memdir, nelem(memdir), devgen);
}

static long
memstat(Chan *c, uchar *db, long n)
{
	return devstat(c, db, n, memdir, nelem(memdir), devgen);
}

static Chan*
memopen(Chan *c, int omode)
{
	Mevents *evs;

	c = devopen(c, omode, memdir, nelem(memdir), devgen);
	switch((ulong)c->qid.path){
	case Qevent:
		evs = &memevents;
		if(incref(evs) != 1){
			decref(evs);
			c->flag &= ~COPEN;
			error(Einuse);
		}
		evs->rd = evs->wr = 0;
		evs->want = 0;
		evs->lost = 0;
		incref(&monitoring);
		setmemprof(mprofmonitor);
		break;
	case Qprof:
		break;
	}
	return c;
}

static void
memclose(Chan *c)
{
	if((c->flag & COPEN) == 0)
		return;
	switch((ulong)c->qid.path) {
	case Qevent:
		if(decref(&monitoring) == 0)
			setmemprof(nil);
		decref(&memevents);
		break;
	case Qprof:
		break;
	}

}

static long
memread(Chan *c, void *va, long count, vlong offset)
{
	uchar *a;
	int i;
	Mevent *pe;
	Mevents *evs;
	Mprof *p;

	if(c->qid.type & QTDIR)
		return devdirread(c, va, count, memdir, nelem(memdir), devgen);

	switch((ulong)c->qid.path) {
	default:
		error(Egreg);
	case Qctl:
		return 0;
	case Qsum:
		return mallocreadsummary(c, va, count, offset);
	case Qevent:
		evs = &memevents;
		while(!isnonempty(evs)){
			evs->want = 1;
			sleep(&evs->r, isnonempty, evs);
		}
		a = va;
		do{
			if((count -= Evreclen) < 0)
				break;
			pe = &evs->events[evs->rd];
			mput4(a+0, 0);
			mput4(a+4, pe->tag);
			mput4(a+8, pe->koff);
			mput4(a+12, pe->size);
			a += Evreclen;
		}while(++evs->rd != evs->wr);
		return a-(uchar*)va;
	case Qprof:
		a = va;
		for(i = offset/Profreclen; i < nelem(memprof.bucket); i++){
			p = &memprof.bucket[i];
			if((p->tag|p->na|p->busy) != 0){
				if((count -= Profreclen) < 0)
					break;
				mput4(a+0, p->tag);
				mput4(a+4, p->na);
				mput4(a+8, p->busy);
				a += Profreclen;
			}
		}
		return a-(uchar*)va;
	}
}

static long
memwrite(Chan *c, void *a, long n, vlong)
{
	Cmdbuf *cb;

	if(c->qid.type & QTDIR)
		error(Eperm);

	switch((ulong)c->qid.path) {
	default:
		error(Egreg);
	case Qctl:
		cb = parsecmd(a, n);
		if(waserror()){
			free(cb);
			nexterror();
		}
		if(cb->nf == 1 && strcmp(cb->f[0], "start") == 0){
			if(incref(&monitoring) == 1)
				setmemprof(mprofmonitor);
		}else if(cb->nf == 1 && strcmp(cb->f[0], "stop") == 0){
			if(decref(&monitoring) == 0)
				setmemprof(nil);
		}else
			cmderror(cb, "unknown command");
		poperror();
		free(cb);
		break;
	}
	return n;
}

Dev memdevtab = {
	'%',
	"mem",

	devreset,
	memprofinit,	//devinit,
	devshutdown,
	memattach,
	memwalk,
	memstat,
	memopen,
	devcreate,
	memclose,
	memread,
	devbread,
	memwrite,
	devbwrite,
	devremove,
	devwstat
};
