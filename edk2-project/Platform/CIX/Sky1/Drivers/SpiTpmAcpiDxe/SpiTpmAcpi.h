/** @file
  Layout of the CRB area shared by SpiTpmAcpiDxe, the TPM2 table and the
  SSDT.  The driver allocates the page (ACPI NVS, below 4 GB) and patches its
  address over SPI_TPM_CRB_PLACEHOLDER in the SSDT and the TPM2 table, and
  the base of the SPI controller (PcdDTPMSpiBus) over
  SPI_TPM_SPI_PLACEHOLDER and the chip select value of its configuration
  register (PcdDTPMSpiChipSelect) over SPI_TPM_CS_PLACEHOLDER in the SSDT.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SPI_TPM_ACPI_H_
#define SPI_TPM_ACPI_H_

#define SPI_TPM_CRB_PLACEHOLDER   0x5A5A3F00
#define SPI_TPM_SPI_PLACEHOLDER   0x5A5A3F01
#define SPI_TPM_CS_PLACEHOLDER    0x5A5A3F02
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
// FCH SPI controllers (PcdDTPMSpiBus 0 and 1).
//
#define SPI_TPM_SPI_BASE(Bus)     (((Bus) == 0) ? 0x04090000 : 0x040A0000)

//
// Cadence SPI configuration register slave select field [13:10] with the
// chip select in PcdDTPMSpiChipSelect (a mask: 1 = SS0, 2 = SS1) asserted.
//
#define SPI_TPM_SPI_CS_FIELD(Mask)  ((~(UINT32)(Mask) << 10) & 0x3C00)

//
// Trace the OS's calls into the TPM device (each _DSM, and the result of
// each command) on the debug UART (PL011), while the OS side is brought up.
//
#define SPI_TPM_TRACE_UART  0x040D0000

#endif
