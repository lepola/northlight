"""Compare GPU height fetch with forced CPU fallback using actual client shaders."""
from pathlib import Path
import argparse,json,os,re,shutil,struct,subprocess,sys
import numpy as np
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root));import northlight_paths as fp
p=argparse.ArgumentParser();p.add_argument('--backend',required=True);p.add_argument('--range-only',action='store_true');p.add_argument('--model-range-only',action='store_true');p.add_argument('--range-streams-only',action='store_true');args=p.parse_args()
name='native' if 'windows' in args.backend.lower() else 'dxvk'
out=fp.out()/('gpu-height-'+name);out.mkdir(parents=True,exist_ok=True)
exe=out/'height.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_OBLIQUE_WIDTH=320','-DNORTHLIGHT_OBLIQUE_HEIGHT=200',*fp.test_include_flags(),str(root/'tests/auto_normal_oblique_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
if args.range_streams_only:
    folder=fp.out()/'displacement-gpu-20261022/terrain-sm3';env=fp.zig_env();env['NORTHLIGHT_TEST_RANGE_STREAMS']='1'
    run=subprocess.run([str(exe),str(Path(args.backend).resolve()),str(folder)+'/', '12','.7'],env=env,cwd=out,capture_output=True,text=True,timeout=90)
    (out/'range-streams.log').write_text(run.stdout+run.stderr,encoding='utf-8');print(run.stdout,flush=True)
    assert run.returncode==0 and 'RANGE_STREAM_TRACKING_PASS' in run.stdout,run.stdout+run.stderr
    raise SystemExit()
if args.model_range_only:
    folder=fp.out()/'model-materials-dxvk/3-weighted4-combiners_opaque';env=fp.zig_env();env['NORTHLIGHT_TEST_MODEL_RANGE']='1'
    run=subprocess.run([str(exe),str(Path(args.backend).resolve()),str(folder)+'/', '12','.7','model4'],env=env,cwd=out,capture_output=True,text=True,timeout=90)
    (out/'model-range.log').write_text(run.stdout+run.stderr,encoding='utf-8');print(run.stdout,flush=True)
    assert run.returncode==0 and 'MODEL_RANGE_PASS' in run.stdout,run.stderr
    raise SystemExit()
if args.range_only:
    folder=fp.out()/'displacement-gpu-20261022/terrain-sm3';env=fp.zig_env();env['NORTHLIGHT_TEST_MATERIAL_RANGE']='1'
    run=subprocess.run([str(exe),str(Path(args.backend).resolve()),str(folder)+'/', '12','.7'],env=env,cwd=out,capture_output=True,text=True,timeout=120)
    (out/'range-workload.log').write_text(run.stdout+run.stderr);print(run.stdout,flush=True)
    assert run.returncode==0 and 'MATERIAL_RANGE_WORKLOAD_PASS' in run.stdout,run.stderr
    records=[dict(gated=bool(int(m[0])),frames=600,texture_allocations=int(m[1]),generated_maps=int(m[2]),callbacks=int(m[3]),wall_ms=float(m[4])) for m in re.findall(r'MATERIAL_RANGE_WORKLOAD gated=(\d).*textureAllocations=(\d+) generatedMaps=(\d+) callbacks=(\d+) wallMs=([\d.]+)',run.stdout)]
    assert len(records)==2
    (out/'range-validation.json').write_text(json.dumps(records,indent=2)+'\n');raise SystemExit()
def frame(path):
    b=path.read_bytes();_,w,h,_,stride=struct.unpack_from('<5I',b)
    return np.frombuffer(b,dtype='<f4' if stride==16 else '<f2',offset=20).reshape(h,w,4).astype(np.float32)
records=[]
fixtures=[('terrain','3'),('terrain','2'),('wall','3'),('grass','3'),('composite','3'),('model','3')]
for family,model in fixtures:
    for cpu in (False,True):
        folder=out/(family+'-'+model+('-cpu' if cpu else '-gpu'));folder.mkdir(exist_ok=True)
        source=fp.out()/('model-materials-dxvk/3-weighted4-combiners_opaque' if family=='model' else 'displacement-gpu-20261022'/Path(family+'-sm'+model))
        for f in source.iterdir():
            if f.name.startswith('source.fgr') or f.suffix=='.bin':shutil.copy2(f,folder/f.name)
        env=fp.zig_env()
        if cpu:env['NORTHLIGHT_TEST_CPU_DISPLACEMENT']='1'
        else:env.pop('NORTHLIGHT_TEST_CPU_DISPLACEMENT',None)
        command=[str(exe),str(Path(args.backend).resolve()),str(folder)+'/', '12','.7']
        if family!='terrain':command.append('model4' if family=='model' else family)
        run=subprocess.run(command,env=env,cwd=out,capture_output=True,text=True,timeout=90)
        (folder/'gpu.log').write_text(run.stdout+run.stderr)
        assert run.returncode==0,run.stdout[-2000:]+run.stderr
        expected=0 if cpu or model=='2' or family=='composite' else 1
        assert f'heightGPU={expected}' in run.stdout,run.stdout
    a=out/(family+'-'+model+'-cpu');b=out/(family+'-'+model+'-gpu')
    before=frame(a/'geometry-on-depth.fgr');after=frame(b/'geometry-on-depth.fgr')
    valid=(before[:,:,3]>.01)&(after[:,:,3]>.01)
    depth=np.abs(before[:,:,2]/np.maximum(before[:,:,3],1e-6)-after[:,:,2]/np.maximum(after[:,:,3],1e-6))
    delta=np.abs(frame(a/'displacement-on.fgr')-frame(b/'displacement-on.fgr'))
    record=dict(family=family,shader_model=model,depth_p99=float(np.quantile(depth[valid],.99)),depth_mean=float(depth[valid].mean()),rgba_mean=float(delta.mean()))
    assert np.isfinite(after).all() and record['depth_p99']<.02 and record['depth_mean']<.003,record
    assert np.max(np.abs(frame(a/'displacement-zero.fgr')-frame(b/'displacement-zero.fgr')))<.0002
    records.append(record);print('GPU_HEIGHT_COMPARE_PASS',record,flush=True)
(out/'validation.json').write_text(json.dumps(records,indent=2)+'\n')
