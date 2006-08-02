	NAME _main
_main:	0	FRAME	8 => r0 r1 [ i i ]	# function prologue
	3	iLDI	1 => r3
	3	i2i	r3 => r2
	4	iADDI	0 r0 => r4		# get address of main_k
	4	iLDI	0 => r5
	4	iSSTor	@main_k_0 4 0 r4 r5
	7	i2i	r2 => r7
	7	iLDI	0 => r8
	7	iCMPne	r7 r8 => r28
	7	BR	L1_main L4_main r28
L4_main:	7	NOP
	7	iLDI	1 => r10
	7	i2i	r10 => r2
	8	JMPl	L2_main
L1_main:	9	NOP
	9	iLDI	2 => r14
	9	i2i	r14 => r2
L2_main:	10	NOP
	10	iADDI	0 r0 => r16		# get address of main_k
	10	i2i	r2 => r17
	10	iLDI	2 => r18
	10	iADD	r17 r18 => r19
	10	iSSTor	@main_k_0 4 0 r16 r19
	12	iLDI	L3_main => r21
	12	iSLDor	@main_k_0 4 0 r16 => r23
	12	iJSRl	_printf	r0   r21 r23 => r26
	18	iLDI	0 => r29
	18	iRTN	r0 r29
	iSSTACK	0	@main_k_0 _main
	iXFUNC _printf
L3_main:	bDATA 37 1
	bDATA 100 1
	bDATA 10 1
	bDATA 0 1
	ALIAS @_addr_globals [ @L3_main_0 ]
	iGLOBAL	0	@L3_main_0	L3_main
