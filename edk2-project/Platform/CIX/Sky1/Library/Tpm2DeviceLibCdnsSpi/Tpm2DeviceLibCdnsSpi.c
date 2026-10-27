/** @file
  TPM 2.0 device library for a discrete TPM (e.g. Nuvoton NPCT750 on an ASUS
  TPM-SPI module) on a Sky1 FCH SPI controller.

  The FCH SPI controllers are Cadence SPI (r1p6) blocks.  The TPM is driven
  with the TCG PC Client Platform TPM Profile (PTP) SPI protocol: a 4-byte
  header, optional wait states signalled by the TPM on MISO, then up to 64
  data bytes, all under one chip select.  The register access goes straight
  to the controller because the protocol needs chip select held across the
  header, the wait states and the data; SpiDxe only has to have enabled the
  controller clocks (see the [Depex] in the INF).

  Sky1 has no PEI phase, so TPM2_Startup and TPM2_SelfTest are sent here, the
  first time Tcg2Dxe requests the TPM.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>

#include <IndustryStandard/Tpm20.h>
#include <IndustryStandard/TpmTis.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/Tpm2DeviceLib.h>

//
// Cadence SPI registers and bits (Linux drivers/spi/spi-cadence.c).
//
#define CDNS_SPI_CR    0x00
#define CDNS_SPI_ISR   0x04
#define CDNS_SPI_IDR   0x0C
#define CDNS_SPI_ER    0x14
#define CDNS_SPI_TXD   0x1C
#define CDNS_SPI_RXD   0x20

#define CDNS_SPI_CR_MSTREN       BIT0
#define CDNS_SPI_CR_BAUD_DIV     (BIT3 | BIT4 | BIT5)
#define CDNS_SPI_CR_BAUD_SHIFT   3
#define CDNS_SPI_CR_SSCTRL       0x3C00
#define CDNS_SPI_CR_SS_SHIFT     10
#define CDNS_SPI_CR_SSFORCE      BIT14

#define CDNS_SPI_IXR_TXFULL      BIT3
#define CDNS_SPI_IXR_RXNEMTY     BIT4
#define CDNS_SPI_IXR_ALL         0x7F

//
// The FCH SPI reference clock is 200 MHz; SCLK = 200 MHz / (2 << BAUD_DIV).
//
#define SPI_REF_CLOCK_HZ   200000000
#define SPI_TPM_BAUD_DIV   4           // 6.25 MHz
#define SPI_PROBE_BAUD_DIV 7           // 781.25 kHz, for the frame width check

#define SPI_POLL_US        1000

//
// TPM SPI protocol (PTP 6.4.6) and locality 0 FIFO registers.
//
#define TPM_SPI_READ           BIT7
#define TPM_SPI_MAX_TRANSFER   64
#define TPM_SPI_WAIT_RETRIES   50
#define TPM_SPI_ADDRESS_HIGH   0xD4

//
// As in SecurityPkg Tpm2Tis.c: the longest a command may take.
//
#define TIS_TIMEOUT_MAX  (90000 * 1000)   // 90s

#define TPM_ACCESS_0      0x0000
#define TPM_STS_0         0x0018
#define TPM_BURST_0       0x0019
#define TPM_DATA_FIFO_0   0x0024
#define TPM_DID_VID_0     0x0F00
#define TPM_RID_0         0x0F04

STATIC UINTN    mSpiBase;
STATIC UINT32   mCsField;
STATIC BOOLEAN  mTpmFound;
STATIC BOOLEAN  mTpmStarted;

#pragma pack(1)
typedef struct {
  TPM2_COMMAND_HEADER    Header;
  TPM_SU                 StartupType;
} TPM2_STARTUP_COMMAND;

typedef struct {
  TPM2_COMMAND_HEADER    Header;
  TPMI_YES_NO            FullTest;
} TPM2_SELF_TEST_COMMAND;
#pragma pack()

STATIC
UINT32
SpiRead (
  IN UINTN  Offset
  )
{
  return MmioRead32 (mSpiBase + Offset);
}

STATIC
VOID
SpiWrite (
  IN UINTN   Offset,
  IN UINT32  Value
  )
{
  MmioWrite32 (mSpiBase + Offset, Value);
}

STATIC
VOID
SpiDrainRx (
  VOID
  )
{
  UINTN  Count;

  for (Count = 0; Count < 256 && (SpiRead (CDNS_SPI_ISR) & CDNS_SPI_IXR_RXNEMTY) != 0; Count++) {
    SpiRead (CDNS_SPI_RXD);
  }
}

STATIC
VOID
SpiSetBaudDiv (
  IN UINT32  BaudDiv
  )
{
  UINT32  Cr;

  SpiWrite (CDNS_SPI_ER, 0);
  Cr = SpiRead (CDNS_SPI_CR) & ~CDNS_SPI_CR_BAUD_DIV;
  SpiWrite (CDNS_SPI_CR, Cr | (BaudDiv << CDNS_SPI_CR_BAUD_SHIFT));
  SpiWrite (CDNS_SPI_ER, 1);
}

STATIC
VOID
SpiChipSelect (
  IN BOOLEAN  Assert
  )
{
  UINT32  Cr;

  Cr = SpiRead (CDNS_SPI_CR) | CDNS_SPI_CR_SSCTRL;
  if (Assert) {
    Cr = (Cr & ~CDNS_SPI_CR_SSCTRL) | mCsField;
  }

  SpiWrite (CDNS_SPI_CR, Cr);
}

/**
  Wait until the controller reports any of Mask in the interrupt status.
**/
STATIC
EFI_STATUS
SpiWaitStatus (
  IN UINT32   Mask,
  IN BOOLEAN  Set
  )
{
  UINTN  Waited;

  for (Waited = 0; Waited < SPI_POLL_US; Waited++) {
    if (((SpiRead (CDNS_SPI_ISR) & Mask) != 0) == Set) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1);
  }

  return EFI_TIMEOUT;
}

