/** @file
  UEFI runtime log output through StandaloneMM: SerialOutStmmLib in UEFI
  sends text through gCixMmSerialOutCommunicateGuid and this driver prints it
  on the OP-TEE console.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiMm.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MmServicesTableLib.h>
#include <Library/OpteeSpServiceLib.h>

typedef struct {
  UINTN         Function;
  EFI_STATUS    ReturnStatus;
  UINT8         Data[1];
} MM_SERIAL_OUT_COMMUNICATE_HEADER;

#define MM_SERIAL_OUT_FUNCTION_PRINT             1
#define MM_SERIAL_OUT_FUNCTION_GET_PAYLOAD_SIZE  2

#define HEADER_SIZE   OFFSET_OF (MM_SERIAL_OUT_COMMUNICATE_HEADER, Data)
#define PAYLOAD_SIZE  0x1000

extern EFI_GUID  gCixMmSerialOutCommunicateGuid;

STATIC CHAR8  mLine[PAYLOAD_SIZE + 1];

STATIC
EFI_STATUS
EFIAPI
MmSerialOutHandler (
  IN     EFI_HANDLE  DispatchHandle,
  IN     CONST VOID  *Context         OPTIONAL,
  IN OUT VOID        *CommBuffer      OPTIONAL,
  IN OUT UINTN       *CommBufferSize  OPTIONAL
  )
{
  MM_SERIAL_OUT_COMMUNICATE_HEADER  *Header;
  UINTN                             PayloadSize;
  UINTN                             Length;

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
    case MM_SERIAL_OUT_FUNCTION_PRINT:
      // Copy first: the normal world can change its buffer meanwhile
      CopyMem (mLine, Header->Data, PayloadSize);
      mLine[PayloadSize] = '\0';
      Length = AsciiStrLen (mLine);
      Header->ReturnStatus = (Length == 0) ? EFI_SUCCESS : OpteeSpConsoleWrite (mLine, Length);
      break;

    case MM_SERIAL_OUT_FUNCTION_GET_PAYLOAD_SIZE:
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

EFI_STATUS
EFIAPI
SerialOutStandaloneMmInitialize (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_MM_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  Handle;

  Handle = NULL;
  return gMmst->MmiHandlerRegister (MmSerialOutHandler, &gCixMmSerialOutCommunicateGuid, &Handle);
}
