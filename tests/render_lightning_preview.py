"""Render controlled offline frames using native sky resources and LocalFlash bytecode."""
from pathlib import Path
import argparse
import json
import math
import re
import struct
import subprocess
import sys
import numpy as np
from PIL import Image,ImageDraw,ImageFont
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets,array,unpack,decode_blp,string
from test_emission_sources import sky_frames

parser=argparse.ArgumentParser(__doc__);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--stand',action='store_true',help='Prepare a runnable interactive stand instead of still frames');args=parser.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
coverage=fp.out()/'test-output/test_sky_lightning_coverage/all-sky-flashes'
cases=json.loads((coverage/'cases.json').read_text());peaks=[]
for line in (coverage/'O2.log').read_text().splitlines():
    if line.startswith('SKY_PEAK '):peaks.append(dict(re.findall(r'(\w+)=([^\s]+)',line)))
W,H=1280,720
eye=np.array([0.,-15.,4.]);forward=np.array([0.,1.,.015]);forward/=np.linalg.norm(forward)
right=np.array([1.,0.,0.]);up=np.cross(right,forward);up/=np.linalg.norm(up)
px,py=1.1,1.1*W/H
u,v=np.meshgrid((np.arange(W)+.5)/W,(np.arange(H)+.5)/H)
ray=(2*u-1)[...,None]/px*right+(1-2*v)[...,None]/py*up+forward
t=np.where(ray[...,2]<-.00001,-eye[2]/ray[...,2],np.inf)
normals=np.zeros((H,W,3));normals[...,2]=1
material=np.full((H,W),0,dtype=int)
boxes=[([-7,4,0],[-5,6,7]),([5,8,0],[7,10,9]),([-15,32,0],[-4,34,9]),([4,32,0],[15,34,9]),([-4,32,7],[4,34,9]),([-2,15,0],[2,19,.8])]
# A deliberately simple mannequin makes the body/limb shadow visible in the stand.
boxes += [([-.4,2.6,2.8],[.4,3.4,3.6]),([-.6,2.7,1.4],[.6,3.3,2.8]),([-.5,2.7,0],[-.1,3.3,1.4]),([.1,2.7,0],[.5,3.3,1.4]),([-.9,2.7,1.5],[-.6,3.3,2.7]),([.6,2.7,1.5],[.9,3.3,2.7])]
casters=[]
for lo,hi in boxes:
    corners=np.array([[hi[k] if i&(1<<k) else lo[k] for k in range(3)] for i in range(8)])
    for face in [(0,1,3,2),(4,6,7,5),(0,4,5,1),(2,3,7,6),(0,2,6,4),(1,5,7,3)]:
        for k in (0,1,2,0,2,3):casters.append([*corners[face[k]],0,0,1,0,0])
caster_bytes=np.array(casters,dtype='<f4').tobytes()
for index,(lo,hi) in enumerate(boxes):
    lo,hi=np.array(lo),np.array(hi)
    a=(lo-eye)/ray;b=(hi-eye)/ray;near=np.minimum(a,b);far=np.maximum(a,b)
    enter=near.max(axis=2);leave=far.min(axis=2);hit=(enter>0)&(leave>=enter)&(enter<t)
    axis=near.argmax(axis=2);n=np.zeros_like(normals)
    for k in range(3):n[...,k]=np.where(axis==k,np.where(ray[...,k]>0,-1.,1.),0.)
    t=np.where(hit,enter,t);normals=np.where(hit[...,None],n,normals);material=np.where(hit,index+1,material)
