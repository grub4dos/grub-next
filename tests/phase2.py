# SPDX-License-Identifier: GPL-3.0-or-later
"""Phase 2: external SDK reproducibility and actual module execution on five targets."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import phase1


def main():
    phase1.OUTPUT = phase1.ROOT / "build/phase2"
    phase1.COMMON += ["BOOT:PASS:module-sdk-execution",
                      "BOOT:PASS:module-duplicate-rollback-freeze",
                      "BOOT:PASS:module-api-call"]
    phase1.main()
    result = phase1.OUTPUT / "results.json"
    report = json.loads(result.read_text())
    result.unlink()  # Never publish a passing report if SDK validation fails.
    report["sdk"] = {}
    for target in phase1.phase0.TARGETS[1:]:
        installed = phase1.OUTPUT / target / "external-sdk"
        original = phase1.OUTPUT / target / "external-sample/build"
        arch, ident, flags = {
            "i386-pc": ("i386", 1, "-march=pentium4 -msse2 -mfpmath=sse"),
            "i386-efi": ("i386", 2, "-march=pentium4 -msse2 -mfpmath=sse"),
            "x86_64-efi": ("x86_64", 3, "-march=x86-64 -mno-red-zone"),
            "arm64-efi": ("aarch64", 4, "-march=armv8-a"),
            "loongarch64-efi": ("loongarch64", 5, "-march=loongarch64 -mabi=lp64d"),
        }[target]
        with tempfile.TemporaryDirectory(prefix="boot-external-sdk-") as tmp:
            root = Path(tmp)
            shutil.copytree(installed, root / "sdk")
            shutil.copytree(root / "sdk/sample", root / "source")
            phase1.phase0.run("cmake", "-S", root / "source", "-B", root / "build", "-G", "Ninja",
                              f"-DBOOT_SDK={root / 'sdk'}", f"-DBOOT_MODULE_TARGET_ID={ident}",
                              "-DCMAKE_SYSTEM_NAME=Generic", "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
                              "-DCMAKE_C_COMPILER=clang", f"-DCMAKE_C_COMPILER_TARGET={arch}-none-elf",
                              f"-DCMAKE_C_FLAGS={flags}", "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld")
            phase1.phase0.run("cmake", "--build", root / "build")
            for name in ("sample", "failing"):
                data = (root / "build" / f"{name}.so").read_bytes()
                assert data == (original / f"{name}.so").read_bytes(), f"{target}/{name}: not reproducible"
                symbols = subprocess.check_output(["readelf", "--dyn-syms", "--wide",
                                                   str(original / f"{name}.so")], text=True)
                (phase1.OUTPUT / f"{target}-{name}.symbols.log").write_text(symbols)
                for line in symbols.splitlines():
                    fields = line.split()
                    if "UND" in fields:
                        assert fields[0] == "0:", line
                report["sdk"][f"{target}/{name}"] = {"sha256": hashlib.sha256(data).hexdigest(),
                    "external_build_identical": True, "undefined_imports": 0}
    result.write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: Phase 2 external SDK and firmware module execution")


if __name__ == "__main__":
    main()
