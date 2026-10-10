#!/usr/bin/env python3
# northlight-test:
"""NightBrightness wiring: quality ini/READMEs, the c19 NightFloor alias and its use in WorldComposite, the renderer's
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
checks['hlsl: NightFloor aliases c19']='float4 NightFloor : register(c19);' in hlsl and 'float4 GridOrigin : register(c19);' in hlsl
comp = hlsl[hlsl.index('float4 WorldComposite('):]
a = comp.find('color=max(color,mad(-.45,transported,original.rgb));')
b = comp.find('color=mad(NightFloor.y/max(dot(oldLight,NightFloor.xxx),1),color-fogPart,color);')
c = comp.find('if(PassInfo.z==1)')
code = re.sub(r'//[^\n]*', '', hlsl)
checks['hlsl: lift after the -.45 bound, before the debug views, NightFloor only in WorldComposite']=(0 <= a < b < c and
    code.count('NightFloor')==3 and 'NightFloor' not in code[:code.index('float4 WorldComposite(')].replace('float4 NightFloor : register(c19);','') and
    'NightFloor' not in code[code.index('float4 WorldComposite('):][code[code.index('float4 WorldComposite('):].index('\n}\n'):])
w = fp.src('world_renderer.h').read_text()
checks['renderer: c19 from constants(), z=0 w=8, old origin upload gone']=('NorthlightNightFloor::constants(' in w and 'context.ambient,c[19]);c[19][2]=0;c[19][3]=8;' in w and
    'c[19][0]=active->origin' not in w and '#include "night_floor.h"' in w)
slots = json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']['WorldComposite']['static_instruction_slots']
checks['manifest: WorldComposite <= 512 slots']=slots <= 512
bad = [k for k, v in checks.items() if not v]
assert not bad, bad
print('PASS night floor wiring:', len(checks), 'checks; WorldComposite slots', slots)
