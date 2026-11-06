#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Execute the renderer's actual budget methods with deterministic stream waits."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

renderer = fp.src('renderer.cpp').read_text()
methods = renderer[renderer.index('    void gpuBudgetFrame(){'):renderer.index('    void flushFrameIntervals(')]
source = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include "gpu_budget.h"
#include "stream_stats.h"
namespace NorthlightRenderThreadProbe {bool profiling(){return false;}}
template<class... T>void logf(const char*,T...){}
struct World {
    unsigned gpu=4,cpu=3,gl=0,cl=0;
    unsigned gpuBudgetMs(){return gpu;}unsigned actorShadowCpuMs(){return cpu;}
    void setGpuBudgetLevel(unsigned v){gl=v;}void setCpuBudgetLevel(unsigned v){cl=v;}
    unsigned effectiveActorShadowRadius(){return NorthlightGpuBudget::actorShadowRadius(40,cl);}
};
struct Timer {unsigned polls=0;bool poll(double& ms){++polls;ms=10;return true;}};
struct Device {
    World settings,*world=&settings;Timer timer,*gpuTimer=&timer;
    NorthlightStream::Counters counters;const NorthlightStream::Counters* streamStats=&counters;
    NorthlightGpuBudget::WaitSignal gpuBound,replayWait;
    NorthlightGpuBudget::Controller gpuBudget,cpuBudget;
    bool enabled=true,limitGpuLatency=true,latencyFailed=false,boundKnown=false,gpuLimited=false,replayKnown=false,cpuLimited=false;
    unsigned clockReads=0;
    struct {std::int64_t QuadPart=1000000;}cpuFrequency;
    std::int64_t tick=1,boundQpc=0,gpuBudgetQpc=0,cpuBudgetQpc=0,cpuEffectsQpc=0;
    std::uint64_t eventGpuWaitPrevNs=0,replayWaitPrevNs=0,replaySkipPrev=0,latencyWaits=0;
    double latencyLastMs=0,latencyWaitMs=0,gpuBudgetLastMs=0,cpuBudgetLastMs=0;
    std::int64_t qpcNow(){++clockReads;return tick;}bool sampled(){return false;}bool diagnostics(){return false;}
''' + methods + r'''
    void frame(std::uint64_t gpu=0,std::uint64_t replay=0){
        NorthlightStream::own(counters.eventGpuWaitNs,gpu);
        NorthlightStream::own(counters.eventReplayWaitNs,replay);
        tick+=16667;cpuEffectsQpc=10000;gpuBudgetFrame();cpuBudgetFrame();
    }
    void run(std::uint64_t gpu,std::uint64_t replay){frame();for(unsigned i=0;i<240;++i)frame(gpu,replay);}
};
int main(){
    {Device d;d.enabled=false;d.run(1000000,1000000);assert(!d.timer.polls&&!d.clockReads&&!d.settings.gl&&!d.settings.cl);
        d.enabled=true;d.run(1000000,0);assert(d.timer.polls&&d.clockReads&&d.settings.gl==3);}
    {Device d;d.run(1000000,0);assert(d.gpuBound.bound()&&d.settings.gl==3&&!d.settings.cl);
        for(unsigned i=0;i<800;++i)d.frame();assert(!d.settings.gl&&!d.settings.cl);}
    {Device d;d.run(0,1000000);assert(!d.gpuBound.bound()&&d.replayWait.bound()&&!d.settings.gl&&d.settings.cl==3);}
    {Device d;d.run(1000000,1000000);assert(d.settings.gl==3&&!d.settings.cl);}
    {Device d;d.run(0,0);assert(!d.settings.gl&&!d.settings.cl);}
    {Device d;d.latencyLastMs=1;d.run(0,0);assert(d.settings.gl==3&&!d.settings.cl);}
    {Device d;d.limitGpuLatency=false;d.run(0,1000000);assert(!d.boundKnown&&d.settings.gl==3&&d.settings.cl==3);}
    {Device d;d.settings.gpu=d.settings.cpu=0;d.run(1000000,1000000);assert(!d.settings.gl&&!d.settings.cl);}
    {Device d;d.frame(1000000000,1000000000);assert(d.gpuBound.smoothed==0&&d.replayWait.smoothed==0);
        d.frame();assert(d.gpuBound.smoothed==0&&d.replayWait.smoothed==0);
        d.frame(1000000,1000000);assert(d.gpuBound.smoothed>0&&d.replayWait.smoothed>0);
        d.counters.eventGpuWaitNs.store(0);d.counters.eventReplayWaitNs.store(0);d.frame();
        assert(d.gpuBound.smoothed<.5f&&d.replayWait.smoothed<.5f);
        d.boundQpc=0;d.gpuBound.reset();d.replayWait.reset();d.frame(1000000000,1000000000);
        assert(d.gpuBound.smoothed==0&&d.replayWait.smoothed==0);}
    {Device d;d.frame();NorthlightStream::own(d.counters.skippedFrames);d.frame();assert(d.replayWait.smoothed>0);}
    {Device d;d.frame();NorthlightStream::own(d.counters.presentNs,1000000);d.frame();assert(d.replayWait.smoothed>0);}
    std::puts("EVENT_BUDGET_PASS GPU/replay/mixed/game/legacy/off/reset/zero-transition/recovery; diagnostics disabled");
}
'''
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
cpp = out / 'event_budget.cpp'
cpp.write_text(source)
for optimization in ['-O2', '-O0']:
    exe = out / ('event_budget' + optimization + '.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', optimization,
                    '-UNDEBUG', *fp.test_include_flags(), str(cpp), '-o', str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)
