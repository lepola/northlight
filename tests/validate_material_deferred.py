#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Compare terrain/WMO/M2 capture, opaque coverage and full submission cost. Never starts WoW."""
from pathlib import Path
import argparse,json,re,shutil,statistics,struct,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import numpy as np

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--backend',required=True);p.add_argument('--benchmark',action='store_true')
p.add_argument('--family',choices=['models','terrain','wall'],default='models')
p.add_argument('--vertex-alpha-zero',action='store_true')
p.add_argument('--wall-diffuse',action='store_true',help='Exercise an opaque draw using the diffuse-alpha WMO pixel program')
p.add_argument('--alpha-test',action='store_true',help='Preserve native alpha coverage and exercise guarded texture-dependent captures')
p.add_argument('--texture-guard',action='store_true',help='Simulate the serialized production texture-write contract')
p.add_argument('--pressure',action='store_true',help='Exercise bounded capture batches with the production low-memory cache limits')
p.add_argument('--lazy-registration',action='store_true',help='Register the original VS with master materials Off, then enable before its first material draw')
p.add_argument('--baseline-ref',help='Compare the coverage defect / far admission with a previous commit')
p.add_argument('--occlusion-off',action='store_true',help='Match a normal-only capture without the auxiliary MRT')
p.add_argument('--reuse-exe',type=Path);p.add_argument('--quick',action='store_true')
p.add_argument('--output-subdir',default='material-deferred')
p.add_argument('--mesh-grid',type=int,default=0,help='Benchmark a buffered tessellated plane (side subdivisions)')
p.add_argument('--capture-draws',type=int,default=32,help='Native/capture draws per benchmark frame')
p.add_argument('--vary-pose',action='store_true',help='Change the four-bone pose between benchmark draws')
p.add_argument('--benchmark-resolution',type=int,nargs=2,default=[3840,2160],metavar=('WIDTH','HEIGHT'))
a=p.parse_args();out=fp.out()/a.output_subdir;out.mkdir(exist_ok=True)
backend=str(Path(a.backend).resolve())
# replay_bounds must accompany actor_deformation: its same-directory quoted
# include would otherwise load the current actor header beside the frozen one.
BASELINE_FILES=['src/water/auto_bump_renderer.h','src/water/auto_displacement_renderer.h','src/world/draw_snapshot.h','src/replay/replay_gpu_cache.h','src/replay/actor_deformation.h','src/replay/replay_bounds.h','src/generated/auto_bump_compiled_shaders.h']

