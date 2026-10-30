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

  The Sky1 controllers have a 32-bit FIFO (Linux: "cix,sky1-spi-r1p6"), so
  every FIFO entry shifts four bytes, most significant byte first.  The
  header fills one entry.  Wait states are counted in bytes, so the data can
  start part way through an entry: reads keep clocking until the TPM's ready
  byte and the data have been seen, and writes place the data after a guess
  of the wait state length (the length last seen).  The controller is fed
  one entry at a time, so a write stops as soon as the guess can be checked:
  if the TPM is still waiting where the data should start, it has taken
  nothing and the transfer is repeated; if it was ready elsewhere, it has
  taken shifted data, so register writes are repeated and a command is sent
  again from the start.

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
#include <Library/GpioLib.h>
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
#define SPI_WORD_BYTES     4

//
// TPM SPI protocol (PTP 6.4.6) and locality 0 FIFO registers.
//
#define TPM_SPI_READ           BIT7
#define TPM_SPI_MAX_TRANSFER   64
#define TPM_SPI_MAX_WAIT       52          // wait state bytes
//
// Measured on the NPCT750 (raw transactions, 2026-10-30): a transfer of up
// to 16 bytes with the header is answered after one wait state byte, a
// longer one only after seven or eight (ready in byte 10 or 11).  Writes
// must guess where the data goes, so they stay within 12 bytes; reads
// follow the ready byte wherever it is and start from the longer guess.
//
#define TPM_SPI_MAX_WRITE      12
#define TPM_SPI_LONG_WAIT      7
#define TPM_SPI_ADDRESS_HIGH   0xD4
#define TPM_SPI_MAX_STREAM     (4 + TPM_SPI_MAX_WAIT + TPM_SPI_MAX_TRANSFER + SPI_WORD_BYTES)
#define TPM_SPI_WRITE_RETRIES  100
#define TPM_SPI_BUSY_RETRIES   2000        // of a FIFO transfer, 50 us apart
#define TPM_SPI_BUSY_DELAY_US  50
#define TPM_COMMAND_RETRIES    4

//
// As in SecurityPkg Tpm2Tis.c: the longest a command may take.
//
#define TIS_TIMEOUT_MAX  (90000 * 1000)   // 90s

#define TPM_ACCESS_0      0x0000
#define TPM_STS_0         0x0018
#define TPM_BURST_0       0x0019
#define TPM_DATA_FIFO_0   0x0024
#define TPM_DID_VID_0     0x0F00

//
// The S5 pads of the first FCH SPI controller (SPI1_MISO, _CS0, _CS1, _MOSI,
// _CLK in CIX's pad names), which the board's PinMuxTableLib leaves alone.
// Values as in the board ACPI's pinctrl_fch_spi0 group: function 0 (SPI),
// pull-up, Schmitt trigger input, drive strength 12; the clock without the
// pull-up.  Layout: function [8:7], pull-up 6, pull-down 5, Schmitt 4, drive
// [3:0].
//
#define SPI0_PAD_BASE     (0x16007000 + 0xA8)
#define SPI0_PAD_COUNT    5
#define PAD_CONFIG_MASK   0x1FF
#define PAD_SPI_DATA      0x5C
#define PAD_SPI_CLOCK     0x1C

//
// TPM_RST_L is GPIO007 (Minisforum's notes in the board's GpioTableLib,
// which only sets it high).  Low holds the TPM in reset.
//
#define TPM_RESET_GPIO     7
#define TPM_RESET_WAIT_US  (100 * 1000)
#define TPM_RID_0         0x0F04

STATIC UINTN    mSpiBase;
STATIC UINT32   mCsField;
STATIC BOOLEAN  mTpmFound;
STATIC BOOLEAN  mTpmStarted;

//
// Wait state bytes last seen: for the registers, for data FIFO transfers of
// up to TPM_SPI_MAX_WRITE bytes, and for longer data FIFO reads.
//
STATIC UINT8  mWaitGuess[3];

