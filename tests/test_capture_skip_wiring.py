#!/usr/bin/env python3
# northlight-test:
"""0.3.141 capture skip wiring (source audit; the decision/schedule itself is exercised in
test_quality_settings.cpp and the snapshot slice in test_capture_skip_revalidation.py).
Every per-frame replay consumer is gated on freshReplays, so a skipped frame neither advances
nor clears their frame-counted state, and nothing is committed from a frame without replays.
0.3.158: consumers use replayShadows (freshReplays&&ActorShadows) and schedules replaysComplete
(freshReplays||!ActorShadows); both are freshReplays at ActorShadows=1 (test_actor_shadows_wiring.py)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import re
HERE=Path(__file__).resolve().parent
w=fp.src('world_renderer.h').read_text();pt=fp.src('world_point_rendering.inl').read_text();ex=fp.src('world_shadow_experiment.inl').read_text();r=fp.src('renderer.cpp').read_text()
def once(text,needle,what):assert text.count(needle)==1,f'{what}: expected exactly one {needle!r}'
render=w[w.index('    bool render(IDirect3DSurface9* targetSurface'):]
# Skip only model capture, inside WorldRenderer: terrain branch, water mask and mirror audit untouched.
once(w,'        if(modelCaptureSkipped())return; /* previous replays/actor packets are not needed this frame */\n        const CaptureShader* found=lookupShader(current).capture;if(!found){if(sample)++unknownCaptureCalls;return;}','model skip at captureModel entry')
cap=w[w.index('    void capture(D3DPRIMITIVETYPE type'):w.index('    void releaseReplayGPU()')]
assert cap.index('if(isTerrain)')<cap.index('captureModel('),'terrain capture precedes (and is independent of) the model skip'
assert 'modelCaptureSkipped' not in r and len(re.findall(r'world->capture(?:UP)?\([^;]*\);captureWater\(vs,',r))==4,'renderer.cpp: every draw still captures water right after world capture'
# Consumers of fresh replays on the render path.
for needle,what in [('if(replayShadows)selectShadowReplays();','selection history + fate frame'),
                    ('(replayShadows&&!timedReplay())','GPU cache LRU/probation clock'),
                    ('if(effects.shadows&&replayShadows){auto proofs','static dedup proofs/cursor'),
                    ('if(effects.shadows&&replayShadows)pointCalculateReplayBounds();','bounds cache resume cursors'),
                    ('renderPointShadow(replaysComplete,actorShadows)','point cube schedule'),
                    ('if(captureMode==CaptureFresh||!actorShadows)reuse.commit(interval,shadowPasses,matrices[cascade]);else captureDemand=true;','cascade commit')]:
    once(render,needle,what)
assert render.count('selectShadowReplays(')==1 and w.count('selectShadowReplays()')==1,'selection runs only from render'
assert ex.count('finishShadowFate();')==1 and 'finishShadowFate' not in w,'fate frame closes only inside the (gated) selection'
assert render.count('uploadReplay(')==1 and render.count('timedReplay()')==1,'replay upload only via timedReplay'
# Point: a frame without replays never commits the schedule.
body=pt[pt.index('    bool renderPointShadow(bool fresh=true,bool withReplays=true){'):pt.index('    bool renderPointLighting(')]
assert body.index('if(!fresh){pointReady=true;captureDemand=true;')<body.index('pointSchedule.commit('),'no point commit from a skipped frame'
assert 'pointSchedule.due(updateAt,pointSelected,meshGeneration,pointReplayCount' in body and 'if(fresh)pointReplayCount=withReplays?replays.size():0;' in body
# 0.3.151 face cycles: an incomplete cycle leaves the schedule stale, so the prediction stays due
# and the next frame captures; a frame without replays neither advances nor commits it.
pred=pt[pt.index('    bool pointRefreshPredicted()const{'):pt.index('    bool renderPointShadow(bool fresh=true,bool withReplays=true){')]
assert 'pointSchedule.due(GetTickCount(),light,meshGeneration,pointReplayCount,rebuild,quality.pointShadowRefreshMs)' in pred
assert 'void stale(){complete=false;}' in fp.src('point_light_shadow.h').read_text() and 'if(invalidated||!complete||' in fp.src('point_light_shadow.h').read_text()
assert body.index('if(!fresh){pointReady=true;captureDemand=true;')<body.index('pointFaceCycle.advance()'),'no cycle progress from a skipped frame'
# Snapshot frame, GI and diagnostics.
once(w,'if(captureMode!=CaptureSkipped)replaySnapshots.clearFrame();','snapshot frame on capture frames only')
once(w,'captureSkipped=%u','sampled capture log marks skipped frames');assert 'captureSkipped=%u' in r and 'world->captureSkippedLastFrame()' in r,'CPU profile marks skipped frames'
fin=w[w.index('    void finishActorScene(){'):w.index('    std::shared_ptr<const NorthlightActorGeometry::ActorJob> completedActorJob()')]
assert fin.index('if(!actorCaptureEnabled())return;')<fin.index('lastActorCapture='),'no GI stamp without a capture'
dec=w[w.index('    bool modelCaptureSkipped(){'):w.index('    void captureModel(')]
assert 'in.actorDue=actorCaptureEnabled();' in dec and 'const bool shadows=NorthlightQuality::actorShadowWork(quality,effects.shadows);' in dec
assert 'captureSkipPossible(quality,shadows)' in dec and 'in.shadows=shadows;' in dec and 'in.pointDue=in.shadows&&!in.actorDue&&!in.demand&&pointRefreshPredicted();' in dec
print('capture-skip wiring: model-only skip inside WorldRenderer; selection/fate, GPU cache, proofs, bounds, point schedule, snapshot frame gated; commits only from capture frames')
