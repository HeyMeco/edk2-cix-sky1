/** @file
  NOR flash for the UEFI variable store in StandaloneMM on CIX Sky1.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef NOR_FLASH_STANDALONE_MM_H_
#define NOR_FLASH_STANDALONE_MM_H_

#include <PiMm.h>

#include <Guid/NvramInformation.h>
#include <Guid/SystemNvDataGuid.h>
#include <Guid/VariableFormat.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MmServicesTableLib.h>
#include <Library/NorFlashLib.h>
#include <Library/OpteeSpServiceLib.h>

#include <Protocol/SmmFirmwareVolumeBlock.h>

// Erase block of every supported SPI NOR
#define NOR_BLOCK_SIZE  SIZE_4KB
// Program page
#define NOR_PAGE_SIZE  256
// Largest read in one transaction
#define NOR_READ_CHUNK  SIZE_4KB
// What the flash header and the firmware layouts address (3-byte addresses)
#define NOR_DEVICE_SIZE  SIZE_16MB

//
// SpiNor.c: SPI NOR commands through OP-TEE
//
EFI_STATUS
SpiNorInit (
  VOID
  );

EFI_STATUS
SpiNorTransaction (
  IN CONST CIX_SPI_NOR_TRANSACTION  *Transaction
  );

//
// NorFlashFvb.c
//
EFI_STATUS
EFIAPI
FvbGetAttributes (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  OUT       EFI_FVB_ATTRIBUTES_2                 *Attributes
  );

EFI_STATUS
EFIAPI
FvbSetAttributes (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN OUT    EFI_FVB_ATTRIBUTES_2                 *Attributes
  );

EFI_STATUS
EFIAPI
FvbGetPhysicalAddress (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  OUT       EFI_PHYSICAL_ADDRESS                 *Address
  );

EFI_STATUS
EFIAPI
FvbGetBlockSize (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN        EFI_LBA                              Lba,
  OUT       UINTN                                *BlockSize,
  OUT       UINTN                                *NumberOfBlocks
  );

EFI_STATUS
EFIAPI
FvbRead (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN        EFI_LBA                              Lba,
  IN        UINTN                                Offset,
  IN OUT    UINTN                                *NumBytes,
  IN OUT    UINT8                                *Buffer
  );

EFI_STATUS
EFIAPI
FvbWrite (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN        EFI_LBA                              Lba,
  IN        UINTN                                Offset,
  IN OUT    UINTN                                *NumBytes,
  IN        UINT8                                *Buffer
  );

EFI_STATUS
EFIAPI
FvbEraseBlocks (
  IN CONST EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  ...
  );

EFI_STATUS
NorFlashFvbInitialize (
  IN NOR_FLASH_INSTANCE                *Instance,
  IN CONST NVRAM_INFORMATION_HOB_DATA  *Nvram
  );

#endif
