#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.188 (task 3), always on: the depth resolve moves ahead of the first translucent Z-writing actor draw.

Native part: translucent_depth.h (the trigger predicate with lazy state reads and the once-per-frame latch) is driven
through the same glue shape renderer.cpp uses (a terrain draw clears `captured`, a failed resolve leaves it false,
the UI-time resolve finishes the frame) over a fake draw sequence. Source part: the glue in prepareDrawImpl, the
RawScope, the reset in clearFrame, and that no setting remains. Native clang++, plain -O2 and ASan/UBSan. No device or game."""
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
// blend 0 = off, 1 = on with SRCALPHA/INVSRCALPHA, 2 = on with ONE/ZERO
struct Draw{const char* what;bool terrain=false,water=false,skinned=false;DWORD z=0,blend=0,color=15;};
static bool triggers(const Draw& d,unsigned* reads=nullptr){
    auto rd=[&](DWORD v){if(reads)++*reads;return v;};
    return shouldResolve(d.water,[&]{return rd(d.z);},[&]{return rd(d.blend!=0);},[&]{return rd(d.blend==2?kBlendOne:5);},[&]{return rd(d.blend==2?kBlendZero:6);},[&]{return rd(d.color);},[&]{return d.skinned;});
}
static bool undoes(const Draw& d,unsigned* reads=nullptr){
    auto rd=[&](DWORD v){if(reads)++*reads;return v;};
    return isUndo(d.water,[&]{return rd(d.z);},[&]{return rd(d.blend!=0);},[&]{return rd(d.blend==2?kBlendOne:5);},[&]{return rd(d.blend==2?kBlendZero:6);},[&]{return rd(d.color);});
}
// The glue of renderer.cpp: beforeDraw (terrain undo), prepareDrawImpl (runtime undo, trigger), UI-time resolve.
struct Sim{
    bool resolveOk=true,terrain=false,captured=false;Frame frame;unsigned reads=0,skinChecks=0,attempts=0,undone=0;
    std::vector<std::string> log;
    void draw(const Draw& d,unsigned index){
        if(d.terrain){if(captured&&frame.earlyCaptured){frame.undo();++undone;}terrain=true;captured=false;}
        if(captured&&frame.earlyCaptured&&undoes(d,&reads)){captured=false;frame.undo();++undone;log.push_back("undo at "+std::string(d.what));}
        if(terrain&&!captured&&frame.armed()&&shouldResolve(d.water,[&]{++reads;return d.z;},[&]{++reads;return d.blend!=0;},[&]{++reads;return d.blend==2?kBlendOne:5;},[&]{++reads;return d.blend==2?kBlendZero:6;},[&]{++reads;return d.color;},[&]{++skinChecks;return d.skinned;})&&frame.attempt()){
            ++attempts;log.push_back("resolve before "+std::string(d.what)+"@"+std::to_string(index));
            if(resolveOk){captured=true;frame.success();}}
        log.push_back(std::string("draw ")+d.what);
    }
    void clearZ(){if(!terrain)return;if(!captured)captured=true;else if(frame.earlyCaptured)frame.freeze();} // renderer.cpp Clear: resolve, or freeze the early capture
    void ui(){if(!captured&&terrain){captured=true;log.push_back("ui resolve");}}
    void end(){terrain=captured=false;frame.reset();}
};
static const Draw TERRAIN{"terrain",true,false,false,1,0,15},OPAQUE_SKIN{"opaqueSkin",false,false,true,1,0,15},
    GHOST{"ghost",false,false,true,1,1,15},PROP_BLEND{"propBlend",false,false,false,1,1,15},NOZ_BLEND{"nozBlend",false,false,true,0,1,15},
    PREPASS{"prepass",false,false,true,1,0,0},WATER{"water",false,true,false,1,1,15},PROP_PREPASS{"propPrepass",false,false,false,1,0,0},
    OPAQUE_PROP{"opaqueProp",false,false,false,1,0,15},ONEZERO_SKIN{"oneZeroSkin",false,false,true,1,2,15},ONEZERO_PROP{"oneZeroProp",false,false,false,1,2,15},
    WATER_NOZ{"waterNoZ",false,true,false,0,1,15},PROP_NOZ{"propNoZ",false,false,false,0,0,15};
static std::vector<std::string> run(std::vector<Draw> draws,bool ok=true,unsigned* attempts=nullptr,Sim* out=nullptr){
    Sim s;s.resolveOk=ok;unsigned i=0;for(const auto& d:draws)s.draw(d,++i);s.ui();
    if(attempts)*attempts=s.attempts;if(out)*out=s;return s.log;
}
static bool has(const std::vector<std::string>& l,const std::string& x){for(const auto& e:l)if(e==x)return true;return false;}
int main(){
    // trigger predicate
    assert(triggers(GHOST)&&triggers(PREPASS)&&!triggers(OPAQUE_SKIN)); /* blended or depth-only; opaque not */
    assert(!triggers(PROP_BLEND)&&!triggers(PROP_PREPASS));             /* non-skinned never */
    { Draw w=GHOST;w.water=true;assert(!triggers(w)); }                 /* water never */
    assert(!triggers(NOZ_BLEND));                                       /* no Z write never */
    assert(!triggers(ONEZERO_SKIN));                                    /* ONE/ZERO blend is opaque */
    { Draw d=ONEZERO_SKIN;d.color=0;assert(triggers(d)); }              /* ... but still depth-only without RGB */
    { Draw d=OPAQUE_SKIN;d.color=8;assert(triggers(d));d.color=7;assert(!triggers(d));d.color=1;assert(!triggers(d)); } /* alpha-only write is no RGB write */
    // undo predicate
    assert(undoes(OPAQUE_SKIN)&&undoes(OPAQUE_PROP)&&undoes(ONEZERO_PROP)&&undoes(ONEZERO_SKIN)); /* Z + RGB + effectively opaque */
    assert(!undoes(GHOST)&&!undoes(PROP_BLEND));       /* blended */
    assert(!undoes(PREPASS)&&!undoes(PROP_PREPASS));   /* no RGB */
    assert(!undoes(PROP_NOZ)&&!undoes(WATER_NOZ));     /* no Z write */
    assert(undoes(WATER)&&undoes(WATER_NOZ)==false);   /* water with Z write, even blended */
    { Draw w=PREPASS;w.water=true;assert(undoes(w)); } /* water needs no RGB */
    { unsigned n=0;undoes(PROP_NOZ,&n);assert(n==1); } /* no Z write: one read */
    { unsigned n=0;undoes(OPAQUE_PROP,&n);assert(n==3); } /* Z, colour, blend (off: no src/dst reads) */
    { unsigned n=0;undoes(ONEZERO_PROP,&n);assert(n==5); } /* blend on: src and dst read too */
    { unsigned n=0;undoes(WATER,&n);assert(n==1); }       /* water: Z only */
    // order: resolve happens before the first qualifying draw, only once, later translucent draws do not repeat it
    {   unsigned a;Sim s;auto l=run({TERRAIN,OPAQUE_SKIN,PROP_BLEND,GHOST,GHOST,PREPASS},true,&a,&s);
        assert(a==1&&s.undone==0);
        std::vector<std::string> want={"draw terrain","draw opaqueSkin","draw propBlend","resolve before ghost@4","draw ghost","draw ghost","draw prepass"};
        assert(l==want);
    }
    { unsigned a;auto l=run({TERRAIN,PROP_BLEND,NOZ_BLEND,PROP_PREPASS,WATER_NOZ,OPAQUE_PROP,ONEZERO_SKIN},true,&a);assert(a==0&&has(l,"ui resolve")); }
    { auto l=run({TERRAIN,WATER,PROP_BLEND,GHOST},true);assert(has(l,"resolve before ghost@4")); } // they do not use up the frame's attempt
    { unsigned a;Sim s;auto l=run({TERRAIN,GHOST,GHOST,PREPASS},false,&a,&s);assert(a==1&&!s.frame.earlyCaptured&&has(l,"ui resolve")); } // failed: one attempt, UI-time resolve
    { Sim s;s.draw(TERRAIN,1);s.draw(GHOST,2);assert(s.captured&&s.frame.earlyCaptured);
      s.draw(TERRAIN,3);assert(!s.captured&&s.undone==1&&!s.frame.earlyCaptured);   // terrain undo ...
      s.draw(GHOST,4);assert(s.captured&&s.attempts==2);                           // ... re-arms: the next translucent draw resolves again
      s.ui();assert(s.captured);
      s.end();assert(!s.frame.tried&&s.frame.attempts==0&&!s.frame.earlyCaptured);s.draw(TERRAIN,1);s.draw(GHOST,2);assert(s.attempts==3&&s.captured); } // next frame: attempts reset
    { Sim s;s.draw(TERRAIN,1);s.draw(GHOST,2);s.draw(OPAQUE_PROP,3);assert(!s.captured&&s.undone==1); // runtime undo by a later opaque draw
      s.draw(GHOST,4);assert(s.captured&&s.attempts==2); }
    { Sim s;s.draw(TERRAIN,1);s.draw(GHOST,2);s.draw(WATER,3);assert(!s.captured&&s.undone==1); } // water Z draw undoes
    { Sim s;s.draw(TERRAIN,1);s.draw(GHOST,2);s.draw(PREPASS,3);s.draw(PROP_BLEND,4);s.draw(WATER_NOZ,5);assert(s.captured&&s.undone==0); } // translucent draws keep it
    { Sim s;s.draw(TERRAIN,1);s.draw(OPAQUE_PROP,2);assert(s.undone==0); } // no early resolve: nothing to undo
    { Sim s;for(unsigned i=0;i<12;++i){s.draw(i==0?TERRAIN:GHOST,2*i+1);s.draw(OPAQUE_PROP,2*i+2);}  // interleaved: capped
      assert(s.attempts==Frame::kMaxAttempts&&s.undone==Frame::kMaxAttempts);assert(!s.captured);s.ui();assert(s.captured);
      s.end();s.draw(TERRAIN,1);s.draw(GHOST,2);assert(s.attempts==Frame::kMaxAttempts+1&&s.captured); } // cap respected, next frame resets
    { Sim s;s.draw(TERRAIN,1);s.draw(GHOST,2);s.draw(TERRAIN,3);assert(s.undone==1&&!s.captured);   // early resolve -> terrain undo
      s.clearZ();assert(s.captured&&!s.frame.earlyCaptured);s.draw(TERRAIN,4);assert(s.undone==1); } // Clear(Z) resolve, terrain draw: still one
    { Sim s;s.draw(TERRAIN,1);s.draw(GHOST,2);assert(s.captured&&s.frame.earlyCaptured);   // early resolve -> Clear(Z) freezes it
      s.clearZ();assert(s.captured&&!s.frame.earlyCaptured&&!s.frame.armed());
      s.draw(OPAQUE_PROP,3);s.draw(WATER,4);assert(s.captured&&s.undone==0);           // no undo after the freeze ...
      s.draw(GHOST,5);assert(s.attempts==1);s.ui();assert(!has(s.log,"ui resolve")); } // ... no re-arm, the UI-time resolve keeps the frozen depth
    { Sim s;s.draw(GHOST,1);assert(s.attempts==0&&!s.frame.tried); } // before any terrain draw: no attempt, flag untouched
    { Sim s;s.draw(TERRAIN,1);s.reads=s.skinChecks=0;s.draw(OPAQUE_PROP,2);assert(s.reads==0&&s.skinChecks==1); } // non-skinned, not captured: one skinned check, no state read
    { Sim s;s.draw(TERRAIN,1);s.reads=s.skinChecks=0;s.draw(OPAQUE_SKIN,2);assert(s.reads==3&&s.skinChecks==1); } // opaque skinned: Z write, colour, blend off
    { Sim s;s.draw(TERRAIN,1);s.reads=s.skinChecks=0;s.draw(NOZ_BLEND,2);assert(s.reads==1&&s.skinChecks==1); } // no Z write: one state read
    { Sim s;s.draw(TERRAIN,1);s.draw(GHOST,2);s.reads=0;s.draw(PROP_NOZ,3);assert(s.reads==1); } // while captured: the undo check reads Z only for a no-Z draw
    std::printf("PASS translucent actor depth predicate, undo and latch\n");
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
dom=impl.index('if(worldDomain){');undo=impl.index('isUndo(');trig=impl.index('shouldResolve(');cen=impl.index('if(sampled()){const unsigned at=++censusDraws;');cap=impl.index('capture(vs);')
checks['undo, trigger, census and capture(vs) in order inside the world domain']=dom<undo<trig<cen<cap
blk=impl[trig:cen]
checks['trigger guarded by terrain, !captured and the armed latch']=all(x in impl[undo:trig+300] for x in ('terrain&&!captured','earlyDepth.armed()'))
checks['attempt latched before the resolve, resolve inside RawScope, success recorded']=blk.index('earlyDepth.attempt()')<blk.index('ExtensionDevice::RawScope')<blk.index('resolveDepth()')<blk.index('earlyDepth.success()')
checks['runtime undo needs captured and earlyCaptured, clears captured, counts undone']=(lambda u:all(x in u for x in ('captured&&earlyDepth.earlyCaptured','captured=false','earlyDepth.undo()','++earlyResolveUndone')))(impl[undo-80:trig])
checks['no SetRenderState and no resolve in the early/undo/census region']='SetRenderState' not in impl[dom:cap] and impl[dom:cap].count('resolveDepth()')==1
checks['state reads include Z write, blend, src/dst, colour write, skinned']=all(x in impl[dom:cen] for x in ('D3DRS_ZWRITEENABLE','D3DRS_ALPHABLENDENABLE','D3DRS_SRCBLEND','D3DRS_DESTBLEND','D3DRS_COLORWRITEENABLE','isSkinnedShader(vs)'))
hook=r[r.index('template<class Capture,class Draw> HRESULT drawHook('):][:3500]
checks['early block runs before the real draw (prepareDraw/prepareDrawImpl precede draw in both drawHook modes)']=(
    hook.index('prepareDraw(capture);')<hook.index('terrainShadowDraw(claimed,draw)')<hook.index('prepareDrawImpl(capture);')<hook.rindex('terrainShadowDraw(claimed,draw)'))
cf=r[r.index('void clearFrame() {'):r.index('void releaseResources()')]
checks['clearFrame resets the latch and the census']='earlyDepth.reset()' in cf and 'resetTranslucentCensus()' in cf
fin=r[r.index('void finishFrameImpl() {'):r.index('++frame;mirrorState.gate.frame')]
checks['TRANSLUCENT logged before clearFrame in finishFrameImpl']=fin.index('TRANSLUCENT frame=')<fin.index('clearFrame();')
terr=r[r.index('drop(worldDepth); worldDepth=ds;'):][:400]
checks['terrain draw undo uses earlyCaptured, counted before captured=false']=terr.index('captured&&earlyDepth.earlyCaptured')<terr.index('++earlyResolveUndone')<terr.index('terrain=true; captured=false;')
clr=r[r.index('HRESULT STDMETHODCALLTYPE Clear('):][:900]
checks['Clear(Z) after the early resolve freezes it instead of resolving']='earlyDepth.earlyCaptured' in clr and 'earlyDepth.freeze()' in clr and 'resolveDepth()' in clr
checks['state reads cached per draw']='known=true' in impl
checks['no TranslucentActorDepth setting']=all('ranslucentActorDepth' not in x for x in (r,q,w))
rd_=r[r.index('bool resolveDepth() {'):r.index('HRESULT quad(UINT w')]
checks['resolveDepth unchanged: SavedState and captured=true on success']='SavedState saved(ext,&stateBlocks);' in rd_ and 'captured = true; return true;' in rd_
checks['banner']='translucent depth census; early depth for translucent actors; backend=' in r
ini=fp.src('windows-package/northlight-quality.ini').read_text();rm=fp.src('windows-package/README.txt').read_text()
checks['docs: no setting in the ini or README']='TranslucentActorDepth' not in ini and 'TranslucentActorDepth' not in rm
for n,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+n)
assert all(checks.values())
print('PASS translucent actor depth wiring')
