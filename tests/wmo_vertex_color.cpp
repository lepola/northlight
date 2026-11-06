#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include "wmo_vertex_color.h"

namespace P=NorthlightWmoVertexColor;
namespace A=NorthlightActorDeformation;
using namespace NorthlightWaterShaderPatch;
using Words=std::vector<Word>;

// Authored arithmetic deliberately shares partial temporaries between colour,
// fog and UVs. Removing colour must retain all other output dependencies.
static Words shader(){
    return {0xfffe0300,
        0x0200001f,0x80000000,dst(1,0),
        0x0200001f,0x80000003,dst(1,1),
        0x0200001f,0x8000000a,dst(1,2),
        0x0200001f,0x80000005,dst(1,3),
        0x0200001f,0x80000000,dst(6,0),
        0x0200001f,0x8000000a,dst(6,1),
        0x0200001f,0x80000005,dst(6,2,3),
        0x0200001f,0x8000000b,dst(6,3,1),
        0x02000001,dst(0,0),src(1,0),
        0x03000005,dst(0,4),src(1,0),src(1,0),
        0x02000001,dst(0,1,1),src(1,0,0),
        0x02000001,dst(0,1,2),src(1,0,0x55),
        0x02000001,dst(0,1,4),src(1,1,0xaa),
        0x02000001,dst(0,1,8),src(1,1,0),
        0x02000001,dst(0,6,1),src(1,0,0),
        0x02000001,dst(0,6,2),src(1,0,0x55),
        0x02000001,dst(0,2,3),src(0,6),
        0x02000001,dst(6,0),src(0,0),
        0x02000001,dst(6,2,1),src(0,2,0),
        0x03000020,dst(0,3,1),src(0,1,0xaa),src(0,1,0xff),
        0x02000001,dst(6,2,2),src(0,3,0),
        0x03000008,dst(0,3,1),src(0,1),src(0,1),
        0x02000001,dst(6,3,1),src(0,3,0),
        0x03000008,dst(0,5,1),src(1,1),src(1,1),
        0x03000005,dst(0,4,7),src(0,4),src(0,5,0),
        0x04000004,dst(6,1,7)|0x00100000,src(1,2),src(0,4),src(2,10),
        0x02000001,dst(6,1,8)|0x00100000,src(1,2),65535};
}
static A::Four evaluate(const Words& code,const A::Four* input,const float* constants,unsigned usage,unsigned mask){
    // The production CPU evaluator selects colour/UV semantics. Relabel the
    // requested output only; all executable instructions remain unchanged.
    auto selected=code;
    if(usage!=10)for(size_t at=1;at<selected.size()&&selected[at]!=65535;){const unsigned n=(selected[at]>>24)&15;
        if((selected[at]&65535)==31&&regType(selected[at+2])==6&&(selected[at+1]&15)==usage)
            selected[at+1]=0x8001000a;
        at+=n+1;}
    A::Program p;
    assert(A::compile(selected.data(),selected.size(),p,true,2,10,usage==10?0:1,mask));
    A::Four value;assert(A::evaluateFast<true>(p,input,constants,value));return value;
}
static void close(const A::Four& a,const A::Four& b,unsigned mask){
    for(unsigned k=0;k<4;++k)if(mask&(1u<<k))assert(std::isfinite(a[k])&&std::isfinite(b[k])&&std::fabs(a[k]-b[k])<2e-6f);
}
static void liveness(){
    const auto code=shader();Words pruned;assert(P::withoutColor(code,6,1,pruned));
    assert(pruned.size()<code.size());
    // r2.xy writes both lanes even though only x reaches an output. Its y
    // producer must survive too, so the retained vector read stays defined.
    bool retainsY=false;
    for(size_t at=1;at<pruned.size()&&pruned[at]!=65535;){const unsigned n=(pruned[at]>>24)&15;
        retainsY|=(pruned[at]&65535)==1&&pruned[at+1]==dst(0,6,2);at+=n+1;}
    assert(retainsY);
    float constants[1024]={};
    for(unsigned n=0;n<64;++n){A::Four input[16]={};
        input[0]={float(n)*.015f,.37f,.71f,1};input[1]={.6f,.3f,.8f,0};input[2]={.7f,.4f,.2f,.8f};
        for(auto pair:{std::pair<unsigned,unsigned>{0,15},{5,3},{11,1}})
            close(evaluate(code,input,constants,pair.first,pair.second),evaluate(pruned,input,constants,pair.first,pair.second),pair.second);
    }
    // Dependency rules for matrix instructions are not inferred from their
    // destination mask. Until explicitly supported they must fail closed.
    for(unsigned opcode:{20u,21u,22u,23u,24u}){
        auto unsupported=code;unsupported.insert(unsupported.end()-1,{opcode|(3u<<24),dst(6,0),src(0,0),src(2,0)});
        assert(!P::withoutColor(unsupported,6,1,pruned));
    }
    auto dp2add=code;dp2add.insert(dp2add.end()-1,{0x0400005a,dst(6,0),src(0,0),src(2,0),src(2,1)});
    assert(!P::withoutColor(dp2add,6,1,pruned));
    auto relative=code;relative.insert(relative.end()-1,{0x02000001,dst(6,0),src(0,0)|0x2000});
    assert(!P::withoutColor(relative,6,1,pruned));
}
static void interpolation(){
    const auto code=shader();P::Layout layout;assert(P::analyse(code.data(),code.size(),layout));
    assert(layout.count==3&&layout.bytes()==96);
    auto duplicate=code;
    for(size_t at=1;at<duplicate.size()&&duplicate[at]!=65535;){const unsigned n=(duplicate[at]>>24)&15;
        if((duplicate[at]&65535)==31&&regType(duplicate[at+2])==1&&regIndex(duplicate[at+2])<3)
            duplicate[at+1]=0x80000000;
        at+=n+1;}
    P::Layout rejected;assert(!P::analyse(duplicate.data(),duplicate.size(),rejected));
    Words patched;assert(P::patch(code,layout,patched));
    std::array<unsigned,10> added{};added.fill(16);
    for(size_t at=1;at<patched.size()&&patched[at]!=65535;){const unsigned op=patched[at]&65535,n=(patched[at]>>24)&15;
        if(op==31&&regType(patched[at+2])==1&&(patched[at+1]&15)==5)
            for(unsigned a=0;a<layout.attributes();++a)if(((patched[at+1]>>16)&15)==layout.semantic[a])added[a]=regIndex(patched[at+2]);
        at+=n+1;}
    for(unsigned a=0;a<layout.attributes();++a)assert(added[a]<16);
    const A::Four positions[]={{.1f,.2f,.3f,1},{.9f,.4f,.5f,1},{.2f,.8f,.4f,1}};
    const A::Four normals[]={{.6f,.3f,.8f,0},{.4f,.7f,.5f,0},{.8f,.2f,.3f,0}};
    const A::Four colors[]={{.8f,.1f,.4f,.3f},{.7f,.9f,.1f,.8f},{.2f,.5f,.9f,.6f}};
    float constants[1024]={};bool oldDiffers=false;unsigned cases=0;
    // Changing current light constants reuses the same bytecode and corner
    // attributes; no stale per-draw lighting is baked into cached geometry.
    for(float gain:{-.05f,.1f,.8f}){
        constants[40]=gain;constants[41]=gain*.5f;constants[42]=gain*.25f;
        for(unsigned i=0;i<=8;++i)for(unsigned j=0;j<=8-i;++j){
            const float weights[]={float(8-i-j)/8,float(i)/8,float(j)/8};
            A::Four input[16]={},expected{};
            for(unsigned corner=0;corner<3;++corner){
                A::Four native[16]={};native[0]=positions[corner];native[1]=normals[corner];native[2]=colors[corner];
                const auto value=evaluate(code,native,constants,10,15);
                for(unsigned k=0;k<4;++k){expected[k]+=value[k]*weights[corner];
                    for(unsigned v=0;v<3;++v)input[v][k]+=native[v][k]*weights[corner];}
                for(unsigned a=0;a<layout.count;++a)input[added[corner*layout.count+a]]=native[layout.color.inputs[a].reg];
            }
            input[added[layout.count*3]]={weights[0],weights[1],weights[2],0};
            close(evaluate(patched,input,constants,10,15),expected,15);
            const auto old=evaluate(code,input,constants,10,15);oldDiffers|=std::fabs(old[0]-expected[0])>.02f;
            for(auto pair:{std::pair<unsigned,unsigned>{0,15},{5,3},{11,1}})
                close(evaluate(code,input,constants,pair.first,pair.second),evaluate(patched,input,constants,pair.first,pair.second),pair.second);
            ++cases;
        }
    }
    assert(oldDiffers);std::printf("PASS WMO colour interpolation: %u barycentric/current-constant cases, original path negative control\n",cases);
}
int main(){liveness();interpolation();std::puts("PASS WMO pruner: partial lanes, fog/UV/position, unsupported matrix/relative inputs");}
