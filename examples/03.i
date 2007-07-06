	NAME _main
_main:	0	FRAME	0 => r0 [ i ]	# function prologue
	0	iSLDor	@A 0 0 r0 => r1
	0	iSLDor	@B 0 0 r0 => r2
	12	BR	L2 L3 r0
L2:	11	NOP
	3	iLDI	1 => r99
	8	JMPl	L4
L3:	12	NOP
	3	iLDI	1 => r99
	8	JMPl	L4
L4:	12	NOP
	13	iADD	r1 r1 => r99
	13	iADD	r2 r2 => r99
	13	iADD	r2 r2 => r99
	13	iADD	r2 r2 => r99
	13	iADD	r2 r2 => r99
	13	iADD	r2 r2 => r99
	13	iADD	r2 r2 => r99
	13	iADD	r2 r2 => r99
	13	iADD	r2 r2 => r99
	13	iADD	r2 r2 => r99
	13	iADD	r2 r2 => r99
	13	iADD	r2 r2 => r99
	12	BR	L5 L6 r0
L5:	11	NOP
	3	iLDI	1 => r99
	8	JMPl	L7
L6:	12	NOP
	3	iLDI	1 => r99
	8	JMPl	L7
L7:	12	NOP
	0	iSLDor	@C 0 0 r0 => r3
	13	iADD	r2 r2 => r99
	12	BR	L8 L9 r0
L8:	11	NOP
	3	iLDI	1 => r99
	8	JMPl	L10
L9:	12	NOP
	3	iLDI	1 => r99
	8	JMPl	L10
L10:	12	NOP
	0	iSLDor	@D 0 0 r0 => r4
	13	iADD	r3 r3 => r99
	13	iADD	r3 r3 => r99
	13	iADD	r3 r3 => r99
	13	iADD	r3 r3 => r99
	13	iADD	r3 r3 => r99
	13	iADD	r3 r3 => r99
	12	BR	L11 L12 r0
L11:	11	NOP
	3	iLDI	1 => r99
	8	JMPl	L13
L12:	12	NOP
	3	iLDI	1 => r99
	8	JMPl	L13
L13:	12	NOP
	0	iSLDor	@E 0 0 r0 => r5
	13	iADD	r4 r4 => r99
	12	BR	L14 L15 r0
L14:	11	NOP
	3	iLDI	1 => r99
	8	JMPl	L16
L15:	12	NOP
	3	iLDI	1 => r99
	8	JMPl	L16
L16:	12	NOP
	0	iSLDor	@F 0 0 r0 => r6
	12	BR	L17 L18 r0
L17:	11	NOP
	3	iLDI	1 => r99
	8	JMPl	L19
L18:	12	NOP
	3	iLDI	1 => r99
	8	JMPl	L19
L19:	12	NOP
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r5 r5 => r99
	13	iADD	r6 r6 => r99
	12	BR	L17 L18 r0
	16	iRTN	r0 r35
