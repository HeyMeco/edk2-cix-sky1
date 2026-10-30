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

## MS-R1 Secure Boot and SPI TPM (tested with Windows 11; Linux not yet)

Secure Boot defaults are Microsoft's certificates (Windows OEM Devices PK;
KEK CA 2011 and 2K CA 2023; Windows Production PCA 2011, Windows UEFI CA 2023,
UEFI CA 2011/2023 and Option ROM UEFI CA 2023 in db). Nothing is enrolled
automatically: Device Manager → Secure Boot Configuration → Reset Secure Boot
Keys enrolls them and turns Secure Boot on. The same page shows the TPM the
firmware found (device, vendor, firmware version, active PCR banks; edk2
patch 0002 in `patches/2026Q3/edk2/`).

The TPM stack is `Tcg2Dxe` over `Tpm2DeviceLibCdnsSpi` (`\_SB.SPI0` at
0x04090000, the "SPI1" dTPM port of Minisforum's block diagram and CIX's
`SPI1_*` pads, which the driver puts in SPI mode; chip select 1, 6.25 MHz,
mode 0; the controller's FIFO is 32 bits wide, so each entry shifts four bytes
and wait states are handled within an entry) in firmware, with
`DxeTpm2MeasureBootLib` measuring loaded images into PCR 4 (`SPI_TPM_ENABLE`,
edk2-platforms patch 0013). For the OS, `SpiTpmAcpiDxe` publishes the TPM as a CRB TPM with
the ACPI start method (TPM2 start method 2): it allocates the CRB page
(reserved memory below 4 GB, as Microsoft's TPM ACPI profile requires for
the control area, and uncached only in the memory map so that the OS driver
and the AML map it alike) and patches its address into the TPM2 table and
the SSDT,
whose `_DSM` start function moves each command over SPI by driving the
controller registers in AML, writing the data FIFO in transfers of at most
12 bytes (longer ones the NPCT750 answers only after seven or eight wait
state bytes). Windows' inbox `tpm.sys` and Linux's `tpm_crb`
therefore need no SPI controller driver. `\_SB.SPI0` is reported absent at
ReadyToBoot so that no OS SPI driver competes for the controller; CIX's
Windows SPI driver (`CixSpiDriver`) is in any case only test-signed, which
Windows does not load with Secure Boot on. The `_DSM` also answers the
Physical Presence Interface (1.3) with every operation reported as not
implemented and the Platform Reset Attack Mitigation (memory clear) request,
and the driver randomizes and disables the platform hierarchy at EndOfDxe.

Expected boot log lines, from first to last failure point:

* `SpiTpm: SPI0 sends N bits per FIFO entry (need 32)`: the FIFO is not
  the 32-bit one of Sky1 (first test, 2026-10-30, measured 31: as expected).
* `SpiTpm: no TPM on SPI0 (...)`: nothing answered. Before probing, the
  driver pulses `TPM_RST_L` (GPIO007, per Minisforum's notes in the board's
  GPIO table): the board only sets it high, and without the pulse MISO stays
  high (ID 0xFFFFFFFF) on every chip select.
* `SpiTpm: SPI0: vendor 0x1050 device 0x00fc ...`: the NPCT750 (first seen
  2026-10-30, on chip select 1 after the reset pulse); Tcg2Dxe then runs
  TPM2_Startup.
* `SpiTpm: wait state shifted the command, sending it again`: harmless if
  rare; if it repeats, the TPM's wait states vary from one transfer to the
  next more than the guess (the length last seen) can follow.
* `SpiTpm: TPM did not take command 0x... : ...`: a command failed; Tcg2Dxe
  then stops using the TPM for the rest of the boot (Secure Boot page: "Not
  found"). Defining `TPM_SPI_DIAG` in the C driver logs, after TPM2_Startup,
  the burst count, which data FIFO write sizes the TPM takes, and raw
  transactions showing in which byte it reports ready.
* `SpiTpmAcpiDxe: TPM published, CRB at 0x...`: the tables are installed. In
  the OS: `tpm.msc` (Windows), `/dev/tpm0` and `tpm2_pcrread` (Linux).
* After the OS starts, the AML traces on the same UART (`SPI_TPM_TRACE_UART`
  in `SpiTpmAcpi.h`): `SpiTpm AML: _CRS` when the OS enumerates the device,
  `SpiTpm AML: _DSM <first UUID dword> function <n>` for every query, PPI and
  memory clear call, and `SpiTpm AML: command 0x<code> -> 0x<response code>`
  for every TPM command, or `failed at step <n> (0x<result>, status 0x<last
  register read>)` with the steps of `TRUN` (1 locality, 2 command size, 3
  commandReady, 4 FIFO write, 5 expect, 6 response wait, 7-9 response read).
  First Windows boot (2026-10-30, before the trace, the reserved memory and
  the memory clear `_DSM`): Device Manager reported the ACPI TPM 2.0 device
  with code 10. Second (with the trace): Windows evaluated `_CRS` and sent
  TPM2_GetCapability, which failed at step 4 with result 3: the data FIFO's
  wait state guess, seeded from the register reads, was lower than what the
  FIFO needed, and a write that found the TPM still waiting did not raise
  it, so every retry failed alike. Third: raising the guess to every wait
  seen made it oscillate (a TPM briefly busy after commandReady pushed it to
  5, the next try was shifted and set it back to 1) until the firmware gave
  up on its first commands and Tcg2Dxe disabled the TPM (Secure Boot page:
  "Not found"). A write that finds the TPM not ready now raises the guess by
  one only when the expected ready byte ended a FIFO entry (the one case a
  short guess is not caught as shifted), and otherwise retries as is (AML
  and C). Fourth, with raw transactions logged (`TPM_SPI_DIAG` in the C
  driver): the NPCT750 answers a transfer of up to 16 bytes with the header
  after one wait state byte, and a longer one only after seven or eight
  (ready in byte 10 or 11), whatever the data. Startup and SelfTest (11 and
  12 bytes) went through; GetCapability (19-byte first chunk) never did,
  since the driver gave up at byte 7. Data FIFO writes are now at most 12
  bytes per transfer, and long reads (which follow the ready byte wherever
  it is) keep their own wait state guess, in both the C driver and the AML.
  Fifth (2026-10-30): no failed commands in firmware, the Secure Boot page
  shows the TPM, and Windows 11 with Secure Boot on recognizes it (Device
  Manager: Trusted Platform Module 2.0 with the CRB page as its only
  resource, no conflicts; `tpm.msc` ready).

Not covered: no PEI phase, so PCR 0 lacks the firmware volumes; no physical
presence operations (TPM clear from the OS); Linux (`tpm_crb`) is untested.
The `_DSM` runs each command to completion instead of returning at once as
Microsoft's profile asks; Windows accepts it. Nothing acknowledges
`CTRL_REQ` cmdReady/goIdle, which Windows does not need for start method 2
(Linux skips them for this start method). The AML trace stays on while the
OS side is young; remove `SPI_TPM_TRACE_UART` to silence it.

## Reporting results

When you boot one of these images, please open an issue with the board, the
release, what works (NVMe, USB, network, display, audio, fan, reboot/power-off)
and the output of `dmesg | grep -iE 'acpi|error'`.
