/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* CRC-32 (IEEE 802.3, reflected, as zlib) */
#include "se.h"
#include "crypto.h"

uint32_t crc32(const uint8_t *buf, uint32_t len)
{
	uint32_t crc = 0xffffffff;
	int k;

	while (len--) {
		crc ^= *buf++;
		for (k = 0; k < 8; k++)
			crc = crc & 1 ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
	}
	return ~crc;
}
