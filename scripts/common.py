# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Shared helpers for the edk2-cix build scripts."""

import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOARDS_DIR = ROOT / "boards"
CACHE = Path(os.environ.get("EDK2CIX_CACHE", ROOT / "blobs-cache"))


def log(msg):
    print(f"[edk2-cix] {msg}", flush=True)


def die(msg):
    print(f"[edk2-cix] error: {msg}", file=sys.stderr, flush=True)
    sys.exit(1)


def run(cmd, cwd=None, env=None, capture=False, check=True):
    res = subprocess.run(cmd, cwd=cwd, env=env, text=True,
                         stdout=subprocess.PIPE if capture else None,
                         stderr=subprocess.STDOUT if capture else None)
    if check and res.returncode != 0:
        if capture:
            sys.stderr.write(res.stdout or "")
        die(f"command failed ({res.returncode}): {' '.join(map(str, cmd))}")
    return res.stdout if capture else res.returncode


def git(*args, cwd=ROOT, check=True):
    return run(["git", *args], cwd=cwd, capture=True, check=check).strip()


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load_json(path):
    with open(ROOT / path) as f:
        return json.load(f)


def _load_yaml(path):
    try:
        import yaml
    except ImportError:
        die("PyYAML is required (it is part of the build container)")
    with open(path) as f:
        return yaml.safe_load(f)


def boards():
    """Return {board_id: config} for every board in boards/."""
    out = {}
    for p in sorted(BOARDS_DIR.glob("*.yaml")):
        cfg = _load_yaml(p)
        out[cfg["board"]] = cfg
    return out


def board(name):
    all_boards = boards()
    if name not in all_boards:
        die(f"unknown board '{name}' (known: {', '.join(all_boards)})")
    return all_boards[name]


def host_arch():
    m = os.uname().machine
    return "AARCH64" if m in ("aarch64", "arm64") else "X86_64"
