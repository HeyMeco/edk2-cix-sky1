/** @file

  Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

External (\_SB.GPI5, DeviceObj)

Device(PWRB)
{
  Name(_HID,EISAID("PNP0C0C"))
  Method(_STA)
  {
    Return(0x0F)
  }
}


Scope (\_SB.GPI5)
{
  Name (_AEI, ResourceTemplate() {
    GpioInt (Edge, ActiveHigh, Exclusive, PullUp, 0, "\\_SB.GPI5") {1} //GPIO016
  })

  Method (_E01) {
    Notify(\_SB.PWRB, 0x80)
  }

}