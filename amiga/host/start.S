/* The program's entry (the first byte of the first code hunk, as AmigaDOS LoadSeg runs it), the
 * library call gate, the VBR read and the level 3 interrupt. GNU as, 68020, Motorola operand order. */

	.section .text.start,"ax"
	.globl	_start
_start:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	jsr	amiga_main
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts				/* d0: the return code */

	.text
/* ULONG lib_call(void *base, LONG lvo, const ULONG *r): r = d0, d1, d2, d3, a0, a1, a2, a3 */
	.globl	lib_call
lib_call:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)	/* 11 registers: 44 bytes */
	move.l	48(%sp),%a6
	move.l	52(%sp),%d7
	move.l	56(%sp),%a5
	movem.l	(%a5),%d0-%d3/%a0-%a3
	jsr	0(%a6,%d7.l)
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

/* ULONG get_vbr(void): exec Supervisor(a5 = code ending in rte) */
	.globl	get_vbr
get_vbr:
	movem.l	%a5-%a6,-(%sp)
	move.l	4.w,%a6
	lea	.Lsuper(%pc),%a5
	jsr	-30(%a6)
	movem.l	(%sp)+,%a5-%a6
	rts
.Lsuper:
	movec	%vbr,%d0
	rte

/* the level 3 interrupt: on vertical blank, vb_count + 1 and vb_hook (a C function), then the
 * request cleared (twice: the 68040/060's write buffer may lag one write) */
	.globl	l3_handler
l3_handler:
	movem.l	%d0-%d1/%a0-%a1,-(%sp)
	lea	0xdff000,%a0
	move.w	0x1e(%a0),%d0		/* INTREQR */
	and.w	0x1c(%a0),%d0		/* & INTENAR */
	btst	#5,%d0			/* VERTB */
	beq.s	.Lnot_vb
	addq.l	#1,vb_count
	move.l	vb_hook,%d1
	beq.s	.Lno_hook
	move.l	%d1,%a1
	jsr	(%a1)
.Lno_hook:
	lea	0xdff000,%a0
	move.w	#0x0020,0x9c(%a0)
	move.w	#0x0020,0x9c(%a0)
.Lnot_vb:
	movem.l	(%sp)+,%d0-%d1/%a0-%a1
	rte

/* setjmp / longjmp for the game's C (src/dispatch.c run68k, restart_at): d2-d7, a2-a7 and the
 * return address, 52 bytes of glibc's 288-byte m68k jmp_buf */
	.globl	_setjmp
	.globl	setjmp
_setjmp:
setjmp:
	move.l	4(%sp),%a0
	move.l	(%sp),48(%a0)
	movem.l	%d2-%d7/%a2-%a7,(%a0)
	moveq	#0,%d0
	rts
	.globl	longjmp
longjmp:
	move.l	4(%sp),%a0
	move.l	8(%sp),%d0
	bne.s	.Lnz
	moveq	#1,%d0
.Lnz:
	movem.l	(%a0),%d2-%d7/%a2-%a7
	move.l	48(%a0),(%sp)
	rts

/* void call_with_stack(void (*fn)(void), void *lower, void *upper): fn on a stack of our own,
 * through exec StackSwap() (the Shell's default stack is 4 KB) */
	.globl	call_with_stack
call_with_stack:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a2
	lea	sss,%a0
	move.l	52(%sp),(%a0)		/* stk_Lower */
	move.l	56(%sp),4(%a0)		/* stk_Upper */
	move.l	56(%sp),8(%a0)		/* stk_Pointer */
	move.l	4.w,%a6
	jsr	-732(%a6)		/* StackSwap: now on the new stack */
	jsr	(%a2)
	lea	sss,%a0
	move.l	4.w,%a6
	jsr	-732(%a6)		/* and back */
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

	.section .keep,"aw"
	.even
sss:	.space	12

	.bss
	.even
	.globl	vb_count
	.globl	vb_hook
vb_count: .space 4
vb_hook:  .space 4

	.section .note.GNU-stack,"",%progbits
