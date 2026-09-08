# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the pinned reference inputs before migration. --record is explicit."""
import hashlib
import json
import pathlib
import subprocess
import sys
ROOT = pathlib.Path(__file__).resolve().parents[1]
NAMES = ["grub", "grub4dos", "ntloader", "wimboot", "libffi", "lvgl",
         "lua-5.5.1", "ipxe", "osloader", "syslinux-6.04-pre1"]
def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args]).decode().strip()
def snapshot():
    records = {}
    for name in NAMES:
        path = ROOT / "ref" / name
        if not path.is_dir():
            raise SystemExit(f"missing reference: {path}")
        entry = {}
        if (path / ".git").exists():
            entry = {"commit": git(path, "rev-parse", "HEAD"),
                     "tree": git(path, "rev-parse", "HEAD^{tree}"),
                     "remote": git(path, "remote", "get-url", "origin")}
            if git(path, "status", "--porcelain", "--untracked-files=no"):
                raise SystemExit(f"modified tracked reference files: {name}")
            raw = subprocess.check_output(["git", "-C", str(path), "ls-files", "-z"])
            paths = [path / p.decode() for p in raw.split(b"\0") if p]
        else:
            paths = sorted(p for p in path.rglob("*") if p.is_file())
        digest = hashlib.sha256()
        count = 0
        for file in sorted(paths):
            if file.is_dir():  # pinned gitlink is recorded by the Git tree
                continue
            data = file.readlink().as_posix().encode() if file.is_symlink() else file.read_bytes()
            digest.update(file.relative_to(path).as_posix().encode() + b"\0")
            digest.update(hashlib.sha256(data).digest())
            count += 1
        entry.update(files=count, sha256=digest.hexdigest())
        records[name] = entry
    return records
if __name__ == "__main__":
    lock = ROOT / "references.lock.json"
    result = snapshot()
    if sys.argv[1:] == ["--record"]:
        lock.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    elif sys.argv[1:]:
        raise SystemExit("usage: check_references.py [--record]")
    elif result != json.loads(lock.read_text()):
        raise SystemExit("reference mismatch; audit changes before updating the lock")
    else:
        print("PASS: all reference hashes match")
