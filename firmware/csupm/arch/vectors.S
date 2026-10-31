/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Reset, register-window and exception vectors of CSU_PM (Xtensa LX7,
 * windowed ABI, XEA2).  VECBASE is 0x20000400; pm.ld places each vector
 * at its offset.
 *
 * Interrupts are handled later, not in the vector: the handler masks the
 * lines that are pending (INTENABLE), adds them to arch_irq_pending and
 * returns; the main loop (scp/src/arch_interrupt.c) runs their handlers
 * with interrupts masked and unmasks the lines.  The handlers here touch
 * nothing but a few registers they save, so they need neither a stack nor
 * a register window.  Every other exception ends in arch_panic.
 */

#include "irq.h"

#define PS_UM		(1 << 5)
#define PS_WOE		(1 << 18)

/* --- reset ---------------------------------------------------------------- */

	.section .ResetVector.text, "ax"
	.global	_ResetVector
_ResetVector:
	j	_ResetHandler

	.section .ResetHandler.text, "ax"
	.literal .Lvecbase, VECBASE
	.literal .Lstack, _stack_top
	.literal .Lbss_start, _bss_start
	.literal .Lbss_end, _bss_end
	.literal .Lps, PS_UM | PS_WOE

	.align	4
	.global	_ResetHandler
	.type	_ResetHandler, @function
_ResetHandler:
	movi	a0, 0
	wsr.intenable a0
	wsr.windowbase a0
	rsync
	movi	a0, 1
	wsr.windowstart a0
	rsync
	l32r	a2, .Lvecbase
	wsr.vecbase a2
	movi	a2, 1
	wsr.cpenable a2			/* coprocessor 0, the FPU */
	rsync
	l32r	a1, .Lstack
	l32r	a2, .Lbss_start
	l32r	a3, .Lbss_end
	movi	a0, 0
1:	bgeu	a2, a3, 2f
	s32i	a0, a2, 0
	addi	a2, a2, 4
	j	1b
2:	l32r	a2, .Lps		/* user mode, window overflow on, EXCM off */
	wsr.ps	a2
	rsync
	call4	main
3:	j	3b
	.size	_ResetHandler, . - _ResetHandler

/* --- register windows (offsets fixed by the architecture) ------------------ */

	.section .WindowVectors.text, "ax"
	.global	_WindowOverflow4
_WindowOverflow4:
	s32e	a0, a5, -16
	s32e	a1, a5, -12
	s32e	a2, a5, -8
	s32e	a3, a5, -4
	rfwo

	.org	0x40
	.global	_WindowUnderflow4
_WindowUnderflow4:
	l32e	a0, a5, -16
	l32e	a1, a5, -12
	l32e	a2, a5, -8
	l32e	a3, a5, -4
	rfwu

	.org	0x80
	.global	_WindowOverflow8
_WindowOverflow8:
	s32e	a0, a9, -16
	l32e	a0, a1, -12		/* the caller's sp: its frame's extra save area */
	s32e	a1, a9, -12
	s32e	a2, a9, -8
	s32e	a3, a9, -4
	s32e	a4, a0, -32
	s32e	a5, a0, -28
	s32e	a6, a0, -24
	s32e	a7, a0, -20
	rfwo

	.org	0xc0
	.global	_WindowUnderflow8
_WindowUnderflow8:
	l32e	a1, a9, -12
	l32e	a0, a9, -16
	l32e	a7, a1, -12
	l32e	a2, a9, -8
	l32e	a4, a7, -32
	l32e	a3, a9, -4
	l32e	a5, a7, -28
	l32e	a6, a7, -24
	l32e	a7, a7, -20
	rfwu

	.org	0x100
	.global	_WindowOverflow12
_WindowOverflow12:
	s32e	a0, a13, -16
	l32e	a0, a1, -12
	s32e	a1, a13, -12
	s32e	a2, a13, -8
	s32e	a3, a13, -4
	s32e	a4, a0, -48
	s32e	a5, a0, -44
	s32e	a6, a0, -40
	s32e	a7, a0, -36
	s32e	a8, a0, -32
	s32e	a9, a0, -28
	s32e	a10, a0, -24
	s32e	a11, a0, -20
	rfwo

	.org	0x140
	.global	_WindowUnderflow12
_WindowUnderflow12:
	l32e	a1, a13, -12
	l32e	a0, a13, -16
	l32e	a11, a1, -12
	l32e	a2, a13, -8
	l32e	a4, a11, -48
	l32e	a8, a11, -32
	l32e	a3, a13, -4
	l32e	a5, a11, -44
	l32e	a6, a11, -40
	l32e	a7, a11, -36
	l32e	a9, a11, -28
	l32e	a10, a11, -24
	l32e	a11, a11, -20
	rfwu

/* --- other vectors ---------------------------------------------------------- */

	.macro	vector name, excsave, target=arch_panic
	.section .\name\().text, "ax"
	.global	_\name
_\name:
	wsr.\excsave a0
	j	\target
	.endm

	vector	Level2InterruptVector, excsave2, _irq_level2
	vector	Level3InterruptVector, excsave3, _irq_level3
	vector	Level4InterruptVector, excsave4, _irq_level4
	vector	Level5InterruptVector, excsave5, _irq_level5
	vector	DebugExceptionVector, excsave6
	vector	NMIExceptionVector, excsave7
	vector	KernelExceptionVector, excsave1
	vector	UserExceptionVector, excsave1, _user_exception
	vector	DoubleExceptionVector, excsave1

/* --- interrupts --------------------------------------------------------------- */

	.data
	.align	4
_irq_save:
	.space	4 * 3 * 6		/* a2-a4, per level 1-5 */
	.global	arch_irq_pending
arch_irq_pending:
	.word	0

/*
 * Mask the pending lines of this level, add them to arch_irq_pending.
 * Entered with a0 in EXCSAVE<level>.  Software and edge interrupts are
 * cleared here (INTCLEAR ignores the others); a level-sensitive line stays
 * masked until its handler has run.
 */
	.macro	defer level, mask
	movi	a0, _irq_save + 12 * \level
	s32i	a2, a0, 0
	s32i	a3, a0, 4
	s32i	a4, a0, 8
	rsr.interrupt a2
	rsr.intenable a3
	and	a2, a2, a3
	movi	a4, \mask
	and	a2, a2, a4
	xor	a3, a3, a2
	wsr.intenable a3
	wsr.intclear a2
	movi	a3, arch_irq_pending
	l32i	a4, a3, 0
	or	a4, a4, a2
	s32i	a4, a3, 0
	l32i	a4, a0, 8
	l32i	a3, a0, 4
	l32i	a2, a0, 0
	rsr.excsave\level a0
	rsync
	.endm

	.text
	.literal_position
	.align	4
_user_exception:
	rsr.exccause a0
	bnei	a0, EXCCAUSE_LEVEL1_INTERRUPT, 1f
	defer	1, IRQ_LEVEL1_MASK
	rfe
1:	rsr.excsave1 a0
	j	arch_panic

	.macro	level n, mask
	.align	4
_irq_level\n:
	defer	\n, \mask
	rfi	\n
	.endm

	level	2, IRQ_LEVEL2_MASK
	level	3, IRQ_LEVEL3_MASK
	level	4, IRQ_LEVEL4_MASK
	level	5, IRQ_LEVEL5_MASK

/* An exception nothing handles: stop here (EXCCAUSE, EPC1 and EXCVADDR,
 * or the level's EPC, tell why). */
	.text
	.align	4
	.global	arch_panic
	.type	arch_panic, @function
arch_panic:
	waiti	15
	j	arch_panic
	.size	arch_panic, . - arch_panic
