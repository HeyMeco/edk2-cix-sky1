/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CSU_PM memory map (PM addresses; the PM SRAM is SoC 0x06a10000).  The
 * regions are those of CIX's image: code in IRAM, read-only data and data
 * in DRAM0, bss and the stack in DRAM1 below the areas other agents use
 * (eFuse copy, scratch, BIOS configuration).  The SE loads the image's
 * sections from bootloader1 (scripts/open/xbin.py) and starts the PM at
 * the reset vector, 0x20000000.
 */
OUTPUT_ARCH(xtensa)
ENTRY(_ResetVector)

MEMORY
{
	iram  (rx)  : ORIGIN = 0x20000000, LENGTH = 0x30000
	dram0 (rw)  : ORIGIN = 0x20040000, LENGTH = 0x10000
	dram1 (rw)  : ORIGIN = 0x20080000, LENGTH = 0x3e700
	efuse (rw)  : ORIGIN = 0x200be700, LENGTH = 0x10
}

VECBASE = 0x20000400;

SECTIONS
{
	.ResetVector.text 0x20000000 : { KEEP(*(.ResetVector.text)) } > iram

	.WindowVectors.text VECBASE : { KEEP(*(.WindowVectors.text)) } > iram
	.Level2InterruptVector.text VECBASE + 0x17c : { KEEP(*(.Level2InterruptVector.text)) } > iram
	.Level3InterruptVector.text VECBASE + 0x19c : { KEEP(*(.Level3InterruptVector.text)) } > iram
	.Level4InterruptVector.text VECBASE + 0x1bc : { KEEP(*(.Level4InterruptVector.text)) } > iram
	.Level5InterruptVector.text VECBASE + 0x1dc : { KEEP(*(.Level5InterruptVector.text)) } > iram
	.DebugExceptionVector.text VECBASE + 0x1fc : { KEEP(*(.DebugExceptionVector.text)) } > iram
	.NMIExceptionVector.text VECBASE + 0x21c : { KEEP(*(.NMIExceptionVector.text)) } > iram
	.KernelExceptionVector.text VECBASE + 0x23c : { KEEP(*(.KernelExceptionVector.text)) } > iram
	.UserExceptionVector.text VECBASE + 0x25c : { KEEP(*(.UserExceptionVector.text)) } > iram
	.DoubleExceptionVector.text VECBASE + 0x27c : { KEEP(*(.DoubleExceptionVector.text)) } > iram

	/* literals before the code that loads them (L32R reaches backwards) */
	.text VECBASE + 0x2a0 : {
		*(.ResetHandler.literal .literal .literal.*)
		*(.ResetHandler.text .text .text.*)
	} > iram

	.rodata : {
		*(.rodata .rodata.*)
		. = ALIGN(4);
	} > dram0

	/* constructors (SCP-firmware's FWK_CONSTRUCTOR), run by main */
	.init_array : {
		__init_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
		KEEP(*(.init_array .ctors))
		__init_array_end = .;
	} > dram0

	.data : {
		*(.data .data.*)
		. = ALIGN(4);
	} > dram0

	.bss (NOLOAD) : {
		_bss_start = .;
		*(.bss .bss.* COMMON)
		. = ALIGN(16);
		_bss_end = .;
	} > dram1

	_stack_top = ORIGIN(dram1) + LENGTH(dram1);
	_stack_bottom = _stack_top - 0x4000;
	ASSERT(_bss_end <= _stack_bottom, "bss overlaps the stack")

	/* the heap (lib/malloc.c) is the rest of DRAM1 */
	_heap_start = _bss_end;
	_heap_end = _stack_bottom;
	/*
	 * ddr_s3_enter borrows PM RAM from 0x237b4 bytes below the stack
	 * pointer (src/mclp/mclp_retention.c); that must be heap at the
	 * stack's deepest, or the blocks would be pushed over the stack
	 */
	ASSERT(_heap_start + 0x237b4 <= _stack_bottom, "heap too small for DDR retention")

	/* the eFuse copy's default (src/pvt.c); the SE overwrites it */
	.dram1.efuse 0x200be700 : { KEEP(*(.dram1.efuse)) } > efuse

	/* Xtensa property tables (instruction ranges, literals), not loaded */
	.xt.lit 0 : { *(.xt.lit .xt.lit.*) }
	.xt.prop 0 : { *(.xt.prop .xt.prop.*) }

	/DISCARD/ : { *(.comment) *(.note.*) }
}
