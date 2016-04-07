#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

Segment* (*_globalsegattach)(Proc*, char*);

static Lock physseglock;

int
addphysseg(Physseg* new)
{
	Physseg *ps;

	/*
	 * Check not already entered and there is room
	 * for a new entry and the terminating null entry.
	 */
	lock(&physseglock);
	for(ps = physseg; ps->name; ps++){
		if(strcmp(ps->name, new->name) == 0){
			unlock(&physseglock);
			return -1;
		}
	}
	if(ps-physseg >= nphysseg-2){
		unlock(&physseglock);
		return -1;
	}

	*ps = *new;
	unlock(&physseglock);

	return 0;
}

int
isphysseg(char *name)
{
	int rv;
	Physseg *ps;

	lock(&physseglock);
	rv = 0;
	for(ps = physseg; ps->name; ps++){
		if(strcmp(ps->name, name) == 0){
			rv = 1;
			break;
		}
	}
	unlock(&physseglock);
	return rv;
}

/* Needs to be non-static for BGP support */
uintptr
ibrk(uintptr addr, int seg)
{
	Segment *s, *ns;
	Pages *nps;
	uintptr newtop;
	long newsize;
	uintptr pgsize;
	int i;

	s = up->seg[seg];
	if(s == 0)
		error(Ebadarg);

	if(addr == 0)
		return s->top;

	wlock(&s->lk);
	if(waserror()){
		wunlock(&s->lk);
		nexterror();
	}

	DBG("ibrk addr %#p seg %d base %#p top %#p\n",
		addr, seg, s->base, s->top);
	/* We may start with the bss overlapping the data */
	if(addr < s->base) {
		if(seg != BSEG || up->seg[DSEG] == 0 || addr < up->seg[DSEG]->base)
			error(Enovmem);
		addr = s->base;
	}

	pgsize = segpgsize(s);
	newtop = ROUNDUP(addr, pgsize);
	newsize = (newtop-s->base)/pgsize;


	DBG("ibrk addr %#p newtop %#p newsize %ld\n", addr, newtop, newsize);

	if(newtop < s->top) {
		/*
		 * do not shrink a segment shared with other procs, as the
		 * to-be-freed address space may have been passed to the kernel
		 * already by another proc and is past the validaddr stage.
		 */
		if(s->ref > 1)
			error(Einuse);
		mfreeseg(s, newtop, s->top);
		s->top = newtop;
		poperror();
		wunlock(&s->lk);
		mmuflush();
		return newtop;
	}

	for(i = 0; i < NSEG; i++) {
		ns = up->seg[i];
		if(ns == 0 || ns == s)
			continue;
		if(newtop >= ns->base && newtop < ns->top)
			error(Esoverlap);
	}

	if(!physmemavail(newtop - s->top))
		error(Enovmem);

	nps = growpages(s->pages, newtop - s->base);
	if(nps == nil)
		error(Enovmem);
	s->pages = nps;
	s->top = newtop;

	poperror();
	wunlock(&s->lk);

	return newtop;
}

void
syssegbrk(Ar0* ar0, va_list list)
{
	int i;
	uintptr addr;
	Segment *s;

	/*
	 * int segbrk(void*, void*);
	 * should be
	 * void* segbrk(void* saddr, void* addr);
	 */
	addr = PTR2UINT(va_arg(list, void*));
	for(i = 0; i < NSEG; i++) {
		s = up->seg[i];
		if(s == nil || addr < s->base || addr >= s->top)
			continue;
		switch(s->type&SG_TYPE) {
		case SG_TEXT:
		case SG_DATA:
		case SG_STACK:
			error(Ebadarg);
		default:
			addr = PTR2UINT(va_arg(list, void*));
			ar0->v = UINT2PTR(ibrk(addr, i));
			return;
		}
	}
	error(Ebadarg);
}

void
sysbrk_(Ar0* ar0, va_list list)
{
	uintptr addr;

	/*
	 * int brk(void*);
	 *
	 * Deprecated; should be for backwards compatibility only.
	 */
	addr = PTR2UINT(va_arg(list, void*));

	ibrk(addr, BSEG);

	ar0->i = 0;
}

