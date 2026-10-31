/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SE SRAM at 0x20000000.  The boot ROM loads the image at its start and
 * keeps its hand-over data (flash parameters, boot information) at the top
 * from 0x20070000, which the image must not reach.
 */
MEMORY
{
	SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x70000
}

ENTRY(Reset_Handler)

SECTIONS
{
	.text : {
		KEEP(*(.vectors))
		*(.text .text.*)
		*(.rodata .rodata.*)
		. = ALIGN(4);
	} > SRAM
	.data : {
		__data_start = .;
		*(.data .data.*)
		. = ALIGN(4);
		__data_end = .;
	} > SRAM
	.bss (NOLOAD) : ALIGN(8) {
		__bss_start = .;
		*(.bss .bss.* COMMON)
		. = ALIGN(8);
		__bss_end = .;
	} > SRAM
	.heap (NOLOAD) : ALIGN(8) {
		*(.heap)
	} > SRAM
	.stack (NOLOAD) : ALIGN(8) {
		__stack_limit = .;
		. += 0x4000;
		__stack_top = .;
	} > SRAM
	/DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
