#include "mem.h"
#include "ureg.s"

#define ATOMICS	1

// if value is Rs, write Rt to 0(Rn), Rs receives previous value
#define CASALW(Rs, Rt, Rn) WORD $(0x88e0fc00 | Rt | (Rs<<16) | (Rn<<5))
#define CASALP(Rs, Rt, Rn) WORD $(0xc8e0fc00 | Rt | (Rs<<16) | (Rn<<5))
// add Rs to 0(Rn) and store in 0(Rn), Rt receives previous value
#define	LDADDALW(Rs, Rt, Rn) WORD $(0xb8e00000 | Rt | (Rs<<16) | (Rn<<5))

#define	SYSARG5(op0, op1, Cn, Cm, op2) ((op0)<<19 | (op1)<<16 | (Cn)<<12 | (Cm)<<8 | (op2)<<5)
#define MIDR_EL1	SYSARG5(3,0,0,0,0)
#define MPIDR_EL1	SYSARG5(3,0,0,0,5)
#define SCTLR_EL1	SYSARG5(3,0,1,0,0)
#define MAIR_EL1	SYSARG5(3,0,10,2,0)
#define TCR_EL1		SYSARG5(3,0,2,0,2)
#define TTBR0_EL1	SYSARG5(3,0,2,0,0)
#define TTBR1_EL1	SYSARG5(3,0,2,0,1)
#define ESR_EL1		SYSARG5(3,0,5,2,0)
#define FAR_EL1		SYSARG5(3,0,6,0,0)
#define VBAR_EL1	SYSARG5(3,0,12,0,0)
#define PMCR_EL0	SYSARG5(3,3,9,12,0)
#define PMCNTENSET	SYSARG5(3,3,9,12,1)
#define PMCCNTR_EL0	SYSARG5(3,3,9,13,0)
#define PMUSERENR_EL0	SYSARG5(3,3,9,14,0)
#define CPACR_EL1	SYSARG5(3,0,1,0,2)
#define CNTKCTL_EL1	SYSARG5(3,0,14,1,0)
#define CNTFRQ_EL0	SYSARG5(3,3,14,0,0)
#define CNTPCT_EL0	SYSARG5(3,3,14,0,1)
#define CNTP_CTL_EL0	SYSARG5(3,3,14,2,1)
#define CNTP_CVAL_EL0	SYSARG5(3,3,14,2,2)
#define CNTV_TVAL_EL0	SYSARG5(3,3,14,3,0)
#define CNTV_CTL_EL0	SYSARG5(3,3,14,3,1)
#define CNTV_CVAL_EL0	SYSARG5(3,3,14,3,2)
#define FPEXC32_EL2	SYSARG5(3,4,5,3,0)

#define SY	(3<<2 | 3)
#define NSHST	(1<<2 | 2)
#define NSH	(1<<2 | 3)
#define ISHLD	(2<<2 | 1)
#define ISHST	(2<<2 | 2)
#define ISH	(2<<2 | 3)

#define Dirq	2
#define Dfiq	1

#define	PADDR(va)	((va) & 0x7FFFFFFFull)

#include "vectors.s"

TEXT	_start+0(SB), 0, $-4
	MOV	R0, R25			/* save fdt pointer */
	MOV	$setSB-KZERO(SB), R28
	MRS	SPR(MPIDR_EL1), R1
	LSR	$8, R1
	AND	$0x3, R1
	ADD	$0x41, R1, R0
	MOV	$0x107d001000, R2
	MOVW R0, (R2)
	CBNZ	R1, 0(PC)		/* loop if not cpu 0 */

	BL	spldone(SB)
	MOV	$0x20, R0
	CBZ	R30, 4(PC)
	LSR	$1, R30
	ADD	$1, R0
	CBNZ	R30, -2(PC)
	MOVW	R0, (R2)

	/*
	 * relocate text & data from firmware load address to 0x100000
	 */
	MOV	$_vectors-KZERO(SB), R1		/* dst start */
	MOV	$edata-KZERO(SB), R2		/* dst end */
	ADD	$15, R2				/* round end up to 64 bits */
	AND	$~15, R2
	ADR	vectors, R3			/* src start */
	SUBS	R3, R1, R4			/* reloc offset */
	BEQ	relocdone			/* zero: no relocation */
	BMI	relocneg			/* negative: reloc to lower addr */
	SUB	R4, R2, R4			/* src end */
