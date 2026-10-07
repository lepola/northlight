#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Lamps in direct sun: native tests of the real CPU policy (local_light_selection.h
sunlitCut, unchanged nightGain; clang++ plain and ASan/UBSan), a numeric mirror of the LocalDirect
sunlit factor (sun 30%, shadow/interior/night unchanged) and source/shader integration. Lamp fog and
the point-shadow lamp are unchanged. Not a GPU or appearance test; no game, Wine or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json
import subprocess
import tempfile
from pathlib import Path

NATIVE = r'''
#include "regional_fog.h"
#include "local_light_selection.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace NorthlightLocalLightSelection;
int main(){
    // K4 revised: no global daylight gain. The lamp gain is exactly 0.3.164's at every hour:
    // LocalDirect/lamp fog .9 x nightGain = .9 at noon, .72 at midnight.
    assert(.9f*nightGain(NorthlightRegionalFog::nightFactor(.5f))==.9f&&std::fabs(.9f*nightGain(NorthlightRegionalFog::nightFactor(0))-.72f)<1e-6f);
    for(unsigned minute=0;minute<1440;++minute){const float n=NorthlightRegionalFog::nightFactor(float(minute)/1440);
        assert(nightGain(n)==1.f-.20f*std::clamp(n,0.f,1.f));}
    // Sunlit cut (LocalDirect c52.z): .7 with the sun up and its visibility in the baseline; faded by
    // the sun weight (elevation) through dusk; 0 at night, without a sun pass, or with NaN.
    assert(std::fabs(sunlitCut(1,true)-.7f)<1e-6f&&sunlitCut(0,true)==0&&sunlitCut(1,false)==0&&sunlitCut(NAN,true)==0);
    float last=sunlitCut(1,true);
    for(int i=100;i>=0;--i){const float c=sunlitCut(i*.01f,true);assert(c<=last+1e-7f&&last-c<.0071f);last=c;}
    // Shader mirror: sunlit=saturate((a-(1-s))/s), factor=1-cut*sunlit, s = GridInfo.z = .85.
    auto factor=[](float alpha,float cut){const float s=.85f;const float lit=std::clamp((alpha-(1-s))/s,0.f,1.f);return 1-cut*lit;};
    const float day=sunlitCut(1,true);
    assert(std::fabs(factor(1,day)-SunlitGain)<1e-6f&&SunlitGain==.3f);   // direct sun: 30%
    assert(factor(.15f,day)==1);                                          // full sun shadow / interior: unchanged
    assert(factor(0,day)==1);                                             // beyond the far cascade (alpha 0): unchanged
    assert(factor(.575f,day)>SunlitGain&&factor(.575f,day)<1);            // penumbra in between
    // Night: the moon is the first pass (its visibility in the baseline), sun inactive -> no cut at any alpha.
    const float night=sunlitCut(0,false);
    for(float a:{0.f,.15f,.5f,1.f})assert(factor(a,night)==1);
    assert(factor(1,sunlitCut(.5f,true))>SunlitGain&&factor(1,sunlitCut(.5f,true))<1); // low sun: partial
    std::printf("{\"sun\":%.4f,\"shadow\":%.4f,\"penumbra\":%.4f,\"night\":%.4f,\"low_sun\":%.4f}\n",factor(1,day),factor(.15f,day),factor(.575f,day),factor(1,night),factor(1,sunlitCut(.5f,true)));
}
'''


def native():
    with tempfile.TemporaryDirectory(prefix='fr-lamp-daylight-') as temp:
        folder = Path(temp)
        (folder/'test.cpp').write_text(NATIVE)
        out = None
        for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']):
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, *fp.test_include_flags(),
                            str(folder/'test.cpp'), '-o', str(folder/'test')], check=True)
            out = json.loads(subprocess.run([str(folder/'test')], check=True, capture_output=True, text=True).stdout)
        return out


def main():
    values = native()
    shader = fp.src('world_effects.hlsl').read_text()
    cpu = fp.src('world_renderer.h').read_text()
    point = fp.src('world_point_rendering.inl').read_text()
    lighting = shader[shader.index('LightingOutput WorldLighting('):shader.index('float4 WorldGI(')]
    local = shader[shader.index('float4 LocalDirect('):shader.index('float3 removalScale(')]
    # WorldLighting keeps this pass's visibility in the (otherwise unread) baseline alpha.
    assert 'o.baseline=float4(max(old+AmbientLight.rgb,.15),lerp(visibility,shadow,SunDirection.w)*known);return o;' in lighting
    assert 'float3 cover=affinePoint(p,FarMatrix);' in lighting and 'float known=max(abs(cover.x),abs(cover.y))<=1&&cover.z>=0&&cover.z<=1?1:0;' in lighting
    for use in [l for l in shader.splitlines() if 'tex2Dlod(BaselineLighting' in l and 'sunlit=' not in l]:
        assert '.rgb' in use, use  # nothing else reads the baseline alpha
    assert 'float sunlit=saturate((tex2Dlod(BaselineLighting,float4(uv,0,0)).a-(1-GridInfo.z))/max(GridInfo.z,.001));' in local
    assert 'return float4(result*(LocalLightInfo.y*(1-LocalLightInfo.z*sunlit)),0);' in local
    # CPU: no global daylight gain (lamp fog c58 and the point lamp as in 0.3.164); c52.z per batch; s12 bound.
    assert 'const float lampGain=.9f*NorthlightLocalLightSelection::nightGain(lampNight);' in cpu and 'daylightGain' not in cpu and 'lampDaylight' not in cpu
    assert 'c[52][1]=.9f*lampGain;' in cpu and 'c[58][2]=10.f*lampGain*wx.lampFogGain();c[58][3]=.12f*lampGain*wx.lampFogGain();' in cpu
    assert 'NorthlightLocalLightSelection::sunlitCut(celestialValid?sourceWeights[0]:0.f,sourceActive[0]&&effects.shadows);' in cpu
    assert 'const float info[4]={float(batch.count),c[52][1],lampSunlitCut,0};' in cpu
    assert cpu.index('d->SetTexture(12,baselineLight); /* sun visibility') < cpu.index('"local direct light batch"')
    # Lamp fog and the point-shadow lamp are pinned unchanged against 0.3.164 (live 7ba4d92).
    assert 'std::memcpy(constants[8],pointSelected.diffuse,12);constants[8][3]=pointSelected.attenuationEnd;' in point
    assert 'const float lampFogInfo[4]={c[21][0],c[21][1],c[21][2],localLights.fogDistance};' in cpu
    result = dict(game_launched=False, gpu_tested=False, native_policy_passed=True, gains=values)
    (fp.output_dir()/'lamp-daylight-validation.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
