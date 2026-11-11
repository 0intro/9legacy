/* riscv64 APE version; must match notetramp.c */
arg=8
link=1
sp=2

TEXT	setjmp(SB), 1, $-4		/* int setjmp(jmp_buf env) */
	MOV	R(sp), (R(arg))
	MOV	R(link), XLEN(R(arg))
	MOV	R0, R(arg)
	RET

TEXT	sigsetjmp(SB), 1, $-4		/* sigsetjmp(sigjmp_buf, int mask) */
	MOVW	savemask+8(FP), R(arg+2)	/* save signal stuff */
	MOVW	R(arg+2), 0(R(arg))
	MOVW	_psigblocked(SB), R(arg+2)
	MOVW	R(arg+2), 4(R(arg))		/* save _psigblocked */
	MOV	R(sp), 8(R(arg))		/* save sp */
	MOV	R(link), 16(R(arg))		/* save return pc */
	MOV	R0, R(arg)
	RET

TEXT	longjmp(SB), 1, $-4		/* void longjmp(jmp_buf env, int val) */
	MOVW	r+XLEN(FP), R(arg+2)
	BNE	R(arg+2), ok		/* ansi: "longjmp(0) => longjmp(1)" */
	MOV	$1, R(arg+2)		/* bless their pointed heads */
ok:	MOV	(R(arg)), R(sp)
	MOV	XLEN(R(arg)), R(link)
	MOV	R(arg+2), R(arg)
	RET