valid=np.isfinite(t)&(t<1900);position=eye+ray*np.where(valid,t,0)[...,None]
floor=np.abs(normals[...,2])>.9
cx=np.floor(position[...,0]/2);cy=np.floor(position[...,1]/2)
checker=(cx+cy)%2
albedo=np.where(floor[...,None],np.where(checker[...,None]>0,np.array([.34,.36,.40]),np.array([.24,.27,.31])),np.array([.37,.34,.31]))
mortar=np.where(floor,((position[...,0]%2)<.055)|((position[...,1]%2)<.055),((position[...,2]%1.25)<.045)|(((position[...,0]+position[...,1])%2.5)<.04))
albedo*=np.where(mortar[...,None],.55,1)
albedo*=1+(.025*np.sin(position[...,0]*17)*np.sin(position[...,1]*13))[...,None]
base=albedo*np.array([.028,.038,.060])*(.65+.35*np.maximum(normals[...,2],0))[...,None]
sky=np.stack([.009+.009*np.maximum(0,1-v/.5),.015+.016*np.maximum(0,1-v/.5),.028+.026*np.maximum(0,1-v/.5)],axis=2)
base=np.where(valid[...,None],base,sky)
nearZ,farZ=.2,2000.
depth=np.where(valid,farZ/(farZ-nearZ)*(1-nearZ/np.maximum(t,nearZ)),1)
normal=np.concatenate([np.where(valid[...,None],normals,0),valid[...,None]],axis=2)
constants=np.zeros((84,4),dtype='<f4');constants[0]=[1/W,1/H,nearZ,farZ];constants[1]=[px,py,1,0];constants[2,0]=1
constants[3,:3]=right;constants[4,:3]=up;constants[5,:3]=forward;constants[6]=[*eye,1];constants[15,:3]=eye
constants[20,2]=1;constants[33]=[W,H,W,H];constants[52]=[1,.648,0,0]
constants[2,1:]=[48,192,1/1024];constants[24,0]=.0512/1280;constants[35,0]=1/1280
def fgr(path,data,fmt=116):
    data=np.ascontiguousarray(data,dtype='<f4' if fmt==116 or fmt==114 else np.uint8)
    channels=data.shape[2] if data.ndim==3 else 1
    path.write_bytes(struct.pack('<5I',0x31524746,data.shape[1],data.shape[0],fmt,channels*data.dtype.itemsize)+data.tobytes())
