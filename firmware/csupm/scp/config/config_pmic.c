/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The EDP rails (CIX's config_pmic.c): element i is the DPM's rail i and
 * answers the PSU element of the same domain (psu_idx).
 *
 * CIX's msg_p2a_on_pmic_error builds the PMICs' status for the AP as a
 * CIX-specific SCMI notification (protocol 0x81); CIX's image calls it only
 * from its debug CLI (prvPmicTest), and its PM-to-AP message path
 * (md_msg_p2a_send) is empty, so the AP never gets it: the AP reads the
 * same report with PM message 0x400 (src/msg).
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

/*
 * CIX's msg_p2a_on_pmic_error: the accumulated report (dev_pmic.c's, up to
 * 40 bytes) in a notification for protocol 0x81 (CIX's event 0x10c7 to its
 * SCMI module).  Nothing in the firmware raises it (see above): it builds
 * the report and stops there.
 */
void msg_p2a_on_pmic_error(void)
{
	uint32_t report[10] = { 0 };

	(void)dev_pmic_build_msg_report_buffer(PMIC_STATUS_ALL, report, sizeof(report));
}

const struct fwk_module_config config_pmic = {
	.elements = FWK_MODULE_STATIC_ELEMENTS_PTR(element_table),
};
