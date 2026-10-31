#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Build bootloader2.img (TF-A BL31 + OP-TEE BL32) from source.

For boards on signing chain A (boards/<board>.yaml) the FIP root of trust
is CIX's published key, so a FIP built here passes the closed BL2 exactly as
the vendor one does:

  1. BL31 from arm-trusted-firmware (PLAT=sky1, SPD=opteed)
  2. OP-TEE from optee_os (PLATFORM=cix-sky1) with a StandaloneMM firmware
     volume: the one build.py builds from source
     (edk2-project/Platform/CIX/Sky1/StandaloneMm), or CIX's, taken
     unchanged from the reference bootloader2 (the selected boot firmware:
     the board vendor's or CIX's)
  3. certificates with TF-A cert_create, signed with the published CIX key
     and the NV counter of the vendor FIP; fiptool from TF-A
  4. an independent check of the certificate chain and image hashes

With a reference bootloader1 it also builds BL2 from source and signs it
into bootloader1 (build_bootloader1), and build_bootloader3 certifies BL33
with cert_create under the same root, so no CIX-built code or tool is left
before BL33 except the SE and CSU_PM firmware.

Runs inside the build container.  Used by build.py --open; standalone:

  scripts/open/trusted.py BOARD [--target RELEASE|DEBUG] [--out DIR]
"""

import argparse
import datetime
import json
import os
import shutil
import sys
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
from common import CACHE, ROOT, board, die, git, log, run, sha256  # noqa: E402
import cixfw  # noqa: E402

TFA = ROOT / "arm-trusted-firmware"
OPTEE = ROOT / "optee_os"
CROSS = "aarch64-none-elf-"


def cix_keys():
    """Fetch the published CIX keys (manifests/cix-keys.json), verified."""
    m = json.loads((ROOT / "manifests/cix-keys.json").read_text())
    repo = m["url"].removeprefix("https://github.com/")
    out = {}
    for k in m["keys"]:
        p = CACHE / "git" / repo / m["commit"] / m["path"] / k["name"]
        if not (p.exists() and sha256(p) == k["sha256"]):
            p.parent.mkdir(parents=True, exist_ok=True)
            url = f"https://raw.githubusercontent.com/{repo}/{m['commit']}/{m['path']}/{k['name']}"
            log(f"downloading {url}")
            with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "edk2-cix"}),
                                        timeout=120) as r:
                p.write_bytes(r.read())
        if p.stat().st_size != k["size"] or sha256(p) != k["sha256"]:
            die(f"{p}: does not match manifests/cix-keys.json")
        out[k["name"]] = p
    return out


def vendor_fip(cfg, ref=None):
    p = Path(ref) if ref else ROOT / cfg["package"] / "Firmwares" / "bootloader2.img"
    if not p.exists():
        die(f"{p} missing (run scripts/fetch-blobs.py {cfg['board']})")
    return p.read_bytes()


def build_env():
    """Reproducible builds: TF-A and OP-TEE embed the build time; take it
    from SOURCE_DATE_EPOCH (default: the commit time), as build.py does."""
    env = dict(os.environ)
    env["SOURCE_DATE_EPOCH"] = env.get("SOURCE_DATE_EPOCH") or git("log", "-1", "--format=%ct", check=False) or "0"
    env["TZ"] = "UTC"
    return env


def tfa_timestamp(env):
    t = datetime.datetime.fromtimestamp(int(env["SOURCE_DATE_EPOCH"]), tz=datetime.timezone.utc)
    return '"' + t.strftime("%H:%M:%S, %b %d %Y") + '"'


def build_bl31(out, debug, jobs):
    env = build_env()
    run(["make", "-C", str(TFA), f"-j{jobs}", "PLAT=sky1", "SPD=opteed", f"DEBUG={int(debug)}",
         f"CROSS_COMPILE={CROSS}", f"BUILD_BASE={out}", f"BUILD_MESSAGE_TIMESTAMP={tfa_timestamp(env)}",
         "bl31"], env=env, capture=True)
    return out / "sky1" / ("debug" if debug else "release") / "bl31.bin"


def build_bl2(out, debug, jobs, root_key):
    """TF-A BL2 (plat/cix BL2 support from the patch queue) with trusted
    board boot against root_key."""
    env = build_env()
    run(["make", "-C", str(TFA), f"-j{jobs}", "PLAT=sky1", "SPD=opteed", f"DEBUG={int(debug)}",
         "TRUSTED_BOARD_BOOT=1", f"MBEDTLS_DIR={ROOT / 'mbedtls'}", f"ROT_KEY={root_key}",
         f"CROSS_COMPILE={CROSS}", f"BUILD_BASE={out}", f"BUILD_MESSAGE_TIMESTAMP={tfa_timestamp(env)}",
         "bl2"], env=env, capture=True)
    return out / "sky1" / ("debug" if debug else "release") / "bl2.bin"


def build_sfh(out):
    """Sensor fusion hub firmware from firmware/sfh (arm-none-eabi), in
    place of CIX's sfh_fw.bin.  Returns (path, manifest info)."""
    out = Path(out) / "sfh"
    run(["make", "-C", str(ROOT / "firmware/sfh"), f"O={out}"], capture=True)
    path = out / "sfh_fw.bin"
    log(f"  sfh_fw.bin: {path.stat().st_size} bytes from source")
    return path, {"sfh_fw": "source", "sfh_fw_sha256": sha256(path)}


