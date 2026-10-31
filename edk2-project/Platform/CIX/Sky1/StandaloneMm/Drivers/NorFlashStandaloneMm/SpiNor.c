/** @file
  SPI NOR access for StandaloneMM on CIX Sky1, and the NorFlashLib block
  interface the FVB code uses.

  Every command is one transaction OP-TEE passes to TF-A's xSPI driver.
  The commands are the ones CIX's NorFlashStandaloneMm uses and every SPI
  NOR in the bootloader1 flash table supports: single-line, 3-byte
  addresses, READ (03h), PAGE PROGRAM (02h), 4 KiB SECTOR ERASE (20h).

  Writes keep the RAM copy of the variable store region (RegionBaseAddress,
  what the variable driver reads) in step with the flash.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "NorFlashStandaloneMm.h"

#define SPI_NOR_OP_WRSR2  0x31
#define SPI_NOR_OP_RDSR2  0x35
#define SPI_NOR_OP_PP     0x02
#define SPI_NOR_OP_READ   0x03
#define SPI_NOR_OP_WRDI   0x04
#define SPI_NOR_OP_RDSR   0x05
#define SPI_NOR_OP_WREN   0x06
#define SPI_NOR_OP_SE     0x20
#define SPI_NOR_OP_RDID   0x9F

#define SR_WIP     BIT0
#define SR2_QE     BIT1

// Polls of the status register before giving up on a program or erase
#define READY_POLLS  50000000

// JEDEC ID of the Winbond W25Q256JW (quad enable kept set, as CIX does)
#define JEDEC_W25Q256JW  0xEF6019

STATIC UINT32  mJedecId;

EFI_STATUS
SpiNorTransaction (
  IN CONST CIX_SPI_NOR_TRANSACTION  *Transaction
  )
{
  return OpteeSpNorTransaction (Transaction);
}

STATIC
EFI_STATUS
SpiNorCommand (
  IN     UINT8   Opcode,
  IN     BOOLEAN WithAddress,
  IN     UINT32  Address,
  IN     UINT32  DataDir,
  IN OUT VOID    *Data,
  IN     UINT32  DataBytes
  )
{
  CIX_SPI_NOR_TRANSACTION  Transaction;

  ZeroMem (&Transaction, sizeof (Transaction));
  Transaction.CmdBusWidth = 1;
  Transaction.Opcode      = Opcode;
  Transaction.BusMode     = SPI_NOR_BUS_1_1_1;
  if (WithAddress) {
    Transaction.AddrBusWidth  = 1;
    Transaction.AddrBytes     = 3;
    Transaction.Address       = Address;
    Transaction.DummyBusWidth = 1;
  }

  if (DataDir != SPI_NOR_DATA_NONE) {
    Transaction.DataDir    = DataDir;
    Transaction.DataBytes  = DataBytes;
    Transaction.DataBuffer = (UINTN)Data;
  }

  return SpiNorTransaction (&Transaction);
}

STATIC
EFI_STATUS
SpiNorReadStatus (
  IN  UINT8  Opcode,
  OUT UINT8  *Status
  )
{
  return SpiNorCommand (Opcode, FALSE, 0, SPI_NOR_DATA_IN, Status, 1);
}

STATIC
EFI_STATUS
SpiNorWriteEnable (
  VOID
  )
{
  return SpiNorCommand (SPI_NOR_OP_WREN, FALSE, 0, SPI_NOR_DATA_NONE, NULL, 0);
}

STATIC
EFI_STATUS
SpiNorWaitReady (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT8       Sr;
  UINTN       Polls;

  for (Polls = 0; Polls < READY_POLLS; Polls++) {
    Status = SpiNorReadStatus (SPI_NOR_OP_RDSR, &Sr);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if ((Sr & SR_WIP) == 0) {
      return EFI_SUCCESS;
    }
  }

  DEBUG ((DEBUG_ERROR, "%a: flash still busy\n", __func__));
  return EFI_TIMEOUT;
}

STATIC
BOOLEAN
SpiNorRangeValid (
  IN UINTN  Address,
  IN UINTN  Length
  )
{
  return (Address <= NOR_DEVICE_SIZE) && (Length <= NOR_DEVICE_SIZE - Address);
}

EFI_STATUS
SpiNorInit (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT8       Id[3];
  UINT8       Sr2;

  Status = SpiNorCommand (SPI_NOR_OP_RDID, FALSE, 0, SPI_NOR_DATA_IN, Id, sizeof (Id));
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: cannot read the JEDEC ID, %r\n", __func__, Status));
    return Status;
  }

  mJedecId = ((UINT32)Id[0] << 16) | ((UINT32)Id[1] << 8) | Id[2];
  DEBUG ((DEBUG_INFO, "%a: JEDEC ID %06x\n", __func__, mJedecId));

  if (mJedecId == JEDEC_W25Q256JW) {
    Status = SpiNorReadStatus (SPI_NOR_OP_RDSR2, &Sr2);
    if (!EFI_ERROR (Status) && ((Sr2 & SR2_QE) == 0)) {
      Sr2   |= SR2_QE;
      Status = SpiNorWriteEnable ();
      if (!EFI_ERROR (Status)) {
        Status = SpiNorCommand (SPI_NOR_OP_WRSR2, FALSE, 0, SPI_NOR_DATA_OUT, &Sr2, 1);
      }

      if (!EFI_ERROR (Status)) {
        Status = SpiNorWaitReady ();
      }
    }

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: cannot set the quad enable bit, %r\n", __func__, Status));
    }
  }

  return EFI_SUCCESS;
}

/**
  Flash size from the JEDEC capacity byte, at most what 3-byte addresses
  reach.
**/
UINTN
SpiNorDeviceSize (
  VOID
  )
{
  UINT8  Capacity;

  Capacity = (UINT8)mJedecId;
  if ((Capacity >= 0x10) && (Capacity < 0x18)) {
    return (UINTN)1 << Capacity;
  }

  return NOR_DEVICE_SIZE;
}

