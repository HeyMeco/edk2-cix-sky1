/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Sensors (CIX's config_sensor.c): each with two trip points */
#include <mod_scmi_sensor.h>
#include <mod_sensor.h>
#include <mod_soc_sensor.h>

#include <fwk_element.h>
#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

#include "scp_config.h"

extern const struct fwk_element scp_sensor_elements[];

static struct fwk_element elements[SCP_SENSOR_COUNT + 1];
static struct mod_sensor_dev_config configs[SCP_SENSOR_COUNT];

static const struct fwk_element *get_elements(fwk_id_t module_id)
{
	(void)module_id;
	for (unsigned int i = 0; i < SCP_SENSOR_COUNT; i++) {
		configs[i] = (struct mod_sensor_dev_config){
			.driver_id = FWK_ID_ELEMENT(FWK_MODULE_IDX_SOC_SENSOR, i),
			.driver_api_id = FWK_ID_API(FWK_MODULE_IDX_SOC_SENSOR,
						    MOD_SOC_SENSOR_API_IDX_DRIVER),
			.notification_id = { 0 },	/* invalid */
			.trip_point_api_id = { 0 },
			.trip_point.count = 2,
		};
		elements[i] = (struct fwk_element){
			.name = scp_sensor_elements[i].name,
			.data = &configs[i],
		};
	}
	return elements;
}

const struct fwk_module_config config_sensor = {
	.data = &(const struct mod_sensor_config){
		.notification_id = FWK_ID_MODULE_INIT(FWK_MODULE_IDX_SCMI_SENSOR),
		.trip_point_api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_SCMI_SENSOR,
						     SCMI_SENSOR_API_IDX_TRIP_POINT),
	},
	.elements = FWK_MODULE_DYNAMIC_ELEMENTS(get_elements),
};
