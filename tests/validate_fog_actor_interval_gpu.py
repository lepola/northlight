#!/usr/bin/env python3
# northlight-test: requires=zig,windows-gpu manual
"""Compare production actor interval HLSL with an independent CPU grid-crossing integral."""
import argparse,ast,importlib.util,json,os,subprocess,sys
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prefix',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('guard',fp.TESTS/'validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard);guard.processes()
os.environ['NORTHLIGHT_OUT']=str(a.output/'compiler');compiler=Compiler(allow_compatible_binary=True)
tree=ast.parse((fp.TESTS/'validate_fog_casters_gpu.py').read_text())
code=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='code' for t in n.targets))
code=code.replace(' const int casterCase=', ' std::vector<float> actorPixels(1024*1024);\n const int casterCase=')
code=code.replace(' ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));',' ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,actor&&casterCase==9?0:0xffffffff,1,0));')
code=code.replace('  ok(d->EndScene());',r'''  ok(d->EndScene());
  if(actor){IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(mapSize,mapSize,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->GetRenderTargetData(surface,staging));D3DLOCKED_RECT lock={};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(unsigned row=0;row<mapSize;++row)std::memcpy(actorPixels.data()+row*mapSize,(char*)lock.pBits+row*lock.Pitch,mapSize*4);ok(staging->UnlockRect());staging->Release();}
''')
code+='\n c[66][0]=c[66][1]=-1;c[66][2]=c[66][3]=1;\n'
fixture=a.output/'caster.hlsl';fixture.write_text('''row_major float4x4 Matrix:register(c0);
struct Out{float4 position:POSITION0;float depth:TEXCOORD0;};
Out CasterVS(float3 p:POSITION0){Out o;o.position=mul(float4(p,1),Matrix);o.depth=o.position.z;return o;}
float4 CasterPS(float z:TEXCOORD0):COLOR0{return z.xxxx;}
''')
for entry,target in [('CasterVS','vs_3_0'),('CasterPS','ps_3_0')]:compiler.compile(fixture,entry,target,a.output/(entry+'.bin'))
hlsl=(fp.SHADERS/'world_effects.hlsl').read_text();start=hlsl.index('sampler2D FogActorMap');end=hlsl.index('\nfloat2 probeUV',start);helper=hlsl[start:end]
combiner=helper[helper.index('float fogCombineActor('):]
point='''float fogActorShadow(float3 q,float3 delta){q+=delta*.5;if(max(abs(q.x),abs(q.y))>=1||q.z<0||q.z>1)return 1;return q.z-PassInfo.x*4>tex2Dlod(FogActorMap,float4(q.xy*float2(.5,-.5)+.5+.5/1024,0,0)).r?0:1;}'''
ray_hlsl='''void rays(float2 uv,out float3 q,out float3 delta){
 float mode=floor(uv.y*8);q=float3(uv.x*2.4-1.2,frac(uv.y*8)*.6-.3,mode==5?.4995:.6);
 delta=mode<1?float3(.25,0,0):mode<2?float3(0,.25,0):mode<3?float3(.5,.4,0):mode<4?float3(-.5,.4,0):mode<5?float3(2.2,2.2,0):mode<6?float3(0,0,.003):mode<7?float3(1e-9,.3,-.002):float3(-2.2,-2.2,.004);
}
float4 WorldFog(float2 uv:TEXCOORD0):COLOR0{float3 q,d;rays(uv,q,d);float v=fogActorShadow(q,d);return float4(v,fogCombineActor(.5,v),fogCombineActor(1.00000011920928955078125,v),1);}
'''
for label,body in [('old',helper[:helper.index('float fogActorShadow(')]+point+'\n'+combiner),('new',helper)]:
 source=a.output/(label+'.hlsl');source.write_text('float4 PassInfo:register(c24);\n'+body+'\n'+ray_hlsl);compiler.compile(source,'WorldFog','ps_3_0',a.output/(label+'.bin'))
