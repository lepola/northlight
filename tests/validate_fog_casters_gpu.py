#!/usr/bin/env python3
# northlight-test: requires=zig,windows-gpu manual
"""Production fog with rasterized authored actor/prop/architecture casters; no game."""
import argparse,importlib.util,json,os,struct,subprocess,sys,statistics
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prefix',required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--benchmark',action='store_true');p.add_argument('--actor-entry',action='store_true');p.add_argument('--baseline-source',type=Path);p.add_argument('--same-output',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('guard',fp.TESTS/'validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
guard.processes()
os.environ['NORTHLIGHT_OUT']=str(a.output/'compiler');compiler=Compiler(allow_compatible_binary=True)
fixture=a.output/'caster.hlsl';fixture.write_text('''row_major float4x4 Matrix:register(c0);
struct Out{float4 position:POSITION0;float depth:TEXCOORD0;};
Out CasterVS(float3 p:POSITION0){Out o;o.position=mul(float4(p,1),Matrix);o.depth=o.position.z;return o;}
float4 CasterPS(float z:TEXCOORD0):COLOR0{return float4(z,z,z,1);}
''')
for name,stage in [('CasterVS','vs_3_0'),('CasterPS','ps_3_0')]:compiler.compile(fixture,name,stage,a.output/(name+'.bin'))
old=a.baseline_source.read_bytes() if a.baseline_source else subprocess.check_output(['git','show','c9a89ea:shaders/world_effects.hlsl'],cwd=fp.REPO)
for label,source in [('old',old),('new',a.source.read_bytes())]:
 folder=a.output/label;folder.mkdir(exist_ok=True);(folder/'world_effects.hlsl').write_bytes(source);(folder/'auto_bump_normal.hlsl').write_bytes((fp.SHADERS/'auto_bump_normal.hlsl').read_bytes())
 for entry in ['WorldFog','FogClouds']:compiler.compile(folder/'world_effects.hlsl',entry+'Actor' if a.actor_entry and label=='new' else entry,'ps_3_0',folder/(entry+'.bin'))
if a.benchmark and a.actor_entry:
 for entry in ['WorldFog','FogClouds']:compiler.compile(a.output/'new/world_effects.hlsl',entry,'ps_3_0',a.output/'new'/(entry+'Baseline.bin'))
source=(fp.TESTS/'regional_fog_cache_gpu.cpp').read_text().replace('#include "regional_fog.h"','#include "regional_fog.h"\n#include "actor_shadow_map.h"')
needle=' IDirect3DVolumeTexture9* noise=nullptr;'
code=r'''
 const int casterCase=std::atoi(std::getenv("NORTHLIGHT_FOG_CASTER_CASE"));bank.count=79;
 c[24][0]=NorthlightWorldMath::ShadowBiasWorld*NorthlightWorldMath::InverseShadowDepth;
 c[65][3]=0; // Isolate 3D shadow behaviour from optional field acceleration metadata.
 using V3=NorthlightGI::Vec3;V3 light(c[16][0],c[16][1],c[16][2]);
 if(casterCase==8){light=V3(-.57735026f,.57735026f,.57735026f);c[17][0]=.15f;c[17][1]=.3f;c[17][2]=.8f;}
 c[16][0]=light.x;c[16][1]=light.y;c[16][2]=light.z;
 NorthlightWorldMath::shadowMatrix(V3(0,8,1),light,192,c[11]);c[2][2]=192;c[2][3]=1.f/128;
 NorthlightActorShadowMap::matrix(V3(0,8,1),V3(0,8,1),light,c[68]);
 c[72][0]=casterCase!=0&&casterCase!=6&&casterCase!=7?1.f:0.f;
 c[66][0]=c[66][1]=-1;c[66][2]=c[66][3]=1;
 if(casterCase>=2&&casterCase!=6){const float x=casterCase==3?2.f:casterCase==5?50.f:0.f,extent=casterCase==4?1.f:.4f,height=casterCase==4?4.f:2.4f;
  const float low[]={x-extent,8-extent,0},high[]={x+extent,8+extent,height};float bound[4];if(NorthlightActorShadowMap::fogBounds(true,low,high,c[68],bound))std::copy(bound,bound+4,c[66]);}

 auto makeMap=[&](bool actor){
  IDirect3DSurface9* previous=nullptr;ok(d->GetRenderTarget(0,&previous));IDirect3DTexture9* texture=nullptr;IDirect3DSurface9 *surface=nullptr,*z=nullptr;
  const unsigned mapSize=actor?NorthlightActorShadowMap::Size:128;
  ok(d->CreateTexture(mapSize,mapSize,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&texture,nullptr));owned.push_back(texture);ok(texture->GetSurfaceLevel(0,&surface));
  ok(d->CreateDepthStencilSurface(mapSize,mapSize,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&z,nullptr));ok(d->SetRenderTarget(0,surface));ok(d->SetDepthStencilSurface(z));D3DVIEWPORT9 mapViewport={0,0,mapSize,mapSize,0,1};ok(d->SetViewport(&mapViewport));
  ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));
  auto vsCode=read(std::string(std::getenv("NORTHLIGHT_FOG_CASTER_SHADER"))+"CasterVS.bin"),psCode=read(std::string(std::getenv("NORTHLIGHT_FOG_CASTER_SHADER"))+"CasterPS.bin");IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
  ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(vsCode.data()),&vs));ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(psCode.data()),&ps));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(ps));ok(d->SetFVF(D3DFVF_XYZ));
  ok(d->SetVertexShaderConstantF(0,actor?c[68]:c[11],4));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
  auto box=[&](float x,float y,float z,float sx,float sy,float sz){
   const V3 corners[]={{x-sx,y-sy,z},{x+sx,y-sy,z},{x-sx,y+sy,z},{x+sx,y+sy,z},{x-sx,y-sy,z+sz},{x+sx,y-sy,z+sz},{x-sx,y+sy,z+sz},{x+sx,y+sy,z+sz}};
   const unsigned indices[]={0,1,2,1,3,2,4,6,5,5,6,7,0,4,1,1,4,5,2,3,6,3,7,6,0,2,4,2,6,4,1,5,3,3,5,7};V3 vertices[36];for(unsigned i=0;i<36;++i)vertices[i]=corners[indices[i]];
   ok(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,12,vertices,sizeof(V3)));
  };
  ok(d->BeginScene());
  if(actor&&casterCase>=2&&casterCase!=6){float x=casterCase==3?2.f:casterCase==5?50.f:0.f;
   if(casterCase==4)box(x,8,0,1,1,4);
   else{box(x,8,1,.4f,.25f,.9f);box(x,8,1.9f,.24f,.24f,.5f);box(x-.24f,8,0,.16f,.2f,1);box(x+.24f,8,0,.16f,.2f,1);}}
  if(!actor&&casterCase==6){box(-3,12,0,1,1,10);box(3,12,0,1,1,10);box(0,12,8,4,1,2);}
  ok(d->EndScene());
  ok(d->SetVertexShader(nullptr));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,previous));previous->Release();surface->Release();z->Release();vs->Release();ps->Release();
  ok(d->SetTexture(actor?4:3,texture));
 };
 makeMap(false);makeMap(true);
'''
assert source.count(needle)==1;source=source.replace(needle,code+needle)
# The production oracle's neutral-state assertion remains separate; active casters intentionally darken RGB.
old='std::fflush(stdout);assert(!changed&&!nonfinite&&maximum==0);'
new='''std::fflush(stdout);assert(!nonfinite&&!alpha);
 for(unsigned n=0;n<2;++n){std::ofstream f(n?"caster-new.fgr":"caster-old.fgr",std::ios::binary);const unsigned header[]={0x31524746,W,H,unsigned(bits==16?D3DFMT_A16B16G16R16F:D3DFMT_A32B32G32R32F),bits/2};f.write(reinterpret_cast<const char*>(header),sizeof header);f.write(reinterpret_cast<const char*>(output[n].data()),output[n].size());}'''
assert source.count(old)==1;source=source.replace(old,new)
cpp=a.output/'casters.cpp';cpp.write_text(source);exe=a.output/'casters.exe'
with (a.output/'compile.log').open('w') as log:subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wno-microsoft-exception-spec','-std=c++17','-I',str(fp.TESTS),*fp.test_include_flags(),str(cpp),str(fp.src('world_gi.cpp')),'-luser32','-o',str(exe)],env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
rows=[]
for backend in [Path(os.environ['SystemRoot'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']:
 for entry in ['WorldFog','FogClouds']:
  for bits in ([16] if a.benchmark else [16,32]):
   for case in ([0,2,4,6] if a.benchmark else range(9)):
    folder=a.output/(('native' if 'SysWOW64' in str(backend) else 'dxvk')+f'-{entry}-{bits}-{case}');folder.mkdir(exist_ok=True)
    env=fp.zig_env();env.update(NORTHLIGHT_FOG_CASTER_CASE=str(case),NORTHLIGHT_FOG_CASTER_SHADER=str(a.output)+'/')
    new_entry=entry+'Baseline' if a.benchmark and a.actor_entry and case in [0,6] else entry
    command=[str(exe),a.prefix,str(a.output/'old'/(entry+'.bin')),str(a.output/'new'/(new_entry+'.bin')),entry,str(backend),'7',str(bits),*(['1920','1080','8'] if a.benchmark else ['0','0','1'])]
    guard.processes()
    run=subprocess.run(command,cwd=folder,env=env,capture_output=True,text=True,timeout=120);(folder/'gpu.log').write_text(run.stdout+run.stderr);assert run.returncode==0,(folder,run.stderr[-1000:])
    row=json.loads(next(s for s in run.stdout.splitlines() if s.startswith('{')));row.update(caster_case=case,backend=str(backend),new_entry=new_entry);assert row['alpha_changes']==0 and row['nonfinite']==0
    old=(folder/'caster-old.fgr').read_bytes();new=(folder/'caster-new.fgr').read_bytes()
    if a.same_output or case in [0,1,5,6,7]:assert old==new,(folder,row)
    else:assert row['changed']>0,(folder,row)
    values=lambda data:[v[0] for v in struct.iter_unpack('<e' if bits==16 else '<f',data[20:])]
    assert all(b<=x+1e-7 for i,(x,b) in enumerate(zip(values(old),values(new))) if i%4!=3),(folder,'caster added light')
    if case==6:
     baseline=a.output/(folder.name.rsplit('-',1)[0]+'-0')/'caster-old.fgr';assert baseline.read_bytes()!=old,'Architecture must cast volumetric shadows'
    rows.append(row)
print('FOG_CASTERS_PASS cases='+str(len(rows)),flush=True)
if a.benchmark:
 for backend in sorted({r['backend'] for r in rows}):
  for entry in ['WorldFog','FogClouds']:
   selected=[r for r in rows if r['backend']==backend and r['shader']==entry];print(backend,entry,[(r['caster_case'],round(r['new_ms']-r['old_ms'],6),round(r['new_ms']/r['old_ms'],3)) for r in selected],flush=True)
(a.output/'validation.json').write_text(json.dumps({'pass':True,'scope':__doc__,'cases':rows},indent=2)+'\n')