/**
  Which of mWaitGuess a transfer uses.
**/
STATIC
UINT8 *
TpmWaitGuess (
  IN UINT16  Register,
  IN UINT16  Length
  )
{
  if (Register != TPM_DATA_FIFO_0) {
    return &mWaitGuess[0];
  }

  return &mWaitGuess[(Length <= TPM_SPI_MAX_WRITE) ? 1 : 2];
}

//
// Bring-up diagnostics (define TPM_SPI_DIAG): after TPM2_Startup, log the
// burst count and which data FIFO write sizes the TPM takes, with the bytes
// it returned, and raw transactions showing where it reports ready.
//

#ifdef TPM_SPI_DIAG
STATIC UINT8  mLastRx[8];
STATIC UINTN  mLastReady;
#endif

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
  Shift one FIFO entry (four bytes, most significant first) out and one in.
**/
STATIC
EFI_STATUS
SpiTransferWord (
  IN  UINT32  Tx,
  OUT UINT32  *Rx
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

  *Rx = SpiRead (CDNS_SPI_RXD);
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
  deasserted, to check that the FIFO is 32 bits wide.

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

  @retval EFI_SUCCESS    The transfer was done.
  @retval EFI_NOT_READY  A write found the TPM still waiting where the data
                         should start; it took no data, and the transfer can
                         be repeated.
  @retval EFI_ABORTED    A write met a different wait state length than
                         guessed, so the TPM took shifted data; the guess is
                         updated for the next try.
  @retval EFI_TIMEOUT    A read found the TPM waiting for longer than
                         TPM_SPI_MAX_WAIT bytes (it took nothing), or the
                         controller did not respond.
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
  UINT8       Tx[TPM_SPI_MAX_STREAM];
  UINT8       Rx[TPM_SPI_MAX_STREAM];
  UINT8       *Guess;
  UINTN       Wait;
  UINTN       Total;
  UINTN       Sent;
  UINTN       Ready;
  UINTN       Index;
  UINT32      Word;

  ASSERT (Length > 0 && Length <= TPM_SPI_MAX_TRANSFER);

  Guess = TpmWaitGuess (Register, Length);
  Wait  = *Guess;
  ZeroMem (Tx, sizeof (Tx));
  Tx[0] = (UINT8)((Read ? TPM_SPI_READ : 0) | (Length - 1));
  Tx[1] = TPM_SPI_ADDRESS_HIGH;
  Tx[2] = (UINT8)(Register >> 8);
  Tx[3] = (UINT8)Register;
  if (!Read) {
    CopyMem (&Tx[4 + Wait], Buffer, Length);
  }

  //
  // Ready is the index of the byte in which the TPM returned 1 in bit 0:
  // the last header byte, or the last wait state byte.  0 = not seen yet.
  //
  Status = EFI_SUCCESS;
  Total  = ALIGN_VALUE (4 + Wait + Length, SPI_WORD_BYTES);
  Ready  = 0;
  SpiDrainRx ();
  SpiChipSelect (TRUE);
  for (Sent = 0; Sent < Total; Sent += SPI_WORD_BYTES) {
    Word   = SwapBytes32 (ReadUnaligned32 ((UINT32 *)&Tx[Sent]));
    Status = SpiTransferWord (Word, &Word);
    if (EFI_ERROR (Status)) {
      goto Done;
    }

    WriteUnaligned32 ((UINT32 *)&Rx[Sent], SwapBytes32 (Word));
    for (Index = MAX (Sent, 3); Ready == 0 && Index < Sent + SPI_WORD_BYTES; Index++) {
      if ((Rx[Index] & BIT0) != 0) {
        Ready = Index;
      }
    }

    //
    // A write stops as soon as the guess can be checked.
    //
    if (!Read) {
      if ((Ready != 0) && (Ready != 3 + Wait)) {
        *Guess = (UINT8)MIN (Ready - 3, TPM_SPI_MAX_WAIT);
        Status = EFI_ABORTED;
        goto Done;
      }

      //
      // Not ready where the data starts: the TPM took nothing.  Usually it
      // is busy and a retry with the same guess goes through.  But if the
      // expected ready byte ended this entry, a guess one short is never
      // seen as shifted (the ready byte would be in the next entry), so
      // try one more wait state byte; an overshoot is caught as shifted.
      //
      if ((Ready == 0) && (Sent + SPI_WORD_BYTES > 3 + Wait)) {
        if ((Sent == Wait) && (Wait < TPM_SPI_MAX_WAIT)) {
          *Guess = (UINT8)(Wait + 1);
        }

        Status = EFI_NOT_READY;
        goto Done;
      }
    }

    //
    // A read goes on until the data after the ready byte is in.
    //
    if (Read) {
      if (Ready != 0) {
        Total = ALIGN_VALUE (Ready + 1 + Length, SPI_WORD_BYTES);
      } else if (Sent + SPI_WORD_BYTES < 4 + TPM_SPI_MAX_WAIT) {
        Total = Sent + 2 * SPI_WORD_BYTES;
      }
    }
  }

  if (Ready == 0) {
    Status = EFI_TIMEOUT;
    goto Done;
  }

  *Guess = (UINT8)MIN (Ready - 3, TPM_SPI_MAX_WAIT);
  if (Read) {
    CopyMem (Buffer, &Rx[Ready + 1], Length);
  }

Done:
  SpiChipSelect (FALSE);
#ifdef TPM_SPI_DIAG
  CopyMem (mLastRx, Rx, sizeof (mLastRx));
  mLastReady = Ready;
#endif
  return Status;
}

