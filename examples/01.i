	NAME _main
_main:	0	FRAME	0 => r0 [ i ]	# function prologue
	3	iLDI	1 => r1
	4	iLDI	2 => r2
	13	iADD	r1 r1 => r99
	13	iADD	r1 r1 => r99
	13	iADD	r2 r2 => r99
	8	JMPl	L2_loop
L2_loop:	11	NOP
	4	iLDI	3 => r3
	13	iADD	r3 r3 => r99
	13	iADD	r3 r3 => r99
	13	iADD	r2 r2 => r99
	8	JMPl	L3_loop
L3_loop:	12	NOP
	4	iLDI	4 => r4
	13	iADD	r3 r3 => r99
	13	iADD	r4 r4 => r99
	13	iADD	r4 r4 => r99
	8	JMPl	L4_loop
L4_loop:	12	NOP
	16	iRTN	r0 r25
