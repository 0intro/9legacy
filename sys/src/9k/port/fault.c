#define	_DBGC_	'F'
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

int
fault(uintptr addr, int read)
{
	Segment *s;
	char *sps;

	if(up == nil)
		panic("fault: nil up");
	if(up->nlocks){
		panic("fault: %#p %s: %s: nlocks %d %#p\n", addr, up->text, up->user, up->nlocks, up->lastlock? lockgetpc(up->lastlock): 0);
		//dumpstack();
	}

	m->pfault++;
	spllo();

	for(;;){
		s = seg(up, addr, rlock);		/* leaves s->lk rlocked */
		if(s == nil)
			return -1;
		if(!read && (s->type&SG_RONLY)){
			runlock(&s->lk);
			return -1;
		}

		sps = up->psstate;
		up->psstate = "Fault";
		if(fixfault(s, addr, read, 1) == 0){	/* runlocks s->lk */
			if(0)
				checkpages();
			up->psstate = sps;
			return 0;
		}
		up->psstate = sps;

		if(up->procctl == Proc_exitbig)
			pexit("out of memory", 1);

		/*
		 * See the comment in newpage that describes
		 * how to get here.
		 */
	}
}

int
fixfault(Segment *s, uintptr addr, int read, int dommuput)
{
	int type;
	Pte **p, *etp;
	uintptr soff;
	uintmem mmuphys;
	Page **pg, *old, *new;
	Page *(*fn)(Segment*, uintptr);
	uintptr pgsize;
	Pages *pages;

	pages = s->pages;	/* TO DO: segwalk */
	pgsize = 1<<pages->lg2pgsize;
	addr &= ~(pgsize-1);
	soff = addr-s->base;

	p = &pages->map[soff/pages->ptemapmem];
	if(*p == nil)
		*p = ptealloc();

	etp = *p;
	pg = &etp->pages[(soff&(pages->ptemapmem-1))>>pages->lg2pgsize];

	if(pg < etp->first)
		etp->first = pg;
	if(pg > etp->last)
		etp->last = pg;

	type = s->type&SG_TYPE;
	if(*pg == nil){
		switch(type){
		case SG_BSS:			/* Zero fill on demand */
		case SG_SHARED:
		case SG_STACK:
			new = newpage(1, s->pages->lg2pgsize, &s->lk);
			if(new == nil)
				return -1;
			*pg = new;
			break;

		case SG_TEXT:	/* demand load */
		case SG_DATA:
			runlock(&s->lk);
			new = imagepage(s->image, s->isec, addr, soff);
			rlock(&s->lk);
			if(*pg == nil){
				*pg = new;
				if(s->flushme)
					mmucachectl(new, PG_TXTFLUSH);
			}else
				putpage(new);
			break;

		case SG_PHYSICAL:
			fn = s->pseg->pgalloc;
			if(fn != nil)
				*pg = (*fn)(s, addr);
			else {
				new = smalloc(sizeof(Page));
				new->pa = s->pseg->pa+(addr-s->base);
				new->ref = 1;
				new->lg2size = s->pseg->lg2pgsize;
				if(new->lg2size == 0)
					new->lg2size = PGSHFT;	/* TO DO */
				*pg = new;
			}
			break;
		default:
			panic("fault on demand");
			break;
		}
	}
	mmuphys = 0;
	switch(type) {
	default:
		panic("fault");
		break;

	case SG_TEXT:
		DBG("text pg %#p: %#p -> %#P %d\n", pg, addr, (*pg)->pa, (*pg)->ref);
		mmuphys = PPN((*pg)->pa) | PTERONLY|PTEVALID;
		break;

	case SG_BSS:
	case SG_SHARED:
	case SG_STACK:
	case SG_DATA:			/* copy on write */
		DBG("data pg %#p: %#p -> %#P %d\n", pg, addr, (*pg)->pa, (*pg)->ref);
		/*
		 *  It's only possible to copy on write if
		 *  we're the only user of the segment.
		 */
		if(read && sys->copymode == 0 && s->ref == 1) {
			mmuphys = PPN((*pg)->pa)|PTERONLY|PTEVALID;
			break;
		}

		old = *pg;
		if(old->ref > 1){
			/* shared (including image pages): make private writable copy */
			new = newpage(0, s->pages->lg2pgsize, &s->lk);
			if(new == nil)
				return -1;
			copypage(old, new);
			*pg = new;
			putpage(old);
			DBG("data' pg %#p: %#p -> %#P %d\n", *pg, addr, old->pa, old->ref);
		}else if(old->ref <= 0)
			panic("fault: page %#p %#P ref %d <= 0", old, old->pa, old->ref);
		mmuphys = PPN((*pg)->pa) | PTEWRITE | PTEVALID;
		break;

	case SG_PHYSICAL:
		mmuphys = PPN((*pg)->pa) | PTEVALID;
		if((s->pseg->attr & SG_RONLY) == 0)
			mmuphys |= PTEWRITE;
		if((s->pseg->attr & SG_CACHED) == 0)
			mmuphys |= PTEUNCACHED;
		break;
	}
	runlock(&s->lk);

	if(dommuput)
		mmuput(addr, mmuphys, *pg);

	return 0;
}

