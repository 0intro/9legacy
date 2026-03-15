TEXT	memcpy(SB), $-20
TEXT	memmove(SB), $-20
	MOV	R0, s1+0(FP)

	MOVWU	n+16(FP), R9	/* count */
	CBZ	R9, return
	MOV	R0, R11		/* dest pointer */
	MOV	s2+8(FP), R10	/* source pointer */
	CMP	R11, R10
	BEQ	return
	BLO	back

/*
 * byte-at-a-time forward copy to
 * get source (R10) vlong aligned.
 */
f1:
	ANDS	$7, R10, R8
	BEQ	f2
	SUBS	$1, R9
	BLT	return
	MOVB	(R10)1!, R8
	MOVB	R8, (R11)1!
	B	f1

/*
 * check that dest is vlong aligned
 * if not, just go byte-at-a-time
 */
f2:
	ANDS	$7, R11, R8
	BEQ	f3
	SUBS	$1, R9
	BLT	return
	B	f5
/*
 * two-vlongs-at-a-time forward copy
 */
f3:
	SUBS	$16, R9
	BLT	f4
	MOVP	(R10)16!, R12, R13
	MOVP	R12, R13, (R11)16!
	B	f3

/*
 * cleanup byte-at-a-time
 */
f4:
	ADDS	$15, R9
	BLT	return
f5:
	MOVB	(R10)1!, R8
	MOVB	R8, (R11)1!
	SUBS	$1, R9
	BGE	f5

return:
	MOVW	s1+0(FP),R0
	RETURN

/*
 * everything the same, but
 * copy backwards
 */
back:
	ADD	R9, R10
	ADD	R9, R11

/*
 * byte-at-a-time backward copy to
 * get source (R10) vlong aligned.
 */
b1:
	ANDS	$7, R10, R8
	BEQ	b2
	SUBS	$1, R9
	BLT	return
	MOVB	-1(R10)!, R8
	MOVB	R8, -1(R11)!
	B	b1

/*
 * check that dest is aligned
 * if not, just go byte-at-a-time
 */
b2:
	ANDS	$7, R11, R8
	BEQ	b3
	SUBS	$1, R9
	BLT	return
	B	b5
/*
 * two-vlongs-at-a-time backward copy
 */
b3:
	SUBS	$16, R9
	BLT	b4
	MOVP	-16(R10)!, R12, R13
	MOVP	R12, R13, -16(R11)!
	B	b3

/*
 * cleanup byte-at-a-time backward
 */
b4:
	ADDS	$15, R9
	BLT	return
b5:
	MOVB	-1(R10)!, R8
	MOVB	R8, -1(R11)!
	SUBS	$1, R9
	BGE	b5
	B	return
