/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The registers the PM's I2C slave (controller 3, address 4) serves
 * (CIX's Source/projects/sky1/i2c/i2c_slave_mode.c and its DEV_REG): the
 * EC's view of the PM.  On boards whose EC runs the fan (the Orion O6,
 * the Orange Pi 6 Plus) the EC reads the SoC's hottest PVT temperature
 * from CUS_TEMPT_REG1 (degrees, then hundredths; src/pvt.c's polling
 * writes it) and the fan mode Linux asked for from CUS_FAN_MODE_REG (a
 * GPIO line, when the configuration gives one, says it changed; reading
 * it clears that), and may write board temperatures into CUS_TEMPR_REG1-6,
 * which the board sensors without a configuration read
 * (board_sensor.c).  The transfers themselves are src/i2c.c's.
 */
#include "board/board.h"

volatile uint8_t DEV_REG[CUS_REG_MAX][2];

/* CIX's cus_dev_reg_get: a byte of a register, as the EC reads it */
uint8_t cus_dev_reg_get(uint32_t reg_index, uint32_t byte_index)
{
	uint8_t v;

	if (reg_index >= CUS_REG_MAX || byte_index >= 2)
		return 0;
	v = DEV_REG[reg_index][byte_index];
	if (reg_index == CUS_FAN_MODE_REG && byte_index == 1) {
		dev_gpio_clear_ec_event();
		DEV_REG[CUS_STAT_REG1][1] &= 0xfe;
	}
	return v;
}
