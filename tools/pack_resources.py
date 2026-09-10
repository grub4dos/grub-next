# SPDX-License-Identifier: GPL-3.0-or-later
"""Pack canonical newc resources; no host metadata or directory traversal."""
import argparse
import hashlib
from pathlib import Path
import re

MAX_SIZE = 16 * 1024 * 1024


def valid_name(name):
    return (len(name.encode()) <= 255 and name != "TRAILER!!!" and
            all(re.fullmatch(r"[A-Za-z0-9_.-]+", part) and part not in (".", "..")
                for part in name.split("/")))


def record(name, data, ino=1, mode=0o100644):
    encoded = name.encode("ascii") + b"\0"
    values = [ino, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(encoded), 0]
    header = b"070701" + "".join(f"{v:08x}" for v in values).encode()
    item = header + encoded
    item += bytes(-len(item) % 4)
    item += data
    return item + bytes(-len(item) % 4)


def pack(files):
    if "manifest.sha256" in files or not all(valid_name(n) for n in files):
        raise ValueError("invalid or reserved resource path")
    if len(files) > 127:
        raise ValueError("too many resources")
    entries = dict(files)
    entries["manifest.sha256"] = b"".join(
        f"{hashlib.sha256(data).hexdigest()}  {name}\n".encode()
        for name, data in sorted(files.items()))
    data = b"".join(record(name, body, i) for i, (name, body) in enumerate(sorted(entries.items()), 1))
    data += record("TRAILER!!!", b"", 0, 0)
    data += bytes(-len(data) % 512)
    if len(data) > MAX_SIZE:
        raise ValueError("resource archive exceeds 16 MiB")
    return data, entries["manifest.sha256"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--file", action="append", default=[], metavar="NAME=PATH")
    args = parser.parse_args()
    files = {}
    for item in args.file:
        name, path = item.split("=", 1)
        if name in files:
            raise ValueError("duplicate resource name")
        files[name] = Path(path).read_bytes()
    data, manifest = pack(files)
    args.output.write_bytes(data)
    args.output.with_suffix(".manifest.sha256").write_bytes(manifest)
    with args.header.open("w") as out:
        out.write('/* SPDX-License-Identifier: GPL-3.0-or-later */\n'
                  'static const unsigned char boot_resource_image[]\n'
                  '__attribute__((section(".bootres"), aligned(16), used)) = {\n')
        for i in range(0, len(data), 24):
            out.write(",".join(str(v) for v in data[i:i+24]) + ",\n")
        out.write("};\n")


if __name__ == "__main__":
    main()
