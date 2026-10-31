#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Stage the vendor binaries a board needs and verify their SHA-256.

Vendor binaries are not stored in this repository.  Each board lists them
in manifests/blobs/<board>.json:

  local         a file stored in this repository (vendor/)
  git           a file from a public vendor git repository, fetched by
                commit from raw.githubusercontent.com
  vendor-image  a byte range of a vendor firmware image
  vendor-acpi   an ACPI table from a vendor firmware image
  deb-image     a byte range of a file inside a released vendor .deb package

Vendor images (manifests/vendor-images.json) are looked up, in order, from
--image KEY=PATH|URL, the environment variable named in the manifest (a path
or an http(s) URL) and blobs-cache/vendor/<file>.

The Orange Pi blobs and the MS-R1 BL1/BL2 are kept under vendor/; each
records in "origin" where it was cut from the vendor image.  --from-vendor re-extracts
them from the images (see manifests/vendor-images.json) and rewrites vendor/.

Usage: fetch-blobs.py BOARD... | --all  [--from-vendor --image KEY=FILE]
"""

import argparse
import io
import lzma
import os
import tarfile
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "extract"))
from common import CACHE, ROOT, board, boards, die, load_json, log, sha256  # noqa: E402
import uefi  # noqa: E402


def download(url, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    log(f"downloading {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "edk2-cix-fetch"})
    with urllib.request.urlopen(req, timeout=300) as r, open(tmp, "wb") as f:
        shutil.copyfileobj(r, f)
    tmp.replace(dest)


def verify(path, size, digest, what):
    if path.stat().st_size != size or sha256(path) != digest:
        die(f"{what}: {path} does not match the manifest "
            f"(size {path.stat().st_size} vs {size}, sha256 {sha256(path)} vs {digest})")


class VendorImages:
    def __init__(self, overrides):
        self.manifest = load_json("manifests/vendor-images.json")
        self.overrides = overrides
        self.cache = {}

    def _locate(self, key, info):
        src = self.overrides.get(key) or os.environ.get(info["env"], "")
        cached = CACHE / "vendor" / info["file"]
        if src.startswith(("http://", "https://")):
            if not (cached.exists() and sha256(cached) == info["sha256"]):
                download(src, cached)
            return cached
        if src:
            p = Path(src).expanduser()
            if not p.exists():
                die(f"vendor image {key}: {p} does not exist")
            return p
        if cached.exists():
            return cached
        die(f"vendor image '{key}' ({info['description']}) is required.\n"
            f"  Provide it with --image {key}=PATH or set {info['env']} to a path or URL,\n"
            f"  or copy {info['file']} to {cached}.\n"
            f"  Expected SHA-256: {info['sha256']}")

    def image(self, key):
        """Return the bytes of the SPI flash image for *key*."""
        if key in self.cache:
            return self.cache[key]
        info = self.manifest.get(key) or die(f"unknown vendor image '{key}'")
        path = self._locate(key, info)
        verify(path, info["size"], info["sha256"], f"vendor image {key}")
        c = info.get("container")
        if c:
            inner = CACHE / "vendor" / f"{key}-{Path(c['path']).name}"
            if not (inner.exists() and sha256(inner) == c["sha256"]):
                if c["type"] != "fat":
                    die(f"unsupported container {c['type']}")
                inner.parent.mkdir(parents=True, exist_ok=True)
                offset = int(c["offset"], 0)
                subprocess.run(["mcopy", "-n", "-o", "-i", f"{path}@@{offset}",
                                f"::/{c['path']}", str(inner)], check=True)
            verify(inner, c["size"], c["sha256"], f"{key}:{c['path']}")
            path = inner
        data = path.read_bytes()
        self.cache[key] = data
        return data


def deb_member(deb, member):
    """Return the bytes of *member* from the data archive of a .deb file."""
    data = deb.read_bytes()
    if not data.startswith(b"!<arch>\n"):
        die(f"{deb} is not a Debian package")
    pos = 8
    while pos < len(data):
        name = data[pos:pos + 16].decode().strip().rstrip("/")
        size = int(data[pos + 48:pos + 58].decode().strip())
        body = data[pos + 60:pos + 60 + size]
        if name.startswith("data.tar"):
            raw = lzma.decompress(body) if name.endswith(".xz") else body
            with tarfile.open(fileobj=io.BytesIO(raw)) as t:
                for m in t.getmembers():
                    if m.name.lstrip("./") == member:
                        return t.extractfile(m).read()
            break
        pos += 60 + size + (size & 1)
    die(f"{member} not found in {deb}")


def acpi_table(image, ffs_guid, signature):
    fip = uefi.find_fip(image, 0x100000)
    fd = uefi.find_fd(image, fip if fip >= 0 else 0)
    for f in uefi.parse_fv(image[fd:]):
        if f.guid != ffs_guid.upper():
            continue
        for s in f.walk_sections({uefi.SEC_RAW}):
            if s.data[:4] == signature.encode():
                return s.data[: struct.unpack_from("<I", s.data, 4)[0]]
    die(f"ACPI table {signature} not found in FFS {ffs_guid}")


def fetch_board(name, images, from_vendor=False):
    cfg = board(name)
    pkg = ROOT / cfg["package"]
    manifest = load_json(cfg["blobs"])
    for b in manifest["blobs"]:
        src = b["source"]
        if from_vendor and src["type"] == "local" and "origin" in b:
            # regenerate the in-repo copy from the vendor image
            dest = ROOT / src["path"]
            src = b["origin"]
        else:
            dest = pkg / b["dest"]
            if dest.exists() and dest.stat().st_size == b["size"] and sha256(dest) == b["sha256"]:
                continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        if src["type"] == "local":
            local = ROOT / src["path"]
            verify(local, b["size"], b["sha256"], b["dest"])
            shutil.copyfile(local, dest)
        elif src["type"] == "git":
            owner_repo = src["url"].removeprefix("https://github.com/")
            cached = CACHE / "git" / owner_repo / src["commit"] / src["path"]
            if not (cached.exists() and sha256(cached) == b["sha256"]):
                download(f"https://raw.githubusercontent.com/{owner_repo}/{src['commit']}/{src['path']}", cached)
            verify(cached, b["size"], b["sha256"], b["dest"])
            shutil.copyfile(cached, dest)
        elif src["type"] == "vendor-image":
            img = images.image(src["image"])
            off = int(src["offset"], 0)
            with tempfile.NamedTemporaryFile(dir=dest.parent, delete=False) as t:
                t.write(img[off: off + b["size"]])
            Path(t.name).replace(dest)
        elif src["type"] == "deb-image":
            deb = CACHE / "deb" / Path(src["url"]).name
            if not (deb.exists() and sha256(deb) == src["sha256"]):
                download(src["url"], deb)
            if sha256(deb) != src["sha256"]:
                die(f"{deb}: SHA-256 does not match the manifest")
            off = int(src["offset"], 0)
            dest.write_bytes(deb_member(deb, src["member"])[off: off + b["size"]])
        elif src["type"] == "vendor-acpi":
            img = images.image(src["image"])
            dest.write_bytes(acpi_table(img, src["ffs"], src["signature"]))
        else:
            die(f"unknown blob source type {src['type']}")
        verify(dest, b["size"], b["sha256"], b["dest"])
        log(f"{name}: staged {b['dest']}")
    prune_staged(name, pkg, manifest)
    log(f"{name}: {len(manifest['blobs'])} blob(s) verified")


def prune_staged(name, pkg, manifest):
    """Remove files a blob directory holds but the manifest no longer lists,
    so the build does not pick up a blob that was dropped."""
    wanted = {(pkg / b["dest"]).resolve() for b in manifest["blobs"]}
    for d in {w.parent for w in wanted}:
        ignore = d / ".gitignore"
        if not (ignore.exists() and "Staged by scripts/fetch-blobs.py" in ignore.read_text()):
            continue
        for f in d.iterdir():
            if f.is_file() and f.name != ".gitignore" and f.resolve() not in wanted:
                f.unlink()
                log(f"{name}: removed {f.relative_to(pkg)} (no longer in the manifest)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("boards", nargs="*")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--image", action="append", default=[], metavar="KEY=PATH|URL")
    ap.add_argument("--from-vendor", action="store_true",
                    help="re-extract in-repo vendor blobs from the vendor images")
    a = ap.parse_args()
    names = list(boards()) if a.all else a.boards
    if not names:
        ap.error("name at least one board or use --all")
    overrides = dict(i.split("=", 1) for i in a.image)
    images = VendorImages(overrides)
    for n in names:
        fetch_board(n, images, a.from_vendor)


if __name__ == "__main__":
    main()
