"""Actual normal/POM/shadow/cavity/displacement draws on 1x/2x/4x/8x, also under cache pressure."""
from pathlib import Path
import argparse,json,shutil,struct,subprocess,sys
import numpy as np
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root));import northlight_paths as fp
p=argparse.ArgumentParser();p.add_argument('--backend',required=True);p.add_argument('--samples',nargs='+',type=int,default=[0,2,4,8]);p.add_argument('--families',nargs='+',default=['terrain','wall','composite','grass']);p.add_argument('--width',type=int,default=320);p.add_argument('--height',type=int,default=200);p.add_argument('--output-subdir');a=p.parse_args()
name='native' if 'windows' in a.backend.lower() else 'dxvk';out=fp.out()/(a.output_subdir or 'parallax-msaa-'+name);out.mkdir(exist_ok=True)
env=fp.zig_env();env.update(TEMP=str(fp.out()/'tmp'),TMP=str(fp.out()/'tmp'))
env['DXVK_CONFIG']='d3d9.customVendorId = 1002; d3d9.cachedDynamicBuffers = True; d3d9.cachedWriteOnlyBuffers = True'
exe=out/'material_msaa_gpu.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',f'-DNORTHLIGHT_OBLIQUE_WIDTH={a.width}',f'-DNORTHLIGHT_OBLIQUE_HEIGHT={a.height}','-DNORTHLIGHT_MATERIAL_MIRROR',*fp.test_include_flags(),str(root/'tests/auto_normal_oblique_gpu.cpp'),'-luser32','-o',str(exe)],env=env,check=True)
# Match WoW's Large Address Aware executable for full-resolution readbacks.
pe=bytearray(exe.read_bytes());flags=struct.unpack_from('<I',pe,0x3c)[0]+22;struct.pack_into('<H',pe,flags,struct.unpack_from('<H',pe,flags)[0]|0x20);exe.write_bytes(pe)
def frame(path):
    data=path.read_bytes();magic,w,h,fmt,stride=struct.unpack_from('<5I',data);assert magic==0x31524746
    return np.frombuffer(data,dtype='<f4' if stride==16 else '<f2',offset=20).reshape(h,w,4).astype(np.float32)
records=[]
for family in a.families:
    folder=out/family;folder.mkdir(exist_ok=True)
    for f in (fp.out()/'oblique-cobble').iterdir():
        if f.name.startswith('source.fgr') or f.name=='PreviewBase.bin':shutil.copy2(f,folder/f.name)
    if family=='grass':
        sys.path.insert(0,str(root/'renderer'));from world_scene_builder import Assets,decode_blp
        assets=Assets()
        try:data=assets.read('world\\nodxt\\detail\\silvermoondetaildoodad01.blp')
        finally:assets.close()
        for level in range(9):
            w,h,pixels=decode_blp(data,max(1,256>>level));bgra=bytearray(pixels);bgra[0::4]=pixels[2::4];bgra[2::4]=pixels[0::4];payload=struct.pack('<5I',0x31524746,w,h,21,4)+bgra
            (folder/('source.fgr.'+str(level))).write_bytes(payload)
            if not level:(folder/'source.fgr').write_bytes(payload)
    vertex={'terrain':'terrain','wall':'mapobjdiffuse_t1','composite':'mapobjspecular_t1','grass':'detaildoodad'}[family]
    pixel={'terrain':'terrain1','wall':'mapobjopaque','composite':'mapobjcomposite','grass':'detaildoodad'}[family]
    for model in ([3] if family=='composite' else [2,3]):
        shutil.copy2(fp.out()/'bump-recon'/(f'shaders_vertex_vs_{model}_0_{vertex}.bls-'+('30' if family=='composite' else '0')+'.bin'),folder/'terrain-vs.bin')
        shutil.copy2(fp.out()/'bump-recon'/f'shaders_pixel_ps_3_0_{pixel}.bls-0.bin',folder/'terrain-ps.bin')
        for samples in a.samples:
            reference={}
            for pressure in (0,1):
                runenv=dict(env,NORTHLIGHT_TEST_MSAA=str(samples))
                if pressure:runenv['NORTHLIGHT_TEST_PRESSURE']='1'
                run=subprocess.run([str(exe),a.backend,str(folder)+'/', '12','.7']+([family] if family!='terrain' else []),cwd=out,env=runenv,capture_output=True,text=True,timeout=60)
                label=f'{family}-sm{model}-msaa{samples}-pressure{pressure}';(out/(label+'.log')).write_text(run.stdout+run.stderr)
                assert run.returncode==0,(label,run.stdout[-3000:]+run.stderr[-2000:])
                assert f'MATERIAL_MSAA_PASS samples={samples} pressure={pressure}' in run.stdout
                images={n:frame(folder/(n+'.fgr')) for n in ['normal-on','pom-on','pom-shadow','pom-shadow-cavity','displacement-on','displacement-indexed','displacement-zero','geometry-off-depth','geometry-on-depth']}
                assert all(np.isfinite(im).all() for im in images.values())
                delta=lambda x,y:float(np.max(np.abs(images[x]-images[y])))
                assert delta('normal-on','pom-on')>.005,(label,'POM absent')
                # The cutout leaf atlas has no occluding neighbour along this light ray.
                if family!='grass':assert delta('pom-on','pom-shadow')>.0001,(label,'self shadow absent')
                assert delta('pom-shadow','pom-shadow-cavity')>.0001,(label,'cavity absent')
                assert delta('normal-on','displacement-on')>.01,(label,'geometry absent')
                assert delta('geometry-off-depth','geometry-on-depth')>.001,(label,'unchanged captured geometry depth')
                assert delta('displacement-on','displacement-indexed')<.0002
                assert delta('normal-on','displacement-zero')<.0002
                if pressure:
                    assert 'MATERIAL_ALLOCATION_RETRY_PASS' in run.stdout
                    assert all(float(np.max(np.abs(images[n]-reference[n])))<.0002 for n in images),(label,'pressure changed effects')
                else:reference=images
                records.append({'family':family,'shader_model':model,'samples':samples,'pressure':bool(pressure),'pom_delta':delta('normal-on','pom-on'),'displacement_delta':delta('normal-on','displacement-on'),'actual_gpu_draws':True,'state_and_depth_restored':True})
                print('MATERIAL_MSAA_PASS',records[-1],flush=True)
(out/'validation.json').write_text(json.dumps(records,indent=2)+'\n')
