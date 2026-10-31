## @file
#  Radxa Orion O6N: board description.  Everything else is shared by
#  the Sky1 boards (Platform/CIX/Sky1/Sky1Board.dsc.inc, Sky1Board.fdf).
#
#  Copyright 2022 Cix Technology (Shanghai) Co., Ltd. All Rights Reserved.
#  Copyright (c) 2011 - 2020, ARM Limited. All rights reserved.
#  Copyright (c) 2017 - 2018, Andrei Warkentin <andrey.warkentin@gmail.com>
#  Copyright (c) 2015 - 2021, Intel Corporation. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = O6N
  PLATFORM_GUID                  = 53cfca21-0399-4802-a3c0-e86437a42183
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x0001001A
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Platform/CIX/Sky1/Sky1Board.fdf
  PCD_DYNAMIC_AS_DYNAMICEX       = TRUE
  BUILD_NUMBER                   = 0x00000005
  # pin multiplexing of the board's ACPI devices, generated from the template
  ACPI_IOMUX_INPUT               = Platform/Radxa/Orion/$(PLATFORM_NAME)/Drivers/AcpiPlatfomTables/Iomux.asl.template
  ACPI_IOMUX_OUTPUT              = $(OUTPUT_DIRECTORY)/Iomux.asl
  PREBUILD                       = python3 Platform/CIX/Sky1/Drivers/AcpiSocTables/tool/python3/ParseIomuxTemplate.py $(ACPI_IOMUX_INPUT) $(ACPI_IOMUX_OUTPUT)

!include  Platform/CIX/Sky1/Sky1Define.dsc.inc

################################################################################
#
# Board description
#
################################################################################
  DEFINE BOARD_DIR                  = Platform/Radxa/Orion/O6N
  DEFINE BOARD_DTB_NAME             = sky1-orion-o6n.dtb
  # Radxa flash layout: bootloader3 region 0x229000 at 0x406000
  DEFINE BOARD_FD_SIZE              = 0x00220000
  DEFINE BOARD_FD_BLOCKS            = 0x220
  # pins are set by the board's PlatformEnvHookLib
  DEFINE BOARD_PINMUX_LIB           = Silicon/CIX/Sky1/Library/PinMuxTableNullLib/PinMuxTableNullLib.inf
  DEFINE BOARD_GPIO_LIB             = Silicon/CIX/Sky1/Library/GpioTableNullLib/GpioTableNullLib.inf
  DEFINE BOARD_ENV_HOOK_LIB         = $(BOARD_DIR)/Library/PlatformEnvHookLib/PlatformEnvHookLib.inf
  DEFINE BOARD_PLATFORM_CONFIG_PARAMS_HOOK_LIB = $(BOARD_DIR)/Library/PlatformConfigParamsHookLib/PlatformConfigParamsHookLib.inf
  DEFINE BOARD_RTC_LIB              = Platform/Radxa/Library/Hym8563RealTimeClockLib/Hym8563RealTimeClockLib.inf
  DEFINE BOARD_RESET_LIB            = Platform/Radxa/Library/ArmPsciResetSystemLib/ArmPsciResetSystemLib.inf
  DEFINE BOARD_ACPI_TABLES          = $(BOARD_DIR)/Drivers/AcpiPlatfomTables/AcpiPlatfomTables.inf
  DEFINE BOARD_ACPI_PLATFORM_DXE    = Platform/Radxa/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf
  DEFINE BOARD_ACPI_CONFIG_DIR      = $(BOARD_DIR)/Drivers
  DEFINE BOARD_SMBIOS               = $(BOARD_DIR)/Drivers/PlatformSmbios/PlatformSmbios.inf
  DEFINE BOARD_LOGO_DXE             = Platform/Radxa/Drivers/Logo/LogoDxe.inf
  DEFINE LINUX_ACPI_CONFIG_OVERRIDE = TRUE
  DEFINE PCIE_RP0_X4_SLOT           = TRUE     # x8 root port wired to an x4 M.2 slot
  DEFINE BOARD_EC_LIB               = Platform/CIX/Library/EcLibNull/EcLibNull.inf
  DEFINE BOARD_FDF_DXE              = $(BOARD_DIR)/BoardDxe.fdf.inc
