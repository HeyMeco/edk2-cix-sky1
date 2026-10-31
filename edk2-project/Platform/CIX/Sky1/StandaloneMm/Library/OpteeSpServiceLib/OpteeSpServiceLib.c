/** @file
  FF-A direct requests from StandaloneMM to CIX OP-TEE's StMM services
  (core/arch/arm/kernel/stmm_sp.c, stmm_handle_debug_message_service and
  stmm_handle_nor_flash_service).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/ArmSvcLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/OpteeSpServiceLib.h>

#define FFA_MSG_SEND_DIRECT_REQ_64  0xC400006F

// OP-TEE endpoint IDs
#define OPTEE_SP_CONSOLE_ID  100
#define OPTEE_SP_NOR_ID      101

// OP-TEE's StMM return codes, in x3 of the direct response (32 bits)
#define STMM_RET_SUCCESS        0
#define STMM_RET_NOT_SUPPORTED  -1
#define STMM_RET_INVALID_PARAM  -2
#define STMM_RET_NO_MEM         -5

STATIC
EFI_STATUS
DirectRequest (
  IN UINT16  Endpoint,
  IN UINTN   Arg2,
  IN UINTN   Arg3,
  IN UINTN   Arg4,
  IN UINTN   Arg5
  )
{
  ARM_SVC_ARGS  Args;

  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = FFA_MSG_SEND_DIRECT_REQ_64;
  Args.Arg1 = Endpoint;
  Args.Arg2 = Arg2;
  Args.Arg3 = Arg3;
  Args.Arg4 = Arg4;
  Args.Arg5 = Arg5;
  ArmCallSvc (&Args);

  switch ((INT32)(UINT32)Args.Arg3) {
    case STMM_RET_SUCCESS:
      return EFI_SUCCESS;
    case STMM_RET_NOT_SUPPORTED:
      return EFI_UNSUPPORTED;
    case STMM_RET_INVALID_PARAM:
      return EFI_INVALID_PARAMETER;
    case STMM_RET_NO_MEM:
      return EFI_OUT_OF_RESOURCES;
    default:
      return EFI_ACCESS_DENIED;
  }
}

/**
  OP-TEE prints the buffer with %s and rewrites its last two bytes when they
  are line ends, so pass the NUL as well: at least two bytes, and the string
  stays terminated inside them.
**/
EFI_STATUS
EFIAPI
OpteeSpConsoleWrite (
  IN CONST CHAR8  *String,
  IN UINTN        Length
  )
{
  if ((String == NULL) || (Length == 0) || (String[Length] != '\0')) {
    return EFI_INVALID_PARAMETER;
  }

  return DirectRequest (OPTEE_SP_CONSOLE_ID, 0, 0, (UINTN)String, Length + 1);
}

/**
  Register layout OP-TEE turns into the CIX_SIP_NOR_STORAGE SMC: x2 opcode
  and address, x3 address bytes and data direction, x4 bus mode and dummy
  cycles, x5 buffer (low 32 bits of a StandaloneMM address OP-TEE
  translates) and length.
**/
EFI_STATUS
EFIAPI
OpteeSpNorTransaction (
  IN CONST CIX_SPI_NOR_TRANSACTION  *Transaction
  )
{
  UINTN  Buffer;
  UINTN  Length;
  UINT8  BusMode;

  Buffer = 0;
  Length = 0;
  if ((Transaction->DataDir == SPI_NOR_DATA_IN) || (Transaction->DataDir == SPI_NOR_DATA_OUT)) {
    if ((Transaction->DataBuffer > MAX_UINT32) || (Transaction->DataBytes == 0)) {
      return EFI_INVALID_PARAMETER;
    }

    Buffer = (UINTN)Transaction->DataBuffer;
    Length = Transaction->DataBytes;
  } else if ((Transaction->DataDir != SPI_NOR_DATA_NONE) || (Transaction->DataBytes != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  BusMode = (Transaction->BusMode != 0) ? Transaction->BusMode : SPI_NOR_BUS_1_1_1;

  return DirectRequest (
           OPTEE_SP_NOR_ID,
           Transaction->Opcode | LShiftU64 ((UINT32)Transaction->Address, 32),
           Transaction->AddrBytes | LShiftU64 (Transaction->DataDir, 32),
           BusMode | LShiftU64 (Transaction->DummyCycles, 32),
           Buffer | LShiftU64 (Length, 32)
           );
}
