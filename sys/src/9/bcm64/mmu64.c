/*
 * armv8-a aarch64 memory management
 * initial implementation: minimally evolved from armv7 LPAE
 *
 * Three levels of page tables:
 * L0 table: we use <128 entries, each mapping 1GiB virtual space, as either
 *   a contiguous block, or
 *   an L1 page: 512 entries, each mapping 2MiB, as either
 *     a contiguous block, or
 *     an L2 page: 512 entries, each mapping 4KiB
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../bcm/arm.h"

typedef uvlong LPTE;

#define L0X(va)		FEXT((va), 30, 8)
#define L1X(va)		FEXT((va), 21, 12)
#define L2X(va)		FEXT((va), 12, 9)

enum {
	L1lo		= L1X(UZERO),
	L1hi		= L1X(USTKTOP+2*MiB-1),
	L2size		= BY2PG,

	LXN			= 1LL<<54,
	LPXN		= 1LL<<53,
	LAF			= 1<<10,
	LShareOuter	= 2<<8,
	LShareInner	= 3<<8,
	LKrw		= 0<<6,
	LUrw		= 1<<6,
	LUro		= 3<<6,
	LMem		= 0<<2,
	LUncached	= 1<<2,
	LDevice		= 2<<2,
	LBlock		= 1<<0,
	LTable		= 3<<0,
	LPage		= 3<<0,

	MMem		= 0xFF,	/* MEM_WB */
	MUncached	= 0x44,	/* MEM_UC */
	MDevice		= 0x04,	/* DEV_nGnRE*/

	EAE_LPAE	= 1<<31,
	SH0_INNER	= 3<<12,
	ORGN0_WB	= 1<<10,
	IRGN0_WB	= 1<<8,
 
	LAttrDRAM	= LKrw | LAF | LShareOuter | LMem,
	LAttrPhys	= LPXN | LAF | LShareOuter | LUncached,
	LAttrIO		= LKrw | LAF | LXN | LPXN | LShareOuter | LDevice,
	LAttrUser	= LPXN | LAF | LShareInner | LMem,
};

#define XPPN(pa)	((pa)&~(BY2PG-1) | (uvlong)((pa)&PTEHIMEM) << (32-8))
#define LPPN(pte)	((pte)&0xFFFFFF000ULL)

/*
 * TTBCR and MAIR0 register settings for LPAE (used in armv7.s)
 */
ulong mair0 = MMem<<0 | MUncached<<8 | MDevice<<16;
ulong ttbcr = EAElpae | SH0inner | ORGN0wb | IRGN0wb;

/*
 * TCR_EL1 and MAIR_EL1 settings for aarch64 (used in l.s)
 */
uvlong tcr_el1 =
	2ull << 32 |		/* phys address 40 bits */
	2 << 30 |		/* ttbr1 granularity 4k */
	3 << 28 |		/* ttbr1 share inner */
	1 << 23 |		/* disable ttbr1 */
	(64-37)<<16 |	/* ttbr1 region size */
	0 << 14 |		/* ttbr0 granularity 4k */
	3 << 12 |		/* ttbr0 share inner */
	1 << 10 |		/* ttbr0 outer cache wb */
	1 << 8  |		/* ttbr0 inner cache wb */
	(64-37);		/* ttbr0 region size */

uvlong mair_el1 = MMem<<0 | MUncached<<8 | MDevice<<16;

/*
 * Set up initial PTEs
 *   Called before main, with mmu off, to initialise cpu0's tables
 *   Called from launchinit to clone cpu0's tables for other cpus
 */
