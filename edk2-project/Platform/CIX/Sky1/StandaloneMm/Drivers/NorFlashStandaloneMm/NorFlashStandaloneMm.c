/** @file
  NOR flash driver for StandaloneMM on CIX Sky1, in place of CIX's binary
  NorFlashStandaloneMm:

  * the Firmware Volume Block protocol for the UEFI variable store, at the
    flash location OP-TEE passes in the NVRAM information HOB; the region is
    kept in RAM as well, which is what the variable driver reads;
  * gCixMmNorFlashCommunicateGuid: single SPI NOR transactions for UEFI's
    NorFlashStmmRuntimeLib (flash access from UEFI at boot and at runtime).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "NorFlashStandaloneMm.h"

typedef struct {
  UINTN         Function;
  EFI_STATUS    ReturnStatus;
  UINT8         Data[1];
} MM_NOR_FLASH_COMMUNICATE_HEADER;

#define MM_NOR_FLASH_FUNCTION_TRANSACTION       1
#define MM_NOR_FLASH_FUNCTION_GET_PAYLOAD_SIZE  2

#define HEADER_SIZE        OFFSET_OF (MM_NOR_FLASH_COMMUNICATE_HEADER, Data)
#define TRANSACTION_SIZE   sizeof (CIX_SPI_NOR_TRANSACTION)
#define TRANSACTION_DATA   SIZE_4KB
#define PAYLOAD_SIZE       (TRANSACTION_SIZE + TRANSACTION_DATA)

extern EFI_GUID  gCixMmNorFlashCommunicateGuid;
extern EFI_GUID  gCixNorFlashDevicePathGuid;

UINTN
SpiNorDeviceSize (
  VOID
  );

STATIC UINT8               *mTransaction;
STATIC NOR_FLASH_INSTANCE  *mInstance;

/**
  One SPI NOR transaction for UEFI.  The data travels in this driver's own
  buffer, whatever DataBuffer the caller filled in, and must fit in what it
  sent: OP-TEE and TF-A use the address and length as they are.
**/
STATIC
EFI_STATUS
EFIAPI
MmNorFlashHandler (
  IN     EFI_HANDLE  DispatchHandle,
  IN     CONST VOID  *Context         OPTIONAL,
  IN OUT VOID        *CommBuffer      OPTIONAL,
  IN OUT UINTN       *CommBufferSize  OPTIONAL
  )
{
  MM_NOR_FLASH_COMMUNICATE_HEADER  *Header;
  CIX_SPI_NOR_TRANSACTION          *Transaction;
  UINTN                            PayloadSize;
  UINT64                           CallerBuffer;

  if ((CommBuffer == NULL) || (CommBufferSize == NULL)) {
    return EFI_SUCCESS;
  }

  if ((*CommBufferSize < HEADER_SIZE) || (*CommBufferSize - HEADER_SIZE > PAYLOAD_SIZE)) {
    DEBUG ((DEBUG_ERROR, "%a: communication buffer size 0x%lx invalid\n", __func__, *CommBufferSize));
    return EFI_SUCCESS;
  }

  Header      = CommBuffer;
  PayloadSize = *CommBufferSize - HEADER_SIZE;

  switch (Header->Function) {
    case MM_NOR_FLASH_FUNCTION_TRANSACTION:
      if (PayloadSize < TRANSACTION_SIZE) {
        Header->ReturnStatus = EFI_INVALID_PARAMETER;
        break;
      }

      // Work on a copy: the normal world can change its buffer meanwhile
      CopyMem (mTransaction, Header->Data, PayloadSize);
      Transaction  = (CIX_SPI_NOR_TRANSACTION *)mTransaction;
      CallerBuffer = Transaction->DataBuffer;
      if ((Transaction->DataDir == SPI_NOR_DATA_IN) || (Transaction->DataDir == SPI_NOR_DATA_OUT)) {
        if ((Transaction->DataBytes == 0) || (Transaction->DataBytes > PayloadSize - TRANSACTION_SIZE)) {
          Header->ReturnStatus = EFI_INVALID_PARAMETER;
          break;
        }

        Transaction->DataBuffer = (UINTN)(mTransaction + TRANSACTION_SIZE);
      } else {
        Transaction->DataBytes  = 0;
        Transaction->DataBuffer = 0;
      }

      Header->ReturnStatus = SpiNorTransaction (Transaction);
      Transaction->DataBuffer = CallerBuffer;
      CopyMem (Header->Data, mTransaction, PayloadSize);
      break;

    case MM_NOR_FLASH_FUNCTION_GET_PAYLOAD_SIZE:
      if (PayloadSize < sizeof (UINTN)) {
        Header->ReturnStatus = EFI_INVALID_PARAMETER;
        break;
      }

      WriteUnaligned64 ((UINT64 *)Header->Data, PAYLOAD_SIZE);
      Header->ReturnStatus = EFI_SUCCESS;
      break;

    default:
      Header->ReturnStatus = EFI_UNSUPPORTED;
      break;
  }

  return EFI_SUCCESS;
}

