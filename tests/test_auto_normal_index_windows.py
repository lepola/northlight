#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""x86 cache-state/collision oracle; complements the existing host sanitizer test."""
import hashlib,json,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir()/'normal-index-windows';out.mkdir(parents=True,exist_ok=True)
sources={
    'linear':(fp.TESTS/'fixtures/reference/auto_normal_cache-linear-43090ea.h','7e0cff3aee386d3e1ec559251e6fc11a6bdf368deb3376bc38007680e1c42c5b'),
    'integrated':(fp.TESTS/'fixtures/reference/auto_normal_cache-index-3dfdb38.h','05e74de76cd64c9e21f48f4b7243873c08a8a8927bf421f8a80a54fd26224463'),
    'current':(fp.src('auto_normal_cache.h'),None),
}
watch=[p for p in fp.SRC.rglob('*') if p.is_file()]+[fp.TESTS/'auto_normal_index.cpp',Path(__file__)]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p):sha(p) for p in watch};runs=[]
for collisions in [False,True]:
    for key,(path,expected) in sources.items():
        if expected:assert hashlib.sha256(path.read_bytes().replace(b'\r\n',b'\n')).hexdigest()==expected
        text='\n'.join(line for line in path.read_text().splitlines() if not line.startswith('#include '))+'\n'
        text=text.replace('class NorthlightAutoNormalCache {','class NorthlightAutoNormalCache {public:',1).replace('NorthlightAutoNormalCache','Cache'+key.capitalize())
        if collisions:
            for type_name in ['IDirect3DTexture9','IDirect3DBaseTexture9']:
                text=text.replace('std::unordered_map<'+type_name+'*,size_t>','std::unordered_map<'+type_name+'*,size_t,ZeroHash>')
        (out/('cache_'+key+'.h')).write_bytes(text.encode())
    for mode in ['O2','O0']:
        exe=out/(str(int(collisions))+'-'+mode+'.exe')
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-'+mode,'-UNDEBUG','-std=c++17','-DNORMAL_INDEX_COLLISIONS='+str(int(collisions)),'-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'auto_normal_index.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        (out/(exe.stem+'.log')).write_text(result.stdout+result.stderr)
        assert result.returncode==0 and 'AUTO_NORMAL_INDEX_PASS' in result.stdout,result.stdout+result.stderr
        runs.append({'mode':mode,'collisions':collisions,'stdout':result.stdout});print(mode,'collisions='+str(collisions),'PASS exact cache state and ownership',flush=True)
assert all(sha(Path(path))==digest for path,digest in inputs.items()),'Inputs changed during validation'
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'runs':runs,'GPU_used':False,'game_launched':False,'sanitizers_run':False},indent=2)+'\n')
