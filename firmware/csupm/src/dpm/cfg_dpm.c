/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The board layout's DVFS limits (CIX's cfg_dpm.c: dpm_edp_cfg_get_type,
 * dpm_edp_cfg_get_pmic_topology_id, dpm_dxs_get_opp_max, dpm_dxs_set_opp_max).
 *
 * CIX keeps each layout's rails and OPP maxima in one table, dpm_edp_scheme;
 * the rails are src/pmic.c's, the maxima dpm_data.c's dpm_edp_opp_max.  The
 * layout is the one src/pmic.c found (CIX's pmic_profile_setup sets
 * dpm_edp_cfg_type from the same probe).
 */
#include "dpm.h"

DPM_EDP_CONFIG_TYPE dpm_edp_cfg_get_type(void)
{
	return pmic_get_preset();
}

/*
 * CIX's pmic_type_id: the layout for the built-in ones; a custom layout (BIOS
 * configuration, not read yet) would be classified by its regulators.
 */
uint32_t dpm_edp_cfg_get_pmic_topology_id(void)
{
	DPM_EDP_CONFIG_TYPE t = dpm_edp_cfg_get_type();

	return t >= PMIC_EVB && t < DPM_EDP_CFG_MAX ? (uint32_t)t : 0;
}

uint16_t dpm_dxs_get_opp_max(DPM_DXS_DOMAIN dxs_id)
{
	DPM_EDP_CONFIG_TYPE t = dpm_edp_cfg_get_type();

	if (dxs_id >= DPM_DxS_MAX || t >= DPM_EDP_CFG_MAX)
		return 0;
	return dpm_edp_opp_max[t][dxs_id];
}

bool dpm_dxs_set_opp_max(DPM_DXS_DOMAIN dxs_id, uint16_t opp_max)
{
	DPM_EDP_CONFIG_TYPE t = dpm_edp_cfg_get_type();

	if (dxs_id >= DPM_DxS_MAX || t >= DPM_EDP_CFG_MAX)
		return false;
	dpm_edp_opp_max[t][dxs_id] = opp_max;
	return true;
}
