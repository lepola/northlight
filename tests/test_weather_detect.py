#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.198 (rain): NorthlightWeatherDetect::Detector (src/sky/weather_detect.h), the real header compiled natively,
and the real per-draw comparison extracted from renderer.cpp's drawHook over a mock mirror.
Signatures (format, aspect 1:16 / 1:2, widths 8/16/32), address reuse (a re-created address with another signature stops being a
candidate, generation bumps, hot cleared), volume/cube forget, reset, per-kind table slots (rain 2, snow 4; never-drawn first, lookalike flood), hot
rotation on every frame without draws, the tall-texture log, and the hook: stage 0 unknown / UI phase (applied) / no hot = no count, a match
accumulates primitives and draws. Native clang++, plain -O2 and ASan/UBSan. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile

r=fp.src('renderer.cpp').read_text()
hook=[l for l in r.split('\n') if l.strip().startswith('if(weatherDetect.hot&&mirrorState')]
assert len(hook)==1,'the draw hook comparison must exist exactly once'
rl=r.split('\n');i0=next(i for i,l in enumerate(rl) if 'template<class Draw> HRESULT rainBlendDraw(' in l);i1=next(i for i in range(i0,len(rl)) if rl[i]=='    }')
rainfn='\n'.join(rl[i0:i1+1])

