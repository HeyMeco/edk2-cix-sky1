/** @file
  Services CIX OP-TEE offers StandaloneMM through FF-A direct requests
  (core/arch/arm/kernel/stmm_sp.c): its console and the boot flash.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef OPTEE_SP_SERVICE_LIB_H_
#define OPTEE_SP_SERVICE_LIB_H_

/**
  SPI NOR transaction, as UEFI's NorFlashStmmRuntimeLib sends it through
  gCixMmNorFlashCommunicateGuid and as OP-TEE forwards it to TF-A
  (CIX_SIP_NOR_STORAGE).  Only the fields OP-TEE passes on are used.
**/
typedef struct {
  UINT8     CmdBusWidth;      // 0x00
  UINT8     Opcode;           // 0x01
  UINT8     Reserved0[6];
  UINT8     AddrBusWidth;     // 0x08
  UINT8     AddrBytes;        // 0x09
  UINT8     Reserved1[6];
  UINT64    Address;          // 0x10
  UINT8     DummyBusWidth;    // 0x18
  UINT8     DummyCycles;      // 0x19
  UINT8     Reserved2[6];
  UINT8     BusMode;          // 0x20: TF-A BUS_MODE_T, 1: 1-1-1
  UINT8     Reserved3[3];
  UINT32    DataBytes;        // 0x24
  UINT32    DataDir;          // 0x28: SPI_NOR_DATA_*
  UINT32    Reserved4;
  UINT64    DataBuffer;       // 0x30: StandaloneMM address, below 4 GiB
} CIX_SPI_NOR_TRANSACTION;

#define SPI_NOR_DATA_NONE  0
#define SPI_NOR_DATA_IN    1
#define SPI_NOR_DATA_OUT   2

#define SPI_NOR_BUS_1_1_1  1

/**
  Print a string on the OP-TEE console (shown in OP-TEE debug builds).

  @param[in]  String  NUL-terminated ASCII string.
  @param[in]  Length  Its length, without the NUL.
**/
EFI_STATUS
EFIAPI
OpteeSpConsoleWrite (
  IN CONST CHAR8  *String,
  IN UINTN        Length
  );

/**
  Run one SPI NOR transaction on the boot flash.

  @param[in]  Transaction  The transaction; DataBuffer must hold DataBytes.
**/
EFI_STATUS
EFIAPI
OpteeSpNorTransaction (
  IN CONST CIX_SPI_NOR_TRANSACTION  *Transaction
  );

#endif
