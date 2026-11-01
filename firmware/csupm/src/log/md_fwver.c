/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The firmware's version strings and their copy in DRAM for the AP (CIX's
 * Source/common/modules/md_fwver.c).  CIX's image has one blob, patched
 * after its build, cut into seven strings: "MINI_PORT_" "00.10.002" "G"
 * "260304" "171415" "cix_trunk" "c19c00b9db2d".  Ours have the same roles:
 * product, version (CSUPM_VERSION in the Makefile), "G", the commit's date
 * and time, branch, commit (the Makefile's, from git).
 *
 * md_version_broadcast puts "<version>-<commit>\0<date>-<time>" at DRAM
 * 0x83e01180 and the board's PMIC layout at 0x83e01480.
 */
#include "fwver.h"
#include "log.h"
#include "pmic.h"
#include "plat.h"
#include "rtt/rtt.h"

#define FWVER_DRAM		0x83e01180u
#define FWVER_PMIC_TYPE		0x300u		/* word offset of the PMIC layout */
#define FWVER_MAX		64

#define FW_STRINGS		7

static const char *const _s_u8FwStrTab[FW_STRINGS] = {
	"OPEN_PORT_", CSUPM_VERSION, "G", CSUPM_GIT_DATE, CSUPM_GIT_TIME, "open", CSUPM_GIT_HASH,
};
static const uint8_t _s_u8FwStrLength[FW_STRINGS] = {
	sizeof("OPEN_PORT_") - 1, sizeof(CSUPM_VERSION) - 1, 1, sizeof(CSUPM_GIT_DATE) - 1,
	sizeof(CSUPM_GIT_TIME) - 1, sizeof("open") - 1, sizeof(CSUPM_GIT_HASH) - 1,
};

/* String u8String into pBuf if it fits in length; its length, 0 if not.
 * CIX's ends it with a zero one past its end (pBuf[n + 1]), pBuf[n] left
 * as it was; the callers below write that byte. */
uint8_t md_version_stringGet(uint8_t u8String, uint8_t *pBuf, uint8_t length)
{
	uint8_t n;

	if (u8String >= FW_STRINGS || pBuf == NULL)
		return 0;
	n = _s_u8FwStrLength[u8String];
	if (n >= length)
		return 0;
	memcpy(pBuf, _s_u8FwStrTab[u8String], n);
	/*
	 * CIX's: the terminator one byte further than the string's end
	 * (pBuf[n] is left as it was; with n == length - 1 this writes
	 * pBuf[length]).  md_version_broadcast's strings stay well inside its
	 * buffer (checked at build time there).
	 */
	pBuf[n + 1] = 0;
	return n;
}

uint8_t md_version_lengthGet(uint8_t u8String)
{
	return u8String < FW_STRINGS ? _s_u8FwStrLength[u8String] : 0;
}

/* Not NUL-terminated: md_version_lengthGet has the length */
const uint8_t *md_version_stringPtr(uint8_t u8String)
{
	return u8String < FW_STRINGS ? (const uint8_t *)_s_u8FwStrTab[u8String] : NULL;
}

/* version-commit, NUL, date-time and the stray terminator fit the buffer */
_Static_assert(sizeof(CSUPM_VERSION) + sizeof(CSUPM_GIT_HASH) + sizeof(CSUPM_GIT_DATE) +
	       sizeof(CSUPM_GIT_TIME) + 1 < FWVER_MAX, "CSUPM_VERSION too long");

/* On parts whose strap word 1 has bit 5 */
void md_version_broadcast(void)
{
	uint8_t buf[FWVER_MAX];
	uint32_t base, pos, n;

	if (!(plat_id_4 & BIT(5)))
		return;
	base = tlb_ctrl_dram_to_syshub(FWVER_DRAM, false);
	if (base == 0) {
		fwlog_write(FWLOG_WARN, "!!PM VERSION CPY ERROR!");
		return;
	}
	n = md_version_stringGet(1, buf, FWVER_MAX);
	buf[n] = '-';
	pos = n + 1;
	n = md_version_stringGet(6, buf + pos, (uint8_t)(FWVER_MAX - 1 - (pos - 1)));
	pos += n;
	buf[pos] = 0;				/* between commit and date */
	pos++;
	n = md_version_stringGet(3, buf + pos, (uint8_t)(FWVER_MAX - 1 - (pos - 1)));
	pos += n;
	buf[pos] = '-';
	pos++;
	n = md_version_stringGet(4, buf + pos, (uint8_t)(FWVER_MAX - 1 - (pos - 1)));
	pos += n;
	buf[pos] = 0;
	if (pos >= FWVER_MAX)
		vAssertCalledWithTRC(0xf14e0222);
	memcpy((void *)(uintptr_t)base, buf, pos);
	REG32(base + FWVER_PMIC_TYPE) = dpm_edp_cfg_get_pmic_topology_id();	/* CIX's pmic_type_id */
}
