#!/usr/bin/env python3
"""Focused native terrain/WMO seam oracle, with MSAA and immediate/deferred capture.

Uses an existing MPQ-derived fixture without rewriting it. Geometry is an authored
folded mesh, not a recorded game draw or a whole-scene FPS benchmark.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, shutil, subprocess, sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--fixture',type=Path,required=True,help='Prepared native terrain VS/PS, PreviewBase and MPQ-derived source.fgr mip chain')
parser.add_argument('--backend',type=Path,required=True)
parser.add_argument('--family',choices=['terrain','wall'],default='terrain')
parser.add_argument('--resolution',type=int,nargs=2,default=[1280,800],metavar=('WIDTH','HEIGHT'))
args=parser.parse_args()
repo=fp.REPO
out=fp.output_dir()/(args.family+'-material-seams');out.mkdir(parents=True,exist_ok=False)
spec=importlib.util.spec_from_file_location('fold_guard',repo/'tests/validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
backend=args.backend.resolve()
seed=args.fixture.resolve()
files=[*seed.glob('source.fgr*'),*(seed/name for name in ['terrain-vs.bin','terrain-ps.bin','PreviewBase.bin'])]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p):sha(p) for p in [*files,backend,repo/'tests/auto_normal_oblique_gpu.cpp',repo/'shaders/auto_bump_normal.hlsl',repo/'src/generated/world_compiled_shaders.h']}
env={k:v for k,v in os.environ.items() if not k.startswith(('NORTHLIGHT_','DXVK_','APITRACE_','TRACE_','RETRACE_'))}
for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache'),('__GL_SHADER_DISK_CACHE_PATH','driver-cache')]:
    (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
(out/'dxvk.conf').write_bytes(b'dxvk.allowFse = False\nd3d9.presentInterval = 0\n')
env.update(DXVK_CONFIG='d3d9.customVendorId = 1002; d3d9.cachedDynamicBuffers = True; d3d9.cachedWriteOnlyBuffers = True',DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DXVK_LOG_PATH=str(out))
report={'pass':False,'inputs':inputs,'commands':[],'game_launched':False,'family':args.family,'resolution':args.resolution,'scope':'Production native material/capture pipeline on authored folded mesh and preserved MPQ-derived inputs; not a game draw trace.'}
def run(label,command,extra=None):
    guard.processes();p=subprocess.run(list(map(str,command)),cwd=out,env={**env,**(extra or {})},capture_output=True,text=True,timeout=180)
    (out/(label+'.log')).write_text(p.stdout+p.stderr);report['commands'].append({'label':label,'command':list(map(str,command)),'exit_code':p.returncode})
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(label,p.returncode,(p.stdout+p.stderr)[-3000:],flush=True);assert p.returncode==0,label
try:
    exe=out/'native-fold.exe'
    run('compile',[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',f'-DNORTHLIGHT_OBLIQUE_WIDTH={args.resolution[0]}',f'-DNORTHLIGHT_OBLIQUE_HEIGHT={args.resolution[1]}',*fp.test_include_flags(),repo/'tests/auto_normal_oblique_gpu.cpp','-luser32','-o',exe])
    for samples in [0,2]:
        for deferred in [False,True]:
            for neutral in [True,False]:
                label=f'msaa{samples}-deferred{int(deferred)}-neutral{int(neutral)}';folder=out/label;folder.mkdir()
                for p in files:shutil.copyfile(p,folder/p.name)
                flags={'NORTHLIGHT_TEST_MSAA':str(samples),'NORTHLIGHT_TEST_MESH_GRID':'2','NORTHLIGHT_TEST_TERRAIN_FOLD':'.2','NORTHLIGHT_TEST_TERRAIN_FOLD_ONLY':'1','NORTHLIGHT_TEST_TERRAIN_SCENE_NORMALS':'1'}
                if deferred:flags['NORTHLIGHT_TEST_DEFERRED_CAPTURE']='1'
                if neutral:flags['NORTHLIGHT_TEST_TERRAIN_NEUTRAL']='1'
                run(label,[exe,backend,str(folder)+'/',*([12,.7,'wall'] if args.family=='wall' else [])],flags)
    report['pass']=True
finally:
    guard.processes();report['inputs_unchanged']=all(sha(Path(p))==h for p,h in inputs.items());report['pass']&=report['inputs_unchanged'];(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['pass']
