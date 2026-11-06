# northlight-test:
"""Water/SSR withdrawal must leave native liquid masks, precipitation and UI fixes intact."""
import hashlib,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
r=fp.src('renderer.cpp').read_text();w=fp.src('world_renderer.h').read_text();surface=fp.src('surface_renderer.h').read_text()
for token in ('waterMaterials','drawWaterMaterial','liquidMaterial(','surfaceFinalDepth(','surfaceDiagnostic(','surfaceFogOutput','surfacePointLights(','WaterMaterial','WaterSSR'):
    assert token not in r+w+surface,token
for token in ('surface->water(','surface->waterReflections(','completedDepth','reflectionPS','copyColor(','depthBounds('):
    assert token not in r+surface,token
for name in ('water_material.h','water_material_mask_compiled.h'):
    assert not any((directory/name).exists() for directory in fp.SRC_DIRS),name
assert hashlib.sha256(fp.src('water_renderer.h').read_bytes()).hexdigest()=='de38a585098fc2803151cf38de5703a0bfe0021d2bd3391cf1c5e818d9b4e523'
assert 'water->plan(vs,ps,desc.Width,desc.Height,viewport)' in r
assert r.count('water->capture(')==2 and 'water->resolveMask()' in r and 'water->maskTextureForDepth(' in r
assert 'surface->precipitation(' in r and 'supplementalWeatherOutdoors()' in w
replay=r[r.index('    void spellReplay()'):r.index('    void spellReplay()')+5000]
assert 'void surfaceWeather()' in r and replay.count('surfaceWeather();')==2
assert replay.index('surfaceWeather();')<replay.index('gpuProfile->endFrame();')
assert 'NorthlightUiBoundary::accepts(zw!=0,c,zf==D3DCMP_ALWAYS,z!=0)' in r
manifest=json.loads((fp.SHADERS/'surface-shader-build.json').read_text())
# Camera frost/sand and particle/weather material updates retain the water withdrawal.
expected={
    'RainQuality': '61f11c91ba98602e046a8819afd2c5c69b1a14b27678c208f24fa16610a955ea',
    'SnowQuality': '607b5dd71175684298beead48000a3934658841e793fae7b600363204679e6f5',
    'RainPerformance': '3874eca5969e75f3b6d235cd0ba69e1193f91a065d5ba8e82521c01b1602012f',
    'SnowPerformance': 'e82040e44fa2a9af5351820659142cf93a111de741d80de7d532644804eb76d7',
    'RainLens': 'fccc18bb213b0e7cff13952f87f5b4dbbe9fbc0d8c4caae1e1e4fd19aa54f20d',
    'SnowLens': '139ebb74b075120060a237d438c7f4c75d0369476bb8b93d7280126a826e3204',
    'FrostVignette': '7a1fe02742a682ef36b8729b1a9285e419f9b8c78fb05f669efecac980fb7b40',
    'SandLens': '9d7f559fdc18f00db7425cac527fed1f8e29c076e7169ef4b598ca469c07dc7f',
    'WeatherLensParticle': 'a87788e26d832ce915ff2803092a429f202def61a188da6d7f46eab62d0762fc',
}
assert set(manifest['shaders'])==set(expected)
for entry,digest in expected.items():
    assert hashlib.sha256((fp.COMPILED/(entry+'.bin')).read_bytes()).hexdigest()==digest,entry
    assert manifest['shaders'][entry]['sha256']==digest
    assert ('k'+entry+'Shader') in fp.src('surface_compiled_shaders.h').read_text()
for entry in ('WaterQuality','WaterBalanced','WaterPerformance','WaterPointLights','WaterSSR','LateWaterSSR','SSRDepthBounds','SurfaceDepthCopy','WaterMaterialMaskVS','WaterMaterialMaskPS'):
    assert not (fp.COMPILED/(entry+'.bin')).exists(),entry
assert 'sampler2D FinalDepth : register(s3);' in (fp.SHADERS/'surface_effects.hlsl').read_text()
print('PASS water/SSR withdrawal: no feature calls/resources; exact upstream native mask; specialized precipitation bytecode; weather/UI/material closure')