relocpos:
	SUB	$16, R2
	SUB	$16, R4
	MOVP	(R4), R5, R6
	MOVP	R5, R6, (R2)
	CMP	R2, R1
	BNE	relocpos
relocdone:
	MOV	$begin-KZERO(SB), R1
	B	(R1)

relocneg:
	MOVP	(R3), R5, R6
	MOVP	R5, R6, (R1)
	ADD	$16, R1
	ADD	$16, R3
	CMP	R1, R2
	BNE	relocneg
	B	relocdone

TEXT	begin(SB), 0, $-4
	/*
	 * go to EL1 mode, interrupts disabled
	 */
	MOV	$1, R1			/* use EL1 SP in EL1 */
	MSR	R1, SPSel
	MOVW	$(0xF<<6 | 5), R1	/* interrupts off, EL1h, aarch64 */
	MRS	CurrentEL, R2
	CMPW	$(2<<2), R2
	BNE	2(PC)			/* not in EL2, skip */
	BL	el2to1(SB)		/* switch from EL2 to EL1 */
	MRS	CurrentEL, R2
	CMPW	$(1<<2), R2
	BNE	0(PC)			/* not in EL1, hang */
	BL	el1to1(SB)

	/*
	 * disable mmu and caches
	 */
	MRS	SPR(SCTLR_EL1), R0
	BIC	$(1<<12 | 1<<2 | 1<<0), R0	/* I, C, M */
	ORR	$(1<<5), R0			/* enable CP15 DMB instruction */
	MSR	R0, SPR(SCTLR_EL1)
	ISB	$SY

	/*
	 * clear mach
	 */
	MOV	$PADDR(MACHADDR), R1
	ADD	$MACHSIZE, R1, R2
	MOV	ZR, (R1)
	ADD	$8, R1
	CMP	R1, R2
	BNE	-3(PC)

	/*
	 * clear bss
	 */
	MOV	$edata-KZERO(SB), R1
	MOV	$end-KZERO(SB), R2
	MOV	ZR, (R1)
	ADD	$8, R1
	CMP	R1, R2
	BNE	-3(PC)

	/*
	 * invalidate tlb
	 */
	DSB	$NSHST
	TLBI	R0, 0,8,7,0		/* VMALLE1 */
	DSB	$NSH
	ISB	$SY
	
	/*
	 * start stack pointer at top of mach (physical addr)
	 * set up page tables for kernel
	 */
	MOV	$PADDR(MACHADDR+MACHSIZE-8), R1
	MOV	R1, SP
	MOV	$PADDR(L1), R0
	BL	mmuinit(SB)

	/*
	 * configure mmu control registers
	 */
	MOV	tcr_el1(SB), R1
	MSR	R1, SPR(TCR_EL1)
	ISB	$SY
	MOV	mair_el1(SB), R1
	MSR	R1, SPR(MAIR_EL1)
	ISB	$SY
	MOV	$PADDR(L1), R1
	ADD	$((L2VA>>21)<<3), R1
	ISB	$SY
	MSR	R1, SPR(TTBR0_EL1)
	MSR	R1, SPR(TTBR1_EL1)
	ISB	$SY

	/*
	 * invalidate my caches before enabling
	 */
	BL	cachedinv(SB)
	BL	cacheiinv(SB)
	BL	l2cacheuinv(SB)
	//MOV	$0x42, R0
	//BL	uartputc(SB)

	/*
	 * enable caches and mmu
	 */
	MRS	SPR(SCTLR_EL1), R1
	ORR	$(1<<12 | 1<<2 | 1<<0), R1	/* I, C, M */
	ISB	$SY
	MSR	R1, SPR(SCTLR_EL1)
	ISB	$SY
	//MOV	$0x32, R0
	//BL	uartputc(SB)

	/*
	 * switch SB, SP and PC into KZERO space
	 */
	MOV	$setSB(SB), R28
	MOV	$(MACHADDR+MACHSIZE-8), R1
	MOV	R1, SP
	BL	pcrelocate(SB)
	//MOV	$0x33, R0
	//BL	uartputc(SB)

	/*
	 * enable cycle counter & access from EL0
	 */
	MOV	$1, R0
	MSR	R0, SPR(PMCR_EL0)
	//MSR	R0, SPR(PMUSERENR_EL0)
	MOV	$(1<<31), R0
	MSR	R0, SPR(PMCNTENSET)
	MOV	$0x33, R0
	MSR	R0, SPR(CNTKCTL_EL1)

	MOV	R25, R0		/* fdt pointer */
	BL	main(SB)
	B	0(PC)

