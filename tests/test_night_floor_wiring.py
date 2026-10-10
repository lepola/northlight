#!/usr/bin/env python3
# northlight-test:
"""NightBrightness wiring: quality ini/READMEs, the single c19 declaration and its use in WorldComposite, the renderer's
c19 upload and the shader budget. Source/manifest audit only; no game, Wine or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json, re

ini = (fp.REPO/'renderer'/'windows-package'/'northlight-quality.ini').read_text()
readme = (fp.REPO/'renderer'/'windows-package'/'README.txt').read_text()
checks = {}
checks['ini: key documented, Allowed 0..100. 50 / 50 / 50']=(';NightBrightness=50' in ini and
    'Allowed 0..100. 50 / 50 / 50' in ini[ini.index(';NightBrightness=50')-500:ini.index(';NightBrightness=50')])
checks['README.txt: settings row']=bool(re.search(r'^  NightBrightness +50 / 50 / 50 ', readme, re.M))
checks['README.md: mentioned']='NightBrightness' in (fp.REPO/'README.md').read_text()
hlsl = (fp.SHADERS/'world_effects.hlsl').read_text()
checks['hlsl: one c19 declaration (GridOrigin), no NightFloor alias']=(len(re.findall(r'register\(c19\)', hlsl))==1 and
    'float4 GridOrigin : register(c19);' in hlsl and 'NightFloor' not in hlsl)
comp = hlsl[hlsl.index('float4 WorldComposite('):]
a = comp.find('color=max(color,mad(-.45,transported,original.rgb));')
b = comp.find('color=mad(albedoT,GridOrigin.rgb,color);')
c = comp.find('if(PassInfo.z==1)')
code = re.sub(r'//[^\n]*', '', hlsl)
checks['hlsl: ambient after the -.45 bound, before the debug views; GridOrigin.rgb only there, no literal']=(0 <= a < b < c and
    code.count('GridOrigin.rgb')==1 and 'GridOrigin.x' not in code and '.435' not in code)
# night_floor.h's ArtLayerNightAmbient is 1/factor-1 of the art layer's night ambient (build_lighting.py, a multi-key band at night).
import build_lighting
night = build_lighting.rgb(build_lighting.transform_color(1, 0xC8C8C8, 0, 2, None))
expected = [200/v-1 for v in night]
header = fp.src('night_floor.h').read_text()
literal = [float(v) for v in re.search(r'ArtLayerNightAmbient\[3\]=\{([^}]*)\}', header).group(1).replace('f','').split(',')]
checks['art layer night ambient: header == build_lighting']=all(abs(a-b)<.01 for a,b in zip(literal,expected))
# A single-key band gets x .78 with no tint, the case night_floor.h documents as not exactly given back.
single = build_lighting.rgb(build_lighting.transform_color(1, 0xC8C8C8, 0, 1, None))
checks['art layer single-key band: x .78, no tint (documented in night_floor.h)']=(all(abs(v/200-.78)<.01 for v in single) and
    'A single-key band gets x .78 with no tint' in header)
w = fp.src('world_renderer.h').read_text()
checks['renderer: c19.rgb from nightAmbient() on c18, w=8, old origin upload gone']=('NorthlightNightFloor::nightAmbient(quality.nightBrightness,' in w and
    '.sun.direction[2]:0.f),c[18],c[19]);\n        c[19][3]=8;' in w and
    'c[19][0]=active->origin' not in w and '#include "night_floor.h"' in w)
slots = json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']['WorldComposite']['static_instruction_slots']
checks['manifest: WorldComposite <= 512 slots']=slots <= 512
bad = [k for k, v in checks.items() if not v]
assert not bad, bad
print('PASS night floor wiring:', len(checks), 'checks; WorldComposite slots', slots)