void
mmuinit(ulong a)
{
	LPTE *l0, *l1;
	uintptr pa, va;
	int i;

	l1 = (LPTE*)(uintptr)a;
	if((uintptr)l1 != PADDR(L1))
		memmove(l1, m->mmul1, L1SIZE);
	else
		memset(l1, 0, L1SIZE);

	/*
	 * embed L0 table as a page of L1 table
	 * first page of L1 table also serves as L2 table page
	 * such that L2 tables are aliased at kernel address L2VA
	 */
	l0 = &l1[L1X(L2VA)];
	pa = PADDR(l1);
	for(i = 0; i < 5; i++){
		l0[i] = pa|LAttrDRAM|LTable;
		pa += BY2PG;
	}
	/* temporary identity map for low 1GB of RAM */
	pa = PHYSDRAM;
	l0[0] = pa|LAttrDRAM|LBlock;
	if((uintptr)l1 != PADDR(L1))
		return;

	/* kernel mapping for 16GB of RAM at KZERO */
	pa = PHYSDRAM;
	va = KZERO;
	for(i = 0; i < 16; i++){
		l0[L0X(va)] = pa|LAttrDRAM|LBlock;
		va += GiB;
		pa += GiB;
	}
	/* map first 1GB of RAM uncached at 1 4000 0000 */
	pa = PHYSDRAM;
	va = UCKZERO;
	l0[L0X(va)] = pa|LAttrPhys|LBlock;
	/*
	 * map 4G for I/O registers at VIRTIO = 06 0000 0000
	 * first half maps to local bus at 10 0000 0000 - 10 7FFF FFFF
	 * second half maps to RP1 in PCI window at 1C 0000 0000 - 1C 7FFF FFFF
	 */
	pa = 0x1000000000ull;
	va = VIRTIO;
	for(i = 0; i < 2; i++){
		l0[L0X(va)] = pa|LAttrIO|LBlock;
		l0[L0X(pa)] = pa|LAttrIO|LBlock;
		va += GiB;
		pa += GiB;
	}
	pa = soc.pcispace;
	for(; i < 4; i++){
		l0[L0X(va)] = pa|LAttrIO|LBlock;
		va += GiB;
		pa += GiB;
	}
}

void
mmuinit1()
{
	LPTE *l0, *l1;
	uintptr pa;

	/*
	 * undo identity mapping for first 1GB of RAM
	 */
	l1 = m->mmull1;
	l0 = &l1[L1X(L2VA)];
	pa = PADDR(l1);
	l0[0] = pa|LAttrDRAM|LTable;
	cachedwbtlb(l0, sizeof(*l0));

	mmuinvalidate();
}

static int
nonzero(uintptr *p)
{
	int i;
	for(i = 0; i < BY2PG/sizeof(uintptr); i++)
		if(*p++ != 0)
			return 1;
	return 0;
}

static void
mmul2empty(Proc* proc, int clear)
{
	LPTE *l1;
	Page **l2, *page;
	KMap *k;

	l1 = m->mmull1;
	l2 = &proc->mmul2;
	for(page = *l2; page != nil; page = page->next){
		if(clear){
			k = kmap(page);
			memset((void*)VA(k), 0, L2size);
			kunmap(k);
		}
		l1[page->daddr] = Fault;
		l2 = &page->next;
	}
	coherence();
	*l2 = proc->mmul2cache;
	proc->mmul2cache = proc->mmul2;
	proc->mmul2 = nil;
}

static void
mmul1empty(void)
{
	LPTE *l1;

	/* clean out any user mappings still in l1 */
	if(m->mmul1lo > 0){
		if(m->mmul1lo == 1)
			m->mmull1[L1lo] = Fault;
		else
			memset(&m->mmull1[L1lo], 0, m->mmul1lo*sizeof(LPTE));
		m->mmul1lo = 0;
	}
	if(m->mmul1hi > 0){
		l1 = &m->mmull1[L1hi - m->mmul1hi];
		if(m->mmul1hi == 1)
			*l1 = Fault;
		else
			memset(l1, 0, m->mmul1hi*sizeof(LPTE));
		m->mmul1hi = 0;
	}
	if(m->kmapll2 != nil)
		memset(m->kmapll2, 0, NKMAPS*sizeof(LPTE));
}

