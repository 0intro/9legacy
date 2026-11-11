/* amd64 APE version; must match notetramp.c */
TEXT	longjmp(SB), $0		/* void longjmp(jmp_buf env, int val) */
	MOVL	r+8(FP), AX
	CMPL	AX, $0
	JNE	ok		/* ansi: "longjmp(0) => longjmp(1)" */
	MOVL	$1, AX		/* bless their pointed heads */
ok:
	MOVQ	0(RARG), SP	/* restore sp */
	MOVQ	8(RARG), BX	/* put return pc on the stack */
	MOVQ	BX, 0(SP)
	RET

TEXT	setjmp(SB), $0		/* int setjmp(jmp_buf env) */
	MOVQ	SP, 0(RARG)	/* store sp */
	MOVQ	0(SP), BX	/* store return pc */
	MOVQ	BX, 8(RARG)
	MOVL	$0, AX		/* return 0 */
	RET

TEXT	sigsetjmp(SB), $0	/* sigsetjmp(sigjmp_buf, int mask) */
	MOVL	savemask+8(FP), BX	/* store signal stuff */
	MOVL	BX, 0(RARG)
	/*
	 * TODO: this seemed wrong (MOVL $_psig...): surely
	 * MOVL _psigblocked(SB), ...?
	 * current code matches all other archs' impl'ns,
	 * but they could all be wrong, this is not heavily used.
	 */
	MOVL	$_psigblocked(SB), 4(RARG)
	MOVQ	SP, 8(RARG)	/* store sp */
	MOVQ	0(SP), BX	/* store return pc */
	MOVQ	BX, 16(RARG)
	MOVL	$0, AX	/* return 0 */
	RET