TEXT	el2to1(SB), 0, $-4
	MSR	LR, ELR_EL2
	MSR	R1, SPSR_EL2
	ERET

TEXT	el1to1(SB), 0, $-4
	MSR	LR, ELR_EL1
	MSR	R1, SPSR_EL1
	ERET

TEXT	pcrelocate(SB), 0, $-4
	ORR	$KZERO, LR
	RETURN

/*
 * Use PSCI interface to start core in R0
 */
TEXT startcpu(SB), 1, $-4
	LSL	$8, R0, R1		/* core number */
	MOV	$0xC4000003UL, R0	/* function CPU_ON */
	MOV	$cpureset-KZERO(SB), R2	/* entry address */
	MOV	$0, R3			/* context (unused) */
	SMC	$0
	RETURN

/*
 * startup entry for cpu(s) other than 0
 */
TEXT cpureset(SB), 1, $-4
	MOV	$setSB-KZERO(SB), R28

	/*
	 * go to EL1 mode, interrupts disabled
	 */
	MOV	$1, R1			/* use EL1 SP in EL1 */
	MSR	R1, SPSel
	MOVW	$(0xF<<6 | 5), R1	/* interrupts off, EL1h, aarch64 */
	MRS	CurrentEL, R2
	CMPW	$(2<<2), R2
	BNE	2(PC)			/* not in EL2, skip */
	BL	el2to1(SB)		/* switch from EL2 to EL1 */
	MRS	CurrentEL, R2
	CMPW	$(1<<2), R2
	BNE	0(PC)			/* not in EL1, hang */
	BL	el1to1(SB)

	/*
	 * disable mmu and caches
	 */
	MRS	SPR(SCTLR_EL1), R0
	BIC	$(1<<12 | 1<<2 | 1<<0), R0	/* I, C, M */
	ORR	$(1<<5), R0			/* enable CP15 DMB instruction */
	MSR	R0, SPR(SCTLR_EL1)
	ISB	$SY

	/*
	 * invalidate tlb
	 */
	DSB	$NSHST
	TLBI	R0, 0,8,7,0		/* VMALLE1 */
	DSB	$NSH
	ISB	$SY

	/*
	 * find Mach for this cpu
	 */
	MRS	SPR(MPIDR_EL1), R1
	LSR	$8, R1
	AND	$0x3, R1
	MOV	$machaddr-KZERO(SB), R0
	ADD	R1<<3, R0, R2
	MOV	(R2), R0		/* machaddr[cpuid] */
	CBZ	R0, 0(PC)		/* must not be zero */
	SUB	$KZERO, R0, R(MACH)	/* m = PADDR(machaddr[cpuid]) */

	/*
	 * start stack at top of local Mach
	 */
	ADD	$(MACHSIZE-8), R(MACH), R1
	MOV	R1, SP

	/*
	 * configure mmu control registers
	 */
	MOV	tcr_el1(SB), R1
	MSR	R1, SPR(TCR_EL1)
	ISB	$SY
	MOV	mair_el1(SB), R1
	MSR	R1, SPR(MAIR_EL1)
	ISB	$SY
	MOV	24(R(MACH)), R1		/* m->mmul1 */
	SUB	$KZERO, R1
	ADD	$((L2VA>>21)<<3), R1
	ISB	$SY
	MSR	R1, SPR(TTBR0_EL1)
	MSR	R1, SPR(TTBR1_EL1)
	ISB	$SY

	/*
	 * invalidate my caches before enabling
	 */
	BL	cachedinv(SB)
	BL	cacheiinv(SB)
	BL	l2cacheuinv(SB)
	//MOV	$0x34, R0
	//BL	uartputc(SB)

	/*
	 * enable caches and mmu
	 */
	MRS	SPR(SCTLR_EL1), R1
	ORR	$(1<<12 | 1<<2 | 1<<0), R1	/* I, C, M */
	ISB	$SY
	MSR	R1, SPR(SCTLR_EL1)
	ISB	$SY
	//MOV	$0x35, R0
	//BL	uartputc(SB)

	/*
	 * switch SB, SP, R(MACH) and PC into KZERO space
	 */
	MOV	$setSB(SB), R28
	MOV	SP, R1
	ORR	$KZERO, R1
	MOV	R1, SP
	ORR	$KZERO, R(MACH)
	BL	pcrelocate(SB)
	//MOV	$0x36, R0
	//BL	uartputc(SB)

	/*
	 * enable cycle counter & access from EL0
	 */
	MOV	$1, R0
	MSR	R0, SPR(PMCR_EL0)
	//MSR	R0, SPR(PMUSERENR_EL0)
	MOV	$(1<<31), R0
	MSR	R0, SPR(PMCNTENSET)
	MOV	$0x33, R0
	MSR	R0, SPR(CNTKCTL_EL1)

	/*
	 * call cpustart and loop forever if it returns
	 */
	//MOV	$0x37, R0
	//BL	uartputc(SB)
	MRS	SPR(0x1800a0), R0	/* mpidr_el1 */
	LSR	$8, R0
	AND	$0x3, R0
	BL	cpustart(SB)
	B	0(PC)

