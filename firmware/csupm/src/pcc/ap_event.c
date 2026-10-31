/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The AP's cores as the PCC tables need them (CIX's
 * Source/common/soc/ap/ap_event.c, ap_is_clk_on and amu_read): whether a
 * core's clock runs, and its activity monitor counters (AMU), read through
 * the SoC window.
 */
#include "clk/clk.h"
#include "dpm/dpm.h"
#include "pcc.h"
#include "soc.h"

#define AP_AMU			0x0f090000u	/* core i at + 1 MB * i */
#define DSU_AMU			0x0f040000u
#define AP_CORES		12
#define AP_AMU_COUNTERS		7

static const uint16_t ap_amu_reg_offsets[AP_AMU_COUNTERS] = {
	0, 8, 0x10, 0x18, 0x100, 0x108, 0x110,
};
static const uint16_t dsu_amu_reg_offsets[5] = { 0, 8, 0x10, 0x18, 0x20 };

bool ap_is_clk_on(uint32_t ele)
{
	CLK_TREE_STRUCT *p = clk_nodes_get(dpm_elem_get_clk_id(ele));

	return clk_tree_get_freq(&p->clk_tree_node) > 0;
}

/*
 * A core's counter, never going backwards: CIX's keeps the last value read
 * and gives it again when a read is below it, unless a second read is at
 * least as high and the first was not small (a counter that restarted:
 * above 0x7ffff for the architected counters 0-3, above 255 for the
 * auxiliary ones), when it takes the second.  The DSU's counters are read
 * as they are.
 */
uint64_t amu_read(uint32_t core_idx, uint32_t cnt)
{
	static uint64_t amu_cache[AP_CORES][AP_AMU_COUNTERS];
	uint64_t *cache, v, v2, small;
	uint32_t addr;

	if (core_idx == DPM_ELE_DSU)
		return soc_read64(DSU_AMU + dsu_amu_reg_offsets[cnt]);
	addr = AP_AMU + (core_idx << 20) + ap_amu_reg_offsets[cnt];
	cache = &amu_cache[core_idx][cnt];
	v = soc_read64(addr);
	if (v > *cache) {
		*cache = v;
	} else if (v != 0) {
		v2 = soc_read64(addr);
		small = cnt >= 4 ? 255 : 0x7ffff;
		if (v2 >= v && v > small)
			*cache = v2;
	}
	return *cache;
}
