#define NPRIVATES	16

arg=0
sb=28

TEXT	_mainp(SB), 1, $(16 + NPRIVATES*8)
	MOV	$setSB(SB), R(sb)
	MOV	R(arg), _tos(SB)

	MOV	$p-(NPRIVATES*8)(SP), R1
	MOV	R1, _privates(SB)
	MOV	$NPRIVATES, R1
	MOV	R1, _nprivates(SB)

	BL	_profmain(SB)
	/* _tos->prof.pp = _tos->prof.next; */
	MOV	_tos(SB), R1
	MOV	8(R1), R0
	MOV	R0, 0(R1)

	MOV	$inargv+0(FP), R(arg)
	MOV	R(arg), argv-(8*NPRIVATES+16)(SP)
	MOV	inargc-8(FP), R(arg)
	BL	main(SB)
loop:
	MOV	$_exitstr<>(SB), R(arg)
	BL	exits(SB)
	B	loop

TEXT	_savearg(SB), 1, $0
	RETURN

TEXT	_callpc(SB), 1, $0
	MOV	0(SP), R0
	RETURN

DATA	_exitstr<>+0(SB)/4, $"main"
GLOBL	_exitstr<>+0(SB), $5
