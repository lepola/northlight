# northlight-test: requires=cxx
"""Run the production frame-boundary settings method with observed resource lifetimes."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
source=fp.src('world_renderer.h').read_text(encoding='utf-8')
start=source.index('    void applyLiveSettings(');end=source.index('{',start)+1;depth=1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}');end+=1
method=source[start:end]
work=source[source.index('    void work()'):source.index('    bool resources(')]
assert 'quality.gi)' not in work and 'if(!r.giEnabled)' in work
assert 'r.giEnabled=quality.gi!=0' in source
assert 'setEffects(requestedEffects)' in method and 'effects.gi=' not in method and 'requestedEffects=requested' in source[source.index('    void setEffects('):][:200]  # GI toggles go through setEffects with the hotkey state and 'settingsRequestDirty?256u' in source
for field in ('giRays','giFastBVH','giThreads'):assert '"'+{'giRays':'GIRays','giFastBVH':'GIFastBVH','giThreads':'GIThreads'}[field]+'"' in fp.src('settings_schema.h').read_text()
# Every Device member copied from the world at creation is either refreshed in the frame-boundary block or its key is restart-only ("R").
renderer=fp.src('renderer.cpp').read_text(encoding='utf-8');block=renderer[renderer.index('settingsService->after(settingsRevision)'):][:2200]
for refreshed in ('contactAO=world->contactAO()','aoStrength=world->aoStrength()','aoRadius=world->aoRadius()','selectAO(world->aoMethod(),world->aoQuality())','frameDrawGates=world->frameDrawGates()','zFuncAware=world->depthFuncAware()','nameMask.debug=','setStrength(world->blobShadowStrength())'):assert refreshed in block,refreshed
assert 'spellOptionsPending' in block and 'bool liveOptions(' in fp.src('spell_layer.h').read_text(encoding='utf-8')
restart=fp.src('settings_schema.h').read_text(encoding='utf-8')
assert 'particleOn=world->equipmentLightsOn()' in block
for key in ('NameMask','SpellLayer','CommandStream'):assert '"'+key+'"' in restart[restart.index('restart={'):][:400],key
harness=r'''
#include "settings_schema.h"
#include <atomic>
#include <cassert>
#include <iostream>
namespace NorthlightDiagnostics {bool value;void configure(bool v){value=v;}}
#ifndef _WIN32
namespace NorthlightWorldContext {struct SelfReadStats{inline static std::atomic<bool> timed{false};};}
#endif
namespace NorthlightWeatherMemory {void configure(bool){}}
namespace NorthlightMemorySwitches {void configure(bool){}}
namespace NorthlightRenderThreadProbe {void configure(bool,bool){}}
struct World {
    NorthlightQuality::Settings quality;
    NorthlightCelestialProfiles::Table celestialProfiles;
    bool settingsRequestDirty=false,paletteFrameValid=true;
    unsigned minSkinnedShadowTriangles=0,captureBudgetMiB=0,actorShadowBudgetMiB=0;
    bool shadowFateDiagnostics=false;
    struct Fx{bool gi=true;};Fx effects,requestedEffects;unsigned effectResets=0;
    void setEffects(const Fx& requested){requestedEffects=requested;Fx next=requested;next.gi=NorthlightQuality::giPass(quality,next.gi);if(effects.gi==next.gi)return;++effectResets;effects=next;} // the production method masks the user's key state by GI=0 the same way
    struct {float steps=0;} staticPrebuild;
    unsigned resets=0,invalidates=0;
    struct {size_t full=0,half=0;void configureBudget(size_t a,size_t b){full=a;half=b;}} replaySnapshots;
    void setNearReserve(){}
    void releaseGPU(bool keep){assert(keep);++resets;}
    void invalidateShadowCache(){++invalidates;}
__METHOD__
};
int main(){
    for(unsigned pass=0;pass<1000;++pass){
        World w;NorthlightSettings::Config next;
        for(const auto& k:NorthlightQuality::Keys)next.quality.*k.field=pass&1?k.low:k.high;
        next.quality.preset=NorthlightQuality::Preset::Performance;
        const auto old=w.quality;const auto oldProfiles=w.celestialProfiles;w.applyLiveSettings(next);
        const auto effective=NorthlightQuality::effective(next.quality);
        for(const auto& k:NorthlightQuality::Keys)assert(w.quality.*k.field==(NorthlightSettings::liveQuality(k.name)?effective.*k.field:old.*k.field));
        assert(w.settingsRequestDirty==NorthlightSettings::giRequestChanged(old,w.quality,oldProfiles,next.celestial));
        assert(!w.paletteFrameValid&&w.quality.preset==next.quality.preset);
        assert(w.effects.gi==(next.quality.gi!=0));
        assert(w.replaySnapshots.full==size_t(w.quality.captureBudgetMiB)*1048576);
        assert(w.resets==unsigned(old.actorShadows!=w.quality.actorShadows||old.actorShadowDetail!=w.quality.actorShadowDetail));
        w.applyLiveSettings(next);assert(w.resets<=1);
    }
    {   // The menu's GI toggle honours Ctrl+Shift+F8: with the key OFF, GI=1 must not switch it on; the key then follows GI=0/1 again.
        World w;w.setEffects({false});NorthlightSettings::Config c;c.quality=w.quality;c.quality.gi=0;w.applyLiveSettings(c);assert(!w.effects.gi);
        c.quality.gi=1;w.applyLiveSettings(c);assert(!w.effects.gi&&!w.requestedEffects.gi);w.setEffects({true});assert(w.effects.gi);
        World v;NorthlightSettings::Config d;d.quality=v.quality;d.quality.gi=0;v.applyLiveSettings(d);assert(!v.effects.gi&&v.effectResets==1);d.quality.gi=1;v.applyLiveSettings(d);assert(v.effects.gi&&v.effectResets==2);
    }
    std::cout<<"Live settings: 1000 production apply cases; F/R, GI request snapshot, CPU budget and optional GPU rebuild passed\n";
}
'''.replace('__METHOD__',method)
with tempfile.TemporaryDirectory(prefix='northlight-live-settings-') as tmp:
    cpp=Path(tmp)/'live.cpp';exe=Path(tmp)/'live.exe';cpp.write_text(harness,encoding='utf-8')
    subprocess.run(['clang++','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