SRC=r'''
#include "weather_detect.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace NorthlightWeatherDetect;using NorthlightWeather::Kind;
typedef unsigned UINT;typedef unsigned DWORD;typedef long HRESULT;
#define TRUE 1
#define SUCCEEDED(h) ((h)>=0)
enum D3DRENDERSTATETYPE{D3DRS_ALPHABLENDENABLE=27,D3DRS_SRCBLEND=19,D3DRS_DESTBLEND=20,D3DRS_BLENDOP=171,D3DRS_ZWRITEENABLE=14};
enum{D3DBLEND_SRCCOLOR=3,D3DBLEND_SRCALPHA=5,D3DBLEND_INVSRCALPHA=6,D3DBLEND_DESTCOLOR=9,D3DBLENDOP_ADD=1};
static std::vector<std::string> lines;
static void sink(const char* l){lines.push_back(l);}
static int A=21,X8=22,A1=25,A4=26,R5G6B5=23,DXT5=0x35545844;
static int cell[64];static const void* P(int i){return &cell[i];}
static unsigned count(const char* prefix){unsigned n=0;for(auto& l:lines)if(l.rfind(prefix,0)==0)++n;return n;}
// The hook: the line extracted from Device::drawHook over a mock mirror.
struct Mirror{bool textureKnown[16]={};void* textures[16]={};};
struct Sample{unsigned primitives=0,draws=0;};
struct Ext{DWORD rs[256]={};std::vector<std::pair<int,DWORD>> sets;
    HRESULT GetRenderState(D3DRENDERSTATETYPE t,DWORD* v){*v=rs[t];return 0;}
    HRESULT SetRenderState(D3DRENDERSTATETYPE t,DWORD v){rs[t]=v;sets.push_back({int(t),v});return 0;}};
struct World{bool on=true;bool rainBlendSetting()const{return on;}};
namespace NorthlightWeather{}
struct Hook{
    Mirror mirrorState;Detector weatherDetect;Sample weatherSample;bool applied=false,terrain=true,rainBoundary=false;Ext extObj;Ext* ext=&extObj;World worldObj;World* world=&worldObj;bool claimedSkip=false;
    template<class Draw> HRESULT blobFaintDraw(bool claimed,Draw draw){return claimed?0:draw();}
    unsigned blendAtDraw[4]={};unsigned drawn=0;
@RAINFN@
    HRESULT draw(UINT count){
        bool claimed=claimedSkip,rainBlend=false;
@HOOK@
        return rainBlendDraw(rainBlend,claimed,[&]{++drawn;blendAtDraw[0]=ext->rs[D3DRS_ALPHABLENDENABLE];blendAtDraw[1]=ext->rs[D3DRS_SRCBLEND];blendAtDraw[2]=ext->rs[D3DRS_DESTBLEND];blendAtDraw[3]=ext->rs[D3DRS_BLENDOP];return HRESULT(0);});
    }
    void bind(unsigned stage,int i,bool known=true){mirrorState.textures[stage]=const_cast<void*>(P(i));mirrorState.textureKnown[stage]=known;}
};
int main(){
    {   // classification: A8R8G8B8 (21) only, rain 1:16, snow 1:2, widths 8/16/32 only
        assert(classify(32,512,A)==Kind::Rain&&classify(16,256,A)==Kind::Rain&&classify(8,128,A)==Kind::Rain);
        assert(classify(32,64,A)==Kind::Snow&&classify(16,32,A)==Kind::Snow&&classify(8,16,A)==Kind::Snow);
        assert(classify(32,128,A)==Kind::None&&classify(16,64,A)==Kind::None&&classify(32,256,A)==Kind::None&&classify(32,32,A)==Kind::None&&classify(32,1024,A)==Kind::None); /* 1:4, 1:8, 1:1, old 1:32 */
        assert(classify(32,512,X8)==Kind::None&&classify(32,512,A1)==Kind::None&&classify(32,512,A4)==Kind::None&&classify(32,64,X8)==Kind::None&&classify(32,64,A1)==Kind::None&&classify(32,64,A4)==Kind::None&&classify(16,32,A4)==Kind::None);
        assert(classify(32,512,DXT5)==Kind::None&&classify(32,512,R5G6B5)==Kind::None&&classify(32,64,DXT5)==Kind::None&&classify(32,512,0)==Kind::None&&classify(32,512,20)==Kind::None&&classify(32,512,27)==Kind::None);
        assert(shapeOf(32,512)==Kind::Rain&&shapeOf(64,1024)==Kind::Rain&&shapeOf(1,16)==Kind::Rain&&shapeOf(32,64)==Kind::Snow&&shapeOf(64,128)==Kind::None&&shapeOf(32,128)==Kind::None&&shapeOf(0,0)==Kind::None);
        assert(classify(64,128,A)==Kind::None&&classify(33,66,A)==Kind::None&&classify(0,0,A)==Kind::None&&classify(32,0,A)==Kind::None);
        assert(classify(4,8,A)==Kind::None&&classify(24,48,A)==Kind::None&&classify(4,64,A)==Kind::None&&classify(64,1024,A)==Kind::None);
    }
    Detector d;d.sink=&sink;
    {   // candidates, generation, hot
        d.noteCreate(P(0),32,512,1,A);assert(d.count()==1&&d.hot==P(0)&&d.hotKind==Kind::Rain&&d.generation==1&&count("WEATHER candidate kind=rain 32x512")==1);
        d.noteCreate(P(1),32,64,1,A);assert(d.count()==2&&d.hot==P(0)&&d.generation==2&&count("WEATHER candidate kind=snow 32x64")==1);
        d.noteCreate(P(2),256,256,1,A);d.noteCreate(P(3),32,512,1,DXT5);d.noteCreate(nullptr,32,512,1,A);d.noteCreate(P(6),32,128,1,A);assert(d.count()==2&&d.generation==2);
    }
    {   // address reuse: P(0) re-created with another signature stops being a candidate; hot cleared
        d.noteCreate(P(0),256,256,1,A);assert(d.count()==1&&d.hot==nullptr&&d.hotKind==Kind::None&&d.generation==3&&!d.isCandidate(P(0))&&d.isCandidate(P(1)));
        d.rotate();assert(d.hot==P(1)&&d.hotKind==Kind::Snow);
        d.noteCreate(P(1),32,64,1,A);assert(d.count()==1&&d.isCandidate(P(1))&&d.hot==P(1)&&d.generation==5); /* removed (hot cleared), re-added: nothing was hot, so it is hot again */
    }
    {   // volume / cube: forget
        d.forget(P(1));assert(d.count()==0&&d.hot==nullptr);const unsigned g=d.generation;d.forget(P(1));d.forget(nullptr);assert(d.generation==g);
        d.noteCreate(P(4),32,512,1,A);assert(d.hot==P(4));d.forget(P(4));assert(d.count()==0&&d.hot==nullptr&&d.generation==g+2);
    }
    {   // reset
        d.noteCreate(P(5),32,512,1,A);d.reset();assert(d.count()==0&&d.hot==nullptr&&d.hotKind==Kind::None);d.rotate();assert(d.hot==nullptr);
    }
    {   // lookalike flood: ARGB textures of other aspects (1:4, 1:8, 1:1, DXT) never evict the rain or snow textures
        Detector o;o.noteCreate(P(10),32,512,1,A);o.noteCreate(P(11),32,512,1,A);o.noteCreate(P(12),32,64,1,A);
        const unsigned g=o.generation;
        for(int i=0;i<200;++i){o.noteCreate(P(13+i%40),32,128,1,A);o.noteCreate(P(13+i%40),16,64,1,A);o.noteCreate(P(13+i%40),32,32,1,A);o.noteCreate(P(13+i%40),32,512,1,DXT5);o.noteCreate(P(13+i%40),64,1024,1,A);}
        assert(o.count()==3&&o.overflows==0&&o.generation==g&&o.isCandidate(P(10))&&o.isCandidate(P(11))&&o.isCandidate(P(12)));
    }
    {   // 1:2 ARGB lookalikes (common) flood the snow slots only: rain is never evicted, the snow table stays at kSnowSlots
        Detector o;o.noteCreate(P(10),32,512,1,A);o.noteCreate(P(11),32,512,1,A);
        for(int i=0;i<100;++i)o.noteCreate(P(13+i%40),i&1?16:32,i&1?32:64,1,A);
        unsigned rain=0,snow=0;for(unsigned i=0;i<o.count();++i)(o.candidate(i).kind==Kind::Rain?rain:snow)++;
        assert(rain==2&&snow==kSnowSlots&&o.isCandidate(P(10))&&o.isCandidate(P(11))&&o.overflows==100-kSnowSlots);
    }
    {   // per-kind slots: a rain candidate replaces only rain, snow only snow; never-drawn first, else the oldest
        Detector o;o.noteCreate(P(10),32,512,1,A);o.noteCreate(P(11),32,512,1,A);
        for(int i=0;i<4;++i)o.noteCreate(P(12+i),32,64,1,A); /* snow P12..P15 */
        assert(o.count()==6&&o.overflows==0);
        // snow flood: all snow entries never drew, so the oldest goes each time; the rain pair stays
        o.noteCreate(P(16),32,64,1,A);assert(o.overflows==1&&!o.isCandidate(P(12))&&o.isCandidate(P(13))&&o.isCandidate(P(16))&&o.isCandidate(P(10))&&o.isCandidate(P(11)));
        // the oldest snow entry (P13) drew: a lookalike evicts the next never-drawn P14 instead
        while(o.hot!=P(13))o.rotate();o.endFrame(true);
        o.noteCreate(P(17),32,64,1,A);assert(o.overflows==2&&o.isCandidate(P(13))&&!o.isCandidate(P(14))&&o.isCandidate(P(17))&&o.isCandidate(P(10))&&o.isCandidate(P(11)));
        // all snow drew: the oldest of the kind goes
        for(unsigned k=0;k<o.count();++k)if(o.candidate(k).kind==Kind::Snow){while(o.hot!=o.candidate(k).raw)o.rotate();o.endFrame(true);}
        const void* oldest=nullptr;for(unsigned k=0;k<o.count();++k)if(o.candidate(k).kind==Kind::Snow){oldest=o.candidate(k).raw;break;}
        o.noteCreate(P(18),32,64,1,A);assert(!o.isCandidate(oldest)&&o.isCandidate(P(18)));
        // rain: P10 drew, P11 did not: a new rain candidate replaces P11
        while(o.hot!=P(10))o.rotate();o.endFrame(true);
        o.noteCreate(P(19),32,512,1,A);assert(o.isCandidate(P(10))&&!o.isCandidate(P(11))&&o.isCandidate(P(19))&&o.count()==6);
        // the hot candidate evicted: hot cleared
        Detector q;q.noteCreate(P(20),32,512,1,A);q.noteCreate(P(21),32,512,1,A);assert(q.hot==P(20));q.noteCreate(P(22),32,512,1,A);assert(!q.isCandidate(P(20))&&q.hot==P(22)&&q.count()==2);
    }
    {   // rotation: every candidate is hot within <= 4 frames, whether or not the tracker is active (endFrame(false) is all that matters)
        Detector o;for(int i=0;i<4;++i)o.noteCreate(P(30+i),32,i&1?64:512,1,A);bool seen[4]={};
        for(int f=0;f<4;++f){for(int i=0;i<4;++i)if(o.hot==P(30+i))seen[i]=true;o.endFrame(false);}
        assert(seen[0]&&seen[1]&&seen[2]&&seen[3]);o.endFrame(false);assert(o.hot==P(31)&&o.hotKind==Kind::Snow);
        o.setOff(true);assert(o.hot==nullptr);o.endFrame(false);assert(o.hot==nullptr);o.noteCreate(P(40),32,512,1,A);assert(o.hot==nullptr);
        o.setOff(false);o.endFrame(false);assert(o.hot!=nullptr);
    }
    {   // hot stays while it draws, leaves the frame it draws nothing
        Detector o;o.noteCreate(P(50),32,512,1,A);o.noteCreate(P(51),32,512,1,A);o.noteCreate(P(52),32,64,1,A);assert(o.hot==P(50));
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
    {   // shape log: every rain/snow aspect create in any format, capped per kind, independent of the tall log
        lines.clear();Detector t;t.sink=&sink;
        t.noteCreate(P(0),32,512,1,DXT5);t.noteCreate(P(1),32,512,1,X8);t.noteCreate(P(2),64,1024,1,A);t.noteCreate(P(3),32,64,1,DXT5);t.noteCreate(P(4),128,256,1,A);t.noteCreate(P(5),32,128,1,A);
        assert(count("WEATHER shape kind=rain w=32 h=512 fmt=894720068 levels=1 matched=0")==1&&count("WEATHER shape kind=rain w=32 h=512 fmt=22 levels=1 matched=0")==1);
        assert(count("WEATHER shape kind=rain w=64 h=1024 fmt=21 levels=1 matched=0")==1&&count("WEATHER shape kind=snow w=32 h=64 fmt=894720068 levels=1 matched=0")==1);
        assert(count("WEATHER shape kind=snow")==1&&count("WEATHER shape ")==4); /* 128x256 is 1:2 but wider than 32; 32x128 is neither */
        assert(count("WEATHER tall ")==4&&t.tallSeen==4); /* the 3 rain-aspect creates and 32x128 are tall (h>=4w) */
        lines.clear();Detector c;c.sink=&sink;
        for(int i=0;i<100;++i){c.noteCreate(P(i%60),16,256,1,DXT5);c.noteCreate(P(i%60),16,32,1,DXT5);}
        assert(count("WEATHER shape kind=rain")==kShapeLogs&&count("WEATHER shape kind=snow")==kShapeLogs&&c.rainShapeSeen==100&&c.snowShapeSeen==100);
        lines.clear();Detector k;k.sink=&sink; /* tall flood does not eat the shape logs */
        for(int i=0;i<20;++i)k.noteCreate(P(i),16,100+i,1,DXT5);k.noteCreate(P(30),32,512,1,A);
        assert(count("WEATHER tall ")==kTallLogs&&count("WEATHER shape kind=rain")==1);
    }
    {   // A4R4G4B4 / X8R8G8B8 / A1R5G5B5 lookalikes of the snow and rain shapes never enter the table nor churn it
        Detector o;o.noteCreate(P(10),32,64,1,A);
        for(int i=0;i<300;++i){o.noteCreate(P(11+i%40),32,64,1,A4);o.noteCreate(P(11+i%40),16,32,1,A1);o.noteCreate(P(11+i%40),32,512,1,X8);}
        assert(o.count()==1&&o.overflows==0&&o.isCandidate(P(10))&&o.generation==1);
    }
    {   // the draw hook
        Hook h;h.weatherDetect.noteCreate(P(50),32,512,1,A);h.weatherDetect.noteCreate(P(51),32,64,1,A);
        h.draw(100);assert(h.weatherSample.draws==0); /* nothing bound, textureKnown false */
        h.bind(0,50,false);h.draw(100);assert(h.weatherSample.draws==0);
        h.bind(0,50);h.draw(100);h.draw(40);assert(h.weatherSample.draws==2&&h.weatherSample.primitives==140);
        h.bind(1,50);h.bind(0,51);h.draw(7);assert(h.weatherSample.draws==2); /* stage 0 only; the other candidate is not hot */
        h.bind(0,50);h.applied=true;h.draw(100);assert(h.weatherSample.draws==3&&h.weatherSample.primitives==240); /* 0.3.199: after the boundary the rest of the rain still counts */
        h.applied=false;h.weatherSample={};h.weatherSample.draws=2;h.weatherSample.primitives=140;h.weatherDetect.rotate();h.bind(0,51);h.draw(9);assert(h.weatherSample.draws==3&&h.weatherSample.primitives==149);
        h.weatherDetect.setOff(true);h.draw(9);assert(h.weatherSample.draws==3); /* mirror inactive: hot is null */
        Hook none;none.bind(0,50);none.draw(10);assert(none.weatherSample.draws==0); /* no candidates: hot null */
    }
    {   // 0.3.199 (rain): the effect boundary flag - rain draw of a terrain frame before applied, with the setting; nothing else
        Hook h;h.weatherDetect.noteCreate(P(50),32,512,1,A);h.weatherDetect.noteCreate(P(51),32,64,1,A);
        h.bind(0,50);h.draw(10);assert(h.rainBoundary); /* first rain draw */
        h.rainBoundary=false;h.applied=true;h.draw(10);assert(!h.rainBoundary&&h.weatherSample.draws==2); /* after the boundary: counted, no second boundary */
        h.applied=false;h.terrain=false;h.draw(10);assert(!h.rainBoundary); /* no terrain this frame: UI fallback */
        h.terrain=true;h.world->on=false;h.draw(10);assert(!h.rainBoundary); /* RainBlend=0 / Weather=0: the UI boundary */
        h.world->on=true;h.bind(0,0);h.draw(10);assert(!h.rainBoundary); /* non-weather draw */
        {const unsigned before=h.weatherSample.draws;h.weatherDetect.rotate();h.bind(0,51);h.draw(10);assert(!h.rainBoundary&&h.weatherSample.draws==before+1);} /* snow: counted, no boundary change */
    }
    {   // 0.3.199 (rain): RainBlend - rain draws run with SrcAlpha/InvSrcAlpha/Add and the game's previous states return; snow, non-weather, setting off and claimed draws are untouched
        auto game=[](Hook& h){h.ext->rs[D3DRS_ALPHABLENDENABLE]=1;h.ext->rs[D3DRS_SRCBLEND]=D3DBLEND_DESTCOLOR;h.ext->rs[D3DRS_DESTBLEND]=D3DBLEND_SRCCOLOR;h.ext->rs[D3DRS_BLENDOP]=D3DBLENDOP_ADD;h.ext->rs[D3DRS_ZWRITEENABLE]=0;};
        Hook h;h.weatherDetect.noteCreate(P(50),32,512,1,A);game(h);h.bind(0,50);
        h.draw(10);assert(h.drawn==1&&h.blendAtDraw[0]==1&&h.blendAtDraw[1]==D3DBLEND_SRCALPHA&&h.blendAtDraw[2]==D3DBLEND_INVSRCALPHA&&h.blendAtDraw[3]==D3DBLENDOP_ADD);
        assert(h.ext->rs[D3DRS_ALPHABLENDENABLE]==1&&h.ext->rs[D3DRS_SRCBLEND]==D3DBLEND_DESTCOLOR&&h.ext->rs[D3DRS_DESTBLEND]==D3DBLEND_SRCCOLOR&&h.ext->rs[D3DRS_BLENDOP]==D3DBLENDOP_ADD&&h.ext->rs[D3DRS_ZWRITEENABLE]==0);
        assert(h.ext->sets.size()==4); /* only the two differing states, set and restored */
        h.ext->rs[D3DRS_ALPHABLENDENABLE]=0;h.draw(10);assert(h.blendAtDraw[0]==1&&h.ext->rs[D3DRS_ALPHABLENDENABLE]==0); /* blend off before: on during, off after */
        h.ext->sets.clear();h.world->on=false;h.draw(10);assert(h.ext->sets.empty()&&h.blendAtDraw[1]==D3DBLEND_DESTCOLOR); /* setting off / Weather=0 */
        h.world->on=true;h.claimedSkip=true;const unsigned n=h.drawn;h.draw(10);assert(h.drawn==n&&h.ext->sets.empty()); /* claimed: the game's draw does not run, nothing touched */
        h.claimedSkip=false;h.weatherDetect.noteCreate(P(51),32,64,1,A);h.weatherDetect.reset();h.weatherDetect.noteCreate(P(51),32,64,1,A);h.bind(0,51);h.draw(10);assert(h.weatherDetect.hotKind==Kind::Snow&&h.ext->sets.empty()&&h.blendAtDraw[1]==D3DBLEND_DESTCOLOR); /* snow untouched */
        h.bind(0,3);h.draw(10);assert(h.ext->sets.empty()); /* not the hot texture */
        Hook nw;nw.weatherDetect.noteCreate(P(50),32,512,1,A);game(nw);nw.bind(0,50);nw.world=nullptr;nw.draw(10);assert(nw.ext->sets.empty()); /* no world */
    }
    std::printf("PASS weather detect\n");
}
'''.replace('@HOOK@',hook[0]).replace('@RAINFN@',rainfn)
with tempfile.TemporaryDirectory() as tmp:
    (Path(tmp)/'t.cpp').write_text(SRC)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/label
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,*fp.test_include_flags(),str(Path(tmp)/'t.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
