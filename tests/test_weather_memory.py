#!/usr/bin/env python3
# northlight-test: requires=cxx
"""NorthlightWeatherMemory (src/sky/weather_memory.h) and the Tracker's memory path (src/sky/weather_state.h), the real headers compiled natively
against a fake client memory: pointer and object double reads, pointer/field validation, kind order (rain, snow, sand), the intensity mapping, the
code proofs (every proof's bytes served from a fake image, one flipped byte rejects), the texture-detector cross-check, and the Tracker following
the memory without a hold (kind incl. sand, intensity as given, ramp out when the weather ends). Plain and ASan/UBSan. No game, no device."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile

SRC=r'''
#include "weather_memory.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace NorthlightWeatherMemory;
using NorthlightWeather::Tracker;using NorthlightWeather::Sample;using NorthlightWeather::State;

struct Memory{
    std::map<std::uintptr_t,unsigned char> bytes;unsigned reads=0;bool tearObject=false;std::uintptr_t torn=0;
    void put(std::uintptr_t a,const void* p,std::size_t n){for(std::size_t i=0;i<n;++i)bytes[a+i]=static_cast<const unsigned char*>(p)[i];}
    void put32(std::uintptr_t a,std::uint32_t v){put(a,&v,4);}
    void putf(std::uintptr_t a,float v){put(a,&v,4);}
    bool read(std::uintptr_t a,void* out,std::size_t n){
        ++reads;for(std::size_t i=0;i<n;++i){auto it=bytes.find(a+i);if(it==bytes.end())return false;static_cast<unsigned char*>(out)[i]=it->second;}
        if(tearObject&&a==torn&&n==kObjectBytes)++bytes[a+kIntensityAt]; /* the game thread moved the intensity between our two reads */
        return true;
    }
    void code(){std::size_t n;const CodeProof* p=proofs(n);for(std::size_t i=0;i<n;++i)put(p[i].address,p[i].bytes,p[i].size);}
    // A Weather object at `at`, zeroed, with the given sub-objects (0 = none) and intensities.
    void weather(std::uintptr_t at,std::uint32_t rain,std::uint32_t snow,std::uint32_t sand,float storm,float fog=1,float density=1){
        unsigned char zero[kObjectBytes]={};put(at,zero,sizeof zero);put32(kPointer,std::uint32_t(at));
        put32(at+kRainAt,rain);put32(at+kSnowAt,snow);put32(at+kSandAt,sand);putf(at+kIntensityAt,storm);putf(at+kFogAt,fog);putf(at+kDensityAt,density);
    }
};
static bool near(float a,float b,float e=1e-5f){return std::fabs(a-b)<=e;}
int main(){
    const std::uintptr_t obj=0x2a40000;
    {   // kinds in GetType order, intensity (storm - .25) * 4/3 clamped
        Memory m;auto rd=[&](std::uintptr_t a,void* o,std::size_t n){return m.read(a,o,n);};Reading r;
        m.weather(obj,0x2b00000,0,0,0.85f);assert(readState(rd,r)&&r.valid&&r.kind==Kind::Rain&&near(r.intensity,0.8f)&&near(r.stormIntensity,0.85f)&&near(r.density,1)&&near(r.fog,1));
        m.weather(obj,0,0x2b00000,0,1.0f);assert(readState(rd,r)&&r.kind==Kind::Snow&&near(r.intensity,1.0f));
        m.weather(obj,0,0,0x2b00000,0.5f);assert(readState(rd,r)&&r.kind==Kind::Sand&&near(r.intensity,.25f/.75f));
        m.weather(obj,0x2b00000,0x2b00010,0x2b00020,0.5f);assert(readState(rd,r)&&r.kind==Kind::Rain); /* rain wins, as in GetType */
        m.weather(obj,0,0,0,0.9f);assert(readState(rd,r)&&r.kind==Kind::None&&r.valid); /* no sub-object: clear, a valid reading */
        m.weather(obj,0x2b00000,0,0,0.2f);assert(readState(rd,r)&&r.kind==Kind::Rain&&r.intensity==0); /* below .25: the game draws nothing */
        m.weather(obj,0x2b00000,0,0,1.0f,0.5f,0.25f);assert(readState(rd,r)&&near(r.fog,0.5f)&&near(r.density,0.25f));
    }
    {   // double reads and validation: a torn object, a moving pointer, implausible pointers and fields leave the output untouched
        Memory m;auto rd=[&](std::uintptr_t a,void* o,std::size_t n){return m.read(a,o,n);};Reading keep;keep.kind=Kind::Snow;keep.intensity=.5f;
        m.weather(obj,0x2b00000,0,0,0.85f);m.tearObject=true;m.torn=obj;Reading r=keep;assert(!readState(rd,r)&&r.kind==Kind::Snow&&r.intensity==.5f);m.tearObject=false;
        for(std::uint32_t bad:{0u,2u,0x1000u,0x80000000u,0xfffffff0u,0x2a40002u}){m.weather(obj,0x2b00000,0,0,0.85f);m.put32(kPointer,bad);r=keep;assert(!readState(rd,r)&&r.kind==Kind::Snow);}
        for(std::uint32_t bad:{1u,0x1000u,0x7fffffffu}){m.weather(obj,bad,0,0,0.85f);r=keep;assert(!readState(rd,r));m.weather(obj,0,0,bad,0.85f);assert(!readState(rd,r));}
        for(float bad:{std::nanf(""),-1.f,1.5f,INFINITY}){m.weather(obj,0x2b00000,0,0,bad);r=keep;assert(!readState(rd,r));m.weather(obj,0x2b00000,0,0,.5f,bad,1);assert(!readState(rd,r));m.weather(obj,0x2b00000,0,0,.5f,1,bad);assert(!readState(rd,r));}
        m.weather(obj,0x2b00000,0,0,0.85f);r=keep;m.bytes.erase(obj+0x100);assert(!readState(rd,r)&&r.kind==Kind::Snow); /* an unreadable byte fails the read */
        Memory empty;auto re=[&](std::uintptr_t a,void* o,std::size_t n){return empty.read(a,o,n);};r=keep;assert(!readState(re,r));
        // the cost: 2 pointer reads, 2 object reads
        Memory c;c.weather(obj,0x2b00000,0,0,.5f);auto rc=[&](std::uintptr_t a,void* o,std::size_t n){return c.read(a,o,n);};readState(rc,r);assert(c.reads==4);
    }
    {   // code proofs: every proof served from a fake image; a flipped byte, a missing page or a changed constant disables the source
        Memory m;m.code();auto rd=[&](std::uintptr_t a,void* o,std::size_t n){return m.read(a,o,n);};
        std::size_t n;const CodeProof* p=proofs(n);assert(n==5&&p[0].address==0x783b60&&p[1].address==0x4f8645&&p[2].address==0x7848ba);
        for(std::size_t i=0;i<n;++i)assert(p[i].size<=64);
        assert(proven(rd));
        for(std::size_t i=0;i<n;++i){Memory x;x.code();x.bytes[p[i].address+p[i].size/2]^=1;auto rx=[&](std::uintptr_t a,void* o,std::size_t k){return x.read(a,o,k);};assert(!proven(rx));
            Memory y;y.code();for(std::size_t k=0;k<p[i].size;++k)y.bytes.erase(p[i].address+k);auto ry=[&](std::uintptr_t a,void* o,std::size_t k){return y.read(a,o,k);};assert(!proven(ry));}
        // Source: proofs once, then a reading per call; without proofs nothing is read from the object
        m.weather(obj,0x2b00000,0,0,0.85f);Source s;Reading r=s.frame(rd);assert(r.valid&&r.kind==Kind::Rain&&s.proofsOk()&&s.reads==1);
        m.bytes[0x783b60]^=1;r=s.frame(rd);assert(r.valid); /* the proofs ran once: code does not change under a running client */
        Source bad;r=bad.frame(rd);assert(!r.valid&&!bad.proofsOk()&&bad.skipped==1&&bad.reads==0);
        r=bad.frame(rd);assert(!r.valid&&bad.skipped==2);
        Memory noObject;noObject.code();auto rn=[&](std::uintptr_t a,void* o,std::size_t k){return noObject.read(a,o,k);};Source late;r=late.frame(rn);assert(!r.valid&&late.proofsOk()&&late.failures==1);
        noObject.weather(obj,0,0x2b00000,0,0.6f);r=late.frame(rn);assert(r.valid&&r.kind==Kind::Snow&&late.reads==1); /* the object appears later (login): valid from then on */
    }
    {   // the cross-check: a lasting disagreement is reported once per sort; a short one, an invalid reading and sand never
        CrossCheck c;using R=CrossCheck::Result;
        for(unsigned i=0;i<10000;++i)assert(c.frame(true,Kind::Rain,Kind::Rain)==R::Agree&&c.frame(true,Kind::None,Kind::None)==R::Agree);
        unsigned at=0;R got=R::Agree;for(unsigned i=1;i<=CrossCheck::kKindFrames&&got==R::Agree;++i){got=c.frame(true,Kind::Rain,Kind::Snow);at=i;}assert(got==R::Kind&&at==CrossCheck::kKindFrames);
        for(unsigned i=0;i<5000;++i)assert(c.frame(true,Kind::Rain,Kind::Snow)==R::Agree); /* once */
        assert(c.frame(true,Kind::Rain,Kind::Rain)==R::Agree);
        for(unsigned i=0;i<CrossCheck::kKindFrames-1;++i)assert(c.frame(true,Kind::Rain,Kind::Snow)==R::Agree);assert(c.frame(true,Kind::Rain,Kind::Rain)==R::Agree); /* a short run resets */
        got=R::Agree;for(unsigned i=1;i<=CrossCheck::kOnlyFrames&&got==R::Agree;++i){got=c.frame(true,Kind::Rain,Kind::None);at=i;}assert(got==R::MemoryOnly&&at==CrossCheck::kOnlyFrames);
        got=R::Agree;for(unsigned i=1;i<=CrossCheck::kOnlyFrames&&got==R::Agree;++i){got=c.frame(true,Kind::None,Kind::Rain);at=i;}assert(got==R::TextureOnly&&at==CrossCheck::kOnlyFrames);
        CrossCheck d;for(unsigned i=0;i<5000;++i)assert(d.frame(false,Kind::Rain,Kind::None)==R::Agree&&d.frame(true,Kind::Sand,Kind::None)==R::Agree); /* invalid memory, sand: never compared */
        CrossCheck e;for(unsigned i=0;i<CrossCheck::kOnlyFrames-1;++i)e.frame(true,Kind::Rain,Kind::None);e.frame(false,Kind::Rain,Kind::None);for(unsigned i=0;i<CrossCheck::kOnlyFrames-1;++i)assert(e.frame(true,Kind::Rain,Kind::None)==R::Agree); /* an invalid frame resets the run */
    }
    {   // A valid zone reading stays primary even with no visible sky or native particle draws.
        assert(counts(true,true)&&!counts(true,false)&&!counts(false,true)&&!counts(false,false));
        for(Kind kind:{Kind::Rain,Kind::Snow,Kind::Sand}){
            Sample weather;weather.memory=counts(true,true);weather.memoryKind=kind;weather.memoryIntensity=.7f;
            Tracker t;for(unsigned frame=0;frame<1800;++frame)t.frame(weather,1.f/60);
            assert(t.state().kind==kind&&t.state().blend==1.f&&near(t.state().intensity,.7f)&&t.state().secondsSinceSeen==0);
            weather.memoryKind=Kind::None;weather.memoryIntensity=0;
            for(unsigned frame=0;frame<600;++frame)t.frame(weather,1.f/60);
            assert(t.state().kind==Kind::None&&t.state().blend==0);
        }
    }
    {   // Tracker, memory path: kind incl. sand, the game's intensity as given, no hold, the ramp in and out
        auto mem=[](Kind k,float i){Sample s;s.memory=true;s.memoryKind=k;s.memoryIntensity=i;return s;};
        auto run=[](Tracker& t,const Sample& s,float seconds){for(float x=0;x<seconds;x+=1.f/60)t.frame(s,1.f/60);};
        Tracker t;run(t,mem(Kind::Rain,0.6f),8);assert(t.state().kind==Kind::Rain&&t.state().blend==1.f&&near(t.state().intensity,0.6f));
        t.frame(mem(Kind::Rain,0.9f),1.f/60);assert(near(t.state().intensity,0.9f)); /* no EMA: the game's value is already smoothed */
        run(t,mem(Kind::None,0),1.0f);assert(t.state().kind==Kind::Rain&&t.state().blend<1.f&&t.state().blend>0.5f); /* ramps out at once, no 2.5 s hold */
        run(t,mem(Kind::None,0),6.f);assert(t.state().kind==Kind::None&&t.state().blend==0&&t.state().intensity==0);
        Tracker u;run(u,mem(Kind::Sand,0.5f),8);assert(u.state().kind==Kind::Sand&&u.state().blend==1.f); /* sand is tracked (weather_effects ignores it) */
        Tracker v;run(v,mem(Kind::Rain,0.f),8);assert(v.state().kind==Kind::None&&v.state().blend==0); /* an intensity of 0 is no weather */
        Tracker w;run(w,mem(Kind::Rain,0.5f),8);run(w,mem(Kind::Snow,0.5f),14);assert(w.state().kind==Kind::Snow); /* a kind switch ramps the old kind out first */
        // memory beats the draws; invalid memory falls back to the draws exactly as before
        Sample both=mem(Kind::Snow,0.4f);both.kind=Kind::Rain;both.primitives=42000;both.draws=3;Tracker x;run(x,both,8);assert(x.state().kind==Kind::Snow&&near(x.state().intensity,0.4f)&&x.state().primitives==0);
        Sample draws;draws.kind=Kind::Rain;draws.primitives=21000;draws.draws=3;Tracker y;run(y,draws,8);assert(y.state().kind==Kind::Rain&&near(y.state().intensity,0.5f,1e-3f)&&y.state().primitives==21000);
        draws.memory=false;draws.memoryKind=Kind::Snow;draws.memoryIntensity=1; /* ignored unless valid */
        Tracker z;run(z,draws,8);assert(z.state().kind==Kind::Rain);
        Sample nanI=mem(Kind::Rain,std::nanf(""));Tracker q;run(q,nanI,2);assert(q.state().kind==Kind::None); /* a non-finite intensity is 0 */
    }
    std::puts("PASS weather memory reader: kinds, intensity, double reads, validation, proofs, cross-check, tracker memory path");
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-weather-memory-') as tmp:
    source=Path(tmp)/'test.cpp';source.write_text(SRC)
    for name,flags in (('plain',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])):
        binary=str(Path(tmp)/name)
        subprocess.run(['clang++','-std=c++17',*flags,*fp.test_include_flags(),str(source),'-o',binary],check=True)
        out=subprocess.check_output([binary],text=True);print(name,out.strip())
print('PASS test_weather_memory')
