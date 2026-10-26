/** @file

  Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#define CLK_TREE_CAMERA_MCLK0     72
#define CLK_TREE_CAMERA_MCLK1     73
#define CLK_TREE_CAMERA_MCLK2     74
#define CLK_TREE_CAMERA_MCLK3     75

External (\_SB.I2C4, DeviceObj)
External (\_SB.I2C3, DeviceObj)
External (\_SB.I2C0, DeviceObj)
External (\_SB.I2C1, DeviceObj)
External (\_SB.ISP0, DeviceObj)

//Camera power enable 0
Device (CPE0) {
  Name (_HID, "PRP0001")
  Name (_UID, 0x03)
  Name (_STA, 0x0B)

  Name (_CRS, ResourceTemplate () {
    GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
    "\\_SB.GPI1", 0, ResourceConsumer) { 16 }
  })

  Name (_DSD, Package () {
    ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package () { "compatible", "regulator-fixed" },
      Package () { "regulator-name", "cam_power_en_0" },
      Package () { "regulator-min-microvolt", 3300000 },
      Package () { "regulator-max-microvolt", 3300000 },
      Package () { "gpio", Package () { ^CPE0, 0, 0, 0 } },
    }
  })
}

//Camera power enable 1
Device (CPE1) {
  Name (_HID, "PRP0001")
  Name (_UID, 0x04)
  Name (_STA, 0x0B)

  Name (_CRS, ResourceTemplate () {
    GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
    "\\_SB.GPI1", 0, ResourceConsumer) { 18 }
  })

  Name (_DSD, Package () {
    ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package () { "compatible", "regulator-fixed" },
      Package () { "regulator-name", "cam_power_en_1" },
      Package () { "regulator-min-microvolt", 3300000 },
      Package () { "regulator-max-microvolt", 3300000 },
      Package () { "gpio", Package () { ^CPE1, 0, 0, 0 } },
    }
  })
}

//Camera power enable 2
Device (CPE2) {
  Name (_HID, "PRP0001")
  Name (_UID, 0x05)
  Name (_STA, 0x0B)

  Name (_CRS, ResourceTemplate () {
    GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
    "\\_SB.GPI1", 0, ResourceConsumer) { 8 }
  })

  Name (_DSD, Package () {
    ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package () { "compatible", "regulator-fixed" },
      Package () { "regulator-name", "cam_power_en_2" },
      Package () { "regulator-min-microvolt", 3300000 },
      Package () { "regulator-max-microvolt", 3300000 },
      Package () { "gpio", Package () { ^CPE2, 0, 0, 0 } },
    }
  })
}

//Camera power enable 3
Device (CPE3) {
  Name (_HID, "PRP0001")
  Name (_UID, 0x06)
  Name (_STA, 0x0B)

  Name (_CRS, ResourceTemplate () {
    GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
    "\\_SB.GPI1", 0, ResourceConsumer) { 11 }
  })

  Name (_DSD, Package () {
    ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package () { "compatible", "regulator-fixed" },
      Package () { "regulator-name", "cam_power_en_3" },
      Package () { "regulator-min-microvolt", 3300000 },
      Package () { "regulator-max-microvolt", 3300000 },
      Package () { "gpio", Package () { ^CPE3, 0, 0, 0 } },
    }
  })
}

Scope (\_SB.I2C0)
{
  //ISP imgsensor00
  Device (IIS0) {
    Name (_HID, "CIXH3024")
    Name (_UID, 0x00)
    Name (_STA, 0x0F)
    Name (_CCA, 0)
    Name (_CRS, ResourceTemplate () {
      I2cSerialBusV2 (0x34,
                      ControllerInitiated,
                      400000,
                      AddressingMode7Bit,
                      "\\_SB.I2C0",
                      0x0,
                      ResourceConsumer,
                      ,
                      Exclusive
                      ,)
      PinGroupFunction(Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_cam0_hw", ResourceConsumer, , RawDataBuffer () { 0x0 })
      PinGroupFunction(Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_cam0_hw", ResourceConsumer, , RawDataBuffer () { 0x1 })
      GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
      "\\_SB.GPI1", 0, ResourceConsumer) { 12, 15 }
    })
    Name (_DSD, Package () {
      ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
          Package () {"actuator-src", \_SB.I2C0.MTR0},
          Package () {"isp-src", \_SB.ISP0},
          Package () {"cix,camera-module-index", 0},
          Package () { "power0-supply",  Package () { \_SB.CPE0 } },
          Package () { "power1-supply",  Package () { \_SB.CPE1 } },
          Package () { "reset-gpios",  Package () { ^IIS0, 0, 0, 0 } },
          Package () { "pwdn-gpios",  Package () { ^IIS0, 0, 1, 0 } },
          Package () { "pinctrl-names", Package () {"default", "gpio"}},
      }
    })
    Name (CLKT, Package() {
      Package() {CLK_TREE_CAMERA_MCLK0, "mclk", \_SB.I2C0.IIS0},
    })
  }
  //ISP motor
  Device (MTR0) {
    Name (_HID, "CIXH3023")
    Name (_UID, 0x0)
    Name (_STA, 0xF)
    Name (_CCA, 0)
    Name (_CRS, ResourceTemplate () {
      I2cSerialBusV2 (0x40,
                      ControllerInitiated,
                      124999999,
                      AddressingMode7Bit,
                      "\\_SB.I2C0",
                      0x0,
                      ResourceConsumer,
                      ,
                      Exclusive
                      ,)
    })
    Name (_DSD, Package () {
      ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
          Package () {"pi-max-frequency", 124999999},
      }
    })
  }
}


Scope (\_SB.I2C1)
{
  //ISP imgsensor02
  Device (IIS2) {
    Name (_HID, "CIXH3024")
    Name (_UID, 0x02)
    Name (_STA, 0x00)
    Name (_CCA, 0)
    Name (_CRS, ResourceTemplate () {
      I2cSerialBusV2 (0x38,
                      ControllerInitiated,
                      400000,
                      AddressingMode7Bit,
                      "\\_SB.I2C1",
                      0x0,
                      ResourceConsumer,
                      ,
                      Exclusive
                      ,)
      PinGroupFunction(Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_cam2_hw", ResourceConsumer, , RawDataBuffer () { 0x0 })
      PinGroupFunction(Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_cam2_hw", ResourceConsumer, , RawDataBuffer () { 0x1 })
      GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
      "\\_SB.GPI1", 0, ResourceConsumer) { 10, 7 }
    })
    Name (_DSD, Package () {
      ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
          Package () {"cix,camera-module-index", 2},
          Package () { "power0-supply",  Package () { \_SB.CPE2 } },
          Package () { "power1-supply",  Package () { \_SB.CPE3 } },
          Package () { "reset-gpios",  Package () { ^IIS2, 0, 0, 0 } },
          Package () { "pwdn-gpios",  Package () { ^IIS2, 0, 1, 0 } },
          Package () { "pinctrl-names", Package () {"default", "gpio"}},
      }
    })
    Name (CLKT, Package() {
      Package() {CLK_TREE_CAMERA_MCLK2, "mclk", \_SB.I2C1.IIS2},
    })
  }
}

Scope (\_SB.I2C3)
{
  //ISP imgsensor01
  Device (IIS1) {
    Name (_HID, "CIXH3024")
    Name (_UID, 0x1)
    Name (_STA, 0xF)
    Name (_CCA, 0)
    Name (_CRS, ResourceTemplate () {
      I2cSerialBusV2 (0x36,
                      ControllerInitiated,
                      400000,
                      AddressingMode7Bit,
                      "\\_SB.I2C3",
                      0x0,
                      ResourceConsumer,
                      ,
                      Exclusive
                      ,)
      PinGroupFunction(Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_cam1_hw", ResourceConsumer, , RawDataBuffer () { 0x0 })
      PinGroupFunction(Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_cam1_hw", ResourceConsumer, , RawDataBuffer () { 0x1 })
      GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
      "\\_SB.GPI1", 0, ResourceConsumer) { 17, 19 }
    })
    Name (_DSD, Package () {
      ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
          Package () {"cix,camera-module-index", 1 },
          Package () { "power0-supply",  Package () { \_SB.CPE0 } },
          Package () { "power1-supply",  Package () { \_SB.CPE1 } },
          Package () { "reset-gpios",  Package () { ^IIS1, 0, 0, 0 } },
          Package () { "pwdn-gpios",  Package () { ^IIS1, 0, 1, 0 } },
          Package () { "pinctrl-names", Package () {"default", "gpio"}},
      }
    })
    Name (CLKT, Package() {
      Package() {CLK_TREE_CAMERA_MCLK1, "mclk", \_SB.I2C3.IIS1},
    })
  }
}

Scope (\_SB.I2C4)
{
  //ISP imgsensor03
  Device (IIS3) {
    Name (_HID, "CIXH3024")
    Name (_UID, 0x3)
    Name (_STA, 0xF)
    Name (_CCA, 0)
    Name (_CRS, ResourceTemplate () {
      I2cSerialBusV2 (0x3a,
                      ControllerInitiated,
                      400000,
                      AddressingMode7Bit,
                      "\\_SB.I2C4",
                      0x0,
                      ResourceConsumer,
                      ,
                      Exclusive
                      ,)
      PinGroupFunction(Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_cam3_hw", ResourceConsumer, , RawDataBuffer () { 0x0 })
      PinGroupFunction(Exclusive, 0x0, "\\_SB.MUX0", 0, "pinctrl_cam3_hw", ResourceConsumer, , RawDataBuffer () { 0x1 })
      GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
      "\\_SB.GPI1", 0, ResourceConsumer) { 13, 14 }
    })
    Name (_DSD, Package () {
      ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
          Package () {"cix,camera-module-index", 3},
          Package () { "power0-supply",  Package () { \_SB.CPE2 } },
          Package () { "power1-supply",  Package () { \_SB.CPE3 } },
          Package () { "reset-gpios",  Package () { ^IIS3, 0, 0, 0 } },
          Package () { "pwdn-gpios",  Package () { ^IIS3, 0, 1, 0 } },
          Package () { "pinctrl-names", Package () {"default", "gpio"}},
      }
    })
    Name (CLKT, Package() {
      Package() {CLK_TREE_CAMERA_MCLK3, "mclk", \_SB.I2C4.IIS3},
    })
  }
}