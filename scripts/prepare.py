#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Prepare the source tree for a build.

- checks out the pinned submodule commits (manifests/pins.json)
- hides the CIX copy of Platform/Radxa so the imported one in edk2-project
  is used (sparse checkout; the submodule commit stays unchanged)
- applies the patch queue in patches/<submodule>/

Usage: prepare.py [--check] [--force]
"""

import argparse
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import ROOT, die, git, load_json, log  # noqa: E402

SPARSE = {"edk2-platforms": ["/*", "!Platform/Radxa"]}


def patch_files(sub):
    return sorted((ROOT / "patches" / sub).glob("*.patch"))


def patch_stamp(sub):
    h = hashlib.sha256()
    for p in patch_files(sub):
        h.update(p.name.encode())
        h.update(p.read_bytes())
    return h.hexdigest()


def stamp_path(sub):
    gitdir = git("rev-parse", "--git-dir", cwd=ROOT / sub)
    return (ROOT / sub / gitdir).resolve() / "edk2-cix-patches"


def ensure_checkout(sub, commit, check):
    path = ROOT / sub
    if not (path / ".git").exists():
        if check:
            die(f"{sub} is not checked out; run scripts/prepare.py")
        log(f"initialising submodule {sub}")
        git("submodule", "update", "--init", sub)
    head = git("rev-parse", "HEAD", cwd=path)
    if head != commit:
        if check:
            die(f"{sub} is at {head}, expected {commit}")
        log(f"checking out {sub} at {commit[:12]}")
        git("fetch", "--quiet", "origin", commit, cwd=path, check=False)
        git("checkout", "--quiet", "--force", commit, cwd=path)
        stamp_path(sub).unlink(missing_ok=True)


def ensure_sparse(sub, check):
    path = ROOT / sub
    patterns = SPARSE.get(sub)
    if not patterns:
        return
    current = git("sparse-checkout", "list", cwd=path, check=False).split("\n")
    if current != patterns:
        if check:
            die(f"{sub}: sparse checkout is not configured")
        log(f"{sub}: hiding {', '.join(p[1:] for p in patterns if p.startswith('!'))}")
        git("sparse-checkout", "set", "--no-cone", *patterns, cwd=path)
    for p in patterns:
        if p.startswith("!") and (path / p[1:]).exists():
            die(f"{sub}/{p[1:]} still exists after sparse checkout")


def ensure_patches(sub, check, force):
    path = ROOT / sub
    files = patch_files(sub)
    stamp = stamp_path(sub)
    want = patch_stamp(sub)
    if not force and stamp.exists() and stamp.read_text().strip() == want:
        return
    if check:
        die(f"{sub}: patch queue is not applied; run scripts/prepare.py")
    if git("status", "--porcelain", "--untracked-files=no", cwd=path):
        log(f"{sub}: discarding local changes before applying the patch queue")
    git("checkout", "--quiet", "--force", "HEAD", "--", ".", cwd=path)
    git("clean", "-fdq", cwd=path)
    for p in files:
        log(f"{sub}: applying {p.name}")
        git("apply", "--whitespace=nowarn", str(p), cwd=path)
    stamp.write_text(want + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="verify only, change nothing")
    ap.add_argument("--force", action="store_true", help="re-apply patches even if the stamp matches")
    a = ap.parse_args()

    pins = load_json("manifests/pins.json")["submodules"]
    for sub, commit in pins.items():
        ensure_checkout(sub, commit, a.check)
    if not a.check and not (ROOT / "edk2/MdeModulePkg/Library/BrotliCustomDecompressLib/brotli/c").exists():
        log("initialising edk2 submodules")
        git("submodule", "update", "--init", "--recursive", cwd=ROOT / "edk2")
    for sub in pins:
        ensure_sparse(sub, a.check)
        ensure_patches(sub, a.check, a.force)
    log("source tree ready" if not a.check else "source tree matches pins and patch queue")


if __name__ == "__main__":
    main()
