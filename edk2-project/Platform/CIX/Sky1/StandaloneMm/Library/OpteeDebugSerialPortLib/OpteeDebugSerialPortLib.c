/** @file
  SerialPortLib for StandaloneMM on CIX Sky1: output goes to the OP-TEE
  console in pieces of up to 255 characters; there is no input.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/OpteeSpServiceLib.h>
#include <Library/SerialPortLib.h>

#define CHUNK  255

RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  return RETURN_SUCCESS;
}

UINTN
EFIAPI
SerialPortWrite (
  IN UINT8  *Buffer,
  IN UINTN  NumberOfBytes
  )
{
  CHAR8  Line[CHUNK + 1];
  UINTN  Done;
  UINTN  Length;

  if (Buffer == NULL) {
    return 0;
  }

  for (Done = 0; Done < NumberOfBytes; Done += Length) {
    Length = MIN (NumberOfBytes - Done, CHUNK);
    CopyMem (Line, Buffer + Done, Length);
    Line[Length] = '\0';
    OpteeSpConsoleWrite (Line, Length);
  }

  return NumberOfBytes;
}

UINTN
EFIAPI
SerialPortRead (
  OUT UINT8  *Buffer,
  IN  UINTN  NumberOfBytes
  )
{
  return 0;
}

BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  return FALSE;
}

RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32  Control
  )
{
  return RETURN_UNSUPPORTED;
}

RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32  *Control
  )
{
  *Control = 0;
  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT UINT32              *Timeout,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  return RETURN_UNSUPPORTED;
}
