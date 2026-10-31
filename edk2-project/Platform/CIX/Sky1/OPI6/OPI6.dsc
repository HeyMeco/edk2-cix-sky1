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
  PLATFORM_NAME                  = OPI6
  PLATFORM_GUID                  = 8E2A6D40-31C7-4B0E-A95F-6C1D7E3B2F94
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
  DEFINE BOARD_DIR                  = Platform/CIX/Sky1/OPI6
  DEFINE BOARD_DTB_NAME             = SKY1-OPI6.DTB
  DEFINE PCIE_RP0_X4_SLOT           = TRUE    # x8 root port wired to an x4 M.2 slot
  DEFINE LINUX_ACPI_CONFIG_OVERRIDE = TRUE    # ACPI/LinuxAcpiConfig.h
  DEFINE BOARD_ENV_HOOK_LIB         = $(BOARD_DIR)/Library/PlatformEnvHookLib/PlatformEnvHookLib.inf
##################################################
# Orange Pi 6: features
##################################################
  DEFINE DTB_UPDATE_ENABLE          = FALSE
  DEFINE PLATFORM_DTB_UPDATE_ENABLE = TRUE
  DEFINE SMBIOS_ENABLE              = FALSE
  DEFINE ACPI_ENABLE                = FALSE
  DEFINE TOKEN_CONSOLE_PREF_SUPPORT = FALSE
  DEFINE FW_VERSION_ENABLE          = TRUE
  DEFINE SOC_PWR_CLK_RST_ENABLE     = TRUE
  DEFINE WATCH_DOG_ENABLE           = FALSE
  DEFINE NO_GIC_NO_TIMER            = FALSE
  DEFINE SOC_I2C_ENABLE             = TRUE
  DEFINE SOC_XSPI_ENABLE            = TRUE
  DEFINE I2C_EC_ENABLE              = FALSE
  DEFINE I2C_HID_ENABLE             = TRUE
  DEFINE FW_UPDATE_ENABLE           = TRUE
  DEFINE PCIE_HOST_ENABLE           = TRUE
  DEFINE SOC_CDNSP_HOST_ENABLE      = TRUE
  DEFINE PLATFORM_PD_ENABLE         = TRUE
  DEFINE SOC_GMAC_ENABLE            = FALSE
  DEFINE TOKEN_SETUP_SUPPORT        = TRUE
  DEFINE NTFS_DRIVER_SUPPORT        = FALSE
  DEFINE EXT4_DRIVER_SUPPORT        = FALSE
  DEFINE AMD_GOP_DRIVER_SUPPORT     = TRUE
  DEFINE TOKEN_RAM_DISK_SUPPORT     = FALSE
  DEFINE VARIABLE_SUPPORT           = $(COMPILE_VARIABLE_TYPE)
  DEFINE STMM_SUPPORT               = $(COMPILE_STMM_SUPPORT)
  DEFINE REALTEK_LAN_DRIVER_SUPPORT = TRUE
  DEFINE PM_CONFIG_UPDATE_SUPPORT   = FALSE
  DEFINE SE_CONFIG_UPDATE_SUPPORT   = TRUE
  DEFINE DYNAMIC_ACPI_CPU_ENABLE    = TRUE
  DEFINE SOC_SPI_ENABLE             = TRUE
  # DEFINE SOC_GPIO_INTR_ENABLE       = TRUE
  DEFINE FV_SIMPLE_FILE_SUPPORT     = FALSE
  DEFINE SOC_GOP_ENABLE             = TRUE
  DEFINE DYNAMIC_GET_MEM_SIZE       = TRUE
  DEFINE SECURE_BOOT_ENABLE         = TRUE
  DEFINE DEFAULT_KEYS               = TRUE
  DEFINE UEFI_FW_STAGE              = Beta2
  DEFINE BOOT_LOGO_ENABLE           = TRUE
  DEFINE FUNC_BOOT_PERF_ENABLE      = TRUE
  DEFINE POWER_BUTTON_ENABLE        = FALSE
  DEFINE DEBUG_MODE_SUPPORT         = FALSE
  DEFINE CIX_GPNV_ENABLE            = TRUE
  DEFINE BIOS_BACKUP_RECOVERY       = FALSE

  DEFINE SPI_VARIABLE_BASE          = 0x00380000
  DEFINE SPI_VARIABLE_SIZE          = 0x28000
  DEFINE DTPM_SUPPORT               = FALSE
  DEFINE FTPM_SUPPORT               = FALSE
  DEFINE I2S_MC_SUPPORT             = TRUE
  DEFINE CAPSULE_ENABLE             = TRUE

!include Platform/CIX/Sky1/Sky1Board.dsc.inc

[PcdsFixedAtBuild.common]
  gCixTokenSpaceGuid.PcdSpi0En|FALSE
  gCixTokenSpaceGuid.PcdAcpiUart3Enable|TRUE

  # USB-C power delivery controllers
  gCixPlatformTokenSpaceGuid.PcdPdDevAlertPins|{ 8, 8, 0xFF, 0xFF }
  gCixPlatformTokenSpaceGuid.PcdPdDevI2cBuses|{ 0x7, 0x7, 0xFF, 0xFF }
  gCixPlatformTokenSpaceGuid.PcdPdDevI2cSlaveAddresses|{ 0x30, 0x31, 0xFF, 0xFF }
  gCixPlatformTokenSpaceGuid.PcdTypecPortDefaultModes|{ 1, 1, 4, 4 }
  gCixPlatformTokenSpaceGuid.PcdTypecPortDefaultOrientations|{ 1, 1, 1, 1 }
