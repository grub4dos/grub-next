# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic build-time ELF64-to-PE32+ layout for LoongArch64.
Only static R_LARCH_RELATIVE relocations are accepted, translated to PE DIR64.
This is a build tool; firmware remains the only runtime PE loader.
"""
import struct
import sys
from pathlib import Path
data = Path(sys.argv[1]).read_bytes()
if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 258:
    raise ValueError("expected ELF64 LoongArch")
entry = struct.unpack_from("<Q", data, 24)[0]
shoff = struct.unpack_from("<Q", data, 40)[0]
shsize, shnum = struct.unpack_from("<HH", data, 58)
sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff+i*shsize) for i in range(shnum)]
alloc = [s for s in sections if s[2]&2 and s[5]]
end = max(s[3]+s[5] for s in alloc)
align = lambda x, a: (x+a-1)&~(a-1)
body = bytearray(align(end-0x1000, 512))
for s in alloc:
    if s[1] != 8:
        body[s[3]-0x1000:s[3]-0x1000+s[5]] = data[s[4]:s[4]+s[5]]
relocations = {}
for s in sections:
    if s[1] == 4:
        for off in range(s[4], s[4]+s[5], 24):
            address, info, addend = struct.unpack_from("<QQq", data, off)
            if info != 3 or address < 0x1000 or address+8 > end:
                raise ValueError(f"unsupported LoongArch relocation {info}")
            struct.pack_into("<Q", body, address-0x1000, addend)
            relocations.setdefault(address&~4095, []).append(0xa000|(address&4095))
reloc = bytearray()
for page, entries in sorted(relocations.items()):
    entries.sort()
    if len(entries)&1:
        entries.append(0)
    reloc += struct.pack("<II", page, 8+len(entries)*2)
    reloc += struct.pack("<"+"H"*len(entries), *entries)
# Empty relocation block still permits position-independent images to relocate.
if not reloc:
    reloc = bytearray(struct.pack("<IIHH", 0x1000, 12, 0, 0))
rva = align(end, 4096)
# Keep the resource archive in its own read-only PE section, just as the
# native COFF linkers do for the other EFI targets.
strings = sections[struct.unpack_from("<H", data, 62)[0]]
names = data[strings[4]:strings[4]+strings[5]]
resource = next(s for s in alloc if names[s[0]:].split(b"\0", 1)[0] == b".bootres")
res_start, res_size = resource[3], resource[5]
tail = align(res_start + res_size, 4096)
assert res_start % 4096 == 0 and tail < end
payloads = [
    (b".core", 0x1000, res_start-0x1000, 0xe0000060),
    (b".bootres", res_start, res_size, 0x40000040),
    (b".data", tail, end-tail, 0xc0000040),
]
header = bytearray(1024)
header[:2] = b"MZ"
struct.pack_into("<I", header, 0x3c, 0x80)
header[0x80:0x84] = b"PE\0\0"
struct.pack_into("<HHIIIHH", header, 0x84, 0x6264, 4, 0, 0, 0, 240, 0x22)
opt = 0x98
struct.pack_into("<H", header, opt, 0x20b)
struct.pack_into("<III", header, opt+4, align(res_start-0x1000,512),
                 sum(align(length,512) for _, _, length, _ in payloads)+align(len(reloc),512), 0)
struct.pack_into("<IIQ", header, opt+16, entry, 0x1000, 0)
struct.pack_into("<II", header, opt+32, 4096, 512)
struct.pack_into("<II", header, opt+56, align(rva+len(reloc),4096), len(header))
struct.pack_into("<HH", header, opt+68, 10, 0x40)
struct.pack_into("<QQQQ", header, opt+72, 0x100000, 0x1000, 0x100000, 0x1000)
struct.pack_into("<I", header, opt+108, 16)
struct.pack_into("<II", header, opt+112+5*8, rva, len(reloc))
def section(off, name, virtual_size, address, raw_size, raw_offset, flags):
    header[off:off+8] = name.ljust(8,b"\0")
    struct.pack_into("<IIIIIIHHI", header, off+8, virtual_size, address, raw_size,
                     raw_offset, 0, 0, 0, 0, flags)
raw = bytearray()
for i, (name, address, length, flags) in enumerate(payloads):
    chunk = body[address-0x1000:address-0x1000+length]
    chunk += bytes(-len(chunk) % 512)
    section(opt+240+i*40, name, length, address, len(chunk), len(header)+len(raw), flags)
    raw += chunk
section(opt+360, b".reloc", len(reloc), rva, align(len(reloc),512), len(header)+len(raw), 0x42000040)
Path(sys.argv[2]).write_bytes(header+raw+reloc+bytes(align(len(reloc),512)-len(reloc)))
