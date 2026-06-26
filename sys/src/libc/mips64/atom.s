/*
 *	R4000 user-level atomic operations
 */

#define	LL(base, rt)	WORD	$((060<<26)|((base)<<21)|((rt)<<16))
#define	SC(base, rt)	WORD	$((070<<26)|((base)<<21)|((rt)<<16))
#define	LLD(base, rt)	WORD	$((064<<26)|((base)<<21)|((rt)<<16))
#define	SCD(base, rt)	WORD	$((074<<26)|((base)<<21)|((rt)<<16))
#define	NOOP		WORD	$0x27

TEXT ainc(SB), 1, $-8			/* long ainc(long *); */
TEXT _xinc(SB), 1, $-8			/* void _xinc(long *); */
	MOVV	R1, R2			/* address of counter */
loop:	MOVW	$1, R3
	LL(2, 1)
	NOOP
	ADDU	R1, R3
	MOVV	R3, R1			/* return new value */
	SC(2, 3)
	NOOP
	BEQ	R3,loop
	RET

TEXT adec(SB), 1, $-8			/* long adec(long*); */
TEXT _xdec(SB), 1, $-8			/* long _xdec(long *); */
	MOVV	R1, R2			/* address of counter */
loop1:	MOVW	$-1, R3
	LL(2, 1)
	NOOP
	ADDU	R1, R3
	MOVV	R3, R1			/* return new value */
	SC(2, 3)
	NOOP
	BEQ	R3,loop1
	RET

/*
 * int cas(uint* p, int ov, int nv);
 */
TEXT cas(SB), 1, $-8
	MOVW	ov+12(FP), R2
	MOVW	nv+20(FP), R3
spincas:
	LL(1, 4)			/* R4 = *R1 */
	NOOP
	BNE	R2, R4, fail
	SC(1, 3)			/* *R1 = R3 */
	NOOP
	BEQ	R3, spincas		/* R3 == 0 means store failed */
	MOVW	$1, R1
	RET
fail:
	MOVV	$0, R1
	RET

/*
 * int casp(void** p, void* ov, void* nv);
 * pointers are 64-bit, so compare and swap the whole doubleword
 */
TEXT casp(SB), 1, $-8
	MOVV	ov+8(FP), R2
	MOVV	nv+16(FP), R3
spincasp:
	LLD(1, 4)			/* R4 = *R1 */
	NOOP
	BNE	R2, R4, faspl
	SCD(1, 3)			/* *R1 = R3 */
	NOOP
	BEQ	R3, spincasp		/* R3 == 0 means store failed */
	MOVV	$1, R1
	RET
faspl:
	MOVV	$0, R1
	RET
