# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare pinned LoongArch test firmware and QEMU host emulator in build/ only."""
import bz2
import hashlib
from pathlib import Path
import subprocess
import tarfile
import tempfile
import uuid
import urllib.request
ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT/"build"
def fetch(url, path, digest):
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url, timeout=120) as response:
            path.write_bytes(response.read())
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f"hash mismatch: {path}; remove the failed download and retry")
def qemu_source_filter(member, destination):
    # Unused by our softmmu build. Keep data_filter for every other member.
    if (member.name == "qemu-9.2.2/roms/edk2/EmulatorPkg/Unix/Host/X11IncludeHack"
            and member.issym() and member.linkname == "/opt/X11/include"):
        return None
    return tarfile.data_filter(member, destination)

def extract_source(archive, source):
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    marker = source/".boot-source-complete"
    if marker.is_file() and marker.read_text() == digest:
        return
    # Publish only a fully extracted tree. An interrupted/older tree is retained
    # for inspection, never used as proof that extraction completed.
    with tempfile.TemporaryDirectory(prefix="qemu-extract-", dir=source.parent) as temporary:
        staging = Path(temporary)
        with tarfile.open(archive) as tar:
            tar.extractall(staging, filter=qemu_source_filter)
        extracted = staging/source.name
        if not (extracted/"configure").is_file():
            raise RuntimeError("QEMU archive has no configure script")
        (extracted/marker.name).write_text(digest)
        if source.exists():
            backup = source.with_name(source.name+".previous-"+uuid.uuid4().hex)
            source.rename(backup)
            print(f"Retained previous source: {backup}")
        extracted.rename(source)

def main():
    firmware = BUILD/"firmware/loongarch.fd.bz2"
    fetch("https://raw.githubusercontent.com/qemu/qemu/v9.2.2/pc-bios/edk2-loongarch64-code.fd.bz2",
          firmware, "4fdedf8501cf970dffaca292f0eddec8f2a6b63c8b7d0c9372940b0eb8d46f4b")
    (firmware.parent/"loongarch.fd").write_bytes(bz2.decompress(firmware.read_bytes()))
    archive = BUILD/"qemu-9.2.2.tar.xz"
    fetch("https://download.qemu.org/qemu-9.2.2.tar.xz", archive,
          "752eaeeb772923a73d536b231e05bcc09c9b1f51690a41ad9973d900e4ec9fbf")
    source = BUILD/"qemu-9.2.2"
    emulator = source/"build/qemu-system-loongarch64"
    if not emulator.exists():
        extract_source(archive, source)
        with (BUILD/"qemu-configure.log").open("w") as log:
            subprocess.run(["./configure", "--target-list=loongarch64-softmmu",
                "--disable-docs", "--disable-werror", "--disable-gtk", "--disable-sdl",
                "--disable-opengl", "--disable-vnc", "--disable-tools", "--disable-user",
                "--disable-guest-agent", "--disable-slirp"], cwd=source,
                stdout=log, stderr=subprocess.STDOUT, check=True)
        with (BUILD/"qemu-build.log").open("w") as log:
            subprocess.run(["ninja", "-C", "build", "-j8", "qemu-system-loongarch64"],
                cwd=source, stdout=log, stderr=subprocess.STDOUT, check=True)
    version = subprocess.check_output([str(emulator), "--version"], text=True)
    if not version.startswith("QEMU emulator version 9.2.2"):
        raise RuntimeError("unexpected local QEMU version")
    print(f"Ready: {emulator}")
if __name__=="__main__":
    main()
