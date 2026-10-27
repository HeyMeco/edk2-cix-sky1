/** @file
  Layout of the CRB area shared by SpiTpmAcpiDxe, the TPM2 table and the
  SSDT.  The driver allocates the page (ACPI NVS, below 4 GB) and patches its
  address over SPI_TPM_CRB_PLACEHOLDER in the SSDT and the TPM2 table.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SPI_TPM_ACPI_H_
#define SPI_TPM_ACPI_H_

#define SPI_TPM_CRB_PLACEHOLDER   0x5A5A3F00
#define SPI_TPM_CRB_SIZE          0x1000

//
// PTP CRB register offsets (locality 0 area followed by the control area).
//
#define SPI_TPM_CRB_LOC_STATE     0x00
#define SPI_TPM_CRB_LOC_STS       0x0C
#define SPI_TPM_CRB_INTF_ID       0x30
#define SPI_TPM_CRB_CONTROL_AREA  0x40
#define SPI_TPM_CRB_DATA_BUFFER   0x80
#define SPI_TPM_CRB_DATA_SIZE     (SPI_TPM_CRB_SIZE - SPI_TPM_CRB_DATA_BUFFER)

//
// The FCH SPI controller the TPM is on (PcdDTPMSpiBus 0).
//
#define SPI_TPM_SPI_BASE          0x04090000

#endif
