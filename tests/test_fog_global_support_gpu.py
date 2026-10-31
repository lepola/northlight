#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,windows-gpu manual
"""Exact old/new fog support clipping at FP16 and FP32; authored standalone inputs."""
from pathlib import Path
import sys,os,subprocess,json,struct,argparse,shutil,hashlib
repo=Path(__file__).resolve().parents[1];sys.path[:0]=[str(repo),str(repo/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler,validate_assembly
from windows_gpu_guard import processes
p=argparse.ArgumentParser();p.add_argument('name',nargs='?',default='fog-global-support');p.add_argument('--reference',type=Path);p.add_argument('--baseline-ref',default='51f0107');p.add_argument('--backend',type=Path);p.add_argument('--entries',nargs='+',default=['WorldFog','WorldFogActor','FogClouds','FogCloudsActor']);p.add_argument('--cases',nargs='+',type=int,default=[0,3,4,7,10,12,13,14,15,16]);a=p.parse_args()
backend=a.backend or fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'
processes()
root=fp.output_dir();out=root/a.name;out.mkdir(exist_ok=True)
fixture=root/'fixture';fixture.mkdir(exist_ok=True);prefix=str(fixture/'capture-1-')
def image(name,w,h,fmt,bpp,pixels):Path(prefix+name+'.fgr').write_bytes(struct.pack('<5I',0x31524746,w,h,fmt,bpp)+pixels)
if not Path(prefix+'constants.f32').exists():
 c=[0.0]*(79*4)
 def put(n,v):c[n*4:n*4+len(v)]=v
 put(0,[1/512,1/512,.1,1000]);put(1,[1,1,1,0]);put(2,[1,1,192,1/64]);put(33,[512,512,256,256]);put(65,[0,0,0,0]);Path(prefix+'constants.f32').write_bytes(struct.pack('<316f',*c))
 image('scene',512,512,21,4,bytes([128,128,128,255])*512*512)
 image('depth',512,512,114,4,struct.pack('<f',.999)*512*512)
current=(repo/'shaders/world_effects.hlsl').read_bytes()
reference=a.reference.read_bytes() if a.reference else subprocess.check_output(['git','show',a.baseline_ref+':shaders/world_effects.hlsl'],cwd=repo)
os.environ['NORTHLIGHT_OUT']=str(out/'compiler');compiler=Compiler(allow_compatible_binary=True);stats={}
for label,data in [('old',reference),('new',current)]:
 folder=out/label;folder.mkdir(exist_ok=True);(folder/'world_effects.hlsl').write_bytes(data);shutil.copyfile(repo/'shaders/auto_bump_normal.hlsl',folder/'auto_bump_normal.hlsl')
 for entry in a.entries:
  binary=folder/(entry+'.bin');compile_entry=entry.replace('Cached','') if label=='old' and entry.endswith('Cached') else entry;compiler.compile(folder/'world_effects.hlsl',compile_entry,'ps_3_0',binary);stats[label+'/'+entry]=validate_assembly(Path(str(binary)+'.asm'),'ps_3_0',slot_limit=800 if entry.endswith('Cached') and label=='new' else 600 if 'Actor' in entry else 512)
exe=root/'world-mini.exe'
if not exe.exists() or exe.stat().st_mtime<max(Path(__file__).stat().st_mtime,(repo/'tests/world_pass_gpu.cpp').stat().st_mtime):
 cpp=(repo/'tests/world_pass_gpu.cpp').read_text();cpp=cpp.replace('#include "removal_distance_gpu.cpp"','#include "'+(repo/'tests/removal_distance_gpu.cpp').as_posix()+'"')
 cpp=cpp.replace('mode=="FogClouds"','(mode.find("FogClouds")==0)')
 cpp=cpp.replace('scenario<=12','scenario<=16')
 cpp=cpp.replace(' IDirect3DPixelShader9* ps[2]{};',r''' bank.count=79;
 for(unsigned i=68;i<72;++i)std::fill(c[i],c[i]+4,0);c[68][0]=c[69][1]=1.f/32;c[70][2]=1.f/32;c[71][2]=.4f;c[71][3]=1;
 c[72][0]=1;c[66][0]=c[66][1]=-.15f;c[66][2]=c[66][3]=.15f;
 c[65][0]=scenario==10?-14.25f:-.25f;c[65][1]=scenario==10?80.25f:66.25f;c[65][2]=scenario==16?0.f:1.f;c[65][3]=0;
 if(scenario==13)c[15][2]=100;if(scenario==14)c[15][2]=-100;if(scenario==15)c[15][2]=66.125f;
 Image actor{1024,1024,D3DFMT_R32F,4,std::vector<unsigned char>(1024*1024*4)};
 for(unsigned y=0;y<1024;++y)for(unsigned x=0;x<1024;++x){float z=x>=442&&x<582&&y>=442&&y<582&&((x/8+y/11)%4!=0)?.48f:1.f;std::memcpy(actor.pixels.data()+(y*1024+x)*4,&z,4);}bind(4,actor);
 IDirect3DPixelShader9* ps[2]{};''')
 cpp=cpp.replace('scenario<0?16u:20u','scenario<0?16u:16u')
 cpp=cpp.replace(' IDirect3DSurface9 *target=nullptr,*staging=nullptr;', ' const unsigned bits=std::getenv("NORTHLIGHT_FOG_TEST_BITS")?unsigned(std::atoi(std::getenv("NORTHLIGHT_FOG_TEST_BITS"))):16,componentBytes=bits/8,targetBytes=componentBytes*4;const D3DFORMAT targetFormat=bits==32?D3DFMT_A32B32G32R32F:D3DFMT_A16B16G16R16F;IDirect3DSurface9 *target=nullptr,*staging=nullptr;')
 cpp=cpp.replace('W,H,D3DFMT_A16B16G16R16F','W,H,targetFormat').replace('size_t(W)*H*8','size_t(W)*H*targetBytes').replace('size_t(y)*W*8','size_t(y)*W*targetBytes').replace('W*8);','W*targetBytes);')
 before='uint16_t a,b;std::memcpy(&a,output[0].data()+2*i,2);std::memcpy(&b,output[1].data()+2*i,2);double e=std::fabs(double(half(a))-half(b));'
 after='uint32_t a=0,b=0;std::memcpy(&a,output[0].data()+componentBytes*i,componentBytes);std::memcpy(&b,output[1].data()+componentBytes*i,componentBytes);float va,vb;if(bits==16){va=half(uint16_t(a));vb=half(uint16_t(b));}else{std::memcpy(&va,&a,4);std::memcpy(&vb,&b,4);}double e=std::fabs(double(va)-vb);'
 assert before in cpp;cpp=cpp.replace(before,after)
 source=root/'world-mini.cpp';source.write_bytes(cpp.encode());subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(source),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
rows=[]
processes()
for entry in a.entries:
 for bits in [16,32]:
  for case in a.cases:
   processes();env=fp.zig_env();env.update(DXVK_LOG_PATH=str(out),DXVK_STATE_CACHE_PATH=str(out),DXVK_SHADER_CACHE_PATH=str(out),NORTHLIGHT_FOG_TEST_BITS=str(bits))
   r=subprocess.run([str(exe),prefix,str(out/'old'/(entry+'.bin')),str(out/'new'/(entry+'.bin')),entry,str(backend.resolve()),str(case)],cwd=out,env=env,capture_output=True,text=True,timeout=120)
   (out/(entry+'-'+str(bits)+'-'+str(case)+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();row=json.loads(next(line for line in r.stdout.splitlines() if line.startswith('{')));assert row['changed']==0 and row['nonfinite']==0;row['bits']=bits;row['scenario']=case;rows.append(row);print(entry,bits,case,'PASS ratio',row['ratio'],flush=True)
(out/'validation.json').write_text(json.dumps({'pass':True,'stats':stats,'rows':rows,'authored_inputs':True,'game_launched':False,'reference_source_sha256':hashlib.sha256(reference).hexdigest(),'candidate_source_sha256':hashlib.sha256(current).hexdigest(),'backend_sha256':hashlib.sha256(backend.read_bytes()).hexdigest()},indent=2))
