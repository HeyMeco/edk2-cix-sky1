#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Fetch and verify the CIX debug firmware builds pinned in
manifests/cix-debug-firmware.json into the blob cache; print a path.

  cix_debug.py [FILE]      (default: debug/se_fw/se_fw.elf)
"""

import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from common import CACHE, die, load_json, sha256  # noqa: E402


def fetch(name="debug/se_fw/se_fw.elf"):
    m = load_json("manifests/cix-debug-firmware.json")
    info = m["files"].get(name)
    if info is None:
        die(f"{name} is not in manifests/cix-debug-firmware.json")
    repo = m["url"].removeprefix("https://github.com/")
    path = CACHE / "git" / repo / m["commit"] / name
    if not (path.exists() and sha256(path) == info["sha256"]):
        path.parent.mkdir(parents=True, exist_ok=True)
        url = f"https://raw.githubusercontent.com/{repo}/{m['commit']}/{name}"
        with urllib.request.urlopen(url, timeout=120) as r, open(path, "wb") as f:
            f.write(r.read())
        if sha256(path) != info["sha256"]:
            path.unlink()
            die(f"{name}: SHA-256 does not match the manifest")
    return path


if __name__ == "__main__":
    print(fetch(*sys.argv[1:2]))
