.sect .text; .sect .rom; .sect .data; .sect .bss
.sect .text
.define cfu
cfu:
	move.l	(sp)+,a0
	move.l	(sp)+,d1	! should be 4
	move.l	(sp)+,d0
	cmp.l	#4,d0
	bne	1f
	move.l	(sp),d0
	move.l	a0,-(sp)
	jsr	(Funs)
	move.l	d0,(4,sp)
	rts
1:
	move.l	(sp)+,d0
	move.l	(sp),d1
	move.l	a0,-(sp)
	jsr	(Fund)
	move.l	d0,(4,sp)
	rts
