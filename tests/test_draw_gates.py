#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.187 per-frame draw gates: the draw hooks do the same work as 0.3.184.

The 0.3.184 hook members and the four Draw* overrides are kept below verbatim. The test compiles
them, and the current members extracted from renderer.cpp (drawHook and its helpers, the gates,
prepareDraw, terrainShadowDraw, planTerrainShadowSwap, DrawPrimitive), against the same mocks of
the device, the world, the sky renderer and the blob filter. It then runs both through randomized
frames with FrameDrawGates=0 and =1: the per-frame inputs change only at the frame boundary, the
per-draw state changes between draws (applied and failed rise, terrain, the world context and
composited shadows flip), and the mocks fault at random. Every side-effecting call (capture,
water capture, sky claim gate and claims, viewport reads, blob claims, pixel shader swaps, the
game's draw, Release, log lines including the fault's stage) must be the same and in the same
order, with the same results and end state. A static part checks that the other three overrides
pass 0.3.184's capture and draw calls unchanged, and that each latched input is written only where
the gates are latched again. Native clang++, plain -O2 and ASan/UBSan. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile

# Verbatim 0.3.184 (src/proxy/renderer.cpp, 264c601): the hook members ...
OLD_MEMBERS = r'''
    template<class Work> bool extensionWork(const char* stage,Work&& work) noexcept {
        if(extensionFault)return false;
        return NorthlightExtensionGuard::run(std::forward<Work>(work),[&](NorthlightExtensionGuard::Fault fault) noexcept {
            extensionFault=true;failed=true;enabled=false;
            MEMORYSTATUSEX memory={};memory.dwLength=sizeof memory;GlobalMemoryStatusEx(&memory);
            logf("EXTENSION fault stage=%s type=%u availableVirtualMiB=%llu; original game calls retained; restart required",stage,unsigned(fault),(unsigned long long)(memory.ullAvailVirtual>>20));
        });
    }
    template<class Capture> void prepareDraw(Capture capture,UINT count) {
        mirrorState.gate.noteFirst(mirrorState.gate.drawTid); /* 0.3.180 (D0): the census' first draw */
        dropTerrainShadowSwap();
        extensionWork("draw capture/effects",[&]{prepareDrawImpl(capture,count);});
    }
    void planTerrainShadowSwap(IDirect3DVertexShader9* vs){
        if(!world||applied||!enabled||failed||!terrain||debugMode!=0||worldDebug!=0||!world->terrainShadowActive())return;
        auto tag=vsTags.find(vs);if(tag==vsTags.end()||(tag->second&kTagMask)!=1)return;
        IDirect3DPixelShader9* ps=nullptr;if(FAILED(ext->GetPixelShader(&ps))||!ps)return;
        IDirect3DPixelShader9* replacement=world->terrainShadowReplacement(ps);
        if(!replacement){ps->Release();return;}
        shadowSwapOriginal=ps;shadowSwapReplacement=replacement;
    }
    void dropTerrainShadowSwap(){if(shadowSwapOriginal)shadowSwapOriginal->Release();shadowSwapOriginal=shadowSwapReplacement=nullptr;}
    template<class Draw> HRESULT terrainShadowDraw(bool claimed,Draw draw){
        IDirect3DPixelShader9* original=shadowSwapOriginal;IDirect3DPixelShader9* replacement=shadowSwapReplacement;
        shadowSwapOriginal=shadowSwapReplacement=nullptr;
        HRESULT hr=D3D_OK;
        if(!claimed&&replacement){ext->SetPixelShader(replacement);hr=draw();ext->SetPixelShader(original);++terrainShadowDraws;}
        else if(!claimed)hr=draw();
        if(original)original->Release();
        return hr;
    }
    bool blobFilterActive()const{return shadowBlobs&&enabled&&effectKeys.settings.shadows&&!applied&&terrain&&!failed&&world&&world->hasContext()&&world->actorShadowsEnabled();}
    bool blobClaim(UINT count){
        if(gateFrame){++gateCounts.blobCalls;if(count&&count<=256)++gateCounts.blobTextures;}
        const bool claimed=shadowBlobs->claim(count);if(gateFrame&&claimed)++gateCounts.blobClaimed;return claimed;
    }
'''
# ... and the four Draw* overrides.
OLD_DRAWS = r'''
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE t,UINT start,UINT count) override { Guard mirrorLock(mirrorState.gate);prepareDraw([&](IDirect3DVertexShader9* vs){world->capture(t,0,0,0,start,count,false,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::NoUserPointer,[&]{return ext->DrawPrimitive(t,start,count);});},count);bool claimed=false;extensionWork("native sky claim",[&]{if(celestialDiscs&&enabled&&!applied&&count<=4&&celestialDiscs->nativeClaimPossible(t,count)){D3DSURFACE_DESC desc;if(fullViewport(desc)){claimed=celestialDiscs->claimNativeGlare(t,count);if(!claimed)claimed=celestialDiscs->claimNativeDraw(t,count,true,[&](const char* map,const float* camera){return world->celestialPalette(map,camera);});}}});if(!claimed&&blobFilterActive())extensionWork("blob shadow filter",[&]{claimed=blobClaim(count);if(claimed&&blobSignatureReports<4){++blobSignatureReports;IDirect3DVertexShader9* bvs=nullptr;IDirect3DPixelShader9* bps=nullptr;ext->GetVertexShader(&bvs);ext->GetPixelShader(&bps);auto vi=vsHashes.find(bvs);auto pi=psHashes.find(bps);logf("SHADOWBLOB draw signature vs=%016llx ps=%016llx primitives=%u",(unsigned long long)(vi==vsHashes.end()?0:vi->second),(unsigned long long)(pi==psHashes.end()?0:pi->second),count);drop(bvs);drop(bps);}});HRESULT hr=terrainShadowDraw(claimed,[&]{return ext->DrawPrimitive(t,start,count);});if(!claimed)extensionWork("native sky observation",[&]{if(celestialDiscs&&enabled&&!applied&&count<=4&&celestialDiscs->nativeObservePossible(hr,t,count)){D3DSURFACE_DESC desc;if(fullViewport(desc))celestialDiscs->observeNativeDraw(hr,t,count,true);}});return hr;}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT base,UINT min,UINT vertices,UINT start,UINT count) override { Guard mirrorLock(mirrorState.gate);prepareDraw([&](IDirect3DVertexShader9* vs){world->capture(t,base,min,vertices,start,count,true,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::NoUserPointer,[&]{return ext->DrawIndexedPrimitive(t,base,min,vertices,start,count);});},count);bool claimed=false;extensionWork("native sky claim",[&]{if(celestialDiscs&&enabled&&!applied&&count<=4&&celestialDiscs->nativeClaimPossible(t,count)){D3DSURFACE_DESC desc;if(fullViewport(desc)){claimed=celestialDiscs->claimNativeGlare(t,count);if(!claimed)claimed=celestialDiscs->claimNativeDraw(t,count,true,[&](const char* map,const float* camera){return world->celestialPalette(map,camera);});}}});if(!claimed&&blobFilterActive())extensionWork("blob shadow filter",[&]{claimed=blobClaim(count);if(claimed&&blobSignatureReports<4){++blobSignatureReports;IDirect3DVertexShader9* bvs=nullptr;IDirect3DPixelShader9* bps=nullptr;ext->GetVertexShader(&bvs);ext->GetPixelShader(&bps);auto vi=vsHashes.find(bvs);auto pi=psHashes.find(bps);logf("SHADOWBLOB draw signature vs=%016llx ps=%016llx primitives=%u",(unsigned long long)(vi==vsHashes.end()?0:vi->second),(unsigned long long)(pi==psHashes.end()?0:pi->second),count);drop(bvs);drop(bps);}});HRESULT hr=terrainShadowDraw(claimed,[&]{return ext->DrawIndexedPrimitive(t,base,min,vertices,start,count);});if(!claimed)extensionWork("native sky observation",[&]{if(celestialDiscs&&enabled&&!applied&&count<=4&&celestialDiscs->nativeObservePossible(hr,t,count)){D3DSURFACE_DESC desc;if(fullViewport(desc))celestialDiscs->observeNativeDraw(hr,t,count,true);}});return hr;}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT count,const void* data,UINT stride) override { Guard mirrorLock(mirrorState.gate);prepareDraw([&](IDirect3DVertexShader9* vs){world->captureUP(t,0,0,count,nullptr,D3DFMT_UNKNOWN,data,stride,false,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::UserVertices,[&]{return ext->DrawPrimitiveUP(t,count,data,stride);});},count);bool claimed=false;extensionWork("native sky claim",[&]{if(celestialDiscs&&enabled&&!applied&&count<=4&&celestialDiscs->nativeClaimPossible(t,count)){D3DSURFACE_DESC desc;if(fullViewport(desc)){claimed=celestialDiscs->claimNativeGlare(t,count);if(!claimed)claimed=celestialDiscs->claimNativeDraw(t,count,true,[&](const char* map,const float* camera){return world->celestialPalette(map,camera);});}}});if(!claimed&&blobFilterActive())extensionWork("blob shadow filter",[&]{claimed=blobClaim(count);if(claimed&&blobSignatureReports<4){++blobSignatureReports;IDirect3DVertexShader9* bvs=nullptr;IDirect3DPixelShader9* bps=nullptr;ext->GetVertexShader(&bvs);ext->GetPixelShader(&bps);auto vi=vsHashes.find(bvs);auto pi=psHashes.find(bps);logf("SHADOWBLOB draw signature vs=%016llx ps=%016llx primitives=%u",(unsigned long long)(vi==vsHashes.end()?0:vi->second),(unsigned long long)(pi==psHashes.end()?0:pi->second),count);drop(bvs);drop(bps);}});HRESULT hr=terrainShadowDraw(claimed,[&]{return ext->DrawPrimitiveUP(t,count,data,stride);});if(!claimed)extensionWork("native sky observation",[&]{if(celestialDiscs&&enabled&&!applied&&count<=4&&celestialDiscs->nativeObservePossible(hr,t,count)){D3DSURFACE_DESC desc;if(fullViewport(desc))celestialDiscs->observeNativeDraw(hr,t,count,true);}});return hr;}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT min,UINT vertices,UINT count,const void* indices,D3DFORMAT fmt,const void* data,UINT stride) override { Guard mirrorLock(mirrorState.gate);prepareDraw([&](IDirect3DVertexShader9* vs){world->captureUP(t,min,vertices,count,indices,fmt,data,stride,true,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::UserVerticesAndIndices,[&]{return ext->DrawIndexedPrimitiveUP(t,min,vertices,count,indices,fmt,data,stride);});},count);bool claimed=false;extensionWork("native sky claim",[&]{if(celestialDiscs&&enabled&&!applied&&count<=4&&celestialDiscs->nativeClaimPossible(t,count)){D3DSURFACE_DESC desc;if(fullViewport(desc)){claimed=celestialDiscs->claimNativeGlare(t,count);if(!claimed)claimed=celestialDiscs->claimNativeDraw(t,count,true,[&](const char* map,const float* camera){return world->celestialPalette(map,camera);});}}});if(!claimed&&blobFilterActive())extensionWork("blob shadow filter",[&]{claimed=blobClaim(count);if(claimed&&blobSignatureReports<4){++blobSignatureReports;IDirect3DVertexShader9* bvs=nullptr;IDirect3DPixelShader9* bps=nullptr;ext->GetVertexShader(&bvs);ext->GetPixelShader(&bps);auto vi=vsHashes.find(bvs);auto pi=psHashes.find(bps);logf("SHADOWBLOB draw signature vs=%016llx ps=%016llx primitives=%u",(unsigned long long)(vi==vsHashes.end()?0:vi->second),(unsigned long long)(pi==psHashes.end()?0:pi->second),count);drop(bvs);drop(bps);}});HRESULT hr=terrainShadowDraw(claimed,[&]{return ext->DrawIndexedPrimitiveUP(t,min,vertices,count,indices,fmt,data,stride);});if(!claimed)extensionWork("native sky observation",[&]{if(celestialDiscs&&enabled&&!applied&&count<=4&&celestialDiscs->nativeObservePossible(hr,t,count)){D3DSURFACE_DESC desc;if(fullViewport(desc))celestialDiscs->observeNativeDraw(hr,t,count,true);}});return hr;}
'''
HERE=Path(__file__).resolve().parent
renderer=fp.src('renderer.cpp').read_text()
world_h=fp.src('world_renderer.h').read_text()


