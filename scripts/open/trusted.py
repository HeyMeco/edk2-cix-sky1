#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Build bootloader2.img (TF-A BL31 + OP-TEE BL32) from source.

For boards on the CIX default signing chain (signing_chain: cix in
boards/<board>.yaml) the FIP root of trust is CIX's published key, so a
FIP built here passes the closed BL2 exactly as the vendor one does:

  1. BL31 from arm-trusted-firmware (PLAT=sky1, SPD=opteed)
  2. OP-TEE from optee_os (PLATFORM=cix-sky1) with the StandaloneMM
     firmware volume taken unchanged from the board's vendor bootloader2
     (CIX's NorFlash/SerialOut/FwVersion StMM drivers are not public yet)
  3. certificates with TF-A cert_create, signed with the published CIX key
     and the NV counter of the vendor FIP; fiptool from TF-A
  4. an independent check of the certificate chain and image hashes

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


def vendor_fip(cfg):
    p = ROOT / cfg["package"] / "Firmwares" / "bootloader2.img"
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


def build_bootloader2(cfg, target="RELEASE", out=None, jobs=None):
    """Build and sign bootloader2.img; returns (path, manifest info)."""
    if cfg.get("signing_chain") != "cix":
        die(f"{cfg['board']}: signing_chain is {cfg.get('signing_chain', 'unset')}; the FIP root key of "
            "this board is not public, so a source-built bootloader2 would not boot")
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
    vfip = vendor_fip(cfg)
    problems, vinfo = cixfw.verify_fip(vfip, cixfw.spki_of(root))
    if problems:
        die(f"{cfg['board']}: vendor bootloader2 does not verify against the CIX root: {problems}")
    nvctr = vinfo["nvctr"]
    tos = {cixfw.FIP_NAMES.get(u, u).split()[0]: vfip[o:o + n] for u, o, n, _ in cixfw.fip_entries(vfip)}["tos-fw"]
    _, _, stmm_fv = cixfw.find_stmm(tos)
    stmm = out / "BL32_AP_EFI_STMM.fd"
    stmm.write_bytes(stmm_fv)
    log(f"  StMM from vendor bootloader2: {len(stmm_fv)} bytes, sha256 {sha256(stmm)[:16]}")

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
        "stmm_from": "vendor bootloader2",
        "nv_counter": nvctr,
        "fip_root": info["root"],
        "bootloader2_sha256": sha256(fip),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("board")
    ap.add_argument("--target", default="RELEASE", choices=["RELEASE", "DEBUG"])
    ap.add_argument("--out")
    ap.add_argument("--jobs", type=int)
    a = ap.parse_args()
    _, info = build_bootloader2(board(a.board), a.target, a.out, a.jobs)
    print(json.dumps(info, indent=2))


if __name__ == "__main__":
    main()
