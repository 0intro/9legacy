/*
 * VFPv2 or VFPv3 floating point unit
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "ureg.h"
#include "../bcm/arm.h"

/* subarchitecture code in m->havefp */
enum {
	VFPv2	= 2,
	VFPv3	= 3,
};

/* fp control regs.  most are read-only */
enum {
	Fpsid =	0,
	Fpscr =	1,			/* rw */
	Mvfr1 =	6,
	Mvfr0 =	7,
	Fpexc =	8,			/* rw */
	Fpinst= 9,			/* optional, for exceptions */
	Fpinst2=10,
};
enum {
	/* Fpexc bits */
	Fpex =		1u << 31,
	Fpenabled =	1 << 30,
	Fpdex =		1 << 29,	/* defined synch exception */
//	Fp2v =		1 << 28,	/* Fpinst2 reg is valid */
//	Fpvv =		1 << 27,	/* if Fpdex, vecitr is valid */
//	Fptfv = 	1 << 26,	/* trapped fault is valid */
//	Fpvecitr =	MASK(3) << 8,
	/* FSR bits appear here */
	Fpmbc =		Fpdex,		/* bits exception handler must clear */

	/* Fpscr bits; see u.h for more */
	Stride =	MASK(2) << 20,
	Len =		MASK(3) << 16,
	Dn=		1 << 25,
	Fz=		1 << 24,
	/* trap exception enables (not allowed in vfp3) */
	FPIDNRM =	1 << 15,	/* input denormal */
	Alltraps = FPIDNRM | FPINEX | FPUNFL | FPOVFL | FPZDIV | FPINVAL,
	/* pending exceptions */
	FPAIDNRM =	1 << 7,		/* input denormal */
	Allexc = FPAIDNRM | FPAINEX | FPAUNFL | FPAOVFL | FPAZDIV | FPAINVAL,
	/* condition codes */
	Allcc =		MASK(4) << 28,
};

static int
havefp(void)
{
	if (m->havefpvalid)
		return m->havefp;

	m->havefp = VFPv3;
	m->fpnregs = 32;
	if (m->machno == 0)
		print("fp: %d registers, simd\n", m->fpnregs);
	m->havefpvalid = 1;
	return 1;
}

/*
 * these can be called to turn the fpu on or off for user procs,
 * not just at system start up or shutdown.
 */

void
fpoff(void)
{
	if (m->fpon) {
		fpwrexc(0);
		m->fpon = 0;
	}
}

void
fpononly(void)
{
	if (!m->fpon && havefp()) {
		/* enable fp.  must be first operation on the FPUs. */
		fpwrexc(Fpenabled);
		m->fpon = 1;
	}
}

static void
fpcfg(void)
{
	static int printed;

	/* clear pending exceptions; no traps in vfp3; all v7 ops are scalar */
	m->fpscr = Dn | FPRNR | (FPINVAL | FPZDIV | FPOVFL) & ~Alltraps;
	/* VFPv2 needs software support for underflows, so force them to zero */
	if(m->havefp == VFPv2)
		m->fpscr |= Fz;
	fpwrscr(m->fpscr);
	m->fpconfiged = 1;

	if (printed)
		return;
	print("fp: arm neon\n");
	printed = 1;
}

void
fpinit(void)
{
	if (havefp()) {
		fpononly();
		fpcfg();
	}
}

void
fpon(void)
{
	if (havefp()) {
	 	fpononly();
		if (m->fpconfiged)
			fpwrscr((fprdscr() & Allcc) | m->fpscr);
		else
			fpcfg();	/* 1st time on this fpu; configure it */
	}
}

void
fpclear(void)
{
//	ulong scr;

	fpon();
//	scr = fprdscr();
//	m->fpscr = scr & ~Allexc;
//	fpwrscr(m->fpscr);

	fpwrexc(fprdexc() & ~Fpmbc);
}


/*
 * Called when a note is about to be delivered to a
 * user process, usually at the end of a system call.
 * Note handlers are not allowed to use the FPU so
 * the state is marked (after saving if necessary) and
 * checked in the Device Not Available handler.
 */
void
fpunotify(Ureg*)
{
	if(up->fpstate == FPactive){
		fpsave(&up->fpsave);
		up->fpstate = FPinactive;
	}
 	up->fpstate |= FPnotestart<<FPnoteshift;
}

/*
 * Called from sysnoted() via the machine-dependent
 * noted() routine.
 * Clear the flag set above in fpunotify().
 */
void
fpunoted(void)
{
	if((up->fpstate>>FPnoteshift) == FPactive)
		fpoff();
	up->fpstate &= ~FPnotemask;
}

