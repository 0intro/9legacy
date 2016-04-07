#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "../port/edf.h"

typedef struct Glares Glares;
typedef struct Glaring Glaring;

struct Glaring
{
	Lock*	l;
	ulong	ticks;
	ulong	lastlock;
	ulong	key;
	uintptr	lpc;
	uintptr	pc;
	ulong	n;
	int	ok;
	int	shown;
};

struct Glares
{
	Glaring	instance[30];
	uint	next;
	uint	total;
};

static Glares	glares[MACHMAX];	/* per core, to avoid lock */
int	lockdebug = 1;

uvlong maxlockcycles;
uvlong maxilockcycles;
uintptr maxlockpc;
ulong maxilockpc;

void	showlockloops(void);

struct
{
	ulong	locks;
	ulong	glare;
	ulong	inglare;
} lockstats;

static void
dumplockmem(char *tag, Lock *l)
{
	uchar *cp;
	int i;

	iprint("%s: ", tag);
	cp = (uchar*)l;
	for(i = 0; i < 64; i++)
		iprint("%2.2ux ", cp[i]);
	iprint("\n");
}

static void
lockcrash(Lock *l, uintptr pc, char *why)
{
	Proc *p;

	p = l->p;
	if(lockdebug > 1){
		dumpaproc(up);
		if(p != nil)
			dumpaproc(p);
	}
	showlockloops();
	panic("cpu%d: %s lock %#p key %#ux pc %#p proc %ud held by pc %#p proc %ud\n",
		m->machno, why, l, l->key, pc, up->pid, l->pc, p? p->pid: 0);
}

/*
 * A "lock loop" is excessive delay in obtaining a spin lock:
 * it could be long delay through contention (ie, inefficient but harmless),
 * or a real deadlock (a programming error);
 * record them for later analysis to discover which.
 * Don't print them at the time, or the harmless cases become deadly.
 */
static Glaring*
lockloop(Glaring *og, Lock *l, uintptr pc)
{
	Glares *mg;
	Glaring *g;
	int i, s;
	

	s = splhi();
	if(l->m == sys->machptr[m->machno])
		lockcrash(l, pc, "deadlock/abandoned");	/* recovery is impossible */
	mg = &glares[m->machno];
	g = mg->instance;
	for(i = 0; i < nelem(mg->instance) && g->l != nil; i++){
		if(g->l == l && g->lpc == l->pc && g->pc == pc){
			g->ok = 0;
			if(og == g){
				if(tickscmp(sys->ticks, g->lastlock) >= 60*HZ)
					lockcrash(l, pc, "stuck");	/* delay is hopelessly long: we're doomed, i tell ye */
			}else{
				g->lastlock = sys->ticks;
				g->n++;
				g->shown = 0;
			}
			splx(s);
			return g;
		}
		g++;
	}
	i = mg->next;
	g = &mg->instance[i];
	g->ticks = sys->ticks;
	g->lastlock = g->ticks;
	g->l = l;
	g->pc = pc;
	g->lpc = l->pc;
	g->n = 1;
	g->ok = 0;
	g->shown = 0;
	if(++i >= nelem(mg->instance))
		i = 0;
	mg->next = i;
	mg->total++;
	splx(s);
	if(islo() && up != nil)
		iprint("cpu%d: slow locks: %d\n", m->machno, glares[m->machno].total);
	if(lockdebug)
		lockcrash(l, pc, "stuck");
	return g;
}

void
showlockloops(void)
{
	Glares *mg;
	Glaring *g;
	int mno, i, p;

	p = 0;
	for(mno = 0; mno < nelem(glares); mno++){
		mg = &glares[mno];
		g = mg->instance;
		for(i = 0; i < nelem(mg->instance) && g->l != nil; i++){
			if(!g->shown){
				g->shown = 0;
				iprint("cpu%d: %d: l=%#p lpc=%#p pc=%#p n=%lud ok=%d\n",
					mno, i, g->l, g->lpc, g->pc, g->n, g->ok);
			}
			g++;
			p++;
		}
	}
	if(p == 0)
		print("no loops\n");
}

