#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Build and package UEFI firmware for one or more boards.

Runs inside the build container (see build.sh / docker/Dockerfile).  For each
board it prepares the tree, stages the vendor binaries, builds the BL33 UEFI
FD with the board DSC, generates the CIX flash configuration blobs, signs the
FD into bootloader3.img and packages the full and OTA SPI images.

The result for each board is dist/<board>_<version>.bin: the full SPI image
(cix_flash_all.bin) padded with 0xFF to the size of the flash part.  The
version defaults to bios_version in manifests/pins.json (the CIX BSP release,
e.g. 2026Q3).  Intermediate files stay in output/<board>/<TARGET>/; --extras
also packs the OTA image, FD and manifest into dist/extras/.

Usage: build.py BOARD... | --all  [--target RELEASE|DEBUG] [--version V] [--extras]
"""

import argparse
import datetime
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import ROOT, board, boards, die, git, host_arch, load_json, log, patch_dir, run, sha256  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent / "open"))
import cixfw  # noqa: E402
import memcfg  # noqa: E402
import trusted  # noqa: E402

PACKAGE_TOOL = ROOT / "edk2-non-osi/Platform/CIX/Sky1/PackageTool"
PACKAGES_PATH = ["edk2-project", "edk2", "edk2-platforms", "edk2-non-osi", "edk2-platforms/Silicon/Intel"]
IMAGE_TYPES = {1: "bootloader1", 2: "bootloader2", 3: "memory_config", 4: "pm_config", 6: "se_config",
               7: "bootloader3", 8: "sfh_fw", 9: "trustzone_config", 100: "reserved"}


def flash_size(cfg):
    v = cfg["flash"]["size"]
    return v if isinstance(v, int) else int(v, 0)


def source_date():
    """Build timestamp: SOURCE_DATE_EPOCH, else the superproject commit time."""
    epoch = os.environ.get("SOURCE_DATE_EPOCH") or git("log", "-1", "--format=%ct", check=False) or "0"
    return datetime.datetime.fromtimestamp(int(epoch), tz=datetime.timezone.utc)


def default_version():
    return load_json("manifests/pins.json")["bios_version"]


def build_env():
    env = dict(os.environ)
    env["WORKSPACE"] = str(ROOT)
    env["PACKAGES_PATH"] = ":".join(str(ROOT / p) for p in PACKAGES_PATH)
    env["EDK_TOOLS_PATH"] = str(ROOT / "edk2/BaseTools")
    env["CONF_PATH"] = str(ROOT / "Conf")
    env.setdefault("PYTHON_COMMAND", "python3")
    if "GCC5_AARCH64_PREFIX" not in env:
        die("GCC5_AARCH64_PREFIX is not set; run builds through ./build.sh (the container)")
    return env


def ensure_basetools(env):
    bindir = ROOT / "edk2/BaseTools/Source/C/bin"
    stamp = bindir / ".edk2-cix-host"
    if (bindir / "GenFw").exists() and stamp.exists() and stamp.read_text() == host_arch():
        return
    if (bindir / "GenFw").exists():
        log("BaseTools were built for another host architecture; rebuilding")
        run(["make", "-C", "edk2/BaseTools", "clean"], cwd=ROOT, env=env, capture=True)
        run(["git", "submodule", "foreach", "--recursive", "-q", "git clean -fdxq"], cwd=ROOT / "edk2", capture=True)
    log("building EDK2 BaseTools")
    run(["make", "-C", "edk2/BaseTools", f"-j{os.cpu_count()}"], cwd=ROOT, env=env)
    stamp.write_text(host_arch())


def edk2_build(cfg, target, version, date, commit, jobs, log_path):
    env = build_env()
    ensure_basetools(env)
    (ROOT / "Conf").mkdir(exist_ok=True)
    defines = {
        "BOARD_NAME": "evb",          # CIX DSCs distinguish evb/fpga/emu here
        "BUILD_DATE": date.strftime("%Y%m%d"),
        "COMMIT_HASH": commit,
        "SMP_ENABLE": "1",
        "ACPI_BOOT_ENABLE": "1",
        "FASTBOOT_LOAD": cfg.get("fastboot", "disable"),
        "VARIABLE_TYPE": "SPI",
        "STANDARD_MM": "TRUE",
        "SYSTEM_LOADER": "common",
        # Version strings: CIX boards use UEFI_FW_STAGE, Radxa boards DEB_VERSION.
        "UEFI_FW_STAGE": version,
        "DEB_VERSION": version,
        "EDK2_COMMIT_HASH": git("rev-parse", "--short=12", "HEAD", cwd=ROOT / "edk2"),
        "EDK2_PLATFORMS_COMMIT_HASH": git("rev-parse", "--short=12", "HEAD", cwd=ROOT / "edk2-platforms"),
        "EDK2_NON_OSI_COMMIT_HASH": git("rev-parse", "--short=12", "HEAD", cwd=ROOT / "edk2-non-osi"),
    }
    defines.update({k: str(v) for k, v in (cfg.get("defines") or {}).items()})
    args = ["build", "-a", "AARCH64", "-t", "GCC5", "-b", target, "-p", cfg["dsc"], "-n", str(jobs)]
    for k, v in defines.items():
        args += ["-D", f"{k}={v}"]
    script = f'source edk2/edksetup.sh --reconfig >/dev/null && exec {" ".join(map(shell_quote, args))}'
    log(f"{cfg['board']}: building {cfg['dsc']} ({target})")
    with open(log_path, "w") as lf:
        rc = subprocess.run(["bash", "-c", script], cwd=ROOT, env=env, stdout=lf, stderr=subprocess.STDOUT).returncode
    if rc != 0:
        tail = Path(log_path).read_text(errors="replace").splitlines()[-40:]
        sys.stderr.write("\n".join(tail) + "\n")
        die(f"{cfg['board']}: EDK2 build failed, see {log_path}")
    fd = ROOT / "Build" / cfg["build_dir"] / f"{target}_GCC5/FV/SKY1_BL33_UEFI.fd"
    if not fd.exists():
        die(f"{fd} was not produced")
    return fd


STMM_DSC = "Platform/CIX/Sky1/StandaloneMm/Sky1StandaloneMm.dsc"


def stmm_build(target, version, commit, jobs, log_path):
    """StandaloneMM from source (edk2-project/Platform/CIX/Sky1/StandaloneMm),
    for OP-TEE in place of the CIX image."""
    env = build_env()
    ensure_basetools(env)
    (ROOT / "Conf").mkdir(exist_ok=True)
    # FwVersionDxe shows this; StmmInfoLib takes at most 63 characters
    stmm_version = f"edk2-cix-{version}-{commit}"[:63]
    args = ["build", "-a", "AARCH64", "-t", "GCC5", "-b", target, "-p", STMM_DSC, "-n", str(jobs),
            "-D", f"STMM_VERSION={stmm_version}"]
    script = f'source edk2/edksetup.sh --reconfig >/dev/null && exec {" ".join(map(shell_quote, args))}'
    log(f"building {STMM_DSC} ({target})")
    with open(log_path, "w") as lf:
        rc = subprocess.run(["bash", "-c", script], cwd=ROOT, env=env, stdout=lf, stderr=subprocess.STDOUT).returncode
    if rc != 0:
        tail = Path(log_path).read_text(errors="replace").splitlines()[-40:]
        sys.stderr.write("\n".join(tail) + "\n")
        die(f"StandaloneMM build failed, see {log_path}")
    fd = ROOT / "Build/Sky1StandaloneMm" / f"{target}_GCC5/FV/BL32_AP_MM.fd"
    if not fd.exists():
        die(f"{fd} was not produced")
    return fd


def shell_quote(s):
    s = str(s)
    return s if s and all(c.isalnum() or c in "-_./=:,+" for c in s) else "'" + s.replace("'", "'\\''") + "'"


def make_config(pkg, name, out_name, dest, env):
    """Build mem_config/pm_config/trustzone_config from a copy of the board source."""
    src = pkg / name
    if not src.is_dir():
        return False
    work = dest.parent.parent / "work" / name
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(src, work)
    run(["make"], cwd=work, env=env, capture=True)
    shutil.copyfile(work / out_name, dest)
    log(f"  built {name}/{out_name}")
    return True


# The AArch64 CIX certificate tools were built against OpenSSL 1.1 headers
# but link libcrypto.so.3.  EVP_PKEY_CTX_set_rsa_pss_saltlen() expanded to
# RSA_pkey_ctx_ctrl(ctx, EVP_PKEY_OP_SIGN|EVP_PKEY_OP_VERIFY, ...) with the
# 1.1 operation bits (0x18); OpenSSL 3 renumbered them, so a DigestSign
# context (EVP_PKEY_OP_SIGNCTX, 0x80) rejects the call, the tools only treat
# 0 as failure, and OpenSSL 3.0 signs with the maximum salt (0x15E) instead of
# 32 bytes, which BL2 rejects.  Passing optype -1 (as the tools already do for
# the padding mode) skips the operation check: "mov w1, #0x18" becomes
# "mov w1, #-1".  The x86_64 builds link OpenSSL statically and are correct.
CIX_TOOL_FIXES = {
    "cert_uefi_create_rsa": (
        "4daec7e0de6f3109752294f976155ad040e5ee319546016141dc01b269ee1e2e", [(0x4B44, "01038052", "01008012")]),
    "cix_regen_trusted_key_cert": (
        "315c334360a4585afb69078a76d9771e99173bfae91018fdd77cfb861fbb3458", [(0x2FC0, "01038052", "01008012")]),
}


def stage_cix_tool(tool, dest):
    """Copy a CIX PackageTool binary for the host architecture, fixed up."""
    arch = host_arch()
    src = PACKAGE_TOOL / arch / tool
    data = bytearray(src.read_bytes())
    if arch == "AARCH64" and tool in CIX_TOOL_FIXES:
        digest, fixes = CIX_TOOL_FIXES[tool]
        if sha256(src) != digest:
            die(f"{src} changed (sha256 {sha256(src)}); re-check the OpenSSL salt length fix in CIX_TOOL_FIXES")
        for off, old, new in fixes:
            old, new = bytes.fromhex(old), bytes.fromhex(new)
            if data[off:off + len(old)] != old:
                die(f"{src}: unexpected bytes at {off:#x}")
            data[off:off + len(new)] = new
    dest.write_bytes(data)
    dest.chmod(0o755)


def cix_tool(tool, *args):
    """Command line for a CIX PackageTool binary staged in the pr/ directory."""
    return [f"./{tool}", *args]


def check_certificates(cfg, pr):
    """The FIP certificates must use RSA-PSS with a 32-byte salt."""
    for crt in ("trusted_key_no.crt", "nt_fw_key.crt", "nt_fw_cert.crt"):
        text = run(["openssl", "x509", "-inform", "DER", "-in", str(pr / "certs" / crt), "-noout", "-text"],
                   capture=True)
        salts = {l.split(":")[1].strip() for l in text.splitlines() if "Salt Length" in l}
        if salts != {"0x20"}:
            die(f"{cfg['board']}: {crt} uses PSS salt length {salts}, expected 0x20")


BOOT_FIRMWARE = ("bootloader1.img", "bootloader2.img", "sfh_fw.bin")


def stage_boot_firmware(cfg, fw, mode, bootloader2=None, bootloader1=None):
    """Select the boot firmware (BL1: SE, CSU_PM, BL2; bootloader2: BL31,
    OP-TEE, StMM) and check that it matches the board's signing chain.

    mode "vendor": the board vendor's images, as staged by fetch-blobs.py.
    mode "cix": CIX's 2026Q3 images signed for the board's chain; the SE,
    CSU_PM, BL2, BL31 and OP-TEE builds are the same on both chains, so all
    boards run one boot firmware.  bootloader2: a source-built FIP,
    bootloader1: one with a source-built BL2 (--open).
    """
    chain = cfg["signing_chain"]
    if mode == "cix":
        src = PACKAGE_TOOL / cixfw.CHAINS[chain]["cix_dir"]
        for f in BOOT_FIRMWARE:
            shutil.copyfile(src / f, fw / f)
        ver = cixfw.mem_config_version((fw / "memory_config.bin").read_bytes())
        if ver != (2, 0):
            die(f"{cfg['board']}: memory_config.bin is v{ver[0]}.{ver[1]}; the CIX 2026Q3 SE firmware "
                "reads v2.0 (port the board's mem_config first)")
    if bootloader2:
        shutil.copyfile(bootloader2, fw / "bootloader2.img")
    if bootloader1:
        shutil.copyfile(bootloader1, fw / "bootloader1.img")
    bl1 = cixfw.bl1_chain((fw / "bootloader1.img").read_bytes())
    bl2 = cixfw.fip_chain((fw / "bootloader2.img").read_bytes())
    if (bl1, bl2) != (chain, chain):
        die(f"{cfg['board']}: boot firmware is signed for chain {bl1}/{bl2}, the board needs {chain}")


def package(cfg, fd, outdir, boot_firmware="vendor", bootloader2=None, bootloader1=None, open_certs=False):
    pkg = ROOT / cfg["package"]
    pr = outdir / "pr"
    shutil.rmtree(pr, ignore_errors=True)
    fw = pr / "Firmwares"
    for d in (fw, pr / "Keys", pr / "certs"):
        d.mkdir(parents=True)

    # 1. CIX base firmware, as in build_and_package.sh
    for f in ["bootloader1.img", "bootloader2.img"] + sorted(p.name for p in (PACKAGE_TOOL / "Firmwares").glob("*.bin")):
        shutil.copyfile(PACKAGE_TOOL / "Firmwares" / f, fw / f)
    (fw / "dummy.bin").write_bytes(b"\xff" * 8192)

    # 2. board configuration blobs built from source
    host_env = {k: v for k, v in os.environ.items() if not k.startswith("GCC5")}
    host_env["SOURCE_DATE_EPOCH"] = str(int(source_date().timestamp()))
    host_env["TZ"] = "UTC"
    # mem_config_v2: the board's DRAM settings in the v2.0 format the CIX
    # 2026Q3 SE firmware reads, for boards whose vendor format is older
    mem = "mem_config_v2" if boot_firmware == "cix" and (pkg / "mem_config_v2").is_dir() else "mem_config"
    make_config(pkg, mem, "memory_config.bin", fw / "memory_config.bin", host_env)
    if mem == "mem_config_v2" and (pkg / "mem_config").is_dir():
        # the port must not change a single DRAM setting
        ref = pr / "memory_config.vendor-format.bin"
        make_config(pkg, "mem_config", "memory_config.bin", ref, host_env)
        diffs = memcfg.compare(ref.read_bytes(), (fw / "memory_config.bin").read_bytes())
        if diffs:
            die(f"{cfg['board']}: mem_config_v2 differs from mem_config: {'; '.join(diffs)}")
        log("  mem_config_v2 has the same settings as mem_config")
    make_config(pkg, "pm_config", "csu_pm_config.bin", fw / "csu_pm_config.bin", host_env)
    make_config(pkg, "trustzone_config", "trustzone_config.bin", fw / "trustzone_config.bin", host_env)

    # 3. board firmware overrides (vendor blobs staged by fetch-blobs.py)
    for f in sorted((pkg / "Firmwares").glob("*")):
        if f.is_file() and not f.name.startswith("."):
            shutil.copyfile(f, fw / f.name)
    stage_boot_firmware(cfg, fw, boot_firmware, bootloader2, bootloader1)

    # 4. tools (host architecture, see CIX_TOOL_FIXES), keys and flash layouts
    for tool in ("cert_uefi_create_rsa", "fiptool", "cix_regen_trusted_key_cert"):
        stage_cix_tool(tool, pr / tool)
    for k in (PACKAGE_TOOL / "Keys").glob("*.pem"):
        shutil.copyfile(k, pr / "Keys" / k.name)
    for k in ("oem_privatekey.pem", "oem_publickey.pem"):
        if (pkg / "Keys" / k).exists():
            shutil.copyfile(pkg / "Keys" / k, pr / "Keys" / k)
    shutil.copyfile(ROOT / cfg["flash"]["config_all"], pr / "spi_flash_config_all.json")
    shutil.copyfile(ROOT / cfg["flash"]["config_ota"], pr / "spi_flash_config_ota.json")
    shutil.copyfile(fd, outdir / "SKY1_BL33_UEFI.fd")

    # 5. sign the FD and wrap it in a FIP (bootloader3.img).  The trusted key
    #    certificate is either the pregenerated, CIX-root-signed one the board
    #    vendor ships (Certs/trusted_key_no.crt, staged by fetch-blobs.py) or,
    #    for boards whose BL2 accepts it, regenerated from the OEM key pair.
    nt_fw = str(outdir / "SKY1_BL33_UEFI.fd")
    pregenerated = pkg / "Certs" / "trusted_key_no.crt"
    if open_certs:
        # --open: TF-A cert_create, the trusted key certificate under the
        # CIX root that the source-built BL2 verifies
        fip, _ = trusted.build_bootloader3(outdir / "SKY1_BL33_UEFI.fd", pr / "Keys" / "oem_privatekey.pem",
                                           outdir / "open" / "bl3")
        shutil.copyfile(fip, fw / "bootloader3.img")
        for crt in ("trusted_key_no.crt", "nt_fw_key.crt", "nt_fw_cert.crt"):
            shutil.copyfile(outdir / "open" / "bl3" / "certs" / crt, pr / "certs" / crt)
    else:
        if cfg.get("trusted_key_cert", "regenerate") == "regenerate":
            run(cix_tool("cix_regen_trusted_key_cert", "-p", "Keys/oem_publickey.pem", "-s", "Keys/oem_privatekey.pem",
                         "-o", "certs/trusted_key_no.crt"), cwd=pr, capture=True)
        else:
            if not pregenerated.exists():
                die(f"{cfg['board']}: {pregenerated} missing (run scripts/fetch-blobs.py)")
            shutil.copyfile(pregenerated, pr / "certs" / "trusted_key_no.crt")
        run(cix_tool("cert_uefi_create_rsa", "--key-alg", "rsa", "--key-size", "3072", "--hash-alg", "sha256", "-p",
             "--ntfw-nvctr", "223", "--nt-fw-cert", "certs/nt_fw_cert.crt", "--nt-fw-key-cert", "certs/nt_fw_key.crt",
             "--nt-fw-key", "Keys/oem_privatekey.pem", "--non-trusted-world-key", "Keys/oem_privatekey.pem",
             "--nt-fw", nt_fw), cwd=pr, capture=True)
        run(cix_tool("fiptool", "create", "--trusted-key-cert", "certs/trusted_key_no.crt",
             "--nt-fw-key-cert", "certs/nt_fw_key.crt", "--nt-fw-cert", "certs/nt_fw_cert.crt",
             "--nt-fw", nt_fw, "Firmwares/bootloader3.img"), cwd=pr, capture=True)
    check_certificates(cfg, pr)

    # 6. SPI images, with the open packer (scripts/open/cixfw.py; its output
    #    is byte-identical to cix_package_tool)
    for cfg_name, out, pack in (("spi_flash_config_all.json", "cix_flash_all.bin", cixfw.pack_all),
                                ("spi_flash_config_ota.json", "cix_flash_ota.bin", cixfw.pack_ota)):
        try:
            img = pack(json.loads((pr / cfg_name).read_text()), pr)
        except cixfw.PackError as e:
            die(f"{cfg['board']}: {cfg_name}: {e}")
        (outdir / out).write_bytes(img)


def check_layout(cfg, outdir):
    """Validate the packaged image against its flash configuration."""
    layout = json.loads((outdir / "pr/spi_flash_config_all.json").read_text())
    img = (outdir / "cix_flash_all.bin").read_bytes()
    size = flash_size(cfg)
    # cix_package_tool stops after the last region it writes, so the image
    # may be shorter than the flash part.
    if len(img) > size or int(layout["flash_size"], 0) != size:
        die(f"{cfg['board']}: image is {len(img):#x} bytes, flash is {size:#x}")
    hdr_off = int(layout["firmware_header_addr"], 0)
    magic, _ver, count = struct.unpack_from("<III", img, hdr_off)
    if magic != 0x55AA55AA:
        die(f"{cfg['board']}: no CIX firmware header at {hdr_off:#x}")
    groups = {}
    for g in layout["image_header_groups"]:
        groups[(g["image_type"], int(g["address"], 0))] = (int(g["size"], 0), g["file"])
    regions = []
    for i in range(count):
        t, addr, length, _ = struct.unpack_from("<IIII", img, hdr_off + 16 + 16 * i)
        if (t, addr) not in groups:
            die(f"{cfg['board']}: header entry type {t} at {addr:#x} is not in the layout")
        gsize, gfile = groups[(t, addr)]
        if length > gsize:
            die(f"{cfg['board']}: {gfile} is {length:#x} bytes, region is {gsize:#x}")
        staged = outdir / "pr" / gfile
        # cix_package_tool rewrites a field in the first 16 bytes of each image.
        if staged.exists() and img[addr + 16: addr + length] != staged.read_bytes()[16:length]:
            die(f"{cfg['board']}: {gfile} content mismatch at {addr:#x}")
        regions.append((addr, addr + gsize, IMAGE_TYPES.get(t, str(t)), length))
    regions.sort()
    for a, b in zip(regions, regions[1:]):
        if a[1] > b[0]:
            die(f"{cfg['board']}: regions {a[2]} and {b[2]} overlap")
    check_nv_store(cfg, layout)
    return [{"type": r[2], "address": f"{r[0]:#x}", "region_size": f"{r[1] - r[0]:#x}", "used": f"{r[3]:#x}"}
            for r in regions]


def check_nv_store(cfg, layout):
    """The variable store (variables + FTW working + FTW spare, three times
    SPI_VARIABLE_SIZE from SPI_VARIABLE_BASE) is erased and rewritten at run
    time, so it must sit inside the layout's reserved region."""
    dsc = (ROOT / cfg["package"] / Path(cfg["dsc"]).name).read_text()
    pattern = r"^\s*DEFINE\s+(SPI_VARIABLE_BASE|SPI_VARIABLE_SIZE)\s*=\s*(\S+)"
    defs = dict(re.findall(pattern, dsc, re.M))
    if "Sky1Board.dsc.inc" in dsc:      # defaults of the shared board body
        shared = (ROOT / "edk2-project/Platform/CIX/Sky1/Sky1Board.dsc.inc").read_text()
        defs = {**dict(re.findall(pattern, shared, re.M)), **defs}
    if len(defs) != 2:
        die(f"{cfg['board']}: SPI_VARIABLE_BASE/SIZE not defined in {cfg['dsc']}")
    start = int(defs["SPI_VARIABLE_BASE"], 0)
    end = start + 3 * int(defs["SPI_VARIABLE_SIZE"], 0)
    for g in layout["image_header_groups"]:
        addr, gsize = int(g["address"], 0), int(g["size"], 0)
        if g["image_type"] == 100 and addr <= start and end <= addr + gsize:
            return
    die(f"{cfg['board']}: variable store {start:#x}-{end:#x} is not inside a reserved region of the flash layout")


