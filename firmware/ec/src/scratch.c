/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Erase-and-program of one e-flash byte, run from scratch ROM.
 *
 * The EC fetches its code from the e-flash, which cannot be read while it
 * is being erased or programmed, so this routine (one self-contained
 * block in segment SCRCODE, linked at 0x7C00) is copied into scratch SRAM
 * by mapping SCAR0 onto it, as the vendor firmware maps its own flash
 * routines.  It calls nothing and reads only XDATA.
 *
 * Follow mode on the e-flash (ECINDAR3 0x4F, ECINDAR 0x4FFFFExx releases
 * the chip select, 0x4FFFFDxx shifts a byte), the vendor's sequence:
 *   wait idle; unprotect (WREN, EWSR, WRSR 0); optionally a 1 KB erase
 *   (WREN, EWSR, D7) and unprotect again; WREN, EWSR, byte program (02);
 *   WRDI.
 */
#include "ec.h"

__xdata uint8_t fm_addr[3];
__xdata uint8_t fm_val;
__xdata uint8_t fm_erase;
__xdata uint8_t fm_bf;		/* EWSR known needed: set by the first erase */

void fm_body(void) __naked;


/* map the routine into scratch ROM (SCAR0 = its flash address) and run it */
void fm_write_byte(void)
{
	XREG(0x1042) = 0x80;
	XREG(0x1041) = 0x7C;
	XREG(0x1040) = 0x00;
	XREG(0x1042) = 0x00;
	fm_body();
	XREG(0x1042) = 0x80;
}