/**
  Shift one byte out and one byte in.
**/
STATIC
EFI_STATUS
SpiTransferByte (
  IN  UINT8  Tx,
  OUT UINT8  *Rx
  )
{
  EFI_STATUS  Status;

  Status = SpiWaitStatus (CDNS_SPI_IXR_TXFULL, FALSE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  SpiWrite (CDNS_SPI_TXD, Tx);
  Status = SpiWaitStatus (CDNS_SPI_IXR_RXNEMTY, TRUE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Rx = (UINT8)SpiRead (CDNS_SPI_RXD);
  return EFI_SUCCESS;
}

/**
  Put the controller in master mode, SPI mode 0, chip select under software
  control and deasserted.
**/
STATIC
VOID
SpiInitController (
  VOID
  )
{
  SpiWrite (CDNS_SPI_ER, 0);
  SpiWrite (CDNS_SPI_IDR, CDNS_SPI_IXR_ALL);
  SpiDrainRx ();
  SpiWrite (CDNS_SPI_ISR, CDNS_SPI_IXR_ALL);
  SpiWrite (
    CDNS_SPI_CR,
    CDNS_SPI_CR_MSTREN | CDNS_SPI_CR_SSFORCE | CDNS_SPI_CR_SSCTRL |
    (SPI_TPM_BAUD_DIV << CDNS_SPI_CR_BAUD_SHIFT)
    );
  SpiWrite (CDNS_SPI_ER, 1);
}

/**
  Measure how many SCLK cycles one TX FIFO entry takes, with chip select
  deasserted.  The TPM protocol needs 8-bit frames; CIX's own SpiLib moves
  32 bits per FIFO entry, so check rather than assume.

  @return  Bits per FIFO entry, or 0 if the controller did not respond.
**/
STATIC
UINT32
SpiFrameBits (
  VOID
  )
{
  UINT64  Frequency;
  UINT64  Start;
  UINT64  Elapsed;
  UINT64  Best;
  UINTN   Try;

  Frequency = GetPerformanceCounterProperties (NULL, NULL);
  if (Frequency == 0) {
    return 0;
  }

  SpiSetBaudDiv (SPI_PROBE_BAUD_DIV);
  Best = MAX_UINT64;
  for (Try = 0; Try < 3; Try++) {
    SpiDrainRx ();
    Start = GetPerformanceCounter ();
    SpiWrite (CDNS_SPI_TXD, 0);
    do {
      Elapsed = GetPerformanceCounter () - Start;
    } while ((SpiRead (CDNS_SPI_ISR) & CDNS_SPI_IXR_RXNEMTY) == 0 &&
             Elapsed < DivU64x32 (Frequency, 1000));

    SpiRead (CDNS_SPI_RXD);
    Best = MIN (Best, Elapsed);
  }

  SpiSetBaudDiv (SPI_TPM_BAUD_DIV);
  if (Best >= DivU64x32 (Frequency, 1000)) {
    return 0;
  }

  return (UINT32)DivU64x64Remainder (
                   MultU64x32 (Best, SPI_REF_CLOCK_HZ / (2 << SPI_PROBE_BAUD_DIV)),
                   Frequency,
                   NULL
                   );
}

/**
  One TPM SPI transaction on locality 0.
**/
STATIC
EFI_STATUS
TpmSpiTransfer (
  IN     UINT16   Register,
  IN     BOOLEAN  Read,
  IN OUT UINT8    *Buffer,
  IN     UINT16   Length
  )
{
  EFI_STATUS  Status;
  UINT8       Header[4];
  UINT8       Rx;
  UINTN       Index;

  ASSERT (Length > 0 && Length <= TPM_SPI_MAX_TRANSFER);

  Status    = EFI_SUCCESS;
  Header[0] = (UINT8)((Read ? TPM_SPI_READ : 0) | (Length - 1));
  Header[1] = TPM_SPI_ADDRESS_HIGH;
  Header[2] = (UINT8)(Register >> 8);
  Header[3] = (UINT8)Register;

  Rx = 0;
  SpiDrainRx ();
  SpiChipSelect (TRUE);
  for (Index = 0; Index < sizeof (Header); Index++) {
    Status = SpiTransferByte (Header[Index], &Rx);
    if (EFI_ERROR (Status)) {
      goto Done;
    }
  }

  //
  // The TPM inserts wait states by returning 0 in bit 0 of the last header
  // byte; keep clocking single bytes until it returns 1.
  //
  for (Index = 0; (Rx & BIT0) == 0; Index++) {
    if (Index == TPM_SPI_WAIT_RETRIES) {
      Status = EFI_TIMEOUT;
      goto Done;
    }

    Status = SpiTransferByte (0, &Rx);
    if (EFI_ERROR (Status)) {
      goto Done;
    }
  }

  for (Index = 0; Index < Length; Index++) {
    if (Read) {
      Status = SpiTransferByte (0, &Buffer[Index]);
    } else {
      Status = SpiTransferByte (Buffer[Index], &Rx);
    }

    if (EFI_ERROR (Status)) {
      goto Done;
    }
  }

Done:
  SpiChipSelect (FALSE);
  return Status;
}

STATIC
UINT8
TpmRead8 (
  IN UINT16  Register
  )
{
  UINT8  Value;

  if (EFI_ERROR (TpmSpiTransfer (Register, TRUE, &Value, sizeof (Value)))) {
    return 0xFF;
  }

  return Value;
}

STATIC
EFI_STATUS
TpmWrite8 (
  IN UINT16  Register,
  IN UINT8   Value
  )
{
  return TpmSpiTransfer (Register, FALSE, &Value, sizeof (Value));
}

STATIC
EFI_STATUS
TpmWaitRegisterBits (
  IN UINT16  Register,
  IN UINT8   BitSet,
  IN UINT8   BitClear,
  IN UINT32  TimeOut
  )
{
  UINT8   Value;
  UINT32  Waited;

  for (Waited = 0; Waited < TimeOut; Waited += 30) {
    Value = TpmRead8 (Register);
    if ((Value != 0xFF) && ((Value & BitSet) == BitSet) && ((Value & BitClear) == 0)) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (30);
  }

  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
TpmReadBurstCount (
  OUT UINT16  *BurstCount
  )
{
  UINT32  Waited;
  UINT8   Data[2];

  for (Waited = 0; Waited < TIS_TIMEOUT_D; Waited += 30) {
    if (!EFI_ERROR (TpmSpiTransfer (TPM_BURST_0, TRUE, Data, sizeof (Data)))) {
      *BurstCount = (UINT16)(Data[0] | (Data[1] << 8));
      if (*BurstCount != 0) {
        return EFI_SUCCESS;
      }
    }

    MicroSecondDelay (30);
  }

  return EFI_TIMEOUT;
}

/**
  Move Length bytes through the data FIFO, in transfers limited by the
  TPM's burst count and the 64-byte SPI transfer size.
**/
STATIC
EFI_STATUS
TpmFifoTransfer (
  IN     BOOLEAN  Read,
  IN OUT UINT8    *Buffer,
  IN     UINT32   Length
  )
{
  EFI_STATUS  Status;
  UINT16      BurstCount;
  UINT32      Chunk;

  while (Length > 0) {
    Status = TpmReadBurstCount (&BurstCount);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Chunk  = MIN (MIN ((UINT32)BurstCount, Length), TPM_SPI_MAX_TRANSFER);
    Status = TpmSpiTransfer (TPM_DATA_FIFO_0, Read, Buffer, (UINT16)Chunk);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Buffer += Chunk;
    Length -= Chunk;
  }

  return EFI_SUCCESS;
}

/**
  Send a command and read the response, as in the memory-mapped TIS
  implementation in SecurityPkg (Tpm2DeviceLibDTpm/Tpm2Tis.c).
**/
STATIC
EFI_STATUS
TpmCommand (
  IN     UINT8   *BufferIn,
  IN     UINT32  SizeIn,
  IN OUT UINT8   *BufferOut,
  IN OUT UINT32  *SizeOut
  )
{
  EFI_STATUS  Status;
  UINT32      TpmOutSize;
  UINT8       Cancel[4];

  if (*SizeOut < sizeof (TPM2_RESPONSE_HEADER)) {
    return EFI_BUFFER_TOO_SMALL;
  }

  TpmWrite8 (TPM_STS_0, TIS_PC_STS_READY);
  Status = TpmWaitRegisterBits (TPM_STS_0, TIS_PC_STS_READY, 0, TIS_TIMEOUT_B);
  if (EFI_ERROR (Status)) {
    DebugPrint (DEBUG_ERROR, "SpiTpm: TPM not ready for a command\n");
    return EFI_DEVICE_ERROR;
  }

  Status = TpmFifoTransfer (FALSE, BufferIn, SizeIn);
  if (EFI_ERROR (Status)) {
    Status = EFI_DEVICE_ERROR;
    goto Exit;
  }

  Status = TpmWaitRegisterBits (TPM_STS_0, TIS_PC_VALID, TIS_PC_STS_EXPECT, TIS_TIMEOUT_C);
  if (EFI_ERROR (Status)) {
    DebugPrint (DEBUG_ERROR, "SpiTpm: TPM still expects command data\n");
    Status = EFI_BUFFER_TOO_SMALL;
    goto Exit;
  }

  TpmWrite8 (TPM_STS_0, TIS_PC_STS_GO);

  //
  // Some commands (key generation) take many seconds.
  //
  Status = TpmWaitRegisterBits (TPM_STS_0, TIS_PC_VALID | TIS_PC_STS_DATA, 0, TIS_TIMEOUT_MAX);
  if (EFI_ERROR (Status)) {
    DebugPrint (DEBUG_ERROR, "SpiTpm: no response, cancelling the command\n");
    ZeroMem (Cancel, sizeof (Cancel));
    Cancel[3] = (UINT8)(TIS_PC_STS_CANCEL >> 24);
    TpmSpiTransfer (TPM_STS_0, FALSE, Cancel, sizeof (Cancel));
    Status = TpmWaitRegisterBits (TPM_STS_0, TIS_PC_VALID | TIS_PC_STS_DATA, 0, TIS_TIMEOUT_B);
    if (EFI_ERROR (Status)) {
      Status = EFI_DEVICE_ERROR;
      goto Exit;
    }
  }

  Status = TpmFifoTransfer (TRUE, BufferOut, sizeof (TPM2_RESPONSE_HEADER));
  if (EFI_ERROR (Status)) {
    Status = EFI_DEVICE_ERROR;
    goto Exit;
  }

  if (SwapBytes16 (ReadUnaligned16 ((UINT16 *)BufferOut)) == TPM_ST_RSP_COMMAND) {
    DebugPrint (DEBUG_ERROR, "SpiTpm: TPM 1.2 response tag\n");
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  TpmOutSize = SwapBytes32 (ReadUnaligned32 ((UINT32 *)(BufferOut + 2)));
  if ((TpmOutSize < sizeof (TPM2_RESPONSE_HEADER)) || (*SizeOut < TpmOutSize)) {
    Status = EFI_BUFFER_TOO_SMALL;
    goto Exit;
  }

  *SizeOut = TpmOutSize;
  Status   = TpmFifoTransfer (
               TRUE,
               BufferOut + sizeof (TPM2_RESPONSE_HEADER),
               TpmOutSize - sizeof (TPM2_RESPONSE_HEADER)
               );
  if (EFI_ERROR (Status)) {
    Status = EFI_DEVICE_ERROR;
  }

Exit:
  TpmWrite8 (TPM_STS_0, TIS_PC_STS_READY);
  return Status;
}

/**
  Send a command that only returns a header and return its response code.
**/
STATIC
EFI_STATUS
TpmSimpleCommand (
  IN  VOID    *Command,
  IN  UINT32  Size,
  OUT UINT32  *ResponseCode
  )
{
  EFI_STATUS            Status;
  TPM2_RESPONSE_HEADER  Response;
  UINT32                ResponseSize;

  ResponseSize = sizeof (Response);
  Status       = TpmCommand (Command, Size, (UINT8 *)&Response, &ResponseSize);
  if (!EFI_ERROR (Status)) {
    *ResponseCode = SwapBytes32 (Response.responseCode);
  }

  return Status;
}

/**
  TPM2_Startup(CLEAR) and TPM2_SelfTest(full), which a PEI phase would
  otherwise have sent.  TPM_RC_INITIALIZE means it was already started.
**/
STATIC
EFI_STATUS
TpmStartup (
  VOID
  )
{
  EFI_STATUS              Status;
  TPM2_STARTUP_COMMAND    Startup;
  TPM2_SELF_TEST_COMMAND  SelfTest;
  UINT32                  ResponseCode;

  Startup.Header.tag         = SwapBytes16 (TPM_ST_NO_SESSIONS);
  Startup.Header.paramSize   = SwapBytes32 (sizeof (Startup));
  Startup.Header.commandCode = SwapBytes32 (TPM_CC_Startup);
  Startup.StartupType        = SwapBytes16 (TPM_SU_CLEAR);
  Status                     = TpmSimpleCommand (&Startup, sizeof (Startup), &ResponseCode);
  if (EFI_ERROR (Status) ||
      ((ResponseCode != TPM_RC_SUCCESS) && (ResponseCode != TPM_RC_INITIALIZE)))
  {
    DebugPrint (DEBUG_ERROR, "SpiTpm: TPM2_Startup failed: %r, rc 0x%x\n", Status, ResponseCode);
    return EFI_DEVICE_ERROR;
  }

  SelfTest.Header.tag         = SwapBytes16 (TPM_ST_NO_SESSIONS);
  SelfTest.Header.paramSize   = SwapBytes32 (sizeof (SelfTest));
  SelfTest.Header.commandCode = SwapBytes32 (TPM_CC_SelfTest);
  SelfTest.FullTest           = YES;
  Status                      = TpmSimpleCommand (&SelfTest, sizeof (SelfTest), &ResponseCode);
  if (EFI_ERROR (Status) || (ResponseCode != TPM_RC_SUCCESS)) {
    DebugPrint (DEBUG_ERROR, "SpiTpm: TPM2_SelfTest: %r, rc 0x%x\n", Status, ResponseCode);
  }

  return EFI_SUCCESS;
}

/**
  Set up the controller and look for a TPM; done once per module.
**/
STATIC
EFI_STATUS
TpmSpiProbe (
  VOID
  )
{
  STATIC BOOLEAN  Probed;
  UINT32          FrameBits;
  UINT8           DidVid[4];
  UINT32          Id;

  if (Probed) {
    return mTpmFound ? EFI_SUCCESS : EFI_NOT_FOUND;
  }

  Probed   = TRUE;
  mSpiBase = (FixedPcdGet8 (PcdDTPMSpiBus) == 0) ? 0x04090000 : 0x040A0000;
  mCsField = ((~(UINT32)FixedPcdGet8 (PcdDTPMSpiChipSelect)) << CDNS_SPI_CR_SS_SHIFT) & CDNS_SPI_CR_SSCTRL;

  SpiInitController ();
  FrameBits = SpiFrameBits ();
  if ((FrameBits < 6) || (FrameBits > 16)) {
    DebugPrint (
      DEBUG_ERROR,
      "SpiTpm: SPI%u sends %u bits per FIFO entry (need 8); TPM not supported\n",
      FixedPcdGet8 (PcdDTPMSpiBus),
      FrameBits
      );
    return EFI_NOT_FOUND;
  }

  if (EFI_ERROR (TpmSpiTransfer (TPM_DID_VID_0, TRUE, DidVid, sizeof (DidVid)))) {
    DebugPrint (DEBUG_INFO, "SpiTpm: no TPM on SPI%u (no response)\n", FixedPcdGet8 (PcdDTPMSpiBus));
    return EFI_NOT_FOUND;
  }

  Id = ReadUnaligned32 ((UINT32 *)DidVid);
  if ((Id == 0) || (Id == MAX_UINT32)) {
    DebugPrint (DEBUG_INFO, "SpiTpm: no TPM on SPI%u (ID 0x%08x)\n", FixedPcdGet8 (PcdDTPMSpiBus), Id);
    return EFI_NOT_FOUND;
  }

  DebugPrint (
    DEBUG_INFO,
    "SpiTpm: SPI%u: vendor 0x%04x device 0x%04x revision 0x%02x\n",
    FixedPcdGet8 (PcdDTPMSpiBus),
    Id & 0xFFFF,
    Id >> 16,
    TpmRead8 (TPM_RID_0)
    );
  mTpmFound = TRUE;
  return EFI_SUCCESS;
}

/**
  This service enables the sending of commands to the TPM2.

  @param[in]      InputParameterBlockSize  Size of the TPM2 input parameter block.
  @param[in]      InputParameterBlock      Pointer to the TPM2 input parameter block.
  @param[in,out]  OutputParameterBlockSize Size of the TPM2 output parameter block.
  @param[in]      OutputParameterBlock     Pointer to the TPM2 output parameter block.

  @retval EFI_SUCCESS            The command byte stream was successfully sent to the device and a response was successfully received.
  @retval EFI_DEVICE_ERROR       The command was not successfully sent to the device or a response was not successfully received from the device.
  @retval EFI_BUFFER_TOO_SMALL   The output parameter block is too small.
**/
EFI_STATUS
EFIAPI
Tpm2SubmitCommand (
  IN UINT32      InputParameterBlockSize,
  IN UINT8       *InputParameterBlock,
  IN OUT UINT32  *OutputParameterBlockSize,
  IN UINT8       *OutputParameterBlock
  )
{
  if (!mTpmFound) {
    return EFI_DEVICE_ERROR;
  }

  return TpmCommand (
           InputParameterBlock,
           InputParameterBlockSize,
           OutputParameterBlock,
           OutputParameterBlockSize
           );
}

/**
  This service requests use TPM2.

  @retval EFI_SUCCESS      Get the control of TPM2 chip.
  @retval EFI_NOT_FOUND    TPM2 not found.
  @retval EFI_DEVICE_ERROR Unexpected device behavior.
**/
EFI_STATUS
EFIAPI
Tpm2RequestUseTpm (
  VOID
  )
{
  EFI_STATUS  Status;

  Status = TpmSpiProbe ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  TpmWrite8 (TPM_ACCESS_0, TIS_PC_ACC_RQUUSE);
  Status = TpmWaitRegisterBits (TPM_ACCESS_0, TIS_PC_ACC_ACTIVE | TIS_PC_VALID, 0, TIS_TIMEOUT_A);
  if (EFI_ERROR (Status)) {
    DebugPrint (DEBUG_ERROR, "SpiTpm: locality 0 not granted: %r\n", Status);
    return EFI_DEVICE_ERROR;
  }

  if (!mTpmStarted) {
    Status = TpmStartup ();
    if (EFI_ERROR (Status)) {
      return Status;
    }

    mTpmStarted = TRUE;
  }

  return EFI_SUCCESS;
}

/**
  This service registers a TPM2 device; only the router library uses it.

  @retval EFI_UNSUPPORTED  This library is not a router.
**/
EFI_STATUS
EFIAPI
Tpm2RegisterTpm2DeviceLib (
  IN TPM2_DEVICE_INTERFACE  *Tpm2Device
  )
{
  return EFI_UNSUPPORTED;
}
