/** @file
  Publish the discrete SPI TPM to the OS as a CRB TPM with the ACPI start
  method.

  The OS writes a command into the CRB data buffer, sets Start and calls the
  TPM device's _DSM (SpiTpm.asl), which sends the command to the TPM over
  SPI, copies the response back and clears Start.  This driver allocates and
  fills in the CRB area, installs the TPM2 table and the SSDT with the
  area's address, hides the SPI controller from the OS (its driver would
  compete with the _DSM for the controller) and locks the TPM platform
  hierarchy at EndOfDxe.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/AcpiAml.h>
#include <IndustryStandard/Tpm2Acpi.h>
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TpmPlatformHierarchyLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Protocol/AcpiTable.h>

#include "SpiTpmAcpi.h"

//
// LOC_STATE: tpmRegValidSts | locAssigned (locality 0 active).
// LOC_STS: Granted.
// INTF_ID: CRB interface type and version, idle bypass, 64-byte transfers,
// CRB capable, CRB selected.
//
#define CRB_LOC_STATE_VALUE  (BIT7 | BIT1)
#define CRB_LOC_STS_VALUE    BIT0
#define CRB_INTF_ID_VALUE    (0x1 | (0x1 << 4) | BIT9 | (0x3 << 11) | BIT14 | (0x1 << 17))

STATIC EFI_PHYSICAL_ADDRESS  mCrbBase;

/**
  Allocate the CRB page and fill in the registers the OS reads.
**/
STATIC
EFI_STATUS
InitCrb (
  VOID
  )
{
  EFI_STATUS                  Status;
  EFI_TPM2_ACPI_CONTROL_AREA  *Control;

  //
  // Below 4 GB: the SSDT describes it with Memory32Fixed.
  //
  mCrbBase = MAX_UINT32;
  Status   = gBS->AllocatePages (
                    AllocateMaxAddress,
                    EfiACPIMemoryNVS,
                    EFI_SIZE_TO_PAGES (SPI_TPM_CRB_SIZE),
                    &mCrbBase
                    );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem ((VOID *)(UINTN)mCrbBase, SPI_TPM_CRB_SIZE);
  *(UINT32 *)(UINTN)(mCrbBase + SPI_TPM_CRB_LOC_STATE) = CRB_LOC_STATE_VALUE;
  *(UINT32 *)(UINTN)(mCrbBase + SPI_TPM_CRB_LOC_STS)   = CRB_LOC_STS_VALUE;
  *(UINT32 *)(UINTN)(mCrbBase + SPI_TPM_CRB_INTF_ID)   = CRB_INTF_ID_VALUE;

  Control               = (EFI_TPM2_ACPI_CONTROL_AREA *)(UINTN)(mCrbBase + SPI_TPM_CRB_CONTROL_AREA);
  Control->CommandSize  = SPI_TPM_CRB_DATA_SIZE;
  Control->Command      = mCrbBase + SPI_TPM_CRB_DATA_BUFFER;
  Control->ResponseSize = SPI_TPM_CRB_DATA_SIZE;
  Control->Response     = mCrbBase + SPI_TPM_CRB_DATA_BUFFER;
  return EFI_SUCCESS;
}

