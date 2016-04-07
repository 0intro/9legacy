#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

#include "../port/error.h"

Palloc palloc;

static	uint	highwater;	/* TO DO */

static	void pageblanks(usize, int);
static	Page*	blankpage(uint);

void
pageinit(void)
{
	uintmem avail;
	uvlong pkb, kkb, kmkb, mkb;

	avail = sys->pmpaged;	/* could include a portion of unassigned memory */
	palloc.user = avail/PGSZ;	/* fairly arbitrary: mainly sets highwater */
print("pmoccupied: %lld unassigned: %lld\n", sys->pmoccupied, sys->pmunassigned);

print("user=%#lud\n", palloc.user);
	/* keep 4% reserve for copy-on-write, but cap it */
	highwater = (palloc.user*4)/100;
	if(highwater > 16*MB/PGSZ)
		highwater = 16*MB/PGSZ;

	/* user, kernel, kernel malloc area, memory */
	pkb = palloc.user*PGSZ/KiB;
	kkb = ROUNDUP((uintptr)end - KTZERO, PGSZ)/KiB;
	kmkb = ROUNDDN(sys->vmunmapped - (uintptr)end, PGSZ)/KiB;
	mkb = sys->pmoccupied/KiB;

	print("%lldM memory: %lldK+%lldM kernel,"
		" %lldM user, %lldM uncommitted\n",
		mkb/KiB, kkb, kmkb/KiB, pkb/KiB, (mkb-kkb-kmkb-pkb)/KiB
	);
}

/*
 * allocate and return a new page for page set s;
 * return nil iff s was locked on entry and had to be unlocked to wait for memory.
 */
Page*
newpage(int clear, uint lg2pgsize, RWlock *locked)
{
	Page *p;
	KMap *k;
	Pallocpg *pg;
	int hw, dontalloc;
	uintmem pa;

	pg = &palloc.avail[lg2pgsize];
	lock(pg);
	hw = highwater >> (lg2pgsize-PGSHFT);
	if(up == nil || up->kp || locked != nil && !clear)
		hw = 0;
	for(;;){
		if(pg->freecount > hw)
			break;
		DBG("freec %lud hw %ud\n", pg->freecount, hw);

		/* try allocating a suitable page */
		pa = physalloc(1<<lg2pgsize);
		if(pa != 0){
			DBG("newpa %#P\n", pa);
			p = pg->blank;
			if(p != nil){
				pg->blank = p->next;
				p->next = nil;
			}else
				p = blankpage(lg2pgsize);
			p->pa = pa;
			p->ref = 1;
			p->mdom = 0;	/* TO DO */
			pg->count++;
			unlock(pg);
			goto Clear;
		}

		unlock(pg);
		dontalloc = 0;
		if(locked != nil) {
			runlock(locked);
			locked = nil;
			dontalloc = 1;
		}
		qlock(&pg->pwait);	/* Hold memory requesters here */

		if(!waserror()){
			tsleep(&pg->r, ispages, pg, 300);
			poperror();
		}

		if(!ispages(pg) && up->procctl != Proc_exitbig){
			print("out of physical memory %dK\n", 1<<lg2pgsize);
			killbig("out of memory");
		}

		qunlock(&pg->pwait);

		/*
		 * If called from fault and we lost the segment from
		 * underneath don't waste time allocating and freeing
		 * a page. Fault will call newpage again when it has
		 * reacquired the segment locks
		 */
		if(dontalloc)
			return nil;

		lock(pg);
	}

	p = pg->head;
	pg->head = p->next;
	pg->freecount--;

	if(p->ref != 0)
		panic("newpage: %#p: p->ref %d != 0", p, p->ref);

	p->ref = 1;
	mmucachectl(p, PG_NOFLUSH);
	unlock(pg);

Clear:
	if(clear) {
		k = kmap(p);
		memset(VA(k), 0, pagesize(p));
		kunmap(k);
	}

	return p;
}

int
physmemavail(uintptr need)
{
	return (SEGMAPSIZE*PTEPERTAB) > (need/PGSZ);
}

int
ispages(void *a)
{
	return ((Pallocpg*)a)->freecount > highwater;
}

void
putpage(Page *p)
{
	Pallocpg *pg;

	if(decref(p) != 0)
		return;

	pg = &palloc.avail[p->lg2size];
	lock(pg);

	p->next = pg->head;
	pg->head = p;
	pg->freecount++;

	if(pg->r.p != 0)
		wakeup(&pg->r);

	unlock(pg);
}