def build_se(out, version):
    """SE firmware from firmware/se, with CIX's DDR driver relinked from
    their debug build (fetched at the commit manifests/cix-debug-firmware.json
    pins).  Returns (path, manifest info)."""
    import cix_debug
    out = Path(out) / "se"
    out.mkdir(parents=True, exist_ok=True)
    elf = cix_debug.fetch()
    island = out / "ddr_island.S"
    run([sys.executable, str(ROOT / "scripts/open/se_ddr_island.py"), str(elf), str(island),
         "--data", "g_soc_pvt_data"], capture=True)
    run(["make", "-C", str(ROOT / "firmware/se"), f"O={out}", f"DDR_ISLAND={island}",
         f"MBEDTLS_DIR={ROOT / 'mbedtls'}", f"SE_FW_VERSION=beta2.0-{version}"], capture=True)
    path = out / "se_fw.bin"
    log(f"  se_fw.bin: {path.stat().st_size} bytes from source (DDR driver: CIX's)")
    return path, {"se_fw": "source, CIX's DDR driver relinked", "se_fw_sha256": sha256(path),
                  "cix_debug_elf_sha256": sha256(elf)}


def build_tools():
    run(["make", "-C", str(TFA), "PLAT=sky1", "certtool", "fiptool"], capture=True)
    return TFA / "tools/cert_create/cert_create", TFA / "tools/fiptool/fiptool"


def build_optee(out, stmm, debug, jobs):
    args = ["make", "-C", str(OPTEE), f"-j{jobs}", "PLATFORM=cix-sky1", "CFG_ARM64_core=y",
            f"CROSS_COMPILE={CROSS}", f"CROSS_COMPILE64={CROSS}", "ta-targets=ta_arm64",
            f"CFG_STMM_PATH={stmm}", f"O={out}"]
    if debug:
        args += ["CFG_TEE_CORE_DEBUG=y", "CFG_TEE_CORE_LOG_LEVEL=3"]
    run(args, env=build_env(), capture=True)
    return out / "core" / "tee-raw.bin"