def member(text,signature):
    """The member starting at signature (its whole first line) through its closing brace."""
    a=text.index(signature);b=text.index('{',a);depth=1;end=b+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[text.rfind('\n',0,a)+1:end]


assert renderer.count('    // 0.3.187 per-frame draw gates')==1
gates_block=renderer[renderer.index('    // 0.3.187 per-frame draw gates'):renderer.index('    // 0.3.154: blob shadow claim')]
new_members='\n'.join([member(renderer,'template<class Work> bool extensionWork('),member(renderer,'template<class Capture> void prepareDraw(Capture capture,UINT count)'),
    renderer[renderer.index('    // 0.3.196 (task 12): one-entry per-draw vertex shader classification'):renderer.index('    std::unordered_map<IDirect3DPixelShader9*, uint64_t> psHashes;')],
    member(renderer,'void planTerrainShadowSwap('),member(renderer,'void dropTerrainShadowSwap('),member(renderer,'template<class Draw> HRESULT terrainShadowDraw('),
    gates_block,member(renderer,'bool blobFilterActive()const'),member(renderer,'NorthlightShadowBlobFilter::Result blobClaim(UINT count)'),
    member(renderer,'HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE t,UINT start,UINT count) override')])
old_draws=[l for l in OLD_DRAWS.strip('\n').split('\n')]
assert len(old_draws)==4

