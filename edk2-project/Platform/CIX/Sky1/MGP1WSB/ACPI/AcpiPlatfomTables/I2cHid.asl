/** @file

  Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#define I2C_KEYBOARD_SUPPORT 0
#define I2C_TP_SUPPORT 1

#if I2C_KEYBOARD_SUPPORT
Device (IKBD)
{
    Name (_ADR, One)
    Name (_HID, "PNP0C50")
    Name (_CID, "PNP0C50")
    Name (_UID, One)

    Method(_DSM, 0x4, NotSerialized)
    {
        // DSM UUID for HIDI2C. Do Not change.
        If(LEqual(Arg0, ToUUID("3CDFF6F7-4267-4555-AD05-B30A3D8938DE")))
        {
            // Function 0 : Query Function
            If(LEqual(Arg2, Zero))
            {
                // Revision 1
                If(LEqual(Arg1, One))
                {
                    Return(Buffer(One) { 0x03 })
                }
                Else
                {
                    Return(Buffer(One) { 0x00 })
                }
            }
            // Function 1 : HID Function
            If(LEqual(Arg2, One))
            {
                // HID Descriptor Address (IHV Specific)
                Return(0x0001)
            }
        }
        Else
        {
            Return(Buffer(One) { 0x00 })
        }
    }

    Method (_STA, 0, NotSerialized)
    {
        Return (0x0F)
    }

    Method (_CRS, 0, Serialized)
    {
        Name (RBUF, ResourceTemplate ()
        {
            I2cSerialBus (
                0x3A,                  //SlaveAddress: bus address
                ControllerInitiated,   //SlaveMode: Default to ControllerInitiated
                100000,                //ConnectionSpeed: in Hz
                AddressingMode7Bit,    //Addressing Mode: default to 7 bit
                "\\_SB.I2C5",          //ResourceSource: I2C bus controller name
                ,                      //ResourceSourceIndex: defaults to 0
                ,                      //ResourceUsage: Defaults to ResourceConsumer
                ,                      //Descriptor Name: creates name for offset of resource descriptor
            )
            GpioIO(Shared, PullUp, 0, 0, IoRestrictionNone, "\\_SB.GPI4", 0, ResourceConsumer, , ) { 0 }
            GpioInt(Level, ActiveLow, Shared, PullUp, 0, "\\_SB.GPI4",) { 0 }
        })
        Return (RBUF)
    }
}  // Device (IKBD)
#endif

#if I2C_TP_SUPPORT
  //GT7986P(Touch controller chip)
  Device (GT7P) {
    Name (_HID, "PNP0C50")
    Name (_CID, "PNP0C50")
    Name (_UID, 0x00)
    Name (_STA, 0x0F)
    Name (_CCA, 0)
    Name (_CRS, ResourceTemplate () {
      I2cSerialBusV2 (0x5D,
                      ControllerInitiated,
                      400000,
                      AddressingMode7Bit,
                      "\\_SB.I2C1",
                      0x0,
                      ResourceConsumer,
                      ,
                      Exclusive
                      ,)
      GpioInt(Edge, ActiveBoth, SharedAndWake, PullNone, , "\\_SB.GPI2") { 19 }
    })

    Method(_DSM, 0x4, NotSerialized)
    {
        // DSM UUID for HIDI2C. Do Not change.
        If(LEqual(Arg0, ToUUID("3CDFF6F7-4267-4555-AD05-B30A3D8938DE")))
        {
            // Function 0 : Query Function
            If(LEqual(Arg2, Zero))
            {
                // Revision 1
                If(LEqual(Arg1, One))
                {
                    Return(Buffer(One) { 0x03 })
                }
                Else
                {
                    Return(Buffer(One) { 0x00 })
                }
            }
            // Function 1 : HID Function
            If(LEqual(Arg2, One))
            {
                // HID Descriptor Address (IHV Specific)
                Return(0x0001)
            }
        }
        Else
        {
            Return(Buffer(One) { 0x00 })
        }
    }
  }
#endif