def build_bootloader2(cfg, target="RELEASE", out=None, jobs=None, ref=None, stmm_fd=None):
    """Build and sign bootloader2.img; returns (path, manifest info).

    ref: the bootloader2 that gives the NV counter, and the StMM payload
    unless stmm_fd names a source-built one (default: the board vendor's,
    staged by fetch-blobs.py)."""
    if not cixfw.CHAINS[cfg["signing_chain"]]["keys_public"]:
        die(f"{cfg['board']}: signing chain {cfg['signing_chain']} has no public keys, so a "
            "source-built bootloader2 would not boot")
    out = Path(out or ROOT / "output" / cfg["board"] / target / "open")
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    jobs = jobs or os.cpu_count()
    debug = target == "DEBUG"
    keys = cix_keys()
    root = keys["cix_privatekey.pem"]

    # The vendor FIP gives the StMM payload and the NV counter, and must
    # itself verify against the CIX root (otherwise the board is not on
    # the CIX chain and the yaml is wrong).
    vfip = vendor_fip(cfg, ref)
    problems, vinfo = cixfw.verify_fip(vfip, cixfw.spki_of(root))
    if problems:
        die(f"{cfg['board']}: vendor bootloader2 does not verify against the CIX root: {problems}")
    nvctr = vinfo["nvctr"]
    tos = {cixfw.FIP_NAMES.get(u, u).split()[0]: vfip[o:o + n] for u, o, n, _ in cixfw.fip_entries(vfip)}["tos-fw"]
    stmm = out / "BL32_AP_EFI_STMM.fd"
    if stmm_fd:
        stmm_fv = Path(stmm_fd).read_bytes()
        stmm.write_bytes(stmm_fv)
        log(f"  StMM from source: {len(stmm_fv)} bytes, sha256 {sha256(stmm)[:16]}")
    else:
        _, _, stmm_fv = cixfw.find_stmm(tos)
        stmm.write_bytes(stmm_fv)
        log(f"  StMM from the reference bootloader2: {len(stmm_fv)} bytes, sha256 {sha256(stmm)[:16]}")

    log("  building TF-A BL31")
    bl31 = build_bl31(out / "tfa", debug, jobs)
    cert_create, fiptool = build_tools()
    log("  building OP-TEE")
    bl32 = build_optee(out / "optee", stmm, debug, jobs)
    if cixfw.find_stmm(bl32.read_bytes())[2] != stmm_fv:
        die("OP-TEE does not embed the expected StandaloneMM image")

    certs = out / "certs"
    certs.mkdir()
    k = str(root)
    run([str(cert_create), "--key-alg", "rsa", "--key-size", "3072", "--hash-alg", "sha256",
         "--tfw-nvctr", str(nvctr),
         "--rot-key", k, "--trusted-world-key", k, "--soc-fw-key", k, "--tos-fw-key", k,
         "--trusted-key-cert", str(certs / "trusted_key.crt"),
         "--soc-fw-key-cert", str(certs / "soc_fw_key.crt"), "--soc-fw-cert", str(certs / "soc_fw_content.crt"),
         "--tos-fw-key-cert", str(certs / "tos_fw_key.crt"), "--tos-fw-cert", str(certs / "tos_fw_content.crt"),
         "--soc-fw", str(bl31), "--tos-fw", str(bl32)], capture=True)
    fip = out / "bootloader2.img"
    run([str(fiptool), "create",
         "--trusted-key-cert", str(certs / "trusted_key.crt"),
         "--soc-fw-key-cert", str(certs / "soc_fw_key.crt"), "--soc-fw-cert", str(certs / "soc_fw_content.crt"),
         "--tos-fw-key-cert", str(certs / "tos_fw_key.crt"), "--tos-fw-cert", str(certs / "tos_fw_content.crt"),
         "--soc-fw", str(bl31), "--tos-fw", str(bl32), str(fip)], capture=True)

    data = fip.read_bytes()
    problems, info = cixfw.verify_fip(data, cixfw.spki_of(root))
    if problems:
        die(f"source-built bootloader2 does not verify: {problems}")
    by = {cixfw.FIP_NAMES.get(u, u).split()[0]: data[o:o + n] for u, o, n, _ in cixfw.fip_entries(data)}
    if by["soc-fw"] != bl31.read_bytes() or by["tos-fw"] != bl32.read_bytes():
        die("FIP payloads differ from the built BL31/BL32")
    log(f"  bootloader2.img: {len(data)} bytes, chain ok (root {info['root']}, NV counter {info['nvctr']})")
    return fip, {
        "bootloader2": "source",
        "bl31_sha256": sha256(bl31),
        "bl32_sha256": sha256(bl32),
        "stmm_sha256": sha256(stmm),
        "stmm_from": "source" if stmm_fd else (str(Path(ref).relative_to(ROOT)) if ref else "vendor bootloader2"),
        "nv_counter": nvctr,
        "fip_root": info["root"],
        "bootloader2_sha256": sha256(fip),
    }


