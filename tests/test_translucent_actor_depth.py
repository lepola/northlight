#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.188 TranslucentActorDepth: the depth resolve moves ahead of the first translucent Z-writing actor draw.

Native part: translucent_depth.h (the trigger predicate with lazy state reads and the once-per-frame latch) is driven
through the same glue shape renderer.cpp uses (a terrain draw clears `captured`, a failed resolve leaves it false,
the UI-time resolve finishes the frame) over a fake draw sequence. Source part: the glue in prepareDrawImpl, the
RawScope, the reset in clearFrame, the setting and the docs. Native clang++, plain -O2 and ASan/UBSan. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile

HARNESS=r'''
#include "translucent_depth.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <string>
typedef unsigned long DWORD;
using namespace NorthlightTranslucentDepth;
struct Draw{const char* what;bool terrain=false,water=false,skinned=false;DWORD z=0,blend=0,color=15;};
struct Sim{
    unsigned mode;bool resolveOk=true;bool terrain=false,captured=false,applied=false;Frame frame;unsigned reads=0,skinChecks=0;
    std::vector<std::string> log;unsigned attempts=0;
    void draw(const Draw& d,unsigned index){
        if(d.terrain){terrain=true;captured=false;}   // beforeDraw: every terrain draw
        if(mode&&terrain&&!captured&&!frame.tried&&shouldResolve(mode,d.water,[&]{++reads;return d.z;},[&]{++reads;return d.blend;},[&]{++reads;return d.color;},[&]{++skinChecks;return d.skinned;})&&frame.attempt()){
            ++attempts;log.push_back("resolve before "+std::string(d.what)+"@"+std::to_string(index));
            if(resolveOk){captured=true;++frame.resolves;}}
        log.push_back(std::string("draw ")+d.what);
    }
    void ui(){if(!captured&&terrain){captured=true;log.push_back("ui resolve");}}
    void end(){terrain=captured=false;frame.reset();}
};
static const Draw TERRAIN{"terrain",true,false,false,1,0,15},OPAQUE_SKIN{"opaqueSkin",false,false,true,1,0,15},
    GHOST{"ghost",false,false,true,1,1,15},PROP_BLEND{"propBlend",false,false,false,1,1,15},NOZ_BLEND{"nozBlend",false,false,true,0,1,15},
    PREPASS{"prepass",false,false,true,1,0,0},WATER{"water",false,true,false,1,1,15},PROP_PREPASS{"propPrepass",false,false,false,1,0,0},
    OPAQUE_PROP{"opaqueProp",false,false,false,1,0,15};
static std::vector<std::string> run(unsigned mode,std::vector<Draw> draws,bool ok=true,unsigned* attempts=nullptr,unsigned* resolves=nullptr,Sim* out=nullptr){
    Sim s;s.mode=mode;s.resolveOk=ok;unsigned i=0;for(const auto& d:draws)s.draw(d,++i);s.ui();
    if(attempts)*attempts=s.attempts;if(resolves)*resolves=s.frame.resolves;if(out)*out=s;return s.log;
}
static bool has(const std::vector<std::string>& l,const std::string& x){for(const auto& e:l)if(e==x)return true;return false;}
int main(){
    // pure predicate
    assert(!triggers(0,true,false,1,1,15));
    assert(triggers(1,true,false,1,1,15)&&triggers(1,true,false,1,0,0)&&!triggers(1,true,false,1,0,15));
    assert(!triggers(1,false,false,1,1,15)&&triggers(2,false,false,1,1,15)&&triggers(2,false,false,1,0,0));
    assert(!triggers(2,true,true,1,1,15)&&!triggers(1,true,true,1,0,0));
    assert(!triggers(1,true,false,0,1,15)&&!triggers(2,true,false,0,1,0)&&!triggers(2,false,false,0,0,0));
    assert(triggers(1,true,false,1,0,8)&&!triggers(1,true,false,1,0,7)&&!triggers(1,true,false,1,0,1)); /* alpha-only write is no RGB write */
    assert(triggers(1,true,false,1,0,0));
    { // order: resolve happens before the first qualifying draw, only once, later translucent draws do not repeat it
        unsigned a,r;auto l=run(1,{TERRAIN,OPAQUE_SKIN,PROP_BLEND,GHOST,GHOST,PREPASS},true,&a,&r);
        assert(a==1&&r==1);
        std::vector<std::string> want={"draw terrain","draw opaqueSkin","draw propBlend","resolve before ghost@4","draw ghost","draw ghost","draw prepass"};
        assert(l==std::vector<std::string>(want.begin(),want.end()));
    }
    { unsigned a,r;auto l=run(0,{TERRAIN,GHOST,PREPASS},true,&a,&r);assert(a==0&&r==0&&!has(l,"resolve before ghost@2")&&has(l,"ui resolve")); } // mode 0: old behaviour
    { Sim s;run(0,{TERRAIN,GHOST},true,nullptr,nullptr,&s);assert(s.reads==0&&s.skinChecks==0); }   // mode 0 reads nothing
    { unsigned a;auto l=run(1,{TERRAIN,PROP_BLEND,NOZ_BLEND,PROP_PREPASS,WATER,OPAQUE_PROP},true,&a);assert(a==0&&has(l,"ui resolve")); } // mode 1 ignores non-skinned
    { auto l=run(2,{TERRAIN,OPAQUE_PROP,PROP_BLEND,GHOST},true);assert(has(l,"resolve before propBlend@3")&&!has(l,"resolve before ghost@4")); } // mode 2 any world draw
    { unsigned a;run(2,{TERRAIN,WATER,NOZ_BLEND,OPAQUE_PROP},true,&a);assert(a==0); }   // water and non-Z-writing blended draws never trigger
    { auto l=run(2,{TERRAIN,WATER,PROP_BLEND},true);assert(has(l,"resolve before propBlend@3")); } // water does not use up the frame's attempt
    { unsigned a,r;auto l=run(1,{TERRAIN,GHOST,GHOST,PREPASS},false,&a,&r);assert(a==1&&r==0&&has(l,"ui resolve")); } // failed: one attempt, UI-time resolve
    { Sim s;s.mode=1;unsigned i=0;for(const auto& d:{TERRAIN,GHOST}){s.draw(d,++i);}assert(s.captured&&s.frame.resolves==1);
      s.draw(TERRAIN,3);assert(!s.captured);          // terrain draw after the early resolve clears captured ...
      s.draw(GHOST,4);assert(!s.captured&&s.attempts==1); // ... and the early resolve is not re-triggered in this frame
      s.ui();assert(s.captured);
      s.end();assert(!s.frame.tried&&s.frame.resolves==0);s.draw(TERRAIN,1);s.draw(GHOST,2);assert(s.attempts==2&&s.captured); } // next frame arms again
    { Sim s;s.mode=1;s.draw(GHOST,1);assert(s.attempts==0&&!s.frame.tried); } // before any terrain draw: no attempt, flag untouched
    { Sim s;s.mode=1;s.draw(TERRAIN,1);s.reads=s.skinChecks=0;s.draw(OPAQUE_PROP,2);assert(s.reads==0&&s.skinChecks==1); } // mode 1: a non-skinned draw costs one skinned check, no state read
    { Sim s;s.mode=1;s.draw(TERRAIN,1);s.reads=s.skinChecks=0;s.draw(OPAQUE_SKIN,2);assert(s.reads==3&&s.skinChecks==1); } // opaque skinned: Z write, blend, colour write
    { Sim s;s.mode=2;s.draw(TERRAIN,1);s.reads=s.skinChecks=0;s.draw(OPAQUE_PROP,2);assert(s.reads==3&&s.skinChecks==0); } // mode 2 never asks for skinned
    { Sim s;s.mode=1;s.draw(TERRAIN,1);s.reads=s.skinChecks=0;s.draw(NOZ_BLEND,2);assert(s.reads==1&&s.skinChecks==1); } // no Z write: one state read
    std::printf("PASS translucent actor depth predicate and latch\n");
}
'''

