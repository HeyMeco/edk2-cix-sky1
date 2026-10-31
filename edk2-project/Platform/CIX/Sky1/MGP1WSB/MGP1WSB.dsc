# @file
#
#  Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved.
#  Copyright (c) 2011 - 2020, ARM Limited. All rights reserved.
#  Copyright (c) 2017 - 2018, Andrei Warkentin <andrey.warkentin@gmail.com>
#  Copyright (c) 2015 - 2021, Intel Corporation. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = MGP1WSB
  PLATFORM_GUID                  = BCBB466A-CF3B-A3F1-1FA5-A9E1C65D8C3F
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x0001001A
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Platform/CIX/Sky1/Sky1Board.fdf
  PCD_DYNAMIC_AS_DYNAMICEX       = TRUE
  BUILD_NUMBER                   = 0x00000005

!include  Platform/CIX/Sky1/Sky1Define.dsc.inc

################################################################################
#
# Board description.  Everything else is shared by the Sky1 boards
# (Platform/CIX/Sky1/Sky1Board.dsc.inc, Sky1Board.fdf).
#
################################################################################
  DEFINE BOARD_DIR                  = Platform/CIX/Sky1/MGP1WSB
  DEFINE BOARD_DTB_NAME             = SKY1-EVB.DTB
  DEFINE BOARD_CONFIG_PARAMS_HOOK_LIB = $(BOARD_DIR)/Library/ConfigParamsHookLib/ConfigParamsHookLib.inf
  DEFINE BOARD_ENV_HOOK_LIB         = $(BOARD_DIR)/Library/PlatformEnvHookLib/PlatformEnvHookLib.inf
  DEFINE BOARD_BOOT_HOOK_LIB        = $(BOARD_DIR)/Library/PlatformBootHookLib/PlatformBootHookLib.inf
  DEFINE BOARD_BOOT_MANAGER_LIB     = $(BOARD_DIR)/Library/PlatformBootManagerLib/PlatformBootManagerLib.inf
  DEFINE BOARD_RESET_LIB            = $(BOARD_DIR)/Library/ArmPsciResetSystemLib/ArmPsciResetSystemLib.inf
  DEFINE BOARD_EC_LIB               = Platform/CIX/Library/EcLibNull/EcLibNull.inf   # no EC
  DEFINE BOARD_ACPI_TABLES          = $(BOARD_DIR)/ACPI/AcpiPlatfomTables/AcpiPlatfomTables.inf
  DEFINE BOARD_ACPI_PLATFORM_DXE    = $(BOARD_DIR)/ACPI/AcpiPlatformDxe/AcpiPlatformDxe.inf
  DEFINE BOARD_LOGO_DXE             = $(BOARD_DIR)/Drivers/Logo/LogoDxe.inf
##################################################
# Hardware (features are shared: Sky1Board.dsc.inc)
##################################################
  DEFINE I2C_EC_ENABLE              = FALSE    # no EC
  DEFINE POWER_BUTTON_ENABLE        = FALSE
  DEFINE GPIO_POWER_BUTTON_ENABLE   = FALSE
  DEFINE PLATFORM_PD_ENABLE         = TRUE     # USB-C PD controllers (PcdPdDev*)
  DEFINE SOC_GMAC_ENABLE            = FALSE
  DEFINE SPI_VARIABLE_BASE          = 0x00380000
  DEFINE SPI_VARIABLE_SIZE          = 0x28000
  DEFINE LINUX_ACPI_CONFIG_OVERRIDE = TRUE

  # Discrete TPM 2.0 header on SPI1 (ASUS TPM-SPI / NPCT750 tested); bus and
  # chip select in PcdDTPMSpiBus / PcdDTPMSpiChipSelect below
  DEFINE SPI_TPM_ENABLE             = TRUE

!include Platform/CIX/Sky1/Sky1Board.dsc.inc

