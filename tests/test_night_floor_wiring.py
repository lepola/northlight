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
checks['ini: key documented, Allowed 0..100. 0 / 0 / 0']=(';NightBrightness=0' in ini and
    'Allowed 0..100. 0 / 0 / 0' in ini[ini.index(';NightBrightness=0')-500:ini.index(';NightBrightness=0')])
checks['README.txt: settings row']=bool(re.search(r'^  NightBrightness +0 / 0 / 0 ', readme, re.M))
checks['README.md: mentioned']='NightBrightness' in (fp.REPO/'README.md').read_text()
hlsl = (fp.SHADERS/'world_effects.hlsl').read_text()
checks['hlsl: one c19 declaration (GridOrigin), no NightFloor alias']=(len(re.findall(r'register\(c19\)', hlsl))==1 and
    'float4 GridOrigin : register(c19);' in hlsl and 'NightFloor' not in hlsl)
comp = hlsl[hlsl.index('float4 WorldComposite('):]
a = comp.find('color=max(color,mad(-.45,transported,original.rgb));')
b = comp.find('color=mad(GridOrigin.x,max(original.rgb-color,0),color);')
c = comp.find('if(PassInfo.z==1)')
code = re.sub(r'//[^\n]*', '', hlsl)
checks['hlsl: limit after the -.45 bound, before the debug views; GridOrigin.x only there']=(0 <= a < b < c and code.count('GridOrigin.x')==1)
w = fp.src('world_renderer.h').read_text()
checks['renderer: c19.x from blend(), yz=0 w=8, old origin upload gone']=('c[19][0]=NorthlightNightFloor::blend(' in w and 'c[19][1]=c[19][2]=0;c[19][3]=8;' in w and
    'c[19][0]=active->origin' not in w and '#include "night_floor.h"' in w)
slots = json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']['WorldComposite']['static_instruction_slots']
checks['manifest: WorldComposite <= 512 slots']=slots <= 512
bad = [k for k, v in checks.items() if not v]
assert not bad, bad
print('PASS night floor wiring:', len(checks), 'checks; WorldComposite slots', slots)