with tempfile.TemporaryDirectory(prefix='northlight-translucent-depth-') as tmp:
    (Path(tmp)/'t.cpp').write_text(HARNESS)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(Path(tmp)/'t.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)

# source checks
r=fp.tracked('renderer.cpp').read_text();q=fp.src('quality_settings.h').read_text();w=fp.src('world_renderer.h').read_text()
impl=r[r.index('template<class Capture> void prepareDrawImpl('):r.index('    // Terrain draws run with the game')]
checks={}
dom=impl.index('if(worldDomain){');early=impl.index('(2A)');cen=impl.index('/* 0.3.188 (task 3)');cap=impl.index('capture(vs);')
checks['early resolve inside the world domain, before the census and capture(vs)']=dom<early<cen<cap
blk=impl[dom:cen]
checks['guarded by mode, terrain, !captured and the once-per-frame flag']='translucentActorDepth&&terrain&&!captured&&!earlyDepth.tried&&NorthlightTranslucentDepth::shouldResolve(translucentActorDepth,drawWaterVS,' in blk
checks['attempt latched before the resolve, resolve inside RawScope']=blk.index('earlyDepth.attempt()')<blk.index('ExtensionDevice::RawScope raw(*ext);')<blk.index('resolveDepth()')
checks['state reads: Z write, blend, colour write, skinned']=all(x in blk for x in ('D3DRS_ZWRITEENABLE','D3DRS_ALPHABLENDENABLE','D3DRS_COLORWRITEENABLE','isSkinnedShader(vs)')) and 'SetRenderState' not in blk
checks['census block has no early resolve']='resolveDepth' not in impl[cen:cap]
hook=r[r.index('template<class Capture,class Draw> HRESULT drawHook('):][:3500]
checks['early block runs before the real draw (prepareDraw/prepareDrawImpl precede draw in both drawHook modes)']=(
    hook.index('prepareDraw(capture);')<hook.index('terrainShadowDraw(claimed,draw)')<hook.index('prepareDrawImpl(capture);')<hook.rindex('terrainShadowDraw(claimed,draw)'))
checks['per-frame flag reset in clearFrame']='drop(worldDepth); terrain = captured = applied = projectionValid = false;earlyDepth.reset();' in r
checks['logged on TRANSLUCENT with position, totals and undone resolves']='earlyResolve=%u earlyResolveAt=%u earlyResolveTotal=%u earlyResolveUndone=%u earlyResolveUndoneTotal=%u' in r and 'earlyDepth.resolves,earlyResolveAt,earlyResolveTotal,earlyResolveUndone,earlyResolveUndoneTotal' in r and 'earlyResolveAt=earlyResolveUndone=0' in r
checks['a terrain draw after the early resolve is counted as undone']='if(captured&&earlyDepth.resolves){++earlyResolveUndone;++earlyResolveUndoneTotal;}' in r and r.index('++earlyResolveUndoneTotal;}')<r.index('terrain=true; captured=false;')
checks['mode read once at device creation']='translucentActorDepth=world->translucentActorDepth();' in r and 'unsigned translucentActorDepth()const{return quality.translucentActorDepth;}' in w
checks['setting: last key, 0..2, presets 1/1/1']=q[q.index('inline const Key Keys[]={'):].split('};')[0].rstrip().endswith('{"TranslucentActorDepth",&Settings::translucentActorDepth,0,2,{1,1,1}},') and 'unsigned translucentActorDepth=1;' in q and 'char origin[33]=' in q
checks['resolveDepth unchanged: SavedState and captured=true on success']=(lambda f:'SavedState saved(ext,&stateBlocks);' in f and f.rstrip().endswith('captured = true; return true;')or 'captured = true; return true;' in f)(r[r.index('bool resolveDepth() {'):r.index('HRESULT quad(UINT w')])
checks['terrain draw still clears captured']='terrain=true; captured=false;' in r
checks['banner']='translucent depth census; early depth for translucent actors; backend=' in r
ini=fp.src('windows-package/northlight-quality.ini').read_text();rd=fp.src('windows-package/README.txt').read_text()
checks['docs: ini and README']=';TranslucentActorDepth=1' in ini and 'TranslucentActorDepth 1 / 1 / 1' in rd
for n,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+n)
assert all(checks.values())
print('PASS translucent actor depth wiring')
