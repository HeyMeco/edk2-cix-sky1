/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The supplies (CIX's config_psu.c): each an EDP rail of the pmic driver */
#include <mod_pmic.h>
#include <mod_psu.h>

#include <fwk_element.h>
#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

#define PSU(n, rail)								\
	{ .name = n, .data = &(const struct mod_psu_element_cfg){		\
		.driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_PMIC, rail),	\
		.driver_api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_PMIC,		\
						 MOD_PMIC_API_IDX_DRIVER),	\
	} }

static const struct fwk_element element_table[] = {
	PSU("PSU_LITTLE", CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_LITTLE),
	PSU("PSU_BIG_G0", CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_BIG_G0),
	PSU("PSU_BIG_G1", CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_BIG_G1),
	PSU("PSU_MID_G0", CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_MID_G0),
	PSU("PSU_MID_G1", CONFIG_PMIC_ELEMENT_IDX_CPU_GROUP_MID_G1),
	PSU("PSU_GPU", CONFIG_PMIC_ELEMENT_IDX_GPU),
	PSU("PSU_DSU", CONFIG_PMIC_ELEMENT_IDX_DSU),
	PSU("PSU_SOC", CONFIG_PMIC_ELEMENT_IDX_SOC),
	{ 0 },
};

/* CIX's psu_get_element_table */
static const struct fwk_element *psu_get_element_table(fwk_id_t module_id)
{
	(void)module_id;
	return element_table;
}

const struct fwk_module_config config_psu = {
	.elements = FWK_MODULE_DYNAMIC_ELEMENTS(psu_get_element_table),
};
