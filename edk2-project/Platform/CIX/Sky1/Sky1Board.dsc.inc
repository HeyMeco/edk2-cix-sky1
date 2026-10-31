## @file
#  Shared body of the CIX Sky1 board DSCs.
#
#  Every board uses the same modules, libraries, build options and PCD
#  defaults from here; a board DSC only gives its [Defines] header, the
#  DEFINEs that describe its hardware (BOARD_DIR, BOARD_DTB_NAME,
#  PCIE_RP0_X4_SLOT, LINUX_ACPI_CONFIG_OVERRIDE, the BOARD_* module macros
#  and its feature switches) and, after the !include, the PCDs, components
#  and build options that are its own.  A PCD set again after this file
#  overrides the value here.  Macros that Sky1Define.dsc.inc already defines
#  (SPI_VARIABLE_*, CAPSULE_ENABLE, I2S_MC_SUPPORT, DTPM/FTPM_SUPPORT, ...)
#  must be set by the board DSC; !ifndef cannot give them a default here.
#
#  Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved.
#  Copyright (c) 2011 - 2020, ARM Limited. All rights reserved.
#  Copyright (c) 2017 - 2018, Andrei Warkentin <andrey.warkentin@gmail.com>
#  Copyright (c) 2015 - 2021, Intel Corporation. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

!ifndef PCIE_RP0_X4_SLOT
  DEFINE PCIE_RP0_X4_SLOT           = FALSE
!endif
!ifndef LINUX_ACPI_CONFIG_OVERRIDE
  DEFINE LINUX_ACPI_CONFIG_OVERRIDE = FALSE
!endif

################################################################################
#
# Define Macro section - list of all macro defined by this Platform.
# Override some marco define for platform requirement
#
################################################################################

!if $(COMPILE_FASTBOOT_LOAD) == nvme
  DEFINE PCIE_HOST_ENABLE           = TRUE
  DEFINE SOC_XSPI_ENABLE            = TRUE
  DEFINE FW_UPDATE_ENABLE           = TRUE
  DEFINE SOC_USB_DEVICE_ENABLE      = TRUE
  DEFINE SOC_CDNSP_ENABLE           = TRUE
!elseif $(COMPILE_FASTBOOT_LOAD) == ddr
  DEFINE PCIE_HOST_ENABLE           = FALSE
  DEFINE SOC_XSPI_ENABLE            = TRUE
  DEFINE FW_UPDATE_ENABLE           = TRUE
  DEFINE SOC_USB_DEVICE_ENABLE      = TRUE
  DEFINE SOC_CDNSP_ENABLE           = TRUE
!elseif $(COMPILE_FASTBOOT_LOAD) == usb
  DEFINE SOC_CDNSP_HOST_ENABLE      = TRUE
  DEFINE SOC_XSPI_ENABLE            = TRUE
  DEFINE FW_UPDATE_ENABLE           = TRUE
  DEFINE SOC_USB_DEVICE_ENABLE      = TRUE
  DEFINE SOC_CDNSP_ENABLE           = TRUE
!endif

!if $(COMPILE_SYSTEM_LOADER) == android
  DEFINE CAPSULE_ENABLE             = TRUE
  DEFINE OPTEE_AVB_ENABLE           = FALSE
!endif

#
# Network definition
#
!ifndef NETWORK_ENABLE
  DEFINE NETWORK_ENABLE                 = TRUE
!endif
!if $(NETWORK_ENABLE) == TRUE
  DEFINE NETWORK_IP4_ENABLE             = TRUE
  DEFINE NETWORK_SNP_ENABLE             = TRUE
  DEFINE NETWORK_TLS_ENABLE             = FALSE
  DEFINE NETWORK_IP6_ENABLE             = TRUE
  DEFINE NETWORK_HTTP_BOOT_ENABLE       = TRUE
  DEFINE NETWORK_HTTP_ENABLE            = TRUE
  DEFINE NETWORK_ALLOW_HTTP_CONNECTIONS = TRUE
  DEFINE NETWORK_ISCSI_ENABLE           = FALSE
!endif

