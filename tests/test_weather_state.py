#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.198 (rain): NorthlightWeather::Tracker (src/sky/weather_state.h), the real header compiled natively.
Ramp in (~4 s) and out (~6 s) timing, the 2.5 s hold over gaps (loading screen, Alt+Tab), the 0.1 s dt clamp,
the intensity EMA, wetness rise/dry (rain only), a kind switch (old kind fully out first, no crossfade) and
never-seen = exactly zero. Native clang++, plain -O2 and ASan/UBSan. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile

SRC=r'''
#include "weather_state.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace NorthlightWeather;
static Sample rain(unsigned prims=3000,unsigned draws=4){Sample s;s.kind=Kind::Rain;s.primitives=prims;s.draws=draws;return s;}
static Sample snow(unsigned prims=3000){Sample s;s.kind=Kind::Snow;s.primitives=prims;s.draws=2;return s;}
static const Sample none;
static void run(Tracker& t,const Sample& s,float seconds,float dt=1.0f/60){for(float x=0;x<seconds;x+=dt)t.frame(s,dt);}
static bool near(float a,float b,float e){return std::fabs(a-b)<=e;}
int main(){
    {   // never seen: every output exactly zero, for any dt
        Tracker t;run(t,none,30);t.frame(none,5.0f);t.frame(none,-1.0f);
        const State& s=t.state();assert(s.kind==Kind::None&&s.intensity==0&&s.blend==0&&s.wetness==0&&s.secondsSinceSeen==0&&s.primitives==0);
        Sample sand;sand.kind=Kind::Sand;sand.primitives=999;sand.draws=3;t.frame(sand,0.1f);assert(t.state().kind==Kind::None&&t.state().blend==0); /* reserved */
        Sample zero=rain(0,0);t.frame(zero,0.1f);assert(t.state().kind==Kind::None);
    }
    {   // ramp in: smoothstep over ~4 s
        Tracker t;run(t,rain(),0.5f);const float b=t.state().blend;assert(t.state().kind==Kind::Rain&&b>0&&b<0.1f);
        run(t,rain(),1.5f);assert(near(t.state().blend,smoothstep01(2.0f/kBlendInSeconds),0.03f)); /* ~2 s in */
        run(t,rain(),2.5f);assert(t.state().blend==1.0f);
    }
    {   // hold: gaps shorter than 2.5 s keep everything; then ramp out in ~6 s
        Tracker t;run(t,rain(),6);const State full=t.state();assert(full.blend==1.0f&&full.intensity>0.3f);
        run(t,none,2.0f);assert(t.state().blend==1.0f&&t.state().kind==Kind::Rain&&near(t.state().intensity,full.intensity,1e-6f));
        run(t,rain(),0.5f);assert(t.state().secondsSinceSeen==0);
        run(t,none,kHoldSeconds-0.1f);assert(t.state().blend==1.0f);
        run(t,none,3.1f);const float mid=t.state().blend;assert(mid<1.0f&&mid>0.2f&&t.state().kind==Kind::Rain); /* ~0.5 s into... ~3 s of the 6 s out ramp */
        run(t,none,4.0f);assert(t.state().kind==Kind::None&&t.state().blend==0&&t.state().intensity==0);
    }
    {   // dt clamp: one huge dt (Alt+Tab, loading screen) is one 0.1 s step
        Tracker t;run(t,rain(),6);t.frame(none,60.0f);assert(t.state().kind==Kind::Rain&&t.state().blend==1.0f&&t.state().secondsSinceSeen<=kMaxDt+1e-6f);
        t.frame(rain(),600.0f);assert(t.state().blend==1.0f);
        Tracker u;u.frame(rain(),100.0f);assert(u.state().blend<=smoothstep01(kMaxDt/kBlendInSeconds)+1e-6f);
        u.frame(rain(),std::nanf(""));assert(u.state().blend==u.state().blend); /* NaN dt = 0 */
    }
    {   // loading screen with no draws shorter than the hold keeps wetness rising as before and the state intact
        Tracker t;run(t,rain(),10);const State a=t.state();for(int i=0;i<10;++i)t.frame(none,1.0f);assert(t.state().kind==Kind::Rain&&t.state().blend==a.blend);
    }
    {   // intensity EMA: first sample seeds, then tau 1.5 s towards primitives/6000, clamped to 1
        Tracker t;t.frame(rain(3000),1.0f/60);assert(near(t.state().intensity,0.5f,1e-4f));
        run(t,rain(6000),kIntensityTau,0.05f);const float i1=t.state().intensity;assert(near(i1,0.5f+0.5f*(1-std::exp(-1.0f)),0.03f));
        run(t,rain(60000),10);assert(t.state().intensity>0.99f&&t.state().intensity<=1.0f&&t.state().primitives==60000);
        run(t,rain(0,1),20);assert(t.state().intensity<0.01f);
    }
    {   // wetness: rain only, ~20 s up, ~90 s dry; it outlives kind; snow never wets
        Tracker t;run(t,rain(),10);assert(near(t.state().wetness,0.5f,0.03f));run(t,rain(),12);assert(t.state().wetness==1.0f);
        run(t,none,kHoldSeconds);run(t,none,45);assert(t.state().kind==Kind::None&&near(t.state().wetness,0.5f,0.1f)); /* dries ~45 s of 90, after the hold */
        run(t,none,60);assert(t.state().wetness==0.0f);
        Tracker s;run(s,snow(),30);assert(s.state().kind==Kind::Snow&&s.state().wetness==0.0f&&s.state().blend==1.0f);
    }
    {   // kind switch: rain fully out (6 s), only then snow in, never both
        Tracker t;run(t,rain(),8);bool snowSeen=false,rainAfterSnow=false;float lastRain=1;
        for(float x=0;x<14;x+=1.0f/60){t.frame(snow(),1.0f/60);const State& s=t.state();
            if(s.kind==Kind::Snow){snowSeen=true;assert(lastRain<=0.01f); /* snow starts only after rain is out */}
            if(s.kind==Kind::Rain){assert(!snowSeen);assert(s.blend<=lastRain+1e-6f);lastRain=s.blend;}
            if(snowSeen&&s.kind==Kind::Rain)rainAfterSnow=true;}
        assert(snowSeen&&!rainAfterSnow&&t.state().kind==Kind::Snow&&t.state().blend>0.5f);
        Tracker u;run(u,rain(),8);int fr=0;for(;u.state().kind==Kind::Rain&&fr<2000;++fr)u.frame(snow(),1.0f/60);assert(fr>=int(kBlendOutSeconds*60)-3&&fr<=int(kBlendOutSeconds*60)+3);
        assert(u.state().wetness<1.0f); /* snow does not keep it wet */
        // switching back before the old kind is gone cancels the pending switch
        Tracker v;run(v,rain(),8);run(v,snow(),1.0f);run(v,rain(),1.0f);assert(v.state().kind==Kind::Rain);
    }
    std::printf("PASS weather state tracker\n");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    (Path(tmp)/'t.cpp').write_text(SRC)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/label
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(Path(tmp)/'t.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
