# northlight-test: requires=windows-gpu,zig
"""Exercise production UI/HDR boundaries with a rare FPS font before the main UI.

The recording device models colour remaining over land and water separately;
it does not pretend to rasterize the removed water shaders. The old renderer/header is
the negative control, supplied by the immutable source next to this checkout.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as paths


def method(source, signature):
    start = source.index(signature)
    at = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[start:at]


def transactions(source):
    methods = '\n'.join(method(source, signature) for signature in (
        'const char* hdrBoundarySource()',
        'template<class Draw> HRESULT hdrDraw(Draw draw)',
        'void uiFallbackDraw(const VsClass& vc,IDirect3DVertexShader9* vs)',
    ))
    stock = method(source, '} else if (tag==2) {')
    return methods + '\nvoid stockUiDraw(){\n' + stock[stock.index('{') + 1:-1] + '\n}'


PREFIX = r'''
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "ui_boundary.h"
#include "old_ui_boundary.h"
using HRESULT=int;using DWORD=unsigned;
constexpr HRESULT S_OK=0,E_FAIL=-1;
constexpr DWORD D3DCMP_ALWAYS=8,D3DCMP_LESS=2;
constexpr unsigned D3DRS_ZWRITEENABLE=14,D3DRS_ZFUNC=23,D3DRS_ZENABLE=7,D3DRS_ALPHABLENDENABLE=27;
#define SUCCEEDED(hr) ((hr)>=0)
#define FAILED(hr) ((hr)<0)
struct D3DSURFACE_DESC{};
struct IDirect3DVertexShader9{unsigned id;};
struct IDirect3DPixelShader9{unsigned id;};
static IDirect3DVertexShader9 uiVS{1},otherVS{2},worldVS{3};
static IDirect3DPixelShader9 commonPS{1},fpsPS{2},replacementPS{3};
struct VsClass {int entry=0;bool world=false,wmo=false,skinned=false;};
struct FakeDevice {
 IDirect3DVertexShader9* vs=&uiVS;IDirect3DPixelShader9* ps=&fpsPS;
 float rows[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
 DWORD zwrite=0,zfunc=D3DCMP_ALWAYS;bool borrowed=true;
 HRESULT shaderResult=S_OK,rowsResult=S_OK,stateResult=S_OK;unsigned stateReads=0,rowReads=0,pixelReads=0;
 bool peekVertexShader(IDirect3DVertexShader9*& out){out=vs;return borrowed;}
 bool peekPixelShader(IDirect3DPixelShader9*& out){out=ps;return borrowed;}
 HRESULT GetVertexShader(IDirect3DVertexShader9** out){*out=vs;return shaderResult;}
 HRESULT GetPixelShader(IDirect3DPixelShader9** out){++pixelReads;*out=ps;return shaderResult;}
 HRESULT GetVertexShaderConstantF(unsigned start,float* out,unsigned count){++rowReads;for(unsigned i=0;i<count*4;++i)out[i]=rows[start*4+i];return rowsResult;}
 HRESULT GetRenderState(unsigned state,DWORD* out){++stateReads;*out=state==D3DRS_ZWRITEENABLE?zwrite:state==D3DRS_ZFUNC?zfunc:1;return stateResult;}
};
template<class T>void drop(T*& p){p=nullptr;}
template<class...T>void logf(const char*,T...){ }
template<class Arming>struct Common {
 static constexpr int kTagMask=3,kWaterTag=4;
 Arming uiFallback;
 FakeDevice device,*ext=&device;VsClass classification;
 struct {bool active=false;struct{float uiWhite=160;}settings;}hdr;
 struct {unsigned rejected=0;template<class Draw>HRESULT draw(FakeDevice*,float,Draw submit){return submit();}}hdrColor;
 std::unordered_map<IDirect3DPixelShader9*,int>psTags;
 std::unordered_map<IDirect3DVertexShader9*,uint64_t>vsHashes;
 bool terrain=true,applied=false,enabled=true,failed=false,drawWaterVS=false;
 bool hashBoundaryThisFrame=false,fallbackBoundaryThisFrame=false,zFuncAware=true;
 bool hdrUiPhase=false,full=true,back=true,stock=false;
 unsigned frame=1,drawCalls=0,uiDraws=0,uiAfterTerrain=0,uiFallbackBoundaries=0,hdrFailuresLogged=0,hdrDeferredLogs=0,hdrDeferredFrame=~0u;
 unsigned triageDraws=0;IDirect3DVertexShader9* triageVs=nullptr;IDirect3DPixelShader9* triagePs=nullptr;DWORD triageZw=0;float triageC3[4]{};
 bool fpsOverScene=false,fpsOverDetail=false,effectsOverwroteFPS=false,effectsCopiedFPS=false;
 unsigned effects=0,submitted=0,conversions=0;
 std::vector<std::string> events{"World"};
 const VsClass& classifyVs(IDirect3DVertexShader9* shader){classification={};classification.world=shader==&worldVS;classification.entry=stock&&shader==&uiVS?2:0;return classification;}
 bool fullViewport(D3DSURFACE_DESC&,void*,bool,bool=true){return full;}
 bool hdrBackTarget(){return back;}
 bool sampled()const{return false;}
 void uiFallbackPublish(){}
 template<class Work>void extensionWork(const char*,Work work){work();}
 void renderEffects(){
  if(applied||!enabled||failed)return;
  ++effects;events.push_back("EffectsBegin");
  // A full-screen effect must complete before the first screen-space UI draw.
  effectsOverwroteFPS=fpsOverDetail;fpsOverDetail=false;
  events.push_back("WorldComposite");
  // A later effect may read the current framebuffer, including incorrectly early UI.
  effectsCopiedFPS=fpsOverScene;events.push_back("EffectsEnd");applied=true;
 }
 void spellReplay(){events.push_back("Spells");}
 HRESULT hdrBegin(bool,const char* ="present"){++conversions;hdrUiPhase=true;events.push_back("HDR");return S_OK;}
 HRESULT submit(bool fps){++submitted;events.push_back(fps?"FPS":"RestUI");if(fps)fpsOverScene=fpsOverDetail=true;return S_OK;}
 void learn(bool rare=true){
  for(unsigned f=0;f<Arming::kArmFrames;++f){
   for(unsigned i=0;i<70;++i)uiFallback.noteCandidate(reinterpret_cast<std::uintptr_t>(&uiVS),reinterpret_cast<std::uintptr_t>(&commonPS));
   if(rare)uiFallback.noteCandidate(reinterpret_cast<std::uintptr_t>(&uiVS),reinterpret_cast<std::uintptr_t>(&fpsPS));
   uiFallback.endFrame(Arming::FrameKind::WorldMissed);
  }
  assert(uiFallback.armed());
 }
};
'''

DRIVER = r'''
template<class Renderer>static void draw(Renderer& renderer,bool fps){
 ++renderer.drawCalls;
 IDirect3DVertexShader9* vs=nullptr;
 const bool borrowed=renderer.device.peekVertexShader(vs);
 if((borrowed||SUCCEEDED(renderer.device.GetVertexShader(&vs)))&&vs){
  const auto& vc=renderer.classifyVs(vs);
  if((vc.entry&Renderer::kTagMask)==2)renderer.stockUiDraw();
  renderer.uiFallbackDraw(vc,vs);
 }
 assert(renderer.hdrDraw([&]{return renderer.submit(fps);})==S_OK);
}
template<class Renderer>static void frame(Renderer& renderer){
 renderer.device.ps=&fpsPS;draw(renderer,true);
 renderer.device.ps=&commonPS;draw(renderer,false);
}
static unsigned at(const std::vector<std::string>& events,const char* name){
 for(unsigned n=0;n<events.size();++n)if(events[n]==name)return n;
 return unsigned(events.size());
}
static unsigned cases=0;
int main(){
 for(bool hdr:{false,true}){
  Old historical;historical.hdr.active=hdr;historical.learn();frame(historical);
  assert(historical.effects==1&&historical.submitted==2);
  assert(historical.effectsOverwroteFPS&&historical.effectsCopiedFPS);
  assert(at(historical.events,"FPS")<at(historical.events,"EffectsBegin"));
  assert(at(historical.events,"EffectsEnd")<at(historical.events,"RestUI"));++cases;
  Fixed fixed;fixed.hdr.active=hdr;fixed.learn();frame(fixed);
  assert(fixed.effects==1&&fixed.submitted==2);
  assert(!fixed.effectsOverwroteFPS&&!fixed.effectsCopiedFPS&&fixed.fpsOverDetail);
  assert(at(fixed.events,"EffectsBegin")<at(fixed.events,"WorldComposite"));
  assert(at(fixed.events,"WorldComposite")<at(fixed.events,"EffectsEnd"));
  assert(at(fixed.events,"EffectsEnd")<at(fixed.events,"FPS"));
  assert(at(fixed.events,"FPS")<at(fixed.events,"RestUI"));
  assert(fixed.conversions==unsigned(hdr));
  if(hdr)assert(at(fixed.events,"EffectsEnd")<at(fixed.events,"HDR")&&at(fixed.events,"HDR")<at(fixed.events,"FPS"));++cases;
  // A newly enabled FPS counter was never part of the learning sample.
  Fixed toggled;toggled.hdr.active=hdr;toggled.learn(false);frame(toggled);
  assert(toggled.effects==1&&!toggled.effectsCopiedFPS&&!toggled.effectsOverwroteFPS);++cases;
  // Recreating a frequent PS must not force the UI VS through another warmup.
  Fixed recreated;recreated.hdr.active=hdr;recreated.learn();
  recreated.uiFallback.forget(reinterpret_cast<std::uintptr_t>(&commonPS));
  assert(recreated.uiFallback.armed());recreated.device.ps=&replacementPS;draw(recreated,true);
  assert(recreated.effects==1&&!recreated.effectsCopiedFPS&&recreated.submitted==1);++cases;
  // Recreating the learned VS does invalidate its trust.
  Fixed forgotten;forgotten.hdr.active=hdr;forgotten.learn();
  forgotten.uiFallback.forget(reinterpret_cast<std::uintptr_t>(&uiVS));
  assert(!forgotten.uiFallback.armed());draw(forgotten,true);
  assert(!forgotten.effects&&!forgotten.conversions&&forgotten.submitted==1);++cases;
  for(unsigned reject=0;reject<6;++reject){
   Fixed guarded;guarded.hdr.active=hdr;guarded.learn();
   if(reject==0)guarded.device.vs=&otherVS; // Unknown VS, despite affine c0..3.
   else if(reject==1)guarded.full=false; // Minimap/offscreen viewport.
   else if(reject==2)guarded.device.rows[15]=0; // Perspective floating text.
   else if(reject==3)guarded.device.vs=&worldVS; // World affine matrix.
   else if(reject==4)guarded.device.zwrite=1;
   else guarded.device.zfunc=D3DCMP_LESS;
   draw(guarded,true);
   assert(!guarded.effects&&!guarded.conversions&&guarded.submitted==1);++cases;
  }
  // The stock UI VS also uses rare font PS variants; no fallback learning.
  Old stockOld;stockOld.stock=true;stockOld.hdr.active=hdr;stockOld.psTags[&commonPS]=2;
  stockOld.uiFallback.onHashBoundary();frame(stockOld);
  assert(stockOld.effects==1&&stockOld.effectsOverwroteFPS&&stockOld.effectsCopiedFPS);++cases;
  Fixed stock;stock.stock=true;stock.hdr.active=hdr;stock.uiFallback.onHashBoundary();frame(stock);
  assert(stock.effects==1&&stock.submitted==2&&!stock.effectsOverwroteFPS&&!stock.effectsCopiedFPS);
  assert(at(stock.events,"EffectsEnd")<at(stock.events,"FPS")&&stock.conversions==unsigned(hdr));++cases;
  Fixed stockPS0;stockPS0.stock=true;stockPS0.hdr.active=hdr;stockPS0.uiFallback.onHashBoundary();stockPS0.device.ps=nullptr;
  draw(stockPS0,true);assert(stockPS0.effects==1&&stockPS0.submitted==1&&stockPS0.conversions==unsigned(hdr));
  assert(at(stockPS0.events,"EffectsEnd")<at(stockPS0.events,"FPS"));++cases;
  Fixed learnedPS0;learnedPS0.hdr.active=hdr;learnedPS0.learn();learnedPS0.device.ps=nullptr;
  draw(learnedPS0,true);assert(learnedPS0.effects==1&&learnedPS0.submitted==1&&learnedPS0.conversions==unsigned(hdr));++cases;
  for(unsigned reject=0;reject<9;++reject){
   Fixed guarded;guarded.stock=true;guarded.hdr.active=hdr;guarded.uiFallback.onHashBoundary();
   if(reject==0)guarded.device.vs=&otherVS;
   else if(reject==1)guarded.full=false;
   else if(reject==2)guarded.device.rows[15]=0;
   else if(reject==3)guarded.device.vs=&worldVS;
   else if(reject==4)guarded.device.zwrite=1;
   else if(reject==5)guarded.device.zfunc=D3DCMP_LESS;
   else if(reject==6)guarded.device.rowsResult=E_FAIL;
   else if(reject==7)guarded.device.stateResult=E_FAIL;
   else{guarded.device.borrowed=false;guarded.device.shaderResult=E_FAIL;}
   draw(guarded,true);assert(!guarded.effects&&!guarded.conversions&&guarded.submitted==1);++cases;
  }
 }

 // HDR classification must remain valid when the master effects switch is off:
 // prepareDraw does not establish terrain in that state.
 for(bool master:{false,true})for(bool learned:{false,true}){
  Fixed world;world.hdr.active=true;world.enabled=master;world.terrain=false;
  if(learned)world.learn();
  world.device.vs=&worldVS;
  draw(world,false);
  assert(!world.conversions&&world.submitted==1);
  assert(!world.device.rowReads&&!world.device.stateReads&&!world.device.pixelReads);++cases;
 }
 for(bool master:{false,true}){
  Fixed unknown;unknown.hdr.active=true;unknown.enabled=master;unknown.terrain=false;unknown.learn();
  unknown.device.vs=&otherVS;draw(unknown,false);
  assert(!unknown.conversions&&unknown.submitted==1);
  unknown.device.vs=&uiVS;draw(unknown,true);
  assert(unknown.conversions==1&&unknown.submitted==2);
  assert(at(unknown.events,"RestUI")<at(unknown.events,"HDR")&&at(unknown.events,"HDR")<at(unknown.events,"FPS"));++cases;
 }
 Fixed stockOff;stockOff.stock=true;stockOff.hdr.active=true;stockOff.enabled=false;stockOff.terrain=false;stockOff.uiFallback.onHashBoundary();
 stockOff.device.vs=&otherVS;draw(stockOff,false);assert(!stockOff.conversions);
 stockOff.device.vs=&uiVS;draw(stockOff,true);assert(stockOff.conversions==1&&stockOff.submitted==2);++cases;
 Fixed login;login.hdr.active=true;login.terrain=false;login.device.vs=&otherVS;draw(login,true);
 assert(login.conversions==1&&login.submitted==1);++cases;
 // A proven shader can be restored on a recreated device, but still must pass
 // every projection, depth and target guard before affecting the frame.
 NorthlightUiBoundary::rememberedVsHash.store(0x1234);
 Fixed recovered;recovered.vsHashes[&uiVS]=0x1234;frame(recovered);
 assert(recovered.effects==1&&recovered.uiFallback.armed()&&!recovered.effectsOverwroteFPS&&!recovered.effectsCopiedFPS);++cases;
 Fixed disabled;disabled.vsHashes[&uiVS]=0x1234;disabled.enabled=false;draw(disabled,true);assert(!disabled.effects&&!disabled.uiFallback.armed());
 disabled.enabled=true;draw(disabled,true);assert(disabled.effects==1&&disabled.uiFallback.armed());++cases;
 for(unsigned reject=0;reject<7;++reject){
  Fixed guarded;guarded.vsHashes[&uiVS]=0x1234;
  if(reject==0)guarded.vsHashes[&uiVS]=0x1235;
  else if(reject==1)guarded.full=false;
  else if(reject==2)guarded.device.rows[15]=0;
  else if(reject==3)guarded.device.zwrite=1;
  else if(reject==4)guarded.device.zfunc=D3DCMP_LESS;
  else if(reject==5)guarded.device.stateResult=E_FAIL;
  else guarded.uiFallback.onHashBoundary();
  draw(guarded,true);assert(!guarded.effects&&!guarded.uiFallback.armed());++cases;
 }
 NorthlightUiBoundary::rememberedVsHash.store(0);
 std::printf("PASS UI/effects boundary host extraction cases=%u: old FPS boundary order reproduced; fixed EffectsBegin->WorldComposite->EffectsEnd->FPS->RestUI; stock/learned VS, rare/PS0, HDR/SDR and query guards preserved\n",cases);
}
'''


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--baseline', type=Path, default=ROOT / 'tests/fixtures/ui-boundary-before')
    args = parser.parse_args()
    baseline = args.baseline.resolve()
    old_header = (baseline / 'ui-before-boundary.h').read_text(encoding='utf-8')
    old_renderer = (baseline / 'ui-before-methods.txt').read_text(encoding='utf-8')
    renderer = (ROOT / 'src/proxy/renderer.cpp').read_text(encoding='utf-8')
    folder = paths.out() / 'ui-water-boundary'
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'old_ui_boundary.h').write_text(old_header.replace('namespace NorthlightUiBoundary {', 'namespace OldNorthlightUiBoundary {'), encoding='utf-8')
    cpp, exe = folder / 'ui_water_boundary.cpp', folder / 'ui_water_boundary.exe'
    cpp.write_text(PREFIX + '\nstruct Old:Common<OldNorthlightUiBoundary::Arming>{\n' + transactions(old_renderer) + '\n};\nstruct Fixed:Common<NorthlightUiBoundary::Arming>{\n' + transactions(renderer) + '\n};\n' + DRIVER, encoding='utf-8')
    subprocess.run([str(paths.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-I' + str(ROOT / 'src/proxy'), '-I' + str(folder), str(cpp), '-o', str(exe)], env=paths.zig_env(), check=True)
    result = subprocess.run([str(exe)], env=paths.zig_env(), text=True, capture_output=True)
    print(result.stdout, end='')
    report = {'baseline': str(baseline), 'baseline_ui_header_sha256': hashlib.sha256((baseline / 'ui-before-boundary.h').read_bytes()).hexdigest(), 'renderer_sha256': hashlib.sha256((ROOT / 'src/proxy/renderer.cpp').read_bytes()).hexdigest(), 'ui_header_sha256': hashlib.sha256((ROOT / 'src/proxy/ui_boundary.h').read_bytes()).hexdigest(), 'exit_code': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr, 'game_launched': False, 'gpu_rasterization_test': False}
    (folder / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if result.returncode:
        raise SystemExit(result.stderr or f'Host regression failed: {result.returncode}')


if __name__ == '__main__':
    main()
