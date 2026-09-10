# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent newc/manifest oracle and malformed format cases."""
import hashlib
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pack_resources import pack, record


def inspect(data):
    result, off = {}, 0
    while True:
        assert data[off:off+6] == b"070701"
        v = [int(data[off+6+i*8:off+14+i*8], 16) for i in range(13)]
        assert v[2:4] == [0, 0] and v[5] == 0 and v[7:11] == [0]*4 and v[12] == 0
        name = data[off+110:off+110+v[11]-1].decode()
        start = (off+110+v[11]+3) & ~3
        body = data[start:start+v[6]]
        off = (start+v[6]+3) & ~3
        if name == "TRAILER!!!":
            assert not body and not any(data[off:])
            break
        assert name not in result
        result[name] = body
    assert list(result) == sorted(result)
    expected = b"".join(f"{hashlib.sha256(b).hexdigest()}  {n}\n".encode()
                        for n, b in result.items() if n != "manifest.sha256")
    assert result["manifest.sha256"] == expected
    return result


def main():
    files = {f"items/f{n:03}": bytes(range(256))[:n] for n in range(120)}
    data, manifest = pack(files)
    assert pack(dict(reversed(list(files.items())))) == (data, manifest)
    assert inspect(data) == dict(files, **{"manifest.sha256": manifest})
    with tempfile.TemporaryDirectory(prefix="boot-archive-") as temp:
        path = Path(temp) / "test.cpio"
        def check(blob, good=False):
            path.write_bytes(blob)
            result = subprocess.run([sys.argv[1], str(path)], capture_output=True)
            assert result.returncode == (0 if good else 1), (result.returncode, result.stderr)
        check(data, True)
        # GNU cpio is an optional independent interoperability oracle.
        if shutil.which("cpio"):
            extracted = Path(temp)/"extracted"
            extracted.mkdir()
            subprocess.run(["cpio", "-id", "--quiet", "--no-absolute-filenames"],
                           input=data, cwd=extracted, check=True, capture_output=True)
            for name, body in dict(files, **{"manifest.sha256": manifest}).items():
                assert (extracted/name).read_bytes() == body
            # Public-tool archive: flat regular files, no './' prefix or links.
            public = Path(temp)/"public"
            public.mkdir()
            for name, body in {"a": b"a", "z": b"z"}.items():
                (public/name).write_bytes(body)
            (public/"manifest.sha256").write_bytes(pack({"a": b"a", "z": b"z"})[1])
            generated = subprocess.run(["cpio", "-o", "-H", "newc", "--quiet"],
                input=b"a\nmanifest.sha256\nz\n", cwd=public, capture_output=True, check=True).stdout
            check(generated, True)
        # CLI output ignores argument order, source path, mtime and epoch.
        for i in range(2):
            root = Path(temp)/f"source-{i}"
            root.mkdir()
            args = []
            for name in ("a", "z"):
                src = root/name
                src.write_bytes(name.encode())
                os.utime(src, (1000+i, 1000+i))
                args.append(f"{name}={src}")
            command = [sys.executable, str(Path(__file__).resolve().parents[1]/"tools/pack_resources.py"),
                       "--output", str(root/"resource.cpio"), "--header", str(root/"resource.h")]
            for mapping in (args if i == 0 else reversed(args)):
                command += ["--file", mapping]
            subprocess.run(command, env=dict(os.environ, SOURCE_DATE_EPOCH=str(100+i)), check=True)
        for name in ("resource.cpio", "resource.h", "resource.manifest.sha256"):
            assert (Path(temp)/"source-0"/name).read_bytes() == (Path(temp)/"source-1"/name).read_bytes()
        # SHA padding boundaries, binary data and zero length are compared to hashlib.
        for n in (0, 1, 55, 56, 63, 64, 65, 127, 128, 1000):
            check(pack({"x": bytes(i % 256 for i in range(n))})[0], True)
        trailer = record("TRAILER!!!", b"", 0, 0)
        for name in ("../x", "/abs", "x//y", "x/./y", "x/", "x\\y", "x\0y"):
            check(record(name, b"x") + trailer)
        for field, value in ((1, "0000a1ff"), (4, "00000002"), (6, "ffffffff"),
                             (11, "ffffffff"), (11, "00000000"), (12, "00000001"),
                             (0, "0000000z")):
            bad = bytearray(data)
            bad[6+field*8:14+field*8] = value.encode()
            check(bad)
        check(b"070702" + data[6:])
        check(record("x", b"a") + record("x", b"b") + trailer)
        check(b"".join(record(f"f{i}", b"") for i in range(129)) + trailer)
        check(data + b"X")
        check(data + data)
        check(record("TRAILER!!!", b"x", 0, 0))
        check(pack({"x": b"a"})[0].replace(b"  x\n", b"  y\n"))
        check(record("x", b"a") + trailer)  # missing manifest
    print("PASS: deterministic packer, independent SHA/newc oracle and malformed corpus")


if __name__ == "__main__":
    main()