##################################################
# Hardware (features are shared: Sky1Board.dsc.inc)
##################################################
  DEFINE I2C_EC_ENABLE              = FALSE    # no EC; CSU_PM runs the fans
  DEFINE POWER_BUTTON_ENABLE        = FALSE
  DEFINE PLATFORM_PD_ENABLE         = FALSE    # PD through the CS32G051 (PdDxe below)
  DEFINE GPIO_POWER_BUTTON_ENABLE   = FALSE
  DEFINE SOC_GMAC_ENABLE            = FALSE    # Ethernet: RTL8126 on PCIe
  DEFINE SPI_VARIABLE_BASE          = 0x00388000
  DEFINE SPI_VARIABLE_SIZE          = 0x28000

!include Platform/CIX/Sky1/Sky1Board.dsc.inc

[LibraryClasses.common]
  AslUpdateLib|Silicon/Intel/IntelSiliconPkg/Library/DxeAslUpdateLib/DxeAslUpdateLib.inf
  PdEcLib|Platform/Radxa/Library/Cs32g051EcLib/Cs32g051EcLib.inf

[Components.common]
  Silicon/Intel/IntelSiliconPkg/Library/DxeAslUpdateLib/DxeAslUpdateLib.inf
  Platform/CIX/Sky1/Drivers/PdDxe/PdDxe.inf {
    <LibraryClasses>
      PdLib|Platform/Radxa/Library/Cs32g051PdLib/Cs32g051PdLib.inf
      RedriverLib|Platform/CIX/Sky1/Library/PI3DPX1207RedriverLib/PI3DPX1207RedriverLib.inf
  }

