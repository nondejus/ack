.define __read
.sect .text
.sect .rom
.sect .data
.sect .bss
.sect .text
.extern __read
.sect .text
__read:
tst.b -40(sp)
link	a6,#-0
move.w 14(a6), d2
ext.l d2
move.w 8(a6), d1
ext.l d1
move.l d2,-(sp)
move.l 10(a6),-(sp)
move.l d1,-(sp)
jsr __Sread
lea 12(sp),sp
unlk a6
rts
__Sread:		trap #0
.data2	0x3
			bcc	1f
			jmp	cerror
1:
			rts
