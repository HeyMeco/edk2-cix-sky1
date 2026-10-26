# Validation status

States: **build** (compiles and packages), **static** (layout / blob / ACPI
checks pass), **boot** (boots an OS on hardware).

| Board | Build | Static | Boot | Notes |
|---|---|---|---|---|
| Radxa Orion O6 | ✓ | ✓ | – | BL1/BL2/TZ/EC/SFH/se_config identical to Radxa release 1.3.1 |
| Radxa Orion O6N | ✓ | ✓ | – | BL1/BL2/TZ/EC/SFH/se_config identical to Radxa release 1.3.1 |
| Minisforum MS-R1 | ✓ | ✓ | – | rebased onto CIX 2026Q3; layout identical to Minisforum's |
| Orange Pi 6 Plus | ✓ | ✓ | – | all regions except BL33 identical to vendor 1.4 image |
| Orange Pi 6 | ✓ | ✓ | – | all regions except BL33 identical to vendor v1.0 image; DSDT board deltas match |

The PCIe 64-bit window option was verified on an Orion O6 by its original
author (Neol00/edk2-cix-unlocked); the other boards share the SoC and the
code but have not been tested with it.

An earlier build of the Orange Pi 6 Plus image failed in the vendor BL2 with
"Verify trusted_key_no.crt fail": the trusted-key certificate was regenerated
with the OEM key instead of the CIX-root-signed one the vendor uses, and the
AArch64 CIX tools had signed with a non-standard PSS salt. Since then the FIP
certificates match each vendor's (the Plus trusted-key certificate is
byte-identical to the vendor's).

Checked on every build (`scripts/build.py`):

* every firmware-header entry lies in its flash region, holds the staged
  file, and regions do not overlap;
* every vendor blob matches its SHA-256 (`fetch-blobs.py`);
* every FIP certificate uses RSA-PSS with a 32-byte salt.

Checked during development:

* `scripts/compare-image.py` against vendor images (table above);
* ACPI parity for the Orange Pi boards and `acpiexec` load of DSDT + SSDT
  ([provenance](provenance/orangepi.md));
* UEFI FD and configuration blobs are bit-identical across rebuilds.

## Reporting results

When you boot one of these images, please open an issue with the board, the
release, what works (NVMe, USB, network, display, audio, fan, reboot/power-off)
and the output of `dmesg | grep -iE 'acpi|error'`.
