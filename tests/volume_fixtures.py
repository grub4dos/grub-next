# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic MD 1.x/LVM2 metadata and mkfs/mtools-created file payloads."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
from storage_fixtures import blank, pattern, run


def fat(path, files, size=8*1024*1024):
    blank(path, size)
    run('mkfs.fat', '-F', '16', '-s', '1' if size < 16*1024*1024 else '4', '-i', '12345678', path)
    for name, source in files.items():
        run('mcopy', '-i', path, source, '::/'+name)


def md(path, payload, level, role, members, minor=2, missing=False):
    chunk = 8*512
    if level == 0:
        data = b''.join(payload[i:i+chunk] for i in range(role*chunk, len(payload), members*chunk))
    elif level in (5, 6):
        # left asymmetric: parity rotates backwards; RAID6 Q follows P.
        disks = [bytearray() for _ in range(members)]
        def mul(a, b):
            v = 0
            while b:
                if b & 1: v ^= a
                a <<= 1
                if a & 256: a ^= 0x11d
                b >>= 1
            return v
        for stripe in range((len(payload)+chunk*(members-(2 if level==6 else 1))-1)//(chunk*(members-(2 if level==6 else 1)))):
            parity = members - 1 - stripe % members
            qdisk = (parity + 1) % members
            indices = [i for i in range(members) if i != parity and (level != 6 or i != qdisk)]
            rows = []
            for j, index in enumerate(indices):
                at = (stripe*len(indices)+j)*chunk
                row = payload[at:at+chunk].ljust(chunk, b'\0')
                rows.append(row); disks[index] += row
            p = bytearray(chunk)
            for row in rows:
                for j, v in enumerate(row): p[j] ^= v
            disks[parity] += p
            if level == 6:
                q = bytearray(chunk)
                # Q coefficients follow positions starting after the parity pair.
                order = [(qdisk + 1 + i) % members for i in range(members - 2)]
                for power, index in enumerate(order):
                    row = bytes(disks[index][-chunk:])
                    coefficient = 1
                    for _ in range(power): coefficient = mul(coefficient, 2)
                    for j, v in enumerate(row): q[j] ^= mul(v, coefficient)
                disks[qdisk] += q
        data = bytes(disks[role])
    else:
        data = payload
    total = ((len(data)+1048576+8191)//4096)*4096
    offset = 2048 if minor else 0
    sector = {0: (total//512-16)&~7, 1: 0, 2: 8}[minor]
    sb = bytearray(512)
    struct.pack_into('<II', sb, 0, 0xa92b4efc, 1)
    sb[16:32] = bytes(range(16))
    sb[32:40] = b'fixture\0'
    struct.pack_into('<IIQII', sb, 72, level, 0, len(data)//512, 8, members)
    struct.pack_into('<QQQ', sb, 128, offset, len(data)//512, sector)
    struct.pack_into('<I', sb, 160, role)
    struct.pack_into('<I', sb, 220, members)
    for i in range(members): struct.pack_into('<H', sb, 256+2*i, i)
    # Linux MD superblock checksum (little endian 32-bit sum folded to 32 bits).
    end = 256 + 2*members
    words = end//4
    checksum = sum(struct.unpack_from('<'+'I'*words, sb))
    if end % 4: checksum += struct.unpack_from('<H', sb, words*4)[0]
    checksum = (checksum & 0xffffffff) + (checksum >> 32)
    struct.pack_into('<I', sb, 216, checksum & 0xffffffff)
    blank(path, total)
    with path.open('r+b') as f:
        f.seek(offset*512); f.write(data)
        f.seek(sector*512); f.write(sb)


def crc(data):
    value = 0xf597a6cf
    for byte in data:
        value ^= byte
        for _ in range(8): value = (value >> 1) ^ (0xedb88320 if value & 1 else 0)
    return value


def lvm(path, payload, cycle=False):
    ident = 'abcdef-1234-5678-90ab-cdef-1234-567890'
    count = len(payload)//4096
    # Two logical segments stored in reverse physical order.
    half = count//2
    metadata = f'''fixture {{
id = "123456-1234-1234-1234-1234-1234-123456"
seqno = 1
status = ["RESIZEABLE", "READ", "WRITE"]
extent_size = 8
physical_volumes {{
pv0 {{
id = "{ident}"
status = ["ALLOCATABLE"]
pe_start = 2048
pe_count = {count}
}}
}}
logical_volumes {{
data {{
id = "654321-1234-1234-1234-1234-1234-654321"
status = ["READ", "WRITE", "VISIBLE"]
segment_count = 2
segment1 {{
start_extent = 0
extent_count = {half}
type = "striped"
stripe_count = 1
stripes = ["pv0", {half}]
}}
segment2 {{
start_extent = {half}
extent_count = {count-half}
type = "striped"
stripe_count = 1
stripes = ["pv0", 0]
}}
}}
}}
}}
'''.encode()+b'\0'
    if cycle:
        start = metadata.index(b'data {\n')
        lv = metadata[start:-5]
        metadata = (metadata[:start] + lv.replace(b'"pv0",', b'"peer",') +
                    lv.replace(b'data {\n', b'peer {\n').replace(b'"pv0",', b'"data",') +
                    metadata[-5:])
    total = len(payload)+1048576
    label = bytearray(512)
    struct.pack_into('<8sQII8s', label, 0, b'LABELONE', 1, 0, 32, b'LVM2 001')
    label[32:64] = ident.replace('-', '').encode()
    struct.pack_into('<Q', label, 64, total)
    struct.pack_into('<QQ', label, 72, 1048576, 0)
    struct.pack_into('<QQ', label, 104, 4096, 1044480)
    struct.pack_into('<I', label, 16, crc(label[20:]))
    header = bytearray(512)
    struct.pack_into('<16sIQQ', header, 4, b' LVM2 x[5A%r0N*>', 1, 4096, 1044480)
    struct.pack_into('<QQI', header, 40, 512, len(metadata), crc(metadata))
    struct.pack_into('<I', header, 0, crc(header[4:]))
    blank(path, total)
    with path.open('r+b') as f:
        f.seek(512); f.write(label)
        f.seek(4096); f.write(header); f.write(metadata)
        f.seek(1048576); f.write(payload[half*4096:]); f.write(payload[:half*4096])


def generate(out):
    out.mkdir(parents=True, exist_ok=True)
    (out/'probe.bin').write_bytes(pattern(131209))
    fat(out/'inner.img', {'probe.bin': out/'probe.bin'})
    fat(out/'outer.img', {'inner.img': out/'inner.img'}, 32*1024*1024)
    fat(out/'nested.img', {'inner.img': out/'outer.img'}, 64*1024*1024)
    payload = (out/'inner.img').read_bytes()
    for level, members in ((0, 2), (1, 2), (5, 3)):
        for role in range(members):
            md(out/f'md{level}-{role}.img', payload, level, role, members)
    for minor in (0, 1): md(out/f'md1-v1{minor}.img', payload, 1, 0, 2, minor)
    lvm(out/'lvm.img', payload)
    lvm(out/'lvm-cycle.img', payload, cycle=True)
    md(out/'lvm-on-md.img', (out/'lvm.img').read_bytes(), 1, 0, 2)
    pv = (out/'lvm.img').read_bytes()
    header = bytearray(1048576)
    header[446+4] = 0x8e
    struct.pack_into('<II', header, 446+8, 2048, len(pv)//512)
    header[510:512] = b'\x55\xaa'
    (out/'lvm-mbr.img').write_bytes(header+pv)
    fat(out/'lvm-loop.img', {'inner.img': out/'lvm-mbr.img'}, 32*1024*1024)
    report = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.img')}
    (out/'hashes.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


def verify(out, binary):
    expected = (out/'probe.bin').read_bytes()
    cases = {'loop':['outer.img'], 'nested':['nested.img'],
             'lvm/fixture-data':['lvm.img'], 'loop-lvm':['lvm-loop.img']}
    result = {}
    scenarios = [(k,k,v) for k,v in cases.items()]
    scenarios += [(f'md{level}', 'md/fixture', [f'md{level}-{r}.img' for r in range(n)])
                  for level,n in ((0,2),(1,2),(5,3))]
    scenarios += [('md1-degraded','md/fixture',['md1-1.img']),
                  ('md5-degraded','md/fixture',['md5-0.img','md5-2.img']),
                  ('md1-v10','md/fixture',['md1-v10.img']),
                  ('md1-v11','md/fixture',['md1-v11.img']),
                  ('lvm-on-md','lvm/fixture-data',['lvm-on-md.img'])]
    for block in (512, 2048, 4096):
        for label,name,images in scenarios:
            p = subprocess.run([str(binary), str(block), name, '/probe.bin',
                                *[str(out/i) for i in images]], capture_output=True, timeout=60)
            assert p.returncode == 0, (label, block, p.stderr.decode(errors='replace'))
            assert p.stdout == expected, (label, block, len(p.stdout))
            result[f'{label}-{block}'] = hashlib.sha256(p.stdout).hexdigest()
    return result


if __name__ == '__main__':
    import sys
    root = Path(__file__).resolve().parents[1]
    out = root/'build/volumes/fixtures'
    generate(out)
    print(json.dumps(verify(out, root/'build/p5/boot-volume-test'), indent=2))