#ACPI Boot
  DEFINE ACPI_ENABLE                = TRUE
  DEFINE SMBIOS_ENABLE              = TRUE



#
# Board modules: each board lives in $(BOARD_DIR) and may point any of these
# elsewhere before including this file.
#
!ifndef BOARD_PINMUX_LIB
  DEFINE BOARD_PINMUX_LIB           = $(BOARD_DIR)/Library/PinMuxTableLib/PinMuxTableLib.inf
!endif
!ifndef BOARD_GPIO_LIB
  DEFINE BOARD_GPIO_LIB             = $(BOARD_DIR)/Library/GpioTableLib/GpioTableLib.inf
!endif
!ifndef BOARD_ENV_HOOK_LIB
  DEFINE BOARD_ENV_HOOK_LIB         = Platform/CIX/Sky1/Merak/Library/PlatformEnvHookLib/PlatformEnvHookLib.inf
!endif
!ifndef BOARD_CONFIG_PARAMS_HOOK_LIB
  DEFINE BOARD_CONFIG_PARAMS_HOOK_LIB = Platform/CIX/Sky1/Library/ConfigParamsHookLib/ConfigParamsHookLib.inf
!endif
!ifndef BOARD_BOOT_MANAGER_LIB
  DEFINE BOARD_BOOT_MANAGER_LIB     = Platform/CIX/Sky1/Library/PlatformBootManagerLib/PlatformBootManagerLib.inf
!endif
!ifndef BOARD_BOOT_HOOK_LIB
  DEFINE BOARD_BOOT_HOOK_LIB        = Platform/CIX/Sky1/Merak/Library/PlatformBootHookLib/PlatformBootHookLib.inf
!endif
!ifndef BOARD_RESET_LIB
  DEFINE BOARD_RESET_LIB            = Platform/CIX/Sky1/Library/ArmPsciResetSystemLib/ArmPsciResetSystemLib.inf
!endif
!ifndef BOARD_EC_LIB
  DEFINE BOARD_EC_LIB               = Platform/CIX/Sky1/Library/Ite5570EcLib/Ite5570EcRuntimeLib.inf
!endif
!ifndef BOARD_ACPI_TABLES
  DEFINE BOARD_ACPI_TABLES          = $(BOARD_DIR)/ACPI/AcpiPlatformTables/AcpiPlatformTables.inf
!endif
!ifndef BOARD_ACPI_PLATFORM_DXE
  DEFINE BOARD_ACPI_PLATFORM_DXE    = Platform/CIX/Sky1/Merak/ACPI/AcpiPlatformDxe/AcpiPlatformDxe.inf
!endif
!ifndef BOARD_SMBIOS
  DEFINE BOARD_SMBIOS               = $(BOARD_DIR)/PlatformSmbios/PlatformSmbios.inf
!endif
!ifndef BOARD_FDF_DXE
  DEFINE BOARD_FDF_DXE              = Platform/CIX/Sky1/Sky1BoardNone.fdf.inc
!endif
!ifndef BOARD_FDF_DXE_EARLY
  DEFINE BOARD_FDF_DXE_EARLY        = Platform/CIX/Sky1/Sky1BoardNone.fdf.inc
!endif
!ifndef BOARD_DTB_UPDATE_DXE
  DEFINE BOARD_DTB_UPDATE_DXE       = TRUE
!endif
!ifndef BOARD_SETUP_MANAGER_DXE
  # TRUE: the board brings its own setup manager (needs the setup variable
  # driver in the APRIORI list even without TOKEN_SETUP_SUPPORT)
  DEFINE BOARD_SETUP_MANAGER_DXE    = FALSE
!endif

!include Platform/CIX/Sky1/Sky1Common.dsc.inc
!include NetworkPkg/NetworkDefines.dsc.inc

################################################################################
#
# Library Class section - list of all Library Classes needed by this Platform.
# Override some library for platform requirement
#
################################################################################

