"""Native M2 combiners, real bone palettes, cached geometry and normal/POM replay."""
from pathlib import Path
import argparse,json,re,shutil,struct,subprocess,sys
import numpy as np
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
import northlight_paths as fp
sys.path.insert(0,str(root/'renderer'))
from world_scene_builder import Assets
from extract_signatures import variants
p=argparse.ArgumentParser();p.add_argument('--backend',required=True);p.add_argument('--quick',action='store_true');args=p.parse_args()
name='native' if 'windows' in args.backend.lower() else 'dxvk';out=fp.out()/('model-materials-'+name);out.mkdir(exist_ok=True)
# Extract authentic mounted-client fixtures read-only, including four-bone skinning.
recon=fp.out()/'material-recon';recon.mkdir(exist_ok=True);assets=Assets()
try:
    for model in ('2','3'):
        codes={i:code for i,_,code in variants(assets.read(f'shaders\\vertex\\vs_{model}_0\\diffuse_t1.bls'))}
        for skin,index in [('rigid',1),('skinned',11),('weighted',21),('weighted4',51),('varying',51)]:
            (recon/f'model-vs{model}-{skin}.bin').write_bytes(codes[index])
    for family in ('combiners_opaque','combiners_mod','combiners_opaque_mod','combiners_mod_mod'):
        for i,_,code in variants(assets.read(f'shaders\\pixel\\ps_3_0\\{family}.bls')):
            (recon/f'{family}-{i}.bin').write_bytes(code)
finally:assets.close()

env=fp.zig_env();env.update(TEMP=str(fp.out()/'tmp'),TMP=str(fp.out()/'tmp'))
exe=out/'models.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-DNORTHLIGHT_MATERIAL_MIRROR','-DNORTHLIGHT_OBLIQUE_WIDTH=320','-DNORTHLIGHT_OBLIQUE_HEIGHT=200',*fp.test_include_flags(),str(root/'tests/auto_normal_oblique_gpu.cpp'),'-luser32','-luuid','-o',str(exe)],env=env,check=True)
def frame(path):
    b=path.read_bytes();magic,w,h,fmt,stride=struct.unpack_from('<5I',b);assert magic==0x31524746
    return np.frombuffer(b,dtype='<f4' if stride==16 else '<f2',offset=20).reshape(h,w,4).astype(np.float32)
records=[]
for model in (('3',) if args.quick else ('3','2')):
    for skin in (('weighted',) if args.quick else ('rigid','skinned','weighted','weighted4','varying')):
        for combiner in (('combiners_opaque',) if args.quick else ('combiners_opaque','combiners_mod','combiners_opaque_mod','combiners_mod_mod')):
            folder=out/(model+'-'+skin+'-'+combiner);folder.mkdir(exist_ok=True)
            for source in (fp.out()/'oblique-cobble').iterdir():
                if source.name.startswith('source.fgr') or source.name=='PreviewBase.bin':shutil.copy2(source,folder/source.name)
            if skin=='varying':
                # A flat .5 height field isolates pose inflation from all relief.
                for fixture in folder.glob('source.fgr*'):
                    data=bytearray(fixture.read_bytes());data[20:]=bytes([128,128,128,255])*((len(data)-20)//4);fixture.write_bytes(data)
            shutil.copy2(fp.out()/f'material-recon/model-vs{model}-{skin}.bin',folder/'terrain-vs.bin')
            shutil.copy2(fp.out()/f'material-recon/{combiner}-0.bin',folder/'terrain-ps.bin')
            run=subprocess.run([str(exe),str(Path(args.backend).resolve()),str(folder)+'/', '12','.7','modelunsafe' if skin=='varying' else 'model4' if skin=='weighted4' else 'model'],cwd=out,env=env,capture_output=True,text=True,timeout=90)
            (folder/'gpu.log').write_text(run.stdout+run.stderr)
            if run.returncode:raise RuntimeError(str(folder)+'\n'+run.stdout[-2500:]+run.stderr[-1000:])
            assert 'DISPLACEMENT_PREPARED mode=9' in run.stdout
            # The separately asserted one-build reuse probe runs only for skinned fixtures.
            probeBuilds=1 if 'MATERIAL_CONSTANT_REUSE_PASS' in run.stdout else 0
            assert len(re.findall(r'DISPLACEMENT build domain=4 ',run.stdout))==2+probeBuilds,'Pose or unused secondary UVs rebuilt the mesh'
            normal=frame(folder/'normal-on.fgr');geometry=frame(folder/'displacement-on.fgr');assert np.isfinite(geometry).all()
            for alt in ('displacement-indexed','displacement-coverage'):assert np.max(np.abs(frame(folder/(alt+'.fgr'))-geometry))<.0002
            assert np.max(np.abs(frame(folder/'displacement-zero.fgr')-normal))<.0002
            assert np.max(np.abs(frame(folder/'displacement-animated.fgr')-geometry))>.001
            assert (np.max(np.abs(frame(folder/'pom-on.fgr')-normal))<.0002 if skin=='varying' else np.max(np.abs(frame(folder/'pom-on.fgr')-normal))>.0001)
            before=frame(folder/'geometry-off-depth.fgr');after=frame(folder/'geometry-on-depth.fgr');valid=(before[:,:,3]>.01)&(after[:,:,3]>.01);assert valid.any()
            # This two-triangle skinned patch has only seam vertices: its native
            # surface must survive, while normal/POM shading remains active.
            delta=np.abs(before[:,:,2]/np.maximum(before[:,:,3],1e-6)-after[:,:,2]/np.maximum(after[:,:,3],1e-6));assert (delta[valid].max()>.0001 if skin=='rigid' else delta[valid].max()<.0002)
            record={'shader_model':model,'skin':skin,'combiner':combiner,'max_view_depth_change':float(delta[valid].max()),'native_seam_surface_preserved':skin!='rigid','native_animated_surface_preserved':skin=='varying','pose_changed':True,'bindings_and_all_vs_constants_restored':True}
            records.append(record);print('MODEL_MATERIAL_PASS',record,flush=True)
(out/'validation.json').write_text(json.dumps(records,indent=2)+'\n')
