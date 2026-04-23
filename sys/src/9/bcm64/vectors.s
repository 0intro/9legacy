#define _ERET 0xd69f03e0
#define NOP WORD $0
#define NOP10	NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP

#define VECTOR0(dest) \
	SUB	$(16+Ureg_size), RSP; \
	MOV	R30, (16+Ureg_r30)(RSP); \
	MRS	SP_EL0, R30; \
	MOV	R30, (16+Ureg_sp)(RSP); \
	BL	uregsave(SB); \
	MOV	$setSB(SB), R28; \
		MRS	SPR(0x1800a0), R30;	/* mpidr_el1 */ \
		LSR	$8, R30; \
		AND	$0x3, R30; \
		MOV	$machaddr(SB), R(MACH); \
		ADD	R30<<3, R(MACH); \
		MOV	(R(MACH)), R(MACH); \
		MOV	16(R(MACH)), R(USER); \
	ADD	$(16+0), RSP, R0; \
	BL	dest(SB); \
	MSR	$0x3, DAIFSet; \
	BL	uregrestore(SB); \
	MOV	(16+Ureg_sp)(RSP), R30; \
	MSR	R30, SP_EL0; \
	MOV	(16+Ureg_r30)(RSP), R30; \
	ADD	$(16+Ureg_size), RSP; \
	WORD	$(_ERET); \
	NOP10

#define VECTOR1(dest) \
	SUB	$(16+Ureg_size), RSP; \
	MOV	R30, (16+Ureg_r30)(RSP); \
	ADD	$(16+Ureg_size), RSP, R30; \
	MOV	R30, (16+Ureg_sp)(RSP); \
	BL	uregsave(SB); \
	ADD	$(16+0), RSP, R0; \
	BL	dest(SB); \
	MSR	$0x3, DAIFSet; \
		MOV	R(MACH), (16+(8*MACH))(RSP); \
		MOV	R(USER), (16+(8*USER))(RSP); \
	BL	uregrestore(SB); \
	MOV	(16+Ureg_r30)(RSP), R30; \
	ADD	$(16+Ureg_size), RSP; \
	WORD	$(_ERET); \
	NOP10; NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP

	
TEXT	_vectors(SB), 0, $-4
vectors:
/* exception from EL1 using SP_EL0 (not used) */
	BL	_start(SB)
	NOP10; NOP10; NOP10; NOP
	VECTOR1(irq)
	VECTOR1(fiq)
	VECTOR1(serror)
/* exception from EL1 using SP_EL1 */
	VECTOR1(trap)
	VECTOR1(irq)
	VECTOR1(fiq)
	VECTOR1(serror)
/* exception from EL0 aarch64 */
	VECTOR0(trap)
	VECTOR0(irq)
	VECTOR0(fiq)
	VECTOR0(serror)
/* exception from EL0 aarch32 */
	VECTOR0(trap)
	VECTOR0(irq)
	VECTOR0(fiq)
	VECTOR0(serror)

TEXT uregsave(SB), 0, $-4
	MOVP	R0, R1, (16+Ureg_r0)(RSP)
	MOVP	R2, R3, (16+Ureg_r2)(RSP)
	MOVP	R4, R5, (16+Ureg_r4)(RSP)
	MOVP	R6, R7, (16+Ureg_r6)(RSP)
	MOVP	R8, R9, (16+Ureg_r8)(RSP)
	MOVP	R10, R11, (16+Ureg_r10)(RSP)
	MOVP	R12, R13, (16+Ureg_r12)(RSP)
	MOVP	R14, R15, (16+Ureg_r14)(RSP)
	MOVP	R16, R17, (16+Ureg_r16)(RSP)
	MOVP	R18, R19, (16+Ureg_r18)(RSP)
	MOVP	R20, R21, (16+Ureg_r20)(RSP)
	MOVP	R22, R23, (16+Ureg_r22)(RSP)
	MOVP	R24, R25, (16+Ureg_r24)(RSP)
	MOVP	R26, R27, (16+Ureg_r26)(RSP)
	MOVP	R28, R29, (16+Ureg_r28)(RSP)
	MRS	ELR_EL1, R0
	MOV	R0, (16+Ureg_pc)(RSP)
	MRS	SPR(ESR_EL1), R0
	MOV	R0, (16+Ureg_type)(RSP)
	MRS	SPSR_EL1, R0
	MOV	R0, (16+Ureg_psr)(RSP)
	RETURN

TEXT uregrestore(SB), 0, $-4
	MOV	(16+Ureg_pc)(RSP), R0
	MSR	R0, ELR_EL1
	MOV	(16+Ureg_psr)(RSP), R0
	MSR	R0, SPSR_EL1
	MOVP	(16+Ureg_r0)(RSP), R0, R1
	MOVP	(16+Ureg_r2)(RSP), R2, R3
	MOVP	(16+Ureg_r4)(RSP), R4, R5
	MOVP	(16+Ureg_r6)(RSP), R6, R7
	MOVP	(16+Ureg_r8)(RSP), R8, R9
	MOVP	(16+Ureg_r10)(RSP), R10, R11
	MOVP	(16+Ureg_r12)(RSP), R12, R13
	MOVP	(16+Ureg_r14)(RSP), R14, R15
	MOVP	(16+Ureg_r16)(RSP), R16, R17
	MOVP	(16+Ureg_r18)(RSP), R18, R19
	MOVP	(16+Ureg_r20)(RSP), R20, R21
	MOVP	(16+Ureg_r22)(RSP), R22, R23
	MOVP	(16+Ureg_r24)(RSP), R24, R25
	MOVP	(16+Ureg_r26)(RSP), R26, R27
	MOVP	(16+Ureg_r28)(RSP), R28, R29
	RETURN

TEXT forkret(SB), 0, $-4
	SUB	$16, RSP
	MOV	R(USER), (16+(8*USER))(RSP)
	BL	uregrestore(SB)
	MOV	(16+Ureg_sp)(RSP), R30
	MSR	R30, SP_EL0
	MOV	(16+Ureg_r30)(RSP), R30
	ADD	$(16+Ureg_size), RSP
	ERET
