#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Exact cache state against the published linear cache and integrated index oracle; no GPU/game."""
from pathlib import Path
import hashlib,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

HERE=Path(__file__).resolve().parent
FROZEN=HERE/'fixtures/reference'
SOURCES={
    'linear':(FROZEN/'auto_normal_cache-linear-43090ea.h','7e0cff3aee386d3e1ec559251e6fc11a6bdf368deb3376bc38007680e1c42c5b'),
    'integrated':(FROZEN/'auto_normal_cache-index-3dfdb38.h','05e74de76cd64c9e21f48f4b7243873c08a8a8927bf421f8a80a54fd26224463'),
    'current':(fp.src('auto_normal_cache.h'),None),
}
for key,(path,expected) in SOURCES.items():
    if expected:assert hashlib.sha256(path.read_bytes().replace(b'\r\n',b'\n')).hexdigest()==expected,key
with tempfile.TemporaryDirectory(prefix='northlight-normal-index-') as work:
    tmp=Path(work)
    for collisions in (False,True):
        for key,(path,_) in SOURCES.items():
            text=path.read_text()
            text='\n'.join(line for line in text.splitlines() if not line.startswith('#include '))+'\n'
            assert text.count('class NorthlightAutoNormalCache {')==1
            text=text.replace('class NorthlightAutoNormalCache {','class NorthlightAutoNormalCache {public:',1)
            text=text.replace('NorthlightAutoNormalCache','Cache'+key.capitalize())
            if collisions:
                for key_type in ['IDirect3DTexture9','IDirect3DBaseTexture9']:
                    text=text.replace('std::unordered_map<'+key_type+'*,size_t>','std::unordered_map<'+key_type+'*,size_t,ZeroHash>')
            (tmp/('cache_'+key+'.h')).write_text(text)
        for label,flags in [('O2',['-O2']),('san',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
            binary=tmp/('index-'+str(int(collisions))+'-'+label)
            build=subprocess.run(['clang++','-std=c++17','-UNDEBUG',*flags,'-DNORMAL_INDEX_COLLISIONS='+str(int(collisions)),'-I',str(tmp),*fp.test_include_flags(),str(HERE/'auto_normal_index.cpp'),'-o',str(binary)],capture_output=True,text=True)
            assert build.returncode==0,build.stdout+build.stderr
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            assert run.returncode==0 and 'AUTO_NORMAL_INDEX_PASS' in run.stdout,run.stdout+run.stderr
            print(label,'collisions='+str(collisions),run.stdout.strip(),flush=True)
