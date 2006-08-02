	NAME _main
_main:	0	FRAME	0 => r0 r1 r2 r3 [ i i i i ]	# function prologue
	3	iLDI	0 => r6
	3	i2i	r6 => r5
	4	i2i	r2 => r7
	4	i2i	r7 => r4
L1_main:	6	NOP
	6	i2i	r4 => r9
	6	iLDI	0 => r10
	6	iCMPle	r9 r10 => r42
	6	BR	L3_main L8_main r42
L8_main:	6	NOP
L2_main:	6	NOP
	7	i2i	r4 => r13
	7	iLDI	4 => r14
	7	iMOD	r13 r14 => r15
	7	iLDI	0 => r16
	7	iCMPne	r15 r16 => r43
	7	BR	L5_main L9_main r43
L9_main:	7	NOP
	8	i2i	r5 => r18
	8	iLDI	1 => r19
	8	iSUB	r18 r19 => r20
	8	i2i	r20 => r5
	9	JMPl	L3_main
L5_main:	10	NOP
	11	i2i	r5 => r24
	11	iLDI	1 => r25
	11	iADD	r24 r25 => r26
	11	i2i	r26 => r5
	13	i2i	r4 => r27
	13	iLDI	1 => r28
	13	iSUB	r27 r28 => r29
	13	i2i	r29 => r4
L4_main:	15	NOP
	15	i2i	r4 => r31
	15	iLDI	0 => r32
	15	iCMPgt	r31 r32 => r44
	15	BR	L2_main L10_main r44
L10_main:	15	NOP
L3_main:	15	NOP
	15	iLDI	L7_main => r35
	15	i2i	r5 => r37
	15	iJSRl	_printf	r0   r35 r37 => r40
	16	iLDI	0 => r45
	16	iRTN	r0 r45
	iXFUNC _printf
L7_main:	bDATA 82 1
	bDATA 101 1
	bDATA 115 1
	bDATA 117 1
	bDATA 108 1
	bDATA 116 1
	bDATA 32 1
	bDATA 61 1
	bDATA 32 1
	bDATA 37 1
	bDATA 100 1
	bDATA 10 1
	bDATA 0 1
	ALIAS @_addr_globals [ @L7_main_0 ]
	iGLOBAL	0	@L7_main_0	L7_main