[LibraryClasses.common]
# PlatformConfigParamsHookBasicLib - RTC voltage drop detect + virtual Batura Board ID
# PlatformConfigParamsHookLib - RTC voltage drop detect + Get EC Board ID

  PinMuxTableLib|$(BOARD_PINMUX_LIB)
  GpioTableLib|$(BOARD_GPIO_LIB)
  PlatformConfigParamsHookLib|Platform/CIX/Sky1/Library/PlatformConfigParamsHookLib/PlatformConfigParamsHookLib.inf
  ConfigParamsHookLib|$(BOARD_CONFIG_PARAMS_HOOK_LIB)
  PlatformEnvHookLib|$(BOARD_ENV_HOOK_LIB)
  RealTimeClockLib|Platform/CIX/Sky1/Library/Ra8900ceRealTimeClockLib/Ra8900ceRealTimeClockLib.inf

  PlatformBootHookLib|$(BOARD_BOOT_HOOK_LIB)
  PlatformBootManagerLib|$(BOARD_BOOT_MANAGER_LIB)

  TrngLib|Silicon/CIX/Sky1/Library/TrngLib/TrngLib.inf
  RngLib|Silicon/CIX/Sky1/Library/RngLib/RngLib.inf
  DtbUpdateLibSi|Platform/CIX/Sky1/Library/DtbUpdateLibSi/DtbUpdateLib.inf
