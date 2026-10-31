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
  PLATFORM_NAME                  = OPI6Plus
  PLATFORM_GUID                  = 5F0C3B1E-7A2D-4C55-9E61-0D4B6F2A8C31
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
  DEFINE BOARD_DIR                  = Platform/CIX/Sky1/OPI6Plus
  DEFINE BOARD_DTB_NAME             = SKY1-OPI6PLUS.DTB
  DEFINE PCIE_RP0_X4_SLOT           = TRUE    # x8 root port wired to an x4 M.2 slot
  DEFINE LINUX_ACPI_CONFIG_OVERRIDE = TRUE    # ACPI/LinuxAcpiConfig.h
##################################################
# Hardware (features are shared: Sky1Board.dsc.inc)
##################################################
  DEFINE I2C_EC_ENABLE              = TRUE 
  DEFINE POWER_BUTTON_ENABLE        = TRUE     # power button through the EC
  DEFINE GPIO_POWER_BUTTON_ENABLE   = FALSE
  DEFINE PLATFORM_PD_ENABLE         = TRUE     # USB-C PD controllers (PcdPdDev*)
  DEFINE SOC_GMAC_ENABLE            = FALSE    # no SoC Ethernet MAC wired out
  DEFINE SPI_VARIABLE_BASE          = 0x00380000
  DEFINE SPI_VARIABLE_SIZE          = 0x28000

!include Platform/CIX/Sky1/Sky1Board.dsc.inc

[PcdsFixedAtBuild.common]
  gCixTokenSpaceGuid.PcdSpi0En|TRUE
  gCixTokenSpaceGuid.PcdAcpiUart3Enable|TRUE

  # USB-C power delivery controllers
  gCixPlatformTokenSpaceGuid.PcdPdDevAlertPins|{ 8, 8, 0xFF, 0xFF }
  gCixPlatformTokenSpaceGuid.PcdPdDevI2cBuses|{ 0x7, 0x7, 0xFF, 0xFF }
  gCixPlatformTokenSpaceGuid.PcdPdDevI2cSlaveAddresses|{ 0x30, 0x31, 0xFF, 0xFF }
  gCixPlatformTokenSpaceGuid.PcdTypecPortDefaultModes|{ 1, 1, 4, 4 }
  gCixPlatformTokenSpaceGuid.PcdTypecPortDefaultOrientations|{ 1, 1, 1, 1 }
