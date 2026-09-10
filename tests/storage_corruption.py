# SPDX-License-Identifier: GPL-3.0-or-later
"""Named corruption/rejection checks supplement mutation/sanitizer coverage."""
import shutil
import struct
import subprocess

def verify(fixtures,exe,out):
    out.mkdir(parents=True,exist_ok=True)
    cases=[]
    def check(name,source,changes,block=512,partition=0,debugfs=None):
        target=out/(name+'.img')
        shutil.copyfile(fixtures/source,target)
        with target.open('r+b') as f:
            for off,data in changes:f.seek(off);f.write(data)
        if debugfs:
            subprocess.run(['debugfs','-w','-R',debugfs,str(target)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        p=subprocess.run([str(exe),str(target),str(block),'/probe.bin',str(partition)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=15)
        assert p.returncode==1 and b'storage status=' in p.stderr,(name,p.returncode,p.stderr)
        cases.append({'case':name,'error':p.stderr.decode().strip()})
        target.unlink()
    check('fat-invalid-geometry','fat16.img',[(16,b'\xff')])
    b=(fixtures/'fat16.img').read_bytes()[:512]
    sector=struct.unpack_from('<H',b,11)[0];reserved=struct.unpack_from('<H',b,14)[0]
    fats=struct.unpack_from('<H',b,22)[0];root=(reserved+b[16]*fats)*sector
    with (fixtures/'fat16.img').open('rb') as f:f.seek(root);directory=f.read(16384)
    at=next(i for i in range(0,len(directory),32) if directory[i:i+11]==b'PROBE   BIN')
    cluster=struct.unpack_from('<H',directory,at+26)[0]
    check('fat-cyclic-file','fat16.img',[(reserved*sector+cluster*2,struct.pack('<H',cluster))])
    check('ext-invalid-inode-size','ext4.img',[(1024+88,b'\x01\x00')])
    # The volume feature bit permits encryption but does not encrypt every file.
    check('ext-encrypted-file','ext4.img',[],debugfs='set_inode_field /probe.bin flags 0x80800')
    with (fixtures/'ntfs.img').open('rb') as f:b=f.read(512)
    mft=struct.unpack_from('<Q',b,48)[0]*struct.unpack_from('<H',b,11)[0]*b[13]
    check('ntfs-mft-fixup','ntfs.img',[(mft+510,b'\0\0')])
    # GRUB bounds attributes by the allocated MFT record, not bytes-in-use.
    check('ntfs-attribute-offset-overflow','ntfs.img',[(mft+20,b'\xff'*2)])
    check('ntfs-invalid-cluster','ntfs.img',[(13,b'\x03')])
    check('iso-descriptor-magic','iso.img',[(16*2048+1,b'BAD!!'),(17*2048+1,b'BAD!!')],2048)
    check('gpt-header-crc','gpt512.img',[(512+16,b'\0'*4)],partition=1)
    check('gpt-entry-crc','gpt4096.img',[(8192+32,b'\xff'*8)],4096,1)
    check('mbr-range-overflow','mbr512.img',[(446+12,b'\xff'*4)],partition=1)
    # A backwards EBR chain must terminate with a corruption result.
    ebr=bytearray(512);ebr[510:]=b'\x55\xaa'
    struct.pack_into('<B3sB3sII',ebr,446,0,b'\0'*3,15,b'\0'*3,1,20)
    mbr=bytearray(512);mbr[510:]=b'\x55\xaa'
    struct.pack_into('<B3sB3sII',mbr,446,0,b'\0'*3,15,b'\0'*3,10,100)
    check('ebr-cycle','mbr512.img',[(0,mbr),(10*512,ebr),(11*512,ebr)],partition=1)
    print('PASS: named corruption cases',len(cases),flush=True)
    return cases
