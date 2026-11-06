"""Compare pressure fallback and bounded demand residency, including live eviction.

Native MPQ-derived terrain shaders, authored mesh, exact color/normal/occlusion
comparison at MSAA 0/2. No game process is started.
"""
from pathlib import Path
import argparse,json,re,shutil,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--fixture',required=True,type=Path);p.add_argument('--backend',required=True,type=Path)
a=p.parse_args();out=fp.output_dir()/'material-pressure-policy';out.mkdir(parents=True,exist_ok=False)
env=fp.zig_env();env.update(NORTHLIGHT_TEST_PRESSURE='1',NORTHLIGHT_TEST_MESH_GRID='24',NORTHLIGHT_TEST_CAPTURE_DRAWS='40',NORTHLIGHT_TEST_DEFERRED_CAPTURE='1',NORTHLIGHT_TEST_MATERIAL_BATCH_TIMING='1',NORTHLIGHT_TEST_MATERIAL_BATCH_VALIDATE='1')
def run(label,command,flags=None):
    r=subprocess.run(list(map(str,command)),cwd=out,env={**env,**(flags or {})},capture_output=True,text=True,timeout=180)
    (out/(label+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');assert r.returncode==0,(label,r.stdout[-4000:],r.stderr[-4000:]);return r.stdout
exe=out/'pressure-policy.exe'
run('compile',[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_TEST_MATERIAL_PRESSURE_POLICY=1','-DNORTHLIGHT_OBLIQUE_WIDTH=320','-DNORTHLIGHT_OBLIQUE_HEIGHT=200',*fp.test_include_flags(),fp.TESTS/'auto_normal_oblique_gpu.cpp','-luser32','-o',exe])
files=[*a.fixture.glob('source.fgr*'),*(a.fixture/n for n in ['terrain-vs.bin','terrain-ps.bin','PreviewBase.bin'])];results=[]
for samples in [0,2]:
    folders={}
    for policy in ['critical','demand-small','demand']:
        label=f'msaa{samples}-{policy}';folder=out/label;folder.mkdir();folders[policy]=folder
        for f in files:shutil.copyfile(f,folder/f.name)
        flags={'NORTHLIGHT_TEST_MSAA':str(samples)}
        if policy!='critical':flags['NORTHLIGHT_TEST_DEMAND_PRESSURE']='1'
        if policy=='demand-small':flags['NORTHLIGHT_TEST_SMALL_PACKETS']='1'
        text=run(label,[exe,a.backend.resolve(),str(folder)+'/',12,.7],flags)
        if policy!='critical' and samples:assert 'MATERIAL_DEMAND_RETENTION_PASS' in text and 'MATERIAL_CRITICAL_RELEASE_PASS' in text
        timing=re.search(r'MATERIAL_BATCH_TIMING .*gpuMs=([\d.]+) submitMs=([\d.]+)',text);assert timing
        limits=re.search(r'MATERIAL_BATCH_LIMIT_PASS rejected=(\d+) packetLimit=(\d+) packetBytes=(\d+)',text);assert limits
        results.append(dict(msaa=samples,policy=policy,gpu_ms=float(timing[1]),submit_ms=float(timing[2]),forced_batches=int(limits[1]),packet_limit=int(limits[2]),packet_bytes=int(limits[3])))
    names=[f.name for f in folders['critical'].glob('batch-*.fgr')];assert len(names)>=2
    for name in names:
        for policy in ['demand-small','demand']:assert (folders['critical']/name).read_bytes()==(folders[policy]/name).read_bytes(),(samples,policy,name)
    if samples:
        critical,small,demand=results[-3:]
        assert demand['forced_batches']<small['forced_batches']==critical['forced_batches'],(critical,small,demand)
(out/'validation.json').write_text(json.dumps(dict(pass_=False,exact_color_normal_occlusion=True,cases=results,negative_controls_pending=True),indent=2)+'\n');print(json.dumps(results,indent=2))

# Counterfactuals retain the same fixture/API and restore each destructive path
# separately. A populated cache, rather than its configured cap, must survive.
header=fp.src('auto_bump_renderer.h').read_text()
controls=[('entry','deferredGPU.setLimit(on?pressureReplayLimit:32u<<20);','deferredGPU.setLimit(on?0:32u<<20);','MATERIAL_DEMAND_ENTRY_RETAIN'),
          ('trim','if(releaseReplay)deferredGPU.clear();','deferredGPU.clear();','MATERIAL_DEMAND_TRIM_RETAIN')]
for label,before,after,rejection in controls:
    assert header.count(before)==1
    folder=out/('control-'+label);folder.mkdir();(folder/'auto_bump_renderer.h').write_text(header.replace(before,after))
    control=folder/'pressure-policy.exe'
    run('compile-control-'+label,[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_TEST_MATERIAL_PRESSURE_POLICY=1','-DNORTHLIGHT_OBLIQUE_WIDTH=320','-DNORTHLIGHT_OBLIQUE_HEIGHT=200','-I',folder,*fp.test_include_flags(),fp.TESTS/'auto_normal_oblique_gpu.cpp','-luser32','-o',control])
    flags={'NORTHLIGHT_TEST_MSAA':'2','NORTHLIGHT_TEST_DEMAND_PRESSURE':'1'}
    result=subprocess.run(list(map(str,[control,a.backend.resolve(),str(folders['demand'])+'/',12,.7])),cwd=out,env={**env,**flags},capture_output=True,text=True,timeout=180)
    text=result.stdout+result.stderr;(out/('control-'+label+'.log')).write_text(text)
    assert result.returncode!=0 and rejection in text,(label,result.returncode,text[-4000:])
(out/'validation.json').write_text(json.dumps(dict(pass_=True,exact_color_normal_occlusion=True,cases=results,negative_controls=[c[0] for c in controls]),indent=2)+'\n')
