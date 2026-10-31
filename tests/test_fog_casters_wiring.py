#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Compile the actual source-specific volumetric actor bindings and reject stale-map controls."""
import argparse,json,subprocess,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=fp.output_dir()/'fog-caster-bindings');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
host=fp.src('world_renderer.h').read_text();start=host.index('            if(volume){\n                const bool fogActor=');end=host.index('            }\n',start)+len('            }\n');body=host[start:end]
source=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include "actor_shadow_map.h"
static void logf(const char*,...){}
struct Device {
 unsigned binds=0;int* texture=nullptr;float matrix[16]={},info[4]={},bounds[4]={};
 void SetTexture(unsigned slot,int* t){assert(slot==4);texture=t;++binds;}
 void SetPixelShaderConstantF(unsigned first,const float* p,unsigned count){assert((first==68&&count==4)||((first==72||first==66)&&count==1));std::memcpy(first==68?matrix:first==72?info:bounds,p,count*16);++binds;}
};
struct Renderer {
 Device device;Device* d=&device;int extended=3;int* fogActorPS=&extended;int* fogCloudsActorPS=&extended;bool fogActorCurrent=false;bool geometryReady=true;struct {bool shadows=true;}effects;struct{bool actorShadows=true,actorShadowDetail=true;}quality;
 bool sourceActive[2]={true,true},actorValid[2]={true,true};unsigned actorCasterCount[2]={1,1},frames=17;
 int white=0,source0=1,source1=2;int* neutralWhite=&white;int* actorShadow[2]={&source0,&source1};float actorMatrices[2][16];float actorFogBounds[2][4]={{-.1f,-.2f,.3f,.4f},{-.5f,-.6f,.7f,.8f}};
 bool profileSampled(){return true;}
 Renderer(){for(unsigned s=0;s<2;++s)for(unsigned i=0;i<16;++i)actorMatrices[s][i]=float(100*s+i);}
 void run(int source,bool volume){const float neutralMatrix[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,.5f,1};
 @BINDINGS@
 }
};
static void boundsCases(){
 unsigned seed=91;auto random=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216;};
 for(unsigned n=0;n<20000;++n){float center[3],low[3],high[3],m[16]={};
  for(unsigned k=0;k<3;++k){center[k]=(random()-.5f)*200000;const float e=.01f+random()*3;low[k]=center[k]-e;high[k]=center[k]+e;}
  const float angle=random()*6.2831853f,c=std::cos(angle),s=std::sin(angle);m[0]=c/12;m[4]=s/12;m[1]=-s/12;m[5]=c/12;m[10]=1.f/1280;m[14]=.5f;m[15]=1;
  m[12]=-(center[0]*m[0]+center[1]*m[4]);m[13]=-(center[0]*m[1]+center[1]*m[5]);float b[4];assert(NorthlightActorShadowMap::fogBounds(true,low,high,m,b));
  for(unsigned corner=0;corner<8;++corner)for(unsigned axis=0;axis<2;++axis){const float p[]={corner&1?high[0]:low[0],corner&2?high[1]:low[1],corner&4?high[2]:low[2]};
   const float q=std::fma(p[0],m[axis],std::fma(p[1],m[4+axis],std::fma(p[2],m[8+axis],m[12+axis])));
   if(q>=-1&&q<=1)assert(q>=b[axis]&&q<=b[axis+2]&&"FOG_CASTER_BOUND_TOO_TIGHT");
  }
  assert(!NorthlightActorShadowMap::fogBounds(false,low,high,m,b));low[0]=NAN;assert(!NorthlightActorShadowMap::fogBounds(true,low,high,m,b));
 }
}
int main(){boundsCases();unsigned cases=0;
 for(unsigned flags=0;flags<1024;++flags)for(int source=0;source<2;++source){Renderer r;
  r.geometryReady=flags&1;r.effects.shadows=flags&2;r.quality.actorShadows=flags&4;r.quality.actorShadowDetail=flags&8;
  r.sourceActive[source]=flags&16;r.actorValid[source]=flags&32;if(!(flags&64))r.actorShadow[source]=nullptr;r.actorCasterCount[source]=(flags&128)?7:0;
  if(!(flags&256))r.fogActorPS=nullptr;if(!(flags&512))r.fogCloudsActorPS=nullptr;
  const bool expected=flags==1023;r.run(source,true);assert(r.device.binds==4&&r.fogActorCurrent==expected&&"STALE_FOG_ACTOR_MAP");
  assert(r.device.info[0]==float(expected)&&r.device.info[1]==0&&"STALE_FOG_ACTOR_MAP");
  assert(r.device.texture==(expected?r.actorShadow[source]:r.neutralWhite));
  if(expected)assert(!std::memcmp(r.device.matrix,r.actorMatrices[source],64)&&"WRONG_FOG_LIGHT_SOURCE");
  else for(unsigned i=0;i<16;++i)assert(r.device.matrix[i]==(i==14?.5f:i==15?1.f:0.f));
  for(unsigned k=0;k<4;++k)assert(r.device.bounds[k]==(expected?r.actorFogBounds[source][k]:k<2?-1.f:1.f));
  r.run(1-source,false);assert(r.device.binds==4);++cases;
 }
 std::printf("FOG_CASTERS_BINDINGS_PASS cases=%u current_source current_map nonempty_casters inactive_shadow_quality_fallback no_extra_geometry\n",cases);
}
'''.replace('@BINDINGS@',body)
(a.output/'bindings.cpp').write_text(source)
for label,flags in [('O2',['-O2']),('O0',['-O0']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
 exe=a.output/label;subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(a.output/'bindings.cpp'),'-o',str(exe)],check=True);print(subprocess.check_output([str(exe)],text=True).strip(),flush=True)
for name,old,new,marker in [('stale','&&actorValid[source]','', 'STALE_FOG_ACTOR_MAP'),('source','fogActor?actorMatrices[source]','fogActor?actorMatrices[1-source]','WRONG_FOG_LIGHT_SOURCE')]:
 assert source.count(old)==1;cpp=a.output/(name+'.cpp');cpp.write_text(source.replace(old,new));exe=a.output/name;subprocess.run(['clang++','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(cpp),'-o',str(exe)],check=True)
 run=subprocess.run([str(exe)],capture_output=True,text=True);(a.output/(name+'.log')).write_text(run.stdout+run.stderr);assert run.returncode!=0 and marker in run.stderr
print('PASS stale-map and wrong-source destructive controls rejected',flush=True)
assert 'memcpy(actorMatrices[source],am,64);actorValid[source]=true;actorCasterCount[source]=drawnHere;' in host
assert host.index('d->SetTexture(4,textures[4]);',host.index('if(profile)profile->mark("FogMarch");'))<host.index('if(localDirectCount&&debug==0){',host.index('if(profile)profile->mark("FogMarch");'))
