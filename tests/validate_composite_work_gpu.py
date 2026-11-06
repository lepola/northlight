#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Compare final-composite neutral-input gates against preserved production shaders.

Prepared depth/lighting fixtures, plus a 4K timing case; never launches WoW.
"""
import argparse,hashlib,json,os,shutil,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from validate_removal_distance_gpu import fixture,image

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--reference-directory',type=Path,required=True)
ap.add_argument('--backend',type=Path,action='append',required=True)
ap.add_argument('--output',type=Path,required=True)
ap.add_argument('--candidate-directory',type=Path,default=fp.COMPILED)
ap.add_argument('--cases',default='edges,odd,sky,near-far,water,4k')
ap.add_argument('--masks',help='Comma-separated fixture-neutral fog/local/AO-alpha masks (bits 1/2/4).')
ap.add_argument('--entries',help='Comma-separated composite entry names.')
ap.add_argument('--base-hdr',action='store_true',help='Compare WorldBaseCompositeHdr against the ordinary HDR shader on proven neutral inputs.')
args=ap.parse_args();args.reference_directory=args.reference_directory.resolve();args.candidate_directory=args.candidate_directory.resolve();args.backend=[p.resolve() for p in args.backend]
out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
env={k:v for k,v in os.environ.items() if not k.startswith(('NORTHLIGHT_','DXVK_','TRACE_','RETRACE_','APITRACE_'))}
env.update(DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_LOG_PATH=str(out),DXVK_CONFIG='d3d9.customVendorId = 1002')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
entries=['WorldComposite','WorldCompositeHdr','WorldUnlitComposite','WorldUnlitCompositeHdr','WorldPrelitComposite','WorldPrelitCompositeHdr']
if args.entries:
 assert set(args.entries.split(','))<=set(entries)
 entries=args.entries.split(',')
if args.base_hdr:
 entries=['WorldCompositeHdr'];args.masks='7'
candidate_name=lambda entry:'WorldBaseCompositeHdr' if args.base_hdr else entry
if args.base_hdr and args.candidate_directory==fp.COMPILED.resolve():
 manifest=json.loads((fp.SHADERS/'world-shader-build.json').read_text())
 assert manifest['shaders']['WorldBaseCompositeHdr']['source_sha256']==sha(fp.SHADERS/'world_effects.hlsl'),'Recompile WorldBaseCompositeHdr before validating the changed source'
inputs={str(p):sha(p) for p in [fp.TESTS/'composite_distance_gpu.cpp',fp.TESTS/'removal_distance_gpu.cpp',fp.SHADERS/'world_effects.hlsl',fp.SHADERS/'world-shader-build.json',fp.src('world_renderer.h'),fp.src('legacy_fog.h'),*(args.candidate_directory/(candidate_name(e)+'.bin') for e in entries),*(args.reference_directory/(e+'.bin') for e in entries)]}
binary=out/'composite-work.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',str(fp.TESTS/'composite_distance_gpu.cpp'),'-luser32','-o',str(binary)],env=fp.zig_env(),check=True)
rows=[]
for name,w,h in [('edges',128,96),('odd',137,79),('sky',128,96),('near-far',137,79),('water',137,79),('fog',137,79),('dense-fog',137,79),('haze',137,79),('night',137,79),('coverage',137,79),('emission',137,79),('rain',137,79),('black-fog',137,79),('held-fog',137,79),('identity-fog',137,79),('float-source',137,79),('4k',3840,2160)]:
 if name not in args.cases.split(','):continue
 prefix=fixture(out,name,w,h);c=list(struct.unpack('<272f',Path(prefix+'constants.f32').read_bytes()))
 if name not in ('fog','dense-fog'):c[25*4:26*4]=[0,1,1,0]
 if name in ('black-fog','held-fog'):
  c[25*4:26*4]=[-.002,1,1,.35 if name=='held-fog' else 1]
  c[26*4:27*4]=[.3,.2,.4,0] if name=='held-fog' else [0,0,0,0]
 if name=='identity-fog':c[25*4:26*4]=[0,1,2,1];c[26*4:27*4]=[.3,.2,.4,0]
 if name=='float-source':image(Path(prefix+'scene.fgr'),w,h,113,'4e',((-.02 if x%3==0 else .4,.2,.1,1) for y in range(h) for x in range(w)))
 c[30*4]=float(name=='water')
 if name in ('haze','night','rain','coverage','emission'):
  c[34*4:35*4]=[.08,.17,.27,.9];c[57*4:58*4]=[0,10,.04,13.8];c[67*4+2:68*4]=[.6,.4];c[35*4+1:36*4]=[1.1,.8,.7];c[64*4+3]=-.01
 if name=='night':c[19*4:19*4+3]=[.03,.05,.09]
 Path(prefix+'constants.f32').write_bytes(struct.pack('<272f',*c))
 if name in ('coverage','emission'):
  image(Path(prefix+'coverage.fgr'),w,h,113,'4e',((.03*(x%5),.02*(y%4),.05,.15*(x%5)) for y in range(h) for x in range(w)))
  Path(prefix+'coverage-info.f32').write_bytes(struct.pack('<4f',1,float(name=='emission'),0,0))
 if name=='rain':image(Path(prefix+'rain.fgr'),w,h,113,'4e',((float((x+y)%3==0),0,0,0) for y in range(h) for x in range(w)))
 shutil.copyfile(prefix+'light.fgr',prefix+'temporal-light.fgr')
 image(Path(prefix+'ao.fgr'),w//2,h//2,113,'4e',((.02*(x%4),.01*(y%5),.03,.2+.1*((x+y)%8)) for y in range(h//2) for x in range(w//2)))
 if name=='water':image(Path(prefix+'water.fgr'),w,h,113,'4e',((12. if x<w//2 else 100.,1. if (x//9+y//11)%2 else 0.,0.,0.) for y in range(h) for x in range(w)))
 for mask in ([int(m) for m in args.masks.split(',')] if args.masks else [0,3] if name=='4k' else range(4)):
  assert 0<=mask<8
  image(Path(prefix+'ao.fgr'),w//2,h//2,113,'4e',((.02*(x%4),.01*(y%5),.03,1. if mask&4 else .2+.1*((x+y)%8)) for y in range(h//2) for x in range(w//2)))
  image(Path(prefix+'fog.fgr'),w//2,h//2,113,'4e',((0,0,0,1) if mask&1 else (.1*(x%4),.05*(y%5),.08,.6+.08*(x%3)) for y in range(h//2) for x in range(w//2)))
  image(Path(prefix+'current-local-light.fgr'),w//2,h//2,113,'4e',((0,0,0,0) if mask&2 else (.02*(x%4),.03*(y%5),.04,0) for y in range(h//2) for x in range(w//2)))
  Path(prefix+'lighting-layout.txt').write_text('separate-current-local-v1\ncurrentLocalLighting=1\n')
  for entry in ([e for e in entries if e in ['WorldComposite','WorldCompositeHdr']] if name=='4k' else entries):
   for backend in args.backend:
    label=f'composite-{name}-{entry}-{mask}'+('-hdr' if entry.endswith('Hdr') else '')
    from windows_gpu_guard import processes
    processes()
    p=subprocess.run([str(binary),prefix,str(args.reference_directory/(entry+'.bin')),str(args.candidate_directory/(candidate_name(entry)+'.bin')),label,str(fp.COMPILED/'WorldNormals.bin'),str(backend)],cwd=out,env={**env,'NORTHLIGHT_COMPOSITE_NEUTRAL':str(mask)},capture_output=True,text=True,timeout=180)
    (out/(label+'-'+backend.parent.name+'.log')).write_text(p.stdout+p.stderr)
    assert p.returncode==0,(label,p.stdout[-1200:],p.stderr[-1200:])
    row=next(json.loads(s) for s in p.stdout.splitlines() if s.startswith('{'));row.update(entry=entry,mask=mask,backend=str(backend))
    proofs=next(s for s in p.stdout.splitlines() if s.startswith('COMPOSITE_PROOFS '))
    row['rain_absent']='rainAbsent=1' in proofs;row['native_surface_shared']='nativeSurfaceShared=1' in proofs
    assert row['rain_absent']==(name!='rain')
    assert row['native_surface_shared']==(name not in ('fog','dense-fog','held-fog','float-source'))
    rows.append(row);print(entry,name,mask,'ratio',row['ratio'],'maxAbs',row['max_abs'],flush=True)
assert all(row['changed']==0 for row in rows),'pixel mismatch against preserved production shader'
report=dict(pass_=True,game_launched=False,fixture_pixels_only=True,inputs=inputs,rows=rows)
report['inputs_unchanged']=all(sha(Path(p))==v for p,v in inputs.items());assert report['inputs_unchanged']
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS composite work',len(rows),'comparisons')
