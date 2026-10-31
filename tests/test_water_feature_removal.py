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
expected={
    'RainQuality': 'b4114756ac7a2436c43ca3569aaaa7643f65956fd196ada4d0080eee84d1263b',
    'SnowQuality': '266a9c1284a7bb113ba62afc14134091421fa0f1819f3126aff1cfa79952b1c4',
    'RainPerformance': '9840fc871916dcabaef3476481994aaa536005a49b5025e52e55cdc42ec23160',
    'SnowPerformance': 'b9f653ca3cee66d8a592ca5dbd4490a5ff78bd223392ba3cce7c80afc30a028b',
    'RainLens': 'fccc18bb213b0e7cff13952f87f5b4dbbe9fbc0d8c4caae1e1e4fd19aa54f20d',
    'SnowLens': '139ebb74b075120060a237d438c7f4c75d0369476bb8b93d7280126a826e3204',
    'WeatherLensParticle': '541807725d3b39f54ea99b200a484a86d5ecb9a578287b28b4c6a40272f25e0c',
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
