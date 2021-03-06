.define .dvi4
.sect .text
.sect .rom
.sect .data
.sect .bss
.sect .text

! 4-byte divide routine for z80
! parameters:
!    stack: divisor
!	    dividend
!    stack: quotient (out)
!    bc de: remainder (out)  (high part in bc)



.dvi4:
	pop hl
	ld (retaddr),hl
	xor a
	ld (.flag1),a
	ld (.flag2),a
	ld ix,0
	add ix,sp
	ld b,(ix+7)		! dividend
	bit 7,b
	jr z,1f
	ld c,(ix+6)
	ld d,(ix+5)
	ld e,(ix+4)
	call .negbd
	ld (ix+7),b
	ld (ix+6),c
	ld (ix+5),d
	ld (ix+4),e
	ld a,1
	ld (.flag1),a
1:
	ld b,(ix+3)
	bit 7,b
	jr z,2f
	call .negst
	ld a,1
	ld (.flag2),a
2:
	call .dvu4
	ld a,(.flag1)
	or a
	jr z,3f
	call .negbd
3:
	ld (.savebc),bc
	ld (.savede),de
	ld a,(.flag2)
	ld b,a
	ld a,(.flag1)
	xor b
	jr z,4f
	call .negst
4:
	ld bc,(.savebc)
	ld de,(.savede)
	ld hl,(retaddr)
	jp (hl)
.negbd:
	xor a
	ld h,a
	ld l,a
	sbc hl,de
	ex de,hl
	ld h,a
	ld l,a
	sbc hl,bc
	ld b,h
	ld c,l
	ret
.negst:
	pop ix
	pop de
	pop bc
	call .negbd
	push bc
	push de
	jp (ix)
.sect .data
	.flag1: .data1 0
	.flag2: .data1 0
	retaddr:.data2 0
	.savebc: .data2 0
	.savede: .data2 0