[PcdsFixedAtBuild.common]
!if $(TARGET) == RELEASE
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVersionString|L"$(UEFI_FW_STAGE)-W$(COMPILE_BUILD_DATE)-$(COMPILE_COMMIT_HASH)"
!else
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVersionString|L"$(UEFI_FW_STAGE)-D$(COMPILE_BUILD_DATE)-$(COMPILE_COMMIT_HASH)"
!endif
  gCixPlatformTokenSpaceGuid.PcdSiliconDtbUpdateFileName|L"SKY1-EVB.DTB"
  gCixPlatformTokenSpaceGuid.PcdSiliconDtbUpdateEnable|TRUE

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
  gCixTokenSpaceGuid.PcdPcieRootPort0PeResetPin|2
  gCixTokenSpaceGuid.PcdPcieRootPort1PeResetPin|4
  gCixTokenSpaceGuid.PcdPcieRootPort2PeResetPin|5
  gCixTokenSpaceGuid.PcdPcieRootPort3PeResetPin|6
  gCixTokenSpaceGuid.PcdPcieRootPort4PeResetPin|3

  gCixTokenSpaceGuid.PcdI2c0En|TRUE
  gCixTokenSpaceGuid.PcdI2c0BusFreq|400000
  gCixTokenSpaceGuid.PcdI2c2En|TRUE
  gCixTokenSpaceGuid.PcdI2c2BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c3En|TRUE
  gCixTokenSpaceGuid.PcdI2c3Runtime|FALSE          # For RTC runtime service
  gCixTokenSpaceGuid.PcdI2c3BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c4En|TRUE
  gCixTokenSpaceGuid.PcdI2c4BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c5En|TRUE
  gCixTokenSpaceGuid.PcdI2c5BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c6En|TRUE
  gCixTokenSpaceGuid.PcdI2c6BusFreq|50000
  #gCixTokenSpaceGuid.PcdI2c6Runtime|TRUE          # For platform specifc ec reset in runtime service
  gCixTokenSpaceGuid.PcdI2c6MutexId|0x48          # For mutex between ACPI EC and runtime ec reset

  # RTC I2C canot be controlled in setup
  gCixTokenSpaceGuid.PcdI2cCtrlEn|0xF7

  # PD
  gCixTokenSpaceGuid.PcdI2c1En|TRUE
  gCixTokenSpaceGuid.PcdI2c1BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c7En|TRUE
  gCixTokenSpaceGuid.PcdI2c7BusFreq|100000

  # USB3_A
  gCixTokenSpaceGuid.PcdUsb3Control0Enable|TRUE
  gCixTokenSpaceGuid.PcdUsb3Control0DataRole|FALSE
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

  gCixTokenSpaceGuid.PcdAudioHdaEn|TRUE

  gArmTokenSpaceGuid.PcdSystemMemorySize|0x400000000

  gEfiNetworkPkgTokenSpaceGuid.PcdNetworkStackSupport|FALSE
  gEfiNetworkPkgTokenSpaceGuid.PcdIPv4PXESupport|TRUE
  gEfiNetworkPkgTokenSpaceGuid.PcdIPv6PXESupport|TRUE
  gEfiNetworkPkgTokenSpaceGuid.PcdIPv4HttpSupport|TRUE
  gEfiNetworkPkgTokenSpaceGuid.PcdIPv6HttpSupport|TRUE

  gCixTokenSpaceGuid.PcdSpi0En|TRUE
# ISP Camera Power
  gCixTokenSpaceGuid.PcdIspCamera0Power|0x00
  gCixTokenSpaceGuid.PcdIspCamera1Power|0x00
  gCixTokenSpaceGuid.PcdIspCamera2Power|0x00
  gCixTokenSpaceGuid.PcdIspCamera3Power|0x00

  gCixPlatformTokenSpaceGuid.PcdEcAcpiI2cEn|TRUE
  gCixPlatformTokenSpaceGuid.PcdAcpiGpio3IoMask|0x00018000 # pwm/edp en pin output
  gCixPlatformTokenSpaceGuid.PcdAcpiGpio4IoMask|0x00180000 # wifi/bt disable pin output

  gCixTokenSpaceGuid.PcdSocWatchdogTimer|0x01

  gCixTokenSpaceGuid.PcdGmac0Enable|FALSE
  gCixTokenSpaceGuid.PcdGmac1Enable|FALSE

  gArmTokenSpaceGuid.PcdProcessorVersion|L""
  # Platform Flash Region for Save Vendor Defined Variable
!if $(BIOS_BACKUP_RECOVERY) == TRUE
  gCixPlatformTokenSpaceGuid.PcdBiosBackupRecovery|TRUE
  gCixPlatformTokenSpaceGuid.PcdNorFlashVarSyncRegionBase|0xFF0000   # SIZE_16MB-64KB
!else
  gCixPlatformTokenSpaceGuid.PcdNorFlashVarSyncRegionBase|0x7F0000   # SIZE_8MB-64KB
