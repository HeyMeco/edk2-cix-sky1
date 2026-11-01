/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The board's temperature sensors (CIX's config_sensor.c, the part that is
 * not SCP-firmware configuration: config_board_sensor, read_board_sensor,
 * i2c_read_ti_tmp75, config_board_sensor_get and the skin temperature's
 * model).
 *
 * Six sensors, SCMI's PCB_SOC to PCB_PMIC (scp/config/config_soc_sensor.c,
 * read by scp/module/soc_sensor from soc_sensor_board_temp).  Without a
 * configuration each is a register the EC writes (CUS_TEMPR_REG1-6,
 * whole degrees then 256ths); the BIOS's pm_config_pvt_t can make the
 * first two a TI TMP75 on I2C (the MS-R1's: controller 3, 0x4d).  CIX
 * reads them all every 200 ms.  A sensor on I2C makes its controller a
 * master (overwrite_i2c_master: src/main.c sets up the I2C controllers
 * after the configuration is read).
 */
#include <mod_pm_svc.h>
#include <mod_soc_sensor.h>

#include "board/board.h"
#include "i2c.h"

#define BOARD_SENSORS		SOC_SENSOR_BOARD_N
#define BOARD_SENSOR_US		200000u
#define SENSOR_EC_REG		0	/* config value: an EC-written register */
#define SENSOR_I2C		1	/* a sensor on I2C */
#define SENSOR_TYPE_TMP75	0

/* CIX's board_sensor_config_t: valid (bit 0 clear) and type (bits 1-31),
 * then the I2C controller, address, sensor type and register, or the EC
 * register's number */
__attribute__((section(".dram1.data"))) static struct {
	uint32_t valid;
	uint8_t i2c_ctrl, i2c_addr, sensor_type, reg_addr;
} bs_config[BOARD_SENSORS] = {
	{ 0, CUS_TEMPR_REG1 + 0, 0, 0, 0 }, { 0, CUS_TEMPR_REG1 + 1, 0, 0, 0 },
	{ 0, CUS_TEMPR_REG1 + 2, 0, 0, 0 }, { 0, CUS_TEMPR_REG1 + 3, 0, 0, 0 },
	{ 0, CUS_TEMPR_REG1 + 4, 0, 0, 0 }, { 0, CUS_TEMPR_REG1 + 5, 0, 0, 0 },
};

/* CIX's overwrite_i2c_master: controllers a sensor needs as masters */
uint32_t overwrite_i2c_master;

/* the skin temperature's model: kh * T0 + ka * T1 + c, and alpha (CIX keeps
 * them next to g_sensor_name and g_board_temp) */
static float skin_kh = 0.5f, skin_ka = 0.5f, skin_c, skin_alpha = 1.0f;

static void bs_set(unsigned i, const uint8_t *p)
{
	bs_config[i].valid = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
			     (uint32_t)p[3] << 24;
	bs_config[i].i2c_ctrl = p[4];
	bs_config[i].i2c_addr = p[5];
	bs_config[i].sensor_type = p[6];
	bs_config[i].reg_addr = p[7];
}

/*
 * CIX's config_board_sensor (from sky1_pvt_data_config): board_sensor1 and
 * 2 of pm_config_pvt_t (at +0x12 and +0x1a) for sensors 0 and 1, each when
 * valid.  CIX's oddity kept: when sensor 1 is the TMP75 at 0x4d on
 * controller 3 register 1, sensor 2 becomes the same device's register
 * 0x23.
 */
void config_board_sensor(const uint8_t *pvt_config)
{
	if (!(pvt_config[0x12] & 1))
		bs_set(0, pvt_config + 0x12);
	if (!(pvt_config[0x1a] & 1))
		bs_set(1, pvt_config + 0x1a);
	if (!(bs_config[1].valid & 1) && bs_config[1].i2c_ctrl == 3 && bs_config[1].i2c_addr == 0x4d &&
	    bs_config[1].reg_addr == 1) {
		bs_config[2] = bs_config[1];
		bs_config[2].reg_addr = 0x23;
	}
	for (unsigned i = 0; i < BOARD_SENSORS; i++) {
		if (bs_config[i].valid == SENSOR_I2C << 1)
			overwrite_i2c_master |= BIT(bs_config[i].i2c_ctrl);
	}
}

/* CIX's i2c_read_ti_tmp75: whole degrees, then sixteenths in the high
 * nibble */
static bool i2c_read_ti_tmp75(uint8_t ctrl, uint8_t addr, uint8_t raddr, float *temp)
{
	uint8_t v[2];

	if (i2c_write_read(ctrl, addr, &raddr, 1, v, 2))
		return false;
	*temp = (float)(v[1] >> 4) * 0.0625f + (float)v[0];
	return true;
}

/* CIX's read_board_sensor, every 200 ms */
static void read_board_sensor(void *arg)
{
	(void)arg;
	for (unsigned i = 0; i < BOARD_SENSORS; i++) {
		uint32_t type = bs_config[i].valid >> 1;

		if (bs_config[i].valid & 1)
			continue;
		if (type == SENSOR_I2C && bs_config[i].sensor_type == SENSOR_TYPE_TMP75) {
			(void)i2c_read_ti_tmp75(bs_config[i].i2c_ctrl, bs_config[i].i2c_addr,
						bs_config[i].reg_addr, &soc_sensor_board_temp[i]);
		} else if (type == SENSOR_EC_REG) {
			uint8_t reg = bs_config[i].i2c_ctrl;	/* reg_id's low byte */

			if (reg < CUS_REG_MAX)
				soc_sensor_board_temp[i] =
					(float)((double)(float)DEV_REG[reg][1] * (1.0 / 256) +
						(double)(float)DEV_REG[reg][0]);
		}
	}
}

/* started with the SCMI sensors (CIX: get_reg_sensor_element_table) */
void config_board_sensor_start(void)
{
	static bool started;

	if (started)
		return;
	started = true;
	(void)pm_svc_timer_alloc(BOARD_SENSOR_US, read_board_sensor, NULL, true, true);
}

float config_board_sensor_get(uint32_t idx)
{
	return idx < BOARD_SENSORS ? soc_sensor_board_temp[idx] : 0.0f;
}

void config_skin_temp_params_set(float kh, float ka, float c, float alpha)
{
	skin_kh = kh;
	skin_ka = ka;
	skin_c = c;
	skin_alpha = alpha;
}

void config_skin_temp_params_get(float *kh, float *ka, float *c, float *alpha)
{
	if (kh)
		*kh = skin_kh;
	if (ka)
		*ka = skin_ka;
	if (c)
		*c = skin_c;
	if (alpha)
		*alpha = skin_alpha;
}

float config_skin_alpha_get(void)
{
	return skin_alpha;
}

/* CIX's config_skin_temp_get: from the first two board sensors */
float config_skin_temp_get(void)
{
	return skin_c + (skin_kh * soc_sensor_board_temp[0] + skin_ka * soc_sensor_board_temp[1]);
}
