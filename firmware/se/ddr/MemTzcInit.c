/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The TZC-400 in front of each DDR channel (CIX
 * driver/ddr/src/tzc400/MemTzcInit.c), at SMN 0x0c0b0000 + channel *
 * 0x20000.  Translated from CIX's SE firmware debug build.
 */
#include "mem_types.h"

#define TZC400_SMN_BASE	0x0c0b0000

/* TZC-400 registers: region 0 (all of DRAM) open to secure and to every
 * non-secure ID; the AP's firmware sets up the other regions */
REG_VAL_PAIR Tzc400InitList[5] = {
	{ 0x004, 3 },		/* ACTION: error response and interrupt */
	{ 0x00c, 0 },		/* SPECULATION_CTRL */
	{ 0x110, 0xc0000000 },	/* REGION_ATTRIBUTES_0: secure read and write */
	{ 0x114, 0xffffffff },	/* REGION_ID_ACCESS_0: every NSAID, read and write */
	{ 0x008, 0xf },		/* GATE_KEEPER: open filters 0-3 */
};

/* Tzc400Init */
void Tzc400Init(void)
{
	uint32_t i;
	uint8_t Channel;

	for (i = 0; i < 5; i++) {
		for (Channel = 0; Channel < 4; Channel++) {
			if (MemChannelPopulated(Channel))
				SmnRegWrite32(TZC400_SMN_BASE + Tzc400InitList[i].offset + ((uint32_t)Channel << 17),
					      Tzc400InitList[i].value);
		}
	}
}