int
lock(Lock *l)
{
	int i;
	uintptr pc;
	Glaring *g;

	pc = getcallerpc(&l);

	lockstats.locks++;
	if(up)
		up->nlocks++;	/* prevent being scheded */
	if(TAS(&l->key) == 0){
		if(up)
			up->lastlock = l;
		l->pc = pc;
		l->p = up;
		l->isilock = 0;
		return 0;
	}
	if(up)
		up->nlocks--;

	g = nil;
	lockstats.glare++;
	for(;;){
		lockstats.inglare++;
		i = 0;
		while(l->key){
			if(sys->nonline < 2 && up && up->edf && (up->edf->flags & Admitted)){
				/*
				 * Priority inversion, yield on a uniprocessor; on a
				 * multiprocessor, the other processor will unlock
				 */
				print("inversion %#p pc %#p proc %d held by pc %#p proc %d\n",
					l, pc, up ? up->pid : 0, l->pc, l->p ? l->p->pid : 0);
				up->edf->d = todget(nil);	/* yield to process with lock */
			}
			if(i++ > 100*1000*1000){
				g = lockloop(g, l, pc);
				i = 0;
			}
		}
		if(up)
			up->nlocks++;
		if(TAS(&l->key) == 0){
			if(up)
				up->lastlock = l;
			l->pc = pc;
			l->p = up;
			l->isilock = 0;
			return 1;
		}
		if(up)
			up->nlocks--;
	}
}

void
ilock(Lock *l)
{
	Mreg s;
	uintptr pc;

	pc = getcallerpc(&l);
	lockstats.locks++;

	s = splhi();
	if(TAS(&l->key) != 0){
		lockstats.glare++;
		/*
		 * Cannot also check l->pc, l->m, or l->isilock here
		 * because they might just not be set yet, or
		 * (for pc and m) the lock might have just been unlocked.
		 */
		for(;;){
			lockstats.inglare++;
			splx(s);
			while(l->key)
				;
			s = splhi();
			if(TAS(&l->key) == 0)
				goto acquire;
		}
	}
acquire:
	m->ilockdepth++;
	m->ilockpc = pc;
	if(up)
		up->lastilock = l;
	l->sr = s;
	l->pc = pc;
	l->p = up;
	l->isilock = 1;
	l->m = m;
}

int
canlock(Lock *l)
{
	if(up)
		up->nlocks++;
	if(TAS(&l->key)){
		if(up)
			up->nlocks--;
		return 0;
	}

	if(up)
		up->lastlock = l;
	l->pc = getcallerpc(&l);
	l->p = up;
	l->m = m;
	l->isilock = 0;
	return 1;
}

void
unlock(Lock *l)
{
	if(l->key == 0)
		print("unlock: not locked: pc %#p\n", getcallerpc(&l));
	if(l->isilock)
		print("unlock of ilock: pc %#p, held by %#p\n", getcallerpc(&l), l->pc);
	if(l->p != up)
		print("unlock: up changed: pc %#p, acquired at pc %#p, lock p %#p, unlock up %#p\n", getcallerpc(&l), l->pc, l->p, up);
	l->m = nil;
	l->key = 0;
	coherence();

	if(up && --up->nlocks == 0 && up->delaysched && islo()){
		/*
		 * Call sched if the need arose while locks were held
		 * But, don't do it from interrupt routines, hence the islo() test
		 */
		sched();
	}
}

void
iunlock(Lock *l)
{
	Mreg s;

	if(l->key == 0)
		print("iunlock: not locked: pc %#p\n", getcallerpc(&l));
	if(!l->isilock)
		print("iunlock of lock: pc %#p, held by %#p\n", getcallerpc(&l), l->pc);
	if(islo())
		print("iunlock while lo: pc %#p, held by %#p\n", getcallerpc(&l), l->pc);
	if(l->m != m){
		print("iunlock by cpu%d, locked by cpu%d: pc %#p, held by %#p\n",
			m->machno, l->m->machno, getcallerpc(&l), l->pc);
	}

	s = l->sr;
	l->m = nil;
	l->key = 0;
	coherence();
	m->ilockdepth--;
	if(up)
		up->lastilock = nil;
	splx(s);
}

int
lockpc(Lock *l, uintptr)
{
	return lock(l);
}

void
ilockpc(Lock *l, uintptr)
{
	ilock(l);
}

int
ownlock(Lock *l)
{
	return l->m == m;
}

uintptr
lockgetpc(Lock *l)
{
	return l->pc;
}

void
locksetpc(Lock *l, uintptr pc)
{
	l->pc = pc;
}