[PcdsFixedAtBuild.common]
  gCixTokenSpaceGuid.PcdPcieRootPort0Enable|TRUE
  gCixTokenSpaceGuid.PcdPcieRootPort1Enable|TRUE
  gCixTokenSpaceGuid.PcdPcieRootPort2Enable|TRUE
  gCixTokenSpaceGuid.PcdPcieRootPort3Enable|TRUE
  gCixTokenSpaceGuid.PcdPcieRootPort4Enable|TRUE
  gCixTokenSpaceGuid.PcdPcieRootPort0MaxSpeed|0x03
  gCixTokenSpaceGuid.PcdPcieRootPort1MaxSpeed|0x03
  gCixTokenSpaceGuid.PcdPcieRootPort2MaxSpeed|0x03
  gCixTokenSpaceGuid.PcdPcieRootPort3MaxSpeed|0x03
  gCixTokenSpaceGuid.PcdPcieRootPort4MaxSpeed|0x03
  gCixTokenSpaceGuid.PcdPcieRootPort0LaneNum|0x02     # M.2 M-key, x4
  gCixTokenSpaceGuid.PcdPcieRootPort1LaneNum|0x02
  gCixTokenSpaceGuid.PcdPcieRootPort2LaneNum|0x01
  gCixTokenSpaceGuid.PcdPcieRootPort3LaneNum|0x00
  gCixTokenSpaceGuid.PcdPcieRootPort4LaneNum|0x00
  gCixTokenSpaceGuid.PcdPcieRootPort0AspmSupport|2
  gCixTokenSpaceGuid.PcdPcieRootPort1AspmSupport|2
  gCixTokenSpaceGuid.PcdPcieRootPort2AspmSupport|2
  gCixTokenSpaceGuid.PcdPcieRootPort3AspmSupport|2
  gCixTokenSpaceGuid.PcdPcieRootPort4AspmSupport|2
  gCixTokenSpaceGuid.PcdPcieRootPort0PeResetPin|2
  gCixTokenSpaceGuid.PcdPcieRootPort1PeResetPin|4
  gCixTokenSpaceGuid.PcdPcieRootPort2PeResetPin|5
  gCixTokenSpaceGuid.PcdPcieRootPort3PeResetPin|3
  gCixTokenSpaceGuid.PcdPcieRootPort4PeResetPin|6

  gCixTokenSpaceGuid.PcdI2c0En|TRUE             # CAM 0
  gCixTokenSpaceGuid.PcdI2c0Runtime|FALSE
  gCixTokenSpaceGuid.PcdI2c0BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c1En|TRUE             # CAM 1
  gCixTokenSpaceGuid.PcdI2c1Runtime|FALSE
  gCixTokenSpaceGuid.PcdI2c1BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c2En|FALSE            # 40-pin
  gCixTokenSpaceGuid.PcdI2c2Runtime|FALSE
  gCixTokenSpaceGuid.PcdI2c2BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c3En|TRUE             # RTC
  gCixTokenSpaceGuid.PcdI2c3Runtime|TRUE
  gCixTokenSpaceGuid.PcdI2c3BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c4En|FALSE            # 40-pin
  gCixTokenSpaceGuid.PcdI2c4Runtime|FALSE
  gCixTokenSpaceGuid.PcdI2c4BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c5En|TRUE             # PD Charge
  gCixTokenSpaceGuid.PcdI2c5Runtime|FALSE
  gCixTokenSpaceGuid.PcdI2c5BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c6En|FALSE            # Board ID
  gCixTokenSpaceGuid.PcdI2c6Runtime|FALSE
  gCixTokenSpaceGuid.PcdI2c6BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c7En|TRUE             # EEPROM & PD Management
  gCixTokenSpaceGuid.PcdI2c7Runtime|FALSE
  gCixTokenSpaceGuid.PcdI2c7BusFreq|100000

  # RTC I2C canot be controlled in setup
  gCixTokenSpaceGuid.PcdI2cCtrlEn|0xF7

  gCixPlatformTokenSpaceGuid.PcdPdDevI2cBuses|{ 5, 0xFF, 0xFF, 0xFF }
  gCixPlatformTokenSpaceGuid.PcdPdDevI2cSlaveAddresses|{ 0x51, 0xFF, 0xFF, 0xFF }
  gCixPlatformTokenSpaceGuid.PcdPdDevAlertPins|{ 9, 0xFF, 0xFF, 0xFF }
  gCixPlatformTokenSpaceGuid.PcdTypecPortDefaultModes|{ 3, 4, 1, 4}
  gRadxaTokenSpaceGuid.PcdPdDevEcI2cBuses|{ 7, 0xFF, 0xFF, 0xFF }
  gRadxaTokenSpaceGuid.PcdPdDevEcI2cSlaveAddresses|{ 0x72, 0xFF, 0xFF, 0xFF }

  # USBC0
  gCixTokenSpaceGuid.PcdUsbCDrdControl0Enable|TRUE
  gCixTokenSpaceGuid.PcdUsbCDrdControl0DataRole|FALSE
  gCixTokenSpaceGuid.PcdUsbCDrdControl0MaxSpeed|3 # Workaround boot stucking under BIOS
  # USBC1
  gCixTokenSpaceGuid.PcdUsbCControl0Enable|TRUE
  # USBC2
  gCixTokenSpaceGuid.PcdUsbCControl1Enable|TRUE
  # USBC3
  gCixTokenSpaceGuid.PcdUsbCControl2Enable|TRUE

  # USB3_A
  gCixTokenSpaceGuid.PcdUsb3Control0Enable|TRUE
  gCixTokenSpaceGuid.PcdUsb3Control0DataRole|FALSE
  gCixTokenSpaceGuid.PcdUsb3Control1Enable|TRUE
  gCixTokenSpaceGuid.PcdUsb3Control1DataRole|FALSE

  # USB2
  gCixTokenSpaceGuid.PcdUsb2Control0Enable|TRUE
  gCixTokenSpaceGuid.PcdUsb2Control1Enable|TRUE
  gCixTokenSpaceGuid.PcdUsb2Control2Enable|TRUE
  gCixTokenSpaceGuid.PcdUsb2Control3Enable|TRUE

  gCixTokenSpaceGuid.PcdAcpiUart0Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiUart1Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiUart2Enable|TRUE
  gCixTokenSpaceGuid.PcdAcpiUart3Enable|FALSE

  gCixTokenSpaceGuid.PcdAcpiI2s0Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiI2s1Enable|FALSE # M.2 E-Key
  gCixTokenSpaceGuid.PcdAcpiI2s2Enable|FALSE # 40-pin
  gCixTokenSpaceGuid.PcdAcpiI2s3Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiI2s4Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiI2s5Enable|TRUE  # DP0
  gCixTokenSpaceGuid.PcdAcpiI2s6Enable|TRUE  # DP1
  gCixTokenSpaceGuid.PcdAcpiI2s7Enable|FALSE # DP2
  gCixTokenSpaceGuid.PcdAcpiI2s8Enable|FALSE # DP3
  gCixTokenSpaceGuid.PcdAcpiI2s9Enable|TRUE  # DP4
  gCixTokenSpaceGuid.PcdAudioHdaEn|FALSE     # N/A

  gCixTokenSpaceGuid.PcdSpi0En|TRUE

  gCixPlatformTokenSpaceGuid.PcdEcAcpiI2cEn|FALSE
  # DP2_DIGON & DP2_BLON
  gCixPlatformTokenSpaceGuid.PcdAcpiGpio3IoMask|0x00018000
  # UFS_5V_EN & USB_DRIVE_VBUS0 & USB_DRIVE_VBUS4 & USB_DRIVE_VBUS5 & PD_RESET
  gCixPlatformTokenSpaceGuid.PcdAcpiGpio4IoMask|0xE0002040

