; SPDX-License-Identifier: BSD-2-Clause-Patent
; Reset and interrupt vectors, and ITE's e-flash signature at 0x40 (the
; boot ROM looks for it at a 16-byte aligned offset in 0x40-0xF0; the
; vendor image has it here, with these bytes: LPC host bus, flags 0x95,
; byte 13 0x7D).  Everything else is linked from 0x50.
	.module	vectors
	.globl	_timer0_isr
	.globl	_intc_isr

	.area	VECT	(ABS,CODE)
	.org	0x0000
	ljmp	0x0050
	.org	0x000B
	ljmp	_timer0_isr
	.org	0x0013
	ljmp	_intc_isr
	.org	0x0040
	.db	0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0x95
	.db	0x85, 0x12, 0x5A, 0x5A, 0xAA, 0x7D, 0x55, 0x55
