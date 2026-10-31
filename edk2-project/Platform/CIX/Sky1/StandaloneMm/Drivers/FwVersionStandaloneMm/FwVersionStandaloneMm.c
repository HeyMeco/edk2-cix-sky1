/** @file
  StandaloneMM firmware version, for UEFI's FwVersionDxe through StmmInfoLib
  (Platform/CIX/Sky1/Library/StmmInfoLib): the version is
  PcdFirmwareVersionString.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiMm.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MmServicesTableLib.h>
#include <Library/PcdLib.h>

// StmmInfoLibInternal.h
typedef struct {
  UINTN         Function;
  EFI_STATUS    ReturnStatus;
  UINT8         Data[1];
} MM_STMM_INFO_COMMUNICATE_HEADER;

#define MM_STMM_INFO_FUNCTION_GET_VERSION       1
#define MM_STMM_INFO_FUNCTION_GET_PAYLOAD_SIZE  2

#define HEADER_SIZE   OFFSET_OF (MM_STMM_INFO_COMMUNICATE_HEADER, Data)
#define PAYLOAD_SIZE  0x400

extern EFI_GUID  gCixMmStmmInfoCommunicateGuid;

STATIC
EFI_STATUS
EFIAPI
MmStmmInfoHandler (
  IN     EFI_HANDLE  DispatchHandle,
  IN     CONST VOID  *Context         OPTIONAL,
  IN OUT VOID        *CommBuffer      OPTIONAL,
  IN OUT UINTN       *CommBufferSize  OPTIONAL
  )
{
  MM_STMM_INFO_COMMUNICATE_HEADER  *Header;
  CONST CHAR16                     *Version;
  UINTN                            PayloadSize;
  UINTN                            VersionSize;

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
    case MM_STMM_INFO_FUNCTION_GET_VERSION:
      Version     = (CONST CHAR16 *)PcdGetPtr (PcdFirmwareVersionString);
      VersionSize = StrSize (Version);
      if (VersionSize > PayloadSize) {
        Header->ReturnStatus = EFI_BUFFER_TOO_SMALL;
        break;
      }

      CopyMem (Header->Data, Version, VersionSize);
      Header->ReturnStatus = EFI_SUCCESS;
      break;

    case MM_STMM_INFO_FUNCTION_GET_PAYLOAD_SIZE:
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
FwVersionStandaloneMmInitialize (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_MM_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  Handle;

  Handle = NULL;
  return gMmst->MmiHandlerRegister (MmStmmInfoHandler, &gCixMmStmmInfoCommunicateGuid, &Handle);
}
