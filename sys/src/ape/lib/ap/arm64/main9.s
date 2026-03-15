TEXT	_main(SB), 1,$24
	MOV	$setSB(SB), R28
	BL	_envsetup(SB)
	MOV	$inargv+0(FP), R0
	MOV	R0, 16(RSP)
	MOV	inargc-8(FP), R0
	BL	main(SB)
	BL	exit(SB)
	B	-1(PC)
