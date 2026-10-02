#!/usr/bin/env python3
# northlight-test: requires=cxx
"""native sun/moon/moon02/sunGlare suppression by the wrapped (proxy)
identity. Native policy test (test_celestial_native_suppress.cpp) plus static
checks: the identity map is wired through the mirror registry without
dereferencing, suppression needs a live late disc (render() of the previous
frame), effects off (Ctrl+Shift+F10) never reaches the claim hooks, and the
early sky-phase disc (F1) stays off. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='northlight-celestial-suppress-') as tmp:
    binary=str(Path(tmp)/'test')
    subprocess.run(['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',*fp.test_include_flags(),
                    str(HERE/'test_celestial_native_suppress.cpp'),'-o',binary],check=True)
    native=subprocess.check_output([binary],text=True).strip()
assert native.startswith('PASS'),native

registry=fp.src('mirror_resources.h').read_text()
raw_of=registry[registry.index('std::uintptr_t rawOf('):]
raw_of=raw_of[:raw_of.index('\n    }\n')]
assert 'exposed_.find(reinterpret_cast<void*>(exposed))' in raw_of
assert 'shaped(' not in raw_of and 'disable(' not in raw_of  # a number from game memory: never dereferenced, never disables
assert 'if(found==exposed_.end())return passThroughMiss?exposed:0;' in raw_of  # mirror active: a miss is 0

renderer=fp.src('renderer.cpp').read_text()
assert 'celestialDiscs->setIdentityMap([this](std::uintptr_t exposed){return mirrorResources.rawOf(exposed,!mirrorState.enabled);});' in renderer
assert 'if((!early&&!observer.lateDisc(i,suppressed))||' in fp.src('celestial_disc_renderer.h').read_text()
# F10: every native claim is gated by the effects switch and the pre-effects phase.
# 0.3.187: one claim helper (count<=4 first) used by drawHook, the shared body of the four draw entry points.
hooks=re.findall(r'if\(count<=4&&celestialDiscs&&enabled&&!applied&&celestialDiscs->nativeClaimPossible\(t,count\)\)',renderer)
assert len(hooks)==1,len(hooks)
assert renderer.count('skyClaim(t,count,claimed);')==2 and renderer.count('return drawHook(t,count,')==4
effects=renderer[renderer.index('    void renderEffects() {'):renderer.index('template<class Capture> void prepareDraw(')]
assert effects.index('if (applied || !enabled || failed || !projectionValid) return;')<effects.index('celestialDiscs->render(')

host=fp.src('celestial_disc_renderer.h').read_text()
claim=host[host.index('template<class Palette> bool claimNativeDraw('):host.index('    bool claimNativeGlare(')]
assert 'if(!owning())return false;' in claim
assert claim.index('if(!owning())return false;')<claim.index('if(!EarlyDisc){suppressed|=1u<<body;++suppressedDraws;return true;}')
assert 'static constexpr bool EarlyDisc=false;' in host  # F1 (early disc) stays off
assert host.count('ownFrame=frames;')==1 and 'observer.finishObservations();ownFrame=frames;' in host  # owned only via render()
assert 'bool owning()const{return ownFrame+1==frames;}' in host
glare=host[host.index('    bool claimNativeGlare('):host.index('    bool drawDisc(')]
assert 'const unsigned owned=drawnEarly|drawnLate|offscreenOwned|suppressed|suppressedPrevious;' in glare
assert '!owning()' in glare and 'NorthlightCelestialGlare::claim(glareIdentities,current,bound,owned)' in glare
assert 'claimNoBody=%u' in host and 'suppressedDraws=%u suppressedGlares=%u' in host
native_h=fp.src('celestial_disc_native.h').read_text()
assert 'Identities read(){auto ids=readIdentities(NorthlightWorldContext::readSelf);return map_?mapIdentities(ids,map_):ids;}' in native_h
print(json.dumps({'status':'PASS','native':native,'claim_hooks':len(hooks),'game_or_gpu_launched':False},indent=2))
