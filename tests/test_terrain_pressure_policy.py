#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Execute production memory-pressure wiring and world cap setter on the logged trace.

Budget consumers are recording fakes; the guard and allocation admission are
production code. The separate MPQ rotation fixture measures actual retention.
Two compiled negative controls must fail the demand and in-episode low-VA checks.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import hashlib
import json
import subprocess
import tempfile

renderer = fp.tracked('renderer.cpp').read_text(encoding='utf-8')
world = fp.tracked('world_memory_guard.inl').read_text(encoding='utf-8')


def statement(prefix):
    matches = [line.strip().split(';', 1)[0] + ';' for line in renderer.splitlines()
               if line.strip().startswith(prefix)]
    assert len(matches) == 1, (prefix, len(matches))
    return matches[0]


def method(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


assignments = '\n'.join(statement(prefix) for prefix in [
    'if(decision.entered||decision.recovered)memoryWorld=',
    'if(decision.capsChanged)memoryCaps=',
])
event = renderer.index('        if(memoryWorld>=0||memoryCaps>=0||memoryTrim){')
start = renderer.index('            LARGE_INTEGER workStart=', event)
last = 'QueryPerformanceCounter(&workEnd);'
end = renderer.index(last, start) + len(last)
wiring = renderer[start:end]
setter = method(world, 'void setMemoryPressure(')

body = r'''
#include "memory_guard.h"
#include "geometry_memory.h"
#include "cpu_retirement.h"
#include <atomic>
#include <cassert>
#include <cstdio>
using namespace NorthlightMemoryGuard;
static bool frameCleared=false;
struct LARGE_INTEGER {long long QuadPart=1;};
static void QueryPerformanceCounter(LARGE_INTEGER*){}
namespace NorthlightDrawSnapshot {struct Frame {static constexpr std::uint64_t SnapshotCacheLimit=64*MiB;};}
namespace NorthlightReplayCopies {
    std::atomic<bool> enabled{true};bool pressure=false;
    void setPressure(bool on){assert(frameCleared);pressure=on;}
}
namespace NorthlightStream {std::atomic<bool> memoryPressure{false};}
struct CacheLimit {
    std::uint64_t bytes;
    void setSnapshotCacheLimit(std::uint64_t n){assert(frameCleared);bytes=n;}
    void setPersistentByteLimit(std::uint64_t n){assert(frameCleared);bytes=n;}
    void setLimit(std::uint64_t n){assert(frameCleared);bytes=n;}
};
struct WorldRenderer {
    struct MemoryTrim {};
    bool memoryPressure=false,nearReserve=true;unsigned updates=0;
    unsigned trims=0;
    CacheLimit replaySnapshots{NorthlightDrawSnapshot::Frame::SnapshotCacheLimit};
    CacheLimit terrainBoundsCache{32*MiB},replayGpuCache{64*MiB};
    void setNearReserve(){assert(frameCleared);nearReserve=!memoryPressure;++updates;}
    MemoryTrim trimMemory(bool,NorthlightStreaming::CpuRetirementBatch*){assert(frameCleared);++trims;return {};}
    @SETTER@
};
struct AutoBumpCaps {
    bool pressure=false,critical=false;unsigned updates=0;
    void setMemoryPressure(bool on){assert(frameCleared);pressure=on;++updates;}
    void setMemoryPressurePolicy(bool on,bool low,NorthlightStreaming::CpuRetirementBatch*){critical=low;if(pressure!=on)setMemoryPressure(on);}
};
struct Controller {
    Guard memoryGuard;WorldRenderer worldState;AutoBumpCaps bumpState;LARGE_INTEGER cpuFrequency;
    WorldRenderer* world=&worldState;AutoBumpCaps* autoBump=&bumpState;
    unsigned trims=0;
    Controller(){NorthlightReplayCopies::pressure=false;NorthlightStream::memoryPressure=false;}
    void clearFrame(){frameCleared=true;}
    void trimMemory(int,const WorldRenderer::MemoryTrim&,double,NorthlightStreaming::CpuRetirementBatch*){assert(frameCleared);assert(worldState.trims==trims+1);++trims;}
    Decision sample(std::uint64_t available,std::uint64_t largest,std::uint32_t tick,std::uint64_t demand=0){
        frameCleared=false;
        const auto decision=memoryGuard.update(available,largest,tick,demand);
        const bool memoryTrim=decision.trim;int memoryCaps=-1,memoryWorld=-1;const int memorySample=0;
        @ASSIGNMENTS@
        clearFrame();
        @WIRING@
        assert(!retirementQueued); // Recording consumers own no retirement payloads.
        return decision;
    }
};
int main(){
    // The sampled rotation episode: aggregate demand is unmet, but the guard's
    // own low-VA thresholds are not crossed. Its 237 MiB hole cannot recover.
    Controller c;c.sample(900*MiB,237*MiB,0);
    auto d=c.sample(755*MiB,237*MiB,1000,768*MiB);
    assert(d.entered&&d.demand&&d.trim&&c.trims==1&&!d.capsChanged);
    assert(c.worldState.terrainBoundsCache.bytes==32*MiB&&"DEMAND_TERRAIN_CAP");
    assert(c.worldState.memoryPressure&&!c.worldState.nearReserve&&c.bumpState.pressure);
    assert(!c.bumpState.critical);
    assert(c.worldState.replaySnapshots.bytes==32*MiB&&c.worldState.replayGpuCache.bytes==32*MiB);
    assert(!NorthlightReplayCopies::pressure&&!NorthlightStream::memoryPressure.load());
    for(std::uint32_t tick=2000;tick<=200000;tick+=1000){
        d=c.sample(755*MiB,237*MiB,tick,tick<20000?768*MiB:0);
        assert(!d.trim&&!d.capsChanged&&!d.recovered&&c.memoryGuard.pressure());
        assert(c.worldState.terrainBoundsCache.bytes==32*MiB&&"DEMAND_TERRAIN_CAP");
    }
    assert(c.trims==1&&c.worldState.updates==1&&c.bumpState.updates==1);
    // This historical 768 MiB demand still exercises the guard above. The
    // geometry policy now admits it, while retaining its new exact boundary.
    namespace G=NorthlightGeometryMemory;
    assert(G::buildBudget().available==512*MiB&&G::ProcessReserve==256*MiB&&G::ContiguousMargin==32*MiB);
    assert(G::admits({755*MiB,237*MiB,true},G::buildBudget()));
    assert(G::admits({767*MiB,237*MiB,true},G::buildBudget()));
    assert(G::admits({768*MiB,237*MiB,true},G::buildBudget()));
    assert(!G::admits({511*MiB,237*MiB,true},G::buildBudget()));
    assert(G::admits({512*MiB,237*MiB,true},G::buildBudget()));
    // Crossing a genuine low threshold inside the same episode must change
    // terrain immediately even though there is no new entered edge.
    d=c.sample(800*MiB,159*MiB,201000);
    assert(!d.entered&&d.capsChanged&&d.halfCaps&&d.trim&&c.trims==2);
    assert(c.worldState.terrainBoundsCache.bytes==16*MiB&&"TRUE_LOW_TERRAIN_CAP");
    assert(c.worldState.updates==2&&c.bumpState.updates==1);
    assert(c.bumpState.critical);
    assert(c.worldState.replaySnapshots.bytes==32*MiB&&c.worldState.replayGpuCache.bytes==32*MiB);
    assert(NorthlightReplayCopies::pressure&&NorthlightStream::memoryPressure.load());
    assert(!G::admits({900*MiB,159*MiB,true},G::buildBudget(128*MiB)));
    assert(G::admits({900*MiB,160*MiB,true},G::buildBudget(128*MiB)));
    // Do not weaken actual-low recovery: the demand regrowth floor and 256 MiB
    // largest-block threshold still hold for the full ten-second interval.
    for(std::uint32_t tick=202000;tick<225000;tick+=1000){
        d=c.sample(900*MiB,237*MiB,tick);
        assert(!d.recovered&&c.worldState.terrainBoundsCache.bytes==16*MiB);
    }
    assert(c.memoryGuard.recoverAvailable()==896*MiB);
    assert(!c.sample(900*MiB,256*MiB,225000).recovered);
    assert(!c.sample(900*MiB,256*MiB,234999).recovered);
    d=c.sample(900*MiB,256*MiB,235000);
    assert(d.recovered&&d.capsChanged&&!d.halfCaps&&!c.memoryGuard.pressure());
    assert(c.worldState.terrainBoundsCache.bytes==32*MiB);
    assert(c.worldState.replaySnapshots.bytes==64*MiB&&c.worldState.replayGpuCache.bytes==64*MiB);
    assert(!c.worldState.memoryPressure&&c.worldState.nearReserve&&!c.bumpState.pressure);
    assert(!NorthlightReplayCopies::pressure&&!NorthlightStream::memoryPressure.load());
    // Aggregate low VA, exact boundary, one-shot demand recovery and cooldown.
    Controller aggregate;
    assert(!aggregate.sample(400*MiB,160*MiB,0).entered);
    d=aggregate.sample(399*MiB,500*MiB,1000);
    assert(d.entered&&d.halfCaps&&aggregate.worldState.terrainBoundsCache.bytes==16*MiB);
    assert(!G::admits({399*MiB,500*MiB,true},G::buildBudget()));
    Controller quiet;quiet.sample(755*MiB,300*MiB,0,768*MiB);
    assert(!quiet.sample(800*MiB,300*MiB,1000).recovered);
    assert(!quiet.sample(800*MiB,300*MiB,10999).recovered);
    assert(quiet.sample(800*MiB,300*MiB,11000).recovered);
    assert(quiet.worldState.terrainBoundsCache.bytes==32*MiB&&!quiet.bumpState.pressure);
    d=quiet.sample(755*MiB,237*MiB,12000,768*MiB);
    assert(d.entered&&d.demand&&!d.trim&&quiet.trims==1);
    assert(quiet.worldState.terrainBoundsCache.bytes==32*MiB);
    std::puts("PASS production wiring: demand 32 MiB and one trim; in-episode true-low 16 MiB; unchanged world savings, recovery hysteresis, allocation floors and cooldown");
}
'''.replace('@SETTER@', setter).replace('@ASSIGNMENTS@', assignments).replace('@WIRING@', wiring)

out = fp.output_dir()
report = {'scope': __doc__, 'sources': {}, 'runs': []}
for name in ['renderer.cpp', 'world_memory_guard.inl', 'memory_guard.h', 'geometry_memory.h']:
    report['sources'][name] = hashlib.sha256(fp.tracked(name).read_bytes()).hexdigest()
with tempfile.TemporaryDirectory(prefix='terrain-pressure-policy-') as temporary:
    folder = Path(temporary)
    cases = [('production', body, None),
             ('demand-halves-terrain', body.replace('world->setMemoryPressure(memoryGuard.pressure(),memoryGuard.capsHalved());',
                                                   'world->setMemoryPressure(memoryGuard.pressure(),memoryGuard.pressure());'), 'DEMAND_TERRAIN_CAP'),
             ('misses-in-episode-low', body.replace('(memoryWorld>=0||memoryCaps>=0)&&world', '(memoryWorld>=0)&&world'), 'TRUE_LOW_TERRAIN_CAP')]
    for name, source, rejection in cases:
        assert name == 'production' or source != body
        path = folder / (name + '.cpp')
        path.write_text(source, encoding='utf-8')
        modes = [('O2', ['-O2'])]
        if not rejection:
            modes.append(('ASan-UBSan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']))
        for mode, flags in modes:
            executable = folder / (name + '-' + mode)
            command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                       *fp.test_include_flags(), str(path), '-o', str(executable)]
            subprocess.run(command, check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            log = result.stdout + result.stderr
            (out / (name + '-' + mode + '.log')).write_text(log, encoding='utf-8')
            if rejection:
                assert result.returncode != 0 and rejection in log, (name, result.returncode, log)
                print('PASS negative control:', name)
            else:
                result.check_returncode()
                print(log, end='')
            report['runs'].append({'case': name, 'mode': mode, 'exit_code': result.returncode,
                                   'expected_rejection': rejection, 'command': command})
(out / 'terrain-pressure-policy.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
