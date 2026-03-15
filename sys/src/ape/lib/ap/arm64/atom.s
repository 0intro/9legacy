#define ISH	(2<<2 | 3)

/*
 * int casl(ulong *p, ulong ov, ulong nv);
 * int cas32(u32int*, u32int, u32int);
 */

TEXT	casl+0(SB),0,$0		/* r0 holds p */
TEXT	cas32+0(SB),0,$0		/* r0 holds p */
	MOVWU	ov+8(FP), R1
	MOVWU	nv+16(FP), R2
spincas:
	LDXRW	0(R0), R3
	CMP	R3, R1
	BNE	fail
	STXRW	R2, 0(R0), R4
	CBNZ	R4, spincas
	MOVW	$1, R0
	DMB	$ISH
	RETURN
fail:
	CLREX
	MOVW	$0, R0
	DMB	$ISH
	RETURN

/*
 * int casp(void**, void*, void*);
 */

TEXT	casp+0(SB),0,$0		/* r0 holds p */
	MOV	ov+8(FP), R1
	MOV	nv+16(FP), R2
spincasp:
	LDXR	0(R0), R3
	CMP	R3, R1
	BNE	fail
	STXR	R2, 0(R0), R4
	CBNZ	R4, spincasp
	MOVW	$1, R0
	DMB	$ISH
	RETURN

TEXT _xinc(SB), $0	/* void	_xinc(long *); */
TEXT ainc(SB), $0	/* long ainc(long *); */
	DMB	$ISH
spinainc:
	LDXRW	0(R0), R3
	ADD	$1,R3
	STXRW	R3, 0(R0), R4
	CBNZ	R4, spinainc
	MOVW	R3, R0
	DMB	$ISH
	RETURN

TEXT _xdec(SB), $0	/* long _xdec(long *); */
TEXT adec(SB), $0	/* long adec(long *); */
	DMB	$ISH
spinadec:
	LDXRW	0(R0), R3
	SUB	$1,R3
	STXRW	R3, 0(R0), R4
	CBNZ	R4, spinadec
	MOVW	R3, R0
	DMB	$ISH
	RETURN
