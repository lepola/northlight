#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Native tests of real switch/neutral-target code; no game, Wine or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json
import re
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
world = fp.src('world_renderer.h').read_text()


def block(start):
    a = world.index(start)
    b = world.index('{', a)
    depth = 1
    end = b + 1
    while depth:
        depth += (world[end] == '{') - (world[end] == '}')
        end += 1
    return world[a:end]


setter = block('    void setEffects(')
shadow_clear = block('        if(!effects.shadows&&!neutralShadowMaps)')
fog_clear = world.split('// Clear every disabled frame', 1)[1]
fog_clear = fog_clear[fog_clear.index('d->SetTexture'):fog_clear.index('\n        }')]
neutral_matrix = re.search(r'const float neutralMatrix\[16\]=\{[^;]+;', world)[0]

body = r'''
#include "effect_switches.h"
#include "quality_settings.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
using UINT=unsigned;
using HRESULT=int;
constexpr unsigned D3DCLEAR_TARGET=1;
struct D3DVIEWPORT9 { unsigned x,y,w,h;float nearZ,farZ; };
struct Surface { std::uint32_t color=0x12345678; };
struct Device {
    Surface* target=nullptr;
    unsigned clearCalls=0,targetCalls=0,failTarget=0,failClear=0;
    D3DVIEWPORT9 viewport{};
    void SetTexture(unsigned,void*){}
    void SetViewport(const D3DVIEWPORT9* p){viewport=*p;}
    HRESULT SetRenderTarget(unsigned,Surface* p){
        if(++targetCalls==failTarget)return -1;
        target=p;return 0;
    }
    HRESULT Clear(unsigned,void*,unsigned flags,std::uint32_t color,float,unsigned){
        assert(flags==D3DCLEAR_TARGET&&target);
        if(++clearCalls==failClear)return -1;
        target->color=color;return 0;
    }
};
struct Fixture {
    NorthlightEffectSwitches::Settings effects;
    NorthlightQuality::Settings quality; // 0.3.138 northlight-quality.ini; defaults: giPass is a pass-through
    bool temporalValid=true,sourceVisValid=true,shadowsComposited=true;
    bool shadowFrameReady=true,pointReady=true,neutralShadowMaps=false;
    bool cacheValid=true;
    Device device;Device* d=&device;
    Surface maps[4],fog;
    Surface* shadowSurface[4]={&maps[0],&maps[1],&maps[2],&maps[3]};
    Surface* fogSurface=&fog;
    UINT w=1920,h=1080;
    void invalidateShadowCache(){cacheValid=false;}
    bool check(HRESULT hr,const char*){return hr==0;}
    SETTER
    bool neutralShadows(){SHADOW_CLEAR return true;}
    bool neutralFog(){FOG_CLEAR return true;}
};
int main(){
    using namespace NorthlightEffectSwitches;
    // All combinations, key repeat, modifiers and focus transitions.
    for(unsigned mask=0;mask<=All;++mask){
        Hotkeys k;
        assert(k.poll(true,true,mask)==mask);
        assert(k.settings.gi==!(mask&GI));
        assert(k.settings.shadows==!(mask&Shadows));
        assert(k.settings.fog==!(mask&Fog));
        auto once=k.settings;
        for(int i=0;i<1000;++i)assert(k.poll(true,true,mask)==0&&k.settings==once);
        k.poll(true,true,0);assert(k.poll(true,true,mask)==mask);
        assert(k.settings==Settings{});
    }
    Hotkeys keys;
    assert(!keys.poll(false,true,All));
    assert(!keys.poll(true,true,All)&&keys.settings==Settings{});
    keys.poll(true,true,0);
    assert(!keys.poll(true,false,All));
    assert(!keys.poll(true,true,All)&&keys.settings==Settings{});
    keys.poll(true,true,0);
    assert(!keys.poll(true,true,8)); // no F11 bit
    assert(keys.poll(true,true,Fog)==Fog);
    assert(keys.poll(true,true,Fog|GI)==GI);
    assert(!keys.settings.fog&&!keys.settings.gi&&keys.settings.shadows);

    // Run the production transition code for each enabled/disabled combination.
    for(unsigned mask=0;mask<=All;++mask){
        Fixture f;Settings next{!(mask&GI),!(mask&Shadows),!(mask&Fog)};
        f.setEffects(next);
        assert(f.effects==next);
        assert(f.temporalValid==!mask&&f.sourceVisValid==!mask);
        assert(f.cacheValid==!(mask&Shadows));
        assert(f.shadowFrameReady==!(mask&Shadows));
        assert(f.pointReady==!(mask&Shadows));
        f.temporalValid=f.sourceVisValid=true;
        f.setEffects(next);assert(f.temporalValid&&f.sourceVisValid);
    }
    // GI=0 (northlight-quality.ini): F8 can never enable the GI pass; shadows/fog still switch.
    for(unsigned mask=0;mask<=All;++mask){
        Fixture g;g.quality.gi=0;g.effects.gi=false;Settings next{!(mask&GI),!(mask&Shadows),!(mask&Fog)};
        g.setEffects(next);
        assert(!g.effects.gi&&g.effects.shadows==next.shadows&&g.effects.fog==next.fog);
        assert(g.cacheValid==!(mask&Shadows));
        const bool onlyGI=(mask&~GI)==0; // GI toggles alone are no transition when GI=0
        assert(g.temporalValid==onlyGI&&g.sourceVisValid==onlyGI);
    }
    Fixture f;
    assert(f.neutralShadows()&&f.device.clearCalls==0);
    f.setEffects({true,false,true});
    assert(f.neutralShadows()&&f.neutralShadowMaps&&f.device.clearCalls==4);
    for(auto& s:f.maps)assert(s.color==0xffffffff);
    assert(f.neutralShadows()&&f.device.clearCalls==4); // cached neutral maps
    f.setEffects({true,true,true});
    assert(!f.neutralShadowMaps&&!f.temporalValid&&!f.cacheValid);
    // Reset/reallocation must force clear again even while still switched off.
    f.setEffects({true,false,true});assert(f.neutralShadows());
    f.neutralShadowMaps=false;
    for(auto& s:f.maps)s.color=0;
    assert(f.neutralShadows());for(auto& s:f.maps)assert(s.color==0xffffffff);
    // A failed target/clear cannot publish a partially cleared shadow set.
    for(unsigned fault=1;fault<=4;++fault)for(bool targetFault:{false,true}){
        Fixture e;e.setEffects({true,false,true});
        if(targetFault)e.device.failTarget=fault;else e.device.failClear=fault;
        assert(!e.neutralShadows()&&!e.neutralShadowMaps);
        e.device.failTarget=e.device.failClear=0;
        assert(e.neutralShadows()&&e.neutralShadowMaps);
        for(auto& s:e.maps)assert(s.color==0xffffffff);
    }
    // The real neutral clear retains scene RGB exactly: alpha must be 1, not 0.
    assert(f.neutralFog()&&f.fog.color==0xff000000);
    for(unsigned color:{0u,0x123456u,0xffffffu}){
        const float alpha=float(f.fog.color>>24)/255;
        assert(float(color)*alpha+float(f.fog.color&0xffffff)==float(color));
    }
    // Production shadow transform cannot produce slope-depth > 1 at PCF edges.
    NEUTRAL_MATRIX
    for(float x:{-1e8f,-192.f,0.f,192.f,1e8f})for(float y:{-1e8f,0.f,1e8f})
      for(float z:{-1e8f,0.f,1e8f}){
        float q[3];for(unsigned j=0;j<3;++j)
            q[j]=x*neutralMatrix[j]+y*neutralMatrix[4+j]+z*neutralMatrix[8+j]+neutralMatrix[12+j];
        assert(q[0]==0&&q[1]==0&&q[2]==.5f);
      }
    puts("PASS: 8 combinations, repeat/focus, histories, neutral shadow/fog and failed clears");
}
'''
for key, value in [('SETTER', setter), ('SHADOW_CLEAR', shadow_clear),
                   ('FOG_CLEAR', fog_clear), ('NEUTRAL_MATRIX', neutral_matrix)]:
    body = body.replace(key, value)

# Production wiring of the fog switch: the real key poll
# reaches setEffects(); fog off clears the fog buffer AND zeroes the horizon haze.
renderer = fp.src('renderer.cpp').read_text()
assert 'if(effectKeys.poll(focus,modifiers,componentKeys)){' in renderer
assert renderer.index('effectKeys.poll(') < renderer.index('if(world)world->setEffects(settings);')
assert 'farZ,hazeZone,effects.fog,celestialValid,hazeSun,sourceWeights[0],hazeLift,wx.hazeTauScale());' in world
assert world.index('d->SetPixelShaderConstantF(34,haze.haze,1);') < world.index('"world composite"')

with tempfile.TemporaryDirectory(prefix='fr-effect-switches-') as temp:
    folder = Path(temp)
    (folder / 'test.cpp').write_text(body)
    for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']):
        subprocess.run(['clang++', '-std=c++17', *flags, *fp.test_include_flags(),
                        str(folder / 'test.cpp'), '-o', str(folder / 'test')], check=True)
        subprocess.run([str(folder / 'test')], check=True)
print(json.dumps({'game_launched': False, 'native_and_sanitizers': 'PASS'}))
