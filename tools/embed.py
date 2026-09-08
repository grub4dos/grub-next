# SPDX-License-Identifier: GPL-3.0-or-later
"""Embed a test child without host paths or timestamps."""
import pathlib
import sys
content = pathlib.Path(sys.argv[1]).read_bytes()
pathlib.Path(sys.argv[2]).write_text(
    "static unsigned char hello_image[] = {\n" +
    ",\n".join(",".join(str(x) for x in content[i:i+24])
               for i in range(0, len(content), 24)) + "\n};\n")
