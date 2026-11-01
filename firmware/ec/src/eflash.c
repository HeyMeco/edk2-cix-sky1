/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The IT5570E's e-flash (where the EC runs from) through the SMFI's
 * follow mode, ECINDAR3 0x40/0x4F, and the mirror reset.
 *
 * Programming the e-flash while running from it is not possible: as the
 * vendor firmware, the routines that erase and program run from scratch
 * ROM (SCAR0 maps them into SRAM) with interrupts off.
 */
#include "ec.h"

uint8_t eflash_read(uint32_t addr)
{
	ECINDAR3 = 0x40 | ((addr >> 24) & 0x0F);
	ECINDAR2 = addr >> 16;
	ECINDAR1 = addr >> 8;
	ECINDAR0 = addr;
	return ECINDDR;
}

/*
 * Erase the 1 KB sector holding addr and program one byte, as the vendor
 * does for its AC-loss flag (the rest of the sector is not kept: the
 * vendor's sector holds nothing else).  See scratch.asm.
 */
extern void fm_write_byte(void);
extern __xdata uint8_t fm_addr[3], fm_val, fm_erase;

static void run(uint32_t addr, uint8_t v, uint8_t erase)
{
	fm_erase = erase;
	fm_addr[0] = addr >> 16;
	fm_addr[1] = addr >> 8;
	fm_addr[2] = addr;
	fm_val = v;
	EA = 0;
	fm_write_byte();
	EA = 1;
	ECINDAR3 = 0;
	ECINDAR2 = 0;
}

/*
 * Erase (optionally) and program one byte.  As the vendor, the erase and
 * the program are separate runs from scratch ROM with interrupts back on
 * in between, so the host bus is not held for the whole erase.
 */
static void write1(uint32_t addr, uint8_t v, uint8_t erase)
{
	if (erase) {
		run(addr, 0, 1);
		delay_ms(12);
	}
	run(addr, v, 0);
}

void eflash_write_byte(uint32_t addr, uint8_t v)
{
	write1(addr, v, 1);
}

/* the 4-byte power-off reason record at 0x1FCFC: one erase, four bytes */
void eflash_write_reason(uint8_t r)
{
	write1(0x1FCFC, 0, 1);
	delay_ms(20);
	write1(0x1FCFD, 0, 0);
	delay_ms(20);
	write1(0x1FCFE, 0, 0);
	delay_ms(20);
	write1(0x1FCFF, r, 0);
}

/* reset the EC through the external watchdog with the mirror requested */
void mirror_reset(void)
{
	EA = 0;
	RSTS = 0x84;
	SMFI_1063 = 0x18;
	ETWCFG = 0x20;
	EWDKEYR = 0x5A;		/* a wrong key: immediate reset */
	for (;;)
		;
}