reference=r'''
 double sumOld=0,sumNew=0,maxOld=0,maxNew=0;unsigned measured=0;double modeMax[8]={};unsigned worstX=0,worstY=0;float worstActual=0;double worstExpected=0;
 for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
  const float u=(x+.5f)/W,v=(y+.5f)/H,mode=std::floor(v*8);float q[]={u*2.4f-1.2f,(v*8-mode)*.6f-.3f,mode==5?.4995f:.6f};
  float delta[3];const float directions[][3]={{.25f,0,0},{0,.25f,0},{.5f,.4f,0},{-.5f,.4f,0},{2.2f,2.2f,0},{0,0,.003f},{1e-9f,.3f,-.002f},{-2.2f,-2.2f,.004f}};std::memcpy(delta,directions[unsigned(mode)],sizeof delta);
  const double px=(double(q[0])*.5+.5)*1024+.5,py=(-double(q[1])*.5+.5)*1024+.5,dx=double(delta[0])*512,dy=-double(delta[1])*512;
  std::vector<double> cuts{0,1};
  auto cut=[&](double t){if(t>0&&t<1)cuts.push_back(t);};
  for(unsigned axis=0;axis<3;++axis)if(delta[axis]){cut(((axis==2?0.0:-1.0)-q[axis])/delta[axis]);cut((1.0-q[axis])/delta[axis]);}
  for(unsigned line=0;line<=1024;++line){if(dx)cut((line-px)/dx);if(dy)cut((line-py)/dy);}
  std::sort(cuts.begin(),cuts.end());double covered=0;
  for(size_t i=1;i<cuts.size();++i){const double lo=cuts[i-1],hi=cuts[i],mid=(lo+hi)*.5;const double at[]={q[0]+delta[0]*mid,q[1]+delta[1]*mid,q[2]+delta[2]*mid};
   if(std::abs(at[0])>1||std::abs(at[1])>1||at[2]<0||at[2]>1)continue;
   const unsigned sx=unsigned(std::clamp(int(std::floor(px+dx*mid)),0,1023)),sy=unsigned(std::clamp(int(std::floor(py+dy*mid)),0,1023));const double threshold=actorPixels[sy*1024+sx]+double(c[24][0])*4;
   if(delta[2]>0)covered+=std::max(0.0,hi-std::max(lo,(threshold-q[2])/delta[2]));
   else if(delta[2]<0)covered+=std::max(0.0,std::min(hi,(threshold-q[2])/delta[2])-lo);
   else if(at[2]>threshold)covered+=hi-lo;
  }
  const double expected=std::clamp(1-covered,0.0,1.0);const size_t at=(size_t(y)*W+x)*4;
  for(unsigned label=0;label<2;++label){float actual;if(bits==16){uint16_t b;std::memcpy(&b,output[label].data()+at*2,2);actual=half(b);}else std::memcpy(&actual,output[label].data()+at*4,4);assert(std::isfinite(actual));const double error=std::abs(actual-expected);if(label){if(error>maxNew){worstX=x;worstY=y;worstActual=actual;worstExpected=expected;}maxNew=std::max(maxNew,error);modeMax[unsigned(mode)]=std::max(modeMax[unsigned(mode)],error);sumNew+=error*error;}else{maxOld=std::max(maxOld,error);sumOld+=error*error;}}
  for(unsigned channel=1;channel<=2;++channel){const float world=channel==1?.5f:std::nextafter(1.f,2.f);const double combined=expected<1?std::min(double(world),expected):world;float value;if(bits==16){uint16_t raw;std::memcpy(&raw,output[1].data()+2*(at+channel),2);value=half(raw);}else std::memcpy(&value,output[1].data()+4*(at+channel),4);assert(std::abs(value-combined)<(bits==16?.0006:.00015));if(bits==32&&expected==1)assert(value==world);}
  ++measured;
 }
 const double newRms=std::sqrt(sumNew/measured),oldRms=std::sqrt(sumOld/measured);
 std::printf("INTERVAL_REFERENCE case=%d bits=%u pixels=%u oldMax=%.9g newMax=%.9g oldRms=%.9g newRms=%.9g\n",casterCase,bits,measured,maxOld,maxNew,oldRms,newRms);std::fflush(stdout);
 for(unsigned n=0;n<8;++n)std::printf("MODE_ERROR %u %.9g\n",n,modeMax[n]);std::printf("WORST x=%u y=%u actual=%.9g expected=%.9g\n",worstX,worstY,worstActual,worstExpected);std::fflush(stdout);
 assert(maxNew<(bits==16?.0006:.00015));if(casterCase==4){assert(maxOld>.01&&oldRms>newRms*10);}
'''
source=(fp.TESTS/'regional_fog_cache_gpu.cpp').read_text().replace('#include "regional_fog.h"','#include "regional_fog.h"\n#include "actor_shadow_map.h"')
needle=' IDirect3DVolumeTexture9* noise=nullptr;';assert source.count(needle)==1;source=source.replace(needle,code+needle)
needle='std::fflush(stdout);assert(!changed&&!nonfinite&&maximum==0);';assert source.count(needle)==1;source=source.replace(needle,'std::fflush(stdout);assert(!nonfinite&&!alpha);'+reference)
cpp=a.output/'interval.cpp';cpp.write_text(source);exe=a.output/'interval.exe'
with (a.output/'compile.log').open('w') as log:subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(cpp),str(fp.src('world_gi.cpp')),'-luser32','-o',str(exe)],env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
rows=[]
for backend in [Path(os.environ['SystemRoot'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']:
 for bits in [16,32]:
  for case in [1,2,4,8,9]:
   guard.processes();folder=a.output/(('native' if 'SysWOW64' in str(backend) else 'dxvk')+f'-{bits}-{case}');folder.mkdir(exist_ok=True);env=fp.zig_env();env.update(NORTHLIGHT_FOG_CASTER_CASE=str(case),NORTHLIGHT_FOG_CASTER_SHADER=str(a.output)+'/')
   run=subprocess.run([str(exe),a.prefix,str(a.output/'old.bin'),str(a.output/'new.bin'),'WorldFog',str(backend),'7',str(bits),'128','64','1'],cwd=folder,env=env,capture_output=True,text=True,timeout=120);(folder/'gpu.log').write_text(run.stdout+run.stderr);assert run.returncode==0,(folder,run.stdout[-1000:],run.stderr[-600:]);result=next(s for s in run.stdout.splitlines() if s.startswith('INTERVAL_REFERENCE'));print(str(backend),result,flush=True);rows.append(result)
(a.output/'validation.json').write_text(json.dumps({'pass':True,'cases':rows,'reference':'Independent CPU sorted grid-plane intersections over the actual rasterized R32 actor depth; midpoint destructive control'},indent=2)+'\n');print('INTERVAL_ORACLE_PASS cases='+str(len(rows)),flush=True)
