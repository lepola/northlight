#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Issue #101: execute production sampled-state capture with CPU-only fake getters."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

renderer = fp.src('world_renderer.h').read_text()
start = renderer.index('    void localSourceBatch(')
end = renderer.index('    void reportLocalSources(', start)
helper = renderer[start:end]
start = renderer.index('        sourceTrace.begin(')
gate = renderer[start:renderer.index(';', start) + 1]
assert 'if(!sourceTrace.wants(first,count))return;' in helper
assert all(forbidden not in helper for forbidden in ('SetRenderState', 'SetViewport', 'SetScissorRect', 'Lock', 'GetRenderTargetData', 'Create'))
assert renderer.count('localSourceBatch(') == 5  # Definition, 8-/16-light direct, fog, all-fog-clipped.
for field in ('particleProbes=', 'particleAccepted=', 'particlePoints=', 'particlePublished=', 'selected=',
              'depthRange=', 'sceneDepthViewport=', 'camera=', 'projection=', 'fogEnabled=', 'fogField=',
              'waterReady=', 'viewportDepth=', 'scissorEnabled=', 'pixelContribution=unmeasured'):
    assert field in renderer, field

source = r'''
#include "local_light_trace.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using HRESULT=long;using DWORD=unsigned long;using UINT=unsigned;
#define SUCCEEDED(x) ((x)>=0)
enum {D3DRS_SCISSORTESTENABLE};
struct D3DVIEWPORT9 {unsigned X,Y,Width,Height;float MinZ,MaxZ;};
struct RECT {long left,top,right,bottom;};
struct Device {
 unsigned reads=0;bool fail=false;
 HRESULT GetViewport(D3DVIEWPORT9* p){++reads;*p={1,2,864,558,0,1};return fail?-1:0;}
 HRESULT GetRenderState(unsigned,DWORD* p){++reads;*p=1;return fail?-1:0;}
 HRESULT GetScissorRect(RECT* p){++reads;*p={10,20,800,500};return fail?-1:0;}
};
static bool diagnostics=true;
namespace NorthlightDiagnostics {bool enabled(){return diagnostics;}}
namespace NorthlightQuality {bool renderProfile(bool profile){return profile;}}
struct World {
 Device device;Device* d=&device;NorthlightLocalLightTrace::Frame sourceTrace;
 NorthlightLocalLightSelection::Selection selection;
 struct {std::array<NorthlightLocalLights::Light,8> output{};unsigned outputCount=1;} particleLights;
 unsigned localSourceTraces=0,frames=127,diagnosticCapture=0;bool eq=true,quality=true,captureSampled=false;
 bool equipmentLightsOn(){return eq;}
 HELPER
 void begin(){const auto& localLights=selection;GATE}
};
int main(){
 World w;w.particleLights.output[0].sourceId=42;w.selection.count=1;w.selection.sources[0].sourceId=42;
 w.selection.color[0]={.1f,.2f,.5f,1};w.selection.fog[0][0]=.006f;
 const NorthlightLocalLightScissor::Rect rect{0,0,864,558};
 for(bool diag:{false,true})for(bool profile:{false,true})for(bool eq:{false,true})for(bool sample:{false,true})for(unsigned budget:{0u,31u,32u,1000u}){
  diagnostics=diag;w.quality=profile;w.eq=eq;w.captureSampled=sample;w.localSourceTraces=budget;w.frames=127;w.diagnosticCapture=0;w.begin();
  assert(w.sourceTrace.enabled==(diag&&profile&&eq&&(budget<32||sample)));
  w.frames=128;w.begin();assert(w.sourceTrace.enabled==(diag&&profile&&eq&&sample));
  w.diagnosticCapture=1;w.begin();assert(w.sourceTrace.enabled==(diag&&profile));
 }
 diagnostics=true;w.quality=w.eq=true;w.captureSampled=false;w.localSourceTraces=0;w.frames=127;w.diagnosticCapture=0;w.begin();
 w.localSourceBatch(false,8,8,rect,true,0);assert(w.device.reads==0);
 w.localSourceBatch(false,0,8,rect,true,0);assert(w.device.reads==3);
 const auto& a=w.sourceTrace.rows[0].directState;assert(a.known==7&&a.viewport[0]==1&&a.viewport[2]==864&&a.viewport[3]==558&&a.minZ==0&&a.maxZ==1&&a.scissorEnabled==1&&a.scissor.left==10&&a.scissor.bottom==500);
 w.device.fail=true;w.localSourceBatch(true,0,4,rect,true,0);assert(w.device.reads==6);
 const auto& b=w.sourceTrace.rows[0].fogState;assert(!b.known&&!b.viewport[2]&&!b.scissor.right&&!b.scissorEnabled);
 assert(w.sourceTrace.rows[0].directState.known==7);
 w.localSourceBatch(true,0,4,rect,false,0);assert(w.device.reads==6&&w.sourceTrace.rows[0].volume==NorthlightLocalLightTrace::Stage::Clipped);
 w.sourceTrace.enabled=false;w.localSourceBatch(false,0,8,rect,true,0);assert(w.device.reads==6);
 std::puts("Particle diagnostics: exact sampled getters, failures unknown, pass attribution, zero disabled work, late-session samples pass");
}
'''.replace('HELPER', helper).replace('GATE', gate)

with tempfile.TemporaryDirectory(prefix='northlight-particle-diagnostics-') as temporary:
    temporary = Path(temporary)
    control = temporary / 'state.cpp'
    control.write_text(source)
    for name, path in (('state', control), ('trace', fp.TESTS / 'test_local_light_trace.cpp')):
        for optimization in ('O0', 'O2'):
            executable = temporary / (name + '-' + optimization + '.exe')
            subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-' + optimization,
                            '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(), str(path),
                            '-o', str(executable)], env=fp.zig_env(), check=True)
            subprocess.run([str(executable)], check=True)
    for label, variant in (('late-sample', source.replace('||captureSampled', '')),
                           ('unsampled-getters', source.replace('if(!sourceTrace.wants(first,count))return;', ''))):
        assert variant != source
        control.write_text(variant)
        executable = temporary / (label + '.exe')
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                        *fp.test_include_flags(), str(control), '-o', str(executable)], env=fp.zig_env(), check=True)
        result = subprocess.run([str(executable)], capture_output=True)
        assert result.returncode != 0, label + ' negative control was not rejected'
    print('Original late-session silence and unconditional getter controls rejected')