/**
  Write a register, again while the TPM is busy or if a wait state shifted
  the data.  Writing 0 or a shifted copy of the value to the access or
  status register does nothing.
**/
STATIC
EFI_STATUS
TpmWriteRegister (
  IN UINT16  Register,
  IN UINT8   *Buffer,
  IN UINT16  Length
  )
{
  EFI_STATUS  Status;
  UINTN       Try;

  for (Try = 0; Try < TPM_SPI_WRITE_RETRIES; Try++) {
    Status = TpmSpiTransfer (Register, FALSE, Buffer, Length);
    if ((Status != EFI_NOT_READY) && (Status != EFI_ABORTED)) {
      break;
    }

    MicroSecondDelay (10);
  }

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
  return TpmWriteRegister (Register, &Value, sizeof (Value));
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
  Put the TPM in the Ready state.  commandReady moves it from Idle to Ready,
  but from Reception or Completion only to Idle (dropping the command or
  response), so keep writing it until the TPM reports Ready.
**/
STATIC
EFI_STATUS
TpmEnterReady (
  VOID
  )
{
  UINT32  Waited;
  UINT8   Status;

  for (Waited = 0; Waited < TIS_TIMEOUT_B; Waited += 1000) {
    TpmWrite8 (TPM_STS_0, TIS_PC_STS_READY);
    Status = TpmRead8 (TPM_STS_0);
    if ((Status != 0xFF) && ((Status & TIS_PC_STS_READY) != 0)) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1000);
  }

  return EFI_TIMEOUT;
}

