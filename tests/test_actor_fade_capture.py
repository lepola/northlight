#!/usr/bin/env python3
# northlight-test:
"""0.3.215 (task 47) faded actors in the shadow capture (source audit of world_renderer.h and shadow_fate.h).
The game draws a faded actor (stealth, ghosts) as a depth-only pre-pass (blend off, Z-write on,
COLORWRITEENABLE=0) and then a blended colour pass; the pre-pass looked opaque and was captured as a solid
caster. captureModel now rejects any model draw that writes no RGB, right after the small-shadow return and
before the admission, the budget and the near reserve, for skinned and other draws alike. The fate tracker
keeps the last outcome per key and frame, so a faded body's frame fate reads `blend` (its colour pass shares
the key); the fadeRejected counter in the MODEL frame capture lines is the in-game evidence. The blend reject
follows right after it, also before the admission and the budget: a blended draw is never captured, so it must
not be counted as radius-gated, cut or budget-refused, feed the next cut or mark the frame short."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re
w=fp.src('world_renderer.h').read_text();fate=fp.src('shadow_fate.h').read_text()
capture=w[w.index('    void captureModel(D3DPRIMITIVETYPE type,'):w.index('    // One directional replay draw in the 0.3.142 order')]
check='GetRenderState(D3DRS_COLORWRITEENABLE,&colorWrite)'
enum=re.search(r'enum Fate : unsigned char \{([^}]*)\}',fate).group(1).split(',')
names=re.search(r'names\[FateCount\]=\{([^}]*)\}',fate).group(1).replace('\n',' ').split(',')
checks={}
blend='GetRenderState(D3DRS_ALPHABLENDENABLE,&blend)'
checks['one fade check and one blend check in captureModel, after the small-shadow return and before admission, budget, alpha test']=(
    capture.count(check)==1 and capture.count('D3DRS_ALPHABLENDENABLE')==1
    and capture.index('if(smallShadow&&!actorCaptureEnabled())')<capture.index(check)<capture.index(blend)<capture.index('NorthlightCaptureAdmission::Decision admit;')
    <capture.index('if(replaySnapshots.captureExhausted(priority))')<capture.index('GetRenderState(D3DRS_ALPHATESTENABLE')
    and capture.index('if(replays.size()>=4096)')<capture.index(check))
legacy_blend=('fate.reason=NorthlightShadowFate::Blend;\n        {DWORD blend=0;if(FAILED(d->'+blend+')||blend){if(sample&&priority)++skinnedBlendRejected;if(sample&&!priority)++otherBlendRejected;return;}}' in capture)
audited_grass_blend=(
    'bool blendAllowed=SUCCEEDED(d->'+blend+');' in capture
    and 'if(blendAllowed&&blend)' in capture
    and 'blendAllowed=metadata.detailDoodad&&' in capture
    and 'NorthlightDetailShadow::blendedMaterial(detailPixel,source,destination,operation)' in capture
    and 'if(!blendAllowed){' in capture
    and 'if(sample&&priority)++skinnedBlendRejected;if(sample&&!priority)++otherBlendRejected;return;}' in capture)
checks['blend: unreadable state rejects, only audited grass can blend, counted before admission']=(
    (legacy_blend or audited_grass_blend) and capture.count('NorthlightShadowFate::Blend')==1)
checks['reads COLORWRITEENABLE, masks RGB, sets the Fade fate first, applies to every model draw, a failed read does not reject']=(
    'fate.reason=NorthlightShadowFate::Fade;\n        {DWORD colorWrite=15;if(SUCCEEDED(d->'+check in capture and '(colorWrite&7)==0' in capture
    and 'if(SUCCEEDED(d->'+check+')&&(colorWrite&7)==0){' in capture and 'if(priority&&(colorWrite' not in capture)
checks['counters only on sample, declared, reset per frame']=('if(sample&&priority)++skinnedFadeRejected;if(sample&&!priority)++otherFadeRejected;return;' in capture
    and 'skinnedFadeRejected=0' in w and 'otherFadeRejected=0' in w
    and any('skinnedFadeRejected=' in l and 'otherFadeRejected=' in l and l.count('skinnedFadeRejected=')==1 for l in w.split('\n') if 'skinnedBlendRejected=skinned' in l or 'otherBlendRejected=other' in l))
checks['logged on the main and the other MODEL frame capture lines']=('blendRejected=%u fadeRejected=%u projectionRejected=%u' in w and 'skinnedBlendRejected,skinnedFadeRejected,skinnedProjectionRejected' in w
    and 'other blendRejected=%u fadeRejected=%u' in w and 'otherBlendRejected,otherFadeRejected,' in w)
checks['shadow_fate.h: Fade after DistanceCut, named "fade", enum and names agree']=('DistanceCut,Fade,FateCount' in fate and '"distanceCut","fade"' in fate
    and enum[-1].strip()=='FateCount' and len(enum)-1==len(names))
ok=True
for k,v in checks.items():
    print('PASS' if v else 'FAIL',k);ok&=bool(v)
assert ok
