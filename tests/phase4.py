# SPDX-License-Identifier: GPL-3.0-or-later
"""Storage acceptance: shared host/runtime readers on BIOS and four EFI targets."""
import json
import os
from pathlib import Path
import subprocess
import phase0
import phase1
import phase3
from storage_fixtures import generate,verify
import storage_corruption

def main():
    output=phase1.ROOT/'build/phase4'
    phase1.COMMON += ['BOOT:PASS:storage-ready','BOOT:PASS:storage-rescan-stale']
    phase1.EXTRA_ARTIFACTS={'host':['boot-storage-read','boot-storage-test','boot-storage-fuzz']}
    phase3.main(output)
    report=json.loads((output/'results.json').read_text())
    (output/'results.json').unlink()
    fixtures=output/'fixtures';generate(fixtures)
    report['filesystems']=verify(fixtures,output/'host/boot-storage-read')
    report['corruption']=storage_corruption.verify(fixtures,output/'host/boot-storage-read',output/'corrupt')
    # Same filesystem bytes through actual BIOS firmware and EFI protocols.
    for name in ('fat12','fat16','fat32','ext2','ext3','ext4','ntfs','iso','mbr512','gpt512','ebr512'):
        meta=report['filesystems']['images'][name+'.img']
        kind=meta['kind'];kind='iso9660' if kind=='iso' else 'fat' if kind in ('mbr','gpt','ebr') else kind
        args=['-m','512M','-cpu','qemu32,+pae,-lm','-kernel',output/'i386-pc/boot-linux.bz']
        if name=='fat12':args+=['-drive',f'if=floppy,format=raw,file={fixtures}/{name}.img']
        elif name=='iso':args+=['-cdrom',fixtures/(name+'.img')]
        else:args+=['-drive',f'format=raw,file={fixtures}/{name}.img']
        key='storage-bios-'+name
        report['qemu'][key]=phase1.qemu(output,key,'qemu-system-x86_64',args,phase1.COMMON+[f'BOOT:PASS:storage-{kind}'])
        assert 'BOOT:PASS:storage-bios-cpu-fp' in (output/f'{key}.serial.log').read_text()
        if name in ('ext2','ext3','ext4','ntfs'):
            assert 'BOOT:PASS:storage-sparse-high-offset' in (output/f'{key}.serial.log').read_text()
    for target in ('i386-efi','x86_64-efi','arm64-efi','loongarch64-efi'):
        previous=report['qemu'][target]['command']
        # Reuse the verified firmware/ESP wiring, excluding runner-managed flags.
        start=previous.index('-m') if target.startswith(('i386','x86')) else previous.index('-M')
        args=previous[start:]
        if '-debugcon' in args:args=args[:args.index('-debugcon')]
        for idx,name in enumerate(('gpt4096','ext2','ext3','ext4','ntfs','iso')):
            args+=['-drive',f'if=none,id=fixture{idx},format=raw,readonly=on,file={fixtures}/{name}.img',
                   '-device',f'virtio-blk-pci,drive=fixture{idx}'+(',logical_block_size=4096,physical_block_size=4096' if name=='gpt4096' else ',logical_block_size=2048,physical_block_size=2048' if name=='iso' else '')]
        key='storage-'+target
        report['qemu'][key]=phase1.qemu(output,key,previous[0],args,phase1.COMMON+
            ['BOOT:PASS:storage-'+k for k in ('fat','ext','ntfs','iso9660')],timeout=180)
        text=(output/f'{key}.serial.log').read_text()
        assert text.count('BOOT:PASS:storage-ext')==3
        assert text.count('BOOT:PASS:storage-sparse-high-offset')==4
    (output/'results.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    print('PASS: Phase 4 storage host and all firmware targets',flush=True)

if __name__=='__main__':main()
