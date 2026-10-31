#!/usr/bin/env python3
# northlight-test: requires=zig,windows-gpu,client manual
"""Compare complete material images with current terrain bounds shared or independently read."""
import argparse,hashlib,importlib.util,json,os,shutil,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--assets',type=Path,default=fp.out()/'oblique')
p.add_argument('--output',type=Path,default=fp.out()/'shared-terrain-bounds-gpu')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('guard',fp.TESTS/'validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
inputs=[*a.assets.glob('source.fgr*'),*[a.assets/n for n in ('terrain-vs.bin','terrain-ps.bin','PreviewBase.bin')]]
assert inputs and all(f.is_file() for f in inputs),'Prepare the MPQ-derived oblique material assets first'
exe=a.output/'shared-bounds.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wno-microsoft-exception-spec','-std=c++17',*fp.test_include_flags(),str(fp.TESTS/'auto_normal_oblique_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
backends={'native':Path(os.environ['SystemRoot'])/'SysWOW64/d3d9.dll','dxvk':fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'}
rows=[]
for backend,runtime in backends.items():
    assert runtime.is_file()
    for msaa in (0,2):
        versions=[]
        for shared in (0,1):
            out=a.output/f'{backend}-msaa{msaa}-shared{shared}';out.mkdir(exist_ok=True)
            for f in inputs:shutil.copy2(f,out/f.name)
            guard.processes()
            env=fp.zig_env();env.update(DXVK_CONFIG='d3d9.customVendorId = 1002; d3d9.cachedDynamicBuffers = True; d3d9.cachedWriteOnlyBuffers = True',NORTHLIGHT_TEST_TERRAIN_ORIGIN='1',NORTHLIGHT_TEST_MSAA=str(msaa),NORTHLIGHT_TEST_SHARED_BUFFERED='1',NORTHLIGHT_TEST_DEFERRED_RESIDENT='1')
            env.pop('NORTHLIGHT_TEST_CAPTURED_BOUNDS',None)
            if shared:env['NORTHLIGHT_TEST_CAPTURED_BOUNDS']='1'
            run=subprocess.run([str(exe),str(runtime),str(out)+'/'],cwd=out,env=env,capture_output=True,text=True,timeout=180)
            (out/'gpu.log').write_text(run.stdout+run.stderr)
            assert run.returncode==0,(out/'gpu.log',run.stdout[-3000:],run.stderr[-3000:])
            assert 'OBLIQUE_NATIVE_POM_PASS' in run.stdout and 'MATERIAL_MSAA_PASS' in run.stdout
            if shared:assert run.stdout.count('CAPTURED_BOUNDS_GPU ')==8,run.stdout
            outputs={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in out.glob('*.fgr') if f.name!='source.fgr'}
            assert len(outputs)==12,outputs
            assert outputs["normal-on.fgr"]!=outputs["native-normal-off.fgr"],"Bump must visibly contribute"
            assert outputs["pom-on.fgr"]!=outputs["normal-on.fgr"],"POM must visibly contribute"
            versions.append(outputs)
        assert versions[0]==versions[1],(backend,msaa,{n for n in versions[0] if versions[0][n]!=versions[1][n]})
        rows.append(dict(backend=backend,msaa=msaa,bit_exact=True,outputs=versions[1]));print(f'SHARED_TERRAIN_GPU_PASS backend={backend} msaa={msaa} images={len(versions[1])}',flush=True)
(a.output/'validation.json').write_text(json.dumps({'pass':True,'game_launched':False,'cases':rows},indent=2)+'\n')
