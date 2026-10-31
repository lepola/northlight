# northlight-test:
"""Supplement production state/image oracles with cross-component material wiring."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
r=(root/'src/proxy/renderer.cpp').read_text()
w=(root/'src/world/world_renderer.h').read_text()
p=(root/'src/world/world_point_rendering.inl').read_text()
s=(root/'shaders/world_effects.hlsl').read_text()
assert r.index('autoBump->finishCaptures();')<r.index('world->setMaterialDetail(')<r.index('world->render(saved.targets[0]')
assert 'world->captureSources().shareSnapshotPool(autoBump->captureSources());' in r
assert 'world->terrainShader(vs)' in r and 'if(sharedTerrain)captureWorld(&positions);' in r
assert 'if(!sharedTerrain)captureWorld();' in r
assert 'positions.owner=&geometry;positions.shader=vs;' in r
assert 'setDeferredCapture(selectedBackend==NorthlightBackend::Kind::Dxvk&&NorthlightUpload::ReadBackNoOverwrite' in r
assert 'setTextureWriteGuard(spellOn&&mirrorState.active()&&NorthlightStream::streamActive' in r
assert 'beforeDisable=&Device::materialMirrorEscape' in r
assert 'autoBump->occlusionDistance():8.f' in r
assert 'materialOcclusionDistance' in w and 'occlusionInfo,1' in w
assert w.index('"temporal light pass"')<w.index('"current local light clear"')<w.index('"local direct light batch"')<w.index('renderPointLighting(depth')
assert 'SetTexture(3,currentLocalLighting?light:neutralZero)' in w
assert 'SetPixelShaderConstantF(12,bumpControl,1)' in p
for sampler,name in [(3,'normalBuffer'),(4,'baselineLight'),(5,'materialDetail')]:
    assert f'SetTexture({sampler},{name})' in p
assert 'sampler<6' in w[w.index('renderPointLighting(depth'):][:1200]
assert 'sampler2D CurrentLocalLighting : register(s3)' in s
assert 'tex2Dlod(CurrentLocalLighting' in s
print('PASS shared terrain bounds/source pool, deferred safety gates, near occlusion, current local light isolation and point input restoration')
