#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "../port/systab.h"

#include "tos.h"
#include "../port/tos32.h"
#include "ureg.h"

#include "../bcm/arm.h"

#include "ureg32.h"

typedef struct {
	uintptr	ip;
	Ureg*	arg0;
	char*	arg1;
	char	msg[ERRMAX];
	Ureg*	old;
	Ureg	ureg;
} NFrame;

typedef struct {
	ulong	ip;
	ulong	arg0;
	ulong	arg1;
	char	msg[ERRMAX];
	Ureg*	old;
	Ureg32	ureg;
} NFrame32;

/*
 *   Return user to state before notify()
 */
static void
noted(Ureg* cur, uintptr arg0)
{
	union {
		NFrame nf;
		NFrame32 nf32;
	} *nf;
	int fsize;
	Ureg *nur;
	Ureg32 *nur32;
	u64int oldpsr;

	qlock(&up->debug);
	if(arg0 != NRSTR && !up->notified){
		qunlock(&up->debug);
		pprint("call to noted() when not notified\n");
		pexit("Suicide", 0);
	}
	up->notified = 0;
	fpunoted();

	nf = up->ureg;
	if(up->compat32)
		fsize = sizeof(NFrame32);
	else
		fsize = sizeof(NFrame);

	/* sanity clause */
	if(!okaddr(PTR2UINT(nf), fsize, 0)){
		qunlock(&up->debug);
		pprint("bad ureg in noted %#p\n", nf);
		pexit("Suicide", 0);
	}

	/* don't let user change system flags */
	if(!up->compat32){
		nur = &nf->nf.ureg;
		nur->psr &= PsrMask|PsrDfiq|PsrDirq;
		nur->psr |= (cur->psr & ~(PsrMask|PsrDfiq|PsrDirq));
		memmove(cur, nur, sizeof(Ureg));
	}else{
		nur32 = &nf->nf32.ureg;
		oldpsr = cur->psr;
		ureg32to64(cur, nur32);
		cur->psr &= PsrMask|PsrDfiq|PsrDirq;
		cur->psr |= (oldpsr & ~(PsrMask|PsrDfiq|PsrDirq));
	}

	switch((int)arg0){
	case NCONT:
	case NRSTR:
		if(!okaddr(cur->pc, BY2WD, 0) ||
		  (up->compat32 ? !okaddr(cur->r13, BY2WD, 0) : !okaddr(cur->sp, sizeof(uintptr), 0))){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		up->ureg = up->compat32? nf->nf32.old : nf->nf.old;
		qunlock(&up->debug);
		break;
	case NSAVE:
		if(!okaddr(cur->pc, BY2WD, 0) ||
		  (up->compat32 ? !okaddr(cur->r13, BY2WD, 0) : !okaddr(cur->sp, sizeof(uintptr), 0))){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		qunlock(&up->debug);

		splhi();
		if(up->compat32){
			nf->nf32.arg1 = PTR2UINT(nf->nf32.msg);
			nf->nf32.arg0 = PTR2UINT(&nf->nf32.ureg);
			nf->nf32.ip = 0;
			cur->r13 = PTR2UINT(nf);
			cur->r0 = PTR2UINT(nf->nf32.arg0);
		}else{
			nf->nf.arg1 = nf->nf.msg;
			nf->nf.arg0 = &nf->nf.ureg;
			nf->nf.ip = 0;
			cur->sp = PTR2UINT(nf);
			cur->r0 = PTR2UINT(nf->nf.arg0);
		}
		break;
	default:
		pprint("unknown noted arg %#p\n", arg0);
		up->lastnote.flag = NDebug;
		/*FALLTHROUGH*/
	case NDFLT:
		if(up->lastnote.flag == NDebug){ 
			qunlock(&up->debug);
			pprint("suicide: %s\n", up->lastnote.msg);
		}
		else
			qunlock(&up->debug);
		pexit(up->lastnote.msg, up->lastnote.flag != NDebug);
	}
}

/*
 *  Call user, if necessary, with note.
 *  Pass user the Ureg struct and the note on his stack.
 */
int
notify(Ureg* ureg)
{
	int l, fsize;
	Note *n;
	u32int s;
	uintptr sp;
	union {
		NFrame nf;
		NFrame32 nf32;
	} *nf;

	if(up->procctl)
		procctl(up);
	if(up->nnote == 0)
		return 0;

	fpunotify(ureg);

	s = spllo();
	qlock(&up->debug);

	up->notepending = 0;
	n = &up->note[0];
	if(strncmp(n->msg, "sys:", 4) == 0){
		l = strlen(n->msg);
		if(l > ERRMAX-23)	/* " pc=0x0123456789abcdef\0" */
			l = ERRMAX-23;
		snprint(n->msg + l, sizeof n->msg - l, " pc=%#p", ureg->pc);
	}

	if(n->flag != NUser && (up->notified || up->notify == 0)){
		if(n->flag == NDebug)
			pprint("suicide: %s\n", n->msg);
		qunlock(&up->debug);
		pexit(n->msg, n->flag != NDebug);
	}

	if(up->notified){
		qunlock(&up->debug);
		splhi();
		return 0;
	}
		
	if(up->notify == nil){
		qunlock(&up->debug);
		pexit(n->msg, n->flag != NDebug);
	}
	if(!okaddr(PTR2UINT(up->notify), 1, 0)){
		pprint("suicide: notify function address %#p\n", up->notify);
		qunlock(&up->debug);
		pexit("Suicide", 0);
	}

	if(up->compat32){
		fsize = sizeof(NFrame32);
		sp = ureg->r13 - fsize;
	}else{
		fsize = sizeof(NFrame);
		sp = ureg->sp - fsize;
	}
	if(!okaddr(sp, fsize, 1)){
		qunlock(&up->debug);
		pprint("suicide: notify stack address %#p\n", sp);
		pexit("Suicide", 0);
	}

	nf = UINT2PTR(sp);
	if(up->compat32){
		ureg64to32(&nf->nf32.ureg, ureg);
		nf->nf32.old = up->ureg;
		up->ureg = nf;
		memmove(nf->nf32.msg, up->note[0].msg, ERRMAX);
		nf->nf32.arg1 = PTR2UINT(nf->nf32.msg);
		nf->nf32.arg0 = PTR2UINT(&nf->nf32.ureg);
		ureg->r0 = PTR2UINT(nf->nf32.arg0);
		nf->nf32.ip = 0;
		ureg->r13 = sp;
	}else{
		memmove(&nf->nf.ureg, ureg, sizeof(Ureg));
		nf->nf.old = up->ureg;
		up->ureg = nf;
		memmove(nf->nf.msg, up->note[0].msg, ERRMAX);
		nf->nf.arg1 = nf->nf.msg;
		nf->nf.arg0 = &nf->nf.ureg;
		ureg->r0 = PTR2UINT(nf->nf.arg0);
		nf->nf.ip = 0;
		ureg->sp = sp;
	}
	ureg->pc = PTR2UINT(up->notify);
	up->notified = 1;
	up->nnote--;
	memmove(&up->lastnote, &up->note[0], sizeof(Note));
	memmove(&up->note[0], &up->note[1], up->nnote*sizeof(Note));

	qunlock(&up->debug);
	splx(s);

	return 1;
}

void
syscall(Ureg* ureg)
{
	char *e;
	u32int s;
	uintptr sp;
	ulong *argp;
	uintptr ret;
	int i, scallnr;
	vlong startns, stopns;

	if(!userureg(ureg))
		panic("syscall: from kernel: pc %#p r14 %#p psr %#p",
			ureg->pc, ureg->r14, ureg->psr);

	cycles(&up->kentry);

	m->syscall++;
	up->insyscall = 1;
	up->pc = ureg->pc;
	up->dbgreg = ureg;

	scallnr = ureg->r0;
	up->scallnr = scallnr;
	if(scallnr == RFORK)
		fpusysrfork(ureg);
	spllo();
	if(up->compat32){
		sp = ureg->r13;
		//print("aarch32 syscall %s: sp %#p sb %#p r1 %#p (%#lux) %#lux %#lux %#...\n", sysctab[scallnr], sp, ureg->r12, ureg->r1, 
		//	((ulong*)sp)[0], ((ulong*)sp)[1], ((ulong*)sp)[2], ((ulong*)sp)[3]);
		if(sp < (USTKTOP-BY2PG) || sp > (USTKTOP-(MAXSYSARG+1)*BY2WD))
			validaddr(sp, (MAXSYSARG+1)*BY2WD, 0);
		argp = (ulong*)(sp + BY2WD);
		for(i = 0; i < MAXSYSARG; i++)
			up->s.args[i] = *argp++;
		switch(scallnr){
		case PREAD:
		case PWRITE:
			up->s.args[3] |= up->s.args[4]<<32;
			break;
		}
	}else{
		sp = ureg->sp;
		//print("aarch64 syscall %s: sp %#p sb %#p r1 %#p (%#p) %#p %#p %#p...\n", sysctab[scallnr], sp, ureg->r28, ureg->r1, 
		//	((uintptr*)sp)[0], ((uintptr*)sp)[1], ((uintptr*)sp)[2], ((uintptr*)sp)[3]);
		if(sp < (USTKTOP-BY2PG) || sp > (USTKTOP-sizeof(Sargs)-sizeof(uintptr)))
			validaddr(sp, sizeof(Sargs)+sizeof(uintptr), 0);
		up->s = *((Sargs*)(sp+sizeof(uintptr)));
		switch(scallnr){
		case SEEK:
			up->s.args[4] = up->s.args[3]&0xFFFFFFFFull;
			up->s.args[3] = (up->s.args[2]>>32)&0xFFFFFFFFull;
			up->s.args[2] &= 0xFFFFFFFFull;
			break;
		}
	}

	if(up->procctl == Proc_tracesyscall){
		syscallfmt(scallnr, ureg->pc, (va_list)(up->s.args));
		up->procctl = Proc_stopme;
		procctl(up);
		if (up->syscalltrace) 
			free(up->syscalltrace);
		up->syscalltrace = nil;
	}

	up->nerrlab = 0;
	ret = -1;
	startns = todget(nil, nil);
	if(!waserror()){
		if(scallnr >= nsyscall){
			pprint("bad sys call number %d pc %#p\n",
				scallnr, ureg->pc);
			postnote(up, 1, "sys: bad sys call", NDebug);
			error(Ebadarg);
		}

		up->psstate = sysctab[scallnr];

		//print("%s %lud: %#p syscall %s\n", up->text, up->pid, ureg->r14, sysctab[scallnr]?sysctab[scallnr]:"huh?");

		if(!up->compat32 && scallnr == NSEC)
			ret = startns;
		else
			ret = systab[scallnr](up->s.args);
		poperror();
	}else{
		/* failure: save the error buffer for errstr */
		e = up->syserrstr;
		up->syserrstr = up->errstr;
		up->errstr = e;
	}
	if(up->nerrlab){
		print("bad errstack [%d]: %d extra\n", scallnr, up->nerrlab);
		for(i = 0; i < NERR; i++)
			print("sp=%#p pc=%#p\n",
				up->errlab[i].sp, up->errlab[i].pc);
		panic("error stack");
	}

	/*
	 *  Put return value in frame.  On the x86 the syscall is
	 *  just another trap and the return value from syscall is
	 *  ignored.  On other machines the return value is put into
	 *  the results register by caller of syscall.
	 */
	ureg->r0 = ret;

	if(up->procctl == Proc_tracesyscall){
		stopns = todget(nil, nil);
		up->procctl = Proc_stopme;
		sysretfmt(scallnr, (va_list)(up->s.args), ret, startns, stopns);
		s = splhi();
		procctl(up);
		splx(s);
		if(up->syscalltrace)
			free(up->syscalltrace);
		up->syscalltrace = nil;
	}

	up->insyscall = 0;
	up->psstate = 0;

	if(scallnr == NOTED)
		noted(ureg, *(ulong*)(sp+BY2WD));

	splhi();
	if(scallnr != RFORK && (up->procctl || up->nnote))
		notify(ureg);

	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched){
		sched();
		splhi();
	}
	kexit(ureg);
}

long
execregs(ulong entry, ulong ssize, ulong nargs)
{
	uintptr sp;
	Ureg *ureg;

	ureg = up->dbgreg;
//	memset(ureg, 0, 15*sizeof(ulong));
	sp = (uintptr)(USTKTOP - ssize);
	if(up->compat32){
		sp -= sizeof(ulong);
		*(ulong*)sp = nargs;
		ureg->r13 = sp;
		ureg->psr |= 1<<4;
	}else{
		sp -= sizeof(uintptr);
		*(uintptr*)sp = nargs;
		ureg->sp = sp;
		ureg->psr &= ~(1<<4);
	}
	ureg->pc = entry;

	/*
	 * return the address of kernel/user shared data
	 * (e.g. clock stuff)
	 */
	return up->compat32? USTKTOP-sizeof(Tos32) : USTKTOP-sizeof(Tos);
}

void
sysprocsetup(Proc* p)
{
	fpusysprocsetup(p);
}

/* 
 *  Craft a return frame which will cause the child to pop out of
 *  the scheduler in user mode with the return register zero.  Set
 *  pc to point to a l.s return function.
 */
void
forkchild(Proc *p, Ureg *ureg)
{
	Ureg *cureg;

	p->sched.sp = STACKALIGN((uintptr)p->kstack+KSTACK-sizeof(Ureg));
	p->sched.pc = (uintptr)forkret;

	cureg = (Ureg*)(p->sched.sp);
	memmove(cureg, ureg, sizeof(Ureg));

	/* syscall returns 0 for child */
	cureg->r0 = 0;

	/* Things from bottom of syscall which were never executed */
	p->psstate = 0;
	p->insyscall = 0;

	fpusysrforkchild(p, cureg, up);
}