/**
  Move Length bytes through the data FIFO, in transfers limited by the
  TPM's burst count and the 64-byte SPI transfer size, and where possible
  sized to end on a FIFO entry.  A transfer the TPM was too busy to take
  (it took nothing) is repeated.  EFI_ABORTED: see TpmSpiTransfer.
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
  UINTN       Try;

  while (Length > 0) {
    Status = TpmReadBurstCount (&BurstCount);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Chunk = MIN (MIN ((UINT32)BurstCount, Length), Read ? TPM_SPI_MAX_TRANSFER : TPM_SPI_MAX_WRITE);
    if (Chunk > SPI_WORD_BYTES) {
      Chunk -= (*TpmWaitGuess (TPM_DATA_FIFO_0, (UINT16)Chunk) + Chunk) % SPI_WORD_BYTES;
    }

    for (Try = 0; Try < TPM_SPI_BUSY_RETRIES; Try++) {
      Status = TpmSpiTransfer (TPM_DATA_FIFO_0, Read, Buffer, (UINT16)Chunk);
      if ((Status != EFI_NOT_READY) && (Status != EFI_TIMEOUT)) {
        break;
      }

      MicroSecondDelay (TPM_SPI_BUSY_DELAY_US);
    }

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
  UINTN       Try;

  if (*SizeOut < sizeof (TPM2_RESPONSE_HEADER)) {
    return EFI_BUFFER_TOO_SMALL;
  }

  //
  // Going back to Ready drops what the TPM has received, so a command whose
  // FIFO write went wrong can be sent again.
  //
  for (Try = 0; ; Try++) {
    Status = TpmEnterReady ();
    if (EFI_ERROR (Status)) {
      DebugPrint (DEBUG_ERROR, "SpiTpm: TPM not ready for a command (status 0x%02x)\n", TpmRead8 (TPM_STS_0));
      return EFI_DEVICE_ERROR;
    }

    Status = TpmFifoTransfer (FALSE, BufferIn, SizeIn);
    if ((Status != EFI_ABORTED) || (Try + 1 == TPM_COMMAND_RETRIES)) {
      break;
    }

    DebugPrint (
      DEBUG_INFO,
      "SpiTpm: wait state shifted the command, sending it again (%u wait bytes)\n",
      mWaitGuess[1]
      );
  }

  //
  // Tcg2Dxe stops using the TPM after a device error, so say why.
  //
  if (EFI_ERROR (Status)) {
    DebugPrint (
      DEBUG_ERROR,
      "SpiTpm: TPM did not take command 0x%x: %r (%u wait bytes)\n",
      SwapBytes32 (ReadUnaligned32 ((UINT32 *)&BufferIn[6])),
      Status,
      mWaitGuess[1]
      );
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
    TpmWriteRegister (TPM_STS_0, Cancel, sizeof (Cancel));
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

#ifdef TPM_SPI_DIAG
/**
  Clock Bytes (a multiple of four) out and in with chip select held, with no
  look at what comes back.
**/
STATIC
VOID
TpmSpiRaw (
  IN  CONST UINT8  *Tx,
  OUT UINT8        *Rx,
  IN  UINTN        Bytes
  )
{
  UINTN   Sent;
  UINT32  Word;

  SpiDrainRx ();
  SpiChipSelect (TRUE);
  for (Sent = 0; Sent < Bytes; Sent += SPI_WORD_BYTES) {
    Word = SwapBytes32 (ReadUnaligned32 ((UINT32 *)&Tx[Sent]));
    if (EFI_ERROR (SpiTransferWord (Word, &Word))) {
      Word = 0xEEEEEEEE;
    }

    WriteUnaligned32 ((UINT32 *)&Rx[Sent], SwapBytes32 (Word));
  }

  SpiChipSelect (FALSE);
}

