#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

enum {
	Nalarm	= 3*1000,	/* alarm when interrupt has run for this long */
};

typedef struct Mon Mon;
struct Mon {
	ulong tk;	/* tick of mach entry */
	ulong ltk;	/* local tick we started tracking it */
};

static void intrmon(Ureg*, Timer*);

/*
 * This code relies on sys->nmach being set properly.  Do not call intrmoninit on
 * any processor until *ncpu/nmach has been sorted.
 */
void
intrmoninit(void)
{
	Timer *t;

	m->intrmon.mon = mallocz(sys->nmach * sizeof (Mon), 1);
	if(m->intrmon.mon == nil) {
		iprint("intrmoninit: mon alloc failure for cpu%d\n", m->machno);
		return;
	}

	t = malloc(sizeof(*t));
	t->tmode = Trelative;
	t->tt = nil;
	/*
	 * stagger out the timers by mach no to start in
	 * order to get one running per second
	 */
	t->tns = m->machno*1000*1000*1000;
	t->tf = intrmon;
	timeradd(t);
}

static void
intrmon(Ureg*, Timer *t)
{
	Mon *mon;
	Mach *mp;
	ulong tk;
	int i;
	Mpl s;

	s = splhi();

	/* reset timer from startup to scaled out by # procs */
	t->tmode = Tperiodic;
	t->tns = sys->nmach*1000*1000*1000;

	mon = m->intrmon.mon;	/* my personal monitor array to protect others */
	for(i = 0; i < sys->nmach; i++){
		mp = sys->machptr[i];
		if(mp == nil || !mp->online)
			continue;
		tk = mp->intrmon.tk;
		if(tk == 0 || tk != mon[i].tk){
			mon[i].tk = tk;
			mon[i].ltk = m->ticks;
			continue;
		}
		if(TK2MS(m->ticks - mon[i].ltk) < Nalarm)
			continue;
		/* Lazy avoid multiple cpus reporting the same condition. */
		mp->intrmon.tk++;
		xdpanic("cpu%d intr watchdog(%d): tk %ld vno %d fn 0x%ulx\n",
			i, m->machno, mp->intrmon.tk, mp->intrmon.vno,
			mp->intrmon.fn);
	}
	splx(s);
}
