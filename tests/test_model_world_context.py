#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Execute the production context/gate bodies with injected camera/device reads.

No game or GPU is started. Camera matrix validation has its own native test.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp


def method(file, signature):
    source = (ROOT / file).read_text()
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


MODEL = method('src/world/world_renderer.h', 'bool modelContext(')
BEFORE = method('src/proxy/renderer.cpp', 'void beforeDraw(')
PROJECTION = method('src/proxy/renderer.cpp', 'bool readProjection(')
assert 'metadata.fog=NorthlightSpellFog::vsFogProven' in (ROOT / 'src/world/world_renderer.h').read_text()

HARNESS = r'''
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <map>
#include "projection.h"
#include "world_camera.h"
#include "wmo_context.h"
#include "world_draw_domain.h"
#include "ui_boundary.h"
void logf(const char*,...){}
namespace NorthlightDiagnostics {bool enabled(){return false;}}
template<class T>void drop(T*& p){p=nullptr;}
bool cameraOK=true,lightOK=true,contextOK=true;
float angle=0;
namespace TestCamera {
using NorthlightWorldCamera::Camera;using NorthlightWorldCamera::Diagnostics;
bool read(char (&map)[64],float sign,Camera& out,Diagnostics*){
    if(!cameraOK)return false;
    float data[12]={5800,300,1041,std::cos(angle),std::sin(angle),0,-std::sin(angle),std::cos(angle),0,0,0,1};
    float view[16]={};view[15]=1;
    for(unsigned k=0;k<3;++k){view[k*4]=data[6+k];view[k*4+1]=data[9+k];view[k*4+2]=data[3+k]*sign;}
    std::strcpy(map,"Northrend");return NorthlightWorldCamera::decode(data,view,sign,out);
}}
namespace TestLight {
using NorthlightWmoContext::Lighting;using NorthlightWmoContext::LightingHold;
bool readGlobalLighting(const float*,Lighting& l){l.lightDirection[2]=1;l.ambient[0]=.1f;l.direct[0]=1;return lightOK;}
bool context(const float* view,const Lighting& l,NorthlightWorldContext::TerrainContext& out){return contextOK&&NorthlightWmoContext::context(view,l,out);}
}
struct Ext {
    D3DVIEWPORT9 vp={0,0,1920,1080,0,.94f};DWORD z=1,zf=D3DCMP_LESSEQUAL;
    float rows[16]={};bool states=true,constants=true;
    Ext(){rows[0]=1.2f;rows[5]=1.7f;rows[10]=1000.f/999.f;rows[11]=1;rows[14]=-1000.f/999.f;}
    HRESULT GetVertexShaderConstantF(UINT reg,float* p,UINT n){
        if(!constants)return E_FAIL;
        for(unsigned r=0;r<n;++r)for(unsigned c=0;c<4;++c)p[r*4+c]=reg==2?rows[c*4+r]:rows[r*4+c];return S_OK;}
    HRESULT GetViewport(D3DVIEWPORT9* p){*p=vp;return S_OK;}
    HRESULT GetRenderState(D3DRENDERSTATETYPE t,DWORD* p){*p=t==D3DRS_ZENABLE?z:t==D3DRS_ZFUNC?zf:0;return states?S_OK:E_FAIL;}
    bool peekPixelShader(IDirect3DPixelShader9*& p){p=nullptr;return true;}
    HRESULT GetPixelShader(IDirect3DPixelShader9** p){*p=nullptr;return E_FAIL;}
};
struct World {
    bool hasOriginalFog()const{return legacyFogKnown;}
    Ext* d;bool failed=false,valid=false,legacyFogKnown=false,fogProof=false;
    struct Capture {bool skinned=true;unsigned projectionKind=2;bool fog=true;};
    std::map<IDirect3DVertexShader9*,Capture> captureShaders;
    NorthlightWorldContext::TerrainContext context;
    NorthlightWmoContext::LightingHold globalLightHold;
    unsigned traceContext=0,traceFog=0,modelContexts=0,updates=0;
    float projection[3]={};bool hasContext()const{return valid&&!failed;}
    void readOriginalFog(unsigned reg,bool proof){assert(reg==30);fogProof=proof;legacyFogKnown=proof;}
    void updateWorldContext(const char* map,const float* eye,const NorthlightWmoContext::Lighting*){assert(!std::strcmp(map,"Northrend"));assert(eye[2]==1041);++updates;}
    bool wmoContext(IDirect3DVertexShader9*){valid=true;return true;}
    void terrainContext(){valid=true;}
#define NorthlightWorldCamera TestCamera
#define NorthlightWmoContext TestLight
__MODEL__
#undef NorthlightWorldCamera
#undef NorthlightWmoContext
};
struct Device {
    struct ActorAO {unsigned resumes=0;void resume(){++resumes;}} actorAO;
    ActorAO* aoActorMask=&actorAO;bool aoActorOn=false;
    struct MaterialProjection {void setProjection(float,float,float){}};
    MaterialProjection* autoBump=nullptr;
    bool drawWaterMaterial=false;bool liquidMaterial(IDirect3DVertexShader9*,bool){return false;}
    Ext state;Ext* ext=&state;World owned{&state};World* world=&owned;
    static constexpr int kTagMask=3,kWaterTag=4;
    struct VsClass{int entry=0;bool wmo=false,skinned=true;} vc;
    struct Counts{unsigned wmo=0,fullPasses=0;} gateCounts;
    bool gateFrame=false,projectionValid=false,terrain=false,captured=false,drawWaterVS=false,drawUiHashVS=false;
    bool viewportOK=true,depthOK=true,hashBoundaryThisFrame=false;
    unsigned terrainDraws=0,projectionRejects=0,earlyResolveUndone=0,earlyResolveUndoneTotal=0,uiDraws=0,uiAfterTerrain=0,frame=0,drawCalls=0,binds=0,allocations=0;
    float nearZ=0,farZ=0,scaleX=0,scaleY=0,fogForward=1,worldMinDepth=0,worldMaxDepth=.94f;
    struct Early{bool earlyCaptured=false;void undo(){}}earlyDepth;
    std::map<IDirect3DPixelShader9*,int> psTags;
    const VsClass& classifyVs(IDirect3DVertexShader9*){return vc;}
    bool uiTextVs(IDirect3DVertexShader9*,int){return false;}
    bool zFuncAware=true;
    bool fullViewport(D3DSURFACE_DESC& d,D3DVIEWPORT9* p=nullptr,bool quiet=false){d.Width=1920;d.Height=1080;d.Format=D3DFMT_A8R8G8B8;if(p)*p=state.vp;return viewportOK;}
    bool bindWorldDepth(const D3DSURFACE_DESC&){++binds;return depthOK;}
    void resources(UINT,UINT,D3DFORMAT){++allocations;}
    void uiFallbackDraw(const VsClass&,IDirect3DVertexShader9*){}
    bool sampled(){return false;}void renderEffects(){}void spellReplay(){}
__PROJECTION__
__BEFORE__
};
int main(){
    auto* shader=reinterpret_cast<IDirect3DVertexShader9*>(1);unsigned cases=0;
    for(float a:{0.f,.01f,.1f,1.f,3.f}){
        angle=a;Device p;p.world->captureShaders[shader]={};p.beforeDraw(shader);
        assert(p.terrain&&p.projectionValid&&p.world->hasContext()&&p.binds==1&&p.allocations==1);
        assert(p.world->traceContext==4&&p.world->traceFog==1&&p.world->updates==1);
        assert(std::fabs(p.world->context.view[2]-std::cos(a))<1e-6f);
        p.beforeDraw(shader);assert(p.binds==1&&p.world->updates==1);++cases;
    }
    for(unsigned reject=0;reject<17;++reject){
        Device p;p.world->captureShaders[shader]={};cameraOK=lightOK=contextOK=true;
        switch(reject){
        case 0:p.vc.skinned=false;break;
        case 1:p.state.vp.MinZ=.9990234375f;p.state.vp.MaxZ=1;break;
        case 2:p.state.vp.MinZ=.998046875f;p.state.vp.MaxZ=.9990234375f;break;
        case 3:p.state.vp.MaxZ=1;break;
        case 4:p.state.z=0;break;
        case 5:p.state.zf=D3DCMP_ALWAYS;break;
        case 6:p.viewportOK=false;break;
        case 7:p.state.states=false;break;
        case 8:p.state.rows[15]=1;break;
        case 9:p.state.constants=false;break;
        case 10:p.world->captureShaders.clear();break;
        case 11:p.world->captureShaders[shader].projectionKind=1;break;
        case 12:cameraOK=false;break;
        case 13:lightOK=false;break;
        case 14:contextOK=false;break;
        case 15:p.world->failed=true;break;
        case 16:p.world->captureShaders[shader].skinned=false;break;
        }
        p.beforeDraw(shader);assert(!p.terrain&&!p.projectionValid&&p.binds==0&&p.allocations==0);++cases;
    }
    cameraOK=lightOK=contextOK=true;
    {Device p;p.world->captureShaders[shader].fog=false;p.beforeDraw(shader);assert(p.terrain&&!p.world->fogProof&&p.world->traceFog==0);++cases;}
    {Device p;p.depthOK=false;p.world->captureShaders[shader]={};p.beforeDraw(shader);assert(!p.terrain&&p.allocations==0);++cases;}
    for(int tag:{1,0}){Device p;p.vc.entry=tag;p.vc.wmo=tag==0;p.vc.skinned=false;p.beforeDraw(shader);assert(p.terrain&&p.projectionValid);++cases;}
    for(bool on:{false,true}){Device p;p.aoActorOn=on;p.captured=true;p.vc.entry=1;p.vc.skinned=false;
        p.beforeDraw(shader);assert(p.terrain&&!p.captured&&p.actorAO.resumes==unsigned(on));++cases;}
    std::printf("production context/gate: %u cases passed; no terrain dependency; sky/UI/bad camera rejected\n",cases);
}
'''

if __name__ == '__main__':
    includes = [v for p in sorted((ROOT / 'src').iterdir()) if p.is_dir() for v in ('-I', str(p))]
    zig = os.environ.get('NORTHLIGHT_ZIG') or str(fp.zig())
    compiler = [zig, 'c++', '-target', 'x86-windows-gnu'] if zig else shlex.split(os.environ.get('CXX', 'c++'))
    with tempfile.TemporaryDirectory(prefix='model-context-') as tmp:
        source = Path(tmp) / 'test.cpp'
        source.write_text(HARNESS.replace('__MODEL__', MODEL).replace('__BEFORE__', BEFORE).replace('__PROJECTION__', PROJECTION))
        binary = Path(tmp) / ('test.exe' if os.name == 'nt' else 'test')
        subprocess.run([*compiler, '-std=c++17', '-O2', '-UNDEBUG', *includes, str(source), '-luser32', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