def load_fgr(path):
    data=path.read_bytes();_,w,h,fmt,stride=struct.unpack_from('<5I',data);assert fmt==116 and stride==16
    return np.frombuffer(data,dtype='<f4',offset=20).reshape(h,w,4)
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',24);small=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',18)
exe=out/'lightning-preview.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-return-type',*fp.include_flags(),str(fp.TESTS/'lightning_preview_gpu.cpp'),'-luser32','-o',str(exe)],check=True,env=fp.zig_env())
assets=Assets(without='z');result=[]
try:
    for stem,wanted,title in [('icecrown','icecrownsky.m2','Ледяная Корона'),('shadowmoon','shadowmoonillidan.m2','Долина Призрачной Луны'),('stormpeaks','stormpeaks_skya.m2','Грозовая Гряда — две текстуры')]:
        candidates=[p for p in peaks if p['mode']=='0' and cases[int(p['packet'])]['model'].endswith('\\'+wanted)]
        peak=max(candidates,key=lambda p:max(float(p[k]) for k in ('r','g','b')));case=cases[int(peak['packet'])]
        folder=out/stem;folder.mkdir(exist_ok=True)
        fgr(folder/'depth.fgr',depth,114);fgr(folder/'normals.fgr',normal);(folder/'casters.bin').write_bytes(caster_bytes)
        if args.stand:
            fgr(folder/'base.fgr',np.concatenate([base,np.ones((H,W,1))],axis=2))
            fgr(folder/'albedo.fgr',np.concatenate([albedo,np.ones((H,W,1))],axis=2))
        direction=np.array([.17,1,.35]);direction/=np.linalg.norm(direction);distance=350/direction[2]
        source=eye+direction*distance;rgb=[float(peak[k]) for k in ('r','g','b')]
        constants[16,:3]=direction
        for cascade,radius in enumerate([48,192]):
            axis=np.cross([0,1,0] if abs(direction[2])>.95 else [0,0,1],direction);axis/=np.linalg.norm(axis);vertical=np.cross(direction,axis)
            texel=2*radius/1024;cx=np.floor(np.dot(eye,axis)/texel)*texel;cy=np.floor(np.dot(eye,vertical)/texel)*texel
            matrix=np.zeros((4,4));matrix[:3,0]=axis/radius;matrix[:3,1]=vertical/radius;matrix[:3,2]=-direction/1280
            matrix[3]=[-cx/radius,-cy/radius,.5+np.dot(eye,direction)/1280,1];constants[7+cascade*4:11+cascade*4]=matrix
        (folder/'constants.f32').write_bytes(constants.tobytes())
        (folder/'source.bin').write_bytes(struct.pack('<8fQ2I',*source,*rgb,.5,2000,0xfed0000000000001,2,32))
        for i,name in enumerate(case['textures']):
            w,h,rgba=decode_blp(assets.read(name),512);pixels=np.frombuffer(rgba,np.uint8).reshape(h,w,4)[...,[2,1,0,3]];fgr(folder/f'bolt{i}.fgr',pixels,21)
        blob=assets.read(case['model']);skin=assets.read(case['model'][:-3]+'00.skin');rows=lambda b,o,f:array(b,*reversed(unpack('<II',b,o)),f)
        batch=rows(skin,36,'<2B11H')[case['batch']];sub=rows(skin,28,'<10H7f')[batch[3]];first=sub[4]+(sub[1]<<16)
        lookup=rows(skin,4,'<H');vertices=rows(blob,60,'<3f4B4B3f4f');indices=rows(skin,12,'<H')[first:first+sub[5]]
        points=np.array([vertices[lookup[i][0]][:3] for i, in indices]);center=points.mean(axis=0);f=center/np.linalg.norm(center)
        r=np.cross(np.array([0,0,1.]),f);r/=np.linalg.norm(r);a=np.cross(f,r)
        xy=np.stack([(points-center)@r,(points-center)@a],axis=1);scale=210/max(np.ptp(xy,axis=0))
        projected_x=W*(.5+px*np.dot(direction,right)/np.dot(direction,forward)/2)
        projected_y=H*(.5-py*np.dot(direction,up)/np.dot(direction,forward)/2)
        tint=[float(peak[k]) for k in ('tintR','tintG','tintB','alpha')]
        colour=(round(tint[3]*255)<<24)|(round(tint[0]*255)<<16)|(round(tint[1]*255)<<8)|round(tint[2]*255)
        data=bytearray()
        for j,(i,) in enumerate(indices):
            vertex=vertices[lookup[i][0]];data+=struct.pack('<4fI4f',projected_x+xy[j,0]*scale,projected_y-xy[j,1]*scale,0,1,colour,*vertex[14:18])
        (folder/'bolt-vertices.bin').write_bytes(data);(folder/'bolt-info.bin').write_bytes(struct.pack('<2I',int(peak['mode']),colour))
        if args.stand:
            period,frames=sky_frames(blob,batch);tick=int(peak['tick'])
            before=max((t for t,*c in frames if t<tick and c[3]==0),default=0);after=min((t for t,*c in frames if t>tick and c[3]==0),default=period)
            _,timeline=sky_frames(blob,batch,range(period))
            payload=struct.pack('<5I4f',0x31464c4e,period,max(0,before-1000),min(period,after+1000),tick,*tint)
            payload+=np.array([row[1:] for row in timeline],dtype='<f4').tobytes()
            (folder/'timeline.bin').write_bytes(payload)
            result.append({'name':stem,'title':title,'model':case['model'],'period_ms':period,'peak_ms':tick})
            continue
        subprocess.run([str(exe),str(folder)+'/',str(fp.COMPILED/'LocalFlash.bin'),str(fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')],check=True)
        period,frames=sky_frames(blob,batch);tick=int(peak['tick']);before=max((t for t,*c in frames if t<tick and c[3]==0),default=0);after=min((t for t,*c in frames if t>tick and c[3]==0),default=period)
        images=[];raw=[]
        for phase,(label,stamp) in enumerate(zip(['До вспышки','Вспышка','После вспышки'],[before,tick,after])):
            light=load_fgr(folder/f'light-{phase}.fgr');bolt=load_fgr(folder/f'bolt-{phase}.fgr')
            assert np.isfinite(light).all() and np.isfinite(bolt).all();linear=base+light[...,:3]*albedo+bolt[...,:3]
            raw.append(linear);toned=1-np.exp(-np.maximum(linear,0));display=np.where(toned<=.0031308,toned*12.92,1.055*np.power(toned,1/2.4)-.055)
            image=Image.fromarray(np.rint(np.clip(display,0,1)*255).astype(np.uint8));draw=ImageDraw.Draw(image);draw.rectangle((0,0,W,75),fill=(10,14,22))
            draw.text((20,12),f'{title} · {label}',font=font,fill=(240,244,250));draw.text((20,44),f'Офлайн GPU-стенд · тестовая геометрия · игровой разряд · t={stamp} мс',font=small,fill=(160,180,200))
            image.save(out/f'{stem}-{phase}.png');images.append(image)
        assert np.array_equal(raw[0],raw[2]);assert np.max(raw[1]-raw[0])>.01
        unshadowed=load_fgr(folder/'unshadowed-1.fgr')[...,:3];shadowed=load_fgr(folder/'light-1.fgr')[...,:3]
        assert np.min(shadowed)>-1e-5 and np.max(unshadowed-shadowed)>.01
        # Same flash with/without its own caster maps: brightness and geometry identical.
        plain=base+unshadowed*albedo+load_fgr(folder/'bolt-1.fgr')[...,:3]
        toned=1-np.exp(-np.maximum(plain,0));display=np.where(toned<=.0031308,toned*12.92,1.055*np.power(toned,1/2.4)-.055)
        plain_image=Image.fromarray(np.rint(np.clip(display,0,1)*255).astype(np.uint8));draw=ImageDraw.Draw(plain_image);draw.rectangle((0,0,W,75),fill=(10,14,22));draw.text((20,12),f'{title} · Вспышка без её теней',font=font,fill=(240,244,250));draw.text((20,44),'Офлайн GPU-стенд · тот же свет и та же геометрия',font=small,fill=(160,180,200))
        pair=Image.new('RGB',(W*2,H));pair.paste(plain_image,(0,0));pair.paste(images[1],(W,0));pair.resize((1920,540),Image.Resampling.LANCZOS).save(out/f'{stem}-shadows.png')
        overview=Image.new('RGB',(W*3,H));[overview.paste(im,(i*W,0)) for i,im in enumerate(images)];overview.resize((1920,360),Image.Resampling.LANCZOS).save(out/f'{stem}-comparison.png')
        result.append({'name':stem,'model':case['model'],'textures':case['textures'],'source_colour':rgb,'native_peak_tick':tick,'before_after_identical':True,'max_added_linear':float(np.max(raw[1]-raw[0]))})
finally:assets.close()
if args.stand:
    import shutil
    sys.path.insert(0,str(fp.SCRIPTS/'shaders'))
    from shader_compiler import Compiler,validate_assembly
    compose=out/'compose.hlsl'
    compose.write_text('''sampler2D Base:register(s0);sampler2D Albedo:register(s1);sampler2D Light:register(s2);sampler2D Bolt:register(s3);
float4 Compose(float2 uv:TEXCOORD0):COLOR0 {
 float3 c=tex2D(Base,uv).rgb+tex2D(Albedo,uv).rgb*tex2D(Light,uv).rgb+tex2D(Bolt,uv).rgb;
 c=1-exp(-max(c,0));return float4(c<=.0031308?c*12.92:1.055*pow(c,1/2.4)-.055,1);
}''',encoding='utf-8',newline='\n')
    Compiler(allow_compatible_binary=True).compile(compose,'Compose','ps_3_0',out/'Compose.bin')
    validate_assembly(out/'Compose.bin.asm','ps_3_0')
    for name in ('LocalFlash','FlashShadow','ShadowVS','ShadowPS'):shutil.copy2(fp.COMPILED/(name+'.bin'),out/(name+'.bin'))
    shutil.copy2(fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll',out/'stand-backend.dll')
    for index,record in enumerate(result):
        shutil.copy2(out/'Compose.bin',out/record['name']/'Compose.bin')
        command='@echo off\r\ncd /d "%~dp0"\r\nstart "" "%~dp0lightning-preview.exe" "%~dp0'+record['name']+'/" "%~dp0LocalFlash.bin" "%~dp0stand-backend.dll" --interactive\r\n'
        (out/(('START' if index==0 else record['name'])+'.cmd')).write_bytes(command.encode('ascii'))
    (out/'README.txt').write_text('Northlight — молнии, офлайн GPU-стенд\n\nЗапуск: START.cmd (Ледяная Корона). Другие разряды: shadowmoon.cmd и stormpeaks.cmd.\nПробел — пауза. S — замедление в 4 раза. L — тени вкл/выкл. T — полный игровой цикл / повтор фрагмента. R — сначала. Esc — выход.\n\nПо умолчанию повторяется короткий фрагмент около разряда; длительность самой вспышки берётся из игровой анимации. T включает исходный полный цикл с длинными паузами.\nТестовая геометрия, настоящие игровые текстуры и покадровые значения цвета/прозрачности. Свет и тени рисуются production-шейдерами Northlight. WoW запускать не нужно.\n',encoding='utf-8')
    (out/'stand-manifest.json').write_text(json.dumps({'controlled_geometry':True,'game_launched':False,'native_timeline_step_ms':1,'scenes':result},indent=2)+'\n',encoding='utf-8')
    print(out/'START.cmd');raise SystemExit(0)
(out/'preview-manifest.json').write_text(json.dumps({'controlled_geometry':True,'game_screenshot':False,'light_shader':'shaders/compiled/LocalFlash.bin','shadow_shader':'shaders/compiled/FlashShadow.bin','caster_shaders':['ShadowVS.bin','ShadowPS.bin'],'source':'native material timeline + native texture/mask + Capture/Frame publication','previews':result},indent=2)+'\n',encoding='utf-8')
print(out/'preview-manifest.json')