/*
 * Called early in the non-interruptible path of
 * sysrfork() via the machine-dependent syscall() routine.
 * Save the state so that it can be easily copied
 * to the child process later.
 */
void
fpusysrfork(Ureg*)
{
	if(up->fpstate == FPactive){
		fpsave(&up->fpsave);
		up->fpstate = FPinactive;
 	}else if((up->fpstate>>FPnoteshift) == FPactive){
 		fpsave(&up->notefpsave);
 		up->fpstate = (up->fpstate&~FPnotemask) | (FPinactive<<FPnoteshift);
 	}
}

/*
 * Obsolete
 */
void
fpusysrforkchild(Proc *p, Ureg *, Proc *up)
{
	USED(p);
	USED(up);
}

/* should only be called if p->fpstate == FPactive */
void
fpsave(FPsave *fps)
{
	fpon();
	fps->control = fps->status = fprdscr();
	assert(m->fpnregs);
	fpsaveregs((uvlong*)fps->regs, m->fpnregs);
	fpoff();
}

static void
fprestore(FPsave *fps)
{
	fpon();
	fpwrscr(fps->control);
	m->fpscr = fprdscr() & ~Allcc;
	assert(m->fpnregs);
	fprestregs((uvlong*)fps->regs, m->fpnregs);
}

/*
 * Called from sched() and sleep() via the machine-dependent
 * procsave() routine.
 * About to go in to the scheduler.
 * If the process wasn't using the FPU
 * there's nothing to do.
 */
void
fpuprocsave(Proc *p)
{
 	if(p->fpstate&FPnotemask){
 		if((p->fpstate>>FPnoteshift) == FPactive){
 			if(p->state == Moribund)
 				fpoff();
 			else
 				fpsave(&p->notefpsave);
 			p->fpstate = (p->fpstate&~FPnotemask) | (FPinactive<<FPnoteshift);
 		}
 	} else if(p->fpstate == FPactive){
		if(p->state == Moribund)
			fpoff();
		else{
			/*
			 * Fpsave() stores without handling pending
			 * unmasked exeptions. Postnote() can't be called
			 * here as sleep() already has up->rlock, so
			 * the handling of pending exceptions is delayed
			 * until the process runs again and generates an
			 * emulation fault to activate the FPU.
			 */
			fpsave(&p->fpsave);
		}
		p->fpstate = FPinactive;
	}
}

/*
 * The process has been rescheduled and is about to run.
 * Nothing to do here right now. If the process tries to use
 * the FPU again it will cause a Device Not Available
 * exception and the state will then be restored.
 */
void
fpuprocrestore(Proc *)
{
}

/*
 * Disable the FPU.
 * Called from sysexec() via sysprocsetup() to
 * set the FPU for the new process.
 */
void
fpusysprocsetup(Proc *p)
{
	p->fpstate = FPinit;
	fpoff();
}

static void
mathnote(void)
{
	ulong status;
	char *msg, note[ERRMAX];

	status = up->fpsave.status;

	/*
	 * Some attention should probably be paid here to the
	 * exception masks and error summary.
	 */
	if (status & FPAINEX)
		msg = "inexact";
	else if (status & FPAOVFL)
		msg = "overflow";
	else if (status & FPAUNFL)
		msg = "underflow";
	else if (status & FPAZDIV)
		msg = "divide by zero";
	else if (status & FPAINVAL)
		msg = "bad operation";
	else
		msg = "spurious";
	snprint(note, sizeof note, "sys: fp: %s fppc=%#p status=%#lux",
		msg, up->fpsave.pc, status);
	postnote(up, 1, note, NDebug);
}