def build_bootloader1(cfg, ref_bl1, target="RELEASE", out=None, jobs=None, se=None):
    """bootloader1 with the source-built BL2 (and, given se, SE firmware) in
    place of CIX's, re-signed with the published chain-A keys; CSU_PM stays
    as it is in ref_bl1.  Returns (path, manifest info)."""
    if not cixfw.CHAINS[cfg["signing_chain"]]["keys_public"]:
        die(f"{cfg['board']}: signing chain {cfg['signing_chain']} has no public keys")
    out = Path(out or ROOT / "output" / cfg["board"] / target / "open")
    out.mkdir(parents=True, exist_ok=True)
    keys = cix_keys()
    key1, key2 = keys["oem_privatekey.pem"], keys["cix_privatekey.pem"]

    ref = cixfw.Bl1(Path(ref_bl1).read_bytes())
    (n1, _), (n2, _) = ref.keys()
    if (cixfw.key_id(n1), cixfw.key_id(n2)) != cixfw.CHAINS["A"]["bl1"]:
        die(f"{ref_bl1}: not signed with the chain-A keys")
    if not all(ok for _, ok in ref.verify()):
        die(f"{ref_bl1}: does not verify")

    log("  building TF-A BL2")
    bl2 = build_bl2(out / "tfa-bl2", target == "DEBUG", jobs or os.cpu_count(), key2)
    payload = bl2.read_bytes()
    slot = [img for img in ref.images() if img[1] == "ap"][0]
    # Keep the container layout of the reference: pad BL2 with zeros to the
    # size of the CIX BL2, so that its signature stays where the boot ROM
    # finds it in every vendor container.  The padding is BL2 .bss, which
    # BL2 clears when it starts.
    size = len(payload)
    if size > slot[3]:
        die(f"BL2 ({size} bytes) is larger than the CIX BL2 ({slot[3]} bytes) it replaces")
    payload += bytes(slot[3] - size)
    ref.replace("ap", payload)
    if se:
        se_payload = Path(se).read_bytes()
        se_slot = [img for img in ref.images() if img[1] == "se"][0]
        if len(se_payload) > se_slot[3]:
            die(f"SE firmware ({len(se_payload)} bytes) is larger than CIX's ({se_slot[3]} bytes)")
        # zero padding to CIX's size: the boot ROM copies it over .bss and the stack
        ref.replace("se", se_payload + bytes(se_slot[3] - len(se_payload)))
        log(f"  bootloader1.img: SE firmware {len(se_payload)} bytes (padded to {se_slot[3]})")
    ref.resign(key1, key2)
    if not all(ok for _, ok in ref.verify()):
        die("re-signed bootloader1 does not verify")
    path = out / "bootloader1.img"
    path.write_bytes(bytes(ref.data))
    log(f"  bootloader1.img: BL2 {size} bytes (padded to {len(payload)}), re-signed")
    return path, {
        "bootloader1": ("CSU_PM from the reference, SE firmware and BL2 from source" if se else
                        "SE and CSU_PM from the reference, BL2 from source"),
        "bootloader1_reference_sha256": sha256(ref_bl1),
        "bl2_sha256": sha256(bl2),
        "bootloader1_sha256": sha256(path),
    }


def build_bootloader3(fd, nt_key, out, nvctr=223):
    """The BL33 FIP with certificates from TF-A cert_create: its trusted key
    certificate (NON_TRUSTED_KEY_CERT_ID) signed with the CIX root, the
    non-trusted world key and the content key nt_key.  Writes the
    certificates to out/certs and returns (FIP path, manifest info)."""
    keys = cix_keys()
    root = keys["cix_privatekey.pem"]
    out = Path(out)
    certs = out / "certs"
    certs.mkdir(parents=True, exist_ok=True)
    cert_create, fiptool = build_tools()
    run([str(cert_create), "--key-alg", "rsa", "--key-size", "3072", "--hash-alg", "sha256",
         "--ntfw-nvctr", str(nvctr), "--tfw-nvctr", "31",
         "--rot-key", str(root), "--non-trusted-world-key", str(nt_key), "--nt-fw-key", str(nt_key),
         "--trusted-key-cert", str(certs / "trusted_key_no.crt"),
         "--nt-fw-key-cert", str(certs / "nt_fw_key.crt"), "--nt-fw-cert", str(certs / "nt_fw_cert.crt"),
         "--nt-fw", str(fd)], capture=True)
    fip = out / "bootloader3.img"
    run([str(fiptool), "create", "--trusted-key-cert", str(certs / "trusted_key_no.crt"),
         "--nt-fw-key-cert", str(certs / "nt_fw_key.crt"), "--nt-fw-cert", str(certs / "nt_fw_cert.crt"),
         "--nt-fw", str(fd), str(fip)], capture=True)
    problems, info = cixfw.verify_fip(fip.read_bytes(), cixfw.spki_of(root))
    if problems:
        die(f"source-built bootloader3 does not verify: {problems}")
    log(f"  bootloader3.img: certificates from cert_create, chain ok (root {info['root']})")
    return fip, {"bootloader3_certificates": "cert_create", "bootloader3_root": info["root"]}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("board")
    ap.add_argument("--target", default="RELEASE", choices=["RELEASE", "DEBUG"])
    ap.add_argument("--out")
    ap.add_argument("--jobs", type=int)
    ap.add_argument("--bl1", help="reference bootloader1: also build bootloader1 with the open BL2")
    a = ap.parse_args()
    cfg = board(a.board)
    _, info = build_bootloader2(cfg, a.target, a.out, a.jobs)
    if a.bl1:
        info.update(build_bootloader1(cfg, a.bl1, a.target, a.out, a.jobs)[1])
    print(json.dumps(info, indent=2))


if __name__ == "__main__":
    main()
