#!/usr/bin/env python3
# northlight-test:
"""Static wiring audit: the geometry worker falls back to the base terrain reach on a memory
refusal, logs the stall episodes; stage entry uses buildBudget (768 MiB, raised by entryLift after a refusal that
followed entry), interior allocations and the after-bvh gate inBuildBudget; stalls publish the guard's demand."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re

w = fp.src('world_renderer.h').read_text()
g = fp.src('geometry_memory.h').read_text()
r = fp.src('renderer.cpp').read_text()
f = fp.src('terrain_reach_fallback.h').read_text()
assert 'constexpr uint64_t ProcessReserve=512*MiB;' in g and 'constexpr uint64_t ContiguousMargin=32*MiB;' in g
assert 'constexpr uint64_t EntryHeadroom=256*MiB;' in g and 'budgetWith(EntryHeadroom,largestAllocation)' in g and 'add(ProcessReserve,headroom,b.available)' in g and 'add(largestAllocation,ContiguousMargin,b.largest)' in g
# Allocations inside a build that passed buildBudget at entry use the smaller in-build headroom.
assert 'constexpr uint64_t InBuildHeadroom=128*MiB;' in g and 'budgetWith(largestAllocation>InBuildHeadroom?largestAllocation:InBuildHeadroom,largestAllocation)' in g
def body(start, end='};'):
    i = w.index(start); return w[i:w.index(end, i)]
for lam in ('NorthlightGI::AllocationAdmission localAdmission=', 'NorthlightGI::AllocationAdmission terrainAdmission='):
    assert 'NorthlightGeometryMemory::inBuildBudget(' in body(lam), lam
    assert 'NorthlightGeometryMemory::buildBudget(' not in body(lam), lam
# The after-bvh gate keeps the in-build floor (the build's own BVH and cache are live there); with the
# entry floor every attempt that entered within one BVH of 768 MiB was refused and rebuilt each second.
plan = body('const auto planBudget=', 'continue;')
assert 'planBudget=NorthlightGeometryMemory::inBuildBudget();' in plan and 'NorthlightGeometryMemory::buildBudget(' not in plan
assert 'logGeometryMemory("after-bvh"' in plan and 'liftEntry("plan",planBudget.available,memory.available,planBudget.largest)' in plan
# Stage entry keeps the full entry budget, raised by entryLift after a refusal that followed entry.
entry = body('auto memory=workerMemory(entryLift.apply(NorthlightGeometryMemory::buildBudget(64', 'stallBegin("build-deferred"')
assert entry.count('NorthlightGeometryMemory::buildBudget(') >= 2 and 'inBuildBudget(' not in entry
assert 'const auto entryBudget=entryLift.apply(plainEntry,GetTickCount());' in entry
# Small shortfalls may reclaim raw payloads first. Every step resamples the same
# plain floor; complete reset remains the final fallback and a lift alone clears nothing.
assert 'if(!NorthlightGeometryMemory::admits(memory,plainEntry)){' in entry
raw = 'unsigned reclaimed=1;sceneSources.trimPayloads();memory=workerMemory(plainEntry);'
local = 'if(!NorthlightGeometryMemory::admits(memory,plainEntry)){reclaimed=2;localGeometry.reset();memory=workerMemory(plainEntry);}'
full = 'if(!NorthlightGeometryMemory::admits(memory,plainEntry)){reclaimed=3;resetPreparation();memory=workerMemory(plainEntry);}'
assert entry.index(raw) < entry.index(local) < entry.index(full)
assert entry.count('sceneSources.trimPayloads();') == 1 and entry.count('localGeometry.reset();') == 1
assert entry.count('resetPreparation();') == 1  # a lift alone keeps every optional owner
reset = body('auto resetPreparation=')
assert all(text in reset for text in ('meshPreparation.reset();', 'sceneSources.reset();', 'localGeometry.reset();'))
assert 'stallBegin("build-deferred",memory,entryBudget.available,entryBudget.largest)' in w
# Every refusal after entry that does not fall back to the base reach raises the next entry.
for stage in ('local-geometry', 'shadow-terrain', 'mesh-page', 'mesh-page-seal'):
    assert any('liftEntry("%s",' % stage in line and 'deferBuild(' in line and 'Largest)' in line for line in w.splitlines()), stage
assert w.count('liftEntry(') == 5  # local, plan, shadow-terrain, mesh-page, mesh-page-seal
assert 'entryLift.clear();' in body('cacheCheckedMap=r.map;', '\n') and 'stallEnd(memoryStall.reduced?"reduced":"published");entryLift.clear();' in w
# The demand the memory guard reads is set by every stall begin and cleared by every stall end.
assert 'geometryMemoryDemand.store(std::min(required,NorthlightGeometryMemory::buildBudget().available)' in body('auto stallBegin=', '};')
assert 'geometryMemoryDemand.store(0,' in body('auto stallEnd=', '};')
# Extended reach is restored only when an entry sample covers what the refused attempt lacked.
assert 'NorthlightTerrainReach::choose(reachState,profileReach,GetTickCount(),entryAvailable,entryLargest)' in w
assert 'memoryRefused(reachState,reach,GetTickCount(),restore,terrainRequiredLargest)' in w
assert 'entryLift.clear();reachState={};' in w  # a map change forgets both
# Entry admitted: the build's own allocations are not a demand on the guard.
assert 'geometryMemoryDemand.store(0,' in body('const uint64_t entryAvailable=', 'auto liftEntry')
assert 'localSample=sample' in w and 'plan-deferred' in w
assert 'BaseReach=928.f' in f and 'BackoffMs=30000' in f and '#include <windows.h>' not in f
st = fp.src('shadow_terrain.h').read_text()
assert 'constexpr float Radius=928.f;' in st
assert '#include "terrain_reach_fallback.h"' in w
for stage in ('shadow-terrain', 'mesh-page"', 'mesh-page-seal'):
    assert re.search(r'stallBegin\("%s,?[^;]*,terrainRequired(?:,terrainRequiredLargest)?\);\s*if\(reduceReach\(\)\)goto terrainStage;' % re.escape(stage.rstrip('"')), w.replace('\n', ' ')), stage
assert w.count('goto terrainStage') == 3 and w.count('terrainStage:') == 1
assert 'NorthlightTerrainReach::choose(reachState,profileReach' in w
assert 'reach=reachChoice.reach;' in w and 'const bool extended=reach>NorthlightShadowTerrain::Radius;' in w
for text in ('WORLD geometry memory stall begin stage=%s availableMiB=%llu largestMiB=%s generations=%zu refused=%s need=%llu',
             'WORLD geometry memory stall end ms=%u attempts=%u outcome=%s',
             'WORLD shadow terrain reach reduced from=%.0f to=%.0f reason=memory',
             'WORLD shadow terrain reach tier from=%.0f to=%.0f reason=headroom availableMiB=%llu largestMiB=%llu',
             'WORLD shadow terrain reach restored to=%.0f'):
    assert text in w, text
# A superseded build ends its stall in abandonGeometry() (one stallEnd("superseded"), also clearing the abandoned region's peak),
# called from the three places that used to call stallEnd("superseded") directly.
assert 'stallEnd(memoryStall.reduced?"reduced":"published")' in w and w.count('stallEnd("superseded")') == 1 and w.count('abandonGeometry();') == 3
assert 'WORLD skip episode begin reason=%s' in r and 'WORLD skip episode reason=%s last=%s frames=%u ms=%lu' in r
assert '#include "terrain_reach_fallback.h"' in (Path(__file__).parent / 'test_concurrent_geometry_build.py').read_text()
assert 'stallEnd("error")' in w and 'if(abandoned)abandonGeometry();' in w
print('terrain reach fallback wiring PASS')

# Geometry-memory logs: a non-exact largest is printed as ">=N" and every refusal names its limit.
assert 'formatLargestMiB(largest,sizeof largest,sample.largest,exactLargest)' in w and 'largestFreeMiB=%s' in w and 'largestFreeMiB=%llu' not in w
assert 'refused=%s need=%llu' in w and w.count('refused=%s need=%llu') == 2
assert 'inline Refusal refusal(Sample sample,Budget budget)' in g and '">=%llu"' in g
