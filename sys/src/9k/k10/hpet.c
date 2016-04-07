#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

typedef struct Hpet Hpet;
typedef struct Tn Tn;

struct Hpet {					/* Event Timer Block */
	u32int	cap;				/* General Capabilities */
	u32int	period;				/* Main Counter Tick Period */
	u32int	_8_[2];
	u32int	cnf;				/* General Configuration */
	u32int	_20_[3];
	u32int	sts;				/* General Interrupt Status */
	u32int	_36_[51];
	u64int	counter;				/* Main Counter Value */
	u32int	_248[2];
	Tn	tn[];				/* Timers */
};

struct Tn {					/* Timer */
	u32int	cnf;				/* Configuration */
	u32int	cap;				/* Capabilities */
	u64int	comparator;			/* Comparator */
	u32int	val;				/* FSB Interrupt Value */
	u32int	addr;				/* FSB Interrupt Address */
	u32int	_24_[2];
};

static Hpet* etb[8];				/* Event Timer Blocks */
static u64int zerostamp;
static u64int *stamper = &zerostamp;		/* hpet counter used for time stamps, or 0 if no hpet */
static u32int period;		/* period of active hpet */

uvlong
hpetticks(uvlong*)
{
	return *stamper;
}

uvlong
hpetticks2ns(uvlong ticks)
{
	return ticks*period / 1000 / 1000;
}

uvlong
hpetticks2us(uvlong ticks)
{
	return hpetticks2ns(ticks) / 1000;
}

/*
 * called from acpi
 */
void
hpetinit(uint id, uint seqno, uintmem pa, int minticks)
{
	Tn *tn;
	int i, n;
	Hpet *hpet;

	print("hpet: id %#ux seqno %d pa %#P minticks %d\n", id, seqno, pa, minticks);
	if(seqno >= nelem(etb))
		return;
	if((hpet = vmap(pa, 1024)) == nil)		/* HPET §3.2.4 */
		return;
	memreserve(pa, 1024);
	etb[seqno] = hpet;

	print("HPET: cap %#8.8ux period %#8.8ux\n", hpet->cap, hpet->period);
	print("HPET: cnf %#8.8ux sts %#8.8ux\n",hpet->cnf, hpet->sts);
	print("HPET: counter %#.16llux\n", hpet->counter);

	n = ((hpet->cap>>8) & 0x0F) + 1;
	for(i = 0; i < n; i++){
		tn = &hpet->tn[i];
		DBG("Tn%d: cnf %#8.8ux cap %#8.8ux\n", i, tn->cnf, tn->cap);
		DBG("Tn%d: comparator %#.16llux\n", i, tn->comparator);
		DBG("Tn%d: val %#8.8ux addr %#8.8ux\n", i, tn->val, tn->addr);
		USED(tn);
	}
	/*
	 * hpet->period is the number of femtoseconds per counter tick.
	 */

	/*
	 * activate the first hpet as the source of time stamps
	 */
	if(seqno == 0){
		period = hpet->period;
		stamper = &hpet->counter;
		/* the timer block must be enabled to start the main counter for timestamping */
		hpet->cap |= 1<<0;	/* ENABLE_CNF */
	}
}
