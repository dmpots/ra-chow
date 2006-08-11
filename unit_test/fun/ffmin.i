	NAME _ffmin
_ffmin:	0	FRAME	8 => r0 r1 r2 r3 [ i i d i ]	# function prologue
	4	d2d	r2 => r4
	4	i2i	r3 => r6
	4	dJSRr	r6	r0   r4 => r7
	4	iADDI	0 r0 => r8		# get address of ffmin_y
	4	dSSTor	@ffmin_y_0 8 0 r8 r7
	5	iADDI	0 r0 => r10		# get address of ffmin_y
	5	dSLDor	@ffmin_y_0 8 0 r10 => r11
	5	dRTN	r0 r11
	fSSTACK	0	@ffmin_y_0 _ffmin
