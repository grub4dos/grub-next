# SPDX-License-Identifier: GPL-3.0-or-later
"""Resource archive acceptance on all five targets, including diskless BIOS."""
import json
import struct
from pathlib import Path
import phase1
import phase2
from archive_pack import inspect


def pe_resource(path, expected):
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    count = struct.unpack_from("<H", data, pe+6)[0]
    start = pe+24+struct.unpack_from("<H", data, pe+20)[0]
    found = 0
    for n in range(count):
        off = start+n*40
        if data[off:off+8] != b".bootres":
            continue
        size, _, raw, pos = struct.unpack_from("<IIII", data, off+8)
        flags = struct.unpack_from("<I", data, off+36)[0]
        assert size == len(expected) and raw >= size and flags & 0x40000000 and not flags & 0xa0000000
        assert data[pos:pos+size] == expected
        found += 1
    assert found == 1, path


def main():
    phase1.RESOURCE_INPUT = True
    phase1.COMMON += ["BOOT:PASS:resource-manifest", "BOOT:PASS:resource-config-font"]
    output = phase1.ROOT / "build/phase3"
    phase2.main(output)
    result = output/"results.json"
    report = json.loads(result.read_text())
    result.unlink()
    logical = None
    for target in phase1.phase0.TARGETS:
        data = (output/target/"resource.cpio").read_bytes()
        files = inspect(data)
        common = {n: files[n] for n in ("boot.lua", "fonts/minimal.hex")}
        if logical is None:
            logical = common
        assert common == logical
        for name in ("sample", "failing"):
            assert files[f"modules/{target}/{name}.so"] == (output/target/f"external-sample/build/{name}.so").read_bytes()
        if target.endswith("-efi"):
            pe_resource(output/target/"boot-core.efi", data)
    args = ["-m", "512M", "-cpu", "qemu32,+pae,-lm", "-kernel", output/"i386-pc/boot-linux.bz"]
    report["qemu"]["bios-diskless-embedded"] = phase1.qemu(output, "bios-diskless-embedded",
        "qemu-system-x86_64", args, phase1.COMMON + ["BOOT:PASS:resource-embedded"])
    for name, offset in (("header", 0), ("content", 120)):
        data = bytearray((output/"i386-pc/resource.cpio").read_bytes())
        data[offset] ^= 1
        path = output/f"bad-{name}.cpio"
        path.write_bytes(data)
        key = f"bios-corrupt-{name}"
        report["qemu"][key] = phase1.qemu(output, key, "qemu-system-x86_64",
            args+["-initrd", path], ["BOOT:PANIC:resource-open"], expected_panic="resource-open")
        text = (output/f"{key}.serial.log").read_text()
        assert "BOOT:PASS:module-" not in text and "BOOT:PASS:resource-embedded" not in text
    report["resource_checks"] = {"logical_resources_identical": True,
        "efi_readonly_section": True, "module_bytes_match_sdk": True,
        "diskless_bios": True, "corrupt_input_no_fallback": True}
    result.write_text(json.dumps(report, indent=2, sort_keys=True)+"\n")
    print("PASS: Phase 3 resources, manifest, embedded EFI and diskless BIOS")


if __name__ == "__main__":
    main()
