/** @file

  Copyright 2025 Cix Technology Group Co., Ltd. All Rights Reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

External (\_SB.SPI0, DeviceObj)
External (\_SB.SPI1, DeviceObj)

Scope (\_SB.SPI0)
{
    Device(TP0) {
        Name (_HID, "SPT0001")
        Name (_UID, 0x0)
        Method(_CRS, 0x0, NotSerialized)
        {
          Name (RBUF, ResourceTemplate ()
          {
              SpiSerialBus (0x0000,
                            PolarityLow,
                            FourWireMode,
                            0x08,
                            ControllerInitiated,
                            0x0007A120,
                            ClockPolarityLow,
                            ClockPhaseFirst,
                            "\\_SB.SPI0",
                            0x00,
                            ResourceConsumer, , )
          })
          Return(RBUF)
      }
    }
}

Scope (\_SB.SPI1)
{
    Device(TP1) {
        Name (_HID, "SPT0002")
        Name (_UID, 0x0)
        Method(_CRS, 0x0, NotSerialized)
        {
          Name (RBUF, ResourceTemplate ()
          {
              SpiSerialBus (0x0000,
                            PolarityLow,
                            FourWireMode,
                            0x08,
                            ControllerInitiated,
                            0x0007A120,
                            ClockPolarityLow,
                            ClockPhaseFirst,
                            "\\_SB.SPI1",
                            0x00,
                            ResourceConsumer, , )
          })
          Return(RBUF)
      }
    }
}
