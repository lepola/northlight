from pathlib import Path
import argparse,json,shutil,struct,subprocess,sys
import numpy as np
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
import northlight_paths as fp
sys.path.insert(0,str(root/'scripts/shaders'))
from shader_compiler import Compiler
parser=argparse.ArgumentParser(__doc__);parser.add_argument('--compatible-compiler',action='store_true');args=parser.parse_args()
compiler=Compiler(allow_compatible_binary=args.compatible_compiler)
for name,material in [('oblique','rocka'),('oblique-cobble','floor')]:
    folder=fp.out()/name;folder.mkdir(parents=True,exist_ok=True)
    sources=list((fp.out()/'bump-gpu'/material).glob('source.fgr*'))
    if not sources:raise SystemExit('Run tests/validate_auto_bump.py first to prepare the local textures.')
    for source in sources:shutil.copy2(source,folder/source.name)
    for stage,family in [('vertex_vs','terrain'),('pixel_ps','terrain1')]:
        source=fp.out()/'bump-recon'/('shaders_'+stage+'_3_0_'+family+'.bls-0.bin')
        shutil.copy2(source,folder/('terrain-vs.bin' if stage.startswith('vertex') else 'terrain-ps.bin'))
    compiler.compile(fp.SHADERS/'auto_normal_preview.hlsl','PreviewBase','ps_3_0',folder/'PreviewBase.bin')

env=fp.zig_env();env.update(TEMP=str(fp.out()/'tmp'),TMP=str(fp.out()/'tmp'))
exe=fp.out()/'oblique-gpu.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(root/'tests/auto_normal_oblique_gpu.cpp'),'-luser32','-o',str(exe)],env=env,check=True)
records=[]
for name in ('oblique','oblique-cobble'):
    folder=fp.out()/name
    run=subprocess.run([str(exe),str(fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'),str(folder)+'/'],env=env,cwd=fp.out(),capture_output=True,text=True,check=True)
    (folder/'native-pom.log').write_text(run.stdout+run.stderr)
    assert 'OBLIQUE_NATIVE_POM_PASS' in run.stdout
    frames=[]
    for key in ('normal-on','pom-on','pom-shadow','pom-shadow-cavity'):
        data=(folder/(key+'.fgr')).read_bytes();magic,w,h,fmt,stride=struct.unpack_from('<5I',data)
        assert magic==0x31524746 and stride==16
        values=np.frombuffer(data,dtype='<f4',offset=20).reshape(h,w,4);assert np.isfinite(values).all()
        frames.append(values[:,:,:3].copy());Image.fromarray(np.uint8(np.clip(values[:,:,:3],0,1)*255)).save(folder/(key+'.png'))
    delta=np.abs(frames[0]-frames[1]);assert float(delta.max())>.05
    canvas=Image.new('RGB',(1024,1368),'#13171c');draw=ImageDraw.Draw(canvas);font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',22)
    for row,(label,key) in enumerate((('Normal maps','normal-on'),('Normal maps + POM','pom-on'))):
        y=row*680;draw.text((14,y+7),label+' | native material shader | camera 12 deg',font=font,fill='#eef2f6')
        canvas.paste(Image.open(folder/(key+'.png')).resize((1024,640)),(0,y+40))
    canvas.save(folder/'normal-vs-pom-oblique.png')
    assert 'OBLIQUE_OCCLUSION_PASS' in run.stdout
    shadow_delta=frames[1]-frames[2];cavity_delta=frames[2]-frames[3]
    assert float(shadow_delta.max())>.01 and float(cavity_delta.max())>.005
    assert float(shadow_delta.min())>-.001 and float(cavity_delta.min())>-.001
    for row,(label,key) in enumerate((('POM','pom-on'),('POM + self-shadowing + cavity','pom-shadow-cavity'))):
        y=row*680;draw.rectangle((0,y,1024,y+680),fill='#13171c');draw.text((14,y+7),label+' | camera 12 deg',font=font,fill='#eef2f6');canvas.paste(Image.open(folder/(key+'.png')).resize((1024,640)),(0,y+40))
    canvas.save(folder/'pom-occlusion-oblique.png')
    records.append({'folder':str(folder),'camera_degrees':12,'shader':'production native POM, normal/occlusion MRT and full-resolution light composite','max_linear_rgb_delta':float(delta.max()),'mean_linear_rgb_delta':float(delta.mean()),'max_shadow_darkening':float(shadow_delta.max()),'max_cavity_darkening':float(cavity_delta.max())})
    print(name,'OBLIQUE_NATIVE_POM_PASS',flush=True)
(fp.out()/'oblique-native-validation.json').write_text(json.dumps(records,indent=2)+'\n')
