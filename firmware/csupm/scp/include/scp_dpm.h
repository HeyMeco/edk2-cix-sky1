/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * What the performance modules (pmic, sky1soc_perf, locked_perf,
 * scmi_perf) and their configurations use from CIX's Source/projects/sky1
 * and Source/common: the DPM's EDP rails, maps and TDP frequencies
 * (src/dpm), the GPU's debug-forced operating point, the power model, the
 * OPP trace.  Prototypes as in CIX's image; scp/src/scp_dpm_stubs.c has
 * weak stand-ins for what the open firmware does not have yet.
 */
#ifndef SCP_DPM_H
#define SCP_DPM_H

#include <stdbool.h>
#include <stdint.h>

#include "dpm/dvfs_cfg.h"
#include "pmic.h"

/* CIX's cfg_dpm.c: DVFS element <-> DxS domain */
extern const uint8_t dpm_dvfs_elem_to_dxs_map[12];
extern const uint8_t dpm_dxs_to_dvfs_elem_map[12];

/* CIX's cfg_dpm.c: the board's rail configuration (DPM_EDP_CONFIG_TYPE) */
void dpm_edp_cfg_set_type(enum pmic_preset type);

/*
 * CIX's dpm_main_tab.opp_run (dpm_dashboard.c): per DxS domain, the
 * frequency (kHz) the DPM lets the domain run at under the TDP, 0 for no
 * limit
 */
extern uint32_t dpm_opp_run[DPM_DxS_MAX];

/* CIX's intr_mgmt.c: a debug-forced operating point, if one is set */
bool get_specific_operating_point(uint32_t *freq_khz, uint32_t *voltage_mv);

/* CIX's sky1_pwr_model.c: the GPU's level for the power model */
void pwr_notify_gpu_level(uint32_t gpu_opp_level);

/* CIX's dvfs_opp.c: record a level change for the AP's OPP trace */
void dvfs_opp_put_event(uint8_t domain, uint32_t level);

#endif
