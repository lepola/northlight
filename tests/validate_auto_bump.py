"""Read client textures, then run the production capture/light shaders offscreen. Never runs WoW."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,sys
root=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(root),str(root/'renderer')]
import northlight_paths as fp
from world_scene_builder import Assets,decode_blp
from PIL import Image,ImageDraw,ImageFont
import numpy as np

parser=argparse.ArgumentParser();parser.add_argument('--backend',required=True);parser.add_argument('--baseline',action='store_true');parser.add_argument('--output-subdir',default='bump-gpu');args=parser.parse_args()
out=fp.out()/args.output_subdir;out.mkdir(exist_ok=True)
assets=Assets();sources={}
try:
    for key in ('grassa','grassb','grassc','rocka'):
        name='tileset\\expansion02\\stormpeaks\\sp_crystalsong'+key+'.blp'
        sources[key]=(name,assets.read(name))
    candidates=[n for n in assets.providers if n.endswith('.blp') and 'dalaran' in n and ('floor' in n or 'brick' in n) and not n.endswith('_s.blp')]
    if candidates:
        chosen=sorted(candidates,key=lambda n:('floor' not in n,n))[0]
        sources['floor' if 'floor' in chosen else 'wall']=(chosen,assets.read(chosen))
finally:assets.close()
env=fp.zig_env();tmp=out/'tmp';tmp.mkdir(exist_ok=True);env.update(TEMP=str(tmp),TMP=str(tmp))
exe=out/'auto-bump-gpu.exe'
build=[str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*(['-I'+str(fp.out()/'perf-baseline')] if args.baseline else []),*fp.test_include_flags(),str(root/'tests/auto_bump_gpu.cpp'),'-luser32','-o',str(exe)]
subprocess.run(build,env=env,check=True)
records=[];previews=[]
for key,(name,data) in sources.items():
    folder=out/key;folder.mkdir(exist_ok=True)
    for p in (root/'out/bump-recon').glob('*.bin'):(folder/p.name).write_bytes(p.read_bytes())
    (folder/'old-LocalDirect.bin').write_bytes((root/'tests/fixtures/material-before/material-before-LocalDirect.bin').read_bytes())
    rgba=None;level=0;w0=h0=0
    while True:
        w,h,pixels=decode_blp(data,256>>level if level<8 else 1)
        if level==0:w0,h0=w,h;rgba=Image.frombytes('RGBA',(w,h),pixels)
        bgra=bytearray(pixels)
        bgra[0::4]=pixels[2::4];bgra[2::4]=pixels[0::4]
        payload=struct.pack('<5I',0x31524746,w,h,21,4)+bgra
        (folder/('source.fgr.'+str(level))).write_bytes(payload)
        if level==0:(folder/'source.fgr').write_bytes(payload)
        if w==h==1:break
        level+=1
    result=subprocess.run([str(exe),str(Path(args.backend).resolve()),str(folder)+'/',str(folder/'source.fgr')],env=env,cwd=out,capture_output=True,text=True,timeout=90)
    (folder/'gpu.log').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(f'{key}: GPU test failed ({result.returncode}): '+result.stdout[-1500:]+result.stderr[-1500:])
    assert 'AUTOBUMP_GPU_PASS' in result.stdout
    # Display the actual production LocalDirect GPU output over the source material.
    family='detaildoodad' if key.startswith('grass') else 'mapobjdiffuse_t1'
    albedo=np.asarray(rgba.resize((256,256))).astype(np.float32)/255
    frames=[]
    for suffix in ('flat','lit'):
        light=np.fromfile(folder/(family+'-'+suffix+'.f32'),dtype='<f4').reshape(256,256,4)
        color=np.clip(albedo[:,:,:3]*(.22+light[:,:,:3]*1.6),0,1)
        frames.append(Image.fromarray(np.uint8(color*255)))
    diff=np.max(np.abs(np.asarray(frames[0],dtype=float)-np.asarray(frames[1],dtype=float)))
    record={'material':key,'asset':name,'sha256':hashlib.sha256(data).hexdigest(),'max_preview_delta_8bit':float(diff),'gpu':result.stdout.strip().splitlines()[-1]}
    records.append(record);previews.append((key,frames))
    print(key,record['gpu'],flush=True)
font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',18)
canvas=Image.new('RGB',(552,len(previews)*300+55),'#13171c');draw=ImageDraw.Draw(canvas)
draw.text((20,12),'Northlight: original textures / automatic bump',font=font,fill='#eff4fa')
for row,(key,frames) in enumerate(previews):
    y=55+row*300;draw.text((20,y),key+'  |  off',font=font,fill='#aeb9c8');draw.text((286,y),'on',font=font,fill='#aeb9c8')
    canvas.paste(frames[0],(16,y+29));canvas.paste(frames[1],(280,y+29))
canvas.save(out/'crystalsong-auto-bump.png')
(out/'validation.json').write_text(json.dumps({'backend':str(Path(args.backend).resolve()),'game_launched':False,'sources':records,'preview':'isolated material planes, production capture and LocalDirect shaders'},indent=2)+'\n')
print('Preview:',out/'crystalsong-auto-bump.png')