def compile_fixture(width,height,name,override=None):
    exe=out/(name+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_MATERIAL_MIRROR',f'-DNORTHLIGHT_OBLIQUE_WIDTH={width}',f'-DNORTHLIGHT_OBLIQUE_HEIGHT={height}',*(['-DNORTHLIGHT_TEST_CAPTURE_BASELINE','-I'+str(override)] if override else []),*fp.test_include_flags(),str(fp.TESTS/'auto_normal_oblique_gpu.cpp'),'-luser32','-luuid','-o',str(exe)],env=fp.zig_env(),check=True)
    data=bytearray(exe.read_bytes());at=struct.unpack_from('<I',data,0x3c)[0]+22
    struct.pack_into('<H',data,at,struct.unpack_from('<H',data,at)[0]|0x20);exe.write_bytes(data)
    return exe

def run(exe,source,label,on,samples=2,**flags):
    folder=out/(label+'-'+str(on));folder.mkdir(exist_ok=True)
    for f in source.iterdir():
        if f.name.startswith('source.fgr') or f.name in ('terrain-vs.bin','terrain-ps.bin','PreviewBase.bin'):
            shutil.copy2(f,folder/f.name)
    if a.wall_diffuse:
        assert a.family=='wall'
        shutil.copy2(fp.out()/'bump-recon/shaders_pixel_ps_3_0_mapobjdiffuse.bls-0.bin',folder/'terrain-ps.bin')
    env=fp.zig_env()
    for k in list(env):
        if k.startswith('NORTHLIGHT_TEST_'):env.pop(k)
    env.pop('NORTHLIGHT_EXPERIMENTAL_SHARED_POM',None)
    env['DXVK_CONFIG']='d3d9.customVendorId = 1002; '+env.get('DXVK_CONFIG','')
    env['NORTHLIGHT_TEST_MSAA']=str(samples)
    env.update({'NORTHLIGHT_TEST_'+k:str(v) for k,v in flags.items()})
    if a.occlusion_off:env['NORTHLIGHT_TEST_MATERIAL_BATCH_NO_OCCLUSION']='1'
    if a.vertex_alpha_zero:env['NORTHLIGHT_TEST_VERTEX_ALPHA_ZERO']='1'
    if a.alpha_test:env['NORTHLIGHT_TEST_MATERIAL_ALPHA_TEST']='1'
    if a.texture_guard:env['NORTHLIGHT_TEST_TEXTURE_GUARD']='1'
    if a.pressure and not flags.get('DEFERRED_PRESSURE'):env['NORTHLIGHT_TEST_PRESSURE']='1'
    if a.lazy_registration:env['NORTHLIGHT_TEST_LAZY_VERTEX']='1'
    if flags.get('TEXTURE_GUARD_OFF'):env.pop('NORTHLIGHT_TEST_TEXTURE_GUARD',None)
    if on:env['NORTHLIGHT_TEST_DEFERRED_CAPTURE']='1'
    variant=['model4' if 'weighted4' in source.name else 'modelunsafe' if 'varying' in source.name else 'model'] if a.family=='models' else [] if a.family=='terrain' else ['wall']
    r=subprocess.run([str(exe),backend,str(folder)+'/', '12','.7',*variant],env=env,cwd=out,capture_output=True,text=True,timeout=180)
    (out/(label+'-'+str(on)+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
    assert r.returncode==0,(label,on,r.stdout[-3000:],r.stderr[-1500:])
    if a.lazy_registration:assert 'MATERIAL_LAZY_VERTEX_REGISTERED_OFF_ENABLED_BEFORE_FIRST_DRAW' in r.stdout
    return folder,r.stdout

def compare(folders,pattern):
    files=list(folders[0].glob(pattern));assert files,pattern
    for f in files:assert f.read_bytes()==(folders[1]/f.name).read_bytes(),(str(f), 'not bit exact')
    return len(files)

source=fp.out()/(('model-materials-dxvk/3-weighted4-combiners_mod_mod' if a.alpha_test else 'model-materials-dxvk/3-weighted4-combiners_opaque') if a.family=='models' else 'material-msaa-release/'+a.family)
assert source.exists(),('Prepare validate_model_materials.py fixtures first' if a.family=='models' else 'Prepare validate_parallax_msaa.py --output-subdir material-msaa-release fixtures first')
exe=a.reuse_exe.resolve() if a.reuse_exe else compile_fixture(320,200,'deferred-check')
records=[]
if not a.reuse_exe:
    sources=[source] if a.quick or a.family!='models' else sorted((fp.out()/'model-materials-dxvk').glob('[23]-*combiners*'))
    for fixture in sources:
        runs=[run(exe,fixture,'model-'+fixture.name,on) for on in [0,1]];pair=[r[0] for r in runs]
        if fixture==source:assert 'MATERIAL_DEFERRED_MODE mode=1 queued=1 flushed=1' in runs[1][1]
        count=compare(pair,'*.fgr');records.append(dict(fixture=fixture.name,outputs=count,bit_exact=True))
    if a.texture_guard and a.alpha_test:
        folder,log=run(exe,source,'texture-guard-off',1,TEXTURE_GUARD_OFF=1)
        assert 'MATERIAL_DEFERRED_MODE mode=1 queued=0 flushed=0' in log
        records.append(dict(safety='TEXTURE_GUARD_OFF',outputs=compare([out/('model-'+source.name+'-1'),folder],'*.fgr'),bit_exact=True))
    for samples in [0,4,8]:
        pair=[run(exe,source,'msaa-'+str(samples),on,samples)[0] for on in [0,1]]
        records.append(dict(msaa=samples,outputs=compare(pair,'*.fgr'),bit_exact=True))
        pair=[run(exe,source,'resident-msaa-'+str(samples),on,samples,MATERIAL_BATCH_TIMING=1,MATERIAL_BATCH_VALIDATE=1,DEFERRED_RESIDENT=1)[0] for on in [0,1]]
        records.append(dict(resident_msaa=samples,outputs=compare(pair,'batch-*.fgr'),bit_exact=True))
    for flag in ['DEFERRED_RESIDENT','DEFERRED_POSE','DEFERRED_STRIP','DEFERRED_CACHE_FAILURE','DEFERRED_MUTATION','DEFERRED_BARRIER','DEFERRED_UNUSED','DEFERRED_LIMIT','DEFERRED_MULTISTREAM']+(['DEFERRED_TEXTURE_WRITE'] if a.texture_guard and a.alpha_test else []):
        flags={flag:1}
        if flag in ['DEFERRED_CACHE_FAILURE','DEFERRED_POSE']:flags['DEFERRED_RESIDENT']=1
        runs=[run(exe,source,flag.lower(),on,MATERIAL_BATCH_TIMING=1,MATERIAL_BATCH_VALIDATE=1,**flags) for on in [0,1]];pair=[r[0] for r in runs]
        if flag=='DEFERRED_UNUSED':assert 'MATERIAL_DEFERRED_UNUSED_PASS' in runs[1][1]
        if flag=='DEFERRED_MULTISTREAM':assert 'MATERIAL_MULTISTREAM_PREFLIGHT_PASS' in runs[1][1]
        records.append(dict(safety=flag,outputs=compare(pair,'batch-*.fgr'),bit_exact=True))
    _,log=run(exe,source,'deferred-failure',1,MATERIAL_BATCH_TIMING=1,DEFERRED_FAILURE=1)
    assert 'MATERIAL_DEFERRED_FAILURE_PASS' in log
    records.append(dict(safety='DEFERRED_FAILURE',incomplete_capture_hidden=True))
    _,log=run(exe,source,'deferred-pressure',1,MATERIAL_BATCH_TIMING=1,DEFERRED_PRESSURE=1,DEFERRED_RESIDENT=1)
    assert 'MATERIAL_DEFERRED_PRESSURE_PASS' in log
    records.append(dict(safety='DEFERRED_PRESSURE',source_cache_released=True))
    _,log=run(exe,source,'deferred-profile',1,MATERIAL_PROFILE=1)
    assert 'stage=deferredFlush' in log and 'rawCaptured=' in log and 'complete=1' in log
    records.append(dict(profiling=True,complete_gpu_coverage=True))
    if a.baseline_ref:
        overrides=out/'baseline';overrides.mkdir(exist_ok=True)
        for file in BASELINE_FILES:
            (overrides/Path(file).name).write_bytes(subprocess.check_output(['git','show',a.baseline_ref+':'+file]))
        old=compile_fixture(320,200,'baseline-check',overrides.resolve())
        if a.family=='models' or a.lazy_registration:
            for fixture in sources:
                folder,_=run(old,fixture,'baseline-'+fixture.name,1)
                count=compare([out/('model-'+fixture.name+'-1'),folder],'*.fgr')
                records.append(dict(baseline_fixture=fixture.name,outputs=count,bit_exact=True))
        if a.family=='terrain' and not a.lazy_registration:
            timing=[]
            for build,binary in [('old',old),('new',exe)]:
                _,log=run(binary,source,'far-admission-'+build,0,FAR_ADMISSION=1)
                m=re.search(r'MATERIAL_FAR_ADMISSION .*cpuMs=([\d.]+) pixelQueries=(\d+) depthQueries=(\d+)',log);assert m
                timing.append(dict(build=build,cpu_ms=float(m[1]),pixel_queries=int(m[2]),depth_queries=int(m[3])))
            assert timing[0]['pixel_queries']==32000 and timing[1]['pixel_queries']==timing[1]['depth_queries']==0
            records.append(dict(far_admission=timing));print('MATERIAL_ADMISSION_COMPARISON',timing,flush=True)
        if a.family=='wall' and a.vertex_alpha_zero and not a.lazy_registration:
            folders=[run(binary,source,'opaque-alpha-'+label,0)[0] for label,binary in [('old',old),('new',exe)]]
            def pixels(path):
                data=path.read_bytes();_,w,h,_,stride=struct.unpack_from('<5I',data)
                return np.frombuffer(data,dtype='<f4' if stride==16 else '<f2',offset=20).reshape(h,w,4).astype(np.float32)
            assert (folders[0]/'native-normal-off.fgr').read_bytes()==(folders[1]/'native-normal-off.fgr').read_bytes()
            cover=[int((pixels(f/'geometry-off-depth.fgr')[:,:,3]>.02).sum()) for f in folders]
            assert cover[0]==0 and cover[1]>0,cover
            before,after=[float(np.max(np.abs(pixels(f/'normal-on.fgr')[:,:,:3]-pixels(f/'native-normal-off.fgr')[:,:,:3]))) for f in folders]
            assert after>max(.0001,before*1000),(before,after)
            records.append(dict(opaque_alpha_zero=dict(old_covered_pixels=cover[0],new_covered_pixels=cover[1],old_rgb_noise=before,normal_rgb_delta=after,native_color_bit_exact=True)))
            print('MATERIAL_OPAQUE_ALPHA_REGRESSION',records[-1],flush=True)
            for state in ['WMO_CUTOUT','WMO_BLEND']:
                pair=[run(binary,source,state.lower()+'-'+label,0,**{state:1})[0] for label,binary in [('old',old),('new',exe)]]
                records.append(dict(native_alpha_state=state,outputs=compare(pair,'*.fgr'),bit_exact=True))
    print('MATERIAL_DEFERRED_EQUIVALENCE',len(records),flush=True)
if a.benchmark or a.reuse_exe:
    exe=a.reuse_exe.resolve() if a.reuse_exe else compile_fixture(*a.benchmark_resolution,'deferred-bench')
    old_bench=None
    if a.baseline_ref:
        overrides=out/'baseline';overrides.mkdir(exist_ok=True)
        for file in BASELINE_FILES:
            (overrides/Path(file).name).write_bytes(subprocess.check_output(['git','show',a.baseline_ref+':'+file]))
        old_bench=compile_fixture(*a.benchmark_resolution,'baseline-bench',overrides.resolve())
    timings=[]
    for repeat in range(3):
        folders={}
        order=[0,1]+([2] if old_bench else [])
        for on in (order if repeat%2==0 else order[::-1]):
            flags=dict(MATERIAL_BATCH_TIMING=1,MATERIAL_BATCH_VALIDATE=1,MESH_GRID=a.mesh_grid,CAPTURE_DRAWS=a.capture_draws)
            if a.vary_pose:flags['DEFERRED_POSE']=1
            folder,log=run(old_bench if on==2 else exe,source,'timing-'+str(repeat)+('-baseline' if on==2 else ''),int(on!=0),**flags)
            m=re.search(r'MATERIAL_BATCH_TIMING .*gpuMs=([\d.]+) submitMs=([\d.]+)',log);assert m
            record=dict(repeat=repeat,deferred=on,gpu_ms=float(m[1]),cpu_ms=float(m[2]));timings.append(record);folders[on]=folder
            print('MATERIAL_DEFERRED_TIMING',record,flush=True)
        assert compare([folders[0],folders[1]],'batch-*.fgr')==(2 if a.occlusion_off else 3)
        if old_bench:assert compare([folders[1],folders[2]],'batch-*.fgr')==(2 if a.occlusion_off else 3)
    result=dict(backend=backend,occlusion_enabled=not a.occlusion_off,runs=timings,checks=records,targets_bit_exact=True)
    for on in [0,1]:result['deferred' if on else 'immediate']={key:statistics.median(r[key] for r in timings if r['deferred']==on) for key in ['gpu_ms','cpu_ms']}
    if old_bench:result['baseline_deferred']={key:statistics.median(r[key] for r in timings if r['deferred']==2) for key in ['gpu_ms','cpu_ms']}
    result['gpu_reduction_percent']=100*(1-result['deferred']['gpu_ms']/result['immediate']['gpu_ms'])
else:result=dict(backend=backend,checks=records)
result.update(family=a.family,vertex_alpha_zero=a.vertex_alpha_zero,wall_diffuse=a.wall_diffuse,baseline_ref=a.baseline_ref,alpha_test=a.alpha_test,texture_guard=a.texture_guard,pressure=a.pressure,lazy_registration=a.lazy_registration,mesh_grid=a.mesh_grid,capture_draws=a.capture_draws,vary_pose=a.vary_pose,benchmark_resolution=a.benchmark_resolution)
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('MATERIAL_DEFERRED_PASS',json.dumps(result),flush=True)
