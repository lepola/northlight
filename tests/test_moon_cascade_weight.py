#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.211 (task 31): a zero moon weight is an exactly zero moon colour (native clang++, plain and ASan/UBSan),
and the shadow cascades, the actor maps and the capture skip all gate a source on that
colour. So no moon cascade is drawn on a frame whose moon lighting weight is zero. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
w=fp.src('world_renderer.h').read_text()
gate='NorthlightGI::dot(sourceColors[0],sourceColors[0])>1e-10f'
checks={
    'cascades gate on the colour':'bool sourceActive[2]={'+gate in w,
    'actor maps follow the cascades':'if(!actorMapOn||!sourceActive[source]){actorValid[source]=false;actorHold[source].invalidate();}' in w,
    'capture skip uses the same test':'in.sourceActive[source]=NorthlightGI::dot(sourceColors[source],sourceColors[source])>1e-10f;' in w,
    'colours are the celestial direct*weight':'resolvedSources.sources[1].color=vec(celestialLight.moonColor);' in w,
    'static proofs use current source demand':'prepareStaticProofs(sourceActive[0]||sourceActive[1]);' in w,
    'static cache stays warm for reentry':'updateStaticCasters(effects.shadows,&streamBudget);' in w,
}
proofs=w[w.index('void prepareStaticProofs(bool directionalActive){'):w.index('void prepareStaticM2(){')]
assert proofs.index('p->staticProofMask=p->staticM2Mask=0;')<proofs.index('if(!directionalActive)return;')<proofs.index('StaticShadowDedup::Budget budget;')
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-moon-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra',*flags,*fp.test_include_flags(),str(HERE/'test_moon_cascade_weight.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS moon cascades: zero weight is zero colour')
