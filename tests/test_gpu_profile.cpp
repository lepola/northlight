#include <d3d9.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
static std::vector<std::string> logs;
static void logf(const char* f,...){char b[2000];va_list a;va_start(a,f);vsnprintf(b,sizeof b,f,a);va_end(a);logs.emplace_back(b);}
#include "gpu_profile.h"
HRESULT IDirect3DDevice9::CreateQuery(D3DQUERYTYPE t,IDirect3DQuery9** out){++creates;if(failCreate){*out=nullptr;return E_FAIL;}*out=new IDirect3DQuery9{this,t};++live;return S_OK;}
HRESULT IDirect3DQuery9::Issue(DWORD flags){++d->issues;if(d->failIssue)return E_FAIL;assert(flags==D3DISSUE_BEGIN||flags==D3DISSUE_END);if(type!=D3DQUERYTYPE_TIMESTAMPDISJOINT)assert(flags==D3DISSUE_END);ended=flags==D3DISSUE_END;if(type==D3DQUERYTYPE_TIMESTAMP){d->time+=1000;stamp=d->time;}return S_OK;}
HRESULT IDirect3DQuery9::GetData(void* out,DWORD size,DWORD flags){assert(flags==0);++d->reads;if(d->failRead)return E_FAIL;if(!ended||!d->ready)return S_FALSE;
    if(type==D3DQUERYTYPE_TIMESTAMPDISJOINT){assert(size==4);BOOL result=d->disjoint;std::memcpy(out,&result,4);}else{assert(size==8);uint64_t result=type==D3DQUERYTYPE_TIMESTAMPFREQ?d->frequency:stamp;std::memcpy(out,&result,8);}return S_OK;}
unsigned IDirect3DQuery9::Release(){--d->live;delete this;return 0;}
static void findLog(const char* s){bool found=false;for(auto& l:logs)if(l.find(s)!=std::string::npos)found=true;assert(found);}
int main(){
    IDirect3DDevice9 d;
    {NorthlightGpuProfile p(&d);assert(!p.beginFrame(1)&&d.creates==0);assert(p.sampledFrame()==0);assert(p.beginFrame(120));assert(p.sampledFrame()==120);char label[]="AO";p.mark(label);label[0]='X';p.mark("worldComposite");p.endFrame();assert(p.sampledFrame()==0);unsigned before=d.reads;p.poll();assert(d.reads-before==1&&logs.empty());d.ready=true;p.poll();findLog("AO=1.000ms");findLog("worldComposite=1.000ms");findLog("total=3.000ms");assert(!p.beginFrame(120));
     assert(p.beginFrame(240));for(unsigned i=0;i<28;++i)p.mark("stage");p.endFrame();p.poll();findLog("marks_truncated=1"); /* 0.3.201: MaxMarks=24 */
     assert(p.beginFrame(360));d.disjoint=true;p.endFrame();p.poll();findLog("discarded: disjoint");d.disjoint=false;
     assert(p.beginFrame(480));d.frequency=0;p.endFrame();p.poll();findLog("zero timestamp frequency");d.frequency=1000000;
     p.reset();assert(d.live==0);d.ready=false;
     for(unsigned i=1;i<=6;++i){assert(p.beginFrame(i*120));p.mark("pass");p.endFrame();}
     assert(!p.beginFrame(840));before=d.reads;p.poll();assert(d.reads-before==6);assert(d.live==6*(24+2+2)); /* 6 slots x (MaxMarks+2 stamps + frequency + disjoint) */d.ready=true;p.poll();assert(p.beginFrame(960));p.mark("recovered");p.endFrame();p.poll();findLog("recovered=1.000ms");
     p.reset();assert(d.live==0);d.failCreate=true;assert(!p.beginFrame(1080));findLog("query creation");auto count=d.creates;assert(!p.beginFrame(1200)&&d.creates==count);p.mark("ignored");p.endFrame();p.poll();assert(d.live==0);
     d.failCreate=false;p.reset();assert(p.beginFrame(120));d.failRead=true;p.endFrame();p.poll();assert(d.live==0);findLog("disjoint read");
     d.failRead=false;p.reset();d.failIssue=true;assert(!p.beginFrame(120));assert(d.live==0);findLog("disjoint begin");d.failIssue=false;
    }
    logs.clear();{NorthlightGpuProfile p(&d);assert(p.beginFrame(120));for(unsigned i=0;i<19;++i)p.mark("stage");p.mark("WorldComposite");p.endFrame();p.poll();findLog("WorldComposite=1.000ms");for(const auto& line:logs)assert(line.find("marks_truncated")==std::string::npos);}
    assert(d.live==0);NorthlightGpuProfile null(nullptr);assert(!null.beginFrame(120));null.poll();null.reset();
    std::puts("PASS sample cadence, copied labels, interval timings, bounded pending polls, ring saturation/recovery, mark cap, invalid samples, failure isolation and query release");
}
