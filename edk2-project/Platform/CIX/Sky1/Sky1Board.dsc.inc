## @file
#  Shared body of the CIX Sky1 board DSCs.
#
#  Every board gets the same firmware from here: modules, libraries, build
#  options, features (the "Shared features" block below) and PCD defaults.
#  A board DSC only describes its hardware: its [Defines] header, the
#  defines for its board modules and hardware (BOARD_DIR, BOARD_DTB_NAME,
#  BOARD_FD_SIZE, PCIE_RP0_X4_SLOT, I2C_EC_ENABLE, POWER_BUTTON_ENABLE,
#  PLATFORM_PD_ENABLE, SOC_GMAC_ENABLE, SPI_TPM_ENABLE, SPI_VARIABLE_*, the
#  BOARD_* module macros) and, after the !include, its own PCDs and
#  components.  A PCD set again after this file overrides the value here.
#
#  Macros that Sky1Define.dsc.inc already defines cannot get an !ifndef
#  default here: a hardware switch must be set by every board DSC, and a
#  shared feature is set below, after the board's defines.
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
!ifndef DEBUG_ON_UART3
  # TRUE: DEBUG() output on UART3 (40-pin header on the Orion boards), the
  # console stays on UART2
  DEFINE DEBUG_ON_UART3             = FALSE
!endif

################################################################################
#
# Shared features: the same on every board (docs/parity.md).
#
################################################################################
  DEFINE DTB_UPDATE_ENABLE          = FALSE
  DEFINE PLATFORM_DTB_UPDATE_ENABLE = TRUE    # DT boot: DTB file BOARD_DTB_NAME from disk
  DEFINE TOKEN_CONSOLE_PREF_SUPPORT = FALSE
  DEFINE FW_VERSION_ENABLE          = TRUE
  DEFINE SOC_PWR_CLK_RST_ENABLE     = TRUE
  DEFINE WATCH_DOG_ENABLE           = FALSE
  DEFINE NO_GIC_NO_TIMER            = FALSE
  DEFINE SOC_I2C_ENABLE             = TRUE
  DEFINE SOC_XSPI_ENABLE            = TRUE
  DEFINE SOC_SPI_ENABLE             = TRUE
  DEFINE SOC_GOP_ENABLE             = TRUE
  DEFINE SOC_CDNSP_HOST_ENABLE      = TRUE
  DEFINE PCIE_HOST_ENABLE           = TRUE
  DEFINE I2C_HID_ENABLE             = TRUE
  DEFINE FW_UPDATE_ENABLE           = TRUE
  DEFINE TOKEN_SETUP_SUPPORT        = TRUE    # the CIX setup interface (SetupManagerDxe)
  DEFINE NTFS_DRIVER_SUPPORT        = FALSE
  DEFINE EXT4_DRIVER_SUPPORT        = FALSE
  DEFINE AMD_GOP_DRIVER_SUPPORT     = TRUE
  DEFINE TOKEN_RAM_DISK_SUPPORT     = FALSE
  DEFINE VARIABLE_SUPPORT           = $(COMPILE_VARIABLE_TYPE)
  DEFINE STMM_SUPPORT               = $(COMPILE_STMM_SUPPORT)
  DEFINE REALTEK_LAN_DRIVER_SUPPORT = FALSE   # Sky1Board.fdf carries the Realtek drivers
  DEFINE MEM_CONFIG_UPDATE_SUPPORT  = TRUE
  DEFINE SE_CONFIG_UPDATE_SUPPORT   = TRUE
  DEFINE PM_CONFIG_UPDATE_SUPPORT   = FALSE
  DEFINE DYNAMIC_ACPI_CPU_ENABLE    = TRUE
  DEFINE FV_SIMPLE_FILE_SUPPORT     = FALSE
  DEFINE DYNAMIC_GET_MEM_SIZE       = TRUE
  DEFINE SECURE_BOOT_ENABLE         = TRUE
  DEFINE DEFAULT_KEYS               = TRUE
  DEFINE UEFI_FW_STAGE              = Beta2
  DEFINE BOOT_LOGO_ENABLE           = FALSE   # the board logo comes from BOARD_LOGO_DXE
  DEFINE FUNC_BOOT_PERF_ENABLE      = TRUE
  DEFINE DEBUG_MODE_SUPPORT         = FALSE
  DEFINE CIX_GPNV_ENABLE            = TRUE
  DEFINE BIOS_BACKUP_RECOVERY       = FALSE
  DEFINE CAPSULE_ENABLE             = FALSE   # no capsule updates: flash cix_flash_all.bin
  DEFINE I2S_MC_SUPPORT             = TRUE
  DEFINE DTPM_SUPPORT               = FALSE   # CIX's dTPM/fTPM drivers are not public;
  DEFINE FTPM_SUPPORT               = FALSE   # SPI_TPM_ENABLE is the discrete TPM
  DEFINE NETWORK_ENABLE             = TRUE    # built in; off until enabled in setup

  #
  # Never burn eFuses.  CIX's EfuseOemKeyDxe writes OemPublicKey.bin into the
  # SoC's OEM root-key eFuses on the first boot of a chip that has none, which
  # is irreversible and would bind the board to that key's boot chain.  UEFI
  # Secure Boot does not need it: its keys are variables in SPI flash.
  #
  DEFINE EFUSE_OEM_KEY_ENABLE       = FALSE

  #
  # Secure Boot defaults: Microsoft's keys (see SecureBootKeys/README.md), so
  # "Reset Secure Boot Keys" enables Windows and shim-signed Linux.  DBX stays
  # on the Sky1 default; Windows Update maintains it.
  #
  DEFINE MS_KEYS                    = Platform/CIX/Sky1/SecureBootKeys/Microsoft
  DEFINE PK_DEFAULT_FILE            = $(MS_KEYS)/WindowsOEMDevicesPK.der
  DEFINE KEK_DEFAULT_FILE1          = $(MS_KEYS)/MicCorKEKCA2011_2011-06-24.der
  DEFINE KEK_DEFAULT_FILE2          = $(MS_KEYS)/MicCorKEK2kCA2023.der
  DEFINE DB_DEFAULT_FILE1           = $(MS_KEYS)/MicWinProPCA2011_2011-10-19.der
  DEFINE DB_DEFAULT_FILE2           = $(MS_KEYS)/WindowsUEFICA2023.der
  DEFINE DB_DEFAULT_FILE3           = $(MS_KEYS)/MicCorUEFCA2011_2011-06-27.der
  DEFINE DB_DEFAULT_FILE4           = $(MS_KEYS)/MicUEFICA2023.der
  DEFINE DB_DEFAULT_FILE5           = $(MS_KEYS)/MicOptionROMUEFICA2023.der

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
!ifndef BOARD_PLATFORM_CONFIG_PARAMS_HOOK_LIB
  DEFINE BOARD_PLATFORM_CONFIG_PARAMS_HOOK_LIB = Platform/CIX/Sky1/Library/PlatformConfigParamsHookLib/PlatformConfigParamsHookLib.inf
