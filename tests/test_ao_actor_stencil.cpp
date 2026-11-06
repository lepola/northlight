#include <d3d9.h>
#include "ao_actor_stencil.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>

struct Control {unsigned calls=0,failAt=0,failed=0;bool call(){if(++calls==failAt){++failed;return false;}return true;}};
struct Owned {unsigned refs=1;virtual ~Owned()=default;};
struct Surface:IDirect3DSurface9,Owned {
    Control* c;D3DSURFACE_DESC desc{};unsigned bits=0xe5,descriptions=0;float colour=7;
    explicit Surface(Control* p):c(p){}
    ULONG AddRef()override{return ++refs;}ULONG Release()override{assert(refs);return --refs;}
    HRESULT GetDesc(D3DSURFACE_DESC* out)override{++descriptions;if(!c->call())return E_FAIL;*out=desc;return D3D_OK;}
};
struct Texture:IDirect3DTexture9,Owned {
    Control* c;Surface* level;explicit Texture(Control* p):c(p),level(nullptr){}
    ULONG AddRef()override{return ++refs;}ULONG Release()override{assert(refs);if(!--refs)level->Release();return refs;}
    HRESULT GetSurfaceLevel(UINT,IDirect3DSurface9** out)override{if(!c->call())return E_FAIL;level->AddRef();*out=level;return D3D_OK;}
};
struct Pixel:IDirect3DPixelShader9,Owned {
    ULONG AddRef()override{return ++refs;}ULONG Release()override{assert(refs);return --refs;}
    HRESULT GetFunction(void*,UINT*)override{assert(false);return E_FAIL;} // ownership capture never reads/patches native bytecode
};
struct VertexShader:IDirect3DVertexShader9,Owned {ULONG AddRef()override{return ++refs;}ULONG Release()override{assert(refs);return --refs;}};
struct State {
    IDirect3DSurface9* rt[4]={};IDirect3DSurface9* depth=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexShader9* vs=nullptr;
    std::array<DWORD,256> rs{};std::array<IDirect3DBaseTexture9*,20> textures{};D3DVIEWPORT9 vp{0,0,80,60,.2f,.94f};DWORD fvf=0x1234;UINT frequency=0x40000002;unsigned stream=123;
    bool operator==(const State& b)const{return std::equal(std::begin(rt),std::end(rt),std::begin(b.rt))&&depth==b.depth&&ps==b.ps&&vs==b.vs&&rs==b.rs&&textures==b.textures&&
        vp.X==b.vp.X&&vp.Y==b.vp.Y&&vp.Width==b.vp.Width&&vp.Height==b.vp.Height&&vp.MinZ==b.vp.MinZ&&vp.MaxZ==b.vp.MaxZ&&fvf==b.fvf&&frequency==b.frequency&&stream==b.stream;}
};
struct Device;
struct Block:IDirect3DStateBlock9 {Device* d;State saved;unsigned refs=1;explicit Block(Device*);~Block();void save();ULONG AddRef()override{return ++refs;}ULONG Release()override{assert(refs);const auto n=--refs;if(!n)delete this;return n;}HRESULT Capture()override;HRESULT Apply()override;};
struct Device:IDirect3DDevice9 {
    Control c;State s;std::vector<std::unique_ptr<Owned>> arena;unsigned nativeDraws=0,quadDraws=0,textureCreates=0,shaderCreates=0,stretches=0,blocks=0;
    template<class T,class... A> T* make(A... args){auto p=std::make_unique<T>(args...);T* raw=p.get();arena.push_back(std::move(p));return raw;}
    Surface* surface(unsigned format=21,unsigned samples=0){auto* p=make<Surface>(&c);p->desc.Width=80;p->desc.Height=60;p->desc.Format=format;p->desc.MultiSampleType=samples;return p;}
    Device(){s.rt[0]=surface();s.depth=surface(75);s.ps=make<Pixel>();s.vs=make<VertexShader>();s.rs[D3DRS_STENCILFUNC]=D3DCMP_ALWAYS;s.rs[D3DRS_STENCILFAIL]=s.rs[D3DRS_STENCILZFAIL]=s.rs[D3DRS_STENCILPASS]=D3DSTENCILOP_KEEP;
        s.rs[D3DRS_STENCILMASK]=s.rs[D3DRS_STENCILWRITEMASK]=255;s.rs[D3DRS_COLORWRITEENABLE]=15;s.rs[D3DRS_COLORWRITEENABLE1]=5;s.rs[D3DRS_ZWRITEENABLE]=TRUE;s.rs[D3DRS_FOGENABLE]=TRUE;s.rs[D3DRS_FILLMODE]=2;}
    template<class T> HRESULT get(T* value,T** out){*out=nullptr;if(!c.call())return E_FAIL;*out=value;if(value)value->AddRef();return value?D3D_OK:D3DERR_NOTFOUND;}
    HRESULT GetRenderTarget(DWORD n,IDirect3DSurface9** out)override{return get(s.rt[n],out);}
    HRESULT SetRenderTarget(DWORD n,IDirect3DSurface9* p)override{if(!c.call())return E_FAIL;s.rt[n]=p;s.vp={1,2,3,4,0,1};return D3D_OK;}
    HRESULT GetDepthStencilSurface(IDirect3DSurface9** out)override{return get(s.depth,out);}
    HRESULT SetDepthStencilSurface(IDirect3DSurface9* p)override{if(!c.call())return E_FAIL;s.depth=p;return D3D_OK;}
    HRESULT GetViewport(D3DVIEWPORT9* out)override{if(!c.call())return E_FAIL;*out=s.vp;return D3D_OK;}
    HRESULT SetViewport(const D3DVIEWPORT9* p)override{if(!c.call())return E_FAIL;s.vp=*p;return D3D_OK;}
    HRESULT GetRenderState(DWORD n,DWORD* out)override{if(!c.call())return E_FAIL;*out=s.rs[n];return D3D_OK;}
    HRESULT SetRenderState(DWORD n,DWORD value)override{if(!c.call())return E_FAIL;s.rs[n]=value;return D3D_OK;}
    HRESULT GetPixelShader(IDirect3DPixelShader9** out)override{return get(s.ps,out);}
    HRESULT SetPixelShader(IDirect3DPixelShader9* p)override{if(!c.call())return E_FAIL;s.ps=p;return D3D_OK;}
    HRESULT SetVertexShader(IDirect3DVertexShader9* p)override{if(!c.call())return E_FAIL;s.vs=p;return D3D_OK;}
    HRESULT SetFVF(DWORD value)override{if(!c.call())return E_FAIL;s.fvf=value;return D3D_OK;}
    HRESULT SetStreamSourceFreq(UINT,UINT value)override{if(!c.call())return E_FAIL;s.frequency=value;return D3D_OK;}
    static unsigned stage(DWORD n){assert(n<16||(n>=257&&n<=260));return n<16?n:n-257+16;}
    HRESULT GetTexture(DWORD n,IDirect3DBaseTexture9** out)override{const auto hr=get(s.textures[stage(n)],out);return hr==D3DERR_NOTFOUND?D3D_OK:hr;}
    HRESULT SetTexture(DWORD n,IDirect3DBaseTexture9* value)override{if(!c.call())return E_FAIL;s.textures[stage(n)]=value;return D3D_OK;}
    HRESULT CreatePixelShader(const DWORD*,IDirect3DPixelShader9** out)override{*out=nullptr;if(!c.call())return E_FAIL;*out=make<Pixel>();++shaderCreates;return D3D_OK;}
    HRESULT CreateTexture(UINT,UINT,UINT,DWORD,D3DFORMAT f,D3DPOOL,IDirect3DTexture9** out,HANDLE*)override{*out=nullptr;if(!c.call())return E_FAIL;auto* t=make<Texture>(&c);t->level=surface(f);*out=t;++textureCreates;return D3D_OK;}
    HRESULT CreateRenderTarget(UINT,UINT,D3DFORMAT f,D3DMULTISAMPLE_TYPE ms,DWORD quality,BOOL,IDirect3DSurface9** out,HANDLE*)override{*out=nullptr;if(!c.call())return E_FAIL;auto* p=surface(f,ms);p->desc.MultiSampleQuality=quality;*out=p;return D3D_OK;}
    HRESULT ColorFill(IDirect3DSurface9*,const RECT*,DWORD)override{assert(false);return E_FAIL;}
    HRESULT StretchRect(IDirect3DSurface9* from,const RECT*,IDirect3DSurface9* to,const RECT*,D3DTEXTUREFILTERTYPE)override{if(!c.call())return E_FAIL;static_cast<Surface*>(to)->colour=static_cast<Surface*>(from)->colour;++stretches;return D3D_OK;}
    HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE,IDirect3DStateBlock9** out)override{*out=nullptr;if(!c.call())return E_FAIL;*out=new Block(this);++blocks;return D3D_OK;}
    bool stencil(bool survives,bool zpass=true) {
        if(!survives)return false;if(!s.rs[D3DRS_STENCILENABLE])return zpass;assert(s.depth);
        auto& bits=static_cast<Surface*>(s.depth)->bits;const DWORD mask=s.rs[D3DRS_STENCILMASK],reference=s.rs[D3DRS_STENCILREF];
        const bool pass=s.rs[D3DRS_STENCILFUNC]==D3DCMP_ALWAYS||(bits&mask)==(reference&mask);
        const DWORD operation=s.rs[!pass?D3DRS_STENCILFAIL:!zpass?D3DRS_STENCILZFAIL:D3DRS_STENCILPASS];
        if(operation==D3DSTENCILOP_REPLACE||operation==D3DSTENCILOP_ZERO){const DWORD write=s.rs[D3DRS_STENCILWRITEMASK];bits=(bits&~write)|(operation==D3DSTENCILOP_ZERO?0:reference&write);}return pass&&zpass;
    }
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE,UINT,const void*,UINT)override {
        if(!c.call())return E_FAIL;++quadDraws;assert(s.frequency==1&&s.rs[D3DRS_MULTISAMPLEANTIALIAS]&&s.rs[D3DRS_FILLMODE]==D3DFILL_SOLID&&!s.rs[D3DRS_ZWRITEENABLE]);
        if(stencil(true)&&s.rs[D3DRS_COLORWRITEENABLE])static_cast<Surface*>(s.rt[0])->colour=1;s.stream=0;return D3D_OK;
    }
    HRESULT Clear(DWORD,const D3DRECT*,DWORD flags,DWORD colour,float,DWORD)override{if(!c.call())return E_FAIL;assert(flags==D3DCLEAR_TARGET);static_cast<Surface*>(s.rt[0])->colour=float(colour);return D3D_OK;}
    HRESULT native(bool survives=true,bool zpass=true,HRESULT hr=D3D_OK){++nativeDraws;if(SUCCEEDED(hr))stencil(survives,zpass);return hr;}
};
Block::Block(Device* owner):d(owner){save();}Block::~Block(){if(saved.ps)saved.ps->Release();if(saved.vs)saved.vs->Release();}
void Block::save(){if(saved.ps)saved.ps->Release();if(saved.vs)saved.vs->Release();saved=d->s;if(saved.ps)saved.ps->AddRef();if(saved.vs)saved.vs->AddRef();}
HRESULT Block::Capture(){if(!d->c.call())return E_FAIL;save();return D3D_OK;}
HRESULT Block::Apply(){if(!d->c.call())return E_FAIL;const auto rt=d->s.rt;IDirect3DSurface9* targets[4];std::copy(rt,rt+4,targets);auto* depth=d->s.depth;d->s=saved;std::copy(targets,targets+4,d->s.rt);d->s.depth=depth;return D3D_OK;}
static void stamp(Device& d,NorthlightAOActorStencil& marker,bool actor,bool survives=true,bool zpass=true){const State before=d.s;assert(marker.beginDraw(actor,true,true,d.s.rt[0],d.s.depth));marker.endDraw(d.native(survives,zpass));assert(d.s==before);}
static void lifecycle() {
    Device d;NorthlightAOActorStencil marker(&d);const State original=d.s;marker.beginFrame(false);const unsigned zero=d.c.calls;
    for(unsigned i=0;i<100;++i)assert(!marker.beginDraw(true,true,true,d.s.rt[0],d.s.depth));marker.beginFrame(true);
    for(unsigned i=0;i<100;++i)assert(!marker.beginDraw(false,true,true,d.s.rt[0],d.s.depth));assert(d.c.calls==zero);
    auto* depth=static_cast<Surface*>(d.s.depth);stamp(d,marker,true);assert(depth->bits==0xe5&&d.quadDraws==1&&!d.textureCreates);
    stamp(d,marker,false,false);assert(depth->bits==0xe5);stamp(d,marker,false,true,false);assert(depth->bits==0xe5);
    assert(depth->descriptions==1&&static_cast<Surface*>(d.s.rt[0])->descriptions==1);
    assert(marker.freeze(d.s.depth)&&d.s==original);auto* mask=static_cast<Texture*>(marker.textureForDepth(d.s.depth));assert(mask&&mask->level->colour==1&&d.quadDraws==2&&!(depth->bits&0x40));
    assert(marker.freeze(d.s.depth)&&d.quadDraws==2);marker.resume();assert(!marker.valid()&&!marker.textureForDepth(d.s.depth));
    marker.beginFrame(true);stamp(d,marker,true);stamp(d,marker,false);assert(depth->bits==0xa5);assert(marker.freeze(d.s.depth));assert(static_cast<Texture*>(marker.textureForDepth(d.s.depth))->level->colour==0);
    const unsigned noQueries=d.c.calls;marker.reset();assert(d.s==original&&d.c.calls==noQueries);for(unsigned i=0;i<4;++i)assert(d.arena[i]->refs==1);for(unsigned i=4;i<d.arena.size();++i)assert(d.arena[i]->refs==0);
    std::puts("Actor stencil: zero-work disabled/no actors, exact bit64 ownership, kill/Z rejection, masked clear, native state/refcounts, freeze/resume");
}
static void composition() {
    for(unsigned samples:{0u,4u}) {
        Device d;for(auto* p:{d.s.rt[0],d.s.depth})static_cast<Surface*>(p)->desc.MultiSampleType=samples;
        auto* depth=static_cast<Surface*>(d.s.depth);depth->bits=0x65;
        d.s.rs[D3DRS_STENCILENABLE]=TRUE;d.s.rs[D3DRS_STENCILREF]=d.s.rs[D3DRS_STENCILWRITEMASK]=0x80;d.s.rs[D3DRS_STENCILPASS]=D3DSTENCILOP_REPLACE;
        d.s.rs[D3DRS_ALPHABLENDENABLE]=TRUE;d.s.rt[1]=d.surface(21,samples);NorthlightAOActorStencil marker(&d,true);marker.beginFrame(true);
        stamp(d,marker,true);assert(depth->bits==0xe5);stamp(d,marker,false);assert(depth->bits==0xa5);const State before=d.s;
        assert(marker.freeze(d.s.depth)&&d.s==before);assert(d.stretches==(samples?1u:0u));
    }
    {Device d;for(auto* p:{d.s.rt[0],d.s.depth})static_cast<Surface*>(p)->desc.MultiSampleType=4;d.s.rt[1]=d.surface(21,4);
    NorthlightAOActorStencil marker(&d);marker.beginFrame(true);const State before=d.s;
    assert(!marker.beginDraw(true,true,true,d.s.rt[0],d.s.depth));assert(d.s==before&&!d.quadDraws&&!d.shaderCreates&&!d.textureCreates);
    assert(d.native()==D3D_OK&&d.nativeDraws==1&&marker.endDraw(D3D_OK));}
    for(unsigned scenario=0;scenario<4;++scenario) {
        Device d;NorthlightAOActorStencil marker(&d);marker.beginFrame(true);
        if(scenario==0)static_cast<Surface*>(d.s.depth)->desc.Format=77;
        if(scenario==1){d.s.rs[D3DRS_STENCILENABLE]=TRUE;d.s.rs[D3DRS_STENCILFUNC]=D3DCMP_EQUAL;}
        if(scenario==2)d.s.vp.MinZ=1;
        if(scenario==3)static_cast<Surface*>(d.s.depth)->desc.MultiSampleType=4;
        const State before=d.s;assert(!marker.beginDraw(true,true,true,d.s.rt[0],d.s.depth)&&d.s==before&&!d.quadDraws);
    }
    for(bool depthWriter:{false,true}) {
        Device d;NorthlightAOActorStencil marker(&d);marker.beginFrame(true);stamp(d,marker,true);
        d.s.rs[D3DRS_STENCILENABLE]=TRUE;d.s.rs[D3DRS_STENCILFUNC]=D3DCMP_EQUAL;d.s.rs[D3DRS_STENCILREF]=0;d.s.rs[D3DRS_STENCILMASK]=0x40;
        const State before=d.s;assert(!marker.beginDraw(false,depthWriter,true,d.s.rt[0],d.s.depth));
        assert(d.s==before&&!marker.valid()&&!(static_cast<Surface*>(d.s.depth)->bits&0x40));assert(d.stencil(true));
    }
    {Device d;NorthlightAOActorStencil marker(&d);marker.beginFrame(true);d.s.rs[D3DRS_STENCILREF]=0xa5;d.s.rs[D3DRS_STENCILMASK]=0x33;
    const State before=d.s;assert(marker.beginDraw(true,true,true,d.s.rt[0],d.s.depth));assert(d.s.rs[D3DRS_STENCILREF]==0xe5&&d.s.rs[D3DRS_STENCILMASK]==0x33);marker.endDraw(d.native());assert(d.s==before);}
    {Device d;NorthlightAOActorStencil marker(&d);marker.beginFrame(true);stamp(d,marker,true);auto* oldDepth=static_cast<Surface*>(d.s.depth);
    d.s.rt[0]=d.surface();d.s.depth=d.surface(75);const State other=d.s;marker.invalidate();assert(d.s==other&&!(oldDepth->bits&0x40)&&static_cast<Surface*>(d.s.depth)->bits==0xe5);}
    std::puts("Actor stencil: NameMask128 composition, all other bits preserved, blend/native MRT accepted, matching MSAA; unknown stencil/depth/domain refused");
}
static void failures() {
    unsigned limit=0;{Device d;NorthlightAOActorStencil marker(&d);marker.beginFrame(true);stamp(d,marker,true);assert(marker.freeze(d.s.depth));limit=d.c.calls;}
    for(unsigned fail=1;fail<=limit;++fail) {
        Device d;const State before=d.s;
        {NorthlightAOActorStencil marker(&d);marker.beginFrame(true);d.c.failAt=fail;marker.beginDraw(true,true,true,d.s.rt[0],d.s.depth);marker.endDraw(d.native());marker.freeze(d.s.depth);assert(d.s==before);}
        assert(d.c.failed==1&&d.nativeDraws==1);for(unsigned i=0;i<4;++i)assert(d.arena[i]->refs==1);for(unsigned i=4;i<d.arena.size();++i)assert(d.arena[i]->refs==0);
    }
    std::printf("Actor stencil failures: %u getter/create/stamp/private-pass/restore failures, native draw/state and resource refs preserved\n",limit);
}
int main(){lifecycle();composition();failures();}
