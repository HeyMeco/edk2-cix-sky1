/** @file

  Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

External (\_SB.GPI4, DeviceObj)

Scope (\_SB.GPI4)
{
  //
  // GPIO4 pin 6 is the EC alert line.  The Orion O6 has no embedded
  // controller behind \_SB.EC0, so the event is not declared.
  //
  Name (_AEI, ResourceTemplate() {
    GpioInt(Level, ActiveLow, ExclusiveAndWake, PullUp, , "\\_SB.GPI4") { 10 }
  })

  Method (_L0A) {
    \_SB.ERTC.CAFG()
  }
}
