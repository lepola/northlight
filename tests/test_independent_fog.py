#!/usr/bin/env python3
"""Fog data and ambient extinction must not depend on a GI/shadow publication.

The corresponding real-MPQ and actual-WorldRenderer checks are
validate_independent_fog.py and test_world_screen_effects_gpu.py.
"""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

host=fp.src('world_renderer.h').read_text()
worker=host[host.index('    void work() {'):host.index('    bool check(HRESULT')]
fog=worker[worker.index('// BEGIN FOG FIELD BUILD'):worker.index('// END FOG FIELD BUILD')]
assert worker.index('// END FOG FIELD BUILD')<worker.index('// BEGIN REGION BUILD')
assert 'r.fogEnabled' in fog and 'inBuildBudget' in fog and 'buildBudget' not in fog
assert 'publishedFogRegion=next' in fog and 'fogRegion=std::move(next)' in fog
assert 'previous field retained' in fog
upload=host[host.index('    bool uploadRegionalFog('):host.index('    bool frameStaging=')]
assert 'active->' not in upload and 'uploadedFogField=field;uploadedFogMap=map;' in upload
assert 'if(!uploadRegionalFog(field,frameMap))return false;' in host
assert 'if(effects.fog&&uploadedFogField)' in host and 'effects.fog&&active&&uploadedFogField' not in host
assert 'if(volume&&!geometryReady)rgb[0]=rgb[1]=rgb[2]=0;' in host
assert 'r.fogEnabled=effects.fog;' in host
cache=fp.src('regional_fog_cache.h').read_text()
assert 'cityInField(region,center.x,center.y)?5u:1u' in cache
assert 'buildField(ground,region,center.x,center.y,scale)' in cache
assert 'BVH' not in cache[cache.index('inline bool buildCachedField'):]
print('PASS independent fog: own publication before heavy admission, bounded terrain/city source, geometry-free upload, ambient retained, unshadowed shafts disabled, fog toggle request')
