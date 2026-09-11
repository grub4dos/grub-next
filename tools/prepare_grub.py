#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare explicit reviewable bug fixes without changing the vendor snapshot."""
from pathlib import Path
import subprocess
import sys
import tempfile
import shutil
import import_grub

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
# Check every vendor file before producing build inputs. No ref/ dependency.
sys.argv = [sys.argv[0]]
import_grub.main()
with tempfile.TemporaryDirectory(prefix="boot-grub-") as directory:
    work = Path(directory)
    shutil.copytree(root / "vendor/grub/grub-core", work / "grub-core")
    for patch in sorted((root / "patches/grub").glob("*.patch")):
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)], cwd=work, check=True)
    for source in (work / "grub-core").rglob("*.c"):
        target = out / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        data = source.read_bytes()
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