/**
  Replace the DWordConst SPI_TPM_CRB_PLACEHOLDER in the SSDT (CRBA) with the
  CRB address.
**/
STATIC
EFI_STATUS
PatchSsdt (
  IN OUT EFI_ACPI_DESCRIPTION_HEADER  *Table
  )
{
  UINT8  *Aml;
  UINTN  Index;
  UINTN  Count;

  Aml   = (UINT8 *)Table;
  Count = 0;
  for (Index = sizeof (*Table); Index + sizeof (UINT32) < Table->Length; Index++) {
    if ((Aml[Index] == AML_DWORD_PREFIX) &&
        (ReadUnaligned32 ((UINT32 *)&Aml[Index + 1]) == SPI_TPM_CRB_PLACEHOLDER))
    {
      WriteUnaligned32 ((UINT32 *)&Aml[Index + 1], (UINT32)mCrbBase);
      Index += sizeof (UINT32);
      Count++;
    }
  }

  return (Count == 1) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

/**
  Install the TPM2 table and the SSDT carried as raw sections in this
  driver's FFS file, with the CRB address filled in.
**/
STATIC
EFI_STATUS
InstallTables (
  VOID
  )
{
  EFI_STATUS                   Status;
  EFI_ACPI_TABLE_PROTOCOL      *AcpiTable;
  EFI_ACPI_DESCRIPTION_HEADER  *Table;
  UINTN                        Size;
  UINTN                        Instance;
  UINTN                        Key;
  UINTN                        Installed;

  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&AcpiTable);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Installed = 0;
  for (Instance = 0; ; Instance++) {
    Status = GetSectionFromFv (&gEfiCallerIdGuid, EFI_SECTION_RAW, Instance, (VOID **)&Table, &Size);
    if (EFI_ERROR (Status)) {
      break;
    }

    if ((Size < sizeof (*Table)) || (Size < Table->Length)) {
      FreePool (Table);
      continue;
    }

    Status = EFI_SUCCESS;
    if (Table->Signature == EFI_ACPI_5_0_TRUSTED_COMPUTING_PLATFORM_2_TABLE_SIGNATURE) {
      ((EFI_TPM2_ACPI_TABLE *)Table)->AddressOfControlArea = mCrbBase + SPI_TPM_CRB_CONTROL_AREA;
    } else if (Table->Signature == EFI_ACPI_6_3_SECONDARY_SYSTEM_DESCRIPTION_TABLE_SIGNATURE) {
      Status = PatchSsdt (Table);
    }

    if (!EFI_ERROR (Status)) {
      Status = AcpiTable->InstallAcpiTable (AcpiTable, Table, Table->Length, &Key);
    }

    if (EFI_ERROR (Status)) {
      DebugPrint (DEBUG_ERROR, "SpiTpmAcpiDxe: installing %-4.4a failed: %r\n", (CHAR8 *)&Table->Signature, Status);
    } else {
      Installed++;
    }

    FreePool (Table);
  }

  return (Installed == 2) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

/**
  Report \_SB.SPI0 as absent in the DSDT: with the TPM on it, the _DSM owns
  the controller, and an OS SPI driver (CixSpiDriver on Windows, the CIX
  spi-cadence driver on Linux) would reprogram it in the middle of a
  command.
**/
STATIC
VOID
EFIAPI
HideSpiController (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS              Status;
  EFI_ACPI_SDT_PROTOCOL   *AcpiSdt;
  EFI_ACPI_SDT_HEADER     *Table;
  EFI_ACPI_TABLE_VERSION  Version;
  UINTN                   Key;
  UINTN                   Index;
  EFI_ACPI_HANDLE         TableHandle;
  EFI_ACPI_HANDLE         Object;
  EFI_ACPI_DATA_TYPE      DataType;
  UINT8                   *Data;
  UINTN                   DataSize;
  BOOLEAN                 Hidden;

  gBS->CloseEvent (Event);

  Status = gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL, (VOID **)&AcpiSdt);
  if (EFI_ERROR (Status)) {
    DebugPrint (DEBUG_ERROR, "SpiTpmAcpiDxe: no ACPI SDT protocol, SPI0 left visible\n");
    return;
  }

  Hidden = FALSE;
  for (Index = 0; !EFI_ERROR (AcpiSdt->GetAcpiTable (Index, &Table, &Version, &Key)); Index++) {
    if (Table->Signature != EFI_ACPI_6_3_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE) {
      continue;
    }

    if (EFI_ERROR (AcpiSdt->OpenSdt (Key, &TableHandle))) {
      continue;
    }

    if (!EFI_ERROR (AcpiSdt->FindPath (TableHandle, "\\_SB.SPI0._STA", &Object))) {
      //
      // Name (_STA, 0x0F): option 2 is the ByteConst.
      //
      Status = AcpiSdt->GetOption (Object, 2, &DataType, (CONST VOID **)&Data, &DataSize);
      if (!EFI_ERROR (Status) && (DataType == EFI_ACPI_DATA_TYPE_CHILD) &&
          (DataSize >= 2) && (Data[0] == AML_BYTE_PREFIX))
      {
        Data[1]         = 0;
        Table->Checksum = 0;
        Table->Checksum = CalculateCheckSum8 ((UINT8 *)Table, Table->Length);
        Hidden          = TRUE;
      }

      AcpiSdt->Close (Object);
    }

    AcpiSdt->Close (TableHandle);
  }

  if (!Hidden) {
    DebugPrint (DEBUG_ERROR, "SpiTpmAcpiDxe: \\_SB.SPI0._STA not found, SPI0 left visible\n");
  }
}

/**
  Randomize the platform hierarchy authorization and disable the platform
  hierarchy before third-party code runs (what Tcg2PlatformDxe does at
  SmmReadyToLock, which this platform does not signal).
**/
STATIC
VOID
EFIAPI
LockPlatformHierarchy (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gBS->CloseEvent (Event);
  ConfigureTpmPlatformHierarchy ();
}

/**
  Entry point.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The TPM is published to the OS.
  @retval other             The CRB area or the tables could not be set up.
**/
EFI_STATUS
EFIAPI
SpiTpmAcpiDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  LockPlatformHierarchy,
                  NULL,
                  &gEfiEndOfDxeEventGroupGuid,
                  &Event
                  );
  ASSERT_EFI_ERROR (Status);

  Status = InitCrb ();
  if (!EFI_ERROR (Status)) {
    Status = InstallTables ();
  }

  if (EFI_ERROR (Status)) {
    DebugPrint (DEBUG_ERROR, "SpiTpmAcpiDxe: TPM not published to the OS: %r\n", Status);
    return EFI_SUCCESS;
  }

  Status = EfiCreateEventReadyToBootEx (TPL_CALLBACK, HideSpiController, NULL, &Event);
  ASSERT_EFI_ERROR (Status);

  DebugPrint (DEBUG_INFO, "SpiTpmAcpiDxe: TPM published, CRB at 0x%lx\n", mCrbBase);
  return EFI_SUCCESS;
}
