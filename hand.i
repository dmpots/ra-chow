  NAME _main
_main:	0	FRAME	0 => r0 [ i ]	# function prologue
  1 NOP #x = 0
  1 NOP#for(i = 0; i < 10; i++)
  1 NOP#{
  1 NOP#  x += 3 + i
  1 NOP#}
  1 NOP#return 0
  1 iLDI 0  => r1  #x = 0
  1 iLDI 0  => r2  #i = 0
  1 iLDI 10 => r3  #loop head test
  1 iCMPlt r2 r3 => r4
  1	BR	L1_main L2_main r4
L1_main:	9	NOP
  2 iADDI 3 r2 => r5
  2	iADD	r1 r5 => r8
  2 i2i   r8 => r1
  2 iADDI 1 r2 =>r9 #update induction variable
  2 i2i   r9 => r2
  2 iCMPlt r2 r3 => r6
  2 BR L1_main L2_main r6
L2_main:	10	NOP
  1	iLDI	0 => r7
  1	iRTN	r0 r7

