#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

static struct{
	Lock;
	RMapel	els[128];
	int	next;
} elpool;

static RMapel*
rmapnew(RMap *r)
{
	RMapel *e;

	if((e = r->free) != nil){
		r->free = e->next;
		return e;
	}
	ilock(&elpool);
	if(elpool.next >= nelem(elpool.els)){
		iunlock(&elpool);
		return nil;
	}
	e = &elpool.els[elpool.next++];
	iunlock(&elpool);
	return e;
}

void
rmapfree(RMap* rmap, uintmem addr, uintmem size)
{
	RMapel *p, *n, **lp;

	if(size == 0)
		return;

	ilock(rmap);
	p = nil;
	for(lp = &rmap->map; (n = *lp) != nil && n->addr <= addr; lp = &n->next)
		{}

	if(p != nil && p->addr+p->size > addr ||
	   n != nil && addr+size > n->addr){
		iunlock(rmap);
		print("rmapfree: overlap: %#P %#P (%lld)\n", addr, size, (u64int)size);
		return;
	}

	if(p != nil && p->addr+p->size == addr){
		p->size += size;
		if(n != nil && addr+size == n->addr){
			p->size += n->size;
			p->next = n->next;

			n->next = rmap->free;
			rmap->free = n;
		}
	}else{
		if(n != nil && addr+size == n->addr){
			n->addr -= size;
			n->size += size;
		}else{
			p = rmapnew(rmap);
			if(p != nil){
				p->addr = addr;
				p->size = size;
				p->next = n;
				*lp = p;
			}else
				print("rmapfree: %s: losing 0x%llux, %llud\n", rmap->name, (u64int)addr, (u64int)size);
		}
	}
	iunlock(rmap);
}

uintmem
rmapalloc(RMap* rmap, uintmem addr, uintmem size, uint align)
{
	RMapel *e, **lp;
	uintmem maddr, oaddr;

	ilock(rmap);
	for(lp = &rmap->map; (e = *lp) != nil; lp = &e->next){
		maddr = e->addr;

		if(addr != 0){
			/*
			 * A specific address range has been given:
			 *   if the current map entry is greater then
			 *   the address is not in the map;
			 *   if the current map entry does not overlap
			 *   the beginning of the requested range then
			 *   continue on to the next map entry;
			 *   if the current map entry does not entirely
			 *   contain the requested range then the range
			 *   is not in the map.
			 */
			if(maddr > addr)
				break;
			if(e->size < addr - maddr)	/* maddr+e->size < addr, but no overflow */
				continue;
			if(addr - maddr > e->size - size)	/* addr+size > maddr+e->size, but no overflow */
				break;
			maddr = addr;
		}

		if(align > 0)
			maddr = ((maddr+align-1)/align)*align;
		if(e->addr+e->size-maddr < size)
			continue;

		oaddr = e->addr;
		e->addr = maddr+size;
		e->size -= maddr-oaddr+size;
		if(e->size == 0){
			*lp = e->next;
			e->next = rmap->free;
			rmap->free = e;
		}

		iunlock(rmap);
		if(oaddr != maddr)
			rmapfree(rmap, oaddr, maddr-oaddr);

		return maddr;
	}
	iunlock(rmap);

	return 0;
}

int
isrmapped(RMap *r, uintmem addr, uintmem *limit)
{
	RMapel *e;

	ilock(r);
	for(e = r->map; e != nil; e = e->next){
		if(e->addr >= addr && addr <= e->addr+e->size-1){
			if(limit != nil)
				*limit = e->addr + e->size;
			iunlock(r);
			return 1;
		}
	}
	iunlock(r);
	return 0;
}

int
rmapfirst(RMap *r, uintmem start, uintmem *addr, uintmem *size)
{
	RMapel *e;
	uintmem lim;

	ilock(r);
	for(e = r->map; e != nil; e = e->next){
		lim = e->addr + e->size;
		if(e->addr <= start && start <= lim-1){
			iunlock(r);
			*addr = start;
			*size = lim-start;
			return 1;
		}
	}
	iunlock(r);
	return 0;
}

uintmem
rmapsize(RMap *r)
{
	uintmem t;
	RMapel *e;

	ilock(r);
	t = 0;
	for(e = r->map; e != nil; e = e->next)
		t += e->size;
	iunlock(r);
	return t;
}

void
rmapgaps(RMap *r2, RMap *r1)
{
	RMapel *e1;
	uintmem prev;

	ilock(r1);
	prev = 0;
	for(e1 = r1->map; e1 != nil; e1 = e1->next){
		if(prev < e1->addr)
			rmapfree(r2, prev, e1->addr - prev);
		prev = e1->addr + e1->size;
	}
	iunlock(r1);
	if(prev-1 != ~(uintmem)0)
		rmapfree(r2, prev, -prev);
}

void
rmapprint(RMap *r)
{
	RMapel *e;

	print("%s:\n", r->name);
	for(e = r->map; e != nil; e = e->next)
		print("\t%#P %#P (%llud)\n", e->addr, e->addr+e->size, (u64int)e->size);
}