EFI_STATUS
SpiNorRead (
  IN  UINTN   Address,
  IN  UINT32  Length,
  OUT UINT8   *Buffer
  )
{
  EFI_STATUS  Status;
  UINT32      Chunk;

  if ((Buffer == NULL) || !SpiNorRangeValid (Address, Length)) {
    return EFI_INVALID_PARAMETER;
  }

  while (Length != 0) {
    Chunk  = MIN (Length, NOR_READ_CHUNK);
    Status = SpiNorCommand (SPI_NOR_OP_READ, TRUE, (UINT32)Address, SPI_NOR_DATA_IN, Buffer, Chunk);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Address += Chunk;
    Buffer  += Chunk;
    Length  -= Chunk;
  }

  return EFI_SUCCESS;
}

/**
  Program, page by page.  The range must be erased, or only clear bits.
**/
EFI_STATUS
SpiNorWrite (
  IN UINTN   Address,
  IN UINT32  Length,
  IN UINT8   *Buffer
  )
{
  EFI_STATUS  Status;
  UINT32      Chunk;

  if ((Buffer == NULL) || !SpiNorRangeValid (Address, Length)) {
    return EFI_INVALID_PARAMETER;
  }

  while (Length != 0) {
    Chunk  = MIN (Length, NOR_PAGE_SIZE - (UINT32)(Address % NOR_PAGE_SIZE));
    Status = SpiNorWriteEnable ();
    if (!EFI_ERROR (Status)) {
      Status = SpiNorCommand (SPI_NOR_OP_PP, TRUE, (UINT32)Address, SPI_NOR_DATA_OUT, Buffer, Chunk);
    }

    if (!EFI_ERROR (Status)) {
      Status = SpiNorWaitReady ();
    }

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: program at 0x%lx failed, %r\n", __func__, Address, Status));
      return Status;
    }

    Address += Chunk;
    Buffer  += Chunk;
    Length  -= Chunk;
  }

  return EFI_SUCCESS;
}

