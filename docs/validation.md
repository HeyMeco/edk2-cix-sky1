# Validation status

States: **build** (compiles and packages), **static** (layout / blob / ACPI
checks pass), **boot** (boots an OS on hardware).

| Board | Build | Static | Boot | Notes |
|---|---|---|---|---|
| Radxa Orion O6 | ✓ | ✓ | – | BL1/BL2/TZ/EC/SFH/se_config identical to Radxa release 1.3.1 |
| Radxa Orion O6N | ✓ | ✓ | – | BL1/BL2/TZ/EC/SFH/se_config identical to Radxa release 1.3.1 |
| Minisforum MS-R1 | ✓ | ✓ | – | rebased onto CIX 2026Q3; all regions except BL33 and the pm_config timestamp identical to the vendor 2026-04-29 image |
| Orange Pi 6 Plus | ✓ | ✓ | ✓ | all regions except BL33 identical to vendor 1.4 image |
| Orange Pi 6 | ✓ | ✓ | ✓ | all regions except BL33 identical to vendor v1.0 image; DSDT board deltas match |

The PCIe 64-bit window option was verified on an Orion O6 by its original
author (Neol00/edk2-cix-unlocked); the other boards share the SoC and the
code but have not been tested with it.

An earlier build of the Orange Pi 6 Plus image failed in the vendor BL2 with
"Verify trusted_key_no.crt fail": the trusted-key certificate was regenerated
with the OEM key instead of the CIX-root-signed one the vendor uses, and the
AArch64 CIX tools had signed with a non-standard PSS salt. Since then the FIP
certificates match each vendor's (the Plus trusted-key certificate is
byte-identical to the vendor's).

Earlier MS-R1 builds produced no UART output at all: BL1/BL2 came from
Minisforum's public `edk2-non-osi` tree, which is signed with the CIX default
root key, while retail boards boot only the `fb3f60e4…` chain in Minisforum's
own firmware (the same as Orange Pi's). The BL33 trusted-key certificate was
also Minisforum's git copy rather than the one the retail BL2 verifies. Both
now follow the vendor image.

The first MS-R1 boot after the vendor firmware then showed "question value
mismatch with option value" on the CPU and SoC setup pages: the vendor's
223-byte `PlatformSetupVar` (Minisforum layout) was kept and read as the
240-byte 2026Q3 structure, which also fed shifted PCIe, I2C, USB, voltage
and clock-gating settings to the platform. Patch 0011 restores the defaults
when the stored size does not match. The vendor Orange Pi 6 image stores 234
bytes, so the same applies after flashing over it.

That alone did not fix the MS-R1: its DSC turns `TOKEN_SETUP_SUPPORT` off for
its own SetupManagerDxe, so `PlatformSetupVariableInitDxe` was listed only
under `APRIORI DXE` and never placed in the FV. Nothing created
`PlatformSetupVar`, `SystemTableVar` or `NetworkStackVar`, F10 inside a setup
page failed with "Submit Fail For Form: System Manager" (HII routing finds no
stored variable to update), and setup choices never reached the config
parameters. `MGP1WSB.fdf` now lists the driver in the FV as well.

Builds before the variable-store fix booted the Orange Pi 6 Plus only once
after each flash: the store was placed at `0x388000` (Merak) instead of the
`0x380000` the Orange Pi layout reserves, so formatting it on first boot
erased the memory/PM configuration, se_config and the BL33 header at
`0x3F8000`–`0x400000`. The Orange Pi 6 had the same setting.

Earlier Orange Pi 6 builds reset in a loop with a NULL-pointer exception in
UiApp: the board's platform hooks did not install the RA8900CE I2C protocol
that `RealTimeClockRuntimeDxe` depends on, so the RTC architectural protocol
was never produced, no UEFI driver (ConSplitter, consoles) was dispatched,
and `gST->ConOut` stayed NULL. The vendor image installs it from
`PlatformDxe`, as Merak does.

Checked on every build (`scripts/build.py`):

* every firmware-header entry lies in its flash region, holds the staged
  file, and regions do not overlap;
* every vendor blob matches its SHA-256 (`fetch-blobs.py`);
* every FIP certificate uses RSA-PSS with a 32-byte salt;
* the variable store (3 × `SPI_VARIABLE_SIZE` from `SPI_VARIABLE_BASE`) lies
  inside the layout's reserved region.

Checked during development:

* `scripts/compare-image.py` against vendor images (table above);
* ACPI parity for the Orange Pi boards and `acpiexec` load of DSDT + SSDT
  ([provenance](provenance/orangepi.md));
* UEFI FD and configuration blobs are bit-identical across rebuilds.

## MS-R1 Secure Boot and SPI TPM (not yet tested on hardware)

Secure Boot defaults are Microsoft's certificates (Windows OEM Devices PK;
KEK CA 2011 and 2K CA 2023; Windows Production PCA 2011, Windows UEFI CA 2023,
UEFI CA 2011/2023 and Option ROM UEFI CA 2023 in db). Nothing is enrolled
automatically: Device Manager → Secure Boot Configuration → Reset Secure Boot
Keys enrolls them and turns Secure Boot on.

The TPM stack is `Tcg2Dxe` over `Tpm2DeviceLibCdnsSpi` (SPI0, chip select 0,
6.25 MHz, mode 0) in firmware, with `DxeTpm2MeasureBootLib` measuring loaded
images into PCR 4 (`SPI_TPM_ENABLE`, patch 0012). For the OS, `SpiTpmAcpiDxe`
publishes the TPM as a CRB TPM with the ACPI start method (TPM2 start method
2): it allocates the CRB page (ACPI NVS, below 4 GB) and patches its address
into the TPM2 table and the SSDT, whose `_DSM` start function moves each
command over SPI0 by driving the controller registers in AML. Windows' inbox
`tpm.sys` and Linux's `tpm_crb` therefore need no SPI controller driver.
`\_SB.SPI0` is reported absent at ReadyToBoot so that no OS SPI driver
competes for the controller; CIX's Windows SPI driver (`CixSpiDriver`) is in
any case only test-signed, which Windows does not load with Secure Boot on.
The `_DSM` also answers the Physical Presence Interface (1.3) with every
operation reported as not implemented, and the driver randomizes and
disables the platform hierarchy at EndOfDxe.

Expected boot log lines, from first to last failure point:

* `SpiTpm: SPI0 sends N bits per FIFO entry (need 8)`: the controller packs
  32-bit FIFO words (as CIX's SpiLib assumes); the driver needs a 32-bit mode.
* `SpiTpm: no TPM on SPI0 (...)`: nothing answered on SPI0/CS0; check the
  module and which SPI bus and chip select the header uses.
* `SpiTpm: SPI0: vendor 0x1050 device 0x...`: NPCT750 found (Nuvoton is
  vendor 0x1050); Tcg2Dxe then runs TPM2_Startup.
* `SpiTpmAcpiDxe: TPM published, CRB at 0x...`: the tables are installed. In
  the OS: `tpm.msc` (Windows), `/dev/tpm0` and `tpm2_pcrread` (Linux).

Not covered: no PEI phase, so PCR 0 lacks the firmware volumes; no physical
presence operations (TPM clear from the OS). Whether Windows accepts a
start-method-2 CRB whose control area is plain memory is untested: nothing
acknowledges `CTRL_REQ` cmdReady/goIdle (Linux skips them for this start
method).

## Reporting results

When you boot one of these images, please open an issue with the board, the
release, what works (NVMe, USB, network, display, audio, fan, reboot/power-off)
and the output of `dmesg | grep -iE 'acpi|error'`.
