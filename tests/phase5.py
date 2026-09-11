# SPDX-License-Identifier: GPL-3.0-or-later
"""Phase 5 loopback and diskfilter slice; cryptodisk remains deferred."""
import argparse
import json
from pathlib import Path
import sys
import phase1
import phase4
import volume_fixtures
import volumes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skip-baseline', action='store_true',
                        help='reuse a completed current-source Phase 4 baseline')
    args = parser.parse_args()
    sys.argv = [sys.argv[0]]
    if not args.skip_baseline:
        phase4.main()
    volumes.main()
    root = phase1.ROOT
    baseline = root/'build/phase4'
    out = root/'build/phase5'
    out.mkdir(parents=True, exist_ok=True)
    (out/'results.json').unlink(missing_ok=True)
    previous = json.loads((baseline/'results.json').read_text())
    report = {'baseline': previous['sha256'], 'qemu': {},
              'host': json.loads((root/'build/volumes/results.json').read_text())}
    fixtures = root/'build/volumes/fixtures'
    cases = [('loop', ['nested.img'], ['BOOT:PASS:loopback-nested']),
             ('lvm-md', ['lvm-on-md.img'], ['BOOT:PASS:volume:lvm/fixture-data',
                                         'BOOT:PASS:storage-fat']),
             ('raid5-degraded', ['md5-0.img','md5-2.img'], ['BOOT:PASS:volume:md/fixture',
                                                       'BOOT:PASS:storage-fat'])]
    for target in ('i386-pc','i386-efi','x86_64-efi','arm64-efi','loongarch64-efi'):
        for label, media, markers in cases:
            if target == 'i386-pc':
                exe = 'qemu-system-x86_64'
                command = ['-m','512M','-cpu','qemu32,+pae,-lm','-kernel',baseline/target/'boot-linux.bz']
                for name in media: command += ['-drive',f'format=raw,file={fixtures/name}']
            else:
                old = previous['qemu'][target]['command']
                exe = old[0]
                start = old.index('-m') if target.startswith(('i386','x86')) else old.index('-M')
                command = old[start:]
                if '-debugcon' in command: command = command[:command.index('-debugcon')]
                for i,name in enumerate(media):
                    command += ['-drive',f'if=none,id=vol{i},format=raw,readonly=on,file={fixtures/name}',
                                '-device',f'virtio-blk-pci,drive=vol{i},logical_block_size=4096,physical_block_size=4096']
            key = target+'-'+label
            report['qemu'][key] = phase1.qemu(out, key, exe, command,
                phase1.COMMON + ['BOOT:PASS:volume-stale','BOOT:PASS:storage-ready'] + markers, timeout=180)
    (out/'results.json').write_text(json.dumps(report, indent=2, sort_keys=True)+'\n')
    print('PASS: Phase 5 loopback/LVM/MD volumes on all five firmware targets')


if __name__ == '__main__':
    main()