TEXT setvbar(SB), 0, $-4
	MSR	R0, SPR(VBAR_EL1)
	RETURN

TEXT cpidget(SB), 1, $-4
	MRS	SPR(MIDR_EL1), R0
	RETURN

TEXT farget(SB), 1, $-4
	MRS	SPR(FAR_EL1), R0
	RETURN

TEXT	cprdtimerfreq(SB), 1, $-4
	MRS	SPR(CNTFRQ_EL0), R0
	RETURN

TEXT	cprdtimerval(SB), 1, $-4
	MRS	SPR(CNTPCT_EL0), R0
	RETURN

TEXT	cpwrtimerphysctl(SB), 1, $-4
	MSR	R0, SPR(CNTV_CTL_EL0)
	RETURN

TEXT	cpwrtimerphysval(SB), 1, $-4
	MSR	R0, SPR(CNTV_TVAL_EL0)
	RETURN

TEXT lcycles(SB), 1, $-4
	MRS	SPR(PMCCNTR_EL0), R0
	RETURN

TEXT splhi(SB), 1, $-4
	MRS	DAIF, R0
	MSR	$Dirq, DAIFSet
	RETURN

TEXT splfhi(SB), 1, $-4
	MRS	DAIF, R0
	MSR	$(Dirq|Dfiq), DAIFSet
	RETURN

TEXT spllo(SB), 1, $-4
	MRS	DAIF, R0
	MSR	$(Dirq|Dfiq), DAIFClr
	RETURN

TEXT splflo(SB), 1, $-4
	MRS	DAIF, R0
	MSR	$Dfiq, DAIFClr
	RETURN

