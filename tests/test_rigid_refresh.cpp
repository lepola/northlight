// 0.3.176 (S3'): the in-place refresh of a remembered rigid copy. The production RigidRef, RigidDraw,
// RigidPayload, rigidFill, rigidCopy and rigidRefresh (world_rigid_memory.inl, pasted below by
// test_rigid_memory.py) over 1000 random refresh sequences: a payload refreshed in place and one
// replaced by a fresh rigidCopy each time (0.3.175) hold the same contents after every step, and every
// fake COM object's count is its own reference plus exactly two per holding draw (one per payload),
// so the references balance. Draw count changes rebuild; pointers repeat, change and go null
// (opaque draws hold no texture). Built by test_rigid_memory.py. Native, no game or GPU.
#include "shader_constant_usage.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>
#include "draw_snapshot.h"
using BOOL=int;constexpr DWORD D3DTADDRESS_WRAP=1;
struct Counted {unsigned refs=1;unsigned AddRef(){return ++refs;}unsigned Release(){assert(refs>1);return --refs;}};
struct IDirect3DVertexShader9:Counted {};struct IDirect3DBaseTexture9:Counted {};
struct Declaration final:IDirect3DVertexDeclaration9 {unsigned refs=1;unsigned AddRef()override{return ++refs;}unsigned Release()override{assert(refs>1);return --refs;}
    HRESULT GetDeclaration(D3DVERTEXELEMENT9*,UINT*)override{return E_POINTER;}};
struct Replay {
    IDirect3DVertexShader9* shader=nullptr;IDirect3DVertexShader9* originalShader=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;IDirect3DBaseTexture9* texture=nullptr;
    std::shared_ptr<const NorthlightDrawSnapshot::Mesh> shared;float constantStorage[1024]={};BOOL boolStorage[16]={};int intStorage[64]={};
    const float* constants=constantStorage;const BOOL* bools=boolStorage;const int* ints=intStorage;
    NorthlightShaderConstants::Usage constantUsage;unsigned projectionKind=2;float cutoff=-1;unsigned alphaMode=0;DWORD addressU=D3DTADDRESS_WRAP,addressV=D3DTADDRESS_WRAP;
    D3DPRIMITIVETYPE type=D3DPT_TRIANGLELIST;UINT count=0;bool indexed=false;
};
struct Harness {
    std::vector<const Replay*> rigidGroupDraws;std::vector<std::pair<size_t,unsigned>> rigidGroups;
/*REFRESH_METHODS*/
};
using Payload=Harness::RigidPayload;
static bool same(const Payload& a,const Payload& b){
    if(a.bone!=b.bone||a.draws.size()!=b.draws.size())return false;
    for(size_t i=0;i<a.draws.size();++i){const auto& x=a.draws[i];const auto& y=b.draws[i];
        if(x.shader.p!=y.shader.p||x.originalShader.p!=y.originalShader.p||x.decl.p!=y.decl.p||x.texture.p!=y.texture.p||x.shared!=y.shared||x.constants!=y.constants||
           std::memcmp(x.bools,y.bools,sizeof x.bools)||std::memcmp(x.ints,y.ints,sizeof x.ints)||std::memcmp(&x.usage,&y.usage,sizeof x.usage)||x.projectionKind!=y.projectionKind||
           x.cutoff!=y.cutoff||x.addressU!=y.addressU||x.addressV!=y.addressV||x.type!=y.type||x.count!=y.count||x.indexed!=y.indexed)return false;}
    return true;
}
int main(){
    std::mt19937 rng(1763);
    std::vector<IDirect3DVertexShader9> shaders(6);std::vector<IDirect3DBaseTexture9> textures(4);std::vector<Declaration> declarations(3);
    std::vector<std::shared_ptr<const NorthlightDrawSnapshot::Mesh>> meshes;for(unsigned i=0;i<5;++i)meshes.push_back(std::make_shared<NorthlightDrawSnapshot::Mesh>());
    auto balanced=[&](const Payload& a){
        auto count=[&](const void* object){unsigned n=0;for(const auto& d:a.draws)n+=(d.shader.p==object)+(d.originalShader.p==object)+(d.decl.p==object)+(d.texture.p==object);return n;};
        for(auto& s:shaders)assert(s.refs==1+2*count(&s));for(auto& t:textures)assert(t.refs==1+2*count(&t));for(auto& d:declarations)assert(d.refs==1+2*count(&d));};
    size_t steps=0,inPlace=0,rebuilt=0;
    for(unsigned sequence=0;sequence<1000;++sequence){
        Payload refreshed,fresh;unsigned draws=1+rng()%4;
        for(unsigned step=0;step<12;++step,++steps){
            if(rng()%5==0)draws=1+rng()%4; /* the group gained or lost a draw: rebuilt */
            std::vector<Replay> replays(draws+rng()%3);Harness h;
            for(auto& p:replays){p.shader=&shaders[rng()%6];p.originalShader=rng()%3?&shaders[rng()%2]:nullptr;p.decl=rng()%4?&declarations[rng()%3]:nullptr;
                p.texture=rng()%3?&textures[rng()%4]:nullptr;p.cutoff=rng()%2?-1.f:float(rng()%4)*.25f;p.shared=rng()%4?meshes[rng()%5]:nullptr;
                for(unsigned k=0;k<1024;++k)p.constantStorage[k]=float(rng()%1000);for(auto& b:p.boolStorage)b=int(rng()%2);for(auto& i:p.intStorage)i=int(rng()%9);
                p.constantUsage.floats={unsigned(rng()%8),unsigned(rng()%256)};p.constantUsage.analyzed=rng()%2;p.projectionKind=1+rng()%2;p.addressU=1+rng()%3;p.addressV=1+rng()%3;
                p.type=rng()%2?D3DPT_TRIANGLELIST:D3DPT_TRIANGLESTRIP;p.count=rng()%500;p.indexed=rng()%2;}
            const size_t lead=replays.size()-draws; /* another group's copies first */
            for(auto& p:replays)h.rigidGroupDraws.push_back(&p);
            if(lead)h.rigidGroups.push_back({0,unsigned(rng()%75)});h.rigidGroups.push_back({lead,unsigned(rng()%75)});
            const size_t n=h.rigidGroups.size()-1;
            const bool equal=refreshed.draws.size()==draws;inPlace+=equal;rebuilt+=!equal;
            h.rigidRefresh(n,refreshed);                /* 0.3.176 */
            fresh=h.rigidCopy(n);                      /* 0.3.175: a fresh copy replaces the payload */
            assert(same(refreshed,fresh));balanced(refreshed);
        }
    }
    for(auto& s:shaders)assert(s.refs==1);for(auto& t:textures)assert(t.refs==1);for(auto& d:declarations)assert(d.refs==1);
    std::printf("PASS rigid refresh in place == fresh rigidCopy: 1000 sequences, %zu refreshes (%zu in place, %zu rebuilt), contents equal and references balanced after each\n",steps,inPlace,rebuilt);
}
