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

	.section .note.GNU-stack,"",%progbits