def write_manifest(cfg, outdir, target, version, date, layout, open_info=None,
                   boot_firmware="vendor"):
    files = {}
    for f in ("cix_flash_all.bin", "cix_flash_ota.bin", "SKY1_BL33_UEFI.fd"):
        p = outdir / f
        files[f] = {"size": p.stat().st_size, "sha256": sha256(p)}
    staged = {p.name: sha256(p) for p in sorted((outdir / "pr/Firmwares").iterdir()) if p.is_file()}
    pins = load_json("manifests/pins.json")
    iasl = run([os.environ.get("IASL_PREFIX", "") + "iasl", "-v"], capture=True, check=False)
    gcc = run([os.environ["GCC5_AARCH64_PREFIX"] + "gcc", "--version"], capture=True, check=False)
    m = {
        "board": cfg["board"],
        "name": cfg["name"],
        "target": target,
        "version": version,
        "build_date": date.isoformat(),
        "commit": git("rev-parse", "HEAD"),
        "dirty": bool(git("status", "--porcelain", "--untracked-files=no", check=False)),
        "submodules": {s: git("rev-parse", "HEAD", cwd=ROOT / s) for s in pins["submodules"]},
        "patches": [str(p.relative_to(ROOT / "patches")) for s in pins["submodules"] for p in sorted(patch_dir(s).glob("*.patch"))],
        "tools": {"gcc": gcc.splitlines()[0] if gcc else "", "iasl": next((l for l in iasl.splitlines() if "version" in l), "")},
        "outputs": files,
        "staged_firmware": staged,
        "flash_layout": layout,
        "boot_firmware": boot_firmware,
        "signing_chain": cfg["signing_chain"],
    }
    if open_info:
        m["open"] = open_info
    (outdir / "manifest.json").write_text(json.dumps(m, indent=2) + "\n")
    return m