/**
  The variable store, FTW working and FTW spare areas: contiguous, in that
  order, in whole blocks, inside the flash.
**/
STATIC
BOOLEAN
NvramLayoutValid (
  IN CONST NVRAM_INFORMATION_HOB_DATA  *Nvram,
  IN UINTN                             DeviceSize
  )
{
  UINT64  Total;

  Total = (UINT64)Nvram->VariableSize + Nvram->FtwWorkingSize + Nvram->FtwSpareSize;
  return (Nvram->VariableSize >= NOR_BLOCK_SIZE) && (Nvram->FtwWorkingSize >= NOR_BLOCK_SIZE) &&
         (Nvram->FtwSpareSize >= NOR_BLOCK_SIZE) &&
         ((Nvram->VariableBase % NOR_BLOCK_SIZE) == 0) &&
         ((Nvram->VariableSize % NOR_BLOCK_SIZE) == 0) &&
         ((Nvram->FtwWorkingSize % NOR_BLOCK_SIZE) == 0) &&
         ((Nvram->FtwSpareSize % NOR_BLOCK_SIZE) == 0) &&
         (Nvram->FtwWorkingBase == Nvram->VariableBase + Nvram->VariableSize) &&
         (Nvram->FtwSpareBase == Nvram->FtwWorkingBase + Nvram->FtwWorkingSize) &&
         (Nvram->VariableBase < DeviceSize) && (Total <= DeviceSize - Nvram->VariableBase);
}