TEXT splx(SB), 1, $-4
	MSR	R0, DAIF
	RETURN

TEXT spldone(SB), 1, $0				/* end marker for devkprof.c */
	RETURN

TEXT islo(SB), 1, $-4
	MRS	DAIF, R0
	AND	$(Dirq<<6), R0
	EOR	$(Dirq<<6), R0
	RETURN

TEXT tas(SB), 1, $-4
TEXT _tas(SB), 1, $-4
#ifdef ATOMICS
	MOV	$0, R1
	MOV	$1, R2
	CASALW(1,2,0)
	MOV	R1, R0
	RETURN
#else
	MOV	$1, R2
	DMB	$ISH
_tas1:
	LDXRW	(R0), R1
	CBNZ	R1, lockbusy
	STXRW	R2, (R0), R3
	CBNZ	R3, _tas1
	DMB	$ISH
	MOV	R1, R0
	RETURN
lockbusy:
	CLREX
	MOV	R1, R0
	DMB	$ISH
	RETURN
#endif

TEXT _xinc(SB), 1, $-4	/* void	_xinc(long *); */
TEXT ainc(SB), 1, $-4	/* long ainc(long *); */
#ifdef ATOMICS
	MOV	$1, R1
	LDADDALW(1,2,0)
	ADD	R1, R2, R0
	RETURN
#else
	DMB	$ISH
spinainc:
	LDXRW	(R0), R3
	ADD	$1, R3
	STXRW	R3, (R0), R4
	CBNZ	R4, spinainc
	DMB	$ISH
	MOV	R3, R0
	RETURN
#endif

TEXT _xdec(SB), 1, $-4	/* long _xdec(long *); */
TEXT adec(SB), 1, $-4	/* long adec(long *); */
#ifdef ATOMICS
	MOV	$-1, R1
	LDADDALW(1,2,0)
	ADD	R1, R2, R0
	RETURN
#else
	DMB	$ISH
spinadec:
	LDXRW	(R0), R3
	SUB	$1,R3
	STXRW	R3, (R0), R4
	CBNZ	R4, spinadec
	DMB	$ISH
	MOV	R3, R0
	RETURN
#endif

TEXT touser(SB), 0, $-4
	MOV	$0x1020, R1
	MOV	$(1<<4), R2			/* aarch32 execution state */
	AND	$0xFFFFFFFF, R0, R13
	MSR	R1, ELR_EL1
	MSR	R2, SPSR_EL1
	ERET

#define FPSRBITS	(0x1F<<27 | 1<<7 | 0x1F)
#define FPCRBITS	(0xFFF<<15 | 0x1F<<8)

TEXT	fprdscr(SB), 1, $-4
	MOV	FPSR, R1
	AND	$FPSRBITS, R1
	MOV	FPCR, R0
	AND	$FPCRBITS, R0
	ORR	R1, R0
	RETURN

TEXT	fprdexc(SB), 1, $-4
	MOV	$0, R0
	MRS	SPR(CPACR_EL1), R1
	AND	$(3<<20), R1
	CMP	$(3<<20), R1
	BNE	2(PC)
	MOV	$(1<<30), R0
	RETURN

TEXT	fprdcpacr(SB), 1, $-4
	MRS	SPR(CPACR_EL1), R0
	RETURN;

TEXT	fpwrscr(SB), 1, $-4
	AND	$FPSRBITS, R0, R1
	MOV	R1, FPSR
	AND	$FPCRBITS, R0, R1
	MOV	R1, FPCR
	RETURN

TEXT	fpwrexc(SB), 1, $-4
	MOV	$0, R1
	AND	$(1<<30), R0
	CBZ	R0, 2(PC)
	MOV	$(3<<20), R1
	MSR	R1, SPR(CPACR_EL1)
	DSB	$SY
	ISB	$SY
	RETURN

TEXT	fpsavef1(SB), 1, $-4
	FMOVD F1, (R0)
	RETURN

