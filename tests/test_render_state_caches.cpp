#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
using UINT=unsigned;using DWORD=uint32_t;using HRESULT=int32_t;
constexpr HRESULT D3D_OK=0;
#define FAILED(x) ((x)<0)
constexpr unsigned MAXD3DDECLLENGTH=64;
constexpr unsigned D3DDECLMETHOD_DEFAULT=0;
struct D3DVERTEXELEMENT9 {uint16_t Stream=0,Offset=0;uint8_t Type=0,Method=0,Usage=0,UsageIndex=0;};
struct IDirect3DVertexDeclaration9 {
    int refs=1,reads=0;bool failure=false,invalid=false;
    void AddRef(){++refs;}void Release(){assert(refs>0);--refs;}
    HRESULT GetDeclaration(D3DVERTEXELEMENT9* e,UINT* n){++reads;if(failure)return -1;*n=2;e[0]={};e[1].Stream=invalid?0:0xff;return 0;}
};
struct IDirect3DVertexBuffer9{};struct IDirect3DIndexBuffer9{};struct IDirect3DVertexShader9{};struct IDirect3DBaseTexture9{};
constexpr unsigned D3DSAMP_ADDRESSU=1,D3DSAMP_ADDRESSV=2;
struct IDirect3DDevice9 {
    unsigned calls=0;bool failNext=false;
    IDirect3DVertexDeclaration9* decl=nullptr;IDirect3DVertexShader9* vs=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
    IDirect3DVertexBuffer9* vb[4]={};UINT offsets[4]={},strides[4]={};IDirect3DBaseTexture9* texture=nullptr;DWORD u=0,v=0;float cutoff=0;
    bool accept(){++calls;bool ok=!failNext;failNext=false;return ok;}
    HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9* p){if(!accept())return -1;decl=p;return 0;}
    HRESULT SetVertexShader(IDirect3DVertexShader9* p){if(!accept())return -1;vs=p;return 0;}
    HRESULT SetIndices(IDirect3DIndexBuffer9* p){if(!accept())return -1;ib=p;return 0;}
    HRESULT SetStreamSource(unsigned s,IDirect3DVertexBuffer9* p,UINT o,UINT stride){if(!accept())return -1;vb[s]=p;offsets[s]=o;strides[s]=stride;return 0;}
    HRESULT SetTexture(unsigned s,IDirect3DBaseTexture9* p){assert(s==0);if(!accept())return -1;texture=p;return 0;}
    HRESULT SetSamplerState(unsigned s,unsigned state,DWORD value){assert(s==0);if(!accept())return -1;(state==D3DSAMP_ADDRESSU?u:v)=value;return 0;}
    HRESULT SetPixelShaderConstantF(unsigned r,const float* p,unsigned n){assert(r==0&&n==1);if(!accept())return -1;assert(p[0]==0&&p[1]==0&&p[2]==0);cutoff=p[3];return 0;}
};
#include "vertex_declaration_cache.h"
#include "replay_draw_state.h"
struct Replay {
    IDirect3DVertexDeclaration9* decl=nullptr;IDirect3DVertexShader9* shader=nullptr;IDirect3DIndexBuffer9* index=nullptr;
    IDirect3DVertexBuffer9* stream[4]={};UINT offset[4]={},stride[4]={};IDirect3DBaseTexture9* texture=nullptr;DWORD addressU=1,addressV=1;float cutoff=-1;unsigned alphaMode=0;
};
int main(){
    std::array<IDirect3DVertexDeclaration9,140> declarations;
    {
        NorthlightVertexDeclarations::Cache c;const D3DVERTEXELEMENT9* e=nullptr;UINT n=0;
        for(unsigned j=0;j<100;++j)assert(c.get(&declarations[0],e,n)&&n==2&&e[1].Stream==0xff);
        assert(declarations[0].reads==1&&declarations[0].refs==2);
        for(auto& d:declarations)assert(c.get(&d,e,n));
        unsigned held=0;for(auto& d:declarations)held+=d.refs==2;assert(held==128);
        c.clear();for(auto& d:declarations)assert(d.refs==1);
        declarations[0].failure=true;assert(!c.get(&declarations[0],e,n)&&!e&&n==0);
        declarations[0].failure=false;declarations[0].invalid=true;assert(!c.get(&declarations[0],e,n));
        declarations[0].invalid=false;assert(c.get(&declarations[0],e,n));
    }
    for(auto& d:declarations)assert(d.refs==1);
    IDirect3DDevice9 d;IDirect3DVertexBuffer9 vb;IDirect3DIndexBuffer9 ib;IDirect3DVertexShader9 vs;IDirect3DBaseTexture9 texture;
    Replay p;p.decl=&declarations[0];p.shader=&vs;p.index=&ib;p.stream[0]=&vb;p.stride[0]=32;p.texture=&texture;
    NorthlightReplayDrawState::Cache c(&d);assert(!c.geometry(p)&&!c.material(p));assert(d.calls==11);
    for(unsigned j=0;j<100;++j)assert(!c.geometry(p)&&!c.material(p));assert(d.calls==11);
    {const auto& n=c.counts;assert(n.declarations==1&&n.streams==4&&n.indices==1&&n.shaders==1&&n.textures==1&&n.samplers==2&&n.cutoffs==1);}
    p.offset[0]=256;assert(!c.geometry(p));assert(d.calls==12&&d.offsets[0]==256);
    p.cutoff=.5f;assert(!c.material(p));assert(d.calls==13&&d.cutoff==.5f);
    p.addressU=3;assert(!c.material(p));assert(d.calls==14&&d.u==3);
    p.stream[0]=nullptr;p.stride[0]=0;assert(!c.geometry(p));assert(d.calls==15&&!d.vb[0]);
    p.offset[0]=512;d.failNext=true;assert(FAILED(c.geometry(p)));unsigned before=d.calls;
    assert(!c.geometry(p));assert(d.calls==before+7&&d.offsets[0]==512);
    p.cutoff=.7f;d.failNext=true;assert(FAILED(c.material(p)));before=d.calls;
    assert(!c.material(p));assert(d.calls==before+4&&d.cutoff==.7f);
    // A fresh pass cannot trust any bindings left by another pass/StateBlock.
    NorthlightReplayDrawState::Cache next(&d);before=d.calls;assert(!next.geometry(p)&&!next.material(p));assert(d.calls==before+11);
    std::puts("PASS declaration lifetime/eviction/failure; repeated replay states, offsets, null streams, failed setter and pass invalidation");
}