checks={}
checkflag=fp.src('shadow_blob_filter.h').read_text()
checks['real source: no compile-time switch left; BlobShadowStrength gates blobFilterActive and the latch (the equivalence runs use strength 0 = skip)']=('HidesNativeBlobs' not in checkflag and 'HidesNativeBlobs' not in renderer
    and 'bool blobFilterActive()const{return shadowBlobs&&shadowBlobs->active()&&' in renderer and 'in.blobs=shadowBlobs!=nullptr&&shadowBlobs->active();' in renderer)
# 0.3.193 faint swap: planned in the guarded claim, applied around the real draw outside every extension region, texture 0 restored.
swap_src=member(renderer,'template<class Draw> HRESULT blobFaintDraw(')
checks['faint swap: SetTexture(faint), the draw, SetTexture(original), then Release; no extensionWork around it']=(
    'ext->SetTexture(0,faint);' in swap_src and 'shadowBlobs->faintTexture()' in swap_src and swap_src.index('ext->SetTexture(0,faint);')<swap_src.index('draw();')<swap_src.index('ext->SetTexture(0,original);')<swap_src.rindex('original->Release();')
    and 'extensionWork' not in swap_src and 'return terrainShadowDraw(claimed,draw)' in swap_src)
checks['the plan keeps only the original (no second GetTexture, no faint pointer member)']=('GetTexture' not in member(renderer,'void planBlobFaint(') and 'blobFaint;' not in renderer and 'blobFaint=' not in renderer)
checks['faint plan dropped at the start of every draw on both paths']=(renderer.count('dropBlobFaint();prepareDraw(capture,count);')==1 and renderer.count('dropTerrainShadowSwap();dropBlobFaint();')==1 and renderer.count('rainBlendDraw(rainBlend,claimed||mist,draw)')==2 and renderer.count('blobFaintDraw(claimed,draw)')==1)
# The other three overrides: 0.3.184's capture call and real draw, passed to drawHook unchanged.
def old_parts(line):
    cap=re.search(r'prepareDraw\(\[&\]\(IDirect3DVertexShader9\* vs\)\{(.*?)captureWater\(vs,(\w+::\w+),\[&\]\{return (ext->\w+\([^)]*\));\}\);\},count\);',line)
    sig=line[:line.index(' override {')]
    return sig,cap.group(1),cap.group(2),cap.group(3),re.search(r'terrainShadowDraw\(claimed,\[&\]\{return (ext->\w+\([^)]*\));\}\)',line).group(1)
def new_parts(sig):
    body=member(renderer,sig+' override')
    draw=re.search(r'auto draw=\[&\]\{return (ext->\w+\([^)]*\));\};',body).group(1)
    cap=re.search(r'return drawHook\(t,count,\[&\]\(IDirect3DVertexShader9\* vs\)\{(.*?)captureWater\(vs,(\w+::\w+),draw\);\},draw\);\}',body)
    return cap.group(1),cap.group(2),draw,body
ok=True
for line in old_draws:
    sig,cap,water,water_draw,draw=old_parts(line)
    assert water_draw==draw,(sig,water_draw,draw)
    ncap,nwater,ndraw,body=new_parts(sig)
    ok&=ncap==cap and nwater==water and ndraw==draw and 'Guard mirrorLock(mirrorState.gate);' in body
checks['four overrides: 0.3.184 capture, water capture and draw, unchanged, through drawHook']=ok and renderer.count('return drawHook(t,count,')==4
# The latched inputs rise only where the gates are latched again (draw_gates.h): device creation,
# Reset, and finishFrameImpl before its final latchDrawGates().
finish=member(renderer,'void finishFrameImpl() {')
reset=member(renderer,'HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pp) override')
ctor=renderer[renderer.index('    Device(IDirect3DDevice9* d,IDirect3D9* p)'):renderer.index('    ~Device() {')]
code=re.sub(r'"(?:[^"\\]|\\.)*"','""',re.sub(r'/\*.*?\*/|//[^\n]*','',renderer))  # no comments, no string literals
writes=lambda name,text=None:re.findall(r'(?<![.>\w])'+name+r'\s*=(?!=)\s*([^;,]*)',code if text is None else text)
checks['enabled: rises only by F10 in finishFrameImpl; else only lowered']=sorted(writes('enabled'))==['!enabled','false','true'] and 'if(k10&&!key10){enabled=!enabled;' in finish
checks['failed=false only in Reset (latched there) and the retry in finishFrameImpl']=(len(re.findall(r'\bfailed\s*=\s*false',code))==3  # + the member initialiser
    and 'failed=false;latchDrawGates();' in reset and 'failed=false;logf("Effects retry requested after frame cleanup");' in finish
    and finish.index('failed=false;logf("Effects retry')<finish.index('latchDrawGates();'))
