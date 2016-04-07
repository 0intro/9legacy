#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * Bibop table
 */
typedef struct Bibop Bibop;
struct Bibop{
	uintmem	base;
	uintmem	limit;
	uint	shift;
	uint	mlen;	/* map length */
	uchar	map[];
};

enum{
	MaxGiB=	64,
	BibUnit=	2*MiB,
	BibSize=	(MaxGiB*GiB/BibUnit),

	IsReap=	0x40,
	IsBusy=	0x80,
	SizeMask=	0xF,	/* up to 16 size codes */
};

static uint
bibopsize(uintmem base, uintmem limit, uint lg2size)
{
	return sizeof(Bibop)+((limit-base)>>lg2size);
}

Bibop*
bibopalloc(uintmem base, uintmem limit, uint lg2size, void* (*alloc)(uint))
{
	uint need;
	Bibop *b;

	need = bibopsize(base, limit, lg2size);
	b = alloc(need);
	if(b == nil)
		panic("needed %ud bytes for bibop table", need);
	b->base = base;
	b->limit = limit;
	b->shift = lg2size;
	b->mlen = need-sizeof(Bibop);
	memset(b->map, 0, b->mlen);
	return b;
}

int
bibopstate(Bibop *b, uintmem pa)
{
	if(pa == 0 || !(pa >= b->base && pa < b->limit))
		return -1;
	return b->map[(pa - b->base)>>b->shift];
}

/*
 * set the state for memory from pa0 to pa1 (half-open) to s
 */
void
bibopsetstate(Bibop *b, uintmem pa0, uintmem pa1, int s)
{
	int i, j;

	s |= IsBusy;
	if(pa == 0 || !(pa0 >= b->base && pa0 < b->limit))
		return;
	if(pa1 > b->limit)
		pa1 = b->limit;
	i = (pa0 - b->base) >> b->shift;
	j = (pa1 - b->base) >> b->shift;
	for(i = 0; i < j; i++)
		b->map[i] = s;
}

static Reap	reaps[~IsReap & 0xFF];

Reap*
findreap(uintmem pa)
{
	int s;

	s = memstatus(pa);
	if(s < 0 || (s&IsReap) == 0)
		return nil;
	return &reap[s & ~IsReap];
}