FLASH_README = """{name} - edk2-cix {version}
{underline}

UEFI firmware built from https://github.com/{repo} (commit {commit}).
Boot mode: ACPI. Build target: {target}.

Files
  cix_flash_all.bin   complete SPI NOR image, unpadded (wipes UEFI settings)
  cix_flash_ota.bin   BL33/UEFI-only update image
  SKY1_BL33_UEFI.fd   raw UEFI firmware device (for debugging)
  manifest.json       versions, hashes and flash layout of this build
  SHA256SUMS

Flashing
  From the UEFI shell of the running firmware:
      FS0:\\> FlashUpdate.efi -f cix_flash_all.bin
  or write cix_flash_all.bin with an external SPI programmer (1.8 V flash).
  Keep a known-good image and a way to recover (programmer) at hand.

See README.md in the repository for board specific notes and validation
status.
"""


def release_name(cfg, manifest):
    stem = f"{cfg['board']}_{manifest['version']}"
    if manifest.get("boot_firmware", "vendor") != board(cfg["board"]).get("boot_firmware", "vendor"):
        stem += f"_{manifest['boot_firmware']}boot"
    if manifest.get("open"):
        stem += "_open"
    if manifest["target"] != "RELEASE":
        stem += f"_{manifest['target'].lower()}"
    return stem


