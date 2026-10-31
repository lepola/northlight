#include "local_light_trace.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace NorthlightLocalLightTrace;
int main(){
    NorthlightLocalLightSelection::Selection selection;
    std::array<NorthlightLocalLights::Light,11> published{};
    for(unsigned i=0;i<published.size();++i){published[i].sourceId=100+i;published[i].position[0]=float(i);published[i].diffuse[1]=.5f;}
    selection.count=20;
    for(unsigned i=0;i<selection.count;++i){selection.sources[i].sourceId=200+i;selection.position[i][0]=float(i);selection.color[i][2]=.25f;selection.fog[i][0]=.003f;}
    selection.sources[1].sourceId=103;selection.sources[8].sourceId=100;selection.sources[16].sourceId=104;
    const auto before=selection;const auto sources=published;
    Frame trace;trace.begin(true,published.data(),published.size(),selection);
    assert(trace.count==8&&trace.omitted==3&&trace.rows[0].slot==8&&trace.rows[3].slot==1&&trace.rows[4].slot==16);
    assert(trace.rows[1].direct==Stage::NotSelected&&trace.rows[0].direct==Stage::NotReached);
    assert(trace.rows[0].position==selection.position[8]&&trace.rows[0].color==selection.color[8]&&trace.rows[0].fog==selection.fog[8]);
    const Rect direct{0,2,30,40},fog{4,5,60,70};
    trace.batch(false,0,16,direct,true,0);
    assert(trace.rows[0].direct==Stage::Submitted&&trace.rows[3].direct==Stage::Submitted&&trace.rows[4].direct==Stage::NotReached);
    trace.batch(false,16,4,direct,true,std::int32_t(-2147467259));
    assert(trace.rows[4].direct==Stage::Failed&&trace.rows[4].directResult<0);
    trace.batch(true,8,4,fog,false,0);
    assert(trace.rows[0].volume==Stage::Clipped&&trace.rows[3].volume==Stage::NotReached&&trace.rows[0].fogRect.right==60);
    assert(trace.rows[0].directRect.left==0&&trace.rows[0].directRect.top==2);
    trace.batch(true,0,4,fog,true,0);assert(trace.rows[3].volume==Stage::Submitted);
    assert(trace.wants(8,4)&&!trace.wants(12,4)&&!trace.wants(0,0));
    Row::DrawState state;state.known=7;state.viewport[2]=864;state.viewport[3]=558;state.maxZ=1;state.scissorEnabled=1;state.scissor=direct;
    trace.batch(false,8,8,direct,true,0,state);
    assert(trace.rows[0].directState.known==7&&trace.rows[0].directState.viewport[2]==864&&trace.rows[0].directState.scissor.right==30);
    assert(trace.rows[0].fogState.known==0); // Per-pass observations cannot overwrite one another.
    assert(!std::strcmp(reason(trace.rows[0],false),"pixel-contribution-unmeasured"));
    trace.batch(true,8,4,fog,true,0);trace.rows[0].fog[0]=0;
    assert(!std::strcmp(reason(trace.rows[0],true),"zero-extinction"));
    trace.rows[0].color={};assert(!std::strcmp(reason(trace.rows[0],false),"zero-selected-rgb"));
    trace.rows[0].color[0]=NAN;assert(!std::strcmp(reason(trace.rows[0],false),"non-finite-selected-rgb"));
    trace.rows[0].color[0]=.1f;trace.rows[0].fog[0]=NAN;assert(!std::strcmp(reason(trace.rows[0],true),"non-finite-extinction"));
    trace.batch(false,8,8,direct,true,-1,state);assert(!std::strcmp(reason(trace.rows[0],false),"failed"));
    trace.batch(false,8,8,direct,false,0);assert(!std::strcmp(reason(trace.rows[0],false),"clipped")&&trace.rows[0].directState.known==0);
    assert(!std::memcmp(&before,&selection,sizeof selection)&&!std::memcmp(sources.data(),published.data(),sizeof published));
    trace.begin(false,published.data(),11,selection);trace.batch(false,0,16,direct,true,0);
    assert(!trace.enabled&&trace.count==0&&trace.omitted==0);
    assert(!trace.wants(0,136));
    trace.begin(true,nullptr,11,selection);assert(trace.count==0);
    trace.begin(true,published.data(),0,selection);assert(trace.count==0);
    std::puts("Bounded source identity, reordered slots, submit/clip/failure, exact inputs and disabled path pass");
}
