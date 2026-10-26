/** @file

  Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

Device (WAK0) {
  Name (_HID, "PRP0001")
  Name (_UID, 0x21)
  Name (_STA, 0x0B)

  Name (_CRS, ResourceTemplate () {
    PinGroupFunction(Exclusive, 0x0, "\\_SB.MUX1", 0, "gbe1_wake_gpio", ResourceConsumer,)
    GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionInputOnly, "\\_SB.GPI5", 0, ResourceConsumer) { 2 }
  })

  Name (_DSD, Package () {
    ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package () { "compatible", "cix,wake-gpio" },
      Package () { "wake-gpios", Package () { ^WAK0, 0, 0, 0 } }
    }
  })
}

Device (WAK1) {
  Name (_HID, "PRP0001")
  Name (_UID, 0x22)
  Name (_STA, 0x0B)

  Name (_CRS, ResourceTemplate () {
    PinGroupFunction(Exclusive, 0x0, "\\_SB.MUX1", 0, "gbe2_wake_gpio", ResourceConsumer,)
    GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionInputOnly, "\\_SB.GPI5", 0, ResourceConsumer) { 0 }
  })

  Name (_DSD, Package () {
    ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package () { "compatible", "cix,wake-gpio" },
      Package () { "wake-gpios", Package () { ^WAK1, 0, 0, 0 } }
    }
  })
}
