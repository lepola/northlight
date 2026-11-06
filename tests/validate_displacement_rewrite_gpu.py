"""Replay unchanged tracked-buffer writes with native MPQ-derived terrain shaders.

Checks private subdivision reuse and exact color/depth at MSAA 0/2 and under
memory pressure. The mesh is authored; this is not a recorded game draw trace.
"""
from pathlib import Path
import argparse,json,os,re,shutil,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser(__doc__)
p.add_argument('--fixture',required=True,type=Path)
p.add_argument('--backend',required=True,type=Path)
p.add_argument('--before-header',type=Path,help='Optional preserved production header for the rebuild counterfactual')
p.add_argument('--position-bounds',action='store_true',help='Also check tracked position-only range admission on real buffers')
args=p.parse_args()
out=fp.output_dir()/'displacement-rewrite';out.mkdir(parents=True,exist_ok=False)
env=fp.zig_env();env['NORTHLIGHT_TEST_GEOMETRY_REWRITE']='1'
files=[*args.fixture.glob('source.fgr*'),*(args.fixture/n for n in ['terrain-vs.bin','terrain-ps.bin','PreviewBase.bin'])]
results=[]

def run(label,command,flags=None):
    result=subprocess.run(list(map(str,command)),cwd=out,env={**env,**(flags or {})},capture_output=True,text=True,timeout=180)
    (out/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode==0,(label,result.stdout[-4000:],result.stderr[-4000:])
    return result.stdout

executables={}
for variant in (['before','current'] if args.before_header else ['current']):
    exe=out/(variant+'.exe');extra=[]
    if variant=='before':
        headers=out/'before';headers.mkdir();shutil.copyfile(args.before_header,headers/'auto_displacement_renderer.h')
        shutil.copyfile(fp.src('auto_bump_renderer.h'),headers/'auto_bump_renderer.h');extra=['-I',headers]
    else:extra=['-DNORTHLIGHT_TEST_GEOMETRY_REWRITE_CURRENT=1']
    if variant=='current' and args.position_bounds:extra+=['-DNORTHLIGHT_TEST_POSITION_BOUNDS_CURRENT=1']
    run('compile-'+variant,[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',
        '-DNORTHLIGHT_OBLIQUE_WIDTH=320','-DNORTHLIGHT_OBLIQUE_HEIGHT=200',*extra,*fp.test_include_flags(),fp.TESTS/'auto_normal_oblique_gpu.cpp','-luser32','-o',exe])
    executables[variant]=exe

for samples in [0,2]:
    for pressure in [False,True]:
        folders={}
        for variant,exe in executables.items():
            label=f'{variant}-msaa{samples}-pressure{int(pressure)}';folder=out/label;folder.mkdir();folders[variant]=folder
            for source in files:shutil.copyfile(source,folder/source.name)
            flags={'NORTHLIGHT_TEST_MSAA':str(samples)}
            if pressure:flags['NORTHLIGHT_TEST_PRESSURE']='1'
            text=run(label,[exe,args.backend.resolve(),str(folder)+'/',12,.7],flags)
            match=re.search(r'DISPLACEMENT_REWRITE_GPU_PASS rewrites=32 rebuilds=(\d+) cpuMs=([\d.]+)',text);assert match,text
            results.append(dict(variant=variant,msaa=samples,pressure=pressure,rebuilds=int(match[1]),cpu_ms=float(match[2])))
        if 'before' in folders:
            for name in ['displacement-indexed.fgr','geometry-on-depth.fgr']:
                assert (folders['before']/name).read_bytes()==(folders['current']/name).read_bytes(),(samples,pressure,name)
if args.position_bounds:
    del env['NORTHLIGHT_TEST_GEOMETRY_REWRITE']
    for samples in [0,2]:
        label=f'position-range-msaa{samples}';folder=out/label;folder.mkdir()
        for source in files:shutil.copyfile(source,folder/source.name)
        text=run(label,[executables['current'],args.backend.resolve(),str(folder)+'/',12,.7],{'NORTHLIGHT_TEST_MSAA':str(samples),'NORTHLIGHT_TEST_RANGE_STREAMS':'1'})
        assert 'RANGE_STREAM_TRACKING_PASS' in text
        results.append(dict(position_range=True,msaa=samples,passed=True))
print(json.dumps(results,indent=2))
(out/'validation.json').write_text(json.dumps({'pass':True,'cases':results,'exact_counterfactual':bool(args.before_header)},indent=2)+'\n')
