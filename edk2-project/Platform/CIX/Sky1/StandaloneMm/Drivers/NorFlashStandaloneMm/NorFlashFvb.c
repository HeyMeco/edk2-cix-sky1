/** @file
  Firmware Volume Block protocol for the UEFI variable store in StandaloneMM,
  after CIX's NorFlashStmmRuntimeDxe/NorFlashFvb.c.

  The volume covers the variable store, the FTW working block and the FTW
  spare (contiguous in the flash).  GetPhysicalAddress() returns the RAM copy
  of it, which VariablePcdLib also gives the variable and FTW drivers.

  Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved
  Copyright (c) 2022, CIX, Ltd. All rights reserved.
  Copyright (c) 2011 - 2021, Arm Limited. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "NorFlashStandaloneMm.h"

#define HEADERS_LENGTH  (sizeof (EFI_FIRMWARE_VOLUME_HEADER) + sizeof (EFI_FV_BLOCK_MAP_ENTRY) + sizeof (VARIABLE_STORE_HEADER))

STATIC
EFI_STATUS
InitializeFvAndVariableStoreHeaders (
  IN NOR_FLASH_INSTANCE                *Instance,
  IN CONST NVRAM_INFORMATION_HOB_DATA  *Nvram
  )
{
  EFI_STATUS                  Status;
  VOID                        *Headers;
  EFI_FIRMWARE_VOLUME_HEADER  *FirmwareVolumeHeader;
  VARIABLE_STORE_HEADER       *VariableStoreHeader;

  Headers = AllocateZeroPool (HEADERS_LENGTH);
  if (Headers == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  FirmwareVolumeHeader = (EFI_FIRMWARE_VOLUME_HEADER *)Headers;
  CopyGuid (&FirmwareVolumeHeader->FileSystemGuid, &gEfiSystemNvDataFvGuid);
  FirmwareVolumeHeader->FvLength   = (UINT64)Nvram->VariableSize + Nvram->FtwWorkingSize + Nvram->FtwSpareSize;
  FirmwareVolumeHeader->Signature  = EFI_FVH_SIGNATURE;
  FirmwareVolumeHeader->Attributes = (EFI_FVB_ATTRIBUTES_2)(
                                                            EFI_FVB2_READ_ENABLED_CAP |
                                                            EFI_FVB2_READ_STATUS      |
                                                            EFI_FVB2_STICKY_WRITE     |
                                                            EFI_FVB2_ERASE_POLARITY   |
                                                            EFI_FVB2_WRITE_STATUS     |
                                                            EFI_FVB2_WRITE_ENABLED_CAP
                                                            );
  FirmwareVolumeHeader->HeaderLength          = sizeof (EFI_FIRMWARE_VOLUME_HEADER) + sizeof (EFI_FV_BLOCK_MAP_ENTRY);
  FirmwareVolumeHeader->Revision              = EFI_FVH_REVISION;
  FirmwareVolumeHeader->BlockMap[0].NumBlocks = (UINT32)Instance->Blocks;
  FirmwareVolumeHeader->BlockMap[0].Length    = Instance->Media.BlockSize;
  FirmwareVolumeHeader->BlockMap[1].NumBlocks = 0;
  FirmwareVolumeHeader->BlockMap[1].Length    = 0;
  FirmwareVolumeHeader->Checksum              = CalculateCheckSum16 ((UINT16 *)FirmwareVolumeHeader, FirmwareVolumeHeader->HeaderLength);

  VariableStoreHeader = (VARIABLE_STORE_HEADER *)((UINTN)Headers + FirmwareVolumeHeader->HeaderLength);
  CopyGuid (&VariableStoreHeader->Signature, &gEfiAuthenticatedVariableGuid);
  VariableStoreHeader->Size   = Nvram->VariableSize - FirmwareVolumeHeader->HeaderLength;
  VariableStoreHeader->Format = VARIABLE_STORE_FORMATTED;
  VariableStoreHeader->State  = VARIABLE_STORE_HEALTHY;

  Status = NorFlashWrite (Instance, Instance->StartLba, 0, HEADERS_LENGTH, Headers);

  FreePool (Headers);
  return Status;
}

/**
  Check the volume and variable store headers, in the RAM copy.
**/
STATIC
EFI_STATUS
ValidateFvHeader (
  IN NOR_FLASH_INSTANCE                *Instance,
  IN CONST NVRAM_INFORMATION_HOB_DATA  *Nvram
  )
{
  EFI_FIRMWARE_VOLUME_HEADER  *FwVolHeader;
  VARIABLE_STORE_HEADER       *VariableStoreHeader;

  FwVolHeader = (EFI_FIRMWARE_VOLUME_HEADER *)Instance->RegionBaseAddress;

  if ((FwVolHeader->Revision != EFI_FVH_REVISION) ||
      (FwVolHeader->Signature != EFI_FVH_SIGNATURE) ||
      (FwVolHeader->FvLength != (UINT64)Nvram->VariableSize + Nvram->FtwWorkingSize + Nvram->FtwSpareSize))
  {
    DEBUG ((DEBUG_INFO, "%a: no firmware volume header\n", __func__));
    return EFI_NOT_FOUND;
  }

  if (!CompareGuid (&FwVolHeader->FileSystemGuid, &gEfiSystemNvDataFvGuid)) {
    DEBUG ((DEBUG_INFO, "%a: firmware volume GUID not compatible\n", __func__));
    return EFI_NOT_FOUND;
  }

  if ((FwVolHeader->HeaderLength > Nvram->VariableSize - sizeof (VARIABLE_STORE_HEADER)) ||
      (CalculateSum16 ((UINT16 *)FwVolHeader, FwVolHeader->HeaderLength) != 0))
  {
    DEBUG ((DEBUG_INFO, "%a: firmware volume checksum invalid\n", __func__));
    return EFI_NOT_FOUND;
  }

  VariableStoreHeader = (VARIABLE_STORE_HEADER *)((UINTN)FwVolHeader + FwVolHeader->HeaderLength);
  if (!CompareGuid (&VariableStoreHeader->Signature, &gEfiVariableGuid) &&
      !CompareGuid (&VariableStoreHeader->Signature, &gEfiAuthenticatedVariableGuid))
  {
    DEBUG ((DEBUG_INFO, "%a: variable store GUID not compatible\n", __func__));
    return EFI_NOT_FOUND;
  }

  if (VariableStoreHeader->Size != Nvram->VariableSize - FwVolHeader->HeaderLength) {
    DEBUG ((DEBUG_INFO, "%a: variable store length does not match\n", __func__));
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
FvbGetAttributes (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  OUT       EFI_FVB_ATTRIBUTES_2                 *Attributes
  )
{
  NOR_FLASH_INSTANCE  *Instance;

  Instance    = INSTANCE_FROM_FVB_THIS (This);
  *Attributes = (EFI_FVB_ATTRIBUTES_2)(
                                       EFI_FVB2_READ_ENABLED_CAP |
                                       EFI_FVB2_READ_STATUS      |
                                       EFI_FVB2_STICKY_WRITE     |
                                       EFI_FVB2_ERASE_POLARITY
                                       );
  if (!Instance->Media.ReadOnly) {
    *Attributes |= EFI_FVB2_WRITE_STATUS | EFI_FVB2_WRITE_ENABLED_CAP;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
FvbSetAttributes (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN OUT    EFI_FVB_ATTRIBUTES_2                 *Attributes
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
FvbGetPhysicalAddress (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  OUT       EFI_PHYSICAL_ADDRESS                 *Address
  )
{
  NOR_FLASH_INSTANCE  *Instance;

  Instance = INSTANCE_FROM_FVB_THIS (This);
  *Address = Instance->RegionBaseAddress;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
FvbGetBlockSize (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN        EFI_LBA                              Lba,
  OUT       UINTN                                *BlockSize,
  OUT       UINTN                                *NumberOfBlocks
  )
{
  NOR_FLASH_INSTANCE  *Instance;

  Instance = INSTANCE_FROM_FVB_THIS (This);
  if (Lba >= Instance->Blocks) {
    return EFI_INVALID_PARAMETER;
  }

  *BlockSize      = Instance->Media.BlockSize;
  *NumberOfBlocks = (UINTN)(Instance->Blocks - Lba);
  return EFI_SUCCESS;
}

/**
  Check an FVB read or write: it stays in the volume and in one block.  A
  request that crosses the block end is cut there (EFI_BAD_BUFFER_SIZE).
**/
STATIC
EFI_STATUS
CheckAccess (
  IN     NOR_FLASH_INSTANCE  *Instance,
  IN     EFI_LBA             Lba,
  IN     UINTN               Offset,
  IN OUT UINTN               *NumBytes,
  IN     VOID                *Buffer
  )
{
  UINTN  BlockSize;

  BlockSize = Instance->Media.BlockSize;
  if ((Buffer == NULL) || (Lba >= Instance->Blocks) || (Offset >= BlockSize) || (*NumBytes == 0)) {
    *NumBytes = 0;
    return EFI_BAD_BUFFER_SIZE;
  }

  if (*NumBytes > BlockSize - Offset) {
    *NumBytes = BlockSize - Offset;
    return EFI_BAD_BUFFER_SIZE;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
FvbRead (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN        EFI_LBA                              Lba,
  IN        UINTN                                Offset,
  IN OUT    UINTN                                *NumBytes,
  IN OUT    UINT8                                *Buffer
  )
{
  NOR_FLASH_INSTANCE  *Instance;
  EFI_STATUS          Status;
  EFI_STATUS          Access;

  Instance = INSTANCE_FROM_FVB_THIS (This);
  Access   = CheckAccess (Instance, Lba, Offset, NumBytes, Buffer);
  if (*NumBytes == 0) {
    return Access;
  }

  Status = NorFlashRead (Instance, Instance->StartLba + Lba, Offset, *NumBytes, Buffer);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: LBA %ld offset 0x%lx: %r\n", __func__, Instance->StartLba + Lba, Offset, Status));
    return EFI_DEVICE_ERROR;
  }

  return Access;
}

EFI_STATUS
EFIAPI
FvbWrite (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN        EFI_LBA                              Lba,
  IN        UINTN                                Offset,
  IN OUT    UINTN                                *NumBytes,
  IN        UINT8                                *Buffer
  )
{
  NOR_FLASH_INSTANCE  *Instance;
  EFI_STATUS          Status;
  EFI_STATUS          Access;

  Instance = INSTANCE_FROM_FVB_THIS (This);
  if (Instance->Media.ReadOnly) {
    return EFI_ACCESS_DENIED;
  }

  Access = CheckAccess (Instance, Lba, Offset, NumBytes, Buffer);
  if (*NumBytes == 0) {
    return Access;
  }

  Status = NorFlashWrite (Instance, Instance->StartLba + Lba, Offset, *NumBytes, Buffer);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: LBA %ld offset 0x%lx: %r\n", __func__, Instance->StartLba + Lba, Offset, Status));
    return EFI_DEVICE_ERROR;
  }

  return Access;
}

EFI_STATUS
EFIAPI
FvbEraseBlocks (
  IN CONST EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  ...
  )
{
  NOR_FLASH_INSTANCE  *Instance;
  VA_LIST             Args;
  EFI_LBA             StartingLba;
  UINTN               NumOfLba;
  EFI_STATUS          Status;

  Instance = INSTANCE_FROM_FVB_THIS (This);
  if (Instance->Media.ReadOnly) {
    return EFI_ACCESS_DENIED;
  }

  // Check the whole list before erasing anything
  VA_START (Args, This);
  while (TRUE) {
    StartingLba = VA_ARG (Args, EFI_LBA);
    if (StartingLba == EFI_LBA_LIST_TERMINATOR) {
      break;
    }

    NumOfLba = VA_ARG (Args, UINTN);
    if ((NumOfLba == 0) || (StartingLba >= Instance->Blocks) || (NumOfLba > Instance->Blocks - StartingLba)) {
      VA_END (Args);
      return EFI_INVALID_PARAMETER;
    }
  }

  VA_END (Args);

  Status = EFI_SUCCESS;
  VA_START (Args, This);
  while (TRUE) {
    StartingLba = VA_ARG (Args, EFI_LBA);
    if (StartingLba == EFI_LBA_LIST_TERMINATOR) {
      break;
    }

    NumOfLba = VA_ARG (Args, UINTN);
    Status   = NorFlashErase (Instance, Instance->StartLba + StartingLba, NumOfLba * Instance->Media.BlockSize);
    if (EFI_ERROR (Status)) {
      Status = EFI_DEVICE_ERROR;
      break;
    }
  }

  VA_END (Args);
  return Status;
}

/**
  Give the volume valid headers if it has none, as CIX's driver does: erase
  it and write empty volume and variable store headers.
**/
EFI_STATUS
NorFlashFvbInitialize (
  IN NOR_FLASH_INSTANCE                *Instance,
  IN CONST NVRAM_INFORMATION_HOB_DATA  *Nvram
  )
{
  EFI_STATUS  Status;

  if (!EFI_ERROR (ValidateFvHeader (Instance, Nvram))) {
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_WARN, "%a: no valid variable store, formatting it\n", __func__));
  Status = NorFlashErase (Instance, Instance->StartLba, Instance->Blocks * Instance->Media.BlockSize);
  if (!EFI_ERROR (Status)) {
    Status = InitializeFvAndVariableStoreHeaders (Instance, Nvram);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: cannot format the variable store, %r\n", __func__, Status));
  }

  return Status;
}