void
mmuswitch(Proc* proc)
{
	int x;
	LPTE *l1;
	Page *page;

	if(proc != nil && proc->newtlb){
		mmul2empty(proc, 1);
		proc->newtlb = 0;
	}

	mmul1empty();

	/* move in new map */
	l1 = m->mmull1;
	if(proc != nil){
	  for(page = proc->mmul2; page != nil; page = page->next){
		x = page->daddr;
		l1[x] = XPPN(page->pa)|LAttrDRAM|LTable;
		if(x >= L1lo + m->mmul1lo && x < L1hi - m->mmul1hi){
			if(x+1 - L1lo < L1hi - x)
				m->mmul1lo = x+1 - L1lo;
			else
				m->mmul1hi = L1hi - x;
		}
	  }
	  if(proc->nkmap)
		memmove(m->kmapll2, proc->kmapltab, sizeof(proc->kmapltab));
	}

	/* make sure map is in memory */
	/* could be smarter about how much? */
	cachedwbtlb(&l1[L1X(UZERO)], (L1hi - L1lo)*sizeof(LPTE));
	if(proc != nil && proc->nkmap)
		cachedwbtlb(m->kmapll2, sizeof(proc->kmapltab));

	/* lose any possible stale tlb entries */
	mmuinvalidate();
}

void
flushmmu(void)
{
	int s;

	s = splhi();
	up->newtlb = 1;
	mmuswitch(up);
	splx(s);
}

void
mmurelease(Proc* proc)
{
	Page *page, *next;

	mmul2empty(proc, 0);
	for(page = proc->mmul2cache; page != nil; page = next){
		next = page->next;
		if(--page->ref)
			panic("mmurelease: page->ref %d", page->ref);
		pagechainhead(page);
	}
	if(proc->mmul2cache && palloc.r.p)
		wakeup(&palloc.r);
	proc->mmul2cache = nil;

	mmul1empty();

	/* make sure map is in memory */
	/* could be smarter about how much? */
	cachedwbtlb(&m->mmull1[L1X(UZERO)], (L1hi - L1lo)*sizeof(LPTE));

	/* lose any possible stale tlb entries */
	mmuinvalidate();
}

void
putmmu(ulong va, ulong pa, Page* page)
{
	int x, s;
	Page *pg;
	LPTE *l1, *pte;
	LPTE nx;
	static int chat = 0;
	int fromcache = 0;

	/*
	 * disable interrupts to prevent flushmmu (called from hzclock)
	 * from clearing page tables while we are setting them
	 */
	s = splhi();
	x = L1X(va);
	l1 = &m->mmull1[x];
	pte = (LPTE*)(L2VA + L2size*x);
	if(*l1 == Fault){
		/* l2 pages have 512 entries */
		if(up->mmul2cache == nil){
			spllo();
			pg = newpage(1, 0, 0);
			splhi();
			/* if newpage slept, we might be on a different cpu */
			l1 = &m->mmull1[x];
		}else{
			fromcache = 1;
			pg = up->mmul2cache;
			up->mmul2cache = pg->next;
		}
		pg->daddr = x;
		pg->next = up->mmul2;
		up->mmul2 = pg;

		*l1 = XPPN(pg->pa)|LAttrDRAM|LTable;
		cachedwbtlb(l1, sizeof *l1);

		/* force l2 page to memory */
		if(nonzero((uintptr*)pte)){
			if(chat++ < 32)
				iprint("nonzero L2 page from %s pa %lux va %lux daddr %lux\n",
						fromcache? "cache" : "newpage", pg->pa, pg->va, pg->daddr);
			memset(pte, 0, L2size);
		}
		cachedwbtlb(pte, L2size);

		if(x >= L1lo + m->mmul1lo && x < L1hi - m->mmul1hi){
			if(x+1 - L1lo < L1hi - x)
				m->mmul1lo = x+1 - L1lo;
			else
				m->mmul1hi = L1hi - x;
		}
	}

	/* protection bits are
	 *	PTERONLY|PTEVALID;
	 *	PTEWRITE|PTEVALID;
	 *	PTEWRITE|PTEUNCACHED|PTEVALID;
	 */
	nx = LAttrUser|LPage;
	if(pa & PTEUNCACHED)
		nx = LAttrPhys|LPage;
	if(pa & PTEWRITE)
		nx |= LUrw;
	else
		nx |= LUro;
	pte[L2X(va)] = XPPN(pa)|nx;
	if(0)if(chat++ < 32)
		iprint("putmmu %lux %lux => %llux\n", va, pa, pte[L2X(va)]);
	cachedwbtlb(&pte[L2X(va)], sizeof(LPTE));

	/* clear out the current entry */
	mmuinvalidateaddr(va);

	if(page->cachectl[m->machno] == PG_TXTFLUSH){
		/* pio() sets PG_TXTFLUSH whenever a text pg has been written */
		cachedwbse((void*)VA(kmap(page)), BY2PG);
		cacheiinvse((void*)page->va, BY2PG);
		page->cachectl[m->machno] = PG_NOFLUSH;
	}
	//checkmmu(va, pa);
	splx(s);
}