!endif
  gCixPlatformTokenSpaceGuid.PcdNorFlashVarSyncRegionSize|0x10000    # SIZE_64KB

# Touch Panel
  gCixPlatformTokenSpaceGuid.PcdAcpiGpio2IoMask|0x000F0000 # tp_pwr_en, tp_en, tp_reset and tp_int

# CSI_DMA
  gCixTokenSpaceGuid.PcdAcpiCsiDmaEnable|FALSE

  # PD Dev
  gCixPlatformTokenSpaceGuid.PcdPdDevCount|4|UINT8|0x0000013C
  # Refer to TYPEC_MODE enum
  gCixPlatformTokenSpaceGuid.PcdTypecPortDefaultModes|{ 1, 1, 4, 1 }|VOID*|0x0000013D # 0: disconnect, 1: usb only, 3: usb+dp 4: dp only
  # Refer to TYPEC_ORIENTATION enum
  gCixPlatformTokenSpaceGuid.PcdTypecPortDefaultOrientations|{ 1, 1, 1, 1 }|VOID*|0x0000013E # 0: none, 1: normal, 2: reverse
  gCixPlatformTokenSpaceGuid.PcdPdDevAlertPins|{ 8, 8, 0xFF, 0xFF }|VOID*|0x0000013F
  gCixPlatformTokenSpaceGuid.PcdPdDevI2cBuses|{ 0x7, 0x7, 0xFF, 0xFF }|VOID*|0x00000140 # 0xFF: not actual pd device
  gCixPlatformTokenSpaceGuid.PcdPdDevI2cSlaveAddresses|{ 0x30, 0x31, 0xFF, 0xFF }|VOID*|0x00000141  # 0xFF: not actual pd device

  gCixPlatformTokenSpaceGuid.PcdPowerButtonGpioNum|16


  gCixTokenSpaceGuid.PcdAcpiUart0Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiUart1Enable|FALSE

  gCixTokenSpaceGuid.PcdAcpiI2s5Enable|TRUE
  gCixTokenSpaceGuid.PcdAcpiI2s6Enable|TRUE
  gCixTokenSpaceGuid.PcdAcpiI2s8Enable|TRUE

  gCixTokenSpaceGuid.PcdCixProcessorVersion|L"CIX P1 CP8180"

#PCIe Max ASPM Support
  gCixTokenSpaceGuid.PcdPcieRootPort0MaxAspmSupport|0x02
  gCixTokenSpaceGuid.PcdPcieRootPort1MaxAspmSupport|0x02
  gCixTokenSpaceGuid.PcdPcieRootPort2MaxAspmSupport|0x02
  gCixTokenSpaceGuid.PcdPcieRootPort3MaxAspmSupport|0x02
  gCixTokenSpaceGuid.PcdPcieRootPort4MaxAspmSupport|0x02

#SPCR Settings
  gArmPlatformTokenSpaceGuid.PcdSerialDbgRegisterBase|0x040d0000
  gArmPlatformTokenSpaceGuid.PcdSerialDbgInterrupt|330

  gCixPlatformTokenSpaceGuid.PcdDTPMSupport|$(DTPM_SUPPORT)
  gCixPlatformTokenSpaceGuid.PcdFTPMSupport|$(FTPM_SUPPORT)
  # Minisforum's block diagram and GPIO datasheet (MS-R1-Docs/Datasheet): the
  # dTPM header is on SPI1 (the FCH controller at 0x04090000, CIX's SPI1_*
  # pads), SPI2 (0x040A0000) goes to the 40-pin GPIO header and SPI0 is the
  # boot flash (XSPI).  The TPM answers on chip select 1 (tested with an ASUS
  # TPM-SPI, NPCT750).
  gCixPlatformTokenSpaceGuid.PcdDTPMSpiBus|0x0          # 0:SPI1=0x04090000  1:SPI2=0x040A0000
  gCixPlatformTokenSpaceGuid.PcdDTPMSpiChipSelect|0x2   # 1:Slave Select 0   2:Slave Select 1

  gCixPlatformTokenSpaceGuid.PcdRegisterFastBootSupport|TRUE

  gEfiMdeModulePkgTokenSpaceGuid.PcdMaxSizeNonPopulateCapsule|0x1700000
  # set by the shared body, not used on this board
  gCixTokenSpaceGuid.PcdAcpiI2s9Enable|FALSE
  gCixTokenSpaceGuid.PcdI2c6Runtime|FALSE
