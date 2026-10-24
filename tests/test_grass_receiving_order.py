#!/usr/bin/env python3
# northlight-test: requires=zig
"""Execute production receiver-order decisions; never infer live API states from BLS."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

renderer = fp.src('renderer.cpp').read_text()
prepare = renderer[renderer.index('template<class Capture> void prepareDrawImpl('):renderer.index('    // Terrain draws run with the game')]
assert prepare.index('beforeDraw(vs);') < prepare.index('capture(vs);')
assert prepare.index('NorthlightTranslucentDepth::isUndo(') < prepare.index('NorthlightTranslucentDepth::shouldResolve(') < prepare.index('capture(vs);')
hook = renderer[renderer.index('template<class Capture,class Draw> HRESULT drawHook('):renderer.index('    // 0.3.154: blob shadow claim')]
assert hook.index('prepareDraw(capture,count)') < hook.index('waterMrtDraw(draw,')
assert hook.index('prepareDrawImpl(capture,count)') < hook.rindex('waterMrtDraw(draw,')
boundary = renderer[renderer.index('void beforeDraw('):renderer.index('    // 0.3.203 (task 17): fallback boundary')]
assert 'renderEffects();spellReplay();' in boundary
effects = renderer[renderer.index('void renderEffects()'):renderer.index('    /* The guards of every mask decision')]
assert effects.index('!resolveDepth()') < effects.index('"copy scene"') < effects.index('world->render(') < effects.index('applied = true')
world = fp.src('world_renderer.h').read_text()
assert 'if(kind==1&&!detail)return;' in world
assert 'terrainShaders.count(shader)||captureShaders.count(shader)||wmoShaders.count(shader)' in world
assert 'if(vc.world||drawWaterVS)return;' in renderer  # native grass is not a fallback UI candidate
composite = fp.tracked('world_effects.hlsl').read_text().split('float4 WorldComposite(')[1].split('// Separate geometry pass')[0]
assert 'bool relight=d<.99999&&liquid<=0;' in composite and 'if(relight)' in composite
assert 'GrassShadows' not in composite and 'DetailDoodad' not in composite  # receiving is independent of caster opt-in

spell = fp.src('spell_layer.h').read_text()
start = spell.index('namespace NorthlightSpellLayer {')
end = spell.index('inline Result classify(')
end = spell.index('\n', end)
# The exact pure production classifier, without the unrelated D3D resource implementation.
classifier = '#include <cstdint>\nconstexpr unsigned D3DLOCK_READONLY=0x10,D3DLOCK_NOOVERWRITE=0x1000,D3DLOCK_DISCARD=0x2000,D3DUSAGE_DYNAMIC=0x200;\n' + spell[start:end] + '\n}\n'
with tempfile.TemporaryDirectory(prefix='northlight-grass-order-') as tmp:
    out = Path(tmp)
    (out / 'grass_spell_classifier.h').write_text(classifier)
    target = out / ('order.exe' if os.name == 'nt' else 'order')
    subprocess.run([str(fp.zig()), 'c++', '-std=c++17', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                    '-I', str(out), *fp.test_include_flags(), str(Path(__file__).with_suffix('.cpp')), '-o', str(target)],
                   env=fp.zig_env(), check=True)
    subprocess.run([str(target)], check=True)
print('PASS source order: game draw before UI composite, depth resolve before scene copy, grass independent of caster flag; live grass order/ZWRITE remain unobserved')
