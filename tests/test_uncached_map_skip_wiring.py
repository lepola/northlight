#!/usr/bin/env python3
# northlight-test:
"""0.3.210 (issue #40): maps without world-cache tiles (dungeons, raids, battlegrounds) skip geometry
builds. The cache covers only the continents; the builder used to run a full region build attempt
(memory sampling, admission, nine fopen probes) and publish a fresh error snapshot for every GI request.
Now the map is probed once per entry (FindFirstFileW on world-cache/<map>/*.fg3), the error is published
once, and no build runs until the map changes. The GI worker drops its region only on the first drop.
Source wiring; the behaviour runs in test_concurrent_geometry_build.py case (h). Writes nothing."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
HERE=Path(__file__).resolve().parent
w=fp.src('world_renderer.h').read_text();t=(HERE/'test_concurrent_geometry_build.py').read_text()
helper=w[w.index('    bool worldCacheHasTiles('):w.index('    NorthlightGeometryMemory::Sample geometryAdmission(')]
work=w[w.index('    void work() {'):w.index('\n    bool check(HRESULT h,const char* s)')]
gate=work[work.index('if(r.map!=cacheCheckedMap){'):work.index('auto built=std::make_unique<Built>();')]
drop=work[work.index('if(!serve){\n                    // Release worker ownership'):]
drop=drop[:drop.index('buildWake.notify_one();continue;')]
checks={
 'worldCacheHasTiles probes the UTF-8 world-cache/<map>/*.fg3 path with FindFirstFileW and closes the handle':
  'FindFirstFileW(std::filesystem::u8path(root+"world-cache/"+map+"/*.fg3").c_str(),&data)' in helper and 'FindClose(h)' in helper and 'if(map.empty())return false;' in helper,
 'only a missing folder or tile means no cache; other probe failures let the normal build run':
  'if(h==INVALID_HANDLE_VALUE){const DWORD error=GetLastError();return error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND;}' in helper,
 'the gate sits after the error lambdas and before the region build, outside the REGION BUILD block':
  work.index('auto deferBuild=')<work.index('if(r.map!=cacheCheckedMap){')<work.index('auto built=std::make_unique<Built>();')<work.index('// BEGIN REGION BUILD'),
 'the memo resets on a map change':
  'cacheCheckedMap=r.map;cacheMapHasTiles=worldCacheHasTiles(r.map);uncachedPublished=false;uncachedSkips=0;' in gate,
 'the error is published once; a superseded publication retries without another probe':
  'if(!uncachedPublished){result->message="No cached geometry for "+r.map;uncachedPublished=publishError();}' in gate and 'else if(r.id!=uncachedSkippedId){uncachedSkippedId=r.id;++uncachedSkips;}' in gate and gate.rstrip().endswith('}'),
 'entry line unconditional, end line Diagnostics-gated':
  gate.count('logf("WORLD cache absent map=\\"%s\\"; geometry builds skipped until the map changes"')==1
  and 'if(!cacheMapHasTiles&&NorthlightDiagnostics::enabled())logf("WORLD cache absent end map=\\"%s\\" skippedBuilds=%u"' in gate,
 'builder memo state':'std::string cacheCheckedMap;bool cacheMapHasTiles=true,uncachedPublished=false;unsigned uncachedSkips=0;uint64_t uncachedSkippedId=0;' in work,
 'the GI worker clears only on the first drop (a region still held), and restarts a stall episode begun on another map, on a drop or an adoption':
  'if(bvh){' in drop and drop.index('if(bvh){')<drop.index('probeCache.clear();')<drop.index('dynamicProbes.reset(')<drop.index('bvh.reset();')
  and 'if(stalled&&stallMap!=r.map)stalled=false;' in drop and 'stallMap=r.map;' in drop and 'geometryWanted=true' in work
  and 'if(stalled){if(stallMap==sceneMap)stallMs+=GetTickCount()-stallStart;stalled=false;}' in work,
 'coverage hold counts only drawable geometry, so the one entry error snapshot never begins or ends a hold':
  'const bool heldDrawable=active&&active->bvh;' in w and 'const bool holding=active&&active->bvh&&!NorthlightWorldStreaming::applicable(active->map,active->center,r.map,r.camera);' in w
  and 'if((retired&&(heldDrawable||coverageHold))||(coverageHold&&(adopted||!holding))){' in w,
 'the test fixture defines its own worldCacheHasTiles and case (h) asserts one probe per entry':
  'bool worldCacheHasTiles(const std::string& m){++cacheChecks;' in t and 'fixtureUncachedMaps' in t and 'assert(h.f.cacheChecks==expectedChecks);' in t,
}
for k,v in checks.items():print(('PASS ' if v else 'FAIL ')+k)
assert all(checks.values())
print('PASS uncached map skip wiring: one probe and one error per map entry, no build until the map changes, first-drop-only worker clears')
