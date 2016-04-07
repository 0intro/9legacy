#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

Segment *
newseg(int type, uintptr base, uintptr top, Image *i, int isec)
{
	Segment *s;

	s = smalloc(sizeof(Segment));
	s->ref = 1;
	s->type = type;
	s->base = base;
	s->top = top;
	s->pages = newpages(PGSHFT, top-base, nil);
	if(s->pages == nil){
		free(s);
		error(Enovmem);
	}
	s->sema.prev = &s->sema;
	s->sema.next = &s->sema;
	if(i != nil){
		incref(i);
		s->image = i;
		s->isec = isec;
	}
	return s;
}

void
putseg(Segment *s)
{
	Image *i;

	if(s == 0)
		return;
	if(decref(s) != 0)
		return;
	i = s->image;
	if(i != nil)
		putimage(i);
	freepages(s->pages);
	if(s->profile != nil)
		free(s->profile);
	free(s);
}

void
relocateseg(Segment *s, uintptr offset)
{
	relocatepages(s->pages, offset);
}

Segment*
dupseg(Segment **seg, int segno, int share)
{
	Segment *n, *s;

	SET(n);
	s = seg[segno];

	rlock(&s->lk);
	if(waserror()){
		runlock(&s->lk);
		nexterror();
	}
	switch(s->type&SG_TYPE) {
	case SG_TEXT:		/* New segment shares pte set */
	case SG_SHARED:
	case SG_PHYSICAL:
		goto sameseg;

	case SG_STACK:
		n = newseg(s->type, s->base, s->top, nil, 0);
		break;

	case SG_BSS:		/* Just copy on write */
		if(share)
			goto sameseg;
		n = newseg(s->type, s->base, s->top, nil, 0);
		break;

	case SG_DATA:		/* Copy on write plus demand load info */
		if(segno == TSEG){
			poperror();
			runlock(&s->lk);
			return data2txt(s);
		}

		if(share)
			goto sameseg;
		n = newseg(s->type, s->base, s->top, s->image, s->isec);
		break;
	}
	duppages(n->pages, s->pages);

	n->flushme = s->flushme;
	if(s->ref > 1)
		procflushseg(s);	/* to force copy-on-write/copy-on-reference */
	poperror();
	runlock(&s->lk);
	return n;

sameseg:
	incref(s);
	poperror();
	runlock(&s->lk);
	return s;
}

/*
 *  called with s->lk wlocked
 */
void
mfreeseg(Segment *s, uintptr start, uintptr top)
{
	usize pages;
	uintptr soff;
	Pages *ps;
	Page *freed;

	ps = s->pages;
	pages = (top-start)>>ps->lg2pgsize;
	soff = start-s->base;
	freed = mfreepages(ps, soff, pages);
	if(s->ref > 1)
		procflushseg(s);
	freepagelist(freed);
}

Segment*
isoverlap(Proc* p, uintptr va, usize len)
{
	int i;
	Segment *ns;
	uintptr newtop;

	newtop = va+len;
	for(i = 0; i < NSEG; i++) {
		ns = p->seg[i];
		if(ns == 0)
			continue;
		if((newtop > ns->base && newtop <= ns->top) ||
		   (va >= ns->base && va < ns->top))
			return ns;
	}
	return nil;
}

Segment*
seg(Proc *p, uintptr addr, void (*dolock)(RWlock*))
{
	Segment **s, **et, *n;
	void (*dounlock)(RWlock*);

	if(dolock == wlock)
		dounlock = wunlock;
	else
		dounlock = runlock;

	et = &p->seg[NSEG];
	for(s = p->seg; s < et; s++) {
		n = *s;
		if(n == 0)
			continue;
		if(addr >= n->base && addr < n->top) {
			if(dolock == nil)
				return n;
			dolock(&n->lk);
			if(addr >= n->base && addr < n->top)
				return n;
			dounlock(&n->lk);
		}
	}

	return 0;
}

void
segclock(uintptr pc)
{
	Segment *s;

	s = up->seg[TSEG];
	if(s == 0 || s->profile == 0)
		return;

	s->profile[0] += TK2MS(1);
	if(pc >= s->base && pc < s->top) {
		pc -= s->base;
		s->profile[pc>>LRESPROF] += TK2MS(1);
	}
}

Segment*
txt2data(Proc *p, Segment *s)
{
	int i;
	Segment *ps;

	ps = newseg(SG_DATA, s->base, s->top, s->image, s->isec);
	ps->flushme = 1;

	qlock(&p->seglock);
	for(i = 0; i < NSEG; i++)
		if(p->seg[i] == s)
			break;
	if(i == NSEG)
		panic("segment gone");

	runlock(&s->lk);
	putseg(s);
	rlock(&ps->lk);
	p->seg[i] = ps;
	qunlock(&p->seglock);

	return ps;
}

Segment*
data2txt(Segment *s)
{
	Segment *ps;

	ps = newseg(SG_TEXT, s->base, s->top, s->image, s->isec);
	ps->flushme = 1;

	return ps;
}