/**
  Erase whole 4 KiB sectors.  WholeChip is not supported.
**/
EFI_STATUS
SpiNorErase (
  IN UINTN    Address,
  IN UINT32   Length,
  IN BOOLEAN  WholeChip
  )
{
  EFI_STATUS  Status;

  if (WholeChip || ((Address % NOR_BLOCK_SIZE) != 0) || ((Length % NOR_BLOCK_SIZE) != 0) ||
      !SpiNorRangeValid (Address, Length))
  {
    return EFI_INVALID_PARAMETER;
  }

  Status = EFI_SUCCESS;
  while (Length != 0) {
    Status = SpiNorWriteEnable ();
    if (!EFI_ERROR (Status)) {
      Status = SpiNorCommand (SPI_NOR_OP_SE, TRUE, (UINT32)Address, SPI_NOR_DATA_NONE, NULL, 0);
    }

    if (!EFI_ERROR (Status)) {
      Status = SpiNorWaitReady ();
    }

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: erase at 0x%lx failed, %r\n", __func__, Address, Status));
      break;
    }

    Address += NOR_BLOCK_SIZE;
    Length  -= NOR_BLOCK_SIZE;
  }

  SpiNorCommand (SPI_NOR_OP_WRDI, FALSE, 0, SPI_NOR_DATA_NONE, NULL, 0);
  return Status;
}

//
// NorFlashLib block interface (Lba counts from the start of the flash)
//

/**
  Bring the RAM copy of the variable store region in line with the flash:
  Data written at flash Address, or erased bytes when Data is NULL.
**/
STATIC
VOID
UpdateRegionCopy (
  IN NOR_FLASH_INSTANCE  *Instance,
  IN UINTN               Address,
  IN UINTN               Length,
  IN CONST UINT8         *Data OPTIONAL
  )
{
  UINTN  Base;
  UINTN  End;
  UINTN  From;
  UINTN  To;

  Base = (UINTN)Instance->StartLba * Instance->Media.BlockSize;
  End  = Base + Instance->Blocks * Instance->Media.BlockSize;
  From = MAX (Address, Base);
  To   = MIN (Address + Length, End);
  if (From >= To) {
    return;
  }

  if (Data != NULL) {
    CopyMem ((VOID *)(Instance->RegionBaseAddress + (From - Base)), Data + (From - Address), To - From);
  } else {
    SetMem ((VOID *)(Instance->RegionBaseAddress + (From - Base)), To - From, 0xFF);
  }
}

/**
  After a failed program or erase the flash holds something in between:
  read it back into the RAM copy, or mark it erased if even that fails.
**/
STATIC
VOID
RefreshRegionCopy (
  IN NOR_FLASH_INSTANCE  *Instance,
  IN UINTN               BlockAddress,
  IN UINTN               Length
  )
{
  UINT8  *Block;
  UINTN  Done;

  Block = Instance->ShadowBuffer;
  for (Done = 0; Done < Length; Done += Instance->Media.BlockSize) {
    if (EFI_ERROR (SpiNorRead (BlockAddress + Done, Instance->Media.BlockSize, Block))) {
      UpdateRegionCopy (Instance, BlockAddress + Done, Instance->Media.BlockSize, NULL);
    } else {
      UpdateRegionCopy (Instance, BlockAddress + Done, Instance->Media.BlockSize, Block);
    }
  }
}

STATIC
BOOLEAN
InstanceRangeValid (
  IN NOR_FLASH_INSTANCE  *Instance,
  IN EFI_LBA             Lba,
  IN UINTN               Offset,
  IN UINTN               Length
  )
{
  UINT64  Start;

  if (Lba > Instance->Size / Instance->Media.BlockSize) {
    return FALSE;
  }

  Start = (UINT64)Lba * Instance->Media.BlockSize;
  return (Offset <= Instance->Size - Start) && (Length <= Instance->Size - Start - Offset);
}