void
copypage(Page *f, Page *t)
{
	KMap *ks, *kd;

	if(f->lg2size != t->lg2size)
		panic("copypage");
	ks = kmap(f);
	kd = kmap(t);
	memmove(VA(kd), VA(ks), pagesize(t));
	kunmap(ks);
	kunmap(kd);
}

Pte*
ptecpy(Pte *old)
{
	Pte *new;
	Page **src, **dst, *pg;

	new = ptealloc();
	dst = &new->pages[old->first-old->pages];
	new->first = dst;
	for(src = old->first; src <= old->last; src++, dst++){
		if((pg = *src) != nil){
			incref(pg);
			new->last = dst;
			*dst = pg;
		}
	}
	return new;
}

Pte*
ptealloc(void)
{
	Pte *new;

	new = smalloc(sizeof(Pte));
	new->first = &new->pages[PTEPERTAB];
	new->last = new->pages;
	return new;
}

void
freepte(void (*fn)(Page*), Pte *p)
{
	Page **pg, **ptop;

	if(fn != nil){
		ptop = &p->pages[PTEPERTAB];
		for(pg = p->pages; pg < ptop; pg++) {
			if(*pg == 0)
				continue;
			(*fn)(*pg);
			*pg = 0;
		}
	}else{
		for(pg = p->first; pg <= p->last; pg++)
			if(*pg) {
				putpage(*pg);
				*pg = 0;
			}
	}
	free(p);
}

void
pteflush(Pte *pte, int s, int e)
{
	int i;
	Page *p;

	for(i = s; i < e; i++) {
		p = pte->pages[i];
		if(p != nil)
			mmucachectl(p, PG_TXTFLUSH);
	}
}

static void
pageblanks(usize n, int lg2size)
{
	Pallocpg *pg;
	Page *p, *pages;
	int j;

	pages = malloc(n*sizeof(Page));
	if(pages == 0)
		panic("pageblanks");

	pg = &palloc.avail[lg2size];
	p = pages;
	for(j=0; j<n; j++){
		p->next = nil;
		p->pa = 0;
		p->lg2size = lg2size;
		p->mdom = 0;		/* TO DO */
		p->next = pg->blank;
		pg->blank = p;
		p++;
	}
}

static Page*
blankpage(uint lg)
{
	Pallocpg *pg;
	Page *p;

	pg = &palloc.avail[lg];
	while((p = pg->blank) == nil)
		pageblanks(256, lg);
	pg->blank = p->next;
	p->next = nil;
	p->pa = 0;
	p->mdom = 0;		/* TO DO */
	p->lg2size = lg;
	return p;
}

char*
seprintpagestats(char *s, char *e)
{
	Pallocpg *pg;
	int i;

	for(i = 0; i < nelem(palloc.avail); i++){
		pg = &palloc.avail[i];
		lock(pg);
		if(pg->freecount != 0)
			s = seprint(s, e, "%lud/%lud %dK user pages avail\n",
				pg->freecount,
				pg->count, (1<<i)/KiB);
		unlock(pg);
	}
	return s;
}

void
pagewake(void)
{
	Pallocpg *pg;
	int i;

	for(i=0; i<nelem(palloc.avail); i++){
		pg = &palloc.avail[i];
		if(pg->r.p != nil)
			wakeup(&pg->r);
	}
}

/*
 * return the Page containing the given offset in Page set s,
 * which must be locked or unchanging;
 * returns nil if page is not allocated.
 */
Page*
segoff2page(Pages *s, uintptr soff)
{
	Pte *pte;

	if(soff >= s->xsize)
		return nil;
	pte = s->map[soff/s->ptemapmem];
	if(pte == nil)
		return nil;
	return pte->pages[(soff&(s->ptemapmem-1))>>s->lg2pgsize];
}

Page*
segva2page(Segment *s, uintptr va)
{
	if(!(va >= s->base && va <= s->top-1))
		return nil;
	return segoff2page(s->pages, va - s->base);
}

/*
 * allocate a new set of Pages
 */
Pages*
newpages(int lg2pgsize, uintptr size, void (*freepage)(Page*))
{
	Pages *ps;
	int mapsize, npages;

	if(size & ((1<<lg2pgsize)-1))
		panic("newpages %#p %d", size, lg2pgsize);

	npages = size>>lg2pgsize;
	if(npages > (SEGMAPSIZE*PTEPERTAB))
		return nil;

	mapsize = HOWMANY(npages, PTEPERTAB);
	ps = smalloc(sizeof(*ps) + mapsize*sizeof(Pte*));
//	if(waserror()){
//		free(ps);
//		nexterror();
//	}

	ps->mapsize = mapsize;
	ps->xsize = size;
	ps->npages = npages;
	ps->lg2pgsize = lg2pgsize;
	ps->ptemapmem = PTEPERTAB<<ps->lg2pgsize;
	ps->freepage = freepage;

//	poperror();
	return ps;
}

