#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Track board packages imported from vendor edk2-platforms trees.

The Radxa and Minisforum board packages under edk2-project/ started as file
snapshots of the vendor trees.  manifests/imports.json records, per file,
the upstream blob and whether this tree changed it.

  imports.py --write     fetch the upstream commits and regenerate the manifest
  imports.py --check     verify the manifest still describes the tree
  imports.py --diff NAME show the local changes against upstream for one import
"""

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import ROOT, die, git, log  # noqa: E402

IMPORTS = [
    {
        "name": "radxa",
        "repo": "https://github.com/radxa/edk2-platforms",
        "branch": "cix_p1_dev_2026q2",
        "commit": "14cf7bdcfe1d0e8635a16a25d4209b6e28b18fca",
        "upstream": "Platform/Radxa",
        "local": "edk2-project/Platform/Radxa",
    },
    {
        "name": "minisforum",
        "repo": "https://github.com/minisforum-cix-p1-repo/cix_opensource__release__edk2-platforms",
        "branch": "a0fb5/5cf6e/cix_p1_mg_mp_dev",
        "commit": "119ae9cc84abade22a48081ec3aa11eb8ee6e74a",
        "upstream": "Platform/CIX/Sky1/MGP1WSB",
        "local": "edk2-project/Platform/CIX/Sky1/MGP1WSB",
    },
]
OBJ_REPO = ROOT / "edk2-platforms"
MANIFEST = ROOT / "manifests/imports.json"
IGNORED = {".gitignore"}


def have(commit):
    return subprocess.run(["git", "cat-file", "-e", f"{commit}^{{commit}}"], cwd=OBJ_REPO,
                          capture_output=True).returncode == 0


def fetch(imp):
    if not have(imp["commit"]):
        log(f"fetching {imp['repo']} {imp['commit'][:12]}")
        git("fetch", "--quiet", "--no-tags", imp["repo"], imp["commit"], cwd=OBJ_REPO)


def upstream_files(imp):
    out = {}
    for line in git("ls-tree", "-r", f"{imp['commit']}:{imp['upstream']}", cwd=OBJ_REPO).splitlines():
        meta, path = line.split("\t", 1)
        out[path] = meta.split()[2]
    return out


def blob_sha(path):
    data = path.read_bytes()
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def local_files(imp):
    base = ROOT / imp["local"]
    return {str(p.relative_to(base)): p for p in sorted(base.rglob("*"))
            if p.is_file() and p.name not in IGNORED and "Firmwares" not in p.parts and "Certs" not in p.parts}


def status(imp):
    up = upstream_files(imp)
    loc = local_files(imp)
    files = {}
    for path in sorted(set(up) | set(loc)):
        if Path(path).name in IGNORED:
            continue
        if path not in loc:
            st = "removed"
        elif path not in up:
            st = "added"
        else:
            st = "same" if blob_sha(loc[path]) == up[path] else "modified"
        files[path] = {"upstream_blob": up.get(path), "status": st}
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--write", action="store_true")
    g.add_argument("--check", action="store_true")
    g.add_argument("--diff", metavar="NAME")
    a = ap.parse_args()

    if a.diff:
        imp = next((i for i in IMPORTS if i["name"] == a.diff), None) or die(f"unknown import {a.diff}")
        fetch(imp)
        for path, info in status(imp).items():
            if info["status"] == "modified":
                up = git("show", f"{imp['commit']}:{imp['upstream']}/{path}", cwd=OBJ_REPO)
                tmp = ROOT / "out" / "imports" / imp["name"] / path
                tmp.parent.mkdir(parents=True, exist_ok=True)
                tmp.write_text(up + "\n")
                subprocess.run(["git", "--no-pager", "diff", "--no-index", "--stat", str(tmp),
                                str(ROOT / imp["local"] / path)])
        return

    if a.write:
        result = []
        for imp in IMPORTS:
            fetch(imp)
            files = status(imp)
            counts = {}
            for f in files.values():
                counts[f["status"]] = counts.get(f["status"], 0) + 1
            result.append({**imp, "summary": counts, "files": files})
            log(f"{imp['name']}: {counts}")
        MANIFEST.write_text(json.dumps(result, indent=2) + "\n")
        return

    recorded = json.loads(MANIFEST.read_text())
    bad = 0
    for imp in recorded:
        loc = local_files(imp)
        for path, info in imp["files"].items():
            if info["status"] == "removed":
                if path in loc:
                    bad += 1
                    print(f"{imp['name']}: {path} exists but is recorded as removed")
                continue
            if path not in loc:
                bad += 1
                print(f"{imp['name']}: {path} is missing")
            elif info["status"] == "same" and blob_sha(loc[path]) != info["upstream_blob"]:
                bad += 1
                print(f"{imp['name']}: {path} changed; run scripts/imports.py --write")
        for path in loc:
            if path not in imp["files"]:
                bad += 1
                print(f"{imp['name']}: {path} is not recorded; run scripts/imports.py --write")
    if bad:
        sys.exit(1)
    log("imports manifest matches the tree")


if __name__ == "__main__":
    main()
