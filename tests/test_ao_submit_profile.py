#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compile the actual AO submission instrumentation: no disabled clocks or extra draws."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp

source = fp.src('renderer.cpp').read_text()
start = source.index('const bool timeAOBlur=')
end = source.index('cpuAOBlurPending=true;}', start) + len('cpuAOBlurPending=true;}')
region = source[start:end]
scope = source[source.index('struct CpuScope {'):source.index('static NorthlightMemoryDiagnostics::Sample')]
assert 'logf(' not in region
assert source.index('logf("AO_SUBMIT') > source.index('if(sampledFrame&&cpuFrequency.QuadPart>0)')
folder = fp.out() / 'rotation-disabled-ao/cpu-submit'
folder.mkdir(parents=True, exist_ok=True)
cpp = folder / 'actual-region.cpp'
cpp.write_text('''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
using LONGLONG=long long;using HRESULT=int;
struct LARGE_INTEGER {LONGLONG QuadPart=0;};
static unsigned clocks=0;
static bool QueryPerformanceCounter(LARGE_INTEGER* value){value->QuadPart=++clocks;return true;}
''' + scope + '''
struct GPU {
    bool on=false;std::vector<std::string> labels;
    bool recording()const{return on;}
    std::uint64_t sampledFrame()const{return on?9876:0;}
    void mark(const char* label){if(on)labels.emplace_back(label);}
};
struct Extension {
    bool fail=false;std::vector<int> calls;
    HRESULT SetRenderTarget(unsigned slot,int target){calls.push_back(int(slot)+target*100);return fail?-1:0;}
    void SetTexture(unsigned slot,int texture){calls.push_back(1000+int(slot)+texture*100);}
    void SetTexture(unsigned slot,std::nullptr_t){SetTexture(slot,0);}
    void SetPixelShader(int shader){calls.push_back(2000+shader);}
};
enum class Bucket {AO};
struct Buckets {unsigned marks=0;void mark(Bucket){++marks;}};
struct Device {
    GPU gpu;GPU* gpuProfile=&gpu;Extension extension;Extension* ext=&extension;Buckets effectsBuckets;
    LARGE_INTEGER cpuFrequency{1000000};
    unsigned width=137,height=79,aoMethod=2,draws=0,bindings=0;bool failDraw=false;
    int aoSurface=3,aoRaw=4,aoBlurPS=5,scene=6;
    LONGLONG cpuAOBlurSetup=0,cpuAOBlurSubmit=0;std::uint64_t cpuAOBlurFrame=0;
    unsigned cpuAOBlurMethod=0,cpuAOBlurWidth=0,cpuAOBlurHeight=0;bool cpuAOBlurPending=false,diagnosticAo=false;
    bool error(HRESULT hr,const char*){return hr<0;}
    void bindLegacyFog(){++bindings;}
    HRESULT quad(unsigned w,unsigned h,bool viewport){assert(w==68&&h==39&&!viewport);++draws;return failDraw?-1:0;}
    void run(){
''' + region + '''
    }
};
int main(){
    for(bool recording:{false,true})for(bool frequency:{false,true})for(bool targetFailure:{false,true})for(bool drawFailure:{false,true}){
        Device d;d.gpu.on=recording;d.cpuFrequency.QuadPart=frequency?1000000:0;d.extension.fail=targetFailure;d.failDraw=drawFailure;
        clocks=0;CpuScope::reads=0;d.run();
        const bool timed=recording&&frequency;
        assert(clocks==(timed?(targetFailure?2u:4u):0u)&&CpuScope::reads==clocks);
        assert(d.draws==(targetFailure?0u:1u)&&d.bindings==(targetFailure?0u:1u));
        assert(d.cpuAOBlurPending==(timed&&!targetFailure&&!drawFailure));assert(d.diagnosticAo==(!targetFailure&&!drawFailure));
        if(d.cpuAOBlurPending){assert(d.cpuAOBlurFrame==9876&&d.cpuAOBlurMethod==2&&d.cpuAOBlurWidth==68&&d.cpuAOBlurHeight==39);assert(d.cpuAOBlurSetup==1&&d.cpuAOBlurSubmit==1);}
        const std::vector<int> states=targetFailure?std::vector<int>{300}:drawFailure?std::vector<int>{300,1402,1400,2005}:std::vector<int>{300,1402,1400,2005,1002,1600};
        assert(d.extension.calls==states);
        const auto count=recording?(targetFailure?0u:drawFailure?1u:2u):0u;assert(d.gpu.labels.size()==count);
        if(count)assert(d.gpu.labels[0]=="AOBlurSetup");if(count==2)assert(d.gpu.labels[1]=="AOBlur");
    }
}
''')
exe = folder / 'actual-region.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                str(cpp), '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True)
print('PASS actual AO instrumentation: 16 success/failure gates, off=0 clocks, sampled=4 clocks, unchanged states and one draw')
ring = folder / 'query-ring.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-I' + str(ROOT / 'tests/support/gpu_profile'), *fp.test_include_flags(),
                str(ROOT / 'tests/test_gpu_profile.cpp'), '-o', str(ring)], env=fp.zig_env(), check=True)
subprocess.run([str(ring)], check=True)
