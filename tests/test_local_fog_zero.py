#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exact-zero lamp extinction skips the whole fog path, without dropping lights."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
cpp = out / 'local-fog-zero.cpp'
cpp.write_text(r'''
#include "local_light_selection.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main(){
    using namespace NorthlightLocalLightSelection;
    Selection s;
    for(unsigned count=0;count<=Slots;++count){
        s={};s.count=count;assert(!s.hasFog(count));
        for(unsigned i=0;i<count;++i){
            s.fog[i][0]=-0.f;assert(!s.hasFog(count));
            for(float value:{.001f,-1.f,std::numeric_limits<float>::denorm_min(),
                    std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
                s.fog[i][0]=value;assert(s.hasFog(count));assert(!s.hasFog(i));assert(s.hasFog(i+1));
            }
            s.fog[i][0]=0;
        }
        if(count<Slots){s.fog[count][0]=1;assert(!s.hasFog(Slots));}
    }
    s={};s.count=Slots+100;assert(!s.hasFog(Slots+100));s.fog.back()[0]=1;assert(s.hasFog(Slots+100));
    std::puts("LOCAL_FOG_ZERO_PASS exact_zero negative_zero partial_selection active_nan_inf_preserved");
}
''', encoding='utf-8')
exe = out / 'local-fog-zero.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                *fp.test_include_flags(), str(cpp), '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True)
renderer = fp.src('world_renderer.h').read_text(encoding='utf-8')
assert 'const bool localFogActive=localDirectCount&&debug==0&&localLights.hasFog(localDirectCount);' in renderer
assert 'const bool fogActive=effects.fog&&(regionalFogActive||localFogActive);' in renderer
start = renderer.index('if(localFogActive){')
end = renderer.index('}else{', start)
assert all(x in renderer[start:end] for x in ('"lamp fog accumulator clear"', '"LocalFogCombine"', '"local fog glow batch"'))
assert 'float3 lamp=LocalLightColor[i].rgb*(integral*LocalLightFog[i].x*nearGlow);' in (fp.SHADERS / 'world_effects.hlsl').read_text(encoding='utf-8')
