# SPDX-License-Identifier: GPL-3.0-or-later
"""Build all configurations twice and boot the Phase 0 x86 probes (Linux host)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import tempfile
import time
ROOT = Path(__file__).resolve().parents[1]
TARGETS = ["host", "i386-pc", "i386-efi", "x86_64-efi", "arm64-efi", "loongarch64-efi"]
ARTIFACTS = {"host": ["boot-info", "boot-log-test"], "i386-pc": ["boot-stage2.elf"],
             "x86_64-efi": ["boot-hello.efi", "boot-efi-test.efi"]}
def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)
def build(source, dest, target):
    options = ["-DCMAKE_BUILD_TYPE=Release"]
    if target == "host":
        options += ["-DCMAKE_C_COMPILER=clang"]
    else:
        options += [f"-DCMAKE_TOOLCHAIN_FILE={source}/cmake/toolchains/{target}.cmake"]
    run("cmake", "--fresh", "-S", source, "-B", dest, "-G", "Ninja", *options)
    run("cmake", "--build", dest)
    if target == "host":
        run("ctest", "--test-dir", dest, "--output-on-failure")
def inspect(target, path):
    data = path.read_bytes()
    if target == "i386-pc":
        if data[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 3:
            raise RuntimeError("BIOS artifact is not little-endian ELF32 i386")
    elif target == "x86_64-efi":
        pe = struct.unpack_from("<I", data, 0x3c)[0]
        if data[pe:pe+4] != b"PE\0\0":
            raise RuntimeError("missing PE signature")
        if struct.unpack_from("<H", data, pe+4)[0] != 0x8664:
            raise RuntimeError("wrong EFI machine")
        if struct.unpack_from("<I", data, pe+8)[0] != 0:
            raise RuntimeError("nonzero PE timestamp")
        opt = pe + 24
        if struct.unpack_from("<H", data, opt)[0] != 0x20b:
            raise RuntimeError("EFI image must be PE32+")
        if struct.unpack_from("<H", data, opt+68)[0] != 10:
            raise RuntimeError("wrong EFI subsystem")
        import_rva, import_size = struct.unpack_from("<II", data, opt+120)
        if (import_rva, import_size) != (0, 0):
            # GNU ld emits an all-zero import terminator even without DLL imports.
            count = struct.unpack_from("<H", data, pe+6)[0]
            sections = opt + struct.unpack_from("<H", data, pe+20)[0]
            empty = False
            for index in range(count):
                section = sections + index * 40
                rva, raw_size, offset = struct.unpack_from("<III", data, section+12)
                relative = import_rva - rva
                if relative >= 0 and import_size >= 20 and relative + import_size <= raw_size:
                    contents = data[offset+relative:offset+relative+import_size]
                    empty = len(contents) == import_size and not any(contents)
                    break
            if not empty:
                raise RuntimeError("unexpected system DLL imports")
        if not all(struct.unpack_from("<II", data, opt+152)):
            raise RuntimeError("missing EFI base relocations")
def qemu(output, name, args, markers, *, negative=False):
    serial = output / f"{name}.serial.log"
    debug = output / f"{name}.debug.log"
    stderr = output / f"{name}.qemu.log"
    # Remove old logs so an earlier success cannot satisfy this run.
    serial.write_bytes(b"")
    debug.write_bytes(b"")
    cmd = ["qemu-system-x86_64", "-accel", "tcg", "-m", "128", "-display", "none",
           "-monitor", "none", "-no-reboot", "-serial", f"file:{serial}",
           "-debugcon", f"file:{debug}", *map(str, args)]
    with stderr.open("wb") as err:
        process = subprocess.Popen(cmd, stdout=err, stderr=err)
        try:
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                text = (debug if negative else serial).read_text(errors="replace")
                if all(marker in text for marker in markers):
                    if negative:
                        if "BOOT:HELLO" in text:
                            raise RuntimeError(f"{name}: unsupported CPU entered C")
                    elif "BOOT:FAIL" in text:
                        raise RuntimeError(f"{name}: failure marker: {text}")
                    if not all(marker in debug.read_text(errors="replace") for marker in markers):
                        raise RuntimeError(f"{name}: debugcon mirror mismatch")
                    print(f"PASS: {name}", flush=True)
                    return
                if process.poll() is not None:
                    break
                time.sleep(0.1)
            raise RuntimeError(f"{name}: missing markers; inspect {output}")
        finally:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/phase0")
    parser.add_argument("--no-qemu", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "results.json").unlink(missing_ok=True)
    os.environ.setdefault("SOURCE_DATE_EPOCH", "1704067200")
    report = {"source_date_epoch": os.environ["SOURCE_DATE_EPOCH"], "sha256": {}}
    with tempfile.TemporaryDirectory(prefix="boot-repro-") as temp:
        other = Path(temp) / "different-source-path"
        shutil.copytree(ROOT, other, ignore=shutil.ignore_patterns(".git", "ref", "build*", "__pycache__"))
        for target in TARGETS:
            first, second = output / target, Path(temp) / "second" / target
            build(ROOT, first, target)
            build(other, second, target)
            for name in ARTIFACTS.get(target, ["libboot-runtime.a"]):
                inspect(target, first / name)
                a, b = (first / name).read_bytes(), (second / name).read_bytes()
                if a != b:
                    raise RuntimeError(f"not reproducible: {target}/{name}")
                report["sha256"][f"{target}/{name}"] = hashlib.sha256(a).hexdigest()
        print("PASS: six configurations and relocated-source reproducibility", flush=True)
    run("grub-file", "--is-x86-multiboot2", output / "i386-pc/boot-stage2.elf")
    if not args.no_qemu:
        iso_root = output / "iso-root"
        (iso_root / "boot/grub").mkdir(parents=True, exist_ok=True)
        shutil.copyfile(output / "i386-pc/boot-stage2.elf", iso_root / "boot/stage2.elf")
        (iso_root / "boot/grub/grub.cfg").write_text(
            'set timeout=0\nset default=0\nmenuentry "Phase 0" {\n'
            ' multiboot2 /boot/stage2.elf\n boot\n}\n')
        iso = output / "bios.iso"
        run("grub-mkrescue", "-o", iso, iso_root, stdout=subprocess.DEVNULL)
        qemu(output, "bios", ["-cdrom", iso, "-boot", "d", "-cpu", "qemu32"],
             ["BOOT:PASS:fp-state", "BOOT:HELLO:i386-pc:multiboot2"])
        for cpu, name in [("pentium3", "no-sse2"), ("486", "cpu486"),
                          ("qemu32,-fxsr", "no-fxsr"), ("qemu32,-sse", "no-sse")]:
            qemu(output, name, ["-cdrom", iso, "-boot", "d", "-cpu", cpu],
                 ["BOOT:FAIL:cpu-sse2-required"], negative=True)
        esp = output / "esp/EFI/BOOT"
        esp.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(output / "x86_64-efi/boot-efi-test.efi", esp / "BOOTX64.EFI")
        firmware = Path(os.environ.get("OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd"))
        variables = Path(os.environ.get("OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd"))
        private_vars = output / "OVMF_VARS.fd"
        shutil.copyfile(variables, private_vars)
        qemu(output, "efi", ["-drive", f"if=pflash,format=raw,readonly=on,file={firmware}",
             "-drive", f"if=pflash,format=raw,file={private_vars}",
             "-drive", f"format=raw,file=fat:rw:{output / 'esp'}"],
             ["BOOT:PASS:fp-state", "BOOT:PASS:invalid-pe-rejected", "BOOT:PASS:LoadImage",
              "BOOT:HELLO:x86_64-efi", "BOOT:PASS:StartImage"])
        report["qemu"] = "BIOS positive + four CPU rejection paths + EFI LoadImage/StartImage"
    (output / "results.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"PASS: results written to {output / 'results.json'}")
if __name__ == "__main__":
    main()
