#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Compare shared POM against its unchanged raymarch, including real cache hits/misses.

Prepare client fixtures with validate_parallax_msaa.py --output-subdir shared-pom-msaa first.
Runs standalone offscreen D3D9 executables; never starts the game.
"""
from pathlib import Path
import argparse,json,re,shutil,struct,subprocess,sys
import numpy as np

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--backend',required=True)
p.add_argument('--samples',nargs='+',type=int,default=[0,2,4,8])
p.add_argument('--families',nargs='+',default=['terrain','wall','composite','grass'])
p.add_argument('--output-subdir',default='shared-pom-oracle')
p.add_argument('--color-only',action='store_true',help='Compare production images/state without the diagnostic hit-mask shader.')
a=p.parse_args()
out=fp.out()/a.output_subdir;out.mkdir(exist_ok=True)
exe=out/'shared-pom.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_OBLIQUE_WIDTH=320','-DNORTHLIGHT_OBLIQUE_HEIGHT=200','-DNORTHLIGHT_MATERIAL_MIRROR',*fp.test_include_flags(),str(fp.TESTS/'auto_normal_oblique_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
pe=bytearray(exe.read_bytes());flags=struct.unpack_from('<I',pe,0x3c)[0]+22
struct.pack_into('<H',pe,flags,struct.unpack_from('<H',pe,flags)[0]|0x20);exe.write_bytes(pe)
names=('normal-on','pom-on','pom-shadow','pom-shadow-cavity')
def image(path):
    raw=path.read_bytes();magic,w,h,fmt,stride=struct.unpack_from('<5I',raw);assert magic==0x31524746 and stride==16
    return np.frombuffer(raw,dtype='<f4',offset=20).reshape(h,w,4).copy()
records=[]
for family in a.families:
    folder=out/family;folder.mkdir(exist_ok=True)
    for f in (fp.out()/'shared-pom-msaa'/family).iterdir():
        if f.name.startswith('source.fgr') or f.name in ('terrain-vs.bin','terrain-ps.bin','PreviewBase.bin'):shutil.copy2(f,folder/f.name)
    def run(label,samples,options=()):
        env=fp.zig_env();env.update(NORTHLIGHT_TEST_MSAA=str(samples))
        for key in list(env):
            if key.startswith(('NORTHLIGHT_TEST_SHARED_','NORTHLIGHT_TEST_POM_ONLY')) or key=='NORTHLIGHT_TEST_NO_SHARED_POM':env.pop(key)
        for key in options:env[key]='1'
        env['NORTHLIGHT_EXPERIMENTAL_SHARED_POM']='1'
        if 'NORTHLIGHT_TEST_SHARED_DEFAULT' in options:env.pop('NORTHLIGHT_EXPERIMENTAL_SHARED_POM')
        result=subprocess.run([str(exe),a.backend,str(folder)+'/', '12','.7']+([family] if family!='terrain' else []),env=env,cwd=out,capture_output=True,text=True,timeout=90)
        (out/f'{family}-{samples}-{label}.log').write_text(result.stdout+result.stderr)
        assert result.returncode==0,(family,samples,label,result.stdout[-2000:],result.stderr[-2000:])
        m=re.search(r'POM_SHARED_TEST produced=(\d+) native=(\d+) clears=(\d+)',result.stdout);assert m
        return {name:image(folder/(name+'.fgr')) for name in names},list(map(int,m.groups()))
    for samples in a.samples:
        baseline,_=run('legacy',samples,['NORTHLIGHT_TEST_NO_SHARED_POM'])
        default,counters=run('default',samples,['NORTHLIGHT_TEST_SHARED_DEFAULT'])
        assert counters==[0,0,0] and all(float(np.max(np.abs(default[name]-baseline[name])))<.0002 for name in names),(family,samples,'default',counters)
        failure,counters=run('shader-failure',samples,['NORTHLIGHT_TEST_SHARED_SHADER_FAILURE'])
        assert counters[:2]==[0,0] and all(float(np.max(np.abs(failure[name]-baseline[name])))<.0002 for name in names),(family,samples,'shader-failure',counters)
        for label,options in [('shared',()),('holes',['NORTHLIGHT_TEST_SHARED_CLEAR_HALF']),('wrap',['NORTHLIGHT_TEST_SHARED_WRAP']),('buffered',['NORTHLIGHT_TEST_SHARED_BUFFERED'])]:
            actual,counters=run(label,samples,options)
            assert counters[0]==3 and counters[1]==3,(family,samples,label,counters)
            if label=='wrap':assert counters[2]>=2
            deltas={name:float(np.max(np.abs(actual[name]-baseline[name]))) for name in names}
            assert max(deltas.values())<.0002,(family,samples,label,deltas)
            records.append(dict(family=family,samples=samples,scenario=label,max_deltas=deltas,produced_draws=counters[0],native_read_draws=counters[1]))
        if family=='composite':
            old,_=run('address-legacy',samples,['NORTHLIGHT_TEST_NO_SHARED_POM','NORTHLIGHT_TEST_SHARED_ADDRESS_MISMATCH'])
            new,counters=run('address-mismatch',samples,['NORTHLIGHT_TEST_SHARED_ADDRESS_MISMATCH'])
            assert counters[0]==3 and counters[1]==0,counters
            assert all(float(np.max(np.abs(old[name]-new[name])))<.0002 for name in names)
        none,_=run('pom-only-disabled',samples,['NORTHLIGHT_TEST_POM_ONLY','NORTHLIGHT_TEST_POM_ONLY_DISABLED'])
        only,counters=run('pom-only',samples,['NORTHLIGHT_TEST_POM_ONLY'])
        delta=float(np.max(np.abs(none['pom-on']-only['pom-on'])))
        assert delta>.005 and counters[:2]==[0,0],(family,samples,delta,counters)
        records.append(dict(family=family,samples=samples,scenario='pom-only',pom_delta=delta,material_capture=False))
    # A production-patched native shader exposes its otherwise unused hit lane.
    # Erase half the cache to require both actual hits and the full raymarch fallback.
    if a.color_only:continue
    _,counters=run('hit-mask',2,['NORTHLIGHT_TEST_SHARED_CLEAR_HALF','NORTHLIGHT_TEST_SHARED_HIT_MASK'])
    mask=image(folder/'shared-hit-mask.fgr');background=np.array([24,33,45],dtype=np.float32)/255
    remainder=mask[:,:,2]/background[2]
    hits=mask[:,:,0]-background[0]*remainder;misses=mask[:,:,1]-background[1]*remainder
    hit_pixels=int(np.count_nonzero(hits>.05));miss_pixels=int(np.count_nonzero(misses>.05))
    assert hit_pixels>100 and miss_pixels>100,(family,hit_pixels,miss_pixels)
    records.append(dict(family=family,samples=2,scenario='hit-mask',hit_pixels=hit_pixels,miss_pixels=miss_pixels))
    print('SHARED_POM_GPU_PASS',family,'hit_pixels',hit_pixels,'miss_pixels',miss_pixels,flush=True)
(out/'validation.json').write_text(json.dumps(records,indent=2)+'\n')
print('SHARED_POM_EQUIVALENCE_PASS comparisons='+str(sum('max_deltas' in r for r in records)*len(names)),flush=True)
