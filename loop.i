	NAME _loop
_loop:	0	FRAME	0 => r0 r1 [ i i ]	# function prologue
	3	iLDI	1 => r4
	3	i2i	r4 => r2
	4	iLDI	0 => r5
	4	i2i	r5 => r3
	7	i2i	r2 => r6
	7	i2i	r3 => r7
	7	iCMPne	r6 r7 => r32
	7	BR	L1_loop L7_loop r32
L7_loop:	7	NOP
	7	iLDI	2 => r9
	7	i2i	r9 => r3
	8	JMPl	L2_loop
L1_loop:	9	NOP
	9	iLDI	3 => r13
	9	i2i	r13 => r3
L2_loop:	11	NOP
L3_loop:	12	NOP
	12	i2i	r2 => r16
	12	iLDI	10 => r17
	12	iCMPge	r16 r17 => r33
	12	BR	L5_loop L8_loop r33
L8_loop:	12	NOP
L4_loop:	12	NOP
	13	i2i	r2 => r20
	13	iLDI	1 => r21
	13	iADD	r20 r21 => r22
	13	i2i	r22 => r2
L6_loop:	15	NOP
	15	i2i	r2 => r24
	15	iLDI	10 => r25
	15	iCMPlt	r24 r25 => r34
	15	BR	L4_loop L9_loop r34
L9_loop:	15	NOP
L5_loop:	15	NOP
	15	iLDI	5 => r28
	15	i2i	r2 => r29
	15	iMUL	r28 r29 => r30
	15	i2i	r30 => r3
	16	iLDI	0 => r35
	16	iRTN	r0 r35
