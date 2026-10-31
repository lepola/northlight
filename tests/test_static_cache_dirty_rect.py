#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.136 static cache dirty rects: native raster proof plus renderer wiring invariants. Never launches game/Wine."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
src=fp.src('world_renderer.h').read_text()
gpu=fp.src('static_shadow_gpu.h').read_text()
# One switch; false restores the 0.3.135 full clear+redraw for every static change.
assert re.search(r'static constexpr bool StaticCacheDirtyRects=(true|false);',src)
# Rects only for static-model-only changes of a valid, anchor-retained cache.
# 0.3.172: the persistent casters (0.3.141) are retired: static models alone take the rect path.
assert 'partialStatic=StaticCacheDirtyRects&&key.valid&&staticDirtyRects(key,cachedMatrix);' in src
assert src.index('}else if(staticChanged){reason="static-models";')>src.index('reason="local-content"')
assert re.search(r'persistent(Casters|Signature|Content|Changed|Id|Now)\b|PersistentCasters',src) is None
# Same scissor for clear, local batches and static casters; disabled afterwards.
assert 'd->Clear(DWORD(staticDirtyClears.size()),staticDirtyClears.data(),D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0)' in src
# Explicit clear rects with the scissor off; the scissor is enabled only after the clear.
i=src.index('staticDirtyClears.data()')
assert src.rindex('d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);',0,i)>src.index('auto renderCache=')
assert i<src.index('d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);if(!band){++shadowCachePartialRenders;')
# Scissor rects are set after SetRenderTarget (which resets them), per draw.
assert src.index('d->SetRenderTarget(0,target)')<src.index('d->SetScissorRect(&r)')
assert 'drawStaticCasters(cachedMatrix,partial?&staticDirty:nullptr)' in src
# RAII: the scissor is switched off on every exit, including device failures.
assert 'struct ScissorOff {IDirect3DDevice9* d;bool on;~ScissorOff(){if(on)d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);}} scissorOff{d,partial};' in src
# Debug self-check exists and is off by default; dedup diagnostic is counted.
assert 'static constexpr bool StaticCacheDirtyRectVerify=false;' in src and 'verifyStaticCache(slot,' in src
assert '++staticDedupUncommitted;' in src and 'dedupUncommitted=%u verified=%u verifyMismatches=%u scissorLeaks=%u' in src
# Cost choice: full redraw whenever the rect walk would not issue fewer draw calls.
# 0.3.152: one dirtyRectsFrom() for the render thread (member buffers/counters) and the plan worker.
assert 'if(w.costs[1]>=w.costs[0]){++w.costFull;return false;}' in src
assert '{staticDirtyScratch,staticDirtyFootprints,staticDirty,staticDirtyBounding,staticDirtyCosts,shadowCachePartialBounding,shadowCacheCostFull,shadowCachePartialCalls,shadowCachePartialFullCalls}' in src
assert 'D3DRS_SCISSORTESTENABLE,rects?TRUE:FALSE' in gpu
# Content record refreshed after every successful redraw, dropped on any doubt.
assert 'staticCasters.record(cachedMatrix,key.staticContent)' in src and 'k.staticContent.valid=false' in src
# Record refuses a different epoch, instancing mode or matrix.
assert 'old.epoch!=epoch_||old.instancing!=canInstance_||std::memcmp(old.matrix,matrix,sizeof(old.matrix))!=0' in gpu
assert 'staticFull=%u staticPartial=%u partialRects=%u partialBounding=%u costFull=%u' in src
# 0.3.151 StaticCacheSlices (static_cache_slices.h). The default 1 never starts a cycle:
# sliceable() is gated on the key, so no slice state is touched.
assert 'if(NorthlightStaticSlices::sliceable(quality.staticCacheSlices,partialStatic,shadowDiagnosticCapture!=0,StaticCacheDirtyRectVerify)){' in src
assert src.count('slices.start(')==1 and 'staticSlices[4]' in src and 'for(auto& c:staticSlices)c.reset();' in src
# Any placement/local-content reason drops a cycle before the change detection (full redraw).
assert src.index('auto& slices=staticSlices[slot];if(reason)slices.reset();')>src.index('reason="local-content"')
# Intermediate bands clear `reason`, so cascadeAction may keep Reuse; they are drawn before the action.
assert 'staticDirty=slices.band();if(!slices.last()){reason=nullptr;sliceBand=true;}' in src
assert 'if(slices.last()){reason=slices.reason;partialStatic=true;}else sliceBand=true;' in src
band=src[src.index('            if(sliceBand){'):src.index('const auto action=NorthlightQuality::cascadeAction(')]
assert 'renderCache(shadowCacheSurface[slot],true,true,true)' in band and 'slices.drew()' in band
assert 'cascadeAction(reuse,interval,shadowPasses,!reason&&key.valid&&!pull' in src
# The key (signatures, recorded content) is written only by the reason path: at completion.
assert not any(k in band for k in ('key.staticSignature=','.record(','key.serial='))
assert src.index('if(slices.active){staticSliceBands+=partialStatic;slices.reset();}')<src.index('staticCasters.record(cachedMatrix,key.staticContent)')
# Restarts and finishing redraws include every band already drawn (the add-then-remove ghost).
assert 'staticDirtyFootprints.insert(staticDirtyFootprints.end(),slices.drawn.begin(),slices.drawn.end());' in src
assert 'NorthlightShadowBounds::dirtyRects(slices.drawn,long(ShadowCacheSize),StaticCacheDirtyTile,StaticCacheDirtyMaxRects,staticDirty);' in src
assert 'slicedBands=%u sliceRestarts=%u' in src and 'staticSliceBands=staticSliceRestarts=0;' in src
with tempfile.TemporaryDirectory() as t:
    slices=Path(t)/'slices'
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',*fp.test_include_flags(),str(HERE/'test_static_cache_slices.cpp'),'-o',str(slices)],check=True)
    out=subprocess.run([str(slices)],check=True,capture_output=True,text=True).stdout
    assert 'cycle/restart/cap/finish table passed' in out,out
    print(out.strip())
with tempfile.TemporaryDirectory() as t:
    exe=Path(t)/'dirty'
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-O2',*fp.test_include_flags(),str(HERE/'test_static_shadow_dirty_rect.cpp'),str(fp.src('static_shadow_scene.cpp')),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
    out=subprocess.run([str(exe)],check=True,capture_output=True,text=True).stdout
    assert 'sliced cycles' in out and 'add-then-remove' in out and 'bit-identical to full redraw' in out and 'readiness at scale' in out and 'order-dependent at D24 tie: yes' in out,out
    print(out.strip())
print('PASS static cache dirty-rect wiring and native raster parity')