/**
  One raw 64-byte data FIFO write transaction: a header announcing Length
  bytes, DataLength bytes of Data at DataOffset (the rest zero), and a log
  line with the byte in which the TPM reported ready and the first 16 bytes
  it returned.
**/
STATIC
VOID
TpmSpiDiagRaw (
  IN CONST CHAR8  *Label,
  IN UINT8        Length,
  IN CONST UINT8  *Data,
  IN UINTN        DataOffset,
  IN UINTN        DataLength
  )
{
  UINT8  Tx[64];
  UINT8  Rx[64];
  UINTN  Index;
  UINTN  Ready;

  ZeroMem (Tx, sizeof (Tx));
  Tx[0] = (UINT8)(Length - 1);
  Tx[1] = TPM_SPI_ADDRESS_HIGH;
  Tx[2] = (UINT8)(TPM_DATA_FIFO_0 >> 8);
  Tx[3] = (UINT8)TPM_DATA_FIFO_0;
  if (Data != NULL) {
    CopyMem (&Tx[DataOffset], Data, DataLength);
  }

  if (EFI_ERROR (TpmEnterReady ())) {
    DebugPrint (DEBUG_INFO, "SpiTpm: diag raw %a: TPM not ready\n", Label);
    return;
  }

  TpmSpiRaw (Tx, Rx, sizeof (Tx));
  Ready = 0;
  for (Index = 3; Index < sizeof (Rx); Index++) {
    if ((Rx[Index] & BIT0) != 0) {
      Ready = Index;
      break;
    }
  }

  DebugPrint (
    DEBUG_INFO,
    "SpiTpm: diag raw %a: write %2u, ready byte %2u, rx %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x, STS 0x%02x\n",
    Label,
    Length,
    Ready,
    Rx[0], Rx[1], Rx[2], Rx[3], Rx[4], Rx[5], Rx[6], Rx[7],
    Rx[8], Rx[9], Rx[10], Rx[11], Rx[12], Rx[13], Rx[14], Rx[15],
    TpmRead8 (TPM_STS_0)
    );
}

/**
  Raw transactions: headers for several lengths followed by zeros, each
  three times, then command bytes at the offsets the driver would use.
**/
STATIC
VOID
TpmSpiDiagRawSweep (
  VOID
  )
{
  STATIC CONST UINT8  Lengths[] = { 1, 11, 12, 16, 19, 64 };
  STATIC CONST UINT8  GetCap[]  = {
    0x80, 0x01, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x01, 0x7A,
    0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x01, 0x05, 0x00, 0x00, 0x00, 0x01
  };
  STATIC CONST UINT8  SelfTest[] = {
    0x80, 0x01, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x01, 0x43, 0x01
  };
  UINTN               Index;
  UINTN               Try;

  for (Index = 0; Index < ARRAY_SIZE (Lengths); Index++) {
    for (Try = 0; Try < 3; Try++) {
      TpmSpiDiagRaw ("zeros", Lengths[Index], NULL, 0, 0);
    }
  }

  TpmSpiDiagRaw ("selftest at 5", 11, SelfTest, 5, sizeof (SelfTest));
  TpmSpiDiagRaw ("getcap at 4", 19, GetCap, 4, 19);
  TpmSpiDiagRaw ("getcap at 5", 19, GetCap, 5, 19);
  TpmSpiDiagRaw ("getcap at 5", 19, GetCap, 5, 19);
  TpmSpiDiagRaw ("getcap at 9", 19, GetCap, 9, 19);
  TpmSpiDiagRaw ("getcap at 5", 19, GetCap, 5, 19);
  TpmSpiDiagRaw ("getcap 22 at 5", 22, GetCap, 5, 22);
  TpmSpiDiagRaw ("getcap 16 at 5", 16, GetCap, 5, 16);
  TpmSpiDiagRaw ("getcap 12 at 5", 12, GetCap, 5, 12);
  TpmEnterReady ();
}

