/** @file
  The discrete SPI TPM as a CRB TPM with the ACPI start method.

  The OS (Windows tpm.sys, Linux tpm_crb) writes a command into the CRB data
  buffer, sets CTRL_START and calls _DSM function 1.  TSTA sends the command
  to the TPM with the PTP SPI protocol and the FIFO interface, by driving the
  Cadence SPI controller registers directly (PcdDTPMSpiBus and
  PcdDTPMSpiChipSelect, mode 0, 6.25 MHz), copies the response into the data buffer and clears CTRL_START.
  The same transfer, in C, is in Tpm2DeviceLibCdnsSpi; its file comment
  explains how wait states are handled with the controller's 32-bit FIFO.

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
      //
      // SPI controller base, patched by SpiTpmAcpiDxe.
      //
      Name (SPIB, SPI_TPM_SPI_PLACEHOLDER)
      //
      // Configuration register slave select field with the TPM's chip
      // select asserted, patched by SpiTpmAcpiDxe.
      //
      Name (SPCS, SPI_TPM_CS_PLACEHOLDER)

      Method (_CRS, 0, Serialized) {
        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0, SPI_TPM_CRB_SIZE, CRBM)
        })
        CreateDWordField (RBUF, CRBM._BAS, BASE)
        BASE = CRBA
#ifdef SPI_TPM_TRACE_UART
        TRCS ("SpiTpm AML: _CRS\r\n")
#endif
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
      // Command header (tag, size, command code).
      //
      Field (CRBR, ByteAcc, NoLock, Preserve) {
        Offset (0x80),
        CHDR, 80
      }

      //
      // Cadence SPI controller registers.
      //
      OperationRegion (SPIR, SystemMemory, SPIB, 0x24)
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
      // Wait state bytes last seen: for the registers, for data FIFO
      // transfers of up to 12 bytes, and for longer data FIFO reads.  The
      // NPCT750 answers a transfer of up to 16 bytes with the header after
      // one wait state byte, a longer one only after seven or eight, so
      // writes (which must guess where their data goes) stay within 12
      // bytes and long reads start from the longer guess.
      //
      Name (WGR, 0)
      Name (WGF, 0)
      Name (WGL, 7)
      Name (WSED, 0)          // WGF started from WGR

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
      // SPI: shift one FIFO entry (four bytes, most significant first) out
      // and one in; Ones on timeout.
      //
      Method (SXFR, 1, Serialized) {
        Local0 = 0
        While ((SISR & 0x08) != 0) {
          Local0++
          If (Local0 > 1000) {
            Return (Ones)
          }
          Stall (1)
        }

        STXD = Arg0
        Local0 = 0
        While ((SISR & 0x10) == 0) {
          Local0++
          If (Local0 > 1000) {
            Return (Ones)
          }
          Stall (1)
        }

        Return (SRXD)
      }

      //
      // One TPM SPI transaction on locality 0.
      // Arg0: 1 = read, 0 = write; Arg1: register; Arg2: length (1-64);
      // Arg3: offset of the data in TBUF.  Returns 0; 1 on error; 2 when a
      // write met a different wait state length than guessed, so the TPM
      // took shifted data (the guess is updated for the next try); 3 when
      // the TPM was still waiting where a write's data should start, or
      // for longer than a read can follow (it took nothing; repeat it).
      //
      Method (TPMX, 4, Serialized) {
        Name (TXS, Buffer (124) {})
        Name (RXS, Buffer (124) {})

        If (Arg1 != 0x24) {
          Local7 = WGR
        } ElseIf (Arg2 <= 12) {
          Local7 = WGF
        } Else {
          Local7 = WGL
        }

        TXS [0] = (Arg0 << 7) | (Arg2 - 1)
        TXS [1] = 0xD4
        TXS [2] = (Arg1 >> 8) & 0xFF
        TXS [3] = Arg1 & 0xFF
        If (Arg0 == 0) {
          Local0 = 0
          While (Local0 < Arg2) {
            TXS [4 + Local7 + Local0] = DerefOf (TBUF [Arg3 + Local0])
            Local0++
          }
        }

        //
        // Local1: bytes to clock; Local2: bytes clocked; Local3: index of
        // the byte in which the TPM returned 1 in bit 0 (the last header
        // byte or the last wait state byte), 0 = not seen yet.
        //
        Local1 = (4 + Local7 + Arg2 + 3) & 0xFFFFFFFC
        Local2 = 0
        Local3 = 0
        Local4 = 0
        SDRN ()
        SCR = (SCR & 0xFFFFC3FF) | SPCS
        While (Local2 < Local1) {
          Local5 = (DerefOf (TXS [Local2]) << 24) | (DerefOf (TXS [Local2 + 1]) << 16) |
                   (DerefOf (TXS [Local2 + 2]) << 8) | DerefOf (TXS [Local2 + 3])
          Local5 = SXFR (Local5)
          If (Local5 == Ones) {
            Local4 = 1
            Break
          }

          RXS [Local2] = (Local5 >> 24) & 0xFF
          RXS [Local2 + 1] = (Local5 >> 16) & 0xFF
          RXS [Local2 + 2] = (Local5 >> 8) & 0xFF
          RXS [Local2 + 3] = Local5 & 0xFF
          If (Local3 == 0) {
            Local6 = Local2
            If (Local6 < 3) {
              Local6 = 3
            }
            While (Local6 < (Local2 + 4)) {
              If (DerefOf (RXS [Local6]) & 0x01) {
                Local3 = Local6
                Break
              }
              Local6++
            }
          }
          Local2 += 4

          //
          // A write stops as soon as the guess can be checked.
          //
          If (Arg0 == 0) {
            If ((Local3 != 0) && (Local3 != (3 + Local7))) {
              Local4 = 2
              Break
            }
            //
            // Not ready where the data starts: the TPM took nothing.
            // Usually it is busy and a retry with the same guess goes
            // through.  But if the expected ready byte ended this entry, a
            // guess one short is never seen as shifted (the ready byte
            // would be in the next entry), so try one more wait state
            // byte; an overshoot is caught as shifted.
            //
            If ((Local3 == 0) && (Local2 > (3 + Local7))) {
              If ((Local2 == (Local7 + 4)) && (Local7 < 52)) {
                Local7++
                If (Arg1 != 0x24) {
                  WGR = Local7
                } ElseIf (Arg2 <= 12) {
                  WGF = Local7
                } Else {
                  WGL = Local7
                }
              }
              Local4 = 3
              Break
            }
          }

          //
          // A read goes on until the data after the ready byte is in.
          //
          If (Arg0) {
            If (Local3) {
              Local1 = (Local3 + Arg2 + 4) & 0xFFFFFFFC
            } ElseIf (Local2 < 56) {
              Local1 = Local2 + 4
            }
          }
        }
        SCR |= 0x3C00

        If ((Local4 == 0) && (Local3 == 0)) {
          Local4 = 3
        }

        If ((Local4 == 0) || (Local4 == 2)) {
          If ((Local4 == 0) && Arg0) {
            Local0 = 0
            While (Local0 < Arg2) {
              TBUF [Arg3 + Local0] = DerefOf (RXS [Local3 + 1 + Local0])
              Local0++
            }
          }
          Local7 = Local3 - 3
          If (Local7 > 52) {
            Local7 = 52
          }

          If (Arg1 != 0x24) {
            WGR = Local7
          } ElseIf (Arg2 <= 12) {
            WGF = Local7
          } Else {
            WGL = Local7
          }
        }

        Return (Local4)
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

      //
      // A write is repeated while the TPM is busy or if a wait state
      // shifted the data; writing 0 or a shifted copy of the value to the
      // access or status register does nothing.
      //
      Method (TWR1, 2, Serialized) {
        Local0 = 0
        While (One) {
          TBUF [0xF80] = Arg1
          Local1 = TPMX (0, Arg0, 1, 0xF80)
          If ((Local1 < 2) || (Local0 >= 100)) {
            Return (Local1)
          }
          Stall (10)
          Local0++
        }
        Return (One)
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
              TLST = Local0
              Return (One)
            }
            Sleep (1)
          }
        }
        Return (One)
      }

      //
      // Put the TPM in the Ready state.  commandReady moves it from Idle to
      // Ready, but from Reception or Completion only to Idle (dropping the
      // command or response), so keep writing it until the TPM reports
      // Ready, for up to 2 s.  Returns 0, or 1 on timeout.
      //
      Method (TRDY, 0, Serialized) {
        Local1 = 0
        While (Local1 < 2000) {
          TWR1 (0x18, 0x40)
          Local0 = TRD1 (0x18)
          If ((Local0 != 0xFF) && ((Local0 & 0x40) != 0)) {
            Return (Zero)
          }
          Sleep (1)
          Local1++
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
      // (Arg0: 1 = read, 0 = write), where possible in transfers that end
      // on a FIFO entry.  Returns as TPMX.
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

          If (Arg0) {
            If (Local2 > 64) {
              Local2 = 64
            }
          } ElseIf (Local2 > 12) {
            Local2 = 12
          }
          If (Local2 > (Local1 - Local0)) {
            Local2 = Local1 - Local0
          }
          If (Local2 > 4) {
            If (Local2 > 12) {
              Local2 -= (WGL + Local2) & 3
            } Else {
              Local2 -= (WGF + Local2) & 3
            }
          }
          //
          // A transfer the TPM was too busy to take is repeated, for up
          // to about 100 ms.
          //
          Local5 = 0
          Local4 = TPMX (Arg0, 0x24, Local2, Local0)
          While ((Local4 == 3) && (Local5 < 2000)) {
            Stall (50)
            Local5++
            Local4 = TPMX (Arg0, 0x24, Local2, Local0)
          }
          If (Local4) {
            Return (Local4)
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
      // Step of TRUN that failed, its result and the last status read, for
      // the trace.
      //
      Name (TSTP, 0)
      Name (TRES, 0)
      Name (TLST, 0)

      //
      // Run the command in the CRB data buffer.  Returns 0, or 1 on error
      // (TSTP, TRES and TLST say where).
      //
      Method (TRUN, 0, Serialized) {
        SINI ()

        //
        // Locality 0 (requested by the firmware; request it again if the
        // TPM was reset).
        //
        TSTP = 1
        TLST = TRD1 (0x00)
        If ((TLST & 0xA0) != 0xA0) {
          TWR1 (0x00, 0x02)
          If (TWAT (0x00, 0xA0, 0x00, 750)) {
            Return (One)
          }
        }

        //
        // Start the data FIFO's wait state guess from what the register
        // reads saw.
        //
        If (WSED == 0) {
          WGF = WGR
          WSED = 1
        }

        //
        // Copy the command (only its own length: storing a buffer into a
        // buffer object is not done alike by every AML interpreter).
        //
        TSTP = 2
        Local3 = CBUF
        Local0 = (DerefOf (Local3 [2]) << 24) | (DerefOf (Local3 [3]) << 16) |
                 (DerefOf (Local3 [4]) << 8) | DerefOf (Local3 [5])
        TRES = Local0
        If ((Local0 < 10) || (Local0 > 0xF80)) {
          Return (One)
        }
        Local1 = 0
        While (Local1 < Local0) {
          TBUF [Local1] = DerefOf (Local3 [Local1])
          Local1++
        }

        //
        // Going back to Ready drops what the TPM has received, so a
        // command whose FIFO write went wrong can be sent again.
        //
        Local1 = 0
        While (One) {
          TSTP = 3
          If (TRDY ()) {
            Return (One)
          }
          TSTP = 4
          Local2 = TFIF (0, 0, Local0)
          TRES = Local2
          If (Local2 == 0) {
            Break
          }
          If ((Local2 != 2) || (Local1 >= 3)) {
            Return (One)
          }
          Local1++
        }
        TSTP = 5
        If (TWAT (0x18, 0x80, 0x08, 750)) {
          Return (One)
        }

        //
        // Wait for the response for up to 25 s: ACPICA (Linux) aborts a
        // While loop that runs for 30 s.
        //
        TWR1 (0x18, 0x20)
        TSTP = 6
        If (TWAT (0x18, 0x90, 0x00, 25000)) {
          Return (One)
        }

        TSTP = 7
        TRES = TFIF (1, 0, 10)
        If (TRES) {
          Return (One)
        }
        TSTP = 8
        Local0 = TSIZ ()
        TRES = Local0
        If ((Local0 < 10) || (Local0 > 0xF80)) {
          Return (One)
        }
        TSTP = 9
        TRES = TFIF (1, 10, Local0 - 10)
        If (TRES) {
          Return (One)
        }

        CBUF = Mid (TBUF, 0, SPI_TPM_CRB_DATA_SIZE)
        TSTP = 0
        Return (Zero)
      }

#ifdef SPI_TPM_TRACE_UART
      //
      // Trace on the debug UART (PL011): data register and flags.
      //
      OperationRegion (UARR, SystemMemory, SPI_TPM_TRACE_UART, 0x1C)
      Field (UARR, DWordAcc, NoLock, Preserve) {
        UDR,  32,
        Offset (0x18),
        UFR,  32
      }

      Method (TRCC, 1, Serialized) {
        Local0 = 0
        While (((UFR & 0x20) != 0) && (Local0 < 1000)) {
          Stall (10)
          Local0++
        }
        UDR = Arg0
      }

      Method (TRCS, 1, Serialized) {
        Local0 = ToBuffer (Arg0)
        Local1 = 0
        While (Local1 < SizeOf (Local0)) {
          Local2 = DerefOf (Local0 [Local1])
          If (Local2 == 0) {
            Break
          }
          TRCC (Local2)
          Local1++
        }
      }

      //
      // Arg0 in hex, Arg1 digits.
      //
      Method (TRCH, 2, Serialized) {
        Name (HEXD, Buffer () { "0123456789ABCDEF" })
        Local0 = Arg1
        While (Local0 > 0) {
          Local0--
          TRCC (DerefOf (HEXD [(Arg0 >> (Local0 * 4)) & 0xF]))
        }
      }
#endif

      Method (TSTA, 0, Serialized) {
        Acquire (TMTX, 0xFFFF)
        Local2 = CHDR
        Local1 = (DerefOf (Local2 [6]) << 24) | (DerefOf (Local2 [7]) << 16) |
                 (DerefOf (Local2 [8]) << 8) | DerefOf (Local2 [9])
        Local0 = TRUN ()
        TWR1 (0x18, 0x40)
        If (Local0) {
          CSTS = 1            // tpmSts: fatal error
        } Else {
          CSTS = 0
        }
#ifdef SPI_TPM_TRACE_UART
        TRCS ("SpiTpm AML: command 0x")
        TRCH (Local1, 8)
        If (Local0) {
          TRCS (" failed at step ")
          TRCH (TSTP, 1)
          TRCS (" (0x")
          TRCH (TRES, 8)
          TRCS (", status 0x")
          TRCH (TLST, 2)
          TRCS (", now 0x")
          TRCH (TRD1 (0x18), 2)
          TRCS (", wait bytes ")
          TRCH (WGR, 2)
          TRCS ("/")
          TRCH (WGF, 2)
          TRCS ("/")
          TRCH (WGL, 2)
          TRCS (")\r\n")
        } Else {
          TRCS (" -> 0x")
          TRCH ((DerefOf (TBUF [6]) << 24) | (DerefOf (TBUF [7]) << 16) |
                (DerefOf (TBUF [8]) << 8) | DerefOf (TBUF [9]), 8)
          TRCS ("\r\n")
        }
#endif
        CSTR = 0
        Release (TMTX)
      }

      Method (_DSM, 4, Serialized) {
#ifdef SPI_TPM_TRACE_UART
        If ((Arg0 != ToUUID ("6bbf6cab-5463-4714-b7cd-f0203c0368d4")) || (ToInteger (Arg2) != 1)) {
          TRCS ("SpiTpm AML: _DSM ")
          TRCH ((DerefOf (Arg0 [3]) << 24) | (DerefOf (Arg0 [2]) << 16) |
                (DerefOf (Arg0 [1]) << 8) | DerefOf (Arg0 [0]), 8)
          TRCS (" function ")
          TRCH (ToInteger (Arg2), 2)
          TRCS ("\r\n")
        }
#endif

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

        //
        // TCG Platform Reset Attack Mitigation: Windows on Arm asks for
        // memory to be cleared through the UEFI variable on every boot, so
        // the request is accepted without changing anything.
        //
        If (Arg0 == ToUUID ("376054ed-cc13-4675-901c-4756d7f2d45d")) {
          Switch (ToInteger (Arg2)) {
            Case (0) {
              Return (Buffer () { 0x03 })
            }
            Case (1) {
              Return (Zero)
            }
          }
        }

        Return (Buffer () { 0x00 })
      }
    }
  }
}
