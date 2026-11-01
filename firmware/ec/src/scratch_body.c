/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The e-flash erase/program routine itself, compiled into its own code
 * segment (SCRCODE, linked at 0x7C00) so it can be run from scratch ROM:
 * see scratch.c.
 */
#include "ec.h"

extern __xdata uint8_t fm_addr[3];
extern __xdata uint8_t fm_val;
extern __xdata uint8_t fm_erase;
extern __xdata uint8_t fm_bf;


void fm_body(void) __naked
{
	__asm
	; ECINDAR3..0 = 4F FF FE 00
	mov	dptr,#0x103E
	mov	a,#0x4F
	movx	@dptr,a
	mov	dptr,#0x103D
	mov	a,#0xFF
	movx	@dptr,a
	mov	dptr,#0x103B
	clr	a
	movx	@dptr,a

	; ---- wait until not busy (RDSR bit 0)
	acall	fm_rdsr_idle

	; ---- unprotect: WREN, EWSR, WRSR 00
	acall	fm_unprotect

	; ---- if fm_erase: WREN, EWSR, wait WEL, 1 KB erase D7 a2 a1&FC 00,
	;      and return; else program (below)
	mov	dptr,#_fm_erase
	movx	a,@dptr
	jz	fm_no_erase
	mov	dptr,#_fm_bf
	mov	a,#1
	movx	@dptr,a
	acall	fm_wren_ewsr
	acall	fm_rdsr_wel
	acall	fm_cs_end
	mov	dptr,#_fm_addr
	movx	a,@dptr
	mov	r5,a
	inc	dptr
	movx	a,@dptr
	anl	a,#0xFC
	mov	r6,a
	acall	fm_cs_go
	mov	a,#0xD7
	movx	@dptr,a
	mov	a,r5
	movx	@dptr,a
	mov	a,r6
	movx	@dptr,a
	clr	a
	movx	@dptr,a
	acall	fm_rdsr_idle
	acall	fm_cs_end
	ret			; the erase is a call of its own (as the vendor)
fm_no_erase:

	; ---- WREN, EWSR, wait WEL, program 02 a2 a1 a0 val
	acall	fm_wren_ewsr
	acall	fm_rdsr_wel
	acall	fm_cs_end
	mov	dptr,#_fm_addr
	movx	a,@dptr
	mov	r5,a
	inc	dptr
	movx	a,@dptr
	mov	r6,a
	inc	dptr
	movx	a,@dptr
	mov	r4,a
	mov	dptr,#_fm_val
	movx	a,@dptr
	mov	r3,a
	acall	fm_cs_go
	mov	a,#0x02
	movx	@dptr,a
	mov	a,r5
	movx	@dptr,a
	mov	a,r6
	movx	@dptr,a
	mov	a,r4
	movx	@dptr,a
	mov	a,r3
	movx	@dptr,a
	acall	fm_rdsr_idle

	; ---- WRDI, wait WEL clear
	mov	r7,#0x04
	acall	fm_cmd1
fm_wrdi_wait:
	acall	fm_cs_end
	acall	fm_cs_go
	mov	a,#0x05
	movx	@dptr,a
	movx	a,@dptr
	anl	a,#0x03
	jnz	fm_wrdi_wait
	acall	fm_cs_end
	ret

	; WREN, EWSR, WRSR 00, wait idle
fm_unprotect:
	mov	dptr,#_fm_bf
	movx	a,@dptr
	jnz	fm_unp_bf
	mov	r7,#0x06
	acall	fm_cmd1
	sjmp	fm_unp_wrsr
fm_unp_bf:
	acall	fm_wren_ewsr
fm_unp_wrsr:
	acall	fm_cs_end
	acall	fm_cs_go
	mov	a,#0x01
	movx	@dptr,a
	clr	a
	movx	@dptr,a
	ajmp	fm_rdsr_idle

	; WREN, then EWSR (the e-flash needs it, as SST parts)
fm_wren_ewsr:
	mov	r7,#0x06
	acall	fm_cmd1
	mov	r7,#0x50
	ajmp	fm_cmd1

	; chip select released: ECINDAR1 = FE, ECINDDR = FF
fm_cs_end:
	mov	dptr,#0x103C
	mov	a,#0xFE
	movx	@dptr,a
	mov	dptr,#0x103F
	mov	a,#0xFF
	movx	@dptr,a
	ret

	; chip select active: ECINDAR1 = FD, dptr = ECINDDR
fm_cs_go:
	mov	dptr,#0x103C
	mov	a,#0xFD
	movx	@dptr,a
	mov	dptr,#0x103F
	ret

	; a one-byte command in r7
fm_cmd1:
	acall	fm_cs_end
	acall	fm_cs_go
	mov	a,r7
	movx	@dptr,a
	ret

	; RDSR until WIP (bit 0) clear
fm_rdsr_idle:
	acall	fm_cs_end
	acall	fm_cs_go
	mov	a,#0x05
	movx	@dptr,a
fm_ri_loop:
	movx	a,@dptr
	jb	acc.0,fm_ri_loop
	ret

	; RDSR until status[1:0] == 2 (WEL set, not busy)
fm_rdsr_wel:
	acall	fm_cs_end
	acall	fm_cs_go
	mov	a,#0x05
	movx	@dptr,a
fm_rw_loop:
	movx	a,@dptr
	anl	a,#0x03
	cjne	a,#0x02,fm_rw_loop
	ret
	__endasm;
}

