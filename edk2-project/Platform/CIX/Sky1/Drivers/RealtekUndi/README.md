# Realtek UEFI UNDI driver

`RtkUndiDxe.efi` is the newer Realtek build Radxa ships for the Orion O6
(`radxa/edk2-platforms` `Platform/Radxa/Drivers/RTL8126`): it drives the
RTL8126 (5 GbE) in addition to the RTL8125, 8136, 8161 and 8168 that the copy
in `edk2-platforms/Platform/CIX/Sky1/Drivers/RealtekLan` covers. Realtek's
license is in `License.txt`.

`Sky1Board.fdf` puts it into the FV in place of the CIX copy, together with
CIX's `RealtekLanDxe` (which gates the UNDI drivers on the setup option
*Network Stack*, default off) and CIX's USB UNDI driver.