checks['F9/F12/setEffects only in finishFrameImpl, before the latch']=(code.count('effectKeys.poll(')==1 and sorted(writes('worldDebug'))==['(worldDebug+1)%4','0'] and code.count('->setEffects(')==1
    and all(x in finish and finish.index(x)<finish.index('latchDrawGates();') for x in ('effectKeys.poll(','worldDebug=(worldDebug+1)%4;','world->setEffects(settings);'))
    and finish.rstrip().endswith('latchDrawGates(); /* 0.3.187: the next frame\'s draw gates, after every input above */\n    }'))
checks['renderers created in the constructor (latched there), released only at destroy']=(
    all(code.count(x+'=std::make_unique<')==1 and x+'=std::make_unique<' in ctor for x in ('world','celestialDiscs','shadowBlobs'))
    and ctor.index('shadowBlobs=std::make_unique<')<ctor.index('frameDrawGates=world->frameDrawGates();latchDrawGates();')
    and code.count('shadowBlobs.reset();celestialDiscs.reset();')==1 and code.count('world.reset();')==1)
wcode=re.sub(r'/\*.*?\*/|//[^\n]*','',world_h)
checks['ActorShadows and effects.shadows: device creation and setEffects only']=(wcode.count('loadQuality();')==1 and wcode.count('effects=next;')==1
    and re.search(r'\beffects\s*=(?!=)',wcode.replace('effects=next;','')) is None and re.search(r'\bquality\s*=(?!=)',wcode[:wcode.index('void loadQuality(){')]+wcode[wcode.index('void loadQuality(){')+len(member(world_h,'void loadQuality(){')):]) is None
    and 'bool shadowsRequested()const{return effects.shadows;}' in world_h)
checks['real draw outside every extension region']=all('extensionWork' not in l for l in gates_block.split('\n') if 'terrainShadowDraw(claimed,draw)' in l)
checks['hook timer latched after the next frame number and its drawGate T/U flag']=(finish.index('++frame;')<finish.index('gateUntimed=gateFrame&&')<finish.index('latchDrawGates();')
    and 'hookTimer=sampledDrawTimers()&&NorthlightRenderThreadProbe::profiling()?&cpuDrawHooks:nullptr;' in gates_block)
checks['CPU profile logs drawHooks and frameDrawGates']='drawHooks=%.3fms frameDrawGates=%u' in renderer and 'NorthlightRenderThreadProbe::profiling()?cpuDrawHooks*ms:-1.0,unsigned(frameDrawGates)' in renderer

