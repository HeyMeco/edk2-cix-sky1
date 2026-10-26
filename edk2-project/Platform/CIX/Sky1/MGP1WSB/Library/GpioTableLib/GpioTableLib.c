/** @file

  Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "GpioTableInternal.h"

GPIO_CFG  *mGpioCfgTable;
UINT32    mGpioCfgTableLength;

GPIO_CFG  GpioCfgTable[] = {
  // { 1,   INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_DISABLE,        EDGE_LOW               }, // EC_HID_INT_L
  //{ 2,   OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // SLT_X8_RST_L
  //{ 3,   OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // SLT_X4_RST_L/M2_WWAN_RST_L
  //{ 4,   OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // M2_NVME_RST_L
  //{ 5,   OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // M2_WLAN_RST_L
  //{ 6,   OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // LOM_RST_L
  // { 7,   INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_DISABLE,        EDGE_LOW               }, // EC_ACPI_INT_L
  //{ 8,   INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_DISABLE,        LEVEL_LOW              }, // I2C_INT_CPU_PD1
  // { 9,   INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_DISABLE,        LEVEL_LOW              }, // I2C_INT_CPU_PD2
  //{ 10,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              }, // TPAD_INT_L
  { 11,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              }, // RTC_ALARM_L
  { 12,  OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // M2_SSD_PWREN
  { 13,  OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // WLAN_PWREN
  { 14,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              }, // HP_MIC_DET#/EP_X8_RST_L
  //{ 30,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // LOM_PEWAKE_L
  //{ 31,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // SLT_X8_PEWAKE_L
  { 32,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // WLAN_WAKE_L
  { 33,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // SLT_X4_PEWAKE_L
  //{ 40,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // UART_WAKE#_M2_SOC
  { 43,  INPUT,            INOUT_VALUE_DEFAULT,  INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW }, // GMAC0_PHY_RESET_L
  { 44,  OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // GMAC1_PHY_RESET_L
  { 45,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              }, // GMAC0_INT_L
  { 46,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              }, // GMAC1_INT_L
  { 81,  OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // IR_EN
  //{ 82,  OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // CAM2_PWDN
  //{ 83,  OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // CAM2_PWREN
  //{ 84,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              }, // TPM_INT_L
  //{ 85,  OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // CAM2_RST_L
  { 86,  OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // CAM3_PWREN
  { 87,  OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // CAM0_RST_L
  //{ 88,  OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // CAM3_RST_L
  //{ 89,  OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // CAM3_PWDN
  { 90,  OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // CAM0_PWDN
  { 91,  OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // CAM0_PWREN
  { 92,  OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // CAM1_RST_L
  { 93,  OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // CAM1_PWREN
  { 94,  OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // CAM1_PWDN
  // NONGMAC1
  //{ 136, INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              }, // TPNL_INT_L
  { 137, INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              }, // NFC_INT_L
  { 143, INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              }, // I2C_NVME_ALERT_L
  { 144, OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // PA0_PDB
  { 145, OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // PA1_PDB
  { 146, OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // PA2_PDB
  { 147, OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // PA3_PDB
  { 148, INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              }, // I2C_WWAN_ALERT_L
  { 149, INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              }, // I2S0_INT
  { 150, INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              }, // I2S2_INT


  // P1WSB porting>>>
  { 16,   INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_DISABLE,        EDGE_LOW               },
  // GPIO135/GMAC0_MDIO    1.8V    S0          Pull Up: 20k-44k        X8_PRSNT_L  PU 10K              GPI: Slot device plugin PRSNT, active high  EVB为GMAC
  // GPIO002   1.8V    S5  Push-Pull   O   Pull Down: 20K-47K      SLT_X16_RST_L   /       GPO Low GPO: PCIEx16 Slot reset, active low
  { 135, INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_DISABLE,        INTERRUPT_TYPE_DEFAULT },
  { 2,   OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // SLT_X8_RST_L
  { 3,   OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // LOM1_RST_L      // SLT_X4_RST_L/M2_WWAN_RST_L
  { 4,   OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // M2_NVME_RST_L

  { 6,   OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // LOM2_RST_L      // LOM_RST_L
  // { 7,   INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_DISABLE,        EDGE_LOW               }, // EC_ACPI_INT_L

  { 8,   INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_DISABLE,        LEVEL_LOW              }, // I2C_INT_CPU_PD1
  // { 9,   INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_DISABLE,        LEVEL_LOW              }, // I2C_INT_CPU_PD2
  // GPIO010   1.8V    S5  Push-Pull   I   Pull Up: 20k-44k    WLAN wakeup WLAN_WAKE_L PU 10K              GPI: WLAN wakeup SOC, active low    EVB为TPAD_INT_L
  // GPIO005   1.8V    S5  Push-Pull   O   Pull Down: 20K-47K      M2_WLAN_RST_L   /       GPO High    GPO: WLAN Reset, active low
  // GPIO030/USB_OC0_L 1.8V    S5  Push-Pull   I   Pull Up: 20k-44k    BT Radio disable    BT_RADIO_DISABLE_L  PU 20K      GPI Low GPO: BT Radio disable, active high    EVB为USB OC
  // GPIO031/USB_OC1_L 1.8V    S5  Push-Pull   Bi  Pull Up: 20k-44k    WLAN Radio disable  WL_RADIO_DISABLE_L  PU 20K              GPO: WLAN Radio disable, active high    EVB为USB OC
  // GPIO042/USB_DRIVE_VBUS5   1.8V    S5  Push-Pull   Bi  Pull Down: 20K-47K  WLAN Power Enable   WLAN_PWREN  PD 4.7K         Low GPO: WLAN Power enable, active high    EVB为USB DRIVE BUS
  { 10,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // WLAN_WAKE_L        // TPAD_INT_L
  { 5,   OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // M2_WLAN_RST_L   // M2_WLAN_RST_L
  { 30,  OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // BT_RADIO_DISABLE_L // LOM_PEWAKE_L  //
  { 31,  OUTPUT,            INOUT_LOW,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // WL_RADIO_DISABLE_L // SLT_X8_PEWAKE_L //
  { 42,  OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, // WLAN Power Enable

  // GPIO015   1.8V    S5  Push-Pull   I   Pull Up: 20k-44k    LAN2 wakeup LOM2_PEWAKE_L                   GPI: LAN2 wakeup SOC, active low    EVB is SFI SPI
  { 15,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT },
  // SFI_GPIO3/GPIO018 1.8V    S5  Push-Pull   I   Pull Up: 20k-44k        TPM_INT_L                   GPI: SPI INT for dTPM   EVB is SFI GPIO
  // GPIO007   1.8V    S5  Push-Pull   I   Pull Up: 20k-44k    EC ACPI I2C interrupt   TPM_STR_L                   GPO: TPM Reset, active high EVB is EC ACPI INT
  { 18,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, LEVEL_LOW              },
  { 7,   OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, //

  // mem ID GPIO082 083 084 085 088 089
  { 82,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT },       // CAM2_PWDN
  { 83,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT },       // CAM2_PWREN
  { 84,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT },       // TPM_INT_L
  { 85,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT },       // CAM2_RST_L
  { 88,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT },       // CAM3_RST_L
  { 89,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT },       // CAM3_PWDN

  // NONGMAC1

  // GPIO003   1.8V    S5  Push-Pull   O   Pull Down: 20K-47K  LAN1 reset  LOM1_RST_L          GPO Low GPO: LAN1 reset, active low EVB为PCIEx4 slot Reset
  // GPIO017   1.8V    S5  Push-Pull   I   Pull Up: 20k-44k    LAN1 wakeup LOM1_PEWAKE_L                   GPI: LAN1 wakeup SOC, active low    EVB为SFI SPI
  // GPIO040/USB_DRIVE_VBUS0   1.8V    S5  Push-Pull   I   Pull Down: 20K-47K  LAN1 disable    LOM1_PWREN                  Reserve EVB为UART Wake
  { 17,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT },
  { 40,  OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT },
  // GPIO006   1.8V    S5  Push-Pull   O   Pull Down: 20K-47K  LAN2 reset  LOM2_RST_L          GPO Low GPO: LAN2 reset, active low
  // GPIO015   1.8V    S5  Push-Pull   I   Pull Up: 20k-44k    LAN2 wakeup LOM2_PEWAKE_L                   GPI: LAN2 wakeup SOC, active low    EVB为SFI SPI
  // GPIO041/USB_DRIVE_VBUS4   1.8V    S5  Push-Pull   Bi  Pull Down: 20K-47K  LAN2 disable    LOM2_PWREN  PD 100K         Low Reserve EVB为USB DRIVE BUS

  { 15,  INPUT,             INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT },
  { 41,  OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT },

  // GPIO136/GMAC1_REFCLK_25M  1.8V    S0      I   Pull Down: 20K-47K  TP I2C interrupt    TPNL_INT_L  PU 10k              GPI: I2C INT for TP GPIO与EVB NOGMAC时一致
  // GPIO124/GMAC0_TXD1    1.8V    S0          Pull Down: 20K-47K  TP Reset    TPNL_RST_L  PD 100k             GPO: TP Reset, active low   EVB为GMAC. 与IO扩展（0x26h）的IO0_6用法一致
  // GPIO125/GMAC0_TXD2    1.8V    S0          Pull Down: 20K-47K  TP Enable   TPNL_EN PD 100k             GPO: TP Enable, active high EVB为GMAC. 与IO扩展（0x26h）的IO0_4用法一致
  // GPIO123/GMAC0_TXD0    1.8V    S0          Pull Down: 20K-47K  TP Power EN TPNL_PWR_EN PD 100k             GPO: TP Power Enable, active high   EVB为GMAC. 与IO扩展（0x26h）的IO0_3用法一致


  { 123, OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, //  TPNL_PWR_EN
  { 124, OUTPUT,            INOUT_HIGH,           INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, //  TPNL_RST_L
  { 125, OUTPUT,            INOUT_HIGH,          INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }, //  TPNL_EN
  { 126, INPUT,             INOUT_HIGH,          INTERRUPT_DISABLE,         LEVEL_LOW              }, // TPNL_INT_L


  { 1,   OUTPUT,            INOUT_HIGH,          INTERRUPT_DISABLE,        INTERRUPT_TYPE_DEFAULT }, // EC_HID_INT_L
  // <<<P1WSB porting
  // the end of GpioCfgTable, no more item allowed to add afterward
  { 0,   DIRECTION_DEFAULT, INOUT_VALUE_DEFAULT, INTERRUPT_ENABLE_DEFAULT, INTERRUPT_TYPE_DEFAULT }
};

/**
  The constructor function includes platform gpio configuration table.

  @param  ImageHandle   The firmware allocated handle for the EFI image.
  @param  SystemTable   A pointer to the EFI System Table.

  @retval EFI_SUCCESS   The constructor always returns RETURN_SUCCESS.

**/
RETURN_STATUS
EFIAPI
GpioTableLibConstructor (
  VOID
  )
{
  EFI_STATUS  Status = EFI_SUCCESS;

  mGpioCfgTable       = GpioCfgTable;
  mGpioCfgTableLength = ARRAY_SIZE (GpioCfgTable);

  return Status;
}
