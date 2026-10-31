/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SFH SRAM: 512 KiB at 0x20000000; the SE loads the image at its start */
MEMORY
{
	SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
}

ENTRY(reset_handler)

SECTIONS
{
	.text : {
		KEEP(*(.vectors))
		*(.text .text.*)
		*(.rodata .rodata.*)
	} > SRAM
	.data : {
		*(.data .data.*)
	} > SRAM
	.bss (NOLOAD) : ALIGN(4) {
		__bss_start = .;
		*(.bss .bss.* COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > SRAM
	/* main stack below the process stack the firmware sets up */
	__stack_top = 0x2007fff0;
	/DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
