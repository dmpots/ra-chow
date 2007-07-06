	NAME _main
_main:	0	FRAME	0 => r0 r1 [ i i ]	# function prologue
	3	iLDI	2 => r2
	4	iLDI	3 => r3
	13	iADD	r3 r3 => r4
	8	JMPl	L2_loop
L2_loop:	11	NOP
	13	iADD	r2 r2 => r4
	13	iADD	r3 r3 => r4
	13	iADD	r3 r3 => r4
	12	iCMPge	r2 r3 => r5
	12	BR	L2_loop L3_loop r5
L3_loop:	12	NOP
	16	iRTN	r0 r35