#define FSTQ(src,dst,off)	WORD	$(0x3D800000 | src<<0 | dst<<5 | (off/16)<<10)
TEXT fpsaveregs(SB), 1, $-4
	FSTQ	(0,0,0)
	FSTQ	(1,0,16)
	FSTQ	(2,0,32)
	FSTQ	(3,0,48)
	FSTQ	(4,0,64)
	FSTQ	(5,0,80)
	FSTQ	(6,0,96)
	FSTQ	(7,0,112)
	FSTQ	(8,0,128)
	FSTQ	(9,0,144)
	FSTQ	(10,0,160)
	FSTQ	(11,0,176)
	FSTQ	(12,0,192)
	FSTQ	(13,0,208)
	FSTQ	(14,0,224)
	FSTQ	(15,0,240)
	RETURN

#define FLDQ(src,off,dst)	WORD	$(0x3DC00000 | dst<<0 | src<<5 | (off/16)<<10)
TEXT fprestregs(SB), 1, $-4
	FLDQ	(0,0,0)
	FLDQ	(0,16,1)
	FLDQ	(0,32,2)
	FLDQ	(0,48,3)
	FLDQ	(0,64,4)
	FLDQ	(0,80,5)
	FLDQ	(0,96,6)
	FLDQ	(0,112,7)
	FLDQ	(0,128,8)
	FLDQ	(0,144,9)
	FLDQ	(0,160,10)
	FLDQ	(0,176,11)
	FLDQ	(0,192,12)
	FLDQ	(0,208,13)
	FLDQ	(0,224,14)
	FLDQ	(0,240,15)
	RETURN

TEXT cas(SB), 1, $-4
	MOVWU	ov+8(FP), R1
	MOVWU	nv+16(FP), R2
#ifdef ATOMICS
	MOV	R1, R3
	CASALW(1,2,0)
	CMP	R1, R3
	BNE	3(PC)
	MOV	$1, R0
	RETURN
	MOV	$0, R0
	RETURN
#else
_cas1:
	LDXRW	(R0), R3
	CMP	R3, R1
	BNE	_cas0
	STXRW	R2, (R0), R4
	CBNZ	R4, _cas1
	MOVW	$1, R0
	DMB	$ISH
	RETURN
_cas0:
	CLREX
	MOVW	$0, R0
	RETURN
#endif

TEXT setlabel(SB), 1, $-4
	MOV	SP, R1
	MOVP	R1, LR, 0(R0)
	MOVW	$0, R0
	RETURN

TEXT gotolabel(SB), 1, $-4
	MOVP	0(R0), R1, LR
	MOV	R1, SP
	MOVW	$1, R0
	RETURN

TEXT getcallerpc(SB), $0
	MOV	0(SP), R0
	RETURN

TEXT idlehands(SB), 1, $-4
	MRS	DAIF, R3
	MSR	$(Dirq|Dfiq), DAIFSet	/* splfhi */
	DSB	$SY
	MOVW	nrdy(SB), R0
	CBNZ	R0, 3(PC)
	WFI
	DSB	$SY
	MSR	R3, DAIF		/* splx */
	RETURN

TEXT cachedwbtlb(SB), 1, $-4
TEXT coherence(SB), 1, $-4
	DSB	$SY
	RETURN

/*
 * invalidate tlb
 */
TEXT	mmuinvalidate(SB), 0, $-4
	DSB	$NSHST
	TLBI	ZR, 0,8,7,0		/* VMALLE1 */
	DSB	$NSH
	ISB	$SY
	RETURN

TEXT	mmuinvalidateaddr(SB), 0, $-4
	DSB	$NSHST
	LSR	$12, R0
	TLBI	R0, 0,8,7,3		/* VAAE1 */
	DSB	$NSH
	ISB	$SY
	RETURN

#include "cache.v8.s"

TEXT reboot(SB), $0
TEXT callwithureg(SB), $0
	RETURN
