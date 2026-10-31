#include <cstdlib>
#include <new>
#include <cstdio>
static unsigned allocations=0;
void* operator new(size_t n){++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
#include "actor_deformation.h"
#include <cassert>
#include <chrono>
#include <atomic>
using namespace NorthlightActorDeformation;
#include "actor_probe_program.h"
static bool reference(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,unsigned n,const float* bank,const float* inverse,Position& at){
    NorthlightDrawSnapshot::Mesh first;first.vertexCount=1;
    for(unsigned s=0;s<4;++s){first.streams[s].stride=mesh.streams[s].stride;if(!mesh.streams[s].bytes.empty())first.streams[s].bytes.assign(mesh.streams[s].bytes.begin(),mesh.streams[s].bytes.begin()+first.streams[s].stride);}
    std::vector<Position> positions;if(!worldPositions(program,first,elements,n,bank,inverse,positions))return false;at=positions[0];return true;
}
static bool candidate(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,unsigned n,const float* bank,const float* inverse,Position& at){
    Four inputs[16];if(!sampledVertexInputsAt(program,mesh,elements,n,0,inputs))return false;
    const float origin[3]={};float distance=0,world[3];
    if(!sampledDistanceFromInputs(program,inputs,bank,inverse,origin,distance,world))return false;
    at={world[0],world[1],world[2]};return true;
}
static volatile float sink=0;
int main(int argc,char** argv){
    unsigned cases=0,oldAllocations=0,newAllocations=0;
    for(unsigned major=1;major<=3;++major)for(unsigned split=0;split<2;++split){
        auto words=code(major);Program program;assert(compile(words.data(),words.size(),program));
        NorthlightDrawSnapshot::Mesh mesh;mesh.vertexCount=16;mesh.streams[0].stride=20;mesh.streams[0].bytes.resize(320);
        D3DVERTEXELEMENT9 decl[]={{0,0,2,0,0,0},{0,16,5,0,2,0},{0xff,0,17,0,0,0}};
        if(split){mesh.streams[1].stride=4;mesh.streams[1].bytes.resize(64);decl[1].Stream=1;decl[1].Offset=0;}
        float bank[1024]={},inverse[16]={};for(unsigned i=0;i<4;++i)inverse[i*5]=1;inverse[12]=1000;inverse[13]=-20;
        for(unsigned bone=0;bone<4;++bone){const unsigned r=31+3*bone;bank[r*4]=bank[(r+1)*4+1]=bank[(r+2)*4+2]=1;bank[r*4+3]=100.f*bone;bank[(r+1)*4+3]=10.f*bone;}
        for(unsigned pose=0;pose<200;++pose){
            const float xyz[]={float(pose)*.13f,-float(pose)*.07f,4};std::memcpy(mesh.streams[0].bytes.data(),xyz,12);
            mesh.streams[split?1:0].bytes[split?0:16]=pose%4;bank[31*4+3]=float(pose)*.17f;
            Position old{},next{};unsigned before=allocations;const bool a=reference(program,mesh,decl,3,bank,inverse,old);oldAllocations+=allocations-before;
            before=allocations;const bool b=candidate(program,mesh,decl,3,bank,inverse,next);newAllocations+=allocations-before;
            assert(a==b);if(a)assert(!std::memcmp(&old,&next,sizeof old));++cases;
        }
        Position old{},next{};mesh.streams[split?1:0].bytes[split?0:16]=255;
        assert(reference(program,mesh,decl,3,bank,inverse,old)==candidate(program,mesh,decl,3,bank,inverse,next));
        mesh.streams[split?1:0].bytes[split?0:16]=1;
        double elapsed[2]={};
        for(unsigned round=0;round<20;++round)for(unsigned turn=0;turn<2;++turn){unsigned mode=(round+turn)&1;auto begin=std::chrono::steady_clock::now();
            for(unsigned repeat=0;repeat<1000;++repeat){std::atomic_signal_fence(std::memory_order_seq_cst);Position p;assert(mode?candidate(program,mesh,decl,3,bank,inverse,p):reference(program,mesh,decl,3,bank,inverse,p));sink=p.x;}
            elapsed[mode]+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count()/1000;
        }
        std::printf("actorProbe major=%u split=%u oldNs=%.3f newNs=%.3f ratio=%.4f\n",major,split,elapsed[0]/20,elapsed[1]/20,elapsed[1]/elapsed[0]);
    }
    assert(cases==1200&&oldAllocations>0&&!newAllocations);
    if(argc>1){
        FILE* file=std::fopen(argv[1],"rb");assert(file);std::fseek(file,0,SEEK_END);const long length=std::ftell(file);std::rewind(file);
        assert(length>0&&length%4==0);std::vector<Word> words(size_t(length)/4);assert(std::fread(words.data(),1,length,file)==size_t(length));std::fclose(file);
        Program program;assert(compile(words.data(),words.size(),program));NorthlightDrawSnapshot::Mesh mesh;mesh.vertexCount=16;mesh.streams[0].stride=48;mesh.streams[0].bytes.resize(16*48);
        D3DVERTEXELEMENT9 decl[]={{0,0,2,0,0,0},{0,12,8,0,1,0},{0,16,5,0,2,0},{0,20,2,0,3,0},{0,32,1,0,5,0},{0xff,0,17,0,0,0}};
        float bank[1024]={},inverse[16]={};for(unsigned i=0;i<4;++i)inverse[i*5]=1;inverse[12]=1000;inverse[13]=-20;
        for(unsigned bone=0;bone<4;++bone){const unsigned r=31+3*bone;bank[r*4]=bank[(r+1)*4+1]=bank[(r+2)*4+2]=1;bank[r*4+3]=100.f*bone;bank[(r+1)*4+3]=10.f*bone;}
        for(unsigned pose=0;pose<256;++pose){const float xyz[]={float(pose)*.13f,-float(pose)*.07f,4};std::memcpy(mesh.streams[0].bytes.data(),xyz,12);
            auto* bytes=mesh.streams[0].bytes.data();bytes[12]=pose;bytes[13]=255-pose;bytes[14]=bytes[15]=0;for(unsigned bone=0;bone<4;++bone)bytes[16+bone]=bone;
            bank[31*4+3]=float(pose)*.17f;Position old{},next{};const bool a=reference(program,mesh,decl,6,bank,inverse,old);const unsigned before=allocations;const bool b=candidate(program,mesh,decl,6,bank,inverse,next);
            assert(a&&a==b&&allocations==before&&!std::memcmp(&old,&next,sizeof old));
        }
        std::puts("PASS actual client four-bone program: 256 current weighted poses, exact positions, zero probe allocations");
    }
    std::printf("PASS actor first vertex: cases=%u exactPositions=1 oldAllocations=%u newAllocations=%u currentPose=1 invalidBone=1\n",cases,oldAllocations,newAllocations);
}
