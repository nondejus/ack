.define	.cmi
.sect .text
.sect .rom
.sect .data
.sect .bss

.sect .text
	! on entry d0: # bytes in 1 block
	! on exit d0: result
.cmi:
	move.l	(sp)+, d2	! return address
	move.l	sp, a0		! address of top block
	lea	(sp,d0.l), a1	! address of lower block
	move.l	d0, d1
	asr.l	#2, d0
1:
	cmp.l	(a0)+, (a1)+
	bne	2f
	sub.l	#1, d0
	bne	1b
2:
	bge	3f
	neg.l	d0		! less
3:
	lea	(sp,d1.l*2), sp	! new sp; two blocks popped
	move.l	d2,a0
	jmp	(a0)		! return
.align 2