def write_release_binary(cfg, outdir, manifest):
    """dist/<board>_<version>.bin: cix_flash_all.bin padded to the flash size."""
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    data = (outdir / "cix_flash_all.bin").read_bytes()
    size = flash_size(cfg)
    if len(data) > size:
        die(f"{cfg['board']}: image is larger than the {size:#x} byte flash")
    # Pad with the erased-flash value; the variable stores in the padding are
    # re-initialised by the firmware on first boot.
    data += b"\xff" * (size - len(data))
    path = dist / f"{release_name(cfg, manifest)}.bin"
    path.write_bytes(data)
    manifest["outputs"][path.name] = {"size": len(data), "sha256": sha256(path)}
    (outdir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    log(f"{cfg['board']}: {path.relative_to(ROOT)} ({len(data) >> 20} MiB)")
    return path


def make_extras(cfg, outdir, manifest, repo):
    dist = ROOT / "dist" / "extras"
    dist.mkdir(parents=True, exist_ok=True)
    stem = release_name(cfg, manifest)
    readme = FLASH_README.format(name=cfg["name"], version=manifest["version"],
                                 underline="=" * (len(cfg["name"]) + len(manifest["version"]) + 13),
                                 repo=repo, commit=manifest["commit"][:12], target=manifest["target"],
                                 flash_mb=flash_size(cfg) >> 20)
    sums = "".join(f"{v['sha256']}  {k}\n" for k, v in manifest["outputs"].items() if not k.endswith("_" + manifest["version"] + ".bin"))
    zpath = dist / f"{stem}_extras.zip"
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in ("cix_flash_all.bin", "cix_flash_ota.bin", "SKY1_BL33_UEFI.fd", "manifest.json"):
            z.write(outdir / f, f"{stem}/{f}")
        z.writestr(f"{stem}/SHA256SUMS", sums)
        z.writestr(f"{stem}/README.txt", readme)
    log(f"{cfg['board']}: {zpath.relative_to(ROOT)}")
    return zpath


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("boards", nargs="*")
    ap.add_argument("--all", action="store_true", help="build every board in boards/")
    ap.add_argument("--list", action="store_true", help="list boards and exit")
    ap.add_argument("--target", default="RELEASE", choices=["RELEASE", "DEBUG"])
    ap.add_argument("--version", default=None, help="version string (default: git describe)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--image", action="append", default=[], metavar="KEY=PATH|URL",
                    help="vendor firmware image, passed to fetch-blobs.py")
    ap.add_argument("--skip-prepare", action="store_true")
    ap.add_argument("--extras", action="store_true",
                    help="also pack OTA image, FD and manifest into dist/extras/")
    ap.add_argument("--boot-firmware", choices=["vendor", "cix"],
                    help="BL1/BL2/bootloader2 source (default: boot_firmware in boards/<board>.yaml); "
                         "cix: CIX 2026Q3 for the board's signing chain")
    ap.add_argument("--open", action="store_true",
                    help="build BL2, BL31 and OP-TEE from source (bootloader1 re-signed, bootloader2, "
                         "bootloader3 certificates); chain-A boards only (docs/open-firmware.md); the "
                         "image is named <board>_<version>_open.bin")
    ap.add_argument("--vendor-stmm", action="store_true",
                    help="with --open, embed CIX's StandaloneMM image from the reference bootloader2 "
                         "instead of the one built from source")
    a = ap.parse_args()
    if a.vendor_stmm and not a.open:
        ap.error("--vendor-stmm needs --open")

    all_boards = boards()
    if a.list:
        for k, v in all_boards.items():
            print(f"{k:12} {v['name']}")
        return
    names = list(all_boards) if a.all else a.boards
    if not names:
        ap.error("name at least one board, or use --all / --list")
    for n in names:
        if a.open and not cixfw.CHAINS[board(n)["signing_chain"]]["keys_public"]:
            die(f"--open: {n} is on signing chain {board(n)['signing_chain']}, whose keys are not public; "
                "its BL2 would reject a source-built bootloader2")

    if not a.skip_prepare:
        run([sys.executable, str(ROOT / "scripts/prepare.py")])
    fetch = [sys.executable, str(ROOT / "scripts/fetch-blobs.py"), *names]
    for i in a.image:
        fetch += ["--image", i]
    run(fetch)

    version = a.version or default_version()
    date = source_date()
    commit = git("rev-parse", "--short=12", "HEAD")
    repo = os.environ.get("GITHUB_REPOSITORY", "edk2-cix/edk2-cix")
    for n in names:
        cfg = board(n)
        outdir = ROOT / "output" / n / a.target
        shutil.rmtree(outdir, ignore_errors=True)
        outdir.mkdir(parents=True)
        fd = edk2_build(cfg, a.target, version, date, commit, a.jobs, outdir / "build.log")
        boot_fw = a.boot_firmware or cfg.get("boot_firmware", "vendor")
        bl1, bl2, open_info = (None, None, None)
        if a.open:
            log(f"{n}: building BL2, BL31, OP-TEE and StandaloneMM from source")
            cix_dir = PACKAGE_TOOL / cixfw.CHAINS[cfg["signing_chain"]]["cix_dir"]
            ref = cix_dir / "bootloader2.img" if boot_fw == "cix" else None
            stmm = None if a.vendor_stmm else stmm_build(a.target, version, commit, a.jobs, outdir / "stmm-build.log")
            bl2, open_info = trusted.build_bootloader2(cfg, a.target, outdir / "open", a.jobs, ref, stmm)
            ref_bl1 = (cix_dir if boot_fw == "cix" else ROOT / cfg["package"] / "Firmwares") / "bootloader1.img"
            bl1, bl1_info = trusted.build_bootloader1(cfg, ref_bl1, a.target, outdir / "open", a.jobs)
            open_info.update(bl1_info)
            open_info["bootloader3_certificates"] = "cert_create"
        package(cfg, fd, outdir, boot_fw, bl2, bl1, open_certs=a.open)
        layout = check_layout(cfg, outdir)
        manifest = write_manifest(cfg, outdir, a.target, version, date, layout, open_info, boot_fw)
        log(f"{n}: cix_flash_all.bin {manifest['outputs']['cix_flash_all.bin']['sha256']}")
        write_release_binary(cfg, outdir, manifest)
        if a.extras:
            make_extras(cfg, outdir, manifest, repo)


if __name__ == "__main__":
    main()
