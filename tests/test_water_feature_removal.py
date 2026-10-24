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
expected={'PrecipitationQuality': '09dc5ddac9346c9b5d7aba438c579461572090170ec3310922e0c4467a3d09ab', 'PrecipitationPerformance': '63d280b89a5f2ac18d7ece6c93aca0d933794dbde701f597159b2f7da9a8f4b5'}
assert set(manifest['shaders'])==set(expected)
for entry,digest in expected.items():
    assert hashlib.sha256((fp.COMPILED/(entry+'.bin')).read_bytes()).hexdigest()==digest,entry
    assert manifest['shaders'][entry]['sha256']==digest
    assert ('k'+entry+'Shader') in fp.src('surface_compiled_shaders.h').read_text()
for entry in ('WaterQuality','WaterBalanced','WaterPerformance','WaterPointLights','WaterSSR','LateWaterSSR','SSRDepthBounds','SurfaceDepthCopy','WaterMaterialMaskVS','WaterMaterialMaskPS'):
    assert not (fp.COMPILED/(entry+'.bin')).exists(),entry
assert 'sampler2D FinalDepth : register(s3);' in (fp.SHADERS/'surface_effects.hlsl').read_text()
print('PASS water/SSR withdrawal: no feature calls/resources; exact upstream native mask; unchanged precipitation bytecode; weather/UI/material closure')