!endif
!ifndef BOARD_RTC_LIB
  DEFINE BOARD_RTC_LIB              = Platform/CIX/Sky1/Library/Ra8900ceRealTimeClockLib/Ra8900ceRealTimeClockLib.inf
!endif
!ifndef BOARD_LOGO_DXE
  DEFINE BOARD_LOGO_DXE             = Platform/CIX/Sky1/Drivers/Logo/LogoDxe.inf
!endif
!ifndef BOARD_ACPI_CONFIG_DIR
  # LinuxAcpiConfig.h of the board (LINUX_ACPI_CONFIG_OVERRIDE)
  DEFINE BOARD_ACPI_CONFIG_DIR      = $(BOARD_DIR)/ACPI
!endif
!ifndef BOARD_FD_SIZE
  # BL33 FD size and its number of 4 KiB blocks; must fit the bootloader3
  # region of the flash layout
  DEFINE BOARD_FD_SIZE              = 0x001f0000
  DEFINE BOARD_FD_BLOCKS            = 0x1f0
!endif
!ifndef SPI_TPM_ENABLE
  DEFINE SPI_TPM_ENABLE             = FALSE
!endif
!ifndef BOARD_UFS_POWER
  # TRUE: switchable UFS module socket; setup shows "UFS Power Mode" and the
  # board's platform code applies it (PLATFORM_SETUP_DATA.UfsPowerMode)
  DEFINE BOARD_UFS_POWER            = FALSE
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
  PlatformConfigParamsHookLib|$(BOARD_PLATFORM_CONFIG_PARAMS_HOOK_LIB)
  ConfigParamsHookLib|$(BOARD_CONFIG_PARAMS_HOOK_LIB)
  PlatformEnvHookLib|$(BOARD_ENV_HOOK_LIB)
  RealTimeClockLib|$(BOARD_RTC_LIB)

  PlatformBootHookLib|$(BOARD_BOOT_HOOK_LIB)
  PlatformBootManagerLib|$(BOARD_BOOT_MANAGER_LIB)
  # Firmware Performance Data Table (FPDT) through the status code router
  PerformanceLib|MdePkg/Library/BasePerformanceLibNull/BasePerformanceLibNull.inf
  ReportStatusCodeLib|MdeModulePkg/Library/DxeReportStatusCodeLib/DxeReportStatusCodeLib.inf

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
  $(BOARD_ACPI_TABLES) {
    <BuildOptions>
      # generated ACPI includes (e.g. Iomux.asl from a PREBUILD step)
      *_*_*_ASLCC_FLAGS = -I$(WORKSPACE)/$(OUTPUT_DIRECTORY)
  }
  $(BOARD_ACPI_PLATFORM_DXE)
!endif
!if $(SMBIOS_ENABLE) == TRUE
  $(BOARD_SMBIOS)
