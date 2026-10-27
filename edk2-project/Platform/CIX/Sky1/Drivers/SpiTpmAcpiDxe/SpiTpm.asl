/** @file
  The discrete SPI TPM as a CRB TPM with the ACPI start method.

  The OS (Windows tpm.sys, Linux tpm_crb) writes a command into the CRB data
  buffer, sets CTRL_START and calls _DSM function 1.  TSTA sends the command
  to the TPM with the PTP SPI protocol and the FIFO interface, by driving the
  Cadence SPI controller registers directly (SPI0, chip select 0, mode 0,
  6.25 MHz), copies the response into the data buffer and clears CTRL_START.
  The same transfer, in C, is in Tpm2DeviceLibCdnsSpi.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "SpiTpmAcpi.h"

DefinitionBlock ("SpiTpm.aml", "SSDT", 2, "CIXTEK", "SPITPM", 1) {
  Scope (\_SB) {
    Device (TPM0) {
      Name (_HID, "MSFT0101")
      Name (_CID, "MSFT0101")
      Name (_UID, 0)
      Name (_STA, 0xF)
      //
      // CRB area address, patched by SpiTpmAcpiDxe.
      //
      Name (CRBA, SPI_TPM_CRB_PLACEHOLDER)

      Method (_CRS, 0, Serialized) {
        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0, SPI_TPM_CRB_SIZE, CRBM)
        })
        CreateDWordField (RBUF, CRBM._BAS, BASE)
        BASE = CRBA
        Return (RBUF)
      }

      //
      // CRB control area (CTRL_STS, CTRL_START) and data buffer.
      //
      OperationRegion (CRBR, SystemMemory, CRBA, SPI_TPM_CRB_SIZE)
      Field (CRBR, DWordAcc, NoLock, Preserve) {
        Offset (0x44),
        CSTS, 32,
        Offset (0x4C),
        CSTR, 32,
        Offset (0x80),
        CBUF, 31744           // SPI_TPM_CRB_DATA_SIZE bytes
      }

      //
      // Cadence SPI controller registers.
      //
      OperationRegion (SPIR, SystemMemory, SPI_TPM_SPI_BASE, 0x24)
      Field (SPIR, DWordAcc, NoLock, Preserve) {
        SCR,  32,             // 0x00 configuration
        SISR, 32,             // 0x04 interrupt status
        Offset (0x0C),
        SIDR, 32,             // 0x0C interrupt disable
        Offset (0x14),
        SER,  32,             // 0x14 enable
        Offset (0x1C),
        STXD, 32,             // 0x1C transmit data
        SRXD, 32              // 0x20 receive data
      }

      //
      // Working copy of the command/response, plus 8 bytes of register
      // scratch at 0xF80.
      //
      Name (TBUF, Buffer (0xF88) {})
      Mutex (TMTX, 0)

      //
      // SPI: drain the receive FIFO.
      //
      Method (SDRN, 0, Serialized) {
        Local0 = 0
        While (((SISR & 0x10) != 0) && (Local0 < 256)) {
          Local1 = SRXD
          Local0++
        }
      }

      //
      // SPI: master, mode 0, SCLK = 200 MHz / 32, chip select under
      // software control and deasserted.
      //
      Method (SINI, 0, Serialized) {
        SER = 0
        SIDR = 0x7F
        SDRN ()
        SISR = 0x7F
        SCR = 0x7C21
        SER = 1
      }

      //
      // SPI: shift one byte out and one in; 0x100 on timeout.
      //
      Method (SXFR, 1, Serialized) {
        Local0 = 0
        While ((SISR & 0x08) != 0) {
          Local0++
          If (Local0 > 1000) {
            Return (0x100)
          }
          Stall (1)
        }

        STXD = Arg0
        Local0 = 0
        While ((SISR & 0x10) == 0) {
          Local0++
          If (Local0 > 1000) {
            Return (0x100)
          }
          Stall (1)
        }

        Return (SRXD & 0xFF)
      }

      //
      // One TPM SPI transaction on locality 0.
      // Arg0: 1 = read, 0 = write; Arg1: register; Arg2: length (1-64);
      // Arg3: offset of the data in TBUF.  Returns 0, or 1 on error.
      //
      Method (TPMX, 4, Serialized) {
        SDRN ()
        SCR = (SCR & 0xFFFFC3FF) | 0x3800
        Local2 = SXFR ((Arg0 << 7) | (Arg2 - 1))
        Local2 |= SXFR (0xD4)
        Local2 |= SXFR ((Arg1 >> 8) & 0xFF)
        Local0 = SXFR (Arg1 & 0xFF)
        Local2 |= Local0

        //
        // Wait states: the TPM returns 0 in bit 0 until it is ready.
        //
        Local1 = 0
        While (((Local2 & 0x100) == 0) && ((Local0 & 0x01) == 0)) {
          Local1++
          If (Local1 > 50) {
            Local2 = 0x100
            Break
          }
          Local0 = SXFR (0)
          Local2 |= Local0
        }

        Local1 = 0
        While (((Local2 & 0x100) == 0) && (Local1 < Arg2)) {
          If (Arg0) {
            Local0 = SXFR (0)
            TBUF [Arg3 + Local1] = Local0 & 0xFF
          } Else {
            Local0 = SXFR (DerefOf (TBUF [Arg3 + Local1]))
          }
          Local2 |= Local0
          Local1++
        }

        SCR |= 0x3C00
        If (Local2 & 0x100) {
          Return (One)
        }
        Return (Zero)
      }

      //
      // Read / write a TPM byte register; reads return 0xFF on error.
      //
      Method (TRD1, 1, Serialized) {
        If (TPMX (1, Arg0, 1, 0xF80)) {
          Return (0xFF)
        }
        Return (DerefOf (TBUF [0xF80]))
      }

      Method (TWR1, 2, Serialized) {
        TBUF [0xF80] = Arg1
        Return (TPMX (0, Arg0, 1, 0xF80))
      }

      //
      // Wait until register Arg0 has bits Arg1 set and Arg2 clear, for up
      // to Arg3 ms.  Polls quickly for 2 ms, then every millisecond.
      // Returns 0, or 1 on timeout.
      //
      Method (TWAT, 4, Serialized) {
        Local1 = 0
        Local2 = 0
        While (One) {
          Local0 = TRD1 (Arg0)
          If ((Local0 != 0xFF) && ((Local0 & Arg1) == Arg1) && ((Local0 & Arg2) == 0)) {
            Return (Zero)
          }
          If (Local1 < 40) {
            Local1++
            Stall (50)
          } Else {
            Local2++
            If (Local2 > Arg3) {
              Return (One)
            }
            Sleep (1)
          }
        }
        Return (One)
      }

      //
      // Burst count, or 0 on error.
      //
      Method (TBRS, 0, Serialized) {
        If (TPMX (1, 0x19, 2, 0xF80)) {
          Return (Zero)
        }
        Return (DerefOf (TBUF [0xF80]) | (DerefOf (TBUF [0xF81]) << 8))
      }

      //
      // Move Arg2 bytes at TBUF offset Arg1 through the data FIFO
      // (Arg0: 1 = read, 0 = write).  Returns 0, or 1 on error.
      //
      Method (TFIF, 3, Serialized) {
        Local0 = Arg1
        Local1 = Arg1 + Arg2
        While (Local0 < Local1) {
          Local3 = 0
          Local2 = TBRS ()
          While (Local2 == 0) {
            Local3++
            If (Local3 > 750) {
              Return (One)
            }
            Sleep (1)
            Local2 = TBRS ()
          }

          If (Local2 > 64) {
            Local2 = 64
          }
          If (Local2 > (Local1 - Local0)) {
            Local2 = Local1 - Local0
          }
          If (TPMX (Arg0, 0x24, Local2, Local0)) {
            Return (One)
          }
          Local0 += Local2
        }
        Return (Zero)
      }

      //
      // Big-endian size field of the command/response header in TBUF.
      //
      Method (TSIZ, 0, Serialized) {
        Return ((DerefOf (TBUF [2]) << 24) | (DerefOf (TBUF [3]) << 16) |
                (DerefOf (TBUF [4]) << 8) | DerefOf (TBUF [5]))
      }

      //
      // Run the command in the CRB data buffer.  Returns 0, or 1 on error.
      //
      Method (TRUN, 0, Serialized) {
        SINI ()

        //
        // Locality 0 (requested by the firmware; request it again if the
        // TPM was reset).
        //
        If ((TRD1 (0x00) & 0xA0) != 0xA0) {
          TWR1 (0x00, 0x02)
          If (TWAT (0x00, 0xA0, 0x00, 750)) {
            Return (One)
          }
        }

        TWR1 (0x18, 0x40)
        If (TWAT (0x18, 0x40, 0x00, 2000)) {
          Return (One)
        }

        TBUF = CBUF
        Local0 = TSIZ ()
        If ((Local0 < 10) || (Local0 > 0xF80)) {
          Return (One)
        }
        If (TFIF (0, 0, Local0)) {
          Return (One)
        }
        If (TWAT (0x18, 0x80, 0x08, 750)) {
          Return (One)
        }

        TWR1 (0x18, 0x20)
        If (TWAT (0x18, 0x90, 0x00, 90000)) {
          Return (One)
        }

        If (TFIF (1, 0, 10)) {
          Return (One)
        }
        Local0 = TSIZ ()
        If ((Local0 < 10) || (Local0 > 0xF80)) {
          Return (One)
        }
        If (TFIF (1, 10, Local0 - 10)) {
          Return (One)
        }

        CBUF = TBUF
        Return (Zero)
      }

      Method (TSTA, 0, Serialized) {
        Acquire (TMTX, 0xFFFF)
        Local0 = TRUN ()
        TWR1 (0x18, 0x40)
        If (Local0) {
          CSTS = 1            // tpmSts: fatal error
        } Else {
          CSTS = 0
        }
        CSTR = 0
        Release (TMTX)
      }

      Method (_DSM, 4, Serialized) {
        //
        // TCG ACPI start method: function 0 query, function 1 start.
        //
        If (Arg0 == ToUUID ("6bbf6cab-5463-4714-b7cd-f0203c0368d4")) {
          Switch (ToInteger (Arg2)) {
            Case (0) {
              Return (Buffer () { 0x03 })
            }
            Case (1) {
              TSTA ()
              Return (Zero)
            }
          }
        }

        //
        // TCG Physical Presence Interface 1.3, without operations: the
        // firmware has no physical presence handling, so every request is
        // reported as not implemented and nothing is pending.
        //
        If (Arg0 == ToUUID ("3dddfaa6-361b-4eb4-a424-8d10089d1653")) {
          Switch (ToInteger (Arg2)) {
            Case (0) {
              Return (Buffer () { 0xFF, 0x01 })     // functions 0-8
            }
            Case (1) {
              Return ("1.3")                        // interface version
            }
            Case (2) {
              Return (One)                          // submit: not implemented
            }
            Case (3) {
              Return (Package () { Zero, Zero })    // no pending operation
            }
            Case (4) {
              Return (2)                            // transition: reboot
            }
            Case (5) {
              Return (Package () { Zero, Zero, Zero })
            }
            Case (6) {
              Return (3)                            // language: not implemented
            }
            Case (7) {
              Return (One)                          // submit 2: not implemented
            }
            Case (8) {
              Return (Zero)                         // operation not implemented
            }
          }
        }

        Return (Buffer () { 0x00 })
      }
    }
  }
}