/*
 * With 64-bit kernel, all memory is addressable with KADDR.
 * The remaining use of cankaddr is to limit (in xinit) the
 * number of kernel pages, to ensure that malloc'ed memory
 * is addressible with 32 bits.
 */
uintptr
cankaddr(uintptr pa)
{
	if(pa == 0 || pa > 1ull<<32)
		return 0;
	return 1ull<<32 - pa;
}

uintptr
mmukmap(uintptr va, uintptr pa, usize size)
{
	uint o;
	LPTE *l0, *l1, *pte;

	print("mmukmapx %#p => %#p (%lux)\n", va, pa, size);
	l1 = (LPTE*)L1;
	l0 = &l1[L1X(L2VA)];
	o = pa & (GiB-1);
	pa -= o;
	pte = &l0[L0X(va)];
	*pte = pa|LAttrIO|LBlock;
	cachedwbse(pte, sizeof *pte);	/* just needs barrier? */
	return va + o;
}

void
checkmmu(ulong va, ulong pa)
{
	int x;
	LPTE *l1, *pte;

	x = L1X(va);
	l1 = &m->mmull1[x];
	if((*l1&LTable) != LTable){
		iprint("checkmmu cpu%d va=%#lux l1 %p=%llux\n", m->machno, va, l1, *l1);
		return;
	}
	pte = (LPTE*)(L2VA + L2size*x);
	pte += L2X(va);
	if(pa == ~0 || (pa != 0 && LPPN(*pte) != XPPN(pa))){
		iprint("checkmmu va=%#lux pa=%lux l1 %p=%llux pte %p", va, pa, l1, *l1, pte);
		iprint("=%llux\n", *pte);
	}
}

KMap*
kmap(Page *p)
{
	uvlong pa;

	pa = XPPN(p->pa);
	return (KMap*)KADDR(pa);
}

void kunmap(KMap*) {}

/*
 * Append pages with physical addresses above 4GiB to the freelist
 */
void
lpapageinit(void)
{
	int j;
	ulong npage;
	Page *pages, *p;
	uvlong pa;

	npage = conf.himem.npage;
	if(npage == 0)
		return;
	
	pages = xalloc(npage*sizeof(Page));
	if(pages == 0)
		panic("lpapageinit");

	p = pages;
	pa = conf.himem.base;
	for(j=0; j<npage; j++){
		p->prev = p-1;
		p->next = p+1;
		p->pa = pa;
		p->pa |= (pa >> (32-8)) & PTEHIMEM;
		p++;
		pa += BY2PG;
	}
	palloc.tail->next = pages;
	pages[0].prev = palloc.tail;
	palloc.tail = p - 1;
	palloc.tail->next = 0;

	palloc.freecount += npage;
	palloc.user += npage;

	/* Paging numbers */
	swapalloc.highwater = (palloc.user*5)/100;
	swapalloc.headroom = swapalloc.highwater + (swapalloc.highwater/4);

	print("%ldM extended memory: ", npage*(BY2PG/1024)/1024);
	print("%ldM user total\n", palloc.user*(BY2PG/1024)/1024);
}
