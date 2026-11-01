/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PMIC event stream (CIX's Source/common/soc/pmic/pmic_event.c): each
 * voltage a rail's regulator is set to (dev_pmic_setVoltage), with the
 * AP's free-running counter, in a ring of eight; the performance streams
 * the AP turns on (CIX's act_streams.c) take a snapshot and send it.
 */
#include "lcru.h"
#include "pmic.h"
#include "rtt/rtt.h"

#define PMIC_EVENTS		8

static uint8_t pmic_rrq_id = U_RRQ_INVALID;
static uint8_t pmic_event_buf[PMIC_EVENTS * sizeof(struct pmic_event_item)];
static struct pmic_event_item pmic_tmp_data[PMIC_EVENTS];
static bool pmic_event_enabled;
static uint8_t pmic_tmp_cnt;
static int16_t lost_pmic_event;

void pmic_event_stream_init(void)
{
	if (pmic_rrq_id != U_RRQ_INVALID)
		return;
	pmic_rrq_id = u_rrq_setup(pmic_event_buf, sizeof(pmic_event_buf), true, U_RRQ_E_RTOS_SEMPH);
}

void pmic_event_stream_setup(void)
{
	pmic_event_enabled = true;
	u_rrq_reset(pmic_rrq_id);
}

void pmic_event_stream_clear(void)
{
	pmic_event_enabled = false;
}

/* what the ring has, up to eight events, out of it */
void pmic_event_fetch_snapshot(void)
{
	while (pmic_rrq_id != U_RRQ_INVALID && pmic_tmp_cnt < PMIC_EVENTS) {
		if (u_rrq_get(pmic_rrq_id, &pmic_tmp_data[pmic_tmp_cnt],
			      sizeof(pmic_tmp_data[0])) != sizeof(pmic_tmp_data[0]))
			break;
		pmic_tmp_cnt++;
	}
}

void pmic_event_free_snapshot(void)
{
	pmic_tmp_cnt = 0;
}

uint8_t pmic_event_item_cnt(void)
{
	return pmic_tmp_cnt;
}

const struct pmic_event_item *pmic_event_get_item(uint8_t idx)
{
	return idx < PMIC_EVENTS ? &pmic_tmp_data[idx] : NULL;
}

void pmic_event_put(uint8_t i2c_port, uint8_t buck, uint16_t voltage)
{
	struct pmic_event_item e;

	if (pmic_rrq_id == U_RRQ_INVALID || !pmic_event_enabled)
		return;
	e.timestamp = dev_cnts_get_ap_free_counter();
	e.i2c_port = i2c_port;
	e.buck = buck;
	e.voltage = voltage;
	e.reserved = 0;
	if (u_rrq_put(pmic_rrq_id, &e, sizeof(e)) != sizeof(e))
		lost_pmic_event++;
}
