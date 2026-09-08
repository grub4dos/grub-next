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
