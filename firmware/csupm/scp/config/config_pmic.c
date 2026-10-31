/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The EDP rails (CIX's config_pmic.c): element i is the DPM's rail i and
 * answers the PSU element of the same domain (psu_idx).
 *
 * CIX's msg_p2a_on_pmic_error, which sends the PMICs' status to the AP as
 * a CIX-specific SCMI message (protocol 0x81) when they report an error,
 * is left out: the open firmware's scmi module has no such messages yet.
 */
#include <mod_pmic.h>

#include <fwk_element.h>
#include <fwk_id.h>
#include <fwk_module.h>

#include "scp_dpm.h"

/* PSU elements (config_psu.c) */
enum {
	PSU_LITTLE,
	PSU_BIG_G0,
	PSU_BIG_G1,
	PSU_MID_G0,
	PSU_MID_G1,
	PSU_GPU,
	PSU_DSU,
	PSU_SOC,
};

#define PMIC(n, psu, rail)	[rail] = { .name = n, .data = MOD_PMIC_ELEMENT_CFG(psu, rail) }

static const struct fwk_element element_table[] = {
	PMIC("PMIC_LIT", PSU_LITTLE, CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_LITTLE),
	PMIC("PMIC_GM0", PSU_MID_G0, CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_MID_G0),
	PMIC("PMIC_GM1", PSU_MID_G1, CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_MID_G1),
	PMIC("PMIC_GB0", PSU_BIG_G0, CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_BIG_G0),
	PMIC("PMIC_GB1", PSU_BIG_G1, CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_BIG_G1),
	PMIC("PMIC_DSU", PSU_DSU, CONFIG_PMIC_ELEMENT_IDX_DSU),
	PMIC("PMIC_GPU", PSU_GPU, CONFIG_PMIC_ELEMENT_IDX_GPU),
	PMIC("PMIC_SOC", PSU_SOC, CONFIG_PMIC_ELEMENT_IDX_SOC),
	[CONFIG_PMIC_ELEMENT_IDX_COUNT] = { 0 },
};

/* CIX's pmic_config_preset: the board's rail configuration to the DPM */
void pmic_config_preset(enum pmic_preset preset_idx);
void pmic_config_preset(enum pmic_preset preset_idx)
{
	dpm_edp_cfg_set_type(preset_idx);
}

const struct fwk_module_config config_pmic = {
	.elements = FWK_MODULE_STATIC_ELEMENTS_PTR(element_table),
};