/**
  Log what the TPM does with data FIFO writes of various sizes: the burst
  count, and for each size whether the write was taken, in which byte the
  TPM reported ready, and the first bytes it returned.  Each write starts a
  command the TPM expects more of (size field 128), which commandReady then
  drops.  Pass is printed with each line.
**/
STATIC
VOID
TpmSpiDiagnose (
  IN UINTN  Pass
  )
{
  STATIC CONST UINT8  Sizes[] = { 1, 4, 8, 11, 12, 13, 15, 16, 17, 19, 20, 24, 32, 48, 64 };
  UINT8               Command[64];
  UINT8               StsDword[4];
  UINT16              BurstCount;
  EFI_STATUS          Status;
  UINTN               Index;
  UINTN               Try;
  UINTN               NotReady;

  ZeroMem (Command, sizeof (Command));
  Command[0] = 0x80;
  Command[1] = 0x01;
  Command[5] = 0x80;
  Command[8] = 0x01;
  Command[9] = 0x7A;

  if (EFI_ERROR (TpmEnterReady ())) {
    DebugPrint (DEBUG_INFO, "SpiTpm: diag %u: TPM not ready\n", Pass);
    return;
  }

  BurstCount = 0;
  Status     = TpmReadBurstCount (&BurstCount);
  ZeroMem (StsDword, sizeof (StsDword));
  TpmSpiTransfer (TPM_STS_0, TRUE, StsDword, sizeof (StsDword));
  DebugPrint (
    DEBUG_INFO,
    "SpiTpm: diag %u: burst count 0x%04x (%r), STS %02x %02x %02x %02x, %u/%u wait bytes\n",
    Pass,
    BurstCount,
    Status,
    StsDword[0],
    StsDword[1],
    StsDword[2],
    StsDword[3],
    mWaitGuess[0],
    mWaitGuess[1]
    );

  for (Index = 0; Index < ARRAY_SIZE (Sizes); Index++) {
    if (EFI_ERROR (TpmEnterReady ())) {
      DebugPrint (DEBUG_INFO, "SpiTpm: diag %u: TPM not ready before a %u byte write\n", Pass, Sizes[Index]);
      return;
    }

    NotReady = 0;
    for (Try = 0; Try < 20; Try++) {
      Status = TpmSpiTransfer (TPM_DATA_FIFO_0, FALSE, Command, Sizes[Index]);
      if (Status != EFI_NOT_READY) {
        break;
      }

      NotReady++;
      MicroSecondDelay (TPM_SPI_BUSY_DELAY_US);
    }

    DebugPrint (
      DEBUG_INFO,
      "SpiTpm: diag %u: write %2u: %r after %u not ready, ready byte %u, rx %02x %02x %02x %02x %02x %02x %02x %02x, STS 0x%02x\n",
      Pass,
      Sizes[Index],
      Status,
      NotReady,
      mLastReady,
      mLastRx[0],
      mLastRx[1],
      mLastRx[2],
      mLastRx[3],
      mLastRx[4],
      mLastRx[5],
      mLastRx[6],
      mLastRx[7],
      TpmRead8 (TPM_STS_0)
      );
  }

  TpmEnterReady ();
}
#endif

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

  ResponseCode               = MAX_UINT32;
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

#ifdef TPM_SPI_DIAG
  DebugPrint (DEBUG_INFO, "SpiTpm: TPM2_Startup rc 0x%x\n", ResponseCode);
#endif

  SelfTest.Header.tag         = SwapBytes16 (TPM_ST_NO_SESSIONS);
  SelfTest.Header.paramSize   = SwapBytes32 (sizeof (SelfTest));
  SelfTest.Header.commandCode = SwapBytes32 (TPM_CC_SelfTest);
  SelfTest.FullTest           = YES;
  Status                      = TpmSimpleCommand (&SelfTest, sizeof (SelfTest), &ResponseCode);
  if (EFI_ERROR (Status) || (ResponseCode != TPM_RC_SUCCESS)) {
    DebugPrint (DEBUG_ERROR, "SpiTpm: TPM2_SelfTest: %r, rc 0x%x\n", Status, ResponseCode);
  }

#ifdef TPM_SPI_DIAG
  DebugPrint (DEBUG_INFO, "SpiTpm: TPM2_SelfTest rc 0x%x\n", ResponseCode);
  TpmSpiDiagRawSweep ();
  TpmSpiDiagnose (1);
  MicroSecondDelay (1000 * 1000);
  TpmSpiDiagnose (2);
#endif

  return EFI_SUCCESS;
}

