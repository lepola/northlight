#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe,client,stormlib manual
"""Exact immutable source-envelope reuse across reconstructed rotation captures."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,struct,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures
p=argparse.ArgumentParser(__doc__);p.add_argument('--before',type=Path);p.add_argument('--ubsan',action='store_true');p.add_argument('--asan',dest='ubsan',action='store_true',help='Deprecated alias: Windows Zig run is UBSan only, not ASan.');p.add_argument('--backend',type=Path);a=p.parse_args()
if '--asan' in sys.argv:print('NOTE: Windows Zig --asan alias runs UBSan only; real ASan requires the separate GCC/runtime-verified runner.',file=sys.stderr)
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True);client_fixtures.actor_client_programs(out)
code=client_fixtures.four_bone_vs3().read_bytes();words=struct.unpack('<%dI'%(len(code)//4),code)
with (out/'actor_client_programs.h').open('a') as f:f.write('\nnamespace ClientShaders {static const std::uint32_t FourBoneVs3[]={'+','.join(hex(w) for w in words)+'};}\n')
results={}
inputs={str(fp.src(name)):hashlib.sha256(fp.src(name).read_bytes()).hexdigest() for name in ['replay_envelopes.inl','replay_bounds.h','replay_bounds_job.h','replay_skin_envelope.inl','source_identity.h','draw_snapshot.h','point_light_shadow.h']}
commands=[]
branches=([('before',0),('current',1)] if a.before else [('current',1)])+([('current-ubsan',1)] if a.ubsan else [])
for name,reuse in branches:
 overlay=out/name;overlay.mkdir(exist_ok=True)
 if not reuse:
  for file in ('replay_bounds.h','replay_bounds_job.h'):shutil.copyfile(fp.src(file),overlay/file)
  shutil.copyfile(a.before,overlay/'replay_envelopes.inl')
 exe=overlay/'replay-source-identity.exe'
 flags=['-g','-fsanitize=undefined','-fno-sanitize-recover=all'] if name.endswith('ubsan') else []
 command=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra',*flags,f'-DNORTHLIGHT_EXPECT_SOURCE_REUSE={reuse}','-I'+str(overlay),'-I'+str(out),*fp.test_include_flags(),fp.TESTS/'replay_source_identity.cpp','-o',exe]
 commands.append(list(map(str,command)))
 with (overlay/'compile.log').open('w') as log:subprocess.run(list(map(str,command)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
 r=subprocess.run(list(map(str,[exe,overlay/'rotation.tsv'])),capture_output=True,text=True,check=False,timeout=180);(overlay/'cpu.log').write_text(r.stdout+r.stderr);print(name,r.stdout+r.stderr,flush=True);r.check_returncode()
 results[name]={k:float(v) if '.' in v else int(v) for k,v in re.findall(r'(\w+)=(\d+(?:\.\d+)?)',r.stdout)}
if a.before:assert results['before']['cubeFaceDraws']>results['current']['cubeFaceDraws']*3 and results['current']['buildVertices']==0
if a.backend:
 exe=out/'source-envelope-gpu.exe';command=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-I'+str(out),*fp.test_include_flags(),fp.TESTS/'replay_source_identity_gpu.cpp','-luser32','-o',exe]
 commands.append(list(map(str,command)))
 with (out/'gpu-compile.log').open('w') as log:subprocess.run(list(map(str,command)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
 r=subprocess.run(list(map(str,[exe,a.backend.resolve(),out])),cwd=out,env=fp.zig_env(),capture_output=True,text=True,timeout=180);(out/'gpu.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,flush=True);r.check_returncode();results['gpu']={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',r.stdout)}
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest for path,digest in inputs.items())
(out/'summary.json').write_text(json.dumps(dict(pass_=True,inputs_sha256=inputs,before_sha256=hashlib.sha256(a.before.read_bytes()).hexdigest() if a.before else None,commands=commands,results=results,game_launched=False,production_dll_built=False),indent=2)+'\n')