static void
mathemu(Ureg *)
{
 	if(up->fpstate & FPnotemask){
 		/* in note handler */
 		switch(up->fpstate>>FPnoteshift){
 		case FPnotestart:
 			/* no FP used yet; copy state at time of note */
 			if((up->fpstate&~FPnotemask) == FPinit) {
 				/* no FP in the process yet */
 				up->fpstate = (up->fpstate&~FPnotemask) | (FPactive<<FPnoteshift);
 				fpinit();
 				return;
 			}
 			up->notefpsave = up->fpsave;
 			/* fall through */
 		case FPinactive:
 			/* restore state */
			if(up->fpsave.status & (FPAINEX|FPAUNFL|FPAOVFL|FPAZDIV|FPAINVAL)){
				postnote(up, 1, "sys: floating point exception in note handler", NDebug);
				break;
			}
 			fprestore(&up->notefpsave);
 			up->fpstate = (up->fpstate&~FPnotemask) | (FPactive<<FPnoteshift);
 			break;
 		case FPactive:
			error("sys: illegal instruction: bad vfp fpu opcode");
 		}
 		fpclear();
 		return;
	}
	switch(up->fpstate){
	case FPemu:
		error("illegal instruction: VFP opcode in emulated mode");
	case FPinit:
		fpinit();
		up->fpstate = FPactive;
		break;
	case FPinactive:
		/*
		 * Before restoring the state, check for any pending
		 * exceptions.  There's no way to restore the state without
		 * generating an unmasked exception.
		 * More attention should probably be paid here to the
		 * exception masks and error summary.
		 */
		if(up->fpsave.status & (FPAINEX|FPAUNFL|FPAOVFL|FPAZDIV|FPAINVAL)){
			mathnote();
			break;
		}
		fprestore(&up->fpsave);
		up->fpstate = FPactive;
		break;
	case FPactive:
		error("sys: illegal instruction: bad vfp fpu opcode");
		break;
	}
	fpclear();
}

void
fpstuck(uintptr pc)
{
	if (m->fppc == pc && m->fppid == up->pid) {
		m->fpcnt++;
		if (m->fpcnt > 4)
			panic("fpuemu: cpu%d stuck at pid %ld %s pc %#p "
				"instr %#8.8lux", m->machno, up->pid, up->text,
				pc, *(ulong *)pc);
	} else {
		m->fppid = up->pid;
		m->fppc = pc;
		m->fpcnt = 0;
	}
}

enum {
	N = 1<<31,
	Z = 1<<30,
	C = 1<<29,
	V = 1<<28,
	REGPC = 15,
};

static int
condok(int cc, int c)
{
	switch(c){
	case 0:	/* Z set */
		return cc&Z;
	case 1:	/* Z clear */
		return (cc&Z) == 0;
	case 2:	/* C set */
		return cc&C;
	case 3:	/* C clear */
		return (cc&C) == 0;
	case 4:	/* N set */
		return cc&N;
	case 5:	/* N clear */
		return (cc&N) == 0;
	case 6:	/* V set */
		return cc&V;
	case 7:	/* V clear */
		return (cc&V) == 0;
	case 8:	/* C set and Z clear */
		return cc&C && (cc&Z) == 0;
	case 9:	/* C clear or Z set */
		return (cc&C) == 0 || cc&Z;
	case 10:	/* N set and V set, or N clear and V clear */
		return (~cc&(N|V))==0 || (cc&(N|V)) == 0;
	case 11:	/* N set and V clear, or N clear and V set */
		return (cc&(N|V))==N || (cc&(N|V))==V;
	case 12:	/* Z clear, and either N set and V set or N clear and V clear */
		return (cc&Z) == 0 && ((~cc&(N|V))==0 || (cc&(N|V))==0);
	case 13:	/* Z set, or N set and V clear or N clear and V set */
		return (cc&Z) || (cc&(N|V))==N || (cc&(N|V))==V;
	case 14:	/* always */
		return 1;
	case 15:	/* never (reserved) */
		return 0;
	}
	return 0;	/* not reached */
}

/* only called to deal with user-mode instruction faults */
int
fpuemu(Ureg* ureg)
{
	int s, cop, op;
	int nfp;
	uintptr pc;
	static int already;

	if(waserror()){
		postnote(up, 1, up->errstr, NDebug);
		return 1;
	}

	pc = ureg->pc;
	validaddr(pc, 4, 0);
	if(m->fpon)
		fpstuck(pc);		/* debugging; could move down 1 line */
	op  = (*(ulong *)pc >> 24) & MASK(4);
	cop = (*(ulong *)pc >>  8) & MASK(4);
	if (up->compat32 && ISFPAOP(cop, op)) {		/* old arm 7500 fpa opcode? */
		s = spllo();
		if(!already++)
			pprint("warning: emulated arm7500 fpa instr %#8.8lux at %#p\n", *(ulong *)pc, pc);
		if(waserror()){
			splx(s);
			nexterror();
		}
		nfp = fpiarm(ureg);	/* advances pc past emulated instr(s) */
		if (nfp > 1)		/* could adjust this threshold */
			m->fppc = m->fpcnt = 0;
		poperror();
	} else { //if (ISVFPOP(cop, op)) {	/* if vfp, fpu off or unsupported instruction */
		mathemu(ureg);		/* enable fpu & retry */
		nfp = 1;
	}

	poperror();
	return nfp;
}

int
fpiarm(Ureg*)
{
	error("emulated arm7500 fpa unsupported");
	return 0;
}
