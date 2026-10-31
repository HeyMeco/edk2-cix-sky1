/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DVFS domains (CIX's config_dvfs.c): twelve elements, gpu_core to
 * mmhub_dfs, whose OPP tables the firmware builds at power-on and init.
 * The table and its construction are in src/dpm/dvfs_cfg.c; by the time
 * the framework asks for the elements, apply_config_dvfs_sustained_level
 * has built it.
 */
#include <fwk_module.h>

#include "dpm/dvfs_cfg.h"

const struct fwk_module_config config_dvfs = {
	.elements = FWK_MODULE_DYNAMIC_ELEMENTS(dvfs_get_element_table),
};
