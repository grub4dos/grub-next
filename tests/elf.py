# SPDX-License-Identifier: GPL-3.0-or-later
"""Real linker fixtures for all four ELF architectures; host execution is x64 only."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/elf"


def run(*args):
    result = subprocess.run(list(map(str, args)), cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    with (OUT / "commands.log").open("a") as log:
        log.write("$ " + " ".join(map(str, args)) + "\n" + result.stdout)
    if result.returncode:
        raise RuntimeError(result.stdout)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "results.json").unlink(missing_ok=True)
    (OUT / "commands.log").write_text("")
    run("cmake", "-S", ROOT, "-B", OUT, "-G", "Ninja",
        "-DCMAKE_C_COMPILER=clang", "-DBOOT_SANITIZERS=ON")
    run("cmake", "--build", OUT)
    run("ctest", "--test-dir", OUT, "--output-on-failure")
    results = {"host_execution": "x86_64: entry returns 42 after RELATIVE and BSS",
               "cross_fixtures": {}}
    for arch, machine, flags in [("i386", 3, "-march=pentium4 -msse2 -mfpmath=sse"),
                                 ("x86_64", 62, "-march=x86-64 -mno-red-zone"),
                                 ("aarch64", 183, "-march=armv8-a"),
                                 ("loongarch64", 258, "-march=loongarch64 -mabi=lp64d")]:
        build = OUT / arch
        run("cmake", "-S", ROOT / "tests/elf_fixture", "-B", build, "-G", "Ninja",
            "-DCMAKE_SYSTEM_NAME=Generic", "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
            "-DCMAKE_C_COMPILER=clang", f"-DCMAKE_C_COMPILER_TARGET={arch}-none-elf",
            f"-DCMAKE_C_FLAGS={flags}", "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld")
        run("cmake", "--build", build)
        fixture = build / "elf-fixture.so"
        run(OUT / "boot-elf-test", fixture, machine)
        run("readelf", "-h", "-l", "-d", "-r", fixture)
        results["cross_fixtures"][arch] = {
            "sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(),
            "result": "host load, independent relocation/BSS checks; no cross-ISA execution"}
    (OUT / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print("PASS: ELF host execution, malformed/sanitizers and four architecture fixtures")


if __name__ == "__main__":
    main()
