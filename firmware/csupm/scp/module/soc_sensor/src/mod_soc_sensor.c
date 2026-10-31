/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SoC and board temperature sensors (CIX's reg_sensor get_value: the PVT
 * reading, or a board temperature, in whole degrees C).
 */
#include <mod_sensor.h>
#include <mod_soc_sensor.h>

#include <fwk_id.h>
#include <fwk_mm.h>
#include <fwk_module.h>
#include <fwk_status.h>

#include "pvt.h"

float soc_sensor_board_temp[SOC_SENSOR_BOARD_N];

static const struct mod_soc_sensor_config **config_table;

static const struct mod_sensor_info info_degrees_c = {
	.type = MOD_SENSOR_TYPE_DEGREES_C,
};

static int get_value(fwk_id_t id, mod_sensor_value_t *value)
{
	const struct mod_soc_sensor_config *c = config_table[fwk_id_get_element_idx(id)];
	float t;

	if (value == NULL)
		return FWK_E_PARAM;
	if (c->pvt >= 1 && c->pvt <= SOC_SENSOR_PVT_LAST) {
		if (pvt_temperature(c->pvt, &t) != 0)
			return FWK_E_DEVICE;
	} else {
		t = soc_sensor_board_temp[c->board];
	}
	*value = t > 0 ? (mod_sensor_value_t)(uint32_t)(int32_t)t : 0;
	return FWK_SUCCESS;
}

static int get_info(fwk_id_t id, struct mod_sensor_info *info)
{
	(void)id;
	if (info == NULL)
		return FWK_E_PARAM;
	*info = info_degrees_c;
	return FWK_SUCCESS;
}

static const struct mod_sensor_driver_api driver_api = {
	.get_value = get_value,
	.get_info = get_info,
};

static int soc_sensor_init(fwk_id_t module_id, unsigned int count, const void *data)
{
	(void)module_id;
	(void)data;
	config_table = fwk_mm_alloc(count, sizeof(config_table[0]));
	return FWK_SUCCESS;
}

static int soc_sensor_element_init(fwk_id_t id, unsigned int sub, const void *data)
{
	(void)sub;
	if (data == NULL)
		return FWK_E_PARAM;
	config_table[fwk_id_get_element_idx(id)] = data;
	return FWK_SUCCESS;
}

static int soc_sensor_process_bind_request(fwk_id_t source_id, fwk_id_t target_id,
					   fwk_id_t api_id, const void **api)
{
	(void)source_id;
	(void)target_id;
	(void)api_id;
	*api = &driver_api;
	return FWK_SUCCESS;
}

const struct fwk_module module_soc_sensor = {
	.type = FWK_MODULE_TYPE_DRIVER,
	.api_count = MOD_SOC_SENSOR_API_IDX_COUNT,
	.init = soc_sensor_init,
	.element_init = soc_sensor_element_init,
	.process_bind_request = soc_sensor_process_bind_request,
};
