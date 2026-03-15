TEXT	memset(SB), $0
	MOV	R0, R11
	MOVBU	c+8(FP), R10
	MOVWU	n+16(FP), R9

	/* align dst to 8 bytes */
f0:
	ANDS	$15, R11, R8
	BEQ	f1
	SUBS	$1, R9
	BLT	done
	MOVB	R10, (R11)1!
	B	f0
f1:
	/* need at least 16 for 2-register store */
	SUBS	$16, R9
	BLT	tail
	/* duplicate low byte 8 times */
	ORR	R10<<8, R10
	ORR	R10<<16, R10
	ORR	R10<<32, R10
f2:
	/* store 16 bytes at a time */
	MOVP	R10, R10, (R11)16!
	SUBS	$16, R9
	BGE	f2

	/* remaining bytes */
tail:
	ADDS	$15, R9
	BLT	done
f3:
	MOVB	R10, (R11)1!
	SUBS	$1, R9
	BGE	f3

done:
	RETURN
