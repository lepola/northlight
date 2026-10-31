#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Exact current-buffer capture/provenance and GPU reuse under rewritten revisions; no GPU/game."""
from pathlib import Path
import ast,hashlib,json,re,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def literal(name,key):
    return next(ast.literal_eval(item.value) for item in ast.parse((fp.TESTS/name).read_text()).body if isinstance(item,ast.Assign) and any(getattr(target,'id','')==key for target in item.targets))

stub=literal('test_terrain_snapshot.py','stub').replace('struct IDirect3DDevice9{','struct IDirect3DDevice9{\nvirtual HRESULT CreateVertexBuffer(UINT,DWORD,UINT,unsigned,IDirect3DVertexBuffer9**,void*){return E_POINTER;}\nvirtual HRESULT CreateIndexBuffer(UINT,DWORD,D3DFORMAT,unsigned,IDirect3DIndexBuffer9**,void*){return E_POINTER;}')
harness=literal('test_draw_snapshot.py','harness').split('int main()')[0]
with tempfile.TemporaryDirectory(prefix='material-replay-content-') as temporary:
    root=Path(temporary);(root/'d3d9.h').write_text(stub);(root/'replay_content_harness.h').write_text(harness)
    paths=[path for path in fp.SRC.rglob('*') if path.is_file() and path.suffix in ('.h','.inl','.cpp')]
    paths += [fp.TESTS/name for name in ('test_material_replay_content.cpp','test_draw_snapshot.py','test_terrain_snapshot.py')]+[Path(__file__).resolve()]
    inputs={str(path.relative_to(fp.REPO)):hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(paths)}
    records=[]
    for name,flags,current in [('old',['-O2'],False),('new',['-O2'],True),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'],True)]:
        executable=root/('fixture-'+name)
        command=['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',str(root),*(['-DNORTHLIGHT_TEST_REPLAY_CONTENT_CURRENT'] if current else []),*fp.test_include_flags(),str(fp.TESTS/'test_material_replay_content.cpp'),'-o',str(executable)]
        subprocess.run(command,check=True)
        for changing in [False,True]:
            result=subprocess.run([str(executable),*(['changing'] if changing else [])],check=True,capture_output=True,text=True);print(name,result.stdout,flush=True)
            match=re.search(r'REPLAY_CONTENT_WORKLOAD (.*)',result.stdout);assert match
            records.append(dict(name=name,changing=changing,values=dict(re.findall(r'(\w+)=([^ ]+)',match[1])),stdout=result.stdout,command=command))
    for changing in [False,True]:
        selected=[record for record in records if record['changing']==changing]
        for key in ['draws','frames','accepted','readBytes','sourceCap','gpuCap','readCap']:
            assert len({record['values'][key] for record in selected})==1,(key,selected)
        assert int(selected[1]['values']['upBytes'])<=int(selected[0]['values']['upBytes'])
        if not changing:assert int(selected[1]['values']['upBytes'])<int(selected[0]['values']['upBytes'])
    assert inputs=={relative:hashlib.sha256((fp.REPO/relative).read_bytes()).hexdigest() for relative in inputs},'Source changed during validation'
    output=fp.output_dir()/'material-replay-content-validation.json';output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes((json.dumps(dict(status='pass',control='Current production headers with replay content reuse disabled',inputs=inputs,records=records),indent=2)+'\n').encode())
print('PASS exact source provenance, current read/admission, byte/metadata collision controls, unchanged caps and bounded replay reuse.')
