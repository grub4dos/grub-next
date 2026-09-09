# SPDX-License-Identifier: GPL-3.0-or-later
"""Wrap the CMake-linked flat i386 payload in a Linux 2.10 boot header."""
import struct
import sys
from pathlib import Path
payload = Path(sys.argv[1]).read_bytes()
# BSS is zero-filled by this deterministic wrapper; no second runtime build.
setup = bytearray(2560)
setup[0x1f1] = 4
struct.pack_into("<I", setup, 0x1f4, (len(payload)+15)//16)
struct.pack_into("<H", setup, 0x1fe, 0xaa55)
setup[0x200:0x202] = b"\xeb\x66"
setup[0x202:0x206] = b"HdrS"
struct.pack_into("<H", setup, 0x206, 0x020a)
setup[0x210] = 0xff
setup[0x211] = 1
struct.pack_into("<I", setup, 0x214, 0x100000)
struct.pack_into("<I", setup, 0x22c, 0x37ffffff)
struct.pack_into("<I", setup, 0x230, 0x100000)
struct.pack_into("<I", setup, 0x238, 2048)
struct.pack_into("<Q", setup, 0x258, 0x100000)
struct.pack_into("<I", setup, 0x260, max(0x200000, len(payload)))
real_setup = Path(sys.argv[3]).read_bytes()
if len(real_setup) > len(setup)-0x268:
    raise ValueError("Linux setup exceeds setup_sects")
setup[0x268:0x268+len(real_setup)] = real_setup
Path(sys.argv[2]).write_bytes(setup+payload)
