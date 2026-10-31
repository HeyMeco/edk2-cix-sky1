## @file
#  Radxa Orion O6: board description.  Everything else is shared by
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
  PLATFORM_NAME                  = O6
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
  ACPI_IOMUX_INPUT               = Platform/Radxa/Orion/$(PLATFORM_NAME)/Drivers/AcpiPlatfomTables/RadxaO6Iomux.asl
  ACPI_IOMUX_OUTPUT              = $(OUTPUT_DIRECTORY)/Iomux.asl
  PREBUILD                       = python3 Platform/CIX/Sky1/Drivers/AcpiSocTables/tool/python3/ParseIomuxTemplate.py $(ACPI_IOMUX_INPUT) $(ACPI_IOMUX_OUTPUT)

!include  Platform/CIX/Sky1/Sky1Define.dsc.inc

################################################################################
#
# Board description
#
################################################################################
  DEFINE BOARD_DIR                  = Platform/Radxa/Orion/O6
  DEFINE BOARD_DTB_NAME             = sky1-orion-o6.dtb
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

##################################################
# Hardware (features are shared: Sky1Board.dsc.inc)
##################################################
  DEFINE I2C_EC_ENABLE              = TRUE     # ITE EC: fan, power button
  DEFINE POWER_BUTTON_ENABLE        = TRUE
  DEFINE PLATFORM_PD_ENABLE         = TRUE     # USB-C PD controllers (PcdPdDev*)
  DEFINE GPIO_POWER_BUTTON_ENABLE   = FALSE
  DEFINE SOC_GMAC_ENABLE            = FALSE    # Ethernet: RTL8126 on PCIe
  DEFINE SPI_VARIABLE_BASE          = 0x00388000
  DEFINE SPI_VARIABLE_SIZE          = 0x28000

!include Platform/CIX/Sky1/Sky1Board.dsc.inc

[LibraryClasses.common]
  AslUpdateLib|Silicon/Intel/IntelSiliconPkg/Library/DxeAslUpdateLib/DxeAslUpdateLib.inf
  PdEcLib|Platform/Radxa/Library/PdEcLibNull/PdEcLibNull.inf

[Components.common]
  Silicon/Intel/IntelSiliconPkg/Library/DxeAslUpdateLib/DxeAslUpdateLib.inf

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
  gCixTokenSpaceGuid.PcdPcieRootPort0LaneNum|0x03
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

  gCixTokenSpaceGuid.PcdI2c2En|FALSE
  gCixTokenSpaceGuid.PcdI2c2BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c3En|TRUE
  gCixTokenSpaceGuid.PcdI2c3Runtime|TRUE          # For RTC runtime service
  gCixTokenSpaceGuid.PcdI2c3BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c4En|TRUE
  gCixTokenSpaceGuid.PcdI2c4BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c5En|TRUE
  gCixTokenSpaceGuid.PcdI2c5BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c6En|TRUE
  gCixTokenSpaceGuid.PcdI2c6BusFreq|50000

  # PD
  gCixTokenSpaceGuid.PcdI2c0En|TRUE
  gCixTokenSpaceGuid.PcdI2c0BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c1En|TRUE
  gCixTokenSpaceGuid.PcdI2c1BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c7En|TRUE
  gCixTokenSpaceGuid.PcdI2c7BusFreq|100000

  # RTC I2C canot be controlled in setup
  gCixTokenSpaceGuid.PcdI2cCtrlEn|0xF7

  gCixPlatformTokenSpaceGuid.PcdPdDevI2cBuses|{ 0x1, 0xFF, 0x1, 0xFF }
  gCixPlatformTokenSpaceGuid.PcdPdDevI2cSlaveAddresses|{ 0x30, 0xFF, 0x31, 0xFF }
  gCixPlatformTokenSpaceGuid.PcdPdDevAlertPins|{ 9, 0xFF, 9, 0xFF }
  gCixPlatformTokenSpaceGuid.PcdTypecPortDefaultModes|{ 1, 4, 1, 4}

  # USB3_A
  gCixTokenSpaceGuid.PcdUsb3Control0Enable|TRUE
  gCixTokenSpaceGuid.PcdUsb3Control1Enable|TRUE

  # USBC0
  gCixTokenSpaceGuid.PcdUsbCDrdControl0Enable|TRUE
  gCixTokenSpaceGuid.PcdUsbCDrdControl0DataRole|FALSE
  # USBC1
  gCixTokenSpaceGuid.PcdUsbCControl0Enable|TRUE
  # USBC2
  gCixTokenSpaceGuid.PcdUsbCControl1Enable|TRUE
  # USBC3
  gCixTokenSpaceGuid.PcdUsbCControl2Enable|TRUE

  gCixTokenSpaceGuid.PcdUsb2Control0Enable|TRUE
  gCixTokenSpaceGuid.PcdUsb2Control1Enable|TRUE
  gCixTokenSpaceGuid.PcdUsb2Control2Enable|TRUE
  gCixTokenSpaceGuid.PcdUsb2Control3Enable|TRUE

  gCixTokenSpaceGuid.PcdAcpiUart0Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiUart1Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiUart2Enable|TRUE
  gCixTokenSpaceGuid.PcdAcpiUart3Enable|FALSE

  gCixTokenSpaceGuid.PcdAcpiI2s0Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiI2s3Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiI2s5Enable|TRUE
  gCixTokenSpaceGuid.PcdAcpiI2s6Enable|TRUE
  gCixTokenSpaceGuid.PcdAcpiI2s7Enable|TRUE
  gCixTokenSpaceGuid.PcdAcpiI2s8Enable|TRUE
  gCixTokenSpaceGuid.PcdAcpiI2s9Enable|TRUE
  gCixTokenSpaceGuid.PcdAudioHdaEn|TRUE

  gCixTokenSpaceGuid.PcdSpi0En|TRUE

  gCixPlatformTokenSpaceGuid.PcdEcAcpiI2cEn|TRUE
  gCixPlatformTokenSpaceGuid.PcdAcpiGpio0IoMask|0x20000000 # vbus for usb port6-7
  gCixPlatformTokenSpaceGuid.PcdAcpiGpio3IoMask|0x00018000 # pwm/edp en pin output

# Platform specific defaults
  # Set SMBIOS product name
  gArmTokenSpaceGuid.PcdSystemProductName|L"Radxa Orion O6"
  gRadxaTokenSpaceGuid.PcdBoardId|1

  # RTC (taken from Phecda PcdI2c3BusFreq)
  gHym8563RealTimeClockLibTokenSpaceGuid.PcdI2cBusFrequency|100000

  # Fill in dpu index to config display priority
  # Index | Output Name
  # ------|------------
  #     0 | USB-C 0
  #     1 | HDMI
  #     2 | eDP
  #     3 | USB-C 1
  #     4 | DP
  gCixTokenSpaceGuid.PcdDPPriority0|1 # highest priority
  gCixTokenSpaceGuid.PcdDPPriority1|4 #
  gCixTokenSpaceGuid.PcdDPPriority2|0 #
  gCixTokenSpaceGuid.PcdDPPriority3|3 #
  gCixTokenSpaceGuid.PcdDPPriority4|2 # lowest priority

  gCixPlatformTokenSpaceGuid.PcdAcpiPrefPmProf|0x01  # Desktop
  gCixTokenSpaceGuid.PcdAcpiCsiDmaEnable|FALSE

  # power on when AC returns (Radxa default)
  gCixTokenSpaceGuid.PcdStateAfterG3|0x01
