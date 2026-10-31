# northlight-test: requires=windows-exe,zig,world-cache
"""Frozen ac966ce plan/page oracle on actual Northrend FGS geometry; no game."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser();p.add_argument('--benchmark',action='store_true');p.add_argument('--baseline-ref',default='ac966ce');p.add_argument('--output',type=Path);a=p.parse_args()
out=a.output or fp.output_dir();out.mkdir(parents=True,exist_ok=True)
baseline=subprocess.check_output(['git','show',a.baseline_ref+':src/world/world_mesh_plan.cpp']);(out/'frozen_world_mesh_plan.cpp').write_bytes(baseline.replace(b'bool buildUploadPlan(',b'bool buildFrozenUploadPlan('))
terrain=subprocess.check_output(['git','show',a.baseline_ref+':src/shadows/shadow_terrain.h']);(out/'frozen_shadow_terrain.h').write_bytes(terrain.replace(b'namespace NorthlightShadowTerrain {',b'namespace FrozenShadowTerrain {').replace(b'NorthlightWorldMesh::buildUploadPlan(',b'NorthlightWorldMesh::buildFrozenUploadPlan('))
exe=out/'shared-mesh.exe';command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'test_shared_mesh_preparation.cpp'),str(fp.src('world_mesh_plan.cpp')),str(fp.src('world_gi.cpp')),str(out/'frozen_world_mesh_plan.cpp'),'-o',str(exe)]
subprocess.run(command,env=fp.zig_env(),check=True)
result=subprocess.run([str(exe),str(fp.world_cache()),*(['benchmark'] if a.benchmark else [])],capture_output=True,text=True,encoding='utf-8',errors='replace');print(result.stdout+result.stderr,end='');(out/'shared-mesh.log').write_bytes((result.stdout+result.stderr).encode())
rows=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in result.stdout.splitlines() if line.startswith('SHARED_MESH ')]
report={'baseline':a.baseline_ref,'baseline_plan_sha256':hashlib.sha256(baseline).hexdigest(),'baseline_terrain_sha256':hashlib.sha256(terrain).hexdigest(),'exit_code':result.returncode,'samples':rows,'benchmark_requested':a.benchmark,'game_launched':False,'source_sha256':{n:hashlib.sha256(fp.src(n).read_bytes()).hexdigest() for n in ['world_mesh_plan.cpp','world_mesh_plan.h','world_mesh_pages.h','shadow_terrain.h','world_gi.cpp','world_gi.h']}}
(out/'shared-mesh-validation.json').write_bytes((json.dumps(report,indent=2)+'\n').encode());result.check_returncode()
