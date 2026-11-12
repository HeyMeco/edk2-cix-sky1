/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The O6's EC pin set-up: control (GPCR: 0x40 output, 0x80 input,
 * 0x84 input with pull-up, 0x00 alternate function) and initial levels,
 * as Radxa's EC V1.07 programs them (its table at 0x3436).  The nets are
 * in emu/src/ec/board.rs (PINS).
 */
#include "ec.h"

struct reg_init {
	uint16_t addr;
	uint8_t val;
};

static __code const struct reg_init gpio_table[] = {
	{ 0x1600, 0x06 },	/* GCR */
	{ 0x16F1, 0x00 },	/* GCR2 */
	{ 0x1601, 0x08 },	/* GPDRA */
	{ 0x1602, 0x10 },	/* GPDRB */
	{ 0x1603, 0x40 },	/* GPDRC */
	{ 0x1604, 0x00 },	/* GPDRD */
	{ 0x1605, 0x04 },	/* GPDRE */
	{ 0x1606, 0x00 },	/* GPDRF */
	{ 0x1607, 0x00 },	/* GPDRG */
	{ 0x1608, 0x28 },	/* GPDRH */
	{ 0x1609, 0x00 },	/* GPDRI */
	{ 0x160A, 0x00 },	/* GPDRJ */
	{ 0x160D, 0x00 },	/* GPDRM */
	{ 0x1671, 0x02 },	/* GPOTA */
	{ 0x1672, 0x00 },	/* GPOTB */
	{ 0x1674, 0x00 },	/* GPOTD */
	{ 0x1675, 0x08 },	/* GPOTE */
	{ 0x1676, 0x00 },	/* GPOTF */
	{ 0x1678, 0x00 },	/* GPOTH */
	{ 0x167A, 0x05 },	/* GPOTJ */
	{ 0x1610, 0x00 },	/* GPCRA0 */
	{ 0x1611, 0x00 },	/* GPCRA1 BLINK_L */
	{ 0x1612, 0x00 },	/* GPCRA2 FANOUT_EC */
	{ 0x1613, 0x84 },	/* GPCRA3 ATX_DET_EC */
	{ 0x1614, 0x00 },	/* GPCRA4 I2C_PD_SCL */
	{ 0x1615, 0x00 },	/* GPCRA5 I2C_PD_SDA */
	{ 0x1616, 0x84 },	/* GPCRA6 PD0_I2C_INT_L */
	{ 0x1617, 0x84 },	/* GPCRA7 PD1_I2C_INT_L */
	{ 0x1618, 0x80 },	/* GPCRB0 GND */
	{ 0x1619, 0x80 },	/* GPCRB1 GPB1 */
	{ 0x161A, 0x80 },	/* GPCRB2 */
	{ 0x161B, 0x80 },	/* GPCRB3 PWRBTN_L_HW */
	{ 0x161C, 0x40 },	/* GPCRB4 EC_PWRON */
	{ 0x161D, 0x84 },	/* GPCRB5 SOC_SLP5 */
	{ 0x161E, 0x40 },	/* GPCRB6 EC_SOC_RST */
	{ 0x161F, 0x80 },	/* GPCRB7 */
	{ 0x1620, 0x80 },	/* GPCRC0 LID_SW_L */
	{ 0x1621, 0x00 },	/* GPCRC1 EC_I2C_ACPI_SCL */
	{ 0x1622, 0x00 },	/* GPCRC2 EC_I2C_ACPI_SDA */
	{ 0x1623, 0x40 },	/* GPCRC3 TP92 */
	{ 0x1624, 0x80 },	/* GPCRC4 */
	{ 0x1625, 0x80 },	/* GPCRC5 TP93 */
	{ 0x1626, 0x40 },	/* GPCRC6 DEPOP_MUTE_L */
	{ 0x1627, 0x00 },	/* GPCRC7 UART_RX_DEBUG */
	{ 0x1628, 0x40 },	/* GPCRD0 EC_SLP_S3_L */
	{ 0x1629, 0x84 },	/* GPCRD1 SOC_RESET_OUT */
	{ 0x162A, 0x40 },	/* GPCRD2 ERST# */
	{ 0x162B, 0x40 },	/* GPCRD3 EC_ACPI_INT */
	{ 0x162C, 0x84 },	/* GPCRD4 SOC_PWR_OK_IN_L */
	{ 0x162D, 0x80 },	/* GPCRD5 */
	{ 0x162E, 0x00 },	/* GPCRD6 FANTACH_EC */
	{ 0x162F, 0x40 },	/* GPCRD7 EC_ALW_EN */
	{ 0x1630, 0x00 },	/* GPCRE0 TP100 */
	{ 0x1631, 0x80 },	/* GPCRE1 VTR_PWR_CTL */
	{ 0x1632, 0x40 },	/* GPCRE2 RSM_RST */
	{ 0x1633, 0x40 },	/* GPCRE3 */
	{ 0x1634, 0x40 },	/* GPCRE4 TP99 */
	{ 0x1635, 0x40 },	/* GPCRE5 TP91 */
	{ 0x1636, 0x00 },	/* GPCRE6 UART_TX_DEBUG */
	{ 0x1637, 0x00 },	/* GPCRE7 TP101 */
	{ 0x1638, 0x00 },	/* GPCRF0 */
	{ 0x1639, 0x00 },	/* GPCRF1 */
	{ 0x163A, 0x00 },	/* GPCRF2 EC_I2C_HID_SCL */
	{ 0x163B, 0x00 },	/* GPCRF3 EC_I2C_HID_SDA */
	{ 0x163C, 0x40 },	/* GPCRF4 EC_HID_INT */
	{ 0x163D, 0x84 },	/* GPCRF5 EC_CSU_INT */
	{ 0x163E, 0x00 },	/* GPCRF6 I2C_CSU_SCL */
	{ 0x163F, 0x00 },	/* GPCRF7 I2C_CSU_SDA */
	{ 0x1640, 0x40 },	/* GPCRG0 +1.8V_S5_EN (NC) */
	{ 0x1641, 0x80 },	/* GPCRG1 EC_SPI_D2 (NC) */
	{ 0x1642, 0x80 },	/* GPCRG2 STRAP0 */
	{ 0x1643, 0x80 },	/* GPCRG3 EC_SPI_CS_L */
	{ 0x1644, 0x80 },	/* GPCRG4 EC_SPI_D0 */
	{ 0x1645, 0x80 },	/* GPCRG5 EC_SPI_D1 */
	{ 0x1646, 0x80 },	/* GPCRG6 EC_SPI_D3 (NC) */
	{ 0x1647, 0x80 },	/* GPCRG7 EC_SPI_CLK */
	{ 0x1648, 0x84 },	/* GPCRH0 SOC_SLP3 */
	{ 0x1649, 0x40 },	/* GPCRH1 PROCHOT */
	{ 0x164A, 0x84 },	/* GPCRH2 THERMTRIP */
	{ 0x164B, 0x40 },	/* GPCRH3 EC_PMIC_EN_GATE_L */
	{ 0x164C, 0x80 },	/* GPCRH4 SYS_S0_PG */
	{ 0x164D, 0x40 },	/* GPCRH5 +1.8V_S5_EN */
	{ 0x164E, 0x80 },	/* GPCRH6 SYS_S3_PG */
	{ 0x164F, 0x84 },	/* GPCRH7 DC_S5_ALW_EN_CTL */
	{ 0x1650, 0x00 },	/* GPCRI0 SOC_NTC */
	{ 0x1651, 0x00 },	/* GPCRI1 BRDEG_NTC */
	{ 0x1652, 0x00 },	/* GPCRI2 HP_ADC (NC) */
	{ 0x1653, 0x00 },	/* GPCRI3 DRAM_NTC */
	{ 0x1654, 0x00 },	/* GPCRI4 USBC_NTC */
	{ 0x1655, 0x00 },	/* GPCRI5 PMIC_NTC */
	{ 0x1656, 0x80 },	/* GPCRI6 TP90 */
	{ 0x1657, 0x40 },	/* GPCRI7 THERMTRIP_EC */
	{ 0x1658, 0x40 },	/* GPCRJ0 TPM_STR_L */
	{ 0x1659, 0x40 },	/* GPCRJ1 TP122 */
	{ 0x165A, 0x40 },	/* GPCRJ2 TPM_PWR_EN */
	{ 0x165B, 0x80 },	/* GPCRJ3 SYS_ALW_PG */
	{ 0x165C, 0x40 },	/* GPCRJ4 PWRBTN_EC */
	{ 0x165D, 0x40 },	/* GPCRJ5 TP89 */
	{ 0x165E, 0x40 },	/* GPCRJ6 EC_SLP_S5_L */
	{ 0x165F, 0x40 },	/* GPCRJ7 GPJ7 */
	{ 0x16A0, 0x40 },	/* GPCRM0 */
	{ 0x16A1, 0x40 },	/* GPCRM1 */
	{ 0x16A2, 0x80 },	/* GPCRM2 RSV2 */
	{ 0x16A3, 0x84 },	/* GPCRM3 RSV1 */
	{ 0x16A4, 0x40 },	/* GPCRM4 */
	{ 0x16A5, 0x40 },	/* GPCRM5 */
	{ 0x16A6, 0x84 },	/* GPCRM6 RSV4 */
};

void gpio_init(void)
{
	uint8_t i;

	for (i = 0; i < sizeof(gpio_table) / sizeof(gpio_table[0]); i++)
		XREG(gpio_table[i].addr) = gpio_table[i].val;
	/* GPB4 to its alternate function XLP_OUT, which the chip drives:
	 * EC_PWRON to the board's power sequence (sheets 34 and 40), as the
	 * vendor sets it just after its pin table */
	GPCR(PB, 4) = 0x00;
}
