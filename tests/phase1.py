# SPDX-License-Identifier: GPL-3.0-or-later
"""Phase 1 runtime acceptance: all entries, ownership, BIOS PAE/E820 and panic."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import phase0
ROOT = phase0.ROOT
OUTPUT = ROOT / "build/phase1"
RESOURCE_INPUT = False
COMMON = ["BOOT:PASS:firmware-time", "BOOT:PASS:loader-abort-commit",
          "BOOT:PASS:memory-owners", "BOOT:PASS:memory-log", "BOOT:PANIC:phase1-reset"]
def qemu(output, name, executable, args, markers, timeout=90, expected_panic="phase1-reset"):
    serial = output/f"{name}.serial.log"
    serial.write_bytes(b"")
    cmd = [executable, "-accel", "tcg", "-display", "none", "-monitor", "none",
           "-no-reboot", "-serial", f"file:{serial}", *map(str, args)]
    debug = output/f"{name}.debug.log"
    if executable == "qemu-system-x86_64":
        debug.write_bytes(b"")
        cmd += ["-debugcon", f"file:{debug}"]
    with (output/f"{name}.qemu.log").open("wb") as log:
        p = subprocess.Popen(cmd, stdout=log, stderr=log)
        try:
            deadline = time.monotonic()+timeout
            while time.monotonic() < deadline:
                text = serial.read_text(errors="replace")
                if p.poll() is not None:
                    if p.returncode != 0 or not all(m in text for m in markers):
                        raise RuntimeError(f"{name}: exited {p.returncode}, missing markers; {text[-2000:]}")
                    if "BOOT:PANIC:" in text.replace(f"BOOT:PANIC:{expected_panic}", "") or "BOOT:FAIL" in text:
                        raise RuntimeError(f"{name}: failure marker")
                    print(f"PASS: {name} including reset", flush=True)
                    return {"markers": markers, "exit_code": p.returncode, "command": cmd}
                time.sleep(.1)
            raise RuntimeError(f"{name}: timeout; {serial.read_text(errors='replace')[-2000:]}")
        finally:
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
def bios_iso(output, linux=False):
    root = output/("linux-root" if linux else "mb-root")
    (root/"boot/grub").mkdir(parents=True, exist_ok=True)
    name = "boot-linux.bz" if linux else "boot-core.elf"
    shutil.copyfile(output/"i386-pc"/name, root/"boot"/name)
    resource = ""
    if RESOURCE_INPUT:
        shutil.copyfile(output/"i386-pc/resource.cpio", root/"boot/resource.cpio")
        resource = ("initrd" if linux else "module2") + " /boot/resource.cpio\n"
    (root/"boot/grub/grub.cfg").write_text(
        'set timeout=0\nset default=0\nmenuentry "Phase1" {\n '+
        ("linux" if linux else "multiboot2")+f" /boot/{name}\n {resource} boot\n}}\n")
    iso = output/("linux.iso" if linux else "mb.iso")
    phase0.run("grub-mkrescue", "-o", iso, root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return iso
def inspect(path, machine):
    data = path.read_bytes()
    assert data[:2]==b"MZ"
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    assert data[pe:pe+4]==b"PE\0\0"
    assert struct.unpack_from("<H", data, pe+4)[0]==machine
    assert struct.unpack_from("<I", data, pe+8)[0]==0
    opt = pe+24
    assert struct.unpack_from("<H", data, opt+68)[0]==10
    base = 96 if machine==0x14c else 112
    assert struct.unpack_from("<II", data, opt+base+8)==(0,0)
    assert all(struct.unpack_from("<II", data, opt+base+40))
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--only", choices=["bios", "efi", "i386-efi", "x86_64-efi", "arm64-efi", "loongarch64-efi"])
    parser.add_argument("--no-repro", action="store_true")
    args = parser.parse_args()
    output = OUTPUT
    output.mkdir(parents=True, exist_ok=True)
    (output/"results.json").unlink(missing_ok=True)
    report = {"qemu": {}, "sha256": {}, "source_date_epoch": os.environ.setdefault("SOURCE_DATE_EPOCH", "1704067200")}
    if not args.skip_build:
        for target in phase0.TARGETS:
            phase0.build(ROOT, output/target, target)
    artifacts = {"host":["boot-core-test"], "i386-pc":["boot-core.elf", "boot-linux.bz"]}
    for target in phase0.TARGETS[2:]:
        artifacts[target] = ["boot-core.efi"]
    if RESOURCE_INPUT:
        for names in artifacts.values():
            names += ["resource.cpio", "resource.manifest.sha256"]
    for target, names in artifacts.items():
        for name in names:
            p = output/target/name
            report["sha256"][f"{target}/{name}"] = hashlib.sha256(p.read_bytes()).hexdigest()
    if not args.no_repro:
        with tempfile.TemporaryDirectory(prefix="boot-phase1-repro-") as temporary:
            source = Path(temporary)/"source"
            shutil.copytree(ROOT, source, ignore=shutil.ignore_patterns(".git", "ref", "build*", "__pycache__"))
            for target in phase0.TARGETS:
                dest = Path(temporary)/target
                phase0.build(source, dest, target)
                for name in artifacts[target]:
                    if (dest/name).read_bytes() != (output/target/name).read_bytes():
                        raise RuntimeError(f"not reproducible: {target}/{name}")
        report["reproducible"] = True
    if args.only in (None, "bios"):
        iso = bios_iso(output)
        for name, cpu, ram, high_marker in [
            ("bios-pae-no-lm", "qemu32,+pae,-lm", "6G", "BOOT:PASS:high-pae-copy-hash-fp"),
            ("bios-no-pae", "qemu32,-pae,-lm", "512M", "BOOT:PASS:low-copy-hash-fp"),
            ("bios-low-ram", "qemu32", "128M", "BOOT:PASS:no-high-ram")]:
            report["qemu"][name] = qemu(output, name, "qemu-system-x86_64",
                ["-m",ram,"-cpu",cpu,"-cdrom",iso,"-boot","d"],
                COMMON+["BOOT:PASS:context:multiboot2",high_marker,"BOOT:PASS:resident-int15-e820"]+
                (["BOOT:PASS:resource-multiboot2"] if RESOURCE_INPUT else []))
        iso = bios_iso(output, True)
        report["qemu"]["bios-linux"] = qemu(output, "bios-linux", "qemu-system-x86_64",
            ["-m","6G","-cpu","qemu32,+pae,-lm","-cdrom",iso,"-boot","d"],
            COMMON+["BOOT:PASS:context:linux","BOOT:PASS:high-pae-copy-hash-fp","BOOT:PASS:resident-int15-e820"]+
            (["BOOT:PASS:resource-linux-initrd"] if RESOURCE_INPUT else []))
        report["qemu"]["bios-linux-real"] = qemu(output, "bios-linux-real", "qemu-system-x86_64",
            ["-m","6G","-cpu","qemu32,+pae,-lm","-kernel",output/"i386-pc/boot-linux.bz"]+
            (["-initrd", output/"i386-pc/resource.cpio"] if RESOURCE_INPUT else []),
            COMMON+["BOOT:PASS:context:linux","BOOT:PASS:high-pae-copy-hash-fp","BOOT:PASS:resident-int15-e820"]+
            (["BOOT:PASS:resource-linux-initrd"] if RESOURCE_INPUT else []))
        if "BOOT:PASS:linux-real-setup" not in (output/"bios-linux-real.debug.log").read_text():
            raise RuntimeError("Linux real-mode setup was not executed")
    if args.only != "bios":
        for target, machine, fallback in [
            ("i386-efi",0x14c,"BOOTIA32.EFI"), ("x86_64-efi",0x8664,"BOOTX64.EFI"),
            ("arm64-efi",0xaa64,"BOOTAA64.EFI"), ("loongarch64-efi",0x6264,"BOOTLOONGARCH64.EFI")]:
            if args.only not in (None, "efi", target):
                continue
            image = output/target/"boot-core.efi"
            inspect(image, machine)
            esp = output/f"esp-{target}"
            (esp/"EFI/BOOT").mkdir(parents=True, exist_ok=True)
            shutil.copyfile(image, esp/"EFI/BOOT"/fallback)
            if target in ("i386-efi", "x86_64-efi"):
                prefix = "/usr/share/OVMF/OVMF32" if target=="i386-efi" else "/usr/share/OVMF/OVMF"
                code, variables = Path(prefix+"_CODE_4M.fd"), Path(prefix+"_VARS_4M.fd")
                exe, extra = "qemu-system-x86_64", ["-m","256M"]
            elif target=="arm64-efi":
                code, variables = Path("/usr/share/AAVMF/AAVMF_CODE.fd"), Path("/usr/share/AAVMF/AAVMF_VARS.fd")
                exe, extra = "qemu-system-aarch64", ["-M","virt","-cpu","cortex-a57","-m","512M"]
            else:
                code = Path(os.environ.get("LOONGARCH_EFI", ROOT/"build/firmware/loongarch.fd"))
                variables = None
                exe, extra = os.environ.get("QEMU_LOONGARCH", str(ROOT/"build/qemu-9.2.2/build/qemu-system-loongarch64")), ["-M","virt","-m","2G"]
            report.setdefault("firmware_sha256", {})[target] = hashlib.sha256(code.read_bytes()).hexdigest()
            if variables:
                private = output/f"{target}-vars.fd"
                shutil.copyfile(variables, private)
                extra += ["-drive",f"if=pflash,format=raw,readonly=on,file={code}",
                          "-drive",f"if=pflash,format=raw,file={private}"]
            else:
                extra += ["-bios",code]
            extra += ["-drive",f"if=none,id=esp,format=raw,file=fat:rw:{esp}",
                      "-device","virtio-blk-pci,drive=esp"]
            report["qemu"][target] = qemu(output, target, exe, extra,
                COMMON+[f"BOOT:PASS:efi-context:{target}", "BOOT:PASS:fp-state",
                        "BOOT:PASS:efi-bs-rt-loader-types", "BOOT:PASS:efi-firmware-reservations"])
    (output/"results.json").write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
if __name__=="__main__":
    main()