static uintptr
segattach(Proc* p, int attr, char* name, uintptr va, usize len)
{
	int sno;
	Segment *s, *os;
	Physseg *ps;

	if((va != 0 && va < UTZERO) || iskaddr(va))
		error("virtual address below text or in kernel");

	vmemchr(name, 0, ~0);

	for(sno = 0; sno < NSEG; sno++)
		if(p->seg[sno] == nil && sno != ESEG)
			break;

	if(sno == NSEG)
		error("too many segments in process");

	/*
	 *  first look for a global segment with the
	 *  same name
	 */
	if(_globalsegattach != nil){
		s = (*_globalsegattach)(p, name);
		if(s != nil){
			p->seg[sno] = s;
			return s->base;
		}
	}

	len = ROUNDUP(len, PGSZ);
	if(len == 0)
		error("length overflow");

	/*
	 * Find a hole in the address space.
	 * Starting at the lowest possible stack address - len,
	 * check for an overlapping segment, and repeat at the
	 * base of that segment - len until either a hole is found
	 * or the address space is exhausted.
	 */
//need check here to prevent mapping page 0?
	if(va == 0) {
		va = p->seg[SSEG]->base - len;
		for(;;) {
			os = isoverlap(p, va, len);
			if(os == nil)
				break;
			va = os->base;
			if(len > va)
				error("cannot fit segment at virtual address");
			va -= len;
		}
	}

	va = va&~(PGSZ-1);
	if(isoverlap(p, va, len) != nil)
		error(Esoverlap);

	for(ps = physseg; ps->name; ps++)
		if(strcmp(name, ps->name) == 0)
			goto found;

	error("segment not found");
found:
	if((len/PGSZ) > ps->size)
		error("len > segment size");

	attr &= ~SG_TYPE;		/* Turn off what is not allowed */
	attr |= ps->attr;		/* Copy in defaults */

	s = newseg(attr, va, va+len, nil, 0);
	s->pseg = ps;
	p->seg[sno] = s;

	return va;
}

void
syssegattach(Ar0* ar0, va_list list)
{
	int attr;
	char *name;
	uintptr va;
	usize len;

	/*
	 * long segattach(int, char*, void*, ulong);
	 * should be
	 * void* segattach(int, char*, void*, usize);
	 */
	attr = va_arg(list, int);
	name = va_arg(list, char*);
	va = PTR2UINT(va_arg(list, void*));
	len = va_arg(list, usize);

	ar0->v = UINT2PTR(segattach(up, attr, validaddr(name, 1, 0), va, len));
}

void
syssegdetach(Ar0* ar0, va_list list)
{
	int i;
	uintptr addr;
	Segment *s;

	/*
	 * int segdetach(void*);
	 */
	addr = PTR2UINT(va_arg(list, void*));

	qlock(&up->seglock);
	if(waserror()){
		qunlock(&up->seglock);
		nexterror();
	}

	s = 0;
	for(i = 0; i < NSEG; i++)
		if(s = up->seg[i]) {
			rlock(&s->lk);
			if((addr >= s->base && addr < s->top) ||
			   (s->top == s->base && addr == s->base))
				goto found;
			runlock(&s->lk);
		}

	error(Ebadarg);

found:
	/*
	 * Can't detach the initial stack segment
	 * because the clock writes profiling info
	 * there.
	 */
	if(s == up->seg[SSEG]){
		runlock(&s->lk);
		error(Ebadarg);
	}
	up->seg[i] = 0;
	runlock(&s->lk);
	putseg(s);
	qunlock(&up->seglock);
	poperror();

	/* Ensure we flush any entries from the lost segment */
	mmuflush();

	ar0->i = 0;
}

void
syssegfree(Ar0* ar0, va_list list)
{
	Segment *s;
	uintptr from, to;
	usize len;

	/*
	 * int segfree(void*, ulong);
	 * should be
	 * int segfree(void*, usize);
	 */
	from = PTR2UINT(va_arg(list, void*));
	s = seg(up, from, wlock);
	if(s == nil)
		error(Ebadarg);
	len = va_arg(list, usize);
	to = (from + len) & ~(PGSZ-1);
	if(to < from || to > s->top){
		wunlock(&s->lk);
		error(Ebadarg);
	}
	from = ROUNDUP(from, PGSZ);

	mfreeseg(s, from, to);
	wunlock(&s->lk);
	mmuflush();

	ar0->i = 0;
}

void
syssegflush(Ar0* ar0, va_list list)
{
	Segment *s;
	uintptr addr;
	usize l, len;

	/*
	 * int segflush(void*, ulong);
	 * should be
	 * int segflush(void*, usize);
	 */
	addr = PTR2UINT(va_arg(list, void*));
	len = va_arg(list, usize);

	while(len > 0) {
		s = seg(up, addr, rlock);
		if(s == nil)
			error(Ebadarg);

		s->flushme = 1;
		l = len;
		if(addr+l > s->top)
			l = s->top - addr;
		if(l == 0 || addr+l < s->base){
			runlock(&s->lk);
			error(Ebadarg);
		}
		pagesflush(s->pages, addr - s->base, l);	/* TO DO: check rounding-up */
		runlock(&s->lk);
		addr += l;
		len -= l;
	}
	mmuflush();
	ar0->i = 0;
}
