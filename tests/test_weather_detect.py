#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.198 (rain): NorthlightWeatherDetect::Detector (src/sky/weather_detect.h), the real header compiled natively,
and the real per-draw comparison extracted from renderer.cpp's drawHook over a mock mirror.
Signatures (format, aspect 1:16 / 1:32, widths 8/16/32), address reuse (a re-created address with another signature stops being a
candidate, generation bumps, hot cleared), volume/cube forget, reset, per-kind table slots (never-drawn first, lookalike flood), hot
rotation on every frame without draws, the tall-texture log, and the hook: stage 0 unknown / UI phase (applied) / no hot = no count, a match
accumulates primitives and draws. Native clang++, plain -O2 and ASan/UBSan. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile

r=fp.src('renderer.cpp').read_text()
hook=[l for l in r.split('\n') if l.strip().startswith('if(weatherDetect.hot&&!applied&&')]
assert len(hook)==1,'the draw hook comparison must exist exactly once'

SRC=r'''
#include "weather_detect.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace NorthlightWeatherDetect;using NorthlightWeather::Kind;
typedef unsigned UINT;
static std::vector<std::string> lines;
static void sink(const char* l){lines.push_back(l);}
static int A=21,X8=22,DXT5=0x35545844;
static int cell[64];static const void* P(int i){return &cell[i];}
static unsigned count(const char* prefix){unsigned n=0;for(auto& l:lines)if(l.rfind(prefix,0)==0)++n;return n;}
// The hook: the line extracted from Device::drawHook over a mock mirror.
struct Mirror{bool textureKnown[16]={};void* textures[16]={};};
struct Sample{unsigned primitives=0,draws=0;};
struct Hook{
    Mirror mirrorState;Detector weatherDetect;Sample weatherSample;bool applied=false;
    void draw(UINT count){
@HOOK@
    }
    void bind(unsigned stage,int i,bool known=true){mirrorState.textures[stage]=const_cast<void*>(P(i));mirrorState.textureKnown[stage]=known;}
};
int main(){
    {   // classification: ARGB only, rain 1:16, snow 1:32, widths 8/16/32 only
        assert(classify(32,512,A)==Kind::Rain&&classify(16,256,A)==Kind::Rain&&classify(8,128,A)==Kind::Rain);
        assert(classify(32,1024,A)==Kind::Snow&&classify(16,512,A)==Kind::Snow&&classify(8,256,A)==Kind::Snow);
        assert(classify(32,128,A)==Kind::None&&classify(16,64,A)==Kind::None&&classify(32,256,A)==Kind::None&&classify(32,32,A)==Kind::None); /* 1:4, 1:8, 1:1 */
        assert(classify(32,512,X8)==Kind::None&&classify(32,512,DXT5)==Kind::None&&classify(32,1024,X8)==Kind::None);
        assert(classify(64,1024,A)==Kind::None&&classify(33,528,A)==Kind::None&&classify(0,0,A)==Kind::None&&classify(32,0,A)==Kind::None);
        assert(classify(4,64,A)==Kind::None&&classify(24,384,A)==Kind::None&&classify(4,128,A)==Kind::None&&classify(64,2048,A)==Kind::None);
    }
    Detector d;d.sink=&sink;
    {   // candidates, generation, hot
        d.noteCreate(P(0),32,512,1,A);assert(d.count()==1&&d.hot==P(0)&&d.hotKind==Kind::Rain&&d.generation==1&&count("WEATHER candidate kind=rain 32x512")==1);
        d.noteCreate(P(1),32,1024,1,A);assert(d.count()==2&&d.hot==P(0)&&d.generation==2&&count("WEATHER candidate kind=snow 32x1024")==1);
        d.noteCreate(P(2),256,256,1,A);d.noteCreate(P(3),32,512,1,X8);d.noteCreate(nullptr,32,512,1,A);d.noteCreate(P(6),32,128,1,A);assert(d.count()==2&&d.generation==2);
    }
    {   // address reuse: P(0) re-created with another signature stops being a candidate; hot cleared
        d.noteCreate(P(0),256,256,1,A);assert(d.count()==1&&d.hot==nullptr&&d.hotKind==Kind::None&&d.generation==3&&!d.isCandidate(P(0))&&d.isCandidate(P(1)));
        d.rotate();assert(d.hot==P(1)&&d.hotKind==Kind::Snow);
        d.noteCreate(P(1),32,1024,1,A);assert(d.count()==1&&d.isCandidate(P(1))&&d.hot==P(1)&&d.generation==5); /* removed (hot cleared), re-added: nothing was hot, so it is hot again */
    }
    {   // volume / cube: forget
        d.forget(P(1));assert(d.count()==0&&d.hot==nullptr);const unsigned g=d.generation;d.forget(P(1));d.forget(nullptr);assert(d.generation==g);
        d.noteCreate(P(4),32,512,1,A);assert(d.hot==P(4));d.forget(P(4));assert(d.count()==0&&d.hot==nullptr&&d.generation==g+2);
    }
    {   // reset
        d.noteCreate(P(5),32,512,1,A);d.reset();assert(d.count()==0&&d.hot==nullptr&&d.hotKind==Kind::None);d.rotate();assert(d.hot==nullptr);
    }
    {   // lookalike flood: ARGB textures of other aspects (1:4, 1:8, 1:1, DXT) never evict the rain or snow textures
        Detector o;o.noteCreate(P(10),32,512,1,A);o.noteCreate(P(11),32,512,1,A);o.noteCreate(P(12),32,1024,1,A);
        const unsigned g=o.generation;
        for(int i=0;i<200;++i){o.noteCreate(P(13+i%40),32,128,1,A);o.noteCreate(P(13+i%40),16,64,1,A);o.noteCreate(P(13+i%40),32,32,1,A);o.noteCreate(P(13+i%40),32,512,1,DXT5);o.noteCreate(P(13+i%40),64,1024,1,A);}
        assert(o.count()==3&&o.overflows==0&&o.generation==g&&o.isCandidate(P(10))&&o.isCandidate(P(11))&&o.isCandidate(P(12)));
    }
    {   // per-kind slots: a rain candidate replaces only rain, snow only snow; never-drawn first, else the oldest
        Detector o;o.noteCreate(P(10),32,512,1,A);o.noteCreate(P(11),32,512,1,A);o.noteCreate(P(12),32,1024,1,A);o.noteCreate(P(13),32,1024,1,A);
        assert(o.count()==4&&o.overflows==0);
        // snow flood: both snow entries never drew, so the oldest of them goes each time; the rain pair stays
        o.noteCreate(P(14),32,1024,1,A);assert(o.overflows==1&&!o.isCandidate(P(12))&&o.isCandidate(P(13))&&o.isCandidate(P(14))&&o.isCandidate(P(10))&&o.isCandidate(P(11)));
        // the older snow entry (P13) drew: a lookalike evicts the never-drawn P14 instead
        while(o.hot!=P(13))o.rotate();o.endFrame(true);
        o.noteCreate(P(15),32,1024,1,A);assert(o.overflows==2&&o.isCandidate(P(13))&&!o.isCandidate(P(14))&&o.isCandidate(P(15))&&o.isCandidate(P(10))&&o.isCandidate(P(11)));
        // both drawn: the oldest of the kind goes
        while(o.hot!=P(15))o.rotate();o.endFrame(true);
        o.noteCreate(P(16),32,1024,1,A);assert(!o.isCandidate(P(13))&&o.isCandidate(P(15))&&o.isCandidate(P(16)));
        // rain: P10 drew, P11 did not: a new rain candidate replaces P11
        while(o.hot!=P(10))o.rotate();o.endFrame(true);
        o.noteCreate(P(17),32,512,1,A);assert(o.isCandidate(P(10))&&!o.isCandidate(P(11))&&o.isCandidate(P(17))&&o.count()==4);
        // the hot candidate evicted: hot cleared
        Detector q;q.noteCreate(P(20),32,512,1,A);q.noteCreate(P(21),32,512,1,A);assert(q.hot==P(20));q.noteCreate(P(22),32,512,1,A);assert(!q.isCandidate(P(20))&&q.hot==P(22)&&q.count()==2);
    }
    {   // rotation: every candidate is hot within <= 4 frames, whether or not the tracker is active (endFrame(false) is all that matters)
        Detector o;for(int i=0;i<4;++i)o.noteCreate(P(30+i),32,i&1?1024:512,1,A);bool seen[4]={};
        for(int f=0;f<4;++f){for(int i=0;i<4;++i)if(o.hot==P(30+i))seen[i]=true;o.endFrame(false);}
        assert(seen[0]&&seen[1]&&seen[2]&&seen[3]);o.endFrame(false);assert(o.hot==P(31)&&o.hotKind==Kind::Snow);
        o.setOff(true);assert(o.hot==nullptr);o.endFrame(false);assert(o.hot==nullptr);o.noteCreate(P(40),32,512,1,A);assert(o.hot==nullptr);
        o.setOff(false);o.endFrame(false);assert(o.hot!=nullptr);
    }
    {   // hot stays while it draws, leaves the frame it draws nothing
        Detector o;o.noteCreate(P(50),32,512,1,A);o.noteCreate(P(51),32,512,1,A);o.noteCreate(P(52),32,1024,1,A);assert(o.hot==P(50));
        for(int f=0;f<10;++f){o.endFrame(true);assert(o.hot==P(50)&&o.hotKind==Kind::Rain);}
        o.endFrame(false);assert(o.hot==P(51));
        o.endFrame(false);assert(o.hot==P(52)&&o.hotKind==Kind::Snow);
        o.endFrame(false);assert(o.hot==P(50)); /* wraps */
        Detector e;e.endFrame(true);e.endFrame(false);assert(e.hot==nullptr); /* no candidates */
    }
    {   // tall log: any format with h>=4w, the first 8 only
        lines.clear();Detector t;t.sink=&sink;
        for(int i=0;i<12;++i)t.noteCreate(P(i),16,64+i,1,DXT5);t.noteCreate(P(20),64,64,1,A);t.noteCreate(P(21),32,128,1,A);
        assert(t.tallSeen==13&&count("WEATHER tall ")==8&&count("WEATHER tall w=16 h=64 fmt=894720068 levels=1")==1&&t.count()==0);
        Detector n;n.noteCreate(P(1),32,512,1,A);assert(n.count()==1); /* no sink: silent */
    }
    {   // the draw hook
        Hook h;h.weatherDetect.noteCreate(P(50),32,512,1,A);h.weatherDetect.noteCreate(P(51),32,1024,1,A);
        h.draw(100);assert(h.weatherSample.draws==0); /* nothing bound, textureKnown false */
        h.bind(0,50,false);h.draw(100);assert(h.weatherSample.draws==0);
        h.bind(0,50);h.draw(100);h.draw(40);assert(h.weatherSample.draws==2&&h.weatherSample.primitives==140);
        h.bind(1,50);h.bind(0,51);h.draw(7);assert(h.weatherSample.draws==2); /* stage 0 only; the other candidate is not hot */
        h.bind(0,50);h.applied=true;h.draw(100);assert(h.weatherSample.draws==2); /* UI phase */
        h.applied=false;h.weatherDetect.rotate();h.bind(0,51);h.draw(9);assert(h.weatherSample.draws==3&&h.weatherSample.primitives==149);
        h.weatherDetect.setOff(true);h.draw(9);assert(h.weatherSample.draws==3); /* mirror inactive: hot is null */
        Hook none;none.bind(0,50);none.draw(10);assert(none.weatherSample.draws==0); /* no candidates: hot null */
    }
    std::printf("PASS weather detect\n");
}
'''.replace('@HOOK@',hook[0])
with tempfile.TemporaryDirectory() as tmp:
    (Path(tmp)/'t.cpp').write_text(SRC)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/label
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,*fp.test_include_flags(),str(Path(tmp)/'t.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
