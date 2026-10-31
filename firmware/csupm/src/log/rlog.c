/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The RAM log (CIX's Source/common/logging/rlog.c): 8 KB of DRAM at
 * 0x83db8000 through a locked hub window; a ring of 0x1f9c bytes of
 * records, the last 100 bytes zero.  The previous boot's log is copied to
 * 0x83dba000 first.  A record:
 *   {"level":"INFO","time":"0x<AP counter high, low>","log":"<message>\n};
 * (no closing quote, as CIX writes it).  Written only once the RTT block
 * is there (fwlog_enable_dram(false, true)).
 */
#include <stdio.h>

#include "lcru.h"
#include "log.h"
#include "rtt/rtt.h"

#define RLOG_SIZE		0x2000u
#define RLOG_RING		0x1f9cu		/* then 100 bytes of zeros */
#define RLOG_LAST_BOOT		(RLOG_DRAM_BASE + RLOG_SIZE)

static struct {
	bool rlog_init_flag;
	char *base;
	uint32_t pos;
} rlog;

/* CIX's ignores base_addr: the log is at RLOG_DRAM_BASE */
void rlog_init(uint32_t base_addr)
{
	char *base;

	(void)base_addr;
	if (rlog.rlog_init_flag)
		return;
	if (!fwlog_is_rt_dram_enabled())
		vAssertCalledWithTRC(0xf1480021);
	base = (char *)(uintptr_t)tlb_ctrl_dram_to_syshub(RLOG_DRAM_BASE, false);
	tlb_ctrl_lock_entry(tlb_ctrl_get_entry_idx_by_dram_base(RLOG_DRAM_BASE, false));
	memcpy((void *)(uintptr_t)tlb_ctrl_dram_to_syshub(RLOG_LAST_BOOT, false), base, RLOG_SIZE);
	memset(base + RLOG_RING, 0, RLOG_SIZE - RLOG_RING);
	rlog.base = base;
	rlog.pos = 0;
	rlog.rlog_init_flag = true;
}

static void rlog_write_buffer(const char *message, size_t length)
{
	if (rlog.pos + length > RLOG_RING) {
		uint32_t first = RLOG_RING - rlog.pos;

		memcpy(rlog.base + rlog.pos, message, first);
		memcpy(rlog.base, message + first, length - first);
	} else {
		memcpy(rlog.base + rlog.pos, message, length);
	}
	rlog.pos += length;
	if (rlog.pos >= RLOG_RING)
		rlog.pos -= RLOG_RING;
}

/* The RAM log's name of a level's bits */
static const char *level_name(uint32_t level)
{
	if (level & BIT(0))
		return "WARN";
	if (level & BIT(1))
		return "ERR";
	if (level & BIT(2))
		return "WARN";
	if (level & (BIT(3) | BIT(7)))
		return "INFO";
	return "UNK";
}

void rlog_write(uint32_t level, const char *message, size_t length)
{
	char hdr[64];
	uint64_t t;
	int n;

	if (!fwlog_is_rt_dram_enabled() || !rlog.rlog_init_flag)
		return;
	if (length > RLOG_RING)
		vAssertCalledWithTRC(0xf1480079);
	t = dev_cnts_get_ap_free_counter();
	n = snprintf(hdr, sizeof(hdr), "{\"level\":\"%s\",\"time\":\"0x%08x%08x\",\"log\":\"",
		     level_name(level), (unsigned int)(t >> 32), (unsigned int)t);
	rlog_write_buffer(hdr, (size_t)n);
	rlog_write_buffer(message, length);
	rlog_write_buffer("\n};", 3);
}
