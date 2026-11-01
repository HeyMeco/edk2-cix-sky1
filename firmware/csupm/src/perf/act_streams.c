/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Performance streams (CIX's Source/common/soc/activities/act_streams.c).
 *
 * Up to 20 streams, each of a type (perf.h) with an interval in coarse
 * ticks.  A type is normal, exclusive (one stream) or shared (one stream
 * whose configuration accumulates); its counters are set up when the
 * first stream of it is created and given back when its last is
 * released.  The coarse tick (1 ms; CIX's md_timer, here the timer
 * module's alarm in scp/module/dpm) snapshots the NI-700 and CI-700
 * counters at once; the streams' work follows (CIX's md_event, an event
 * here): increments fetched, then with the fixed polling on (pm_ready) the
 * DPM is fed (app_pmucnt.c), else packages go to the AP on RTT channel 1:
 * clock changes (type 1), PMU increments (0), AMU (2), OPP changes (3),
 * PMIC voltages (4), PVT (5).  The fine tick (50 us, CCOMPARE0) samples the
 * DDR controllers' and NPU's utilisation.  Across suspend the streams are
 * suspended, their counters set up again on resume (perf_stream_restore).
 *
 * CIX's critical sections are left out: the handlers here run from the
 * main loop, never interrupting each other.  CIX's logging of the
 * packages (log flag) is left out; the flag is kept.
 */
#include <fwk_interrupt.h>

#include "board/board.h"
#include "clk/clk.h"
#include "irq.h"
#include "lcru.h"
#include "mclp/mclp.h"
#include "perf.h"
#include "pmic.h"
#include "pvt.h"
#include "rtt/rtt.h"

#include "dev_counters.h"

#define STREAMS			20
#define RTT_PERF_CHANNEL	1
#define RTT_PERF_SPACE		0xfffffu
#define PKG_SIGNATURE		0xa5a7a6a8u
#define PKG_RESERVED		0x0fff0000u	/* kept from the package's last use */
#define FINE_TICK_US		50

/* CIX's STREAM_CTRL */
typedef struct {
	uint8_t type;		/* PERF_STREAM_TYPE */
	uint16_t interval;	/* coarse ticks */
	PERF_STREAM_CONFIG cfg;
	uint32_t tmp_data;	/* PMU increments not packed yet */
	void *collection_data;
} STREAM_CTRL;

/* CIX's STREAM_META: kind (0 normal, 1 exclusive, 2 shared), granularity
 * (fine tick), streams of the type */
#define META_KIND(m)		((m) & 3)
#define META_FINE		BIT(2)
#define META_REF(m)		((m) >> 3)

static STREAM_CTRL _s_perfstrm[STREAMS];
static uint8_t _s_perfstrm_meta[PERF_STREAM_TYPE_MAX + 1] = {
	[PERF_STREAM_CLOCK_ALL_IN_ONE] = 2, [PERF_STREAM_AP_AMU] = 2, [PERF_STREAM_DVFS_OPP_SET] = 2,
	[PERF_STREAM_PMIC] = 1, [PERF_STREAM_PVT] = 2, [PERF_STREAM_DDR] = META_FINE,
	[PERF_STREAM_NPU] = META_FINE,
};

/* CIX's statics after _s_perfstrm */
static uint64_t soc_idle_ts_ms;		/* when the SoC last left idle */
static uint64_t sys_cnt;		/* the AP's counter at the coarse tick */
static bool suspended;
static uint8_t coarse_streams, fine_streams;
static uint8_t log_flag;
static bool soc_idle_state;
static uint32_t lost_pkg;
static uint32_t clk_change_cnt;
static uint32_t stream_bitmap;		/* types with streams */
static uint32_t perf_tick;
static uint32_t clk_change_cc;		/* CCOUNT at the first clock change */
static uint32_t ddr_ctl_191, ddr_ctl_192;	/* DDR controllers' idle settings kept */

/* CIX's PERF_STREAM_PKG_CLK_EVENT_WITH_PAYLOAD */
static struct {
	uint32_t signature;
	uint32_t len_type;
	uint64_t system_cnt;
	uint32_t clk_change[20][2];	/* kHz | clock << 23, cycles since the first */
} clk_pkg;

/* CIX's _s_pkg (PERF_STREAM_PKG_SINGLETON) */
static union {
	struct {
		uint32_t signature;
		uint32_t len_type;
		uint32_t system_cnt[2];
		uint32_t mask;
		uint32_t evt_delta[166];
	};
	uint8_t bytes[688];
} _s_pkg;

static bool _ni_has_clock(const NI700_INSTANCE *ni_inst)
{
	CLK_TREE_STRUCT *p = clk_nodes_get(ni700_clk_tree_idx(ni_inst));

	return clk_tree_get_freq(&p->clk_tree_node) > 0;
}

static bool perf_stream_is_shared(PERF_STREAM_TYPE type)
{
	return META_KIND(_s_perfstrm_meta[type]) == 2;
}

static bool perf_stream_is_exclusive(PERF_STREAM_TYPE type)
{
	return META_KIND(_s_perfstrm_meta[type]) == 1;
}

static bool perf_stream_is_free(PERF_STREAM_TYPE type)
{
	return _s_perfstrm_meta[type] < 8;
}

/* --- ticks --------------------------------------------------------------------- */

void perf_stream_fine_tick_handler(void);

static uint32_t fine_tick_period(void)
{
	return core_hz / 1000000 * FINE_TICK_US;
}

static void wsr_ccompare0(uint32_t v)
{
	__asm__ volatile("wsr.ccompare0 %0; rsync" : : "a"(v));
}

static uint32_t rsr_ccompare0(void)
{
	uint32_t v;

	__asm__ volatile("rsr.ccompare0 %0" : "=a"(v));
	return v;
}

static void perf_stream_fine_tick_enable(void)
{
	(void)fwk_interrupt_set_isr(IRQ_TIMER0, perf_stream_fine_tick_handler);
	(void)fwk_interrupt_enable(IRQ_TIMER0);
	wsr_ccompare0(ccount() + fine_tick_period());
}

static void perf_stream_fine_tick_disable(void)
{
	(void)fwk_interrupt_disable(IRQ_TIMER0);
}

/*
 * Every 50 us: the DDR and NPU samples, weighted by the ticks since the
 * last (ticks missed counted, a tick due within 25 us taken now)
 */
void perf_stream_fine_tick_handler(void)
{
	uint32_t period = fine_tick_period(), n = 1, next, d;
	uint32_t compare = rsr_ccompare0(), now = ccount();

	d = now - compare;
	if (d > period)
		n = d / period + 1;
	next = compare + n * period;
	if (next - now < core_hz / 1000000 * 25) {
		n++;
		next += period;
	}
	if (stream_bitmap & BIT(PERF_STREAM_DDR))
		ddr_util_tick_handler(n);
	if (stream_bitmap & BIT(PERF_STREAM_NPU))
		npu_tick_handler(n);
	wsr_ccompare0(next);
}

static void perf_stream_timer_disable(void)
{
	perf_tick_timer_disable();
	perf_tick_event_clear();
	perf_stream_fine_tick_disable();
}

/* --- set-up ------------------------------------------------------------------- */

/* Every counter given back; with for_restore_flow the streams stay */
static void _sw_stream_reset(bool for_restore_flow)
{
	for (uint32_t i = 0; i < 4; i++) {
		if (_ni_has_clock(_s_ni700_hubs[i]))
			ni700_free_all_pmu_counters(_s_ni700_hubs[i]);
	}
	ci700_free_all_pmu_counters();
	ap_clear_amu_cntr();
	ap_clr_mpmm_gear(0xfff);
	dvfs_opp_clear_all_cntr();
	pmic_event_stream_clear();
	pvt_event_stream_clear();
	if (!for_restore_flow)
		(void)clk_api_set_tracking(UINT32_MAX, false);
	stream_bitmap = 0;
	perf_tick = 0;
	sys_cnt = 0;
	lost_pkg = 0;
	coarse_streams = 0;
	fine_streams = 0;
	for (uint32_t t = 0; t <= PERF_STREAM_TYPE_MAX; t++)
		_s_perfstrm_meta[t] &= 7;
}

/* CIX's ss_init_enter: the tick's timer and event exist (scp/module/dpm),
 * no counter is in use */
void perf_stream_init(void)
{
	_sw_stream_reset(false);
	ci700_dfd_trace_init();
	dvfs_opp_evt_stream_init();
	pmic_event_stream_init();
}

/* All streams gone, the AP's stream emptied */
void perf_stream_reset(void)
{
	perf_stream_timer_disable();
	memset(_s_perfstrm, 0, sizeof(_s_perfstrm));
	_sw_stream_reset(false);
	RTT_Reset(RTT_PERF_CHANNEL, true);
}

void perf_stream_suspend(void)
{
	perf_stream_timer_disable();
	suspended = true;
}

/*
 * A stream's counters (CIX's _setup_stream): the source set up, the type's
 * streams counted, the tick it needs counted when the stream is new (or
 * set up again after suspend)
 */
static bool _setup_stream(PERF_STREAM_TYPE type, const PERF_STREAM_CONFIG *cfg, uint8_t alloc_id,
			  bool for_restore_flow)
{
	STREAM_CTRL *c = &_s_perfstrm[alloc_id];
	uint8_t old_type = c->type, *meta = &_s_perfstrm_meta[type];

	if (META_KIND(*meta) != 2)
		c->cfg = *cfg;
	switch (type) {
	case PERF_STREAM_NI700_MM_PMU:
	case PERF_STREAM_NI700_PCIE_PMU:
	case PERF_STREAM_NI700_SYS_PMU:
	case PERF_STREAM_NI700_SMN_PMU: {
		NI700_INSTANCE *ni = _s_ni700_hubs[type - PERF_STREAM_NI700_BASE_IDX];
		uint32_t bitmap = 0;

		if (!_ni_has_clock(ni) ||
		    !ni700_setup_pmu_events(ni, cfg->ni700.port_mask, cfg->ni700.event_id, &bitmap))
			return false;
		c->collection_data = (void *)(uintptr_t)bitmap;
		break;
	}
	case PERF_STREAM_CI700_PMU: {
		CI700_PMU_EVT_COLLECTION *coll = ci700_create_pmu_event_collection(
			cfg->ci700.node_mask, cfg->ci700.event_encode, cfg->ci700.cascade_mode);

		if (!coll)
			return false;
		c->collection_data = coll;
		break;
	}
	case PERF_STREAM_CLOCK_SINGLE_NODE:
		if (!clk_api_set_tracking(cfg->clock.clk_enum_idx, true))
			return false;
		c->collection_data = (void *)(uintptr_t)cfg->clock.clk_enum_idx;
		break;
	case PERF_STREAM_CLOCK_ALL_IN_ONE:
		/* the clocks accumulate: a count of them, the last one asked for gone */
		c->cfg = *cfg;
		if (cfg->clock.clk_enum_idx == UINT32_MAX)
			break;
		if (!clk_api_set_tracking(cfg->clock.clk_enum_idx, true)) {
			c->cfg.clock.clk_enum_idx = UINT32_MAX;
			return false;
		}
		c->cfg.clock.clk_enum_idx = UINT32_MAX;
		c->collection_data = (void *)((uintptr_t)c->collection_data + 1);
		break;
	case PERF_STREAM_AP_AMU:
		c->cfg.amu.core_mask |= cfg->amu.core_mask;
		ap_setup_amu_cntr(cfg->amu.core_mask);
		ap_set_mpmm_gear(0xfff, 0);
		break;
	case PERF_STREAM_DVFS_OPP_SET:
		c->cfg.dvfs_opp.domain_mask |= cfg->dvfs_opp.domain_mask;
		dvfs_opp_setup_cntr((uint16_t)cfg->dvfs_opp.domain_mask);
		break;
	case PERF_STREAM_PMIC:
		pmic_event_stream_setup();
		break;
	case PERF_STREAM_PVT:
		c->cfg.pvt.pvt_idx_bitmask |= cfg->pvt.pvt_idx_bitmask;
		pvt_event_stream_setup(cfg->pvt.pvt_idx_bitmask);
		break;
	case PERF_STREAM_DDR:
		break;
	case PERF_STREAM_NPU:
		npu_cntr_setup();
		break;
	default:
		return false;
	}
	*meta += 8;
	stream_bitmap |= BIT(type);
	if (old_type == PERF_STREAM_NONE || for_restore_flow) {
		if (*meta & META_FINE)
			fine_streams++;
		else
			coarse_streams++;
	}
	return true;
}

/* After suspend (CIX's ss_run_enter): the streams' counters set up again */
void perf_stream_restore(void)
{
	perf_tick_timer_disable();
	perf_tick_event_clear();
	_sw_stream_reset(true);
	for (uint8_t i = 0; i < STREAMS; i++) {
		if (_s_perfstrm[i].type)
			(void)_setup_stream(_s_perfstrm[i].type, &_s_perfstrm[i].cfg, i, true);
	}
	suspended = false;
	if (coarse_streams || fine_streams)
		perf_tick_timer_enable();
	if (fine_streams)
		perf_stream_fine_tick_enable();
}

/*
 * A stream (CIX's perf_stream_create; its overlay): an exclusive type's
 * second is refused, a shared type's adds to the one there; the ticks start
 * with the first stream.  Returns its id, PERF_STREAM_INVALID if none.
 */
uint8_t perf_stream_create(PERF_STREAM_TYPE type, uint16_t interval_ms, const PERF_STREAM_CONFIG *cfg)
{
	STREAM_CTRL *slot = NULL;
	uint8_t id = STREAMS, coarse, fine;

	if (perf_stream_is_shared(type) || perf_stream_is_exclusive(type)) {
		for (uint8_t i = 0; i < STREAMS; i++) {
			if (_s_perfstrm[i].type == type) {
				id = i;
				break;
			}
		}
	}
	if (id < STREAMS) {
		if (perf_stream_is_exclusive(type))
			return PERF_STREAM_INVALID;
	} else {
		for (uint8_t i = 0; i < STREAMS; i++) {
			if (!_s_perfstrm[i].type && !_s_perfstrm[i].interval) {
				id = i;
				break;
			}
		}
		if (id == STREAMS)
			__builtin_trap();	/* CIX: vAssertCalledWithTRC(0xf15f0567) */
		slot = &_s_perfstrm[id];
		slot->interval = interval_ms;
	}
	coarse = coarse_streams;
	fine = fine_streams;
	if (!_setup_stream(type, cfg, id, false)) {
		if (perf_stream_is_free(type))
			memset(&_s_perfstrm[id], 0, sizeof(_s_perfstrm[id]));
		return PERF_STREAM_INVALID;
	}
	if (!coarse && !fine && (coarse_streams || fine_streams))
		perf_tick_timer_enable();
	if (!fine && fine_streams)
		perf_stream_fine_tick_enable();
	if (slot)
		slot->type = (uint8_t)type;
	return id;
}

bool perf_stream_release(uint8_t stream_id)
{
	STREAM_CTRL *c;
	uint8_t type, *meta, m;

	if (stream_id >= STREAMS)
		return false;
	c = &_s_perfstrm[stream_id];
	type = c->type;
	switch (type) {
	case PERF_STREAM_NI700_MM_PMU:
	case PERF_STREAM_NI700_PCIE_PMU:
	case PERF_STREAM_NI700_SYS_PMU:
	case PERF_STREAM_NI700_SMN_PMU: {
		NI700_INSTANCE *ni = _s_ni700_hubs[type - PERF_STREAM_NI700_BASE_IDX];

		if (!_ni_has_clock(ni) || !ni700_free_pmu_events(ni, (uint32_t)(uintptr_t)c->collection_data))
			return false;
		break;
	}
	case PERF_STREAM_CI700_PMU:
		if (!ci700_free_pmu_event_collection(c->collection_data))
			return false;
		break;
	case PERF_STREAM_CLOCK_SINGLE_NODE:
		if (!clk_api_set_tracking((uint32_t)(uintptr_t)c->collection_data, false))
			return false;
		break;
	case PERF_STREAM_CLOCK_ALL_IN_ONE:
		if (!clk_api_set_tracking(UINT32_MAX, false))
			return false;
		break;
	case PERF_STREAM_AP_AMU:
		ap_clear_amu_cntr();
		ap_clr_mpmm_gear(0xfff);
		break;
	case PERF_STREAM_DVFS_OPP_SET:
		dvfs_opp_clear_all_cntr();
		break;
	case PERF_STREAM_PMIC:
		pmic_event_stream_clear();
		break;
	case PERF_STREAM_PVT:
		pvt_event_stream_clear();
		break;
	case PERF_STREAM_DDR:
	case PERF_STREAM_NPU:
		break;
	default:
		return false;
	}
	meta = &_s_perfstrm_meta[type];
	m = *meta;
	if (m < 8)
		__builtin_trap();	/* CIX: vAssertCalledWithTRC(0xf15f0185) */
	*meta = (uint8_t)((((m >> 3) + 31) << 3) | (m & 7));
	if (!(((m >> 3) + 31) & 31))
		stream_bitmap &= ~BIT(type);
	if (m & META_FINE) {
		if (!fine_streams)
			__builtin_trap();	/* CIX: 0xf15f0195 */
		fine_streams--;
	} else {
		if (!coarse_streams)
			__builtin_trap();	/* CIX: 0xf15f0192 */
		coarse_streams--;
	}
	memset(c, 0, sizeof(*c));
	if (!coarse_streams && !fine_streams)
		perf_stream_timer_disable();
	else if (!fine_streams)
		perf_stream_fine_tick_disable();
	return true;
}

uint32_t perf_stream_lost_pkg(void)
{
	return lost_pkg;
}

void perf_stream_set_log_flag(uint8_t flag)
{
	log_flag = flag;
}

bool perf_stream_is_active(void)
{
	return !suspended;
}

/* A clock that changed, for the next clock package (20 at most) */
void perf_stream_track_clk_change(uint32_t clk_id, uint32_t clk_freq_khz)
{
	uint32_t n = clk_change_cnt, cc, d = 0;

	if (n > 19)
		return;
	cc = ccount();
	if (n) {
		d = cc - clk_change_cc;
	} else {
		clk_change_cc = cc;
		clk_pkg.system_cnt = dev_cnts_get_ap_free_counter();
	}
	clk_pkg.clk_change[n][0] = (clk_freq_khz & 0x7fffff) + (clk_id << 23);
	clk_pkg.clk_change[n][1] = d;
	clk_change_cnt = n + 1;
}

/* --- the coarse tick --------------------------------------------------------- */

/* CIX's timer callback: the PMU counters snapshot, the AP's counter */
void perf_stream_coarse_tick_handler(void *pData)
{
	(void)pData;
	for (uint32_t t = PERF_STREAM_NI700_MM_PMU; t <= PERF_STREAM_NI700_SMN_PMU; t++) {
		if (stream_bitmap & BIT(t))
			ni700_take_pmu_cnt_snapshot(_s_ni700_hubs[t - 1]);
	}
	if (stream_bitmap & BIT(PERF_STREAM_CI700_PMU)) {
		ci700_take_pmu_cnt_snapshot();
		ci700_dfd_read_sampling();
	}
	sys_cnt = dev_cnts_get_ap_free_counter();
	perf_tick_event_signal();
}

/* Free space for a package on the AP's stream; a package lost counted */
static bool _flush_enough_space(uint32_t max_pkg_len)
{
	if (RTT_PERF_SPACE - RTT_BytesInQueue(RTT_PERF_CHANNEL, true) >= max_pkg_len)
		return true;
	lost_pkg++;
	return false;
}

static STREAM_CTRL *_find_stream_ctrl(PERF_STREAM_TYPE stream_type)
{
	for (uint32_t i = 0; i < STREAMS; i++) {
		if (_s_perfstrm[i].type == stream_type)
			return &_s_perfstrm[i];
	}
	return NULL;
}

static bool _flush_cond_met(uint32_t tick, PERF_STREAM_TYPE stream_type, uint32_t max_pkg_len)
{
	STREAM_CTRL *c;

	if (!_flush_enough_space(max_pkg_len) || !(c = _find_stream_ctrl(stream_type)))
		return false;
	return tick % c->interval == 0;
}

void *perf_stream_get_pkg(void)
{
	return &_s_pkg;
}

void perf_stream_put_pkg(void *pkg)
{
	const uint8_t *p = pkg;
	uint32_t len = p[4] | (uint32_t)p[5] << 8;

	if (_flush_enough_space(len))
		(void)RTT_Write(RTT_PERF_CHANNEL, pkg, len);
}

/* A PMU stream's increment since the last fetch */
uint32_t perf_stream_calc_delta_of_pmu(uint8_t stream_id)
{
	const STREAM_CTRL *c = &_s_perfstrm[stream_id];

	if ((uint8_t)(c->type - 1) < 4) {
		NI700_INSTANCE *ni = _s_ni700_hubs[c->type - 1];

		if (!_ni_has_clock(ni))
			return 0;
		return ni700_calc_event_increment(ni, (uint32_t)(uintptr_t)c->collection_data);
	}
	if (c->type == PERF_STREAM_CI700_PMU)
		return ci700_increment_pmu_event_collection(c->collection_data);
	return 0;
}

static void pkg_head(uint32_t len_type)
{
	_s_pkg.signature = PKG_SIGNATURE;
	_s_pkg.len_type = (_s_pkg.len_type & PKG_RESERVED) + len_type;
}

static void pkg_sys_cnt(uint64_t cnt)
{
	_s_pkg.system_cnt[0] = (uint32_t)cnt;
	_s_pkg.system_cnt[1] = (uint32_t)(cnt >> 32);
}

/* The streams' work after a coarse tick (CIX's md_event callback) */
void perf_stream_event_handler(uint32_t evtid, uint32_t signaledBits)
{
	uint64_t cnt;
	uint32_t n, mask, len;

	(void)evtid;
	(void)signaledBits;
	for (uint32_t t = PERF_STREAM_NI700_MM_PMU; t <= PERF_STREAM_NI700_SMN_PMU; t++) {
		if (stream_bitmap & BIT(t))
			ni700_fetch_pmu_cnt_snapshot(_s_ni700_hubs[t - 1]);
	}
	if (stream_bitmap & BIT(PERF_STREAM_CI700_PMU))
		ci700_fetch_pmu_cnt_snapshot();
	if (stream_bitmap & BIT(PERF_STREAM_AP_AMU))
		ap_fetch_amu_cntr_snapshot();
	cnt = sys_cnt;
	perf_tick++;
	if (sky1_pmu_is_fixed_polling_enabled()) {
		app_stream_event_post_handler(perf_tick, cnt);
		return;
	}
	if (stream_bitmap & BIT(PERF_STREAM_DVFS_OPP_SET))
		dvfs_opp_fetch_snapshot();
	if (stream_bitmap & BIT(PERF_STREAM_PMIC))
		pmic_event_fetch_snapshot();
	for (uint8_t i = 0; i < STREAMS; i++) {
		if ((uint8_t)(_s_perfstrm[i].type - 1) < 5)
			_s_perfstrm[i].tmp_data += perf_stream_calc_delta_of_pmu(i);
	}
	/* the clocks that changed */
	if (_flush_enough_space(176) && clk_change_cnt) {
		len = (clk_change_cnt + 2) * 8 & 0xfff8;
		clk_pkg.signature = PKG_SIGNATURE;
		clk_pkg.len_type = (clk_pkg.len_type & PKG_RESERVED) + len + (1u << 28);
		(void)RTT_Write(RTT_PERF_CHANNEL, &clk_pkg, len);
		clk_change_cnt = 0;
	}
	/* the PMU streams due, and the CI-700's trace sample */
	if (_flush_enough_space(100)) {
		n = 0;
		mask = 0;
		for (uint32_t i = 0; i < STREAMS; i++) {
			STREAM_CTRL *c = &_s_perfstrm[i];

			if ((uint8_t)(c->type - 1) < 5 && perf_tick % c->interval == 0) {
				_s_pkg.evt_delta[n++] = c->tmp_data;
				mask |= BIT(i);
				c->tmp_data = 0;
			}
		}
		if (n < 20) {
			uint16_t sample[16];

			if (ci700_dfd_trace_process(sample)) {
				mask |= BIT(31);
				_s_pkg.evt_delta[n++] = sample[0];
			}
		}
		if (n) {
			len = (n + 5) * 4 & 0xfffc;
			pkg_head(len);
			pkg_sys_cnt(cnt);
			_s_pkg.mask = mask;
			(void)RTT_Write(RTT_PERF_CHANNEL, &_s_pkg, len);
		}
	}
	/* the AP's activity monitors */
	if (_flush_cond_met(perf_tick, PERF_STREAM_AP_AMU, 0x180)) {
		n = 0;
		mask = 0;
		if (!_find_stream_ctrl(PERF_STREAM_AP_AMU))
			__builtin_trap();	/* CIX: 0xf15f0936 */
		for (uint8_t c = 0; c < AP_AMU_CORES; c++) {
			if (!ap_amu_contain_data(c))
				continue;
			for (uint8_t k = 0; k < 7; k++)
				_s_pkg.evt_delta[n + k] = ap_get_and_clr_amu_event_incre(c, k);
			mask |= BIT(c);
			n += 7;
		}
		if (n) {
			len = (n + 5) * 4 & 0xfffc;
			pkg_head(len + (1u << 29));
			pkg_sys_cnt(cnt);
			_s_pkg.mask = mask;
			(void)RTT_Write(RTT_PERF_CHANNEL, &_s_pkg, len);
		}
	}
	/* the OPP changes */
	if ((stream_bitmap & BIT(PERF_STREAM_DVFS_OPP_SET)) &&
	    _flush_cond_met(perf_tick, PERF_STREAM_DVFS_OPP_SET, 112) && (n = dvfs_opp_item_cnt())) {
		pkg_head(0x30000008u + 13 * n);
		for (uint8_t i = 0; i < n; i++) {
			const uint8_t *item = dvfs_opp_get_item(i);

			if (!item)
				__builtin_trap();	/* CIX: 0xf15f1013 */
			memcpy(_s_pkg.bytes + 8 + 13 * i, item, 13);
		}
		dvfs_opp_free_snapshot();
		(void)RTT_Write(RTT_PERF_CHANNEL, &_s_pkg, _s_pkg.len_type & 0xffff);
	}
	/* the PMIC voltages */
	if ((stream_bitmap & BIT(PERF_STREAM_PMIC)) && _flush_cond_met(perf_tick, PERF_STREAM_PMIC, 104) &&
	    (n = pmic_event_item_cnt())) {
		pkg_head(0x40000008u + 12 * n);
		for (uint8_t i = 0; i < n; i++) {
			const struct pmic_event_item *item = pmic_event_get_item(i);
			uint8_t *p = _s_pkg.bytes + 8 + 12 * i;

			if (!item)
				__builtin_trap();	/* CIX: 0xf15f1058 */
			memcpy(p, &item->timestamp, 8);
			memcpy(p + 8, &item->i2c_port, 2);
			memcpy(p + 10, &item->voltage, 2);
		}
		pmic_event_free_snapshot();
		(void)RTT_Write(RTT_PERF_CHANNEL, &_s_pkg, _s_pkg.len_type & 0xffff);
	}
	/* the PVT sensors */
	if (_flush_cond_met(perf_tick, PERF_STREAM_PVT, 198)) {
		struct pvt_event evt;
		uint8_t idx = 0, i;
		uint16_t pvt_mask = 0;

		n = 0;
		while ((i = pvt_event_fetch(&evt, idx)) != 0xff) {
			memcpy(_s_pkg.bytes + 18 + 12 * n, &evt, 12);
			pvt_mask |= (uint16_t)BIT(i);
			idx = (uint8_t)(i + 1);
			n++;
		}
		if (n) {
			len = (18 + 12 * n) & 0xfffe;
			pkg_head(len + (5u << 28));
			pkg_sys_cnt(cnt);
			memcpy(_s_pkg.bytes + 16, &pvt_mask, 2);
			(void)RTT_Write(RTT_PERF_CHANNEL, &_s_pkg, len);
		}
	}
}

/* --- the SoC's idle switch (app_pmucnt.c decides) ------------------------------- */

bool perf_soc_is_in_idle_state(void)
{
	return soc_idle_state;
}

/* The DDR controllers' self-refresh idle counters (CTL 191/192), longer while idle */
static void perf_stream_ddr_sr_idle_counter(bool is_idle)
{
	for (uint32_t ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (!(g_chEnMask & BIT(ch)))
			continue;
		if (is_idle) {
			if (!ddr_ctl_191)
				mclp_swif_reg_read(MEM_REG_DDRC, 0x2fc, ch, &ddr_ctl_191);
			if (!ddr_ctl_192)
				mclp_swif_reg_read(MEM_REG_DDRC, 0x300, ch, &ddr_ctl_192);
			mclp_swif_reg_write(MEM_REG_DDRC, 0x2fc, ch, 0x10007);
			mclp_swif_reg_write(MEM_REG_DDRC, 0x300, ch, 0x1010200);
		} else if (ddr_ctl_191 && ddr_ctl_192) {
			mclp_swif_reg_write(MEM_REG_DDRC, 0x2fc, ch, ddr_ctl_191);
			mclp_swif_reg_write(MEM_REG_DDRC, 0x300, ch, ddr_ctl_192);
		}
	}
}

/* the longest switch (us) and their count, in the PM's scratch words */
#define SCRATCH_IDLE_SWITCH_US	(PM_SCRATCH + 0x58)
#define SCRATCH_IDLE_SWITCHES	(PM_SCRATCH + 0x5c)

static void idle_switch_account(uint32_t cc0)
{
	uint32_t us = (ccount() - cc0) / (core_hz / 1000000);

	if (REG32(SCRATCH_IDLE_SWITCH_US) < us)
		REG32(SCRATCH_IDLE_SWITCH_US) = us;
	REG32(SCRATCH_IDLE_SWITCHES)++;
}

static uint64_t arch_getTimeNowMs(void)
{
	return arch_getTimeNowUs() / 1000;
}

extern uint32_t soc_idle_min_interval[5];

/*
 * The SoC idles (and has been busy at least soc_idle_min_interval ms): DDR
 * to its low-power state, two PLLs' outputs divided, the controllers'
 * self-refresh sooner, the core at 200 MHz, no fine tick
 */
void perf_soc_busy_to_idle(void)
{
	uint32_t cc0;

	if (soc_idle_state || arch_getTimeNowMs() - soc_idle_ts_ms <= soc_idle_min_interval[0])
		return;
	cc0 = ccount();
	perf_stream_fine_tick_disable();
	(void)mclp_hwif_parallel_run_lp_cmd(20, 8000, 1, true);
	pll_ctrl_set_POSTDIV2_with_gating(4, (void *)0xa0065200, 0xa0065214, 1);
	pll_ctrl_set_POSTDIV2_with_gating(2, (void *)0xa0062000, 0xa0062014, 16);
	perf_stream_ddr_sr_idle_counter(true);
	idle_switch_account(cc0);
	lcru_core_clock(3);		/* CIX's dev_lx7clk_freq_set(3) */
	soc_idle_state = true;
}

void perf_soc_idle_to_busy(void)
{
	uint32_t cc0;

	if (!soc_idle_state)
		return;
	lcru_core_clock(1);		/* CIX's dev_lx7clk_freq_set(1) */
	cc0 = ccount();
	(void)mclp_hwif_parallel_run_lp_cmd(20, 8000, 2, true);
	perf_stream_ddr_sr_idle_counter(false);
	pll_ctrl_set_POSTDIV2_with_gating(1, (void *)0xa0065200, 0xa0065214, 1);
	pll_ctrl_set_POSTDIV2_with_gating(1, (void *)0xa0062000, 0xa0062014, 16);
	perf_stream_fine_tick_enable();
	idle_switch_account(cc0);
	soc_idle_state = false;
	soc_idle_ts_ms = arch_getTimeNowMs();
}
