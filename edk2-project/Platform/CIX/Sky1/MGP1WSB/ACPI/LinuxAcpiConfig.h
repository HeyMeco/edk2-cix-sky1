#ifndef __DEFAULT_LINUX_ACPI_CONFIG_H__
#define __DEFAULT_LINUX_ACPI_CONFIG_H__

/*
 * USB Typec port configs
 *
 * %USBCx_PD_EN set 1 if USB work with a PD controller.
 * Typec usb device or typc display device will be recognized by PD
 * controller, as well as forward or backward insertion.
 * if USBCx_PD_EN set, USBCx_PD_DEVICE must be sepcified, and the
 * usb phy mode config USBCx_DEF_PMODE will be ignored.
 *
 * %USBCx_PD_DEVICE USB PD controller device.
 *
 * %USBCx_DEF_PMODE is the USBC phy default mode.
 *      USBC_PHY_MODE_USB   : phy mode for usb only
 *      USBC_PHY_MODE_DP    : phy mode for dp only
 *      USBC_PHY_MODE_COMBO : phy mode for usb and dp
 * ignored if USBCx_PD_EN set.
 *
 * %USBCx_DISABLE_USB3 force usb in high speed mode(USB2.0).
 *
 * %USBxx_OC_EN USB over current mode enable.
 * the "oc" pin must be set as "oc" function in pin settings.
 */
#define USBC_PHY_MODE_USB    0x1
#define USBC_PHY_MODE_DP     0x2
#define USBC_PHY_MODE_COMBO  0x3// USB and DP

#define USBC0_PD_EN         1
#define USBC0_PD_DEVICE     \_SB.I2C7.PD00
#define USBC0_DEF_PMODE     USBC_PHY_MODE_COMBO
#define USBC0_DISABLE_USB3  0

#define USBC1_PD_EN         1
#define USBC1_PD_DEVICE     \_SB.I2C7.PD01
#define USBC1_DEF_PMODE     USBC_PHY_MODE_COMBO
#define USBC1_DISABLE_USB3  0

#define USBC2_PD_EN         0
#define USBC2_DEF_PMODE     USBC_PHY_MODE_DP
#define USBC2_DISABLE_USB3  0

#define USBC3_PD_EN         0
#define USBC3_DEF_PMODE     USBC_PHY_MODE_USB
#define USBC3_DISABLE_USB3  0

// PCIe cfg for PERST and EP Vbat gpio
#define PERST_GPIO_ACTIVE_LEVEL GPIO_ACTIVE_HIGH

#define PCIE_X8_PERST           1
#define PCIE_X8_PERST_GPIO_CTR  "\\_SB.GPI4"
#define PCIE_X8_PERST_GPIO      1

#define PCIE_X4_PERST           1
#define PCIE_X4_PERST_GPIO_CTR  "\\_SB.GPI4"
#define PCIE_X4_PERST_GPIO      3

#define PCIE_X2_PERST           1
#define PCIE_X2_PERST_GPIO_CTR  "\\_SB.GPI4"
#define PCIE_X2_PERST_GPIO      4

#define PCIE_X1_1_PERST           1
#define PCIE_X1_1_PERST_GPIO_CTR  "\\_SB.GPI4"
#define PCIE_X1_1_PERST_GPIO      5

#define PCIE_X1_0_PERST           1
#define PCIE_X1_0_PERST_GPIO_CTR  "\\_SB.GPI4"
#define PCIE_X1_0_PERST_GPIO      2

#define PCIE_X8_VCC_SUPPLY 0
#define PCIE_X4_VCC_SUPPLY 0
#define PCIE_X2_VCC_SUPPLY 1
#define PCIE_X2_VCC_REGULATOR \_SB.PVC2
#define PCIE_X1_1_VCC_SUPPLY 1
#define PCIE_X1_1_VCC_REGULATOR \_SB.PVC3
#define PCIE_X1_0_VCC_SUPPLY 1
#define PCIE_X1_0_VCC_REGULATOR \_SB.PVC4

#define PCIE_X1_1_STR_PWRON 1
#define PCIE_X1_1_STD_PWRON 1
#define PCIE_X1_0_STR_PWRON 1
#define PCIE_X1_0_STD_PWRON 1

#define I2C3_EXT_CRS \
        PinGroupFunction(Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_fch_i2c3", ResourceConsumer,) \
        GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly, "\\_SB.GPI0", 0, ResourceConsumer) { 15, 16 }
#endif

#define I2C3_EXT_DSD_PROPERTY \
        Package () { "scl-gpios", Package () { ^I2C3, 0, 0, 0 } }, \
        Package () { "sda-gpios", Package () { ^I2C3, 0, 1, 0 } },
