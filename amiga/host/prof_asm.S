/* amiga/host/prof.c's interrupt handler (the verify program only). GNU as, 68020. */

	.text
/* the sampling profiler's level 3 interrupt (amiga/host/prof.c): on a copper interrupt, the
 * interrupted PC's 16-byte block in the program's code is counted (68020: scaled index) */
	.globl	prof_handler
prof_handler:
	movem.l	%d0-%d1/%a0,-(%sp)		/* 12 bytes: the frame's PC is at 14(sp) */
	lea	0xdff000,%a0
	move.w	0x1e(%a0),%d0
	btst	#4,%d0				/* COPER */
	beq.s	.Lpclr
	move.l	14(%sp),%d0
	sub.l	prof_base,%d0
	cmp.l	prof_size,%d0
	bcc.s	.Lpout
	lsr.l	#4,%d0
	move.l	prof_hist,%a0
	addq.l	#1,(%a0,%d0.l*4)
	bra.s	.Lpdone
.Lpout:
	addq.l	#1,prof_other
.Lpdone:
	lea	0xdff000,%a0
.Lpclr:
	move.w	#0x0030,0x9c(%a0)		/* COPER and VERTB, twice */
	move.w	#0x0030,0x9c(%a0)
	movem.l	(%sp)+,%d0-%d1/%a0
	rte

/* the level 3 interrupt while the picture is shown and profiled (amiga/host/screen.c): a copper
 * interrupt counts the interrupted PC's block when prof_on is set; a vertical blank counts vb_count */
	.globl	show_handler
show_handler:
	movem.l	%d0-%d1/%a0,-(%sp)
	move.l	show_dbg,%d1			/* the debug block in Chip RAM (screen.c): the PC at each vblank */
	beq.s	.Lnodbg
	move.l	%d1,%a0
	move.l	14(%sp),12(%a0)
	addq.l	#1,16(%a0)
	move.w	0xdff01e,28(%a0)		/* INTREQR, INTENAR at the entry (the debug block's words 14, 15) */
	move.w	0xdff01c,30(%a0)
.Lnodbg:
	lea	0xdff000,%a0
	move.w	0x1e(%a0),%d0
	and.w	0x1c(%a0),%d0
	btst	#4,%d0				/* COPER */
	beq.s	.Lsnocop
	tst.l	prof_on
	beq.s	.Lsclr
	move.l	14(%sp),%d1
	sub.l	prof_base,%d1
	cmp.l	prof_size,%d1
	bcc.s	.Lsout
	lsr.l	#4,%d1
	move.l	prof_hist,%a0
	addq.l	#1,(%a0,%d1.l*4)
	lea	0xdff000,%a0
	bra.s	.Lsclr
.Lsout:
	addq.l	#1,prof_other
.Lsclr:
	move.w	#0x0010,0x9c(%a0)
	move.w	#0x0010,0x9c(%a0)
.Lsnocop:
	btst	#6,%d0				/* BLIT: the next queued blit (blit.c) */
	beq.s	.Lsnoblit
	move.w	#0x0040,0x9c(%a0)
	move.w	#0x0040,0x9c(%a0)
	movem.l	%d0/%a0-%a1,-(%sp)
	jsr	blit_irq
	movem.l	(%sp)+,%d0/%a0-%a1
	lea	0xdff000,%a0
.Lsnoblit:
	btst	#5,%d0				/* VERTB */
	beq.s	.Lsdone
	addq.l	#1,vb_count
	move.w	0x4(%a0),%d1			/* the beam's line now (VPOSR's V8-V10, VHPOSR's V0-V7): where this vblank began */
	and.w	#7,%d1
	lsl.w	#8,%d1
	move.b	0x6(%a0),%d1
	move.w	%d1,vb_vpos
	move.w	#0x0020,0x9c(%a0)
	move.w	#0x0020,0x9c(%a0)
.Lsdone:
	movem.l	(%sp)+,%d0-%d1/%a0
	rte

/* the level 2 interrupt while the machine is taken (amiga/host/input.c): the keyboard */
	.globl	l2_handler
l2_handler:
	movem.l	%d0-%d1/%a0-%a1,-(%sp)
	move.w	0xdff01e,%d0
	btst	#3,%d0				/* PORTS */
	beq.s	.Ll2done
	jsr	input_l2
.Ll2done:
	movem.l	(%sp)+,%d0-%d1/%a0-%a1
	rte

/* D-2's pacing (amiga/host/pace.c): CIA-B timer A's level 6 interrupt counts a Genesis vblank and sets
 * the next period's latch, the base or one more by the fraction's carry, so that the mean is exact */
	.globl	pace_handler
pace_handler:
	movem.l	%d0-%d1,-(%sp)
	move.b	0xbfdd00,%d0			/* CIA-B ICR: reading it clears it */
	btst	#0,%d0
	beq.s	.Lpcnone
	addq.l	#1,pace_count
	move.w	pace_acc,%d0
	add.w	pace_frac,%d0
	scs	%d1
	move.w	%d0,pace_acc
	move.w	pace_base,%d0
	tst.b	%d1
	beq.s	.Lpcbase
	addq.w	#1,%d0
.Lpcbase:
	move.b	%d0,0xbfd400			/* TALO, then TAHI: the latch for the period after this one */
	lsr.w	#8,%d0
	move.b	%d0,0xbfd500
.Lpcnone:
	move.w	#0x2000,0xdff09c		/* EXTER, twice */
	move.w	#0x2000,0xdff09c
	movem.l	(%sp)+,%d0-%d1
	rte

	.globl	pace_nop
pace_nop:
	rts

	.section .note.GNU-stack,"",%progbits
