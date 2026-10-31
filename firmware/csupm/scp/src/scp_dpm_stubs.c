/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Weak stand-ins for parts of CIX's firmware the performance modules call
 * (scp_dpm.h) that the open firmware does not have yet; a real definition
 * elsewhere replaces each.  Each does what CIX's does in the state CIX's
 * firmware boots into.
 */
#include <stdbool.h>
#include <stdint.h>

#include "scp_dpm.h"

/* dpm_dashboard.c: no TDP limit until the DPM runs */
__attribute__((weak)) uint32_t dpm_opp_run[DPM_DxS_MAX];

/* cfg_dpm.c: the layout is the one found by probing (src/pmic.c) */
__attribute__((weak)) void dpm_edp_cfg_set_type(enum pmic_preset type)
{
	(void)type;
}

/* intr_mgmt.c: no operating point is forced (CIX: set from its debug CLI) */
__attribute__((weak)) bool get_specific_operating_point(uint32_t *freq_khz, uint32_t *voltage_mv)
{
	(void)freq_khz;
	(void)voltage_mv;
	return false;
}

/* sky1_pwr_model.c: the power model is not ported */
__attribute__((weak)) void pwr_notify_gpu_level(uint32_t gpu_opp_level)
{
	(void)gpu_opp_level;
}

/* dvfs_opp.c: the AP has not asked for the OPP trace (CIX's opp_rrq_id -1) */
__attribute__((weak)) void dvfs_opp_put_event(uint8_t domain, uint32_t level)
{
	(void)domain;
	(void)level;
}