EFI_STATUS
NorFlashRead (
  IN NOR_FLASH_INSTANCE  *Instance,
  IN EFI_LBA             Lba,
  IN UINTN               Offset,
  IN UINTN               Length,
  OUT VOID               *Buffer
  )
{
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Length == 0) {
    return EFI_SUCCESS;
  }

  if (!InstanceRangeValid (Instance, Lba, Offset, Length)) {
    DEBUG ((DEBUG_ERROR, "%a: read past the end of the flash\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  return SpiNorRead ((UINTN)Lba * Instance->Media.BlockSize + Offset, (UINT32)Length, Buffer);
}

EFI_STATUS
NorFlashReadBlocks (
  IN NOR_FLASH_INSTANCE  *Instance,
  IN EFI_LBA             Lba,
  IN UINTN               BufferSizeInBytes,
  OUT VOID               *Buffer
  )
{
  if ((BufferSizeInBytes % Instance->Media.BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  return NorFlashRead (Instance, Lba, 0, BufferSizeInBytes, Buffer);
}

/**
  Write Length bytes at Offset in block Lba and on, block by block: where the
  new data only clears bits it is programmed in place, otherwise the block
  is read, merged, erased and programmed again (as CIX's driver does).
**/
EFI_STATUS
NorFlashWrite (
  IN NOR_FLASH_INSTANCE  *Instance,
  IN EFI_LBA             Lba,
  IN UINTN               Offset,
  IN UINTN               Length,
  IN UINT8               *Buffer
  )
{
  EFI_STATUS  Status;
  UINTN       BlockSize;
  UINTN       Address;
  UINTN       BlockAddress;
  UINTN       Start;
  UINTN       Chunk;
  UINTN       Done;
  UINTN       Index;
  UINT8       *Old;
  BOOLEAN     NeedsErase;

  if ((Buffer == NULL) || (Instance->ShadowBuffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!InstanceRangeValid (Instance, Lba, Offset, Length)) {
    DEBUG ((DEBUG_ERROR, "%a: write past the end of the flash\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  BlockSize = Instance->Media.BlockSize;
  Address   = (UINTN)Lba * BlockSize + Offset;
  Old       = Instance->ShadowBuffer;

  for (Done = 0; Done < Length; Done += Chunk) {
    BlockAddress = (Address + Done) - ((Address + Done) % BlockSize);
    Start        = (Address + Done) - BlockAddress;
    Chunk        = MIN (Length - Done, BlockSize - Start);

    Status = SpiNorRead (BlockAddress, (UINT32)BlockSize, Old);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    NeedsErase = FALSE;
    for (Index = 0; Index < Chunk; Index++) {
      if ((Buffer[Done + Index] & ~Old[Start + Index]) != 0) {
        NeedsErase = TRUE;
        break;
      }
    }

    if (!NeedsErase) {
      if (CompareMem (Old + Start, Buffer + Done, Chunk) != 0) {
        Status = SpiNorWrite (BlockAddress + Start, (UINT32)Chunk, Buffer + Done);
      }
    } else {
      CopyMem (Old + Start, Buffer + Done, Chunk);
      Status = SpiNorErase (BlockAddress, (UINT32)BlockSize, FALSE);
      if (!EFI_ERROR (Status)) {
        Status = SpiNorWrite (BlockAddress, (UINT32)BlockSize, Old);
      }
    }

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: write at 0x%lx failed, %r\n", __func__, Address + Done, Status));
      RefreshRegionCopy (Instance, BlockAddress, BlockSize);
      return Status;
    }

    UpdateRegionCopy (Instance, Address + Done, Chunk, Buffer + Done);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
NorFlashWriteBlocks (
  IN NOR_FLASH_INSTANCE  *Instance,
  IN EFI_LBA             Lba,
  IN UINTN               BufferSizeInBytes,
  IN VOID                *Buffer
  )
{
  if ((BufferSizeInBytes % Instance->Media.BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  return NorFlashWrite (Instance, Lba, 0, BufferSizeInBytes, Buffer);
}

EFI_STATUS
NorFlashErase (
  IN NOR_FLASH_INSTANCE  *Instance,
  IN EFI_LBA             Lba,
  IN UINTN               Length
  )
{
  EFI_STATUS  Status;
  UINTN       Address;

  if (((Length % Instance->Media.BlockSize) != 0) || !InstanceRangeValid (Instance, Lba, 0, Length)) {
    return EFI_INVALID_PARAMETER;
  }

  Address = (UINTN)Lba * Instance->Media.BlockSize;
  Status  = SpiNorErase (Address, (UINT32)Length, FALSE);
  if (EFI_ERROR (Status)) {
    RefreshRegionCopy (Instance, Address, Length);
  } else {
    UpdateRegionCopy (Instance, Address, Length, NULL);
  }

  return Status;
}

EFI_STATUS
NorFlashReset (
  IN NOR_FLASH_INSTANCE  *Instance
  )
{
  return EFI_SUCCESS;
}
