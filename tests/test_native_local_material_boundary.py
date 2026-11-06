#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib manual
"""Actual material raw-submit boundary; private geometry/POM bypass and exact images."""
from pathlib import Path
import argparse,os,re,shutil,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets
from extract_signatures import variants
p=argparse.ArgumentParser(__doc__);p.add_argument('--fixture',required=True,type=Path);p.add_argument('--backend',required=True,type=Path);a=p.parse_args()
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True);exe=out/'native-material-boundary.exe'
subprocess.run(list(map(str,[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_OBLIQUE_WIDTH=320','-DNORTHLIGHT_OBLIQUE_HEIGHT=200',*fp.test_include_flags(),fp.TESTS/'auto_normal_oblique_gpu.cpp','-luser32','-o',exe])),env=fp.zig_env(),check=True,timeout=240)
files=[*a.fixture.glob('source.fgr*'),a.fixture/'PreviewBase.bin']
assets=Assets(archives='stock')
try:programs={name:next(variants(assets.read(path)))[2] for name,path in [('terrain-vs.bin',r'shaders\vertex\vs_3_0\terrain.bls'),('terrain-ps.bin',r'shaders\pixel\ps_3_0\terrain1.bls')]}
finally:assets.close()
for samples,shared in ((0,1),(2,1),(0,0)):
 runs=[]
 for boundary in (0,1):
  run=out/f'msaa{samples}-shared{shared}-boundary{boundary}';run.mkdir(exist_ok=True);runs.append(run)
  for source in files:shutil.copyfile(source,run/source.name)
  for name,code in programs.items():(run/name).write_bytes(code)
  env={**fp.zig_env(),'NORTHLIGHT_TEST_MSAA':str(samples),'NORTHLIGHT_EXPERIMENTAL_SHARED_POM':'1','DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(run),'DXVK_CONFIG':'d3d9.customVendorId = 1002'}
  if not shared:env['NORTHLIGHT_TEST_NO_SHARED_POM']='1'
  if boundary:env['NORTHLIGHT_TEST_NATIVE_SLOT_BOUNDARY']='1'
  result=subprocess.run(list(map(str,[exe,a.backend.resolve(),str(run)+'/',12,.7])),env=env,cwd=run,capture_output=True,text=True,timeout=180)
  (run/'gpu.log').write_text(result.stdout+result.stderr);assert result.returncode==0,result.stdout[-6000:]+result.stderr
  if boundary:
   rows=[dict((k,int(v)) for k,v in re.findall(r'(\w+)=(\d+)',line)) for line in result.stdout.splitlines() if line.startswith('NATIVE_RAW_BOUNDARY')]
   assert any(r['rawCalls']==1 for r in rows) and any(r['preparedGeometry']==1 and r['rawCalls']==0 for r in rows),rows
   assert any(r['sharedPOM']==1 and r['rawCalls']==0 for r in rows) if shared else any(r['privateVS']==1 and r['rawCalls']==1 for r in rows)
   print('NATIVE_MATERIAL_BOUNDARY',samples,rows)
 images=0
 for source in runs[0].glob('*.fgr'):
  if source.name=='source.fgr':continue
  assert source.read_bytes()==(runs[1]/source.name).read_bytes(),source.name;images+=1
 assert images>=8
 print(f'NATIVE_MATERIAL_BOUNDARY_PASS msaa={samples} shared={shared} exactImages={images} rawAfterBinding=1 privateGeometryAndSharedPOM=bypass')
