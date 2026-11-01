/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The OPP trace for the AP's performance stream (CIX's
 * Source/common/soc/dvfs/dvfs_opp.c): each level scmi_perf applies to a
 * domain the stream asked for (locked_perf's report) goes, with the AP's
 * counter, into a ring of 8; each tick takes what is there for the next
 * package.
 */
#include "dev_counters.h"
#include "perf.h"
#include "rtt/rtt.h"

/* CIX's struct dvfs_opp_event (16 bytes in the ring, 13 in a package) */
struct dvfs_opp_event {
	uint64_t system_cnt;
	uint32_t level;
	uint8_t domain;
	uint8_t pad[3];
};

static uint8_t opp_event_buf[128];
static struct dvfs_opp_event opp_tmp_data[8];
static bool opp_enabled;
static uint8_t opp_tmp_cnt;
static uint16_t domain_mask;
static int16_t lost_opp_event;
static uint8_t opp_rrq_id = 0xff;

void dvfs_opp_evt_stream_init(void)
{
	opp_rrq_id = u_rrq_setup(opp_event_buf, sizeof(opp_event_buf), true, U_RRQ_E_RTOS_SEMPH);
	if (opp_rrq_id == 0xff)
		__builtin_trap();	/* CIX: vAssertCalledWithTRC(0xf1670019) */
}

void dvfs_opp_setup_cntr(uint16_t _domain_mask)
{
	opp_enabled = true;
	if (!domain_mask)
		u_rrq_reset(opp_rrq_id);
	domain_mask |= _domain_mask;
}

void dvfs_opp_clear_all_cntr(void)
{
	domain_mask = 0;
	opp_enabled = false;
}

void dvfs_opp_fetch_snapshot(void)
{
	while (opp_rrq_id != 0xff && opp_tmp_cnt < 8) {
		uint32_t n = u_rrq_get(opp_rrq_id, &opp_tmp_data[opp_tmp_cnt], 16);

		if (n == 16) {
			opp_tmp_cnt++;
			continue;
		}
		if (n)
			__builtin_trap();	/* CIX: 0xf1670049 */
		break;
	}
}

void dvfs_opp_free_snapshot(void)
{
	opp_tmp_cnt = 0;
}

uint8_t dvfs_opp_item_cnt(void)
{
	return opp_tmp_cnt;
}

const void *dvfs_opp_get_item(uint8_t idx)
{
	if (idx >= 8)
		__builtin_trap();	/* CIX: 0xf1670068 */
	return &opp_tmp_data[idx];
}

void dvfs_opp_put_event(uint8_t domain, uint32_t level)
{
	struct dvfs_opp_event ev = { 0 };

	if (opp_rrq_id == 0xff || !opp_enabled || !(domain_mask & BIT(domain & 31)))
		return;
	ev.system_cnt = dev_cnts_get_ap_free_counter();
	ev.level = level;
	ev.domain = domain;
	if (u_rrq_put(opp_rrq_id, &ev, 16) != 16)
		lost_opp_event++;
}