[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  EfiResetSystemLib|$(BOARD_RESET_LIB)
  EcLib|$(BOARD_EC_LIB)

################################################################################
#
# Components Section - list of all EDK II Modules needed by this Platform
# Add some INF components for platform requirement
#
################################################################################
[Components.common]
# Network stack
  !include NetworkPkg/Network.dsc.inc
# This modification is to fix a PXE bug.
# If the Code Base is upgraded, this modification will cause a compilation error and should be deleted.
  NetworkPkg/UefiPxeBcDxe/UefiPxeBcDxe.inf {
    <PcdsFixedAtBuild>
      gEfiNetworkPkgTokenSpaceGuid.PcdIPv4PXESupport|TRUE
      gEfiNetworkPkgTokenSpaceGuid.PcdIPv6PXESupport|TRUE
  }

  Platform/CIX/Sky1/PrePi/PeiUniCore.inf
!if $(SHELL_EMBEDDED_ENABLE) == TRUE
  ShellPkg/Application/Shell/Shell.inf {
    <LibraryClasses>
      ShellCommandLib|ShellPkg/Library/UefiShellCommandLib/UefiShellCommandLib.inf
      NULL|ShellPkg/Library/UefiShellLevel2CommandsLib/UefiShellLevel2CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellLevel1CommandsLib/UefiShellLevel1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellLevel3CommandsLib/UefiShellLevel3CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellDriver1CommandsLib/UefiShellDriver1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellDebug1CommandsLib/UefiShellDebug1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellInstall1CommandsLib/UefiShellInstall1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellAcpiViewCommandLib/UefiShellAcpiViewCommandLib.inf
      NULL|ShellPkg/Library/UefiShellNetwork1CommandsLib/UefiShellNetwork1CommandsLib.inf
      HandleParsingLib|ShellPkg/Library/UefiHandleParsingLib/UefiHandleParsingLib.inf
      PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
      BcfgCommandLib|ShellPkg/Library/UefiShellBcfgCommandLib/UefiShellBcfgCommandLib.inf

    <PcdsFixedAtBuild>
      gEfiShellPkgTokenSpaceGuid.PcdShellLibAutoInitialize|FALSE
      gEfiMdePkgTokenSpaceGuid.PcdUefiLibMaxPrintBufferSize|8000
      gEfiShellPkgTokenSpaceGuid.PcdShellFileOperationSize|0x200000
  }
!endif
  Platform/CIX/Sky1/Drivers/DtbUpdateDxeSi/DtbUpdateDxe.inf
!if $(ACPI_ENABLE) == TRUE
  $(BOARD_ACPI_TABLES)
  $(BOARD_ACPI_PLATFORM_DXE)
!endif
!if $(SMBIOS_ENABLE) == TRUE
  $(BOARD_SMBIOS)
!endif

###################################################################################################
# BuildOptions Section - Define the module specific tool chain flags that should be used as
#                        the default flags for a module. These flags are appended to any
#                        standard flags that are defined by the build process.
# add some BuildOptions for platform requirement
###################################################################################################
[BuildOptions]
  GCC:DEBUG_*_*_CC_FLAGS          = -DDEBUG_MODE
!if $(PCIE_RP0_X4_SLOT) == TRUE
  # Port0 (the x8 root port) is wired x4: no X8 width in setup
  GCC:*_*_*_VFRPP_FLAGS           = -DPCIE_RP0_X4_SLOT
!endif
!if $(DEBUG_MODE_SUPPORT) == TRUE
  GCC:RELEASE_*_*_CC_FLAGS        = -DDEBUG_MODE
  GCC:*_*_*_CC_FLAGS              = -DDEBUG_MODE_SUPPORT
  GCC:*_*_*_VFRPP_FLAGS           = -DDEBUG_MODE_SUPPORT
!else
  GCC:RELEASE_*_*_CC_FLAGS        = -DMDEPKG_NDEBUG -DNDEBUG
!endif

!if $(TARGET) == RELEASE
  GCC:*_*_*_CC_FLAGS              = -DUEFI_FW_VERSION=$(UEFI_FW_STAGE)"-W"$(COMPILE_BUILD_DATE)
!else
  GCC:*_*_*_CC_FLAGS              = -DUEFI_FW_VERSION=$(UEFI_FW_STAGE)"-D"$(COMPILE_BUILD_DATE)
!endif

!if $(COMPILE_FASTBOOT_LOAD) == nvme
  GCC:*_*_*_CC_FLAGS          = -DFASTBOOT_NVME
!if $(OPTEE_AVB_ENABLE) == TRUE
  GCC:*_*_*_CC_FLAGS          = -DOPTEE_AVB_ENABLE
!endif
!elseif $(COMPILE_FASTBOOT_LOAD) == ddr
  GCC:*_*_*_CC_FLAGS          = -DFASTBOOT_DDR
!endif

!if $(COMPILE_SMP_ENABLE) == 1
  GCC:*_*_*_CC_FLAGS              = -DMULTICORE_ENABLE
  GCC:*_*_*_ASLCC_FLAGS           = -DMULTICORE_ENABLE
  GCC:*_*_*_ASLPP_FLAGS           = -DMULTICORE_ENABLE
!endif

!if $(COMPILE_SYSTEM_LOADER) == android
  GCC:*_*_*_CC_FLAGS              = -DANDROID_BOOT
!endif

!if $(DYNAMIC_GET_MEM_SIZE) == TRUE
  GCC:*_*_*_PP_FLAGS              = -DUPDATE_MEMORY_TOP
  GCC:*_*_*_CC_FLAGS              = -DUPDATE_MEMORY_TOP
!endif

!if $(RAM_LOG_ENABLE) == TRUE
  GCC:*_*_*_CC_FLAGS              = -DCONFIG_RLOG_ENABLE
!endif

!if $(REBOOT_REASON_ENABLE) == TRUE
  GCC:*_*_*_CC_FLAGS              = -DREBOOT_REASON_ENABLE
!endif

!if $(NO_GIC_NO_TIMER) == TRUE
  GCC:*_*_*_CC_FLAGS              = -DNO_GIC_NO_TIMER
!endif

  GCC:*_*_*_CC_FLAGS              = -DPHYSICAL_PHY

!if $(FV_MAIN_COMPRESSED_ENABLE) == TRUE
  GCC:*_*_*_CC_FLAGS              = -DFV_MAIN_COMPRESSED
!endif

!if $(DYNAMIC_ACPI_CPU_ENABLE) == TRUE
  GCC:*_*_*_CC_FLAGS              = -DDYNAMIC_ACPI_CPU_ENABLE
  GCC:*_*_*_ASLPP_FLAGS           = -DDYNAMIC_ACPI_CPU_ENABLE
  GCC:*_*_*_ASLPP_FLAGS           = -DDYNAMIC_ACPI_CPU_ENABLE
!endif

!if $(SOC_GPIO_INTR_ENABLE) == TRUE
  GCC:*_*_*_CC_FLAGS              = -DSOC_GPIO_INTR_ENABLE
!endif

!if $(STMM_SUPPORT) == TRUE
  GCC:*_*_*_CC_FLAGS              = -DSTMM_SUPPORT
!endif

!ifdef FW_CONFIG_UPDATE_SUPPORT
!if $(FW_CONFIG_UPDATE_SUPPORT) == TRUE
  GCC:*_*_*_VFRPP_FLAGS           = -DFW_CONFIG_UPDATE_SUPPORT=1
!endif
!endif

!if $(LINUX_ACPI_CONFIG_OVERRIDE) == TRUE
  # Board DSDT configuration ($(BOARD_DIR)/ACPI/LinuxAcpiConfig.h)
  GCC:*_*_*_ASLPP_FLAGS           = -DLINUX_ACPI_CONFIG_OVERRIDE -I$(WORKSPACE)/edk2-project/$(BOARD_DIR)/ACPI
!endif

!if $(USERDATA_RESIZE) == enable
  GCC:*_*_*_CC_FLAGS              = -DUSERDATA_RESIZE_SUPPORT
!endif

################################################################################
#
# Pcd Section - list of all EDK II PCD Entries defined by this Platform
# Override some PCD define for platform requirement
#
################################################################################
[PcdsFixedAtBuild.common]
!if $(TARGET) == RELEASE
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVersionString|L"$(UEFI_FW_STAGE)-W$(COMPILE_BUILD_DATE)-$(COMPILE_COMMIT_HASH)"
!else
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVersionString|L"$(UEFI_FW_STAGE)-D$(COMPILE_BUILD_DATE)-$(COMPILE_COMMIT_HASH)"
!endif
  gCixPlatformTokenSpaceGuid.PcdSiliconDtbUpdateFileName|L"$(BOARD_DTB_NAME)"
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
  gCixTokenSpaceGuid.PcdPcieRootPort0LaneNum|0x02
  gCixTokenSpaceGuid.PcdPcieRootPort1LaneNum|0x02
  gCixTokenSpaceGuid.PcdPcieRootPort2LaneNum|0x00
  gCixTokenSpaceGuid.PcdPcieRootPort3LaneNum|0x00
  gCixTokenSpaceGuid.PcdPcieRootPort4LaneNum|0x00
  gCixTokenSpaceGuid.PcdPcieRootPort0PeResetPin|2
  gCixTokenSpaceGuid.PcdPcieRootPort1PeResetPin|4
  gCixTokenSpaceGuid.PcdPcieRootPort2PeResetPin|3
  gCixTokenSpaceGuid.PcdPcieRootPort3PeResetPin|6
  gCixTokenSpaceGuid.PcdPcieRootPort4PeResetPin|5

  gCixTokenSpaceGuid.PcdI2c0En|TRUE
  gCixTokenSpaceGuid.PcdI2c0BusFreq|400000
  gCixTokenSpaceGuid.PcdI2c2En|TRUE
  gCixTokenSpaceGuid.PcdI2c2BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c3En|TRUE
  gCixTokenSpaceGuid.PcdI2c3BusFreq|400000
  gCixTokenSpaceGuid.PcdI2c4En|TRUE
  gCixTokenSpaceGuid.PcdI2c4BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c5En|TRUE
  gCixTokenSpaceGuid.PcdI2c5BusFreq|100000
  gCixTokenSpaceGuid.PcdI2c6En|TRUE
  gCixTokenSpaceGuid.PcdI2c6BusFreq|50000
  gCixTokenSpaceGuid.PcdI2c6Runtime|TRUE          # For platform specifc ec reset in runtime service
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

  gCixTokenSpaceGuid.PcdAcpiI2s0Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiI2s3Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiI2s5Enable|TRUE
  gCixTokenSpaceGuid.PcdAcpiI2s6Enable|TRUE
  gCixTokenSpaceGuid.PcdAcpiI2s7Enable|FALSE
  gCixTokenSpaceGuid.PcdAcpiI2s8Enable|TRUE
  gCixTokenSpaceGuid.PcdAcpiI2s9Enable|TRUE
  gCixTokenSpaceGuid.PcdAudioHdaEn|TRUE

  gArmTokenSpaceGuid.PcdSystemMemorySize|0x400000000

  gEfiNetworkPkgTokenSpaceGuid.PcdNetworkStackSupport|FALSE
  gEfiNetworkPkgTokenSpaceGuid.PcdIPv4PXESupport|TRUE
  gEfiNetworkPkgTokenSpaceGuid.PcdIPv6PXESupport|TRUE
  gEfiNetworkPkgTokenSpaceGuid.PcdIPv4HttpSupport|TRUE
  gEfiNetworkPkgTokenSpaceGuid.PcdIPv6HttpSupport|TRUE

# ISP Camera Power
  gCixTokenSpaceGuid.PcdIspCamera0Power|0x00
  gCixTokenSpaceGuid.PcdIspCamera1Power|0x00
  gCixTokenSpaceGuid.PcdIspCamera2Power|0x00
  gCixTokenSpaceGuid.PcdIspCamera3Power|0x00

  gCixPlatformTokenSpaceGuid.PcdEcAcpiI2cEn|TRUE
  gCixPlatformTokenSpaceGuid.PcdAcpiGpio3IoMask|0x00018000 # pwm/edp en pin output

  gCixTokenSpaceGuid.PcdSocWatchdogTimer|0x01

  gCixTokenSpaceGuid.PcdGmac0Enable|FALSE
  gCixTokenSpaceGuid.PcdGmac1Enable|FALSE

  gCixTokenSpaceGuid.PcdCixProcessorVersion|L"CIX P1 CP8180"
  # Platform Flash Region for Save Vendor Defined Variable
!if $(BIOS_BACKUP_RECOVERY) == TRUE
  gCixPlatformTokenSpaceGuid.PcdBiosBackupRecovery|TRUE
  gCixPlatformTokenSpaceGuid.PcdNorFlashVarSyncRegionBase|0xFF0000   # SIZE_16MB-64KB
!else
  gCixPlatformTokenSpaceGuid.PcdNorFlashVarSyncRegionBase|0x7F0000   # SIZE_8MB-64KB
!endif
  gCixPlatformTokenSpaceGuid.PcdNorFlashVarSyncRegionSize|0x10000    # SIZE_64KB

  gCixPlatformTokenSpaceGuid.PcdDTPMSupport|$(DTPM_SUPPORT)
  gCixPlatformTokenSpaceGuid.PcdFTPMSupport|$(FTPM_SUPPORT)
  gCixPlatformTokenSpaceGuid.PcdDTPMSpiBus|0x0          # 0:SPI1=0x04090000  1:SPI2=0x040A0000
  gCixPlatformTokenSpaceGuid.PcdDTPMSpiChipSelect|0x1   # 1:Slave Select 0   2:Slave Select 1

  gCixPlatformTokenSpaceGuid.PcdRegisterFastBootSupport|TRUE

  gEfiMdeModulePkgTokenSpaceGuid.PcdMaxSizeNonPopulateCapsule|0x1700000

[PcdsDynamicDefault.common]

  gEmbeddedTokenSpaceGuid.PcdDmaDeviceLimit|0x47fffffff

  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|1920
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|1080
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoHorizontalResolution|800
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoVerticalResolution|600

  gCixPlatformTokenSpaceGuid.PcdDynamicUint64Test|0x11111111
!if $(COMPILE_SYSTEM_LOADER) == android
  gCixPlatformTokenSpaceGuid.AndroidFastboot|TRUE
!endif
  gEfiSecurityPkgTokenSpaceGuid.PcdTpmInstanceGuid|{0x5a, 0xf2, 0x6b, 0x28, 0xc3, 0xc2, 0x8c, 0x40, 0xb3, 0xb4, 0x25, 0xe6, 0x75, 0x8b, 0x73, 0x17}
  gEfiSecurityPkgTokenSpaceGuid.PcdActiveTpmInterfaceType|0x01

[PcdsDynamicHii.common.DEFAULT]