HARNESS=r'''
#include "extension_guard.h"
#include "draw_gates.h"
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <map>
#include <new>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
typedef long HRESULT;typedef unsigned UINT;typedef int INT;typedef long long LONGLONG;typedef unsigned DWORD;
#define TRUE 1
#define FALSE 0
enum D3DRENDERSTATETYPE{D3DRS_ALPHABLENDENABLE=27,D3DRS_SRCBLEND=19,D3DRS_DESTBLEND=20,D3DRS_BLENDOP=171,D3DRS_COLORWRITEENABLE=168,D3DRS_SEPARATEALPHABLENDENABLE=206,D3DRS_ZWRITEENABLE=14,D3DRS_SRGBWRITEENABLE=194,D3DRS_SCISSORTESTENABLE=174,D3DRS_STENCILENABLE=52,D3DRS_ZFUNC=23};enum{D3DCMP_LESS=2,D3DCMP_LESSEQUAL=4};struct RECT{long left,top,right,bottom;};enum{D3DBLEND_ZERO=1,D3DBLEND_ONE=2,D3DBLEND_SRCCOLOR=3,D3DBLEND_SRCALPHA=5,D3DBLEND_INVSRCALPHA=6,D3DBLEND_DESTCOLOR=9,D3DBLENDOP_ADD=1,D3DBLENDOP_MAX=5,D3DCOLORWRITEENABLE_ALPHA=8,D3DCLEAR_TARGET=1,D3DUSAGE_RENDERTARGET=1};enum D3DMULTISAMPLE_TYPE{D3DMULTISAMPLE_NONE=0};enum D3DPOOL{D3DPOOL_DEFAULT=0};struct D3DVIEWPORT9{DWORD X=0,Y=0,Width=0,Height=0;float MinZ=0,MaxZ=1;};struct D3DRECT{long x1,y1,x2,y2;};typedef DWORD D3DCOLOR; /* 0.3.201 (rain): rainMaskPass compiles; it never runs here (test_weather_detect drives it) */
namespace NorthlightWeather{enum class Kind{None,Rain,Snow};} /* 0.3.199 (rain): rainBlendDraw compiles; it never runs here (hot stays null, test_weather_detect drives it) */
#define STDMETHODCALLTYPE
#define FAILED(hr) (((HRESULT)(hr))<0)
#define SUCCEEDED(hr) (((HRESULT)(hr))>=0)
constexpr HRESULT D3D_OK=0;
enum D3DPRIMITIVETYPE{D3DPT_POINTLIST=1,D3DPT_LINELIST,D3DPT_LINESTRIP,D3DPT_TRIANGLELIST,D3DPT_TRIANGLESTRIP,D3DPT_TRIANGLEFAN};
enum D3DFORMAT{D3DFMT_UNKNOWN=0,D3DFMT_A8R8G8B8=21};
struct D3DSURFACE_DESC{UINT Width=0,Height=0;D3DMULTISAMPLE_TYPE MultiSampleType=D3DMULTISAMPLE_NONE;DWORD MultiSampleQuality=0;};
struct IDirect3DSurface9{void Release(){}void GetDesc(D3DSURFACE_DESC*){}};
struct MEMORYSTATUSEX{unsigned dwLength=0;unsigned long long ullAvailVirtual=0;};
static int GlobalMemoryStatusEx(MEMORYSTATUSEX* m){m->ullAvailVirtual=1ull<<30;return 1;}
using Trace=std::vector<std::string>;
// Answers depend on the scenario, frame, draw, query name and its ordinal within the draw, never on
// which code path asked; tagged calls fault (std::bad_alloc) one time in faultOneIn.
struct Env{
    std::uint64_t seed=0;unsigned frame=0,draw=0,faultOneIn=0;std::map<std::string,unsigned> ord;Trace* trace=nullptr;
    std::uint64_t h(const std::string& what){const unsigned k=ord[what]++;std::uint64_t x=seed*0x9E3779B97F4A7C15ull^(std::uint64_t(frame)<<32)^(std::uint64_t(draw)<<12)^k;
        for(char c:what)x=(x^std::uint8_t(c))*0x100000001B3ull;x^=x>>29;x*=0xBF58476D1CE4E5B9ull;x^=x>>32;return x;}
    bool bit(const std::string& what,unsigned oneIn=2){return h(what)%oneIn==0;}
    void call(const std::string& line,const char* fault=nullptr){trace->push_back(line);if(fault&&faultOneIn&&h(fault)%faultOneIn==0){trace->push_back(std::string("throw ")+fault);throw std::bad_alloc();}}
};
struct IDirect3DVertexShader9{int id;Env* env;void Release(){env->trace->push_back("Release vs"+std::to_string(id));}};
struct IDirect3DPixelShader9{int id;Env* env;void Release(){env->trace->push_back("Release ps"+std::to_string(id));}};
struct IDirect3DBaseTexture9{int id;Env* env;void Release(){env->trace->push_back("Release tex"+std::to_string(id));}};
struct IDirect3DTexture9:IDirect3DBaseTexture9{HRESULT GetSurfaceLevel(UINT,IDirect3DSurface9**){return D3D_OK;}};
static std::string psName(IDirect3DPixelShader9* p){return p?"ps"+std::to_string(p->id):"null";}
static std::string drawName(D3DPRIMITIVETYPE t,UINT a,UINT c){return std::to_string(int(t))+","+std::to_string(a)+","+std::to_string(c);}
struct MockExt{
    Env* env=nullptr;IDirect3DPixelShader9 ps[2]={{1,nullptr},{2,nullptr}};IDirect3DVertexShader9 vs{1,nullptr};
    HRESULT GetPixelShader(IDirect3DPixelShader9** out){env->call("GetPixelShader");const unsigned w=unsigned(env->h("ps")%3);*out=w<2?&ps[w]:nullptr;return D3D_OK;}
    HRESULT SetPixelShader(IDirect3DPixelShader9* p){env->call("SetPixelShader "+psName(p));return D3D_OK;}
    IDirect3DTexture9 original;
    HRESULT GetTexture(unsigned stage,IDirect3DBaseTexture9** out){env->call("GetTexture "+std::to_string(stage),"fault gettexture");*out=env->bit("no texture",8)?nullptr:&original;if(!*out)env->trace->push_back("GetTexture null");return *out?D3D_OK:HRESULT(-1);}
    HRESULT SetTexture(unsigned stage,IDirect3DBaseTexture9* t){env->call("SetTexture "+std::to_string(stage)+(t==&original?" orig":" faint"));return D3D_OK;}
    HRESULT GetRenderTarget(DWORD,IDirect3DSurface9**){return HRESULT(-1);}HRESULT SetRenderTarget(DWORD,IDirect3DSurface9*){return D3D_OK;}HRESULT GetDepthStencilSurface(IDirect3DSurface9**){return HRESULT(-1);}HRESULT GetViewport(D3DVIEWPORT9*){return D3D_OK;}HRESULT SetViewport(const D3DVIEWPORT9*){return D3D_OK;}HRESULT GetScissorRect(RECT*){return D3D_OK;}HRESULT SetScissorRect(const RECT*){return D3D_OK;}HRESULT Clear(DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD){return D3D_OK;}HRESULT CreateTexture(UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,void*){return HRESULT(-1);}HRESULT CreateRenderTarget(UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,int,IDirect3DSurface9**,void*){return HRESULT(-1);}
    HRESULT GetRenderState(D3DRENDERSTATETYPE,DWORD* v){*v=0;return D3D_OK;}HRESULT SetRenderState(D3DRENDERSTATETYPE,DWORD){return D3D_OK;}
    HRESULT GetVertexShader(IDirect3DVertexShader9** out){env->call("GetVertexShader");*out=&vs;return D3D_OK;}
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE t,UINT s,UINT c){env->call("DrawPrimitive "+drawName(t,s,c));return env->bit("draw failure",6)?HRESULT(-2005530516):D3D_OK;}
};
struct MockWorld{
    Env* env=nullptr;bool context=true,actorShadows=true,shadows=true,composited=true;IDirect3DPixelShader9 replacement{9,nullptr};
    bool hasContext()const{return context;}
    bool rainBlendSetting()const{return false;}
    bool actorShadowsEnabled()const{return actorShadows;}
    bool shadowsRequested()const{return shadows;}
    bool terrainShadowActive()const{return shadows&&composited;}
    bool frameDrawGates()const{return true;}
    bool recognizesWmo(IDirect3DVertexShader9*)const{return false;}bool isWorldShader(IDirect3DVertexShader9*)const{return false;}bool isSkinnedShader(IDirect3DVertexShader9*)const{return false;}
    IDirect3DPixelShader9* terrainShadowReplacement(IDirect3DPixelShader9* p){env->call("terrainShadowReplacement "+psName(p),"fault replacement");return env->bit("replacement")?&replacement:nullptr;}
    void capture(D3DPRIMITIVETYPE t,INT base,UINT min,UINT vertices,UINT start,UINT count,bool indexed,IDirect3DVertexShader9*,bool a,bool b){
        env->call("capture "+drawName(t,start,count)+" "+std::to_string(base+min+vertices)+std::to_string(indexed)+std::to_string(a)+std::to_string(b),"fault capture");}
    int celestialPalette(const char*,const float*){env->call("celestialPalette");return 0;}
};
struct MockSky{
    Env* env=nullptr;unsigned claimGate=0;
    bool nativeClaimPossible(D3DPRIMITIVETYPE t,UINT c){env->call("nativeClaimPossible "+drawName(t,0,c),"fault claim gate");const bool r=env->bit("claim possible");if(!r)++claimGate;return r;}
    bool claimNativeGlare(D3DPRIMITIVETYPE,UINT){env->call("claimNativeGlare","fault glare");return env->bit("glare",4);}
    template<class Palette> bool claimNativeDraw(D3DPRIMITIVETYPE,UINT,bool skyPhase,Palette palette){env->call("claimNativeDraw "+std::to_string(skyPhase),"fault claim draw");if(env->bit("palette"))palette("map",nullptr);return env->bit("claim draw",3);}
    bool nativeObservePossible(HRESULT hr,D3DPRIMITIVETYPE,UINT){env->call("nativeObservePossible "+std::to_string(hr));return SUCCEEDED(hr)&&env->bit("observe possible");}
    void observeNativeDraw(HRESULT hr,D3DPRIMITIVETYPE,UINT,bool){env->call("observeNativeDraw "+std::to_string(hr),"fault observe");}
};
// Result converts to bool (Skip) for the 0.3.184 code; the current code reads claim and original.
struct NorthlightShadowBlobFilter{enum class Claim{None,Skip,Faint};struct Result{Claim claim=Claim::None;IDirect3DBaseTexture9* original=nullptr;operator bool()const{return claim==Claim::Skip;}};};
using ClaimResult=NorthlightShadowBlobFilter::Result;
// faintMode off (old-vs-new runs): Skip or None as before 0.3.193. On: None, Skip or Faint (Faint = GetTexture(0) taken inside the claim and
// handed back AddRef'd, None when that fails, like the real claim()), and a null faint texture now and then.
struct MockBlobs{Env* env=nullptr;MockExt* ext=nullptr;bool faintMode=false;unsigned strengthValue=0;IDirect3DTexture9 faint;
    bool active()const{return strengthValue<100;}
    ClaimResult claim(UINT c){env->call("blob claim "+std::to_string(c),"fault blob");using C=NorthlightShadowBlobFilter::Claim;
        if(!faintMode)return {env->bit("blob",3)?C::Skip:C::None};
        const unsigned r=unsigned(env->h("blob")%4);if(r==0)return {C::Skip};if(r==1)return {};
        IDirect3DBaseTexture9* original=nullptr;if(FAILED(ext->GetTexture(0,&original))||!original)return {};   /* claim() returns None when its GetTexture fails */
        return {C::Faint,original};}
    IDirect3DTexture9* faintTexture(){return env->bit("faint texture",8)?nullptr:&faint;}};
struct Gate{int drawTid=7;void noteFirst(int){}};
struct MirrorStateMock{Gate gate;bool textureKnown[1]={};void* textures[1]={};};
struct WeatherDetectMock{const void* hot=nullptr;NorthlightWeather::Kind hotKind=NorthlightWeather::Kind::None;bool mistArmed=false;bool isMist(const void*)const{return false;}void proveMist(const void*){}};struct WeatherSampleMock{unsigned primitives=0,draws=0;}; /* 0.3.198 (rain): drawHook's one comparison; hot stays null here (test_weather_detect drives it) */
struct Guard{explicit Guard(Gate&){}};
namespace NorthlightRenderThreadProbe{inline bool sampleFrame(unsigned f){return f%3==0;}inline bool profiling(){return true;}}
namespace NorthlightWaterRenderer{enum UserPointer:unsigned{NoUserPointer,UserVertices,UserVerticesAndIndices};}
struct CpuScope{LONGLONG* e;explicit CpuScope(LONGLONG* o):e(o){}~CpuScope(){if(e)++*e;}};
template<class T> static void drop(T*& p){if(p){p->Release();p=nullptr;}}
struct Keys{struct{bool shadows=true;}settings;};
struct GateCounts{unsigned blobCalls=0,blobTextures=0,blobClaimed=0;};
struct Base{
    virtual ~Base()=default;
    virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE,UINT,UINT)=0;
    Trace trace;Env env;MockExt extObj;MockWorld worldObj;MockSky skyObj;MockBlobs blobsObj;
    MockExt* ext=&extObj;MockWorld* world=&worldObj;MockSky* celestialDiscs=nullptr;MockBlobs* shadowBlobs=nullptr;
    MirrorStateMock mirrorState;
    bool extensionFault=false,failed=false,enabled=true,applied=false,terrain=false,gateFrame=false,rainMaskCleared=false,rainMaskDrawn=false,rainMaskFrame=false,rainMaskOk=false,rainScrubDraw=false,rainMaskFailed=false,rainMaskMismatchLogged=false;UINT width=0,height=0;IDirect3DTexture9* rainMask=nullptr;IDirect3DSurface9* rainMaskSurface=nullptr;IDirect3DSurface9* rainMaskMS=nullptr;D3DMULTISAMPLE_TYPE rainMaskMsType=D3DMULTISAMPLE_NONE,rainMaskWantType=D3DMULTISAMPLE_NONE;DWORD rainMaskMsQuality=0,rainMaskWantQuality=0;unsigned rainMaskDraws=0,rainMaskScrubs=0;
    static constexpr int debugMode=0,kTagMask=3;int worldDebug=0;
    std::unordered_map<IDirect3DVertexShader9*,int> vsTags;
    std::unordered_map<IDirect3DVertexShader9*,std::uint64_t> vsHashes;std::unordered_map<IDirect3DPixelShader9*,std::uint64_t> psHashes;
    unsigned blobSignatureReports=0,terrainShadowDraws=0,frame=0,drawCalls=0;
    IDirect3DPixelShader9 *shadowSwapOriginal=nullptr,*shadowSwapReplacement=nullptr;
    Keys effectKeys;GateCounts gateCounts;WeatherDetectMock weatherDetect;WeatherSampleMock weatherSample;unsigned weatherMistSkips=0,weatherMistUnknown=0,weatherMistOtherStage=0,weatherMistReports=0,weatherStateReports=0;void weatherDrawStates(UINT){}void weatherProbeDraw(UINT){}
    Base(){blobsObj.ext=&extObj;env.trace=&trace;blobsObj.faint.id=6;blobsObj.faint.env=extObj.original.env=&env;extObj.original.id=5;extObj.env=worldObj.env=skyObj.env=blobsObj.env=&env;extObj.ps[0].env=extObj.ps[1].env=extObj.vs.env=worldObj.replacement.env=&env;}
    bool sampledDrawTimers()const{return frame%2==0;}
    void logf(const char* format,...){char b[512];va_list a;va_start(a,format);std::vsnprintf(b,sizeof b,format,a);va_end(a);trace.push_back(std::string("log ")+b);}
    bool fullViewport(D3DSURFACE_DESC& d){env.call("fullViewport","fault viewport");d.Width=1;return !env.bit("small viewport",4);}
    template<class Draw> void captureWater(IDirect3DVertexShader9*,unsigned up,Draw draw){env.call("captureWater "+std::to_string(up),"fault water");if(env.bit("water mask",3))trace.push_back("water mask hr="+std::to_string(draw()));}
};
// prepareDrawImpl stand-in: beforeDraw may find terrain, fail or apply the effects (UI), then the swap plan and the capture.
#define PREPARE_IMPL \
    template<class Capture> void prepareDrawImpl(Capture capture,UINT count){ \
        ++drawCalls;env.call("prepare","fault prepare"); \
        if(failed||!enabled)return; \
        if(applied)return; \
        IDirect3DVertexShader9* vs=&extObj.vs;setTag(vs,env.bit("terrain tag")?1:2); \
        if(env.bit("finds terrain",5))terrain=true; \
        if(env.bit("fails",60))failed=true; \
        if(env.bit("applies",30)){applied=true;trace.push_back("renderEffects");} \
        planTerrainShadowSwap(vs); \
        if(env.bit("world domain"))capture(vs); \
    }
struct OldDevice final:Base{
    void setTag(IDirect3DVertexShader9* vs,int t){vsTags[vs]=t;}
    PREPARE_IMPL
@OLD@
};
struct NewDevice final:Base{
    // A registration: the generation moves when the tag at an address changes (the real CreateVertexShader bumps it before every registration).
    void setTag(IDirect3DVertexShader9* vs,int t){auto it=vsTags.find(vs);if(it==vsTags.end()||it->second!=t)++vsGeneration;vsTags[vs]=t;}
    PREPARE_IMPL
@NEW@
};
struct Coverage{unsigned draws=0,claims=0,blobs=0,swaps=0,observes=0,faults=0,captures=0,skipped=0;};
// mutate: a latched input rises inside the frame (F9 / F10 / a setEffects outside the boundary) -
// the case the gates must not see; the run must then differ somewhere.
static bool run(std::uint64_t seed,bool gates,unsigned faultOneIn,bool mutate,Coverage& cover){
    OldDevice o;NewDevice n;Base* both[2]={&o,&n};
    for(Base* d:both){d->env.seed=seed;d->env.faultOneIn=faultOneIn;d->celestialDiscs=seed&1?&d->skyObj:nullptr;d->shadowBlobs=seed&2?&d->blobsObj:nullptr;d->worldObj.actorShadows=(seed&4)!=0;}
    n.frameDrawGates=gates;n.blobsObj.strengthValue=0;n.latchDrawGates();
    std::mt19937 r(unsigned(seed*2654435761u+gates));
    for(unsigned f=0;f<24;++f){
        const bool f10=r()%6==0,f9=r()%4==0,f12=r()%6==0,effects=r()%5==0,retry=r()%5==0;
        for(Base* d:both){ /* clearFrame, then finishFrameImpl's inputs */
            d->applied=d->terrain=false;d->frame=f;d->gateFrame=f%2;
            if(f10)d->enabled=!d->enabled;if(f9)d->effectKeys.settings.shadows=!d->effectKeys.settings.shadows;
            if(f12)d->worldDebug=(d->worldDebug+1)%4;if(effects)d->worldObj.shadows=!d->worldObj.shadows;if(retry)d->failed=false;}
        n.latchDrawGates();
        const unsigned draws=8+r()%24;
        for(unsigned i=0;i<draws;++i){
            const bool applies=r()%14==0,terrainFlip=r()%4==0,contextFlip=r()%6==0,compositeFlip=r()%5==0,fails=r()%50==0,rise=mutate&&r()%3==0;
            const D3DPRIMITIVETYPE t=D3DPRIMITIVETYPE(1+r()%6);const UINT start=r()%100,count=r()%3?r()%6:r()%400;
            for(Base* d:both){
                if(applies)d->applied=true;if(terrainFlip)d->terrain=!d->terrain;if(contextFlip)d->worldObj.context=!d->worldObj.context;
                if(compositeFlip)d->worldObj.composited=!d->worldObj.composited;if(fails)d->failed=true;
                if(rise){d->effectKeys.settings.shadows=true;d->worldObj.shadows=true;if(!d->extensionFault)d->enabled=true;}
                d->env.frame=f;d->env.draw=i;d->env.ord.clear();}
            const std::size_t before=n.trace.size();
            const HRESULT a=o.DrawPrimitive(t,start,count),b=n.DrawPrimitive(t,start,count);
            if(a!=b||o.trace!=n.trace){
                if(mutate)return false;
                std::fprintf(stderr,"MISMATCH seed=%llu gates=%d faultOneIn=%u frame=%u draw=%u hr=%ld/%ld\n",(unsigned long long)seed,int(gates),faultOneIn,f,i,a,b);
                const std::size_t from=before>6?before-6:0;
                for(std::size_t k=from;k<std::max(o.trace.size(),n.trace.size());++k)
                    std::fprintf(stderr,"  %-48s | %s\n",k<o.trace.size()?o.trace[k].c_str():"",k<n.trace.size()?n.trace[k].c_str():"");
                std::abort();}
            ++cover.draws;
            for(std::size_t k=before;k<n.trace.size();++k){const std::string& l=n.trace[k];
                cover.claims+=l=="claimNativeGlare";cover.blobs+=l.rfind("blob claim",0)==0;cover.swaps+=l.rfind("SetPixelShader ps9",0)==0;
                cover.observes+=l.rfind("observeNativeDraw",0)==0;cover.faults+=l.rfind("log EXTENSION fault",0)==0;cover.captures+=l.rfind("capture ",0)==0;}
            cover.skipped+=gates&&n.trace.size()-before<=3;
        }
    }
    const bool same=o.trace==n.trace&&o.extensionFault==n.extensionFault&&o.failed==n.failed&&o.enabled==n.enabled&&o.terrainShadowDraws==n.terrainShadowDraws
        &&o.blobSignatureReports==n.blobSignatureReports&&o.drawCalls==n.drawCalls&&o.skyObj.claimGate==n.skyObj.claimGate&&o.gateCounts.blobCalls==n.gateCounts.blobCalls
        &&o.gateCounts.blobTextures==n.gateCounts.blobTextures&&o.gateCounts.blobClaimed==n.gateCounts.blobClaimed&&!n.shadowSwapOriginal&&!o.shadowSwapOriginal&&!n.blobOriginal;
    if(!same&&!mutate){std::fprintf(stderr,"END STATE MISMATCH seed=%llu gates=%d\n",(unsigned long long)seed,int(gates));std::abort();}
    return same;
}
// 0.3.193 faint blobs (strength 50): per draw, texture 0 is the faint disc only around the real draw, the original comes back right
// after it (also when the extension work faults before or inside the claim: then nothing was planned), every GetTexture is released,
// and a swap composes with the terrain pixel shader swap. Only the current code runs here: the 0.3.184 code never swapped textures.
struct FaintCoverage{unsigned draws=0,swaps=0,composed=0,faults=0,noPlan=0;};
static void runFaint(std::uint64_t seed,bool gates,unsigned faultOneIn,FaintCoverage& cover){
    NewDevice n;n.env.seed=seed;n.env.faultOneIn=faultOneIn;n.shadowBlobs=&n.blobsObj;n.blobsObj.faintMode=true;n.worldObj.actorShadows=true;
    n.frameDrawGates=gates;n.blobsObj.strengthValue=50;n.latchDrawGates();
    std::mt19937 r(unsigned(seed*2654435761u+gates+17));
    for(unsigned f=0;f<24;++f){
        n.applied=n.terrain=false;n.frame=f;n.gateFrame=f%2;if(r()%6==0)n.failed=false;
        n.latchDrawGates();
        const unsigned draws=8+r()%24;
        for(unsigned i=0;i<draws;++i){
            if(r()%20==0)n.applied=true;if(r()%4==0)n.terrain=!n.terrain;if(r()%6==0)n.worldObj.context=!n.worldObj.context;if(r()%5==0)n.worldObj.composited=!n.worldObj.composited;
            n.env.frame=f;n.env.draw=i;n.env.ord.clear();
            const std::size_t before=n.trace.size();
            n.DrawPrimitive(D3DPT_TRIANGLELIST,r()%100,r()%3?1+r()%5:1+r()%400);
            const Trace t(n.trace.begin()+before,n.trace.end());
            unsigned gets=0,thrownGets=0,faintSets=0,origSets=0,releases=0,draws1=0;
            for(std::size_t k=0;k<t.size();++k){const std::string& l=t[k];
                gets+=l=="GetTexture 0";thrownGets+=l=="throw fault gettexture"||l=="GetTexture null";releases+=l=="Release tex5";draws1+=l.rfind("DrawPrimitive",0)==0;
                if(l=="SetTexture 0 faint"){++faintSets;
                    assert(k+2<t.size()&&t[k+1].rfind("DrawPrimitive",0)==0&&t[k+2]=="SetTexture 0 orig");   /* the draw is bracketed */
                    if(k&&t[k-1]=="SetPixelShader ps9")++cover.composed;}
                origSets+=l=="SetTexture 0 orig";
                if(l.rfind("log EXTENSION fault",0)==0)++cover.faults;}
            assert(faintSets==origSets&&faintSets<=1&&(faintSets==0||draws1>=1));
            assert(gets-thrownGets==releases);          /* every texture taken is released */
            assert(faintSets<=gets-thrownGets);         /* a swap needs a plan, which holds the original */
            if(thrownGets)++cover.noPlan;   /* GetTexture failed or threw: nothing planned, the draw ran unchanged */
            assert(!n.blobOriginal&&!n.shadowSwapOriginal);
            cover.swaps+=faintSets;++cover.draws;
        }
    }
}
int main(){
    {FaintCoverage fc[2];
     for(std::uint64_t seed=0;seed<3000;++seed)for(int gates=0;gates<2;++gates)runFaint(seed,gates,seed%4==3?0u:unsigned(40+seed%200),fc[gates]);
     for(int g=0;g<2;++g){std::fprintf(stderr,"faint gates=%d draws=%u swaps=%u composedWithTerrainSwap=%u faults=%u getTextureFailed=%u\n",g,fc[g].draws,fc[g].swaps,fc[g].composed,fc[g].faults,fc[g].noPlan);std::fflush(stdout);
        assert(fc[g].swaps>200&&fc[g].composed>5&&fc[g].faults>100&&fc[g].noPlan>5);}}
    Coverage cover[2];unsigned caught=0;
    for(std::uint64_t seed=0;seed<4000;++seed)for(int gates=0;gates<2;++gates)run(seed,gates,seed%4==3?0u:unsigned(40+seed%200),false,cover[gates]);
    for(std::uint64_t seed=0;seed<400;++seed)caught+=!run(seed|7,true,0,true,cover[1]);
    Coverage unused;for(std::uint64_t seed=0;seed<400;++seed)assert(run(seed|7,false,0,true,unused)); /* gates off: no latch to break */
    for(int g=0;g<2;++g){const Coverage& c=cover[g];
        std::printf("gates=%d draws=%u nativeClaims=%u blobClaims=%u terrainSwaps=%u observations=%u faults=%u captures=%u hookFreeDraws=%u\n",g,c.draws,c.claims,c.blobs,c.swaps,c.observes,c.faults,c.captures,c.skipped);
        assert(c.claims&&c.blobs&&c.swaps&&c.observes&&c.faults>100&&c.captures);}
    assert(cover[1].skipped>cover[1].draws/10);
    std::printf("mutation (a latched input rising inside a frame) detected in %u/400 runs\n",caught);
    assert(caught>40);
    std::printf("PASS draw gates: 0.3.184 and 0.3.187 hooks issue the same calls, results and faults\n");
}
'''

src=HARNESS.replace('@OLD@',OLD_MEMBERS+'\n'+old_draws[0]).replace('@NEW@',new_members)
with tempfile.TemporaryDirectory(prefix='northlight-draw-gates-') as tmp:
    (Path(tmp)/'draw_gates_test.cpp').write_text(src)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-private-field',*flags,*fp.test_include_flags(),str(Path(tmp)/'draw_gates_test.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
print('PASS FrameDrawGates: same hook work as 0.3.184, latched inputs rise only at the latch points')
