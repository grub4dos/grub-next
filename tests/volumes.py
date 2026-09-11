# SPDX-License-Identifier: GPL-3.0-or-later
"""Loopback/diskfilter host acceptance with ASan/UBSan and generated media."""
import json
import os
from pathlib import Path
import struct
import subprocess
import volume_fixtures

ROOT = Path(__file__).resolve().parents[1]


def main():
    os.environ.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:symbolize=0')
    os.environ.setdefault('ASAN_OPTIONS', 'symbolize=0')
    out = ROOT/'build/volumes'
    out.mkdir(parents=True, exist_ok=True)
    (out/'results.json').unlink(missing_ok=True)
    with (out/'commands.log').open('w') as log:
        for cmd in (['cmake','-S',str(ROOT),'-B',str(out/'host'),'-G','Ninja',
                     '-DCMAKE_C_COMPILER=clang','-DBOOT_SANITIZERS=ON','-DCMAKE_BUILD_TYPE=Debug'],
                    ['cmake','--build',str(out/'host')],
                    ['ctest','--test-dir',str(out/'host'),'--output-on-failure']):
            log.write('$ '+' '.join(cmd)+'\n'); log.flush()
            subprocess.run(cmd, check=True, stdout=log, stderr=subprocess.STDOUT)
    fixtures = out/'fixtures'
    report = {'images': volume_fixtures.generate(fixtures)}
    binary = out/'host/boot-volume-test'
    report['content'] = volume_fixtures.verify(fixtures, binary)
    report['rejections'] = {}
    changes = [('md-count', 'md1-0.img', 4096+92, '<I', 33),
               ('md-role', 'md1-0.img', 4096+256, '<H', 2),
               ('md-overflow', 'md1-0.img', 4096+128, '<Q', 2**64-16),
               ('md-version', 'md1-0.img', 4096+4, '<I', 9),
               ('lvm-label-offset', 'lvm.img', 512+20, '<I', 511),
               ('lvm-metadata-overflow', 'lvm.img', 4096+40, '<Q', 2**64-512),
               ('lvm-metadata-size', 'lvm.img', 4096+48, '<Q', 2**64-1)]
    for label, source, offset, fmt, value in changes:
        data = bytearray((fixtures/source).read_bytes())
        struct.pack_into(fmt, data, offset, value)
        target = out/(label+'.img'); target.write_bytes(data)
        name = 'md/fixture' if label.startswith('md-') else 'lvm/fixture-data'
        p = subprocess.run([str(binary), '512', name, '/probe.bin', str(target)],
                           capture_output=True, timeout=30)
        (out/(label+'.log')).write_bytes(p.stderr)
        assert p.returncode == 1, (label, p.returncode, p.stderr.decode(errors='replace'))
        assert b'AddressSanitizer' not in p.stderr and b'runtime error' not in p.stderr
        report['rejections'][label] = 'not found'
    p = subprocess.run([str(binary), '512', 'lvm/fixture-data', '/probe.bin',
                        str(fixtures/'lvm-cycle.img')], capture_output=True, timeout=30)
    (out/'lvm-cycle.log').write_bytes(p.stderr)
    assert p.returncode == 1 and b'AddressSanitizer' not in p.stderr and b'runtime error' not in p.stderr
    report['rejections']['lvm-mutual-cycle'] = 'not found; bounded graph validation'
    (out/'results.json').write_text(json.dumps(report, indent=2, sort_keys=True)+'\n')
    print('PASS: volume content, nested/cache/stale/EOF and metadata rejections under ASan/UBSan')


if __name__ == '__main__':
    main()
