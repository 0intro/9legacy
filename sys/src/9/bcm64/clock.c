/*
 * bbcm2712 timers
 *	System timers run at 1MHz (timers 1 and 2 are used by GPU)
 *	ARM timer usually runs at 250MHz (may be slower in low power modes)
 *	Cycle counter runs at 700MHz (unless overclocked)
 *    All are free-running up-counters
 *  Cortex-a7 has local generic timers per cpu (which we run at 1MHz)
 *
 * Use system timer 3 (64 bits) for hzclock interrupts and fastticks
 *   For smp on bcm2836, use local generic timer for interrupts on cpu1-3
 * Use ARM timer (32 bits) for perfticks
 * Use ARM timer to force immediate interrupt
 * Use cycle counter for cycles()
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"

enum {
	SYSTIMERS	= VIRTIO+0x7c003000,

	SystimerFreq	= 1*Mhz,
	MaxPeriod	= SystimerFreq / HZ,
	MinPeriod	= 100,

};

typedef struct Systimers Systimers;

struct Systimers {
	u32int	cs;
	u32int	clo;
	u32int	chi;
	u32int	c0;
	u32int	c1;
	u32int	c2;
	u32int	c3;
};

enum {
	/* generic timer (cortex-a7) */
	Enable	= 1<<0,
	Imask	= 1<<1,
	Istatus = 1<<2,
};

static void
clockintr(Ureg *ureg, void *)
{
	Systimers *tn;

	if(m->machno != 0)
		panic("cpu%d: unexpected system timer interrupt", m->machno);
	tn = (Systimers*)SYSTIMERS;
	/* dismiss interrupt */
	tn->c3 = tn->clo - 1;
	tn->cs = 1<<3;
	timerintr(ureg, 0);
}

static void
localclockintr(Ureg *ureg, void *)
{
	if(m->machno == 0)
		panic("cpu0: Unexpected local generic timer interrupt");
	cpwrtimerphysctl(Imask|Enable);
	timerintr(ureg, 0);
}

void
clockshutdown(void)
{
	if(cpuserver)
		wdogfeed();
	else
		wdogoff();
}

void
clockinit(void)
{
	Systimers *tn;
	u32int t0, t1, tstart;
	//u64int p0, p1;

	/* generic timer supported */
	cpwrtimerphysctl(Imask);

	tn = (Systimers*)SYSTIMERS;
	tstart = tn->clo;
	do{
		t0 = lcycles();
	}while(tn->clo == tstart);
	//p0 = cprdtimerval();
	do{
		t1 = lcycles();
		//p1 = cprdtimerval();
	}while(tn->clo - tstart < 10000);
	//print("in 10ms, timer incremented %llud\n", p1 - p0);
	t1 -= t0;
	m->cpuhz = 100 * t1;
	m->cpumhz = (m->cpuhz + Mhz/2 - 1) / Mhz;
	m->cyclefreq = m->cpuhz;
	if(m->machno == 0){
		tn->c3 = tn->clo - 1;
		intrenable(IRQtimer3, clockintr, nil, 0, "clock");
	}else{
		intrenable(IRQcntpns, localclockintr, nil, 0, "clock");
	}
}

void
timerset(uvlong next)
{
	Systimers *tn;
	uvlong now;
	long period;

	now = fastticks(nil);
	period = next - now;
	if(period < MinPeriod)
		period = MinPeriod;
	else if(period > MaxPeriod)
		period = MaxPeriod;
	if(m->machno > 0){
		cpwrtimerphysval(54*period);
		cpwrtimerphysctl(Enable);
	}else{
		tn = (Systimers*)SYSTIMERS;
		tn->c3 = tn->clo + period;
	}
}

uvlong
fastticks(uvlong *hz)
{
	Systimers *tn;
	ulong lo, hi;
	uvlong now;

	if(hz)
		*hz = SystimerFreq;
	tn = (Systimers*)SYSTIMERS;
	do{
		hi = tn->chi;
		lo = tn->clo;
	}while(tn->chi != hi);
	now = (uvlong)hi<<32 | lo;
	return now;
}

ulong
perfticks(void)
{
	return fastticks(nil);
}

ulong
µs(void)
{
	if(SystimerFreq != 1*Mhz)
		return fastticks2us(fastticks(nil));
	return ((Systimers*)SYSTIMERS)->clo;
}

void
microdelay(int n)
{
	Systimers *tn;
	u32int now, diff;

	diff = n + 1;
	tn = (Systimers*)SYSTIMERS;
	now = tn->clo;
	while(tn->clo - now < diff)
		;
}

void
delay(int n)
{
	while(--n >= 0)
		microdelay(1000);
}
