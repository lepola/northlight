#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Execute the sky-only context recovery with fresh cameras and rejected reads."""
import os
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

world = fp.src('world_renderer.h').read_text()
device = fp.src('renderer.cpp').read_text()
context = world[world.index('    bool skyContext('):world.index('    bool wmoContext(')]
recover = device[device.index('    void recoverSkyContext('):device.index('    void skyObserve(')]
source = r'''
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
using DWORD=unsigned;
static unsigned GetTickCount(){return 100;}
static bool cameraOK=true,lightOK=true,decodeOK=true;
static const char* currentMap="Northrend";
static float pitch=0;
namespace NorthlightWorldCamera {
struct Camera{float view[16]={},camera[3]={};};struct Diagnostics{};
static unsigned reads=0;
static bool readSkyPhase(char (&map)[64],float sign,Camera& out,Diagnostics*){
 ++reads;assert(sign==1);if(!cameraOK)return false;
 std::strcpy(map,currentMap);out.view[0]=pitch;out.camera[0]=123;return true;
}}
struct Context{float pitch=0;};
namespace NorthlightWmoContext {
struct Lighting{};
static bool readGlobalLighting(const float* camera,Lighting&){assert(camera[0]==123);return lightOK;}
static bool context(const float* view,const Lighting&,Context& out){if(!decodeOK)return false;out.pitch=view[0];return true;}
}
struct Hold{void filter(NorthlightWmoContext::Lighting&,const char*,unsigned){}};
struct World{
 bool failed=false,valid=false;unsigned traceContext=0,updates=0;
 float projection[3]={};Context context;Hold globalLightHold;
 struct{std::string map="Northrend";}lastRequest;
 void updateWorldContext(const char* map,const float* camera,const NorthlightWmoContext::Lighting*){assert(map==lastRequest.map&&camera[0]==123);++updates;}
 bool hasContext()const{return valid;}
CONTEXT
};
struct D3DSURFACE_DESC{unsigned Width=1920,Height=1080;};
struct Device{
 World state;World* world=&state;struct{bool earlyValid=true;unsigned earlyWidth=1920,earlyHeight=1080;} sky;
 decltype(sky)* celestialDiscs=&sky;
 bool projectionValid=false,terrain=false,captured=true,depthOK=true,skySeen=true;
 float scaleX=1.2f,scaleY=2.1f,fogForward=1;
 unsigned depthBinds=0;
 bool bindWorldDepth(const D3DSURFACE_DESC&){++depthBinds;return depthOK;}
RECOVER
};
int main(){
 D3DSURFACE_DESC desc;
 for(float p:{0.f,.95f,1.f,-1.f}){
  Device d;pitch=p;d.recoverSkyContext(desc);
  assert(d.projectionValid&&d.terrain&&!d.captured&&d.state.context.pitch==p&&d.state.traceContext==5);
  assert(d.state.updates==1&&d.depthBinds==1&&d.state.projection[0]==1.2f&&d.state.projection[1]==2.1f);
  d.recoverSkyContext(desc);assert(d.state.updates==1&&d.depthBinds==1);
 }
 for(unsigned fail=0;fail<9;++fail){
  Device d;cameraOK=fail!=0;lightOK=fail!=1;decodeOK=fail!=2;
  currentMap=fail==3?"Azeroth":"Northrend";d.sky.earlyValid=fail!=4;
  if(fail==5)d.sky.earlyWidth=1280;if(fail==6)d.state.failed=true;if(fail==7)d.depthOK=false;if(fail==8)d.skySeen=false;
  d.recoverSkyContext(desc);assert(!d.projectionValid&&!d.terrain&&d.captured);
  assert(!d.depthBinds||fail==7);
 }
 std::puts("PASS sky-only recovery: fresh zenith/nadir camera, same-map projection, one update per frame, rejected read/depth/resize/reset");
}
'''.replace('CONTEXT', context).replace('RECOVER', recover)
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
cpp, exe = out/'sky_only.cpp', out/'sky_only.exe'
cpp.write_text(source)
compiler = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if os.name == 'nt' else ['c++']
subprocess.run([*compiler, '-std=c++17', '-O2', '-UNDEBUG', str(cpp), '-o', str(exe)], env=fp.zig_env() if os.name=='nt' else None, check=True)
subprocess.run([str(exe)], check=True)
