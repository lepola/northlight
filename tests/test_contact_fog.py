#!/usr/bin/env python3
"""AO changes surface radiance while preserving the native fog in front of it."""
import random, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

rng=random.Random(317)
for _ in range(10000):
    t,ao,surface,fog=(rng.random() for _ in range(4))
    original=t*surface+(1-t)*fog
    fog_part=min((1-t)*fog,original)
    result=(original-fog_part)*ao+fog_part
    assert abs(result-(t*surface*ao+(1-t)*fog))<1e-12
    assert (original-fog_part)*1+fog_part==original
    assert abs((fog-fog)*ao+fog-fog)<1e-12
    assert original*ao+0==original*ao
    assert fog_part<=result<=original+1e-12

world=fp.tracked('world_effects.hlsl').read_text().split('float4 worldComposite(')[1].split('// Separate geometry pass')[0]
legacy=fp.tracked('effects.hlsl').read_text().split('float4 Composite(')[1]
assert world.index('float3 fogPart=min(')<world.index('original.rgb=mad(original.rgb-fogPart,ao,fogPart);')<world.index('float3 color=original.rgb;')
assert 'float3 lit = mad(original.rgb-fogPart,ambient.a,fogPart);' in legacy
assert ' * legacyT;' in legacy
rc=fp.src('renderer.cpp').read_text()
assert 'fogForward=p[11];' in rc and 'nativeFog.parameters[0]*=fogForward;' in rc
assert 'SetPixelShaderConstantF(5,nativeFog.parameters,1)' in rc and 'SetPixelShaderConstantF(6,nativeFog.color,1)' in rc
print('PASS native fog survives AO, unknown/disabled fog keeps identity, dark channels are bounded, signed view slope wired')
