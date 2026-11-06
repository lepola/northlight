#!/usr/bin/env python3
# northlight-test:
"""Zone tuning wiring audit (static source analysis; nothing is run): every dev tuning identifier in
src/world/world_renderer.h and src/proxy/renderer.cpp sits inside `#if NORTHLIGHT_DEV_TUNING` (the shipped build
compiles none of it and keeps its #else lines), the macro is only ever used as a plain `#if`, the overlay header is included
only there, and the shared sources keep the identity defaults (buildField's zoneScale is null by default)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re

TOKENS = re.compile(r'NorthlightZoneTuning|\bdev(?:Built|BuiltSnapshot|Builds)\b|\btuning\b|\b(?:requestedFogTuning|fogTuning)\b|zone_tuning\.h|\bdev(?:Poll|Reload|WriteState|Snapshot|Overrides|Mutex|TerrainReach|Error|Revision|Path|FogScale)\b|\bcelestialShipped\b|\breachZone\b')

def outside_dev(text):
    """Lines (number, text) not inside an active `#if NORTHLIGHT_DEV_TUNING` branch, comments removed."""
    stack, found = [], []
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    for number, line in enumerate(text.splitlines(), 1):
        code = re.sub(r'//.*', '', line).strip()
        if code.startswith('#'):
            directive = code[1:].strip()
            if directive.startswith(('if', 'ifdef', 'ifndef')):
                stack.append(directive.split(None, 1)[1].strip() == 'NORTHLIGHT_DEV_TUNING' if directive.startswith('if ') else None)
            elif directive.startswith('else') and stack and stack[-1] is not None:
                stack[-1] = not stack[-1]
            elif directive.startswith('endif'):
                stack.pop()
            continue
        if TOKENS.search(code) and not any(s for s in stack if s):
            found.append((number, code))
    return found

for name in ('world_renderer.h', 'renderer.cpp'):
    text = fp.src(name).read_text()
    bad = outside_dev(text)
    assert not bad, f'{name}: dev tuning code outside #if NORTHLIGHT_DEV_TUNING: {bad[:3]}'  # the shipped DLL must contain none of it
    uses = re.findall(r'^\s*#\s*(?:if|ifdef|ifndef|elif)\b[^\n]*NORTHLIGHT_DEV_TUNING[^\n]*$', text, flags=re.M)
    assert all(re.fullmatch(r'\s*#\s*if NORTHLIGHT_DEV_TUNING\s*', u) for u in uses), f'{name}: use only a plain #if NORTHLIGHT_DEV_TUNING'
w = fp.src('world_renderer.h').read_text()
assert w.count('#if NORTHLIGHT_DEV_TUNING') >= 6, 'the dev hooks (include, members, constructor, palette, worker reach and fog) exist'
# Release builds use the authored reach and the independent cached fog field with identity scale.
assert 'const float profileReach=shadowRanges.at(r.map,NorthlightRegionalFog::zoneAt(paletteRegion->region,center.x,center.y));' in w
assert 'buildCachedField(root+"world-cache",r.map,r.camera,paletteRegion->region,*field,fogError,fogAdmission,sceneSource,scale)' in w
assert 'const NorthlightRegionalFog::ZoneScale* scale=nullptr;' in w
cache=fp.src('regional_fog_cache.h').read_text()
assert 'const ZoneScale* scale=nullptr' in cache and 'buildField(ground,region,center.x,center.y,scale)' in cache
# Geometry and fog now publish independently. Each keeps the immutable tuning snapshot used by that build.
assert 'NorthlightZoneTuning::terrainRadius(*tuning,' in w and 'devBuilt=tuning;' in w
assert 'const auto requestedFogTuning=devSnapshot();' in w and 'NorthlightZoneTuning::fogScale(*requestedFogTuning,' in w
assert 'if(accepted)fogTuning=requestedFogTuning;' in w and 'fogTuningChanged=requestedFogTuning!=fogTuning;' in w
assert w.index('const auto tuning=devSnapshot();') < w.index('// BEGIN REGION BUILD')
f = fp.src('regional_fog.h').read_text()
assert 'const ZoneScale* zoneScale=nullptr' in f and '(*zoneScale)(zone,cell.day,cell.nightExtra);' in f  # identity unless a scale is passed
assert '!indoors&&cell.height>0)(*zoneScale)' in f, 'the hook never touches indoor cells or an unknown zone (policy returned early there)'
assert 'zone_tuning' not in f and 'ZoneTuning' not in f, 'the portable fog policy stays free of the dev overlay'
b = (fp.REPO / 'scripts' / 'build_renderer.py').read_text()
assert "'-DNORTHLIGHT_DEV_TUNING=1'" in b and b.count('NORTHLIGHT_DEV_TUNING') == 1, 'the define is added in one place, behind --dev-tuning'
# glow_strength split: the fog volume pass (shafts / aureole) reads the glow colours, the surface passes the light colours.
assert 'const V& shown=volume?sourceGlowColors[source]:sourceColors[source];' in w and 'celestialLight.sunGlowColor' in w
print('zone tuning wiring ok')
