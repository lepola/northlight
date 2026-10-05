#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.159 jump-stable cascade anchor: native model test of cascade_anchor.h (clang++, plain and
ASan/UBSan) and a wiring audit of world_renderer.h: the cascade frames, their cache placement and the
static prebuild frame and matrix use the anchor; ActorShadowRadius and the static caster request keep
the raw pivot; reset(), a new requested map and a camera jump over 40 yd restart the window. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
w=fp.src('world_renderer.h').read_text()
render=w[w.index('    bool render(IDirect3DSurface9* targetSurface'):]
checks={}
update='        const V cascadePivot=cascadeAnchor.update(pivot,vec(context.camera),uint32_t(GetTickCount()),anchorNewMap);\n'
checks['anchor updated once per render, right after the raw pivot']=(w.count('cascadeAnchor.update(')==1 and update in render
    and render.index('const V pivot=staticPivotReady?staticFramePivot:shadowPivot();')<render.index('actorShadowOrigin[0]=pivot.x;actorShadowOrigin[1]=pivot.y;actorShadowOrigin[2]=pivot.z;')<render.index(update)
    and 'const bool anchorNewMap=cascadeAnchorMap!=lastRequest.map;if(anchorNewMap)cascadeAnchorMap=lastRequest.map;' in render)
checks['cascade frames, prebuild frame and prebuild matrix use the anchor']=('cascadeFrames[source][cascade]=NorthlightWorldMath::shadowFrame(cascadePivot,sourceDirections[source],radius);' in render
    and 'staticPrebuild.frame(cascadePivot,' in render and 'NorthlightWorldMath::shadowMatrixFrom(NorthlightWorldMath::shadowFrame(cascadePivot,direction,radius),' in render
    and 'slotPlacement[slot]=NorthlightWorldMath::shadowCachePlacement(cascadeFrames[source][cascade],' in render)
after=render[render.index(update)+len(update):]
checks['no other consumer of the raw pivot after the anchor']=re.search(r'\bpivot\b',re.sub(r'//[^\n]*','',after)) is None
checks['static caster request keeps the raw pivot']='staticFramePivot=shadowPivot();staticPivotReady=true;' in w
checks['reset() restarts the window']='pivotValid=false;pivotDistance=12.f;pivotCorrection.reset();pivotSelfCaptured=false;cascadeAnchor.reset();' in w and '#include "cascade_anchor.h"' in w
a=fp.src('cascade_anchor.h').read_text()
checks['1 s window, 40 yd teleport reset, 24 yd lag bound, fixed 256-entry ring (no allocation)']=('constexpr uint32_t Window=1000;' in a and 'constexpr float ResetDistance=40;' in a
    and 'constexpr float MaxLag=24;' in a and 'constexpr unsigned Capacity=256;' in a and 'Sample minima[Capacity];' in a and all(x not in a for x in ('#include <deque>','#include <vector>','push_back','malloc')) and re.search(r'\bnew\s+\w+\s*[\[({]',a) is None)
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-anchor-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_cascade_anchor.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS jump-stable cascade anchor: model and wiring')