/**
  Put the SPI0 pads in SPI mode, as the OS's pin control driver would from
  the ACPI pin group (the boot firmware leaves them in SPI mode with a
  pull-up on the clock as well).
**/
STATIC
VOID
SpiConfigurePads (
  VOID
  )
{
  UINTN  Index;
  UINTN  Address;

  if (FixedPcdGet8 (PcdDTPMSpiBus) != 0) {
    return;
  }

  for (Index = 0; Index < SPI0_PAD_COUNT; Index++) {
    Address = SPI0_PAD_BASE + Index * sizeof (UINT32);
    MmioAndThenOr32 (
      Address,
      ~(UINT32)PAD_CONFIG_MASK,
      (Index == SPI0_PAD_COUNT - 1) ? PAD_SPI_CLOCK : PAD_SPI_DATA
      );
  }
}

/**
  Reset the TPM.  It does not answer until it has seen a reset pulse after
  power-up, which the board's GPIO table (TPM_RST_L set high) does not give.
**/
STATIC
VOID
TpmReset (
  VOID
  )
{
  GpioConfig (TPM_RESET_GPIO, OUTPUT, INOUT_LOW, INTERRUPT_DISABLE, INTERRUPT_TYPE_DEFAULT);
  MicroSecondDelay (TPM_RESET_WAIT_US);
  GpioConfig (TPM_RESET_GPIO, OUTPUT, INOUT_HIGH, INTERRUPT_DISABLE, INTERRUPT_TYPE_DEFAULT);
  MicroSecondDelay (TPM_RESET_WAIT_US);
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
  UINT8           Rid;
  EFI_STATUS      Status;

  if (Probed) {
    return mTpmFound ? EFI_SUCCESS : EFI_NOT_FOUND;
  }

  Probed   = TRUE;
  mSpiBase = (FixedPcdGet8 (PcdDTPMSpiBus) == 0) ? 0x04090000 : 0x040A0000;
  mCsField = ((~(UINT32)FixedPcdGet8 (PcdDTPMSpiChipSelect)) << CDNS_SPI_CR_SS_SHIFT) & CDNS_SPI_CR_SSCTRL;

  SpiConfigurePads ();
  SpiInitController ();
  FrameBits = SpiFrameBits ();
  if ((FrameBits < 24) || (FrameBits > 40)) {
    DebugPrint (
      DEBUG_ERROR,
      "SpiTpm: SPI%u sends %u bits per FIFO entry (need 32); TPM not supported\n",
      FixedPcdGet8 (PcdDTPMSpiBus),
      FrameBits
      );
    return EFI_NOT_FOUND;
  }

  TpmReset ();
  if (EFI_ERROR (TpmSpiTransfer (TPM_DID_VID_0, TRUE, DidVid, sizeof (DidVid)))) {
    DebugPrint (DEBUG_INFO, "SpiTpm: no TPM on SPI%u (no response)\n", FixedPcdGet8 (PcdDTPMSpiBus));
    return EFI_NOT_FOUND;
  }

  Id = ReadUnaligned32 ((UINT32 *)DidVid);
  if ((Id == 0) || (Id == MAX_UINT32)) {
    DebugPrint (DEBUG_INFO, "SpiTpm: no TPM on SPI%u (ID 0x%08x)\n", FixedPcdGet8 (PcdDTPMSpiBus), Id);
    return EFI_NOT_FOUND;
  }

  //
  // Start the data FIFO's wait state guesses from what the register reads
  // saw, and from what long transfers were measured to need.
  //
  mWaitGuess[1] = mWaitGuess[0];
  mWaitGuess[2] = TPM_SPI_LONG_WAIT;
  Rid           = 0xFF;
  Status        = TpmSpiTransfer (TPM_RID_0, TRUE, &Rid, sizeof (Rid));
  DebugPrint (
    DEBUG_INFO,
    "SpiTpm: SPI%u: vendor 0x%04x device 0x%04x revision 0x%02x (%r, %u wait bytes)\n",
    FixedPcdGet8 (PcdDTPMSpiBus),
    Id & 0xFFFF,
    Id >> 16,
    Rid,
    Status,
    mWaitGuess[0]
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