/*
 * caller must hold lock on structure that owns s
 */
void
duppages(Pages *n, Pages *s)
{
	uint size;
	Pte *pte;
	int i;

	size = s->mapsize;
	for(i = 0; i < size; i++)
		if((pte = s->map[i]) != nil)
			n->map[i] = ptecpy(pte);
}

void
freepages(Pages *ps)
{
	Pte **pp, **emap;

	emap = &ps->map[ps->mapsize];
	for(pp = ps->map; pp < emap; pp++)
		if(*pp)
			freepte(ps->freepage, *pp);
	free(ps);
}

void
addpage(Pages *ps, uintptr soff, Page *p)
{
	Pte **pte;
	Page **pg;

	/* no lock, since this is called only during initialisation */

	if(soff >= ps->npages)
		panic("addpage");
	pte = &ps->map[soff/ps->ptemapmem];
	if(*pte == 0)
		*pte = ptealloc();

	pg = &(*pte)->pages[(soff&(ps->ptemapmem-1))>>ps->lg2pgsize];
	*pg = p;
	if(pg < (*pte)->first)
		(*pte)->first = pg;
	if(pg > (*pte)->last)
		(*pte)->last = pg;
}

/*
 * free a range of pages in a page set, and return the list,
 * for re-use, or to be freed after synchronisation.
 * any locks needed are in the structure that refers to the page set,
 * and that structure is also responsible for synchronising MMUs.
 */
Page*
mfreepages(Pages *ps, uintptr soff, usize pages)
{
	int i, j, size;
	Page *pg;
	Page *list;

	j = (soff&(ps->ptemapmem-1))>>ps->lg2pgsize;

	size = ps->mapsize;
	list = nil;
	for(i = soff/ps->ptemapmem; i < size; i++) {
		if(pages <= 0)
			break;
		if(ps->map[i] == 0) {
			pages -= PTEPERTAB-j;
			j = 0;
			continue;
		}
		while(j < PTEPERTAB) {
			pg = ps->map[i]->pages[j];
			if(pg != nil){
				pg->next = list;
				list = pg;
				ps->map[i]->pages[j] = nil;
			}
			if(--pages == 0)
				return list;
			j++;
		}
		j = 0;
	}
	return list;
}

void
freepagelist(Page *list)
{
	Page *pg;

	for(pg = list; pg != nil; pg = list){
		list = list->next;
		putpage(pg);
	}
}

/*
 * caller must mmuflush
 */
void
pagesflush(Pages *ps, uintptr soff, uintptr len)
{
	Pte *pte;
	usize chunk, l, sp, ep;
	uintptr pgsize;

	pgsize = 1<<ps->lg2pgsize;
	l = len >> ps->lg2pgsize;
	while(l != 0){
		pte = ps->map[soff/ps->ptemapmem];
		sp = soff & (ps->ptemapmem-1);
		ep = ps->ptemapmem;
		if(sp-ep > l){
			ep = sp + l;
			ep = (ep+pgsize-1)&~(pgsize-1);
		}
		if(sp == ep)
			error(Ebadarg);

		if(pte)
			pteflush(pte, sp/pgsize, ep/pgsize);

		chunk = ep-sp;
		len -= chunk;
		soff += chunk;
	}
}

void
relocatepages(Pages *s, uintptr offset)
{
	/* TO DO: remove */
	USED(s);
	USED(offset);
}

Pages*
growpages(Pages *ps, uintptr newsize)
{
	Pages *ns;

	DBG("growpages %#p -> %#p\n", ps->xsize, newsize);
	ns = newpages(ps->lg2pgsize, newsize, ps->freepage);
	if(ns == nil)
		return nil;
	memmove(ns->map, ps->map, ps->mapsize*sizeof(Pte*));
	free(ps);
	return ns;
}

void
printpages(Pages *ps)
{
	int i;
	Pte *pte;
	Page **pg;

	print("pid %d pages %#p xsize %#p npages %ld mapsize %d\n",
		up->pid, ps, ps->xsize, ps->npages, ps->mapsize);
	for(i = 0; i < ps->mapsize; i++){
		pte = ps->map[i];
		if(pte != nil){
			print("%d: %#p\n", i, pte);
			for(pg = pte->first; pg <= pte->last; pg++)
				if(*pg)
					print("%#p %#p [%d] %#P\n", (uintptr)(pg - pte->pages), *pg, (*pg)->ref, (*pg)->pa);
		}
	}
}
