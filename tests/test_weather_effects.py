#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.198 (rain): NorthlightWeatherEffects (src/sky/weather_effects.h), the real header compiled natively.
All-zero state, Weather=0 and RainFog=0 give exactly the identity (gains 1, addends 0, wetness 0); rain vs snow (snow at 60 percent, no
wetness); the intensity floor (0.35); the coefficient table; RainFog=2 never goes negative or removes shadows altogether; non-finite input is dry. Native clang++, plain
-O2 and ASan/UBSan. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile

SRC=r'''
#include "weather_effects.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorthlightWeather;namespace WE=NorthlightWeatherEffects;
static State st(Kind k,float intensity,float blend,float wetness){State s;s.kind=k;s.intensity=intensity;s.blend=blend;s.wetness=wetness;return s;}
static bool identity(const WE::Frame& f){
    return f.fog==0&&f.rain==0&&f.wet==0&&f.hazeTauScale()==1.0f&&f.airExtinction()==0.0f&&f.shaftGain()==1.0f&&f.discGain()==1.0f&&f.shadowSoften()==0.0f
        &&f.lampFogGain()==1.0f&&f.ambientLift()==0.0f&&!f.any();}
static bool near(float a,float b){return std::fabs(a-b)<=1e-6f;}
int main(){
    {   // no weather: exact identity for every setting
        for(unsigned w:{0u,1u})for(unsigned fog:{0u,1u,2u})for(unsigned wet:{0u,1u,2u})assert(identity(WE::derive(State{},w,fog,wet)));
        assert(identity(WE::derive(st(Kind::None,1,1,0),1,2,2)));            /* a kind of None is dry whatever the numbers say */
        assert(identity(WE::derive(st(Kind::Sand,1,1,0),1,2,2)));            /* reserved, never acts */
    }
    {   // Weather=0: nothing reacts, in heavy rain with full wetness
        assert(identity(WE::derive(st(Kind::Rain,1,1,1),0,2,2)));assert(identity(WE::derive(st(Kind::Snow,1,1,0),0,2,2)));
    }
    {   // RainFog=0 and RainWetness=0 each cut only their part
        auto a=WE::derive(st(Kind::Rain,1,1,1),1,0,1);assert(a.fog==0&&a.rain==0&&a.hazeTauScale()==1.0f&&a.discGain()==1.0f&&a.wet==1.0f);
        auto b=WE::derive(st(Kind::Rain,1,1,1),1,1,0);assert(b.fog==1.0f&&b.wet==0.0f&&b.any());
    }
    {   // the coefficient table at full rain, RainFog=1, RainWetness=1
        auto f=WE::derive(st(Kind::Rain,1,1,1),1,1,1);
        assert(f.fog==1.0f&&f.rain==1.0f&&f.wet==1.0f);
        assert(near(f.hazeTauScale(),2.5f)&&near(f.airExtinction(),0.0025f)&&near(f.shaftGain(),0.25f)&&near(f.discGain(),0.15f)
            &&near(f.shadowSoften(),0.5f)&&near(f.lampFogGain(),1.5f)&&near(f.ambientLift(),0.25f));
        auto h=WE::derive(st(Kind::Rain,0.5f,0.5f,0.2f),1,1,1);const float lv=0.5f*(0.35f+0.65f*0.5f);assert(near(h.fog,lv)&&near(h.rain,lv)&&near(h.wet,0.2f)&&near(h.shaftGain(),1-0.75f*lv));
    }
    {   // intensity floor: faint rain (intensity 0) still acts at 35 percent; blend 0 stays exactly dry whatever the intensity
        auto f=WE::derive(st(Kind::Rain,0,1,0),1,1,1);assert(near(f.fog,0.35f)&&near(f.rain,0.35f)&&f.any());
        assert(near(WE::derive(st(Kind::Rain,0,0.5f,0),1,1,1).fog,0.175f)&&near(WE::derive(st(Kind::Snow,0,1,0),1,1,1).fog,0.6f*0.35f));
        for(float i:{0.f,0.5f,1.f})assert(identity(WE::derive(st(Kind::Rain,i,0,0),1,2,2))&&identity(WE::derive(st(Kind::Snow,i,0,0),1,2,2)));
    }
    {   // snow: fog-like effects at 60 percent, no GI ambient lift, never wet (even with leftover wetness)
        auto s=WE::derive(st(Kind::Snow,1,1,0.7f),1,1,1);
        assert(near(s.fog,0.6f)&&s.rain==0&&s.wet==0&&s.ambientLift()==0.0f&&near(s.discGain(),1-0.85f*0.6f)&&near(s.hazeTauScale(),1+1.5f*0.6f));
        assert(WE::derive(st(Kind::Snow,1,1,0),1,2,2).fog<=1.2f+1e-6f);
    }
    {   // wetness outlives the rain (kind None, blend 0) and is dried by RainWetness=0
        auto d=WE::derive(st(Kind::None,0,0,0.6f),1,1,1);assert(d.fog==0&&near(d.wet,0.6f)&&d.any()&&d.shaftGain()==1.0f&&d.discGain()==1.0f&&d.shadowSoften()==0.0f);
        assert(WE::derive(st(Kind::None,0,0,0.6f),1,1,0).wet==0.0f&&near(WE::derive(st(Kind::None,0,0,0.6f),1,1,2).wet,1.2f));
    }
    {   // RainFog=2 at full rain: gains stay in range
        auto f=WE::derive(st(Kind::Rain,1,1,1),1,2,2);
        assert(f.fog==2.0f&&f.shaftGain()==0.0f&&f.discGain()==0.0f&&near(f.shadowSoften(),0.95f)&&near(f.hazeTauScale(),4.0f)&&near(f.lampFogGain(),2.0f));
        assert(WE::derive(st(Kind::Rain,1,1,1),1,9,9).fog==2.0f); /* settings past the parser's range clamp */
    }
    {   // inputs outside 0..1 or non-finite are clamped or dry, never NaN
        const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
        assert(identity(WE::derive(st(Kind::Rain,nan,1,0),1,1,1))&&identity(WE::derive(st(Kind::Rain,1,inf,0),1,1,1))&&identity(WE::derive(st(Kind::Snow,1,nan,0),1,1,1)));
        assert(WE::derive(st(Kind::Rain,1,1,nan),1,1,1).wet==0.0f);
        auto f=WE::derive(st(Kind::Rain,5,3,2),1,1,1);assert(f.fog==1.0f&&f.wet==1.0f);
    }
    std::printf("PASS weather effects scalars\n");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    (Path(tmp)/'t.cpp').write_text(SRC)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/label
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(Path(tmp)/'t.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
