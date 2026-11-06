"""Actual native Terrain/WMO geometry, depth, silhouette and settings checks; never starts WoW."""
from pathlib import Path
import argparse,json,shutil,struct,subprocess,sys
import numpy as np
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
import northlight_paths as fp
sys.path.insert(0,str(root/'renderer'))
from world_scene_builder import Assets,decode_blp
parser=argparse.ArgumentParser();parser.add_argument('--backend',required=True);parser.add_argument('--output-subdir',default='displacement');parser.add_argument('--families',nargs='+',default=['terrain','wall','composite','grass']);args=parser.parse_args()
out=fp.out()/args.output_subdir;out.mkdir(exist_ok=True)
env=fp.zig_env();env.update(TEMP=str(fp.out()/'tmp'),TMP=str(fp.out()/'tmp'))
def compile_test(name,target='x86-windows-gnu'):
    exe=out/(name+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target',target,'-O2','-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(root/'tests'/(name+'.cpp')),'-luser32','-o',str(exe)],env=env,check=True)
    return exe
cpu=compile_test('test_displacement_geometry');subprocess.run([str(cpu)],env=env,check=True)
snapshot=out/'displacement-snapshot.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-I'+str(root/'tests/displacement-snapshot'),*fp.test_include_flags(),str(root/'tests/displacement-snapshot/test.cpp'),'-o',str(snapshot)],env=env,check=True)
subprocess.run([str(snapshot)],env=env,check=True)
gpu=compile_test('auto_normal_oblique_gpu')
def frame(path):
    data=path.read_bytes();magic,w,h,fmt,stride=struct.unpack_from('<5I',data);assert magic==0x31524746
    return np.frombuffer(data,dtype='<f4' if stride==16 else '<f2',offset=20).reshape(h,w,4).astype(np.float32)
records=[]
for family in args.families:
    for model in (('3',) if family=='composite' else ('3','2')):
        folder=out/(family+'-sm'+model);folder.mkdir(exist_ok=True)
        for p in (fp.out()/'oblique-cobble').iterdir():
            if p.name.startswith('source.fgr') or p.name=='PreviewBase.bin':shutil.copy2(p,folder/p.name)
        if family=='grass':
            assets=Assets()
            try:data=assets.read('world\\nodxt\\detail\\silvermoondetaildoodad01.blp')
            finally:assets.close()
            level=0
            while True:
                w,h,pixels=decode_blp(data,max(1,256>>level));bgra=bytearray(pixels);bgra[0::4]=pixels[2::4];bgra[2::4]=pixels[0::4];payload=struct.pack('<5I',0x31524746,w,h,21,4)+bgra
                (folder/('source.fgr.'+str(level))).write_bytes(payload)
                if not level:(folder/'source.fgr').write_bytes(payload)
                if w==h==1:break
                level+=1
        vertex='detaildoodad' if family=='grass' else 'terrain' if family=='terrain' else 'mapobjspecular_t1' if family=='composite' else 'mapobjdiffuse_t1'
        pixel='detaildoodad' if family=='grass' else 'terrain1' if family=='terrain' else 'mapobjcomposite' if family=='composite' else 'mapobjopaque'
        shutil.copy2(fp.out()/'bump-recon'/(f'shaders_vertex_vs_{model}_0_{vertex}.bls-'+('30' if family=='composite' else '0')+'.bin'),folder/'terrain-vs.bin')
        shutil.copy2(fp.out()/'bump-recon'/f'shaders_pixel_ps_3_0_{pixel}.bls-0.bin',folder/'terrain-ps.bin')
        for profile in (False,True):
            command=[str(gpu),str(Path(args.backend).resolve()),str(folder)+'/', '1' if profile else '12',('.005' if family=='composite' else '.02') if profile else '.7']+([family] if family!='terrain' else [])
            run=subprocess.run(command,env=env,cwd=out,capture_output=True,text=True)
            (folder/('profile.log' if profile else 'oblique.log')).write_text(run.stdout+run.stderr)
            if run.returncode:raise RuntimeError(str(folder)+' '+run.stdout[-1500:]+run.stderr[-1500:])
            assert 'DISPLACEMENT_PREPARED' in run.stdout
            original=frame(folder/'normal-on.fgr');displaced=frame(folder/'displacement-on.fgr')
            assert np.isfinite(displaced).all()
            for name in ['displacement-indexed','displacement-coverage']:
                alternate=frame(folder/(name+'.fgr'));assert np.max(np.abs(alternate-displaced))<.0002,(family,model,name,float(np.max(np.abs(alternate-displaced))))
            zero=frame(folder/'displacement-zero.fgr');assert np.max(np.abs(zero-original))<.0002
            before=frame(folder/'geometry-off-depth.fgr');after=frame(folder/'geometry-on-depth.fgr')
            valid=(before[:,:,3]>.01)&(after[:,:,3]>.01)
            depth_delta=np.abs(before[:,:,2]/np.maximum(before[:,:,3],1e-6)-after[:,:,2]/np.maximum(after[:,:,3],1e-6));delta=np.abs(original[:,:,:3]-displaced[:,:,:3])
            assert valid.any()
            assert float(depth_delta[valid].max())>.001 and float(delta.max())>.01
            clear=original[0,0,:3];background=np.max(np.abs(original[:,:,:3]-clear),axis=2)<.0001
            silhouette=int(np.count_nonzero(background&(np.max(np.abs(displaced[:,:,:3]-clear),axis=2)>.005)))
            name='profile' if profile else 'oblique'
            canvas=Image.new('RGB',(1024,1360),'#13171c');draw=ImageDraw.Draw(canvas);font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',21)
            for i,(label,pixels) in enumerate((('Normal maps, original geometry',original),('Displacement + normal maps, actual geometry',displaced))):
                draw.text((12,i*680+6),f'{label} | camera {1 if profile else 12} deg',font=font,fill='#eef2f6')
                canvas.paste(Image.fromarray(np.uint8(np.clip(pixels[:,:,:3],0,1)*255)).resize((1024,640)),(0,i*680+40))
            canvas.save(folder/(name+'.png'))
            if profile:assert silhouette>8,(family,model,silhouette)
            records.append({'family':family,'height_balance':100 if family=='minimum' else 50,'shader_model':model,'profile':profile,'max_rgb_delta':float(delta.max()),'max_view_depth_delta':float(depth_delta[valid].max()),'new_silhouette_pixels':silhouette})
            print('DISPLACEMENT_GPU_PASS',records[-1],flush=True)
(out/'validation.json').write_text(json.dumps(records,indent=2)+'\n')
