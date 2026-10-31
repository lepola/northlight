#include <cstdlib>
#include <new>
#include <cstdio>
static unsigned allocations=0;
void* operator new(size_t n){++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
#include "actor_deformation.h"
#include "actor_reference.h"
#include <cassert>
#include <chrono>
#include <atomic>
using namespace NorthlightActorDeformation;
#include "actor_probe_program.h"
static volatile float sink=0;
template<class P,class V> __attribute__((noinline)) static bool positions(const P& p,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* decl,const float* bank,const float* inverse,V& output){
    return worldPositions(p,mesh,decl,4,bank,inverse,output);
}
template<class P> __attribute__((noinline)) static bool coordinates(const P& p,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* decl,const float* bank,std::vector<std::array<float,2>>& output){
    return textureUVs(p,mesh,decl,4,bank,output);
}
__attribute__((noinline)) static void maskTiming(const Program& p,const ActorReference::Program& old,unsigned major,unsigned uv,unsigned split){
    double maskNs[2]={};unsigned maskSums[2]={};
    for(unsigned round=0;round<12;++round)for(unsigned turn=0;turn<2;++turn){unsigned mode=(round+turn)&1;auto start=std::chrono::steady_clock::now();unsigned sum=0;
        for(unsigned mask=0;mask<65536;++mask){std::atomic_signal_fence(std::memory_order_seq_cst);sum+=mode?clearVertexInputsMask(p,mask):ActorReference::clearVertexInputsMask(old,mask);}
        maskSums[mode]+=sum;maskNs[mode]+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/65536;}
    assert(maskSums[0]==maskSums[1]);std::printf("inputMask major=%u uv=%u split=%u oldNs=%.3f newNs=%.3f ratio=%.4f\n",major,uv,split,maskNs[0]/12,maskNs[1]/12,maskNs[1]/maskNs[0]);
}
int main(int argc,char** argv){
    unsigned cases=0,extraCases=0,oldAllocations=0,newAllocations=0;
    for(unsigned major=1;major<=3;++major)for(unsigned uv=0;uv<2;++uv)for(unsigned split=0;split<2;++split){
        auto words=code(major);Program p;ActorReference::Program old;
        assert(compile(words.data(),words.size(),p,uv)&&ActorReference::compile(words.data(),words.size(),old,uv));
        for(unsigned mask=0;mask<65536;++mask)assert(clearVertexInputsMask(p,mask)==ActorReference::clearVertexInputsMask(old,mask));
        maskTiming(p,old,major,uv,split);
        NorthlightDrawSnapshot::Mesh mesh;mesh.vertexCount=24;mesh.streams[0].stride=32;mesh.streams[0].bytes.resize(32*24);
        D3DVERTEXELEMENT9 decl[]={{0,0,2,0,0,0},{0,16,5,0,2,0},{0,24,1,0,5,0},{0xff,0,17,0,0,0}};
        if(split){mesh.streams[1].stride=12;mesh.streams[1].bytes.resize(12*24);decl[1].Stream=decl[2].Stream=1;decl[1].Offset=0;decl[2].Offset=4;}
        for(unsigned v=0;v<24;++v){float xyz[]={v*.23f,2,-3};std::memcpy(mesh.streams[0].bytes.data()+32*v,xyz,12);
            auto* bytes=mesh.streams[split].bytes.data()+v*mesh.streams[split].stride;bytes[split?0:16]=v%4;
            float tex[]={v*.011f,v*.07f};std::memcpy(bytes+(split?4:24),tex,8);}
        float bank[1024]={},inverse[16]={};for(unsigned i=0;i<4;++i)inverse[i*5]=1;inverse[12]=1000;
        for(unsigned bone=0;bone<4;++bone){unsigned r=31+3*bone;bank[4*r]=bank[4*(r+1)+1]=bank[4*(r+2)+2]=1;}
        bank[24]=2;bank[25]=3;bank[28]=.25f;bank[29]=-.5f;
        std::vector<Position> result;std::vector<ActorReference::Position> expected;
        std::vector<std::array<float,2>> tex,expectedTex;
        auto run=[&](bool candidate){return uv?(candidate?coordinates(p,mesh,decl,bank,tex):coordinates(old,mesh,decl,bank,expectedTex)):
            (candidate?positions(p,mesh,decl,bank,inverse,result):positions(old,mesh,decl,bank,inverse,expected));};
        auto equal=[&]{if(uv)return tex==expectedTex;return result.size()==expected.size()&&(result.empty()||!std::memcmp(result.data(),expected.data(),result.size()*sizeof(Position)));};
        assert(run(false)&&run(true)&&equal());
        for(unsigned pose=0;pose<200;++pose){bank[31*4+3]=pose*.17f;bank[28]=pose*.003f;
            unsigned before=allocations;bool a=run(false);oldAllocations+=allocations-before;before=allocations;bool b=run(true);newAllocations+=allocations-before;assert(a==b&&equal());++cases;}
        if(!uv){double times[2]={};float xyz[2][3]={{}};
            for(unsigned round=0;round<12;++round)for(unsigned turn=0;turn<2;++turn){unsigned mode=(round+turn)&1;auto start=std::chrono::steady_clock::now();
                for(unsigned n=0;n<1000;++n){std::atomic_signal_fence(std::memory_order_seq_cst);bank[34*4+3]=n*.013f;assert(mode?sampledExtraWorld(p,mesh,decl,4,n%2,bank,inverse,xyz[1]):ActorReference::sampledExtraWorld(old,mesh,decl,4,n%2,bank,inverse,xyz[0]));sink=xyz[mode][0];}
                times[mode]+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/1000;}
            assert(!std::memcmp(xyz[0],xyz[1],12));std::printf("extraWorld major=%u split=%u oldNs=%.3f newNs=%.3f ratio=%.4f\n",major,split,times[0]/12,times[1]/12,times[1]/times[0]);
            for(unsigned n=0;n<200;++n)for(unsigned slot=0;slot<2;++slot){bank[31*4+3]=n*.23f;bank[34*4+3]=-float(n)*.13f;inverse[12]=n*17.3f;inverse[13]=-float(n)*.07f;
                bool a=ActorReference::sampledExtraWorld(old,mesh,decl,4,slot,bank,inverse,xyz[0]),b=sampledExtraWorld(p,mesh,decl,4,slot,bank,inverse,xyz[1]);assert(a==b&&(!a||!std::memcmp(xyz[0],xyz[1],12)));++extraCases;}
        }
        double elapsed[2]={};
        for(unsigned round=0;round<12;++round)for(unsigned turn=0;turn<2;++turn){unsigned mode=(round+turn)&1;auto begin=std::chrono::steady_clock::now();
            for(unsigned n=0;n<1000;++n){std::atomic_signal_fence(std::memory_order_seq_cst);assert(run(mode));sink=uv?tex[0][0]:result[0].x;}
            elapsed[mode]+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count()/1000;}
        std::printf("transient major=%u uv=%u split=%u oldNs=%.2f newNs=%.2f ratio=%.4f\n",major,uv,split,elapsed[0]/12,elapsed[1]/12,elapsed[1]/elapsed[0]);
        for(unsigned fault=0;fault<7;++fault){auto saved=decl[uv?2:0];const auto bytes=mesh.streams[split].bytes.size();
            switch(fault){case 0:decl[uv?2:0].Method=1;break;case 1:decl[uv?2:0].Stream=4;break;case 2:decl[uv?2:0].Offset=65535;break;case 3:decl[uv?2:0].Type=17;break;case 4:decl[uv?2:0].Usage=15;break;case 5:mesh.streams[split].bytes.resize(1);break;case 6:bank[31*4]=NAN;bank[24]=NAN;break;}
            bool a=run(false),b=run(true);assert(a==b&&equal());++cases;decl[uv?2:0]=saved;mesh.streams[split].bytes.resize(bytes);}
    }
    if(argc>1){FILE* f=std::fopen(argv[1],"rb");assert(f);std::fseek(f,0,SEEK_END);long size=std::ftell(f);std::rewind(f);assert(size>0&&size%4==0);
        std::vector<Word> words(size_t(size)/4);assert(std::fread(words.data(),1,size,f)==size_t(size));std::fclose(f);Program p;ActorReference::Program old;assert(compile(words.data(),words.size(),p)&&ActorReference::compile(words.data(),words.size(),old));
        for(unsigned mask=0;mask<65536;++mask)assert(clearVertexInputsMask(p,mask)==ActorReference::clearVertexInputsMask(old,mask));
        NorthlightDrawSnapshot::Mesh mesh;mesh.vertexCount=24;mesh.streams[0].stride=48;mesh.streams[0].bytes.resize(24*48);mesh.indices={23,17,5,0,6,20};
        D3DVERTEXELEMENT9 decl[]={{0,0,2,0,0,0},{0,12,8,0,1,0},{0,16,5,0,2,0},{0,20,2,0,3,0},{0,32,1,0,5,0},{0xff,0,17,0,0,0}};
        float bank[1024]={},inverse[16]={};for(unsigned i=0;i<4;++i)inverse[i*5]=1;inverse[12]=1000;inverse[13]=-20;
        for(unsigned bone=0;bone<4;++bone){unsigned r=31+3*bone;bank[r*4]=bank[(r+1)*4+1]=bank[(r+2)*4+2]=1;bank[r*4+3]=100.f*bone;bank[(r+1)*4+3]=10.f*bone;}
        for(unsigned pose=0;pose<256;++pose){for(unsigned v=0;v<24;++v){auto* b=mesh.streams[0].bytes.data()+48*v;float xyz[]={v*.13f,-float(v)*.07f,4};std::memcpy(b,xyz,12);b[12]=pose;b[13]=255-pose;b[14]=b[15]=0;for(unsigned k=0;k<4;++k)b[16+k]=(k+v)%4;}
            bank[31*4+3]=pose*.17f;for(unsigned indexed=0;indexed<2;++indexed)for(unsigned slot=0;slot<2;++slot){mesh.indexed=indexed;float a[3],b[3];assert(ActorReference::sampledExtraWorld(old,mesh,decl,6,slot,bank,inverse,a)&&sampledExtraWorld(p,mesh,decl,6,slot,bank,inverse,b)&&!std::memcmp(a,b,12));}}
        std::puts("PASS actual client four-bone extra samples: 1024 exact weighted poses; all 65536 input masks");
    }
    assert(!newAllocations);std::printf("PASS transient bindings: cases=%u oldAllocations=%u newAllocations=%u exactPositionUV=1 failures=1 allInputMasks=1 extraSamples=%u\n",cases,oldAllocations,newAllocations,extraCases);
}