/*
 * Called only in a system call
 */
int
okaddr(uintptr addr, long len, int write)
{
	Segment *s;

	if(len >= 0) {
		for(;;) {
			s = seg(up, addr, nil);
			if(s == 0 || (write && (s->type&SG_RONLY)))
				break;

			if(addr+len > s->top) {
				len -= s->top - addr;
				addr = s->top;
				continue;
			}
			return 1;
		}
	}
	return 0;
}

void*
validaddr(void* addr, long len, int write)
{
	if(!okaddr(PTR2UINT(addr), len, write)){
		pprint("trap: invalid address %#p/%lud in sys call pc=%#P\n", addr, len, userpc(nil));
		postnote(up, 1, "sys: bad address in syscall", NDebug);
		error(Ebadarg);
	}

	return UINT2PTR(addr);
}

/*
 * &s[0] is known to be a valid address.
 */
void*
vmemchr(void *s, int c, int n)
{
	int np;
	uintptr a;
	void *t;

	a = PTR2UINT(s);
	while(ROUNDUP(a, PGSZ) != ROUNDUP(a+n-1, PGSZ)){
		/* spans pages; handle this page */
		np = PGSZ - (a & (PGSZ-1));
		t = memchr(UINT2PTR(a), c, np);
		if(t)
			return t;
		a += np;
		n -= np;
		if(!iskaddr(a))
			validaddr(UINT2PTR(a), 1, 0);
	}

	/* fits in one page */
	return memchr(UINT2PTR(a), c, n);
}

void
checkpages(void)
{
	uintptr addr, off;
	Pte *p;
	Page *pg;
	Segment **sp, **ep, *s;
	Pages *ps;
	uint pgsize;

	if(up == nil || up->newtlb)
		return;

	for(sp=up->seg, ep=&up->seg[NSEG]; sp<ep; sp++){
		s = *sp;
		if(s == nil)
			continue;
		rlock(&s->lk);
		ps = s->pages;
		pgsize = 1<<ps->lg2pgsize;
		for(addr=s->base; addr<s->top; addr+=pgsize){
			off = addr - s->base;
			if(off >= ps->xsize){
				print("%d %s: seg %ld off %#p outside %#p\n", up->pid, up->text, sp-up->seg, off, ps->xsize);
				continue;
			}
			p = ps->map[off/ps->ptemapmem];
			if(p == nil)
				continue;
			pg = p->pages[(off&(ps->ptemapmem-1))/pgsize];
			if(pg == 0)
				continue;
			if(!iskaddr(pg)){
				print("%d %s: invalid page off %#p pg %#p\n", up->pid, up->text, off, pg);
				printpages(ps);
				continue;
			}
			checkmmu(addr, pg->pa);
		}
		runlock(&s->lk);
	}
}
