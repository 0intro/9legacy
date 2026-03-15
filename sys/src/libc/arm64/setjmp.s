arg=0
link=30

TEXT	setjmp(SB), 1, $-4
	MOV	SP, R2
	MOVP	R2, R(link), 0(R(arg))
	MOV	$0, R(arg)
	RETURN

TEXT	longjmp(SB), 1, $-4
	MOVP	0(R(arg)), R2, R(link)
	MOV	R2, SP	
	MOV	r+8(FP), R(arg)
	CBZ	R(arg), ret1
	RETURN
ret1:
	/* ansi: "longjmp(0) => longjmp(1)" */
	MOVW	$1, R(arg)
	RETURN
