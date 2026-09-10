# SPDX-License-Identifier: GPL-3.0-or-later
"""Host storage content, corruption and bounded mutation tests under sanitizers."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
from storage_fixtures import generate,verify
import storage_corruption
ROOT=Path(__file__).resolve().parents[1]

def main():
    # Fail on the first diagnostic; symbolizer startup is not part of the test.
    os.environ.setdefault('UBSAN_OPTIONS','halt_on_error=1:symbolize=0')
    os.environ.setdefault('ASAN_OPTIONS','symbolize=0')
    out=ROOT/'build/storage';out.mkdir(parents=True,exist_ok=True)
    (out/'results.json').unlink(missing_ok=True)
    with (out/'commands.log').open('w') as log:
        def run(*cmd):
            cmd=list(map(str,cmd));log.write('$ '+' '.join(cmd)+'\n');log.flush()
            subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
        run('cmake','-S',ROOT,'-B',out/'host','-G','Ninja','-DCMAKE_C_COMPILER=clang','-DBOOT_SANITIZERS=ON','-DCMAKE_BUILD_TYPE=Debug')
        run('cmake','--build',out/'host')
        run('ctest','--test-dir',out/'host','--output-on-failure')
        fixtures=out/'fixtures';generate(fixtures)
        report={'content':verify(fixtures,out/'host/boot-storage-read')}
        report['corruption']=storage_corruption.verify(fixtures,out/'host/boot-storage-read',out/'corrupt')
        report['mutations']={}
        for name in ('fat12','fat16','fat32','ext2','ext3','ext4','ntfs','iso','gpt512','gpt4096'):
            bs=2048 if name=='iso' else 4096 if name=='gpt4096' else 512
            run(out/'host/boot-storage-fuzz',fixtures/(name+'.img'),bs)
            report['mutations'][name]=2000
        report['sha256']={}
        for image in sorted(fixtures.glob('*.img')):
            with image.open('rb') as f:report['sha256'][image.name]=hashlib.file_digest(f,'sha256').hexdigest()
    (out/'results.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    print('PASS: storage content, named corruption, 20000 mutations and ASan/UBSan',flush=True)

if __name__=='__main__':main()
