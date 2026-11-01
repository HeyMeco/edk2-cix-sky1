/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The task switch of the CSU_PM port (portmacro.h says why a call
 * boundary is enough).
 *
 * port_switch: every register window older than this one is spilled to
 * its frame by a chain of call12s deep enough to go round the 64
 * registers (the window overflow handlers in arch/vectors.S store each
 * frame where the ABI says), so all of the task is in memory: its stack,
 * this frame's return address (saved at sp + 0) and the stack pointer
 * (saved in the TCB).  The next task's stack pointer and return address
 * are then loaded and every window but this one marked free: RETW then
 * takes window underflows that reload the new task's frames from its
 * stack, as if it returned from its own port_switch.
 *
 * A new task's stack (pxPortInitialiseStack) looks like one that called
 * port_switch from _port_task_tramp with call4: the trampoline's a0-a3
 * (no return address, its stack pointer, the parameter, the function) in
 * the base save area below the frame.
 *
 * Interrupts are off throughout (vPortYield, port_start_first): window
 * exceptions are still taken with PS.INTLEVEL raised, and the vectors
 * never touch a task's state.
 */

	.literal_position

/* call12 a2 more times: each rotates the windows by 12 registers */
	.text
	.align	4
	.type	_port_spill, @function
_port_spill:
	entry	a1, 48			/* 16 base save area, 32 extra (a call12 caller) */
	beqz	a2, 1f
	addi	a14, a2, -1
	call12	_port_spill
1:	retw
	.size	_port_spill, . - _port_spill

	.align	4
	.global	port_switch
	.type	port_switch, @function
port_switch:
	entry	a1, 64			/* 48 as _port_spill, our return address at sp + 0 */
	movi	a14, 6			/* 7 call12s: 84 registers */
	call12	_port_spill
	s32i	a0, a1, 0
	movi	a2, pxCurrentTCB
	l32i	a2, a2, 0
	s32i	a1, a2, 0		/* pxTopOfStack */
	call8	vTaskSwitchContext
.Lresume:
	movi	a2, pxCurrentTCB
	l32i	a2, a2, 0
	l32i	a1, a2, 0
	l32i	a0, a1, 0
	rsr.windowbase a2
	movi	a3, 1
	ssl	a2
	sll	a3, a3
	wsr.windowstart a3
	rsync
	retw
	.size	port_switch, . - port_switch

/* the first task, from xPortStartScheduler: this stack is abandoned */
	.align	4
	.global	port_start_first
	.type	port_start_first, @function
port_start_first:
	entry	a1, 32
	rsil	a2, 15
	j	.Lresume
	.size	port_start_first, . - port_start_first

/*
 * Where a new task's RETW lands: a2 the parameter, a3 the function, as
 * the base save area gave them; port_task_start(function, parameter)
 * never returns.
 */
	.align	4
	.global	_port_task_tramp
	.type	_port_task_tramp, @function
_port_task_tramp:
	mov	a6, a3
	mov	a7, a2
	call4	port_task_start
1:	j	1b
	.size	_port_task_tramp, . - _port_task_tramp
