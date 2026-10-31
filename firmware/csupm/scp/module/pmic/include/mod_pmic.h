/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * PSU driver for the EDP rails (CIX's extmodules/pmic): element i is EDP
 * rail i of the DPM (dpm_edp_get_voltage/dpm_edp_set_voltage), answering
 * PSU element psu_idx.  The SOC rail is not driven: its voltage is only
 * remembered.
 */
#ifndef MOD_PMIC_H
#define MOD_PMIC_H

#include <stdint.h>

/* CIX's config_pmic_element_idx: the EDP rails */
enum config_pmic_element_idx {
	CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_LITTLE,
	CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_MID_G0,
	CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_MID_G1,
	CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_BIG_G0,
	CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_BIG_G1,
	CONFIG_PMIC_ELEMENT_IDX_DSU,
	CONFIG_PMIC_ELEMENT_IDX_GPU,
	CONFIG_PMIC_ELEMENT_IDX_SOC,
	CONFIG_PMIC_ELEMENT_IDX_COUNT,
};

/*
 * An element's configuration is this value itself, not a pointer to it
 * (fwk_element.data = MOD_PMIC_ELEMENT_CFG(...)); none_zero keeps it from
 * being NULL.
 */
union mod_pmic_element_cfg {
	struct {
		uint8_t psu_idx;	/* the PSU element this rail answers */
		uint8_t edp_idx;	/* the DPM's EDP rail */
		uint8_t reserved;
		uint8_t none_zero;
	};
	uint32_t pmic_data;
};

#define MOD_PMIC_ELEMENT_CFG(psu, edp) \
	((const void *)(uintptr_t)(0xab000000u | ((uint32_t)(edp) << 8) | (uint32_t)(psu)))

enum mod_pmic_api_idx {
	MOD_PMIC_API_IDX_DRIVER,	/* mod_psu_driver_api */
	MOD_PMIC_API_IDX_COUNT,
};

/* Internal: a voltage change, run after the request returns */
enum mod_pmic_event_idx {
	MOD_PMIC_EVENT_IDX_SWITCH,
	MOD_PMIC_EVENT_IDX_COUNT,
};

#endif
