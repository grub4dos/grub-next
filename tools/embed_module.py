# SPDX-License-Identifier: GPL-3.0-or-later
"""Embed SDK acceptance fixtures, independently of the Phase 3 resource archive."""
from pathlib import Path
import sys
with Path(sys.argv[3]).open("w") as out:
    for name, path in zip(("sample", "failing"), sys.argv[1:3]):
        data = Path(path).read_bytes()
        out.write(f"static const unsigned char {name}_image[] = {{\n")
        for i in range(0, len(data), 24):
            out.write(",".join(str(b) for b in data[i:i+24]) + ",\n")
        out.write("};\n")