# Platform specific defaults
  # Set SMBIOS product name
  gArmTokenSpaceGuid.PcdSystemProductName|L"Radxa Orion O6N"
  gRadxaTokenSpaceGuid.PcdBoardId|2

  # RTC (taken from Phecda PcdI2c3BusFreq)
  gHym8563RealTimeClockLibTokenSpaceGuid.PcdI2cBusFrequency|100000

  # Fill in dpu index to config display priority
  # Index | Output Name
  # ------|------------
  #     0 | USB-C 0
  #     1 | HDMI
  #     2 | N/A
  #     3 | N/A
  #     4 | DisplayPort
  gCixTokenSpaceGuid.PcdDP0Enable|TRUE
  gCixTokenSpaceGuid.PcdDP1Enable|TRUE
  gCixTokenSpaceGuid.PcdDP2Enable|FALSE
  gCixTokenSpaceGuid.PcdDP3Enable|FALSE
  gCixTokenSpaceGuid.PcdDP4Enable|TRUE
  gCixTokenSpaceGuid.PcdDPPriority0|0
  gCixTokenSpaceGuid.PcdDPPriority1|4
  gCixTokenSpaceGuid.PcdDPPriority2|1
  gCixTokenSpaceGuid.PcdDPPriority3|2
  gCixTokenSpaceGuid.PcdDPPriority4|3

  gCixPlatformTokenSpaceGuid.PcdAcpiPrefPmProf|0x01  # Desktop
  gCixTokenSpaceGuid.PcdAcpiCsiDmaEnable|FALSE

  gRadxaTokenSpaceGuid.PcdUFSPowerMode|0x00
  gRadxaTokenSpaceGuid.PcdUFSPowerDetectGPIO|27
  gRadxaTokenSpaceGuid.PcdUFSPowerControlGPIO|14

  # power on when AC returns (Radxa default)
  gCixTokenSpaceGuid.PcdStateAfterG3|0x01