STATIC
EFI_STATUS
CreateInstance (
  IN  CONST NVRAM_INFORMATION_HOB_DATA  *Nvram,
  OUT NOR_FLASH_INSTANCE                **Out
  )
{
  NOR_FLASH_INSTANCE    *Instance;
  EFI_PHYSICAL_ADDRESS  Region;
  EFI_STATUS            Status;
  UINTN                 Total;

  Total  = Nvram->VariableSize + Nvram->FtwWorkingSize + Nvram->FtwSpareSize;
  Status = gMmst->MmAllocatePages (AllocateAnyPages, EfiRuntimeServicesData, EFI_SIZE_TO_PAGES (Total), &Region);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Instance = AllocateZeroPool (sizeof (*Instance));
  if (Instance == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Instance->ShadowBuffer = AllocatePool (NOR_BLOCK_SIZE);
  if (Instance->ShadowBuffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Instance->Signature            = NOR_FLASH_SIGNATURE;
  Instance->DeviceBaseAddress    = 0;
  Instance->Size                 = SpiNorDeviceSize ();
  Instance->Media.MediaId        = 0;
  Instance->Media.MediaPresent   = TRUE;
  Instance->Media.ReadOnly       = FALSE;
  Instance->Media.BlockSize      = NOR_BLOCK_SIZE;
  Instance->Media.IoAlign        = 4;
  Instance->Media.LastBlock      = Instance->Size / NOR_BLOCK_SIZE - 1;
  Instance->DevicePath.Vendor.Header.Type    = HARDWARE_DEVICE_PATH;
  Instance->DevicePath.Vendor.Header.SubType = HW_VENDOR_DP;
  SetDevicePathNodeLength (&Instance->DevicePath.Vendor, sizeof (VENDOR_DEVICE_PATH) + sizeof (UINT8));
  CopyGuid (&Instance->DevicePath.Vendor.Guid, &gCixNorFlashDevicePathGuid);
  SetDevicePathEndNode (&Instance->DevicePath.End);
  Instance->RegionBaseAddress    = (UINTN)Region;
  Instance->StartLba             = Nvram->VariableBase / NOR_BLOCK_SIZE;
  Instance->Blocks               = Total / NOR_BLOCK_SIZE;

  Instance->FvbProtocol.GetAttributes      = FvbGetAttributes;
  Instance->FvbProtocol.SetAttributes      = FvbSetAttributes;
  Instance->FvbProtocol.GetPhysicalAddress = FvbGetPhysicalAddress;
  Instance->FvbProtocol.GetBlockSize       = FvbGetBlockSize;
  Instance->FvbProtocol.Read               = FvbRead;
  Instance->FvbProtocol.Write              = FvbWrite;
  Instance->FvbProtocol.EraseBlocks        = FvbEraseBlocks;
  Instance->FvbProtocol.ParentHandle       = NULL;

  // The RAM copy starts as what the flash holds
  Status = SpiNorRead ((UINTN)Nvram->VariableBase, (UINT32)Total, (UINT8 *)(UINTN)Region);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: cannot read the variable store, %r\n", __func__, Status));
    return Status;
  }

  *Out = Instance;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
NorFlashStandaloneMmInitialize (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_MM_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                  Status;
  EFI_HANDLE                  Handle;
  EFI_HOB_GUID_TYPE           *Hob;
  NVRAM_INFORMATION_HOB_DATA  Nvram;

  Status = SpiNorInit ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mTransaction = AllocatePool (PAYLOAD_SIZE);
  if (mTransaction == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Handle = NULL;
  Status = gMmst->MmiHandlerRegister (MmNorFlashHandler, &gCixMmNorFlashCommunicateGuid, &Handle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Hob = GetFirstGuidHob (&gNvramInformationHobGuid);
  if ((Hob == NULL) || (GET_GUID_HOB_DATA_SIZE (Hob) < sizeof (Nvram))) {
    DEBUG ((DEBUG_ERROR, "%a: no NVRAM information from OP-TEE\n", __func__));
    return EFI_NOT_FOUND;
  }

  CopyMem (&Nvram, GET_GUID_HOB_DATA (Hob), sizeof (Nvram));
  if (!NvramLayoutValid (&Nvram, SpiNorDeviceSize ())) {
    DEBUG ((DEBUG_ERROR, "%a: invalid NVRAM layout 0x%lx+0x%x/0x%lx+0x%x/0x%lx+0x%x\n", __func__,
            Nvram.VariableBase, Nvram.VariableSize, Nvram.FtwWorkingBase, Nvram.FtwWorkingSize,
            Nvram.FtwSpareBase, Nvram.FtwSpareSize));
    return EFI_INVALID_PARAMETER;
  }

  DEBUG ((DEBUG_INFO, "%a: variables at 0x%lx, 0x%x + 0x%x + 0x%x bytes\n", __func__,
          Nvram.VariableBase, Nvram.VariableSize, Nvram.FtwWorkingSize, Nvram.FtwSpareSize));

  Status = CreateInstance (&Nvram, &mInstance);
  if (!EFI_ERROR (Status)) {
    Status = NorFlashFvbInitialize (mInstance, &Nvram);
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

  mInstance->Handle = NULL;
  return gMmst->MmInstallProtocolInterface (
                  &mInstance->Handle,
                  &gEfiSmmFirmwareVolumeBlockProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mInstance->FvbProtocol
                  );
}
