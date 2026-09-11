#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Import/check byte-identical GRUB sources from the locked local reference.

No source transformations are permitted here. Environment adaptation belongs
in core/grub, not in the imported files. Updating the lock is a separate action.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "vendor/grub"
MANIFEST = DEST / "sources.json"
COMMIT = "2f972128c48b90bf8b63aadffe6d546976e1dee6"
FS = ["fat", "ext2", "iso9660", "ntfs", "ntfscomp", "fshelp"]
DISK = ["loopback", "diskfilter", "lvm", "mdraid_linux", "mdraid_linux_be", "mdraid1x_linux",
        "raid5_recover", "raid6_recover", "dmraid_nvidia", "ldm"]
ACTIVE_DISK = {"loopback", "diskfilter", "lvm", "mdraid1x_linux", "raid5_recover", "raid6_recover"}
HEADERS = ["fshelp", "ntfs", "fat", "datetime", "compiler", "safemath",
           "diskfilter", "list", "lvm"]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--import", dest="import_files", action="store_true")
    args = parser.parse_args()
    if args.import_files:
        subprocess.run(["python3", str(ROOT / "tools/check_references.py")], check=True)
        paths = [(f"grub-core/fs/{name}.c", "phase4") for name in FS]
        paths += [(f"grub-core/disk/{name}.c", "phase5" if name in ACTIVE_DISK else "staged")
                  for name in DISK]
        paths += [(f"include/grub/{name}.h", "header") for name in HEADERS]
        paths += [("grub-core/kern/list.c", "phase5")]
        records = []
        for path, role in paths:
            data = (ROOT / "ref/grub" / path).read_bytes()
            target = DEST / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            records.append(dict(path=path, role=role, sha256=digest(data)))
        MANIFEST.write_text(json.dumps(dict(commit=COMMIT, files=records), indent=2) + "\n")
    manifest = json.loads(MANIFEST.read_text())
    assert manifest["commit"] == COMMIT, "unexpected source revision"
    for item in manifest["files"]:
        assert digest((DEST / item["path"]).read_bytes()) == item["sha256"], item["path"]
    actual = {str(p.relative_to(DEST)) for p in DEST.rglob("*") if p.is_file()}
    assert actual == {item["path"] for item in manifest["files"]} | {"sources.json"}
    print(f"GRUB import: {len(manifest['files'])} byte-identical files; {COMMIT}")


if __name__ == "__main__":
    main()
