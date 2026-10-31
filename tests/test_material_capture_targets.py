#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Actual capture flush: MRT continuity, near-only writes, viewport/depth and error restoration."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--headers',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
out=a.output or fp.output_dir()/'capture-targets';out.mkdir(parents=True,exist_ok=True)
headers=a.headers or fp.src('auto_bump_renderer.h').parent
reference=(fp.TESTS/'displacement_state_reuse.cpp').read_text().split('int main(){')[0]
reference=reference.replace('struct MaterialCaptureStateTestAccess {','struct MaterialCaptureStateTestAccess {\n static void replayBindings();static void replayConstantRanges();static void captureTargets();')
(out/'material_bindings_reference.h').write_bytes(reference.encode())
watch=[headers/'auto_bump_renderer.h',fp.TESTS/'displacement_state_reuse.cpp',fp.TESTS/'test_material_capture_targets.cpp',fp.TESTS/'test_material_batch_bindings.cpp',Path(__file__)]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();inputs={str(p):sha(p) for p in watch};runs=[]
for mode,flags in [('O2',['-O2']),('O0',['-O0'])]:
 exe=out/(mode+'.exe');command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG','-Wno-microsoft-exception-spec','-Wno-return-type',*flags,'-I',str(headers),'-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'test_material_capture_targets.cpp'),'-luser32','-luuid','-o',str(exe)]
 subprocess.run(command,env=fp.zig_env(),check=True);r=subprocess.run([str(exe)],capture_output=True,text=True);(out/(mode+'.log')).write_text(r.stdout+r.stderr);print((r.stdout+r.stderr)[-3000:],flush=True);r.check_returncode();assert 'CAPTURE_TARGETS_PASS' in r.stdout;runs.append(mode)
header=(headers/'auto_bump_renderer.h').read_text();negatives=[]
for name,old,new,reason in [
 ('far-write','packet->occlusion?15:0','15','CAPTURE_TARGET_WRITE_MISMATCH'),
 ('viewport','boundAuxiliary=attachAuxiliary;viewportKnown=false;','boundAuxiliary=attachAuxiliary;','CAPTURE_TARGET_VIEWPORT_MISMATCH'),
 ('up-mask','auxiliaryWritesKnown=false;}}','}}','CAPTURE_TARGET_WRITE_MISMATCH'),
 ('caps','auxiliary&&occlusionTarget&&independentCaptureWrites','auxiliary&&occlusionTarget','CAPTURE_TARGET_WRITE_MISMATCH')]:
 assert header.count(old)==1,(name,old);folder=out/('negative-'+name);folder.mkdir(exist_ok=True);(folder/'auto_bump_renderer.h').write_bytes(header.replace(old,new).encode());exe=folder/'test.exe'
 subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG','-O2','-Wno-microsoft-exception-spec','-Wno-return-type','-I',str(folder),'-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'test_material_capture_targets.cpp'),'-luser32','-luuid','-o',str(exe)],env=fp.zig_env(),check=True)
 r=subprocess.run([str(exe),*(['no-faults'] if name=='caps' else [])],capture_output=True,text=True);(folder/'result.log').write_text(r.stdout+r.stderr);assert r.returncode!=0 and reason in r.stderr,(name,r.stdout[-1000:],r.stderr);negatives.append(name)
assert all(sha(Path(p))==h for p,h in inputs.items())
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'modes':runs,'negative_controls':negatives,'game_launched':False,'GPU_used':False},indent=2)+'\n')
