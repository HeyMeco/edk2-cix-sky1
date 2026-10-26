/**

  Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/ConfigParamsHookLib.h>
#include <Library/UsbLib.h>
#include <Protocol/ResetId.h>

EFI_STATUS
EFIAPI
ConfigParamsHookLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status = EFI_SUCCESS;

  return Status;
}

STATIC EFI_STATUS
UsbPhyConfigInit (
  IN OUT CONFIG_PARAMS_DATA_BLOCK  *ConfigData
  )
{
  UINT32          Index;
  UINT32          Usb20PhyReleaseResetIdMap;
  USB_REG_CONFIG  *Usb20PhyRegConfig;
  USB_REG_CONFIG  *Usb32PhyRegConfig;
  USB_REG_CONFIG  *UsbComboPhyRegConfig;

  ZeroMem(&ConfigData->USB20PHY, sizeof(CONFIG_PARAMS_USB20_PHY));
  ZeroMem(&ConfigData->USB32PHY, sizeof(CONFIG_PARAMS_USB32_PHY));
  ZeroMem(&ConfigData->USBCOMPHY, sizeof(CONFIG_PARAMS_USBCOM_PHY));

  Usb20PhyReleaseResetIdMap = PcdGet32(PcdUsb20PhyReleasePrstIdMap);
  Index = 0;
  if (Usb20PhyReleaseResetIdMap & BIT0) {
    ConfigData->USB20PHY.usb20_phy_reset[Index] = USBPHY_HS0_PRST_N;
    Index++;
  }
  if (Usb20PhyReleaseResetIdMap & BIT1) {
    ConfigData->USB20PHY.usb20_phy_reset[Index] = USBPHY_HS1_PRST_N;
    Index++;
  }
  if (Usb20PhyReleaseResetIdMap & BIT2) {
    ConfigData->USB20PHY.usb20_phy_reset[Index] = USBPHY_HS2_PRST_N;
    Index++;
  }
  if (Usb20PhyReleaseResetIdMap & BIT3) {
    ConfigData->USB20PHY.usb20_phy_reset[Index] = USBPHY_HS3_PRST_N;
    Index++;
  }
  if (Usb20PhyReleaseResetIdMap & BIT4) {
    ConfigData->USB20PHY.usb20_phy_reset[Index] = USBPHY_HS4_PRST_N;
    Index++;
  }
  if (Usb20PhyReleaseResetIdMap & BIT5) {
    ConfigData->USB20PHY.usb20_phy_reset[Index] = USBPHY_HS5_PRST_N;
    Index++;
  }
  if (Usb20PhyReleaseResetIdMap & BIT6) {
    ConfigData->USB20PHY.usb20_phy_reset[Index] = USBPHY_HS6_PRST_N;
    Index++;
  }
  if (Usb20PhyReleaseResetIdMap & BIT7) {
    ConfigData->USB20PHY.usb20_phy_reset[Index] = USBPHY_HS7_PRST_N;
    Index++;
  }
  if (Usb20PhyReleaseResetIdMap & BIT8) {
    ConfigData->USB20PHY.usb20_phy_reset[Index] = USBPHY_HS8_PRST_N;
    Index++;
  }
  if (Usb20PhyReleaseResetIdMap & BIT9) {
    ConfigData->USB20PHY.usb20_phy_reset[Index] = USBPHY_HS9_PRST_N;
    Index++;
  }

  Usb20PhyRegConfig = PcdGetPtr(PcdUsb20PhyConfig);
  Index = 0;
  while ((Usb20PhyRegConfig->UsbPhyType != USBTYPE_UNDEFINED) && (Index < MAX_USB_PHY_CONFIG_PARAMS_NUM)) {
    CopyMem(&ConfigData->USB20PHY.usb20_phy[Index], Usb20PhyRegConfig, sizeof(USB_REG_CONFIG));
    Usb20PhyRegConfig++;
    Index++;
  };

  Usb32PhyRegConfig = PcdGetPtr(PcdUsb32PhyConfig);
  Index = 0;
  while ((Usb32PhyRegConfig->UsbPhyType != USBTYPE_UNDEFINED) && (Index < MAX_USB_PHY_CONFIG_PARAMS_NUM)){
    CopyMem(&ConfigData->USB32PHY.usb32_phy[Index], Usb32PhyRegConfig, sizeof(USB_REG_CONFIG));
    Usb32PhyRegConfig++;
    Index++;
  };

  UsbComboPhyRegConfig = PcdGetPtr(PcdUsbComboPhyConfig);
  Index = 0;
  while ((UsbComboPhyRegConfig->UsbPhyType != USBTYPE_UNDEFINED) && (Index < MAX_USB_PHY_CONFIG_PARAMS_NUM)){
    CopyMem(&ConfigData->USBCOMPHY.usbcom_phy[Index], UsbComboPhyRegConfig, sizeof(USB_REG_CONFIG));
    UsbComboPhyRegConfig++;
    Index++;
  };

  return EFI_SUCCESS;
}

STATIC CONFIG_PARAMS_INIT_FUNC  mConfigParamsInitTable[] = {
  UsbPhyConfigInit,
  // add initializatrion routines for configuration parameters BEFORE this line, and they were invoked from top to down.
  NULL,
};

STATIC UINT32  mConfigParamsInitTableSize = ARRAY_SIZE (mConfigParamsInitTable);

VOID
EFIAPI
ConfigParamsHook (
  IN OUT CONFIG_PARAMS_DATA_BLOCK  *ConfigData
  )
{
  EFI_STATUS  Status = EFI_SUCCESS;
  UINT32      Index  = 0;

  if (ConfigData == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: config params hook routine failed to get config data\n", __FUNCTION__));
    return;
  }

  while (Index < mConfigParamsInitTableSize) {
    if (mConfigParamsInitTable[Index] != NULL) {
      Status = mConfigParamsInitTable[Index](ConfigData);
      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "%a: config params hook routine failed at index %d, status %r\n", __FUNCTION__, Index, Status));
      }
    }

    Index++;
  }
}
