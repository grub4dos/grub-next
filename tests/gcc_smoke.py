# SPDX-License-Identifier: GPL-3.0-or-later
"""Optional x86 GCC/binutils compatibility smoke test; run on Ubuntu."""
import os
from pathlib import Path
import shutil
from phase0 import ROOT, run, qemu, inspect
out = ROOT / "build/gcc-smoke"
for target in ["i386-pc", "x86_64-efi"]:
    run("cmake", "-S", ROOT, "-B", out / target, "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/toolchains/{target}-gcc.cmake",
        "-DCMAKE_BUILD_TYPE=Release")
    run("cmake", "--build", out / target)
iso_root = out / "iso-root"
(iso_root / "boot/grub").mkdir(parents=True, exist_ok=True)
shutil.copyfile(out / "i386-pc/boot-stage2.elf", iso_root / "boot/stage2.elf")
(iso_root / "boot/grub/grub.cfg").write_text(
    'set timeout=0\nmenuentry "GCC" { multiboot2 /boot/stage2.elf; boot; }\n')
run("grub-file", "--is-x86-multiboot2", out / "i386-pc/boot-stage2.elf")
run("grub-mkrescue", "-o", out / "bios.iso", iso_root)
qemu(out, "gcc-bios", ["-cdrom", out / "bios.iso", "-boot", "d", "-cpu", "qemu32"],
     ["BOOT:PASS:fp-state", "BOOT:HELLO:i386-pc:multiboot2"])
esp = out / "esp/EFI/BOOT"
esp.mkdir(parents=True, exist_ok=True)
shutil.copyfile(out / "x86_64-efi/boot-efi-test.efi", esp / "BOOTX64.EFI")
code = os.environ.get("OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd")
shutil.copyfile(os.environ.get("OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd"), out / "vars.fd")
for name in ["boot-hello.efi", "boot-efi-test.efi"]:
    inspect("x86_64-efi", out / "x86_64-efi" / name)
qemu(out, "gcc-efi", ["-drive", f"if=pflash,format=raw,readonly=on,file={code}",
     "-drive", f"if=pflash,format=raw,file={out / 'vars.fd'}",
     "-drive", f"format=raw,file=fat:rw:{out / 'esp'}"],
     ["BOOT:PASS:fp-state", "BOOT:PASS:invalid-pe-rejected", "BOOT:PASS:LoadImage",
      "BOOT:HELLO:x86_64-efi", "BOOT:PASS:StartImage"])

# Exercise the same Phase 1 product paths with GCC-produced images.
from phase1 import qemu as qemu_core, COMMON
shutil.copyfile(out/"i386-pc/boot-core.elf", iso_root/"boot/core.elf")
(iso_root/"boot/grub/grub.cfg").write_text(
    'set timeout=0\nmenuentry "GCC core" { multiboot2 /boot/core.elf; boot; }\n')
run("grub-mkrescue", "-o", out/"core.iso", iso_root)
qemu_core(out, "gcc-core-bios", "qemu-system-x86_64",
    ["-m","6G","-cpu","qemu32,+pae,-lm","-cdrom",out/"core.iso","-boot","d"],
    COMMON+["BOOT:PASS:context:multiboot2","BOOT:PASS:high-pae-copy-hash-fp",
            "BOOT:PASS:resident-int15-e820"])
qemu_core(out, "gcc-core-linux", "qemu-system-x86_64",
    ["-m","6G","-cpu","qemu32,+pae,-lm","-kernel",out/"i386-pc/boot-linux.bz"],
    COMMON+["BOOT:PASS:context:linux","BOOT:PASS:high-pae-copy-hash-fp",
            "BOOT:PASS:resident-int15-e820"])
assert "BOOT:PASS:linux-real-setup" in (out/"gcc-core-linux.debug.log").read_text()
shutil.copyfile(out/"x86_64-efi/boot-core.efi", esp/"BOOTX64.EFI")
qemu_core(out, "gcc-core-efi", "qemu-system-x86_64",
    ["-m","256M","-drive",f"if=pflash,format=raw,readonly=on,file={code}",
     "-drive",f"if=pflash,format=raw,file={out/'vars.fd'}",
     "-drive",f"format=raw,file=fat:rw:{out/'esp'}"],
    COMMON+["BOOT:PASS:efi-context:x86_64-efi","BOOT:PASS:efi-firmware-reservations"])
