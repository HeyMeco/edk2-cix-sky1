/** @file
  DSDT configuration for the Orange Pi 6.

  Selected with LINUX_ACPI_CONFIG_OVERRIDE; replaces
  Platform/CIX/Sky1/Drivers/AcpiSocTables/DefaultLinuxAcpiConfig.h.
  Values follow the board as described by the Orange Pi firmware
  (see docs/provenance/orangepi.md).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef __OPI6_LINUX_ACPI_CONFIG_H__
#define __OPI6_LINUX_ACPI_CONFIG_H__

#define USBC_PHY_MODE_USB    0x1
#define USBC_PHY_MODE_DP     0x2
#define USBC_PHY_MODE_COMBO  0x3

/* USB-C 0/1: PD controllers PD00/PD01 on I2C7 */
#define USBC0_PD_EN         1
#define USBC0_PD_DEVICE     \_SB.I2C7.PD00
#define USBC0_DEF_PMODE     USBC_PHY_MODE_COMBO
#define USBC0_DISABLE_USB3  0

#define USBC1_PD_EN         1
#define USBC1_PD_DEVICE     \_SB.I2C7.PD01
#define USBC1_DEF_PMODE     USBC_PHY_MODE_COMBO
#define USBC1_DISABLE_USB3  0

/* USB-C PHY 2/3: no PD controller, wired as DisplayPort outputs */
#define USBC2_PD_EN         0
#define USBC2_PD_DEVICE     \_SB.I2C1.PD10
#define USBC2_DEF_PMODE     USBC_PHY_MODE_DP
#define USBC2_DISABLE_USB3  0

#define USBC3_PD_EN         0
#define USBC3_PD_DEVICE     \_SB.I2C1.PD11
#define USBC3_DEF_PMODE     USBC_PHY_MODE_DP
#define USBC3_DISABLE_USB3  0

#define USBC0_VBUS_OVERRIDE  0
#define USBA4_VBUS_OVERRIDE  0
#define USBA5_VBUS_OVERRIDE  0

#define USBC0_OC_EN  1
#define USBC1_OC_EN  1
#define USBC2_OC_EN  1
#define USBC3_OC_EN  1
#define USBA4_OC_EN  1
#define USBA5_OC_EN  1
#define USBH6_OC_EN  1
#define USBH7_OC_EN  1
#define USBH8_OC_EN  1
#define USBH9_OC_EN  1

/* PCIe reset and power; regulators PVCx are declared in the board SSDT */
#define PERST_GPIO_ACTIVE_LEVEL  GPIO_ACTIVE_HIGH

#define PCIE_X8_PERST           1
#define PCIE_X8_PERST_GPIO_CTR  "\\_SB.GPI4"
#define PCIE_X8_PERST_GPIO      1

#define PCIE_X4_PERST           1
#define PCIE_X4_PERST_GPIO_CTR  "\\_SB.GPI4"
#define PCIE_X4_PERST_GPIO      3

#define PCIE_X2_PERST           1
#define PCIE_X2_PERST_GPIO_CTR  "\\_SB.GPI4"
#define PCIE_X2_PERST_GPIO      2

#define PCIE_X1_1_PERST           1
#define PCIE_X1_1_PERST_GPIO_CTR  "\\_SB.GPI4"
#define PCIE_X1_1_PERST_GPIO      5

#define PCIE_X1_0_PERST           1
#define PCIE_X1_0_PERST_GPIO_CTR  "\\_SB.GPI4"
#define PCIE_X1_0_PERST_GPIO      4

#define PCIE_X8_VCC_SUPPLY       1
#define PCIE_X8_VCC_REGULATOR    \_SB.PVC0
#define PCIE_X4_VCC_SUPPLY       1
#define PCIE_X4_VCC_REGULATOR    \_SB.PVC1
#define PCIE_X2_VCC_SUPPLY       0
#define PCIE_X1_1_VCC_SUPPLY     0
#define PCIE_X1_0_VCC_SUPPLY     1
#define PCIE_X1_0_VCC_REGULATOR  \_SB.PVC4

#define PCIE_X1_0_STR_PWRON  0
#define PCIE_X1_0_STD_PWRON  0

#define PCIE_X1_1_STD_PWRON  1
#define PCIE_X1_1_STR_PWRON  1

/* HDA: no power-down GPIO */
#define HDA_EXT_DSD_PROPERTY \
      Package () { "cix,model", "CIX SKY1 ORAPI 6P HDA" }


/*
 * Board guards consumed by the DSDT fragments in
 * Platform/CIX/Sky1/Drivers/AcpiSocTables (patches/edk2-platforms).
 */

/* USB-C 0..3 VBUS/over-current GPIOs on GPIO4 25..28 are not wired */
#undef  USBC0_OC_EN
#undef  USBC1_OC_EN
#undef  USBC2_OC_EN
#undef  USBC3_OC_EN
#define USBC0_OC_EN  0
#define USBC1_OC_EN  0
#define USBC2_OC_EN  0
#define USBC3_OC_EN  0

/* UART3 is routed to the board header with DMA */
#define ACPI_UART3_DMA

/* FCH PWM: PWM1 is the timer at 0x04112000; PWM3 and PWM5 are present */
#define ACPI_PWM1_BASE  0x04112000
#define ACPI_PWM3_PWM5_ENABLE

/* I2C3 bus-recovery GPIOs and I2C3..5 pin groups */
#define ACPI_I2C3_EXT_CRS \
    GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly, "\\_SB.GPI0", 0, ResourceConsumer) { 15, 16 } \
    PinGroupFunction (Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_fch_i2c3", ResourceConsumer,)
#define ACPI_I2C4_EXT_CRS \
    PinGroupFunction (Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_fch_i2c4", ResourceConsumer,)
#define ACPI_I2C5_EXT_CRS \
    PinGroupFunction (Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_fch_i2c5", ResourceConsumer,)

/* Orange Pi 6 */

/* UART0/UART1 have no pin group in the board pin controller */
#define ACPI_UART01_NO_PINCTRL

/* I2C2, I2C4 and I2C5 are not used by the OS */
#define ACPI_I2C_FORCE_DISABLE_MASK  ((1 << 2) | (1 << 4) | (1 << 5))

/* SPI0 (two chip selects) and SPI1 are disabled */
#define ACPI_SPI0_STA     0x0
#define ACPI_SPI1_STA     0x0
#define ACPI_SPI0_NUM_CS  2

/* DP4 drives an on-board PS185 DP-to-HDMI bridge */
#define DP04_EXT_CRS \
    PinGroupFunction (Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_dp_to_hdmi_ps185", ResourceConsumer,) \
    GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly, "\\_SB.GPI0", 0, ResourceConsumer) { 31 }
#define DP04_EXT_DSD_PROPERTY \
    Package () { "dp_to_hdmi", "yes" }, \
    Package () { "pinctrl-names", "default" }, \
    Package () { "pdb-gpios", Package () { ^DP04, 0, 0, 1 } },

#endif
