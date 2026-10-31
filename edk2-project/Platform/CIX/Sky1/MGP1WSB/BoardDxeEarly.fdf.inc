## @file
#  MS-R1 DXE drivers placed before the shared list of Sky1Board.fdf.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##
  INF Silicon/CIX/Sky1/Drivers/GenericWatchdogDxe/GlobalWatchdogDxe.inf
  # Sky1Common.fdf.inc only adds this with TOKEN_SETUP_SUPPORT, which this
  # board turns off for its own SetupManagerDxe; listing it under APRIORI
  # alone leaves it out of the FV.
  INF Platform/CIX/Sky1/Drivers/PlatformSetupVariableInitDxe/PlatformSetupVariableInitDxe.inf
!if $(SPI_TPM_ENABLE) == TRUE
  INF SecurityPkg/Tcg/Tcg2Dxe/Tcg2Dxe.inf
  INF Platform/CIX/Sky1/Drivers/SpiTpmAcpiDxe/SpiTpmAcpiDxe.inf
!endif
