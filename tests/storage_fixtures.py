# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate filesystem media with public format tools; no committed binary blobs."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

def run(*args):
    subprocess.run(list(map(str,args)),check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)

def blank(path,size):
    with path.open('wb') as f:
        f.truncate(size)

def pattern(size):
    block=bytes((i*17+(i>>8))&255 for i in range(65536))
    return (block*(size//len(block)+1))[:size]

def fragment_fat(path,bits):
    """Permute one real mtools-created chain and relocate its bytes accordingly."""
    with path.open('r+b') as f:
        b=f.read(512);sector=struct.unpack_from('<H',b,11)[0];spc=b[13]
        reserved=struct.unpack_from('<H',b,14)[0];copies=b[16]
        fatsize=struct.unpack_from('<H',b,22)[0] or struct.unpack_from('<I',b,36)[0]
        roots=struct.unpack_from('<H',b,17)[0];rootsectors=(roots*32+sector-1)//sector
        data=(reserved+copies*fatsize+rootsectors)*sector;cluster_size=sector*spc
        root=data+(struct.unpack_from('<I',b,44)[0]-2)*cluster_size if bits==32 else (reserved+copies*fatsize)*sector
        wanted=b'PROBE   BIN' if bits==12 else b'LARGE   BIN'
        f.seek(root);directory=f.read(cluster_size if bits==32 else roots*32)
        entry=next(i for i in range(0,len(directory),32) if directory[i:i+11]==wanted)
        first=struct.unpack_from('<H',directory,entry+26)[0]
        if bits==32:first|=struct.unpack_from('<H',directory,entry+20)[0]<<16
        f.seek(reserved*sector);fat=bytearray(f.read(fatsize*sector))
        def get(c):
            if bits==12:
                v=struct.unpack_from('<H',fat,c+c//2)[0];return (v>>4) if c&1 else v&4095
            return struct.unpack_from('<H' if bits==16 else '<I',fat,c*(bits//8))[0]&((1<<bits)-1 if bits!=32 else 0xfffffff)
        def put(c,v):
            if bits==12:
                at=c+c//2;old=struct.unpack_from('<H',fat,at)[0]
                struct.pack_into('<H',fat,at,(old&15)|(v<<4) if c&1 else (old&0xf000)|v)
            else:struct.pack_into('<H' if bits==16 else '<I',fat,c*(bits//8),v)
        chain=[];c=first;eof={12:0xff8,16:0xfff8,32:0xffffff8}[bits]
        while c<eof:
            assert c>=2 and c not in chain;chain.append(c);c=get(c)
        content=[]
        for c in chain:f.seek(data+(c-2)*cluster_size);content.append(f.read(cluster_size))
        order=chain[::2]+chain[1::2]
        assert len(order)>2
        for i,c in enumerate(order):
            f.seek(data+(c-2)*cluster_size);f.write(content[i]);put(c,order[i+1] if i+1<len(order) else eof)
        for i in range(copies):f.seek((reserved+i*fatsize)*sector);f.write(fat)
        f.seek(root+entry+26);f.write(struct.pack('<H',order[0]&65535))
        if bits==32:f.seek(root+entry+20);f.write(struct.pack('<H',order[0]>>16))

def multi_extent_iso(path):
    """Split a real ISO record into two standard multi-extent records."""
    with path.open('r+b') as f:
        roots=set()
        for sector in range(16,80):
            f.seek(sector*2048);d=f.read(2048)
            if d[0]==255:break
            if d[0] in (1,2):roots.add(struct.unpack_from('<I',d,158)[0])
        for root in roots:
            f.seek(root*2048);data=bytearray(f.read(2048));at=0
            while data[at]:
                n=data[at];name=bytes(data[at+33:at+33+data[at+32]])
                if name.upper()==b'LARGE.BIN;1' or name.decode('utf-16-be',errors='ignore').upper().split(';')[0]=='LARGE.BIN':
                    first=bytearray(data[at:at+n]);second=bytearray(first)
                    size=struct.unpack_from('<I',first,10)[0];lba=struct.unpack_from('<I',first,2)[0]
                    first[25]|=128
                    struct.pack_into('<I',first,10,4096);struct.pack_into('>I',first,14,4096)
                    struct.pack_into('<I',second,2,lba+2);struct.pack_into('>I',second,6,lba+2)
                    struct.pack_into('<I',second,10,size-4096);struct.pack_into('>I',second,14,size-4096)
                    assert not any(data[-n:])
                    data[at:]=first+second+data[at+n:-n]
                    f.seek(root*2048);f.write(data);break
                at+=n
            else:raise AssertionError('missing ISO large file')

def generate(out):
    out.mkdir(parents=True,exist_ok=True)
    source=out/'source';source.mkdir(exist_ok=True)
    probe=pattern(131209);large=pattern(12*1024*1024+137)
    (source/'probe.bin').write_bytes(probe)
    (source/'large.bin').write_bytes(large)
    (source/'empty.bin').write_bytes(b'')
    (source/'zeros.bin').write_bytes(b'\0'*16387)
    (source/'Long Unicode 测试.txt').write_bytes(b'UTF-8 filename\n')
    nested=source/'nested';nested.mkdir(exist_ok=True)
    (nested/'inside.txt').write_bytes(b'nested path\n')
    report={'files':{p.relative_to(source).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in source.rglob('*') if p.is_file()},'images':{}}
    for bits,size in ((12,1440*1024),(16,64*1024*1024),(32,128*1024*1024)):
        path=out/f'fat{bits}.img';blank(path,size)
        run('mkfs.fat','-F',bits,'-i','12345678',path)
        for p in source.iterdir():
            if bits==12 and p.name=='large.bin':continue
            run('mcopy','-s','-i',path,p,'::/')
        fragment_fat(path,bits)
        report['images'][path.name]={'kind':'fat','block':512}
    for kind in ('ext2','ext3','ext4'):
        path=out/f'{kind}.img';blank(path,96*1024*1024)
        run(f'mkfs.{kind}','-q','-F','-U','12345678-1234-5678-9abc-123456789abc','-d',source,path)
        # Sparse logical file beyond 4 GiB with real metadata from e2fsprogs.
        sparse=out/'sparse.bin'
        with sparse.open('wb') as f:
            f.write(b'head');f.seek(5*1024**3+17);f.write(b'high-tail')
        run('debugfs','-w','-R',f'write {sparse} /sparse.bin',path)
        sparse.unlink()
        report['images'][path.name]={'kind':'ext','block':512}
    path=out/'ntfs.img';blank(path,96*1024*1024)
    run('mkfs.ntfs','-F','-Q','-s','512',path)
    for p in source.iterdir():
        if p.is_file():run('ntfscp','-f',path,p,'/'+p.name)
    tiny=out/'tiny.bin';tiny.write_bytes(b'head')
    run('ntfscp','-f',path,tiny,'/sparse.bin');tiny.unlink()
    listing=subprocess.check_output(['ntfsls','-i',str(path)],text=True)
    inode=next(line.split()[0] for line in listing.splitlines() if line.endswith('sparse.bin'))
    run('ntfstruncate','-f','-q',path,inode,str(5*1024**3+26))
    report['images'][path.name]={'kind':'ntfs','block':512}
    path=out/'iso.img'
    run('xorriso','-as','mkisofs','-quiet','-J','-o',path,source)
    multi_extent_iso(path)
    report['images'][path.name]={'kind':'iso','block':2048}
    # MBR and GPT contain the exact same real FAT filesystem bytes.
    payload=(out/'fat16.img').read_bytes()
    for bs in (512,4096):
        for scheme in ('mbr','gpt','ebr'):
            path=out/f'{scheme}{bs}.img';start=2048;blocks=(len(payload)+bs-1)//bs
            total=start+blocks+2048;blank(path,total*bs)
            m=bytearray(512);m[510:]=b'\x55\xaa';m[440:444]=b'BOOT'
            struct.pack_into('<B3sB3sII',m,446,0,b'\0'*3,0xee if scheme=='gpt' else 6,b'\0'*3,1 if scheme=='gpt' else start,total-1 if scheme=='gpt' else blocks)
            with path.open('r+b') as f:
                f.write(m);f.seek(start*bs);f.write(payload)
                if scheme=='ebr':
                    struct.pack_into('<B3sB3sII',m,446,0,b'\0'*3,15,b'\0'*3,start-1,blocks+1)
                    f.seek(0);f.write(m)
                    e=bytearray(512);e[510:]=b'\x55\xaa'
                    struct.pack_into('<B3sB3sII',e,446,0,b'\0'*3,6,b'\0'*3,1,blocks)
                    f.seek((start-1)*bs);f.write(e)
                if scheme=='gpt':
                    entries=bytearray(128*128)
                    entries[:16]=bytes.fromhex('a2a0d0ebe5b9334487c068b6b72699c7')
                    entries[16:32]=bytes.fromhex('78563412341278569abc123456789abc')
                    struct.pack_into('<QQ',entries,32,start,start+blocks-1)
                    for lba,backup,table in ((1,total-1,2),(total-1,1,total-1-len(entries)//bs)):
                        h=bytearray(bs);h[:8]=b'EFI PART'
                        struct.pack_into('<IIIIQQQQ',h,8,0x10000,92,0,0,lba,backup,34,total-34)
                        h[56:72]=b'0123456789abcdef'
                        struct.pack_into('<QIII',h,72,table,128,128,zlib.crc32(entries))
                        struct.pack_into('<I',h,16,zlib.crc32(h[:92]))
                        f.seek(lba*bs);f.write(h);f.seek(table*bs);f.write(entries)
            report['images'][path.name]={'kind':scheme,'block':bs,'partition':5 if scheme=='ebr' else 1}
    (out/'manifest.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n')
    return report

def verify(out,exe):
    report=json.loads((out/'manifest.json').read_text())
    for image,meta in report['images'].items():
        listing=subprocess.run([str(exe),str(out/image),str(meta['block']),'/',str(meta.get('partition',0))],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60).stdout.decode()
        assert 'f 131209 probe.bin\n' in listing,(image,listing)
        assert 'Long Unicode 测试.txt\n' in listing,(image,listing)
        if meta['kind']!='ntfs':
            assert 'd 0 nested\n' in listing,(image,listing)
        for path,digest in report['files'].items():
            if image=='fat12.img' and path=='large.bin':continue
            if meta['kind']=='ntfs' and '/' in path:continue
            command=[str(exe),str(out/image),str(meta['block']),'/'+path,str(meta.get('partition',0))]
            result=subprocess.run(command,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60)
            assert hashlib.sha256(result.stdout).hexdigest()==digest,(image,path,result.stderr)
        if meta['kind']=='ext':
            command=[str(exe),str(out/image),'512','/sparse.bin','0',str(5*1024**3)]
            result=subprocess.run(command,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
            assert result.stdout==b'\0'*17+b'high-tail',image
            # Range in a multi-GiB hole, avoiding writing a 5-GiB extraction artifact.
        if meta['kind']=='ntfs':
            result=subprocess.run([str(exe),str(out/image),'512','/sparse.bin','0',str(5*1024**3)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
            assert result.stdout==b'\0'*26
        print('PASS: filesystem content',image,flush=True)
    return report

if __name__=='__main__':
    import sys
    out=Path(sys.argv[1]);generate(out)
    if len(sys.argv)>2:verify(out,Path(sys.argv[2]))
