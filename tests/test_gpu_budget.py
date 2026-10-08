#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.200 (gpu budget): NorthlightGpuBudget (src/core/gpu_budget.h) and NorthlightGpuFrameTimer (src/core/gpu_frame_timer.h), the real
headers compiled natively. The controller: off at budget 0, invalid readings ignored, one step down after ~0.5 s over the budget, one step up
only after ~2 s well under it, no oscillation on a scene at the edge (backoff), spikes absorbed; the level map is the full image at level 0.
The frame timer on the fake device of tests/support/gpu_profile: every GetData with flags 0, a full ring skips (never waits), disjoint or
nonmonotonic intervals dropped, failures disable it, queries released. Native clang++, plain -O2 and ASan/UBSan. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile

SRC=r'''
#include <d3d9.h>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
static std::vector<std::string> logs;
static void logf(const char* f,...){char b[2000];va_list a;va_start(a,f);vsnprintf(b,sizeof b,f,a);va_end(a);logs.emplace_back(b);}
#include "gpu_budget.h"
#include "gpu_frame_timer.h"
HRESULT IDirect3DDevice9::CreateQuery(D3DQUERYTYPE t,IDirect3DQuery9** out){++creates;if(failCreate){*out=nullptr;return E_FAIL;}*out=new IDirect3DQuery9{this,t};++live;return S_OK;}
HRESULT IDirect3DQuery9::Issue(DWORD flags){++d->issues;if(d->failIssue)return E_FAIL;assert(flags==D3DISSUE_BEGIN||flags==D3DISSUE_END);if(type!=D3DQUERYTYPE_TIMESTAMPDISJOINT)assert(flags==D3DISSUE_END);ended=flags==D3DISSUE_END;if(type==D3DQUERYTYPE_TIMESTAMP){d->time+=1000;stamp=d->time;}return S_OK;}
HRESULT IDirect3DQuery9::GetData(void* out,DWORD size,DWORD flags){assert(flags==0);++d->reads;if(d->failRead)return E_FAIL;if(!ended||!d->ready)return S_FALSE;
    if(type==D3DQUERYTYPE_TIMESTAMPDISJOINT){assert(size==4);BOOL result=d->disjoint;std::memcpy(out,&result,4);}else{assert(size==8);uint64_t result=type==D3DQUERYTYPE_TIMESTAMPFREQ?d->frequency:stamp;std::memcpy(out,&result,8);}return S_OK;}
unsigned IDirect3DQuery9::Release(){--d->live;delete this;return 0;}
namespace B=NorthlightGpuBudget;
static const float Dt=1.f/60;
// A scene: the GPU time per level (index = level). Runs `seconds` at 60 samples/s and returns the number of level changes.
static unsigned run(B::Controller& c,float budget,const float* ms,float seconds,unsigned* finalLevel=nullptr){
    const unsigned before=c.changes;for(int i=0;i<int(seconds*60);++i)c.update(budget,ms[c.level],Dt);
    if(finalLevel)*finalLevel=c.level;return c.changes-before;}
int main(){
    {   // level map: level 0 is exactly the full image
        assert(B::cloudSteps(0)==40&&B::cloudSteps(1)==32&&B::cloudSteps(2)==24&&B::cloudSteps(3)==24);
        assert(B::fogSteps(0)==48&&B::fogSteps(2)==48&&B::fogSteps(3)==40);
        for(unsigned limit=8;limit<=64;++limit){assert(B::lightLimit(limit,0)==limit&&B::lightLimit(limit,2)==limit&&B::lightLimit(limit,3)==(limit>16?16:limit));}
        assert(B::spacingDelta(128,40,40)==0.f&&B::spacingDelta(128,48,48)==0.f&&B::spacingDelta(128,0,40)==0.f);
        for(unsigned n:{24u,32u,40u}){const float spacing=std::fma(128.f,1.f/40,B::spacingDelta(128,n,40));assert(std::fabs(spacing-128.f/float(n))<1e-4f);}
        {const float spacing=std::fma(128.f,1.f/48,B::spacingDelta(128,40,48));assert(std::fabs(spacing-3.2f)<1e-4f);}
        assert(std::fma(128.f,1.f/40,0.f)==128.f*(1.f/40)); /* the shader's mad with a zero addend is the old product */
    }
    {   // off: budget 0 is level 0 whatever the readings, and forgets everything
        B::Controller c;for(int i=0;i<600;++i)assert(c.update(0,50,Dt)==0);assert(!c.primed&&c.smoothed==0);
        const float heavy[4]={9,9,9,9};run(c,4,heavy,5);assert(c.level==B::MaxLevel);assert(c.update(0,9,Dt)==0&&c.level==0&&!c.primed);
    }
    {   // invalid readings change nothing
        B::Controller c;c.update(4,3,Dt);const float s=c.smoothed;
        for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),0.f,-1.f,251.f,1e9f}){assert(c.update(4,bad,Dt)==0);assert(c.smoothed==s);}
        assert(c.update(4,3,std::numeric_limits<float>::quiet_NaN())==0&&c.smoothed==s); /* NaN dt: no time passes */
    }
    {   // under the budget: level 0 forever
        B::Controller c;const float light[4]={3.5f,3,2.5f,2};assert(run(c,4,light,120)==0&&c.level==0);
    }
    {   // over: one step after ~0.5 s, not before; then each further step needs the settle and another 0.5 s
        B::Controller c;const float heavy[4]={6,6,6,6};
        for(int i=0;i<27;++i)c.update(4,6,Dt); /* .45 s */
        assert(c.level==0);
        for(int i=0;i<6;++i)c.update(4,6,Dt);assert(c.level==1); /* ~.55 s */
        for(int i=0;i<40;++i)c.update(4,6,Dt);assert(c.level==1); /* settle .25 + .5 not yet over */
        run(c,4,heavy,5);assert(c.level==B::MaxLevel&&c.changes==3);
        run(c,4,heavy,30);assert(c.level==B::MaxLevel&&c.changes==3); /* clamps at MaxLevel */
    }
    {   // a single spike (or a few) is absorbed by the smoothing
        B::Controller c;for(int i=0;i<600;++i){c.update(4,(i%60==30)?20.f:2.f,Dt);}assert(c.level==0&&c.changes==0);
        for(int i=0;i<600;++i){c.update(4,(i%30<3)?12.f:2.f,Dt);}assert(c.level==0);
        for(int i=0;i<1200;++i){c.update(4,(i%120==60)?30.f:2.8f,(i%120==60)?.25f:Dt);}assert(c.level==0&&c.changes==0); /* a hitch reading after a long gap is clamped */
        B::Controller h;const float over[4]={30,30,30,30};run(h,4,over,1.f);assert(h.level>=1); /* a sustained overload still steps down */
    }
    {   // step up only after ~2 s well under (75 %); at 80 % of the budget the level stays
        B::Controller c;const float heavy[4]={6,6,6,6};run(c,4,heavy,1.5f);const unsigned high=c.level;assert(high>=1);
        const float edge[4]={3.2f,3.2f,3.2f,3.2f};assert(run(c,4,edge,60)==0&&c.level==high);
        const float calm[4]={2,2,2,2};
        B::Controller u=c;for(int i=0;i<int(1.8f*60);++i)u.update(4,2,Dt);assert(u.level==high); /* smoothing + 2 s hold */
        unsigned last=0;run(c,4,calm,30,&last);assert(last==0);
    }
    {   // a scene at the edge: level 0 over, level 1 just under (not well under): settles on 1, never switches back
        B::Controller c;const float scene[4]={4.6f,3.6f,3.f,2.6f};unsigned changes=run(c,4,scene,300);assert(c.level==1&&changes==1);
    }
    {   // the worst case: level 0 over, level 1 well under. The controller may try level 0 again, but the backoff doubles the wait each bounce
        B::Controller c;const float scene[4]={4.8f,2.8f,2.4f,2.f};const unsigned changes=run(c,4,scene,300);
        std::printf("edge scene: %u changes in 300 s, upHold %.0f s\n",changes,double(c.upHold));
        assert(changes<=24&&c.upHold==B::UpHoldMax); /* 2, 4, 8, 16, then one probe of level 0 every ~33 s */
        // the time spent over budget stays small: the frames at level 0 in a further two minutes
        unsigned atZero=0;for(int i=0;i<7200;++i){c.update(4,scene[c.level],Dt);atZero+=c.level==0;}std::printf("edge scene: %.1f %% of frames at level 0\n",100.*atZero/7200);assert(atZero<7200/20);
        // the scene gets lighter: the probe sticks, and after CalmSeconds the backoff is forgotten
        B::Controller k=c;const float calm[4]={2,2,2,2};run(k,4,calm,75);assert(k.level==0&&k.upHold==B::UpHoldBase&&!k.probing);
    }
    {   // late or gappy readings: a long gap counts as MaxDt, never a jump of several levels
        B::Controller c;c.update(4,9,Dt);c.update(4,9,10.f);assert(c.level<=1);
    }
    std::puts("PASS gpu budget controller and level map");
    // ---- frame timer ----
    IDirect3DDevice9 d;
    {   NorthlightGpuFrameTimer t(&d);double ms=-1;
        assert(!t.poll(ms)&&d.creates==0); /* nothing pending: no query, no read */
        assert(t.begin());t.end();assert(d.live==4);
        unsigned before=d.reads;assert(!t.poll(ms)&&d.reads-before==1&&ms==-1); /* not ready: one try, flags 0, pending */
        d.ready=true;assert(t.poll(ms)&&ms==1.0);assert(!t.poll(ms)); /* 1000 ticks at 1 MHz; consumed */
        // ring: 4 frames in flight, the 5th is skipped instead of waited for
        d.ready=false;for(int i=0;i<4;++i){assert(t.begin());t.end();}
        assert(!t.begin());t.end();before=d.reads;assert(!t.poll(ms)&&d.reads-before==4);
        d.ready=true;assert(t.poll(ms)&&ms==1.0);assert(t.begin());t.end();assert(d.live==16);
        // newest of several completed
        d.ready=false;assert(t.begin());d.time+=5000;t.end();assert(t.begin());d.time+=1000;t.end();d.ready=true;assert(t.poll(ms)&&ms==2.0);
        // disjoint and nonmonotonic intervals are dropped, the slot is freed
        assert(t.begin());t.end();d.disjoint=true;assert(!t.poll(ms));d.disjoint=false;assert(!t.poll(ms));
        d.frequency=0;assert(t.begin());t.end();assert(!t.poll(ms));d.frequency=1000000;
        // end without begin, begin twice: balanced, no stray pending slot
        t.end();assert(t.begin());assert(t.begin());t.end();assert(t.poll(ms)&&ms==1.0);
        t.reset();assert(d.live==0);
        // failures: creation, issue, read each disable the timer (logged once) and release everything
        d.failCreate=true;logs.clear();assert(!t.begin()&&t.disabled()&&d.live==0&&logs.size()==1&&logs[0].find("GPUBUDGET timer disabled: query creation")==0);
        const unsigned count=d.creates;assert(!t.begin()&&d.creates==count&&logs.size()==1);d.failCreate=false;
        t.reset();assert(!t.disabled());d.failIssue=true;assert(!t.begin()&&t.disabled()&&d.live==0);d.failIssue=false;
        t.reset();assert(t.begin());t.end();d.failRead=true;assert(!t.poll(ms)&&t.disabled()&&d.live==0);d.failRead=false;
        t.reset();assert(t.begin());t.end();
    }
    assert(d.live==0);NorthlightGpuFrameTimer null(nullptr);assert(!null.begin()&&null.disabled());double ms=0;assert(!null.poll(ms));null.end();null.reset();
    std::puts("PASS gpu frame timer: flags-0 reads, ring skip, newest result, dropped intervals, failure isolation, query release");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    (Path(tmp)/'t.cpp').write_text(SRC)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/label
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',str(fp.TESTS/'support'/'gpu_profile'),*fp.test_include_flags(),str(Path(tmp)/'t.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