!endif

  $(BOARD_LOGO_DXE)
  Silicon/CIX/Sky1/Drivers/GenericWatchdogDxe/GlobalWatchdogDxe.inf
  Platform/CIX/Sky1/Drivers/RealtekLan/RealtekLanDxe.inf

  #
  # Firmware Performance Data Table (FPDT)
  #
  MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  MdeModulePkg/Universal/Acpi/FirmwarePerformanceDataTableDxe/FirmwarePerformanceDxe.inf {
    <LibraryClasses>
      LockBoxLib|MdeModulePkg/Library/LockBoxNullLib/LockBoxNullLib.inf
  }
  MdeModulePkg/Universal/Disk/RamDiskDxe/RamDiskDxe.inf

!if $(DEBUG_ON_UART3) == TRUE
  MdeModulePkg/Universal/SerialDxe/SerialDxe.inf {
    <PcdsFixedAtBuild>
      gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x040D0000
  }
!endif

  #
  # Discrete TPM 2.0 on an SPI bus (Tpm2DeviceLibCdnsSpi): the board sets
  # SPI_TPM_ENABLE and gCixPlatformTokenSpaceGuid.PcdDTPMSpiBus /
  # PcdDTPMSpiChipSelect.  Without a TPM answering, both drivers stay idle.
  #
!if $(SPI_TPM_ENABLE) == TRUE
  SecurityPkg/Tcg/Tcg2Dxe/Tcg2Dxe.inf {
    <LibraryClasses>
      Tpm2DeviceLib|Platform/CIX/Sky1/Library/Tpm2DeviceLibCdnsSpi/Tpm2DeviceLibCdnsSpi.inf
      Tpm2CommandLib|SecurityPkg/Library/Tpm2CommandLib/Tpm2CommandLib.inf
      HashLib|SecurityPkg/Library/HashLibBaseCryptoRouter/HashLibBaseCryptoRouterDxe.inf
      NULL|SecurityPkg/Library/HashInstanceLibSha1/HashInstanceLibSha1.inf
      NULL|SecurityPkg/Library/HashInstanceLibSha256/HashInstanceLibSha256.inf
      NULL|SecurityPkg/Library/HashInstanceLibSha384/HashInstanceLibSha384.inf
  }
  Platform/CIX/Sky1/Drivers/SpiTpmAcpiDxe/SpiTpmAcpiDxe.inf {
    <LibraryClasses>
      Tpm2DeviceLib|SecurityPkg/Library/Tpm2DeviceLibTcg2/Tpm2DeviceLibTcg2.inf
      Tpm2CommandLib|SecurityPkg/Library/Tpm2CommandLib/Tpm2CommandLib.inf
      TpmPlatformHierarchyLib|SecurityPkg/Library/PeiDxeTpmPlatformHierarchyLib/PeiDxeTpmPlatformHierarchyLib.inf
  }
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

# Setup shows only what the board has (patches 0014, 0015 in edk2-platforms)
!if $(I2C_EC_ENABLE) == FALSE
  GCC:*_*_*_VFRPP_FLAGS           = -DBOARD_NO_EC
!endif
!if $(SOC_GMAC_ENABLE) == FALSE
  GCC:*_*_*_VFRPP_FLAGS           = -DBOARD_NO_GMAC
!endif
!if $(BOARD_UFS_POWER) == TRUE
  GCC:*_*_*_VFRPP_FLAGS           = -DBOARD_UFS_POWER
!endif

!ifdef FW_CONFIG_UPDATE_SUPPORT
!if $(FW_CONFIG_UPDATE_SUPPORT) == TRUE
  GCC:*_*_*_VFRPP_FLAGS           = -DFW_CONFIG_UPDATE_SUPPORT=1
!endif
!endif

!if $(LINUX_ACPI_CONFIG_OVERRIDE) == TRUE
  # Board DSDT configuration ($(BOARD_ACPI_CONFIG_DIR)/LinuxAcpiConfig.h)
  GCC:*_*_*_ASLPP_FLAGS           = -DLINUX_ACPI_CONFIG_OVERRIDE -I$(WORKSPACE)/edk2-project/$(BOARD_ACPI_CONFIG_DIR)
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

  # Network stack built in, off until "Network Stack" is enabled in setup
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

  # SPCR: the console UART (UART2)
  gArmPlatformTokenSpaceGuid.PcdSerialDbgRegisterBase|0x040D0000
  gArmPlatformTokenSpaceGuid.PcdSerialDbgInterrupt|330
  gCixPlatformTokenSpaceGuid.PcdAcpiSpcrEnable|TRUE
!if $(DEBUG_ON_UART3) == TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x040E0000
!endif

  # Firmware Performance Data Table (FPDT)
  gEfiMdePkgTokenSpaceGuid.PcdPerformanceLibraryPropertyMask|1
  gEfiMdePkgTokenSpaceGuid.PcdReportStatusCodePropertyMask|0x07
  gEfiMdeModulePkgTokenSpaceGuid.PcdExtFpdtBootRecordPadSize|0x300000

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
