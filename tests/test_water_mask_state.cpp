// Native equivalence test of the 0.3.141 water-mask capture against the verbatim
// 0.3.140 one (support/water_mask/water_renderer_0_3_140.h: SavedState with a
// pooled D3DSBT_ALL block). Both run through the production ExtensionDevice
// (device_mirror.h) over identical full-state fake D3D9 backends and the same
// randomized game script. After every step the complete backend state (all
// bindings, render/sampler/stage states, viewport, scissor, constants, streams),
// every Clear/draw with the full state it saw, and the object reference counts
// must be identical. Covered: all four draw kinds, leaked mask bindings, game
// state blocks captured with a leaked mask and applied later, and a failure
// injected at every Set/Clear/draw call of the mask pass in turn. Failing
// getters (not a DXVK case) must leave the device state untouched.
// The new path must also leave the mirror consistent (audit). No graphics API,
// driver, Wine or game is involved.
// 0.3.214 (water MRT): the second-draw pass above stays the fallback, so its equivalence runs with the
// renderer's MRT variants off (the constructor flag). mrtPath() below drives the MRT path on the same
// fake backend: no render target 0 / depth switch and no second draw per water draw, render target 1
// bound lazily and kept across consecutive draws, the shaders and write mask swapped for the draw only,
// the exact device state after unbind, the once-per-frame clear / resolve / decode, and the fallbacks.
#define NORTHLIGHT_DEVICE_MIRROR_TEST_API
#include <d3d9.h>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
static unsigned logLines=0;
static void logf(const char*,...){++logLines;}
template<class T> static void drop(T*& p){if(p){p->Release();p=nullptr;}}
#include "device_mirror.h"
#include "saved_state.h"
#include "water_shader_signatures.h"   // support/water_mask stub: synthetic programs
#include "water_compiled_shaders.h"
namespace legacy {
#include "water_renderer_0_3_140.h"
}
#include "water_renderer.h"

struct Owned {virtual ~Owned()=default;LONG refs=1;int id=0;};
template<class I> struct Fake final:I,Owned {
    ULONG AddRef()override{return ULONG(++refs);}
    ULONG Release()override{assert(refs>0);return ULONG(--refs);} // arena-owned: never freed early
};
struct FakeSurface final:IDirect3DSurface9,Owned {
    D3DSURFACE_DESC desc;
    ULONG AddRef()override{return ULONG(++refs);}ULONG Release()override{assert(refs>0);return ULONG(--refs);}
    HRESULT GetDesc(D3DSURFACE_DESC* out)override{if(!out)return D3DERR_INVALIDCALL;*out=desc;return D3D_OK;}
};
struct FakeTexture final:IDirect3DTexture9,Owned {
    FakeSurface* level=nullptr;
    ULONG AddRef()override{return ULONG(++refs);}ULONG Release()override{assert(refs>0);return ULONG(--refs);}
    HRESULT GetSurfaceLevel(UINT n,IDirect3DSurface9** out)override{if(n||!out)return D3DERR_INVALIDCALL;level->AddRef();*out=level;return D3D_OK;}
};
struct State {
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;DWORD fvf=0;
    IDirect3DIndexBuffer9* ib=nullptr;IDirect3DVertexBuffer9* vb[16]={};UINT off[16]={},stride[16]={},freq[16]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
    IDirect3DBaseTexture9* tex[16]={};IDirect3DSurface9* rt[4]={};IDirect3DSurface9* ds=nullptr;
    DWORD rs[256]={},samp[16][16]={},tss[8][33]={};D3DVIEWPORT9 vp{};RECT scissor{};
    float vsf[256][4]={},psf[224][4]={};int vsi[16][4]={};BOOL vsb[16]={};
};
// Getter failure modes. Pure: what a native D3D9 PUREDEVICE refuses.
enum GetFail : unsigned {GetsWork,PureDevice,ScissorGet,TextureGet,StreamGets,TargetGet,GetFailModes};
struct Backend;
struct Block final:IDirect3DStateBlock9 {
    LONG refs=1;Backend* owner;State saved;
    explicit Block(Backend* b);
    ULONG AddRef()override{return ULONG(++refs);}ULONG Release()override{auto n=--refs;if(!n)delete this;return ULONG(n);}
    HRESULT GetDevice(IDirect3DDevice9**)override;HRESULT Capture()override;HRESULT Apply()override;
};
struct Backend final:IDirect3DDevice9 {
    LONG refs=1;State s;
    std::vector<std::unique_ptr<Owned>> arena;int nextId=1;std::map<const void*,int> ids;
    std::vector<std::vector<long long>> events;
    unsigned gets=0,sets=0,applies=0,captures=0,blocks=0,aliases=0;bool failCreate=false,refuseColorFill=false,failStretch=false;unsigned failDraw=0;
    unsigned getFail=GetsWork,failStage=0;
    // Mask-pass failure injection: the failAt-th Set/Clear/draw while armed fails
    // without changing state. The mask draw disarms and records how many calls
    // preceded it (restores differ by design and are never injected).
    bool armed=false;unsigned failAt=0,mutations=0,injected=0,forwardCalls=0;
    std::vector<std::string>* trace=nullptr;
    template<class T> T* make(T* p){p->id=nextId++;arena.emplace_back(p);ids[static_cast<const void*>(p)]=p->id;return p;}
    FakeSurface* surface(UINT w,UINT h,D3DFORMAT f,DWORD usage){auto* p=make(new FakeSurface);p->desc.Width=w;p->desc.Height=h;p->desc.Format=f;p->desc.Usage=usage;return p;}
    FakeTexture* texture(UINT w,UINT h,D3DFORMAT f,DWORD usage){auto* t=make(new FakeTexture);t->level=surface(w,h,f,usage);return t;}
    long long id(const void* p)const{if(!p)return 0;auto it=ids.find(p);return it==ids.end()?-1:it->second;}
    std::vector<long long> snapshot()const{
        std::vector<long long> o;auto f=[&](float v){std::uint32_t b;std::memcpy(&b,&v,4);o.push_back(b);};
        o.push_back(id(s.vs));o.push_back(id(s.ps));o.push_back(id(s.decl));o.push_back(s.fvf);o.push_back(id(s.ib));
        for(unsigned i=0;i<16;++i){o.push_back(id(s.vb[i]));o.push_back(s.off[i]);o.push_back(s.stride[i]);o.push_back(s.freq[i]);o.push_back(id(s.tex[i]));}
        for(auto* t:s.rt)o.push_back(id(t));o.push_back(id(s.ds));
        for(auto v:s.rs)o.push_back(v);for(auto& a:s.samp)for(auto v:a)o.push_back(v);for(auto& a:s.tss)for(auto v:a)o.push_back(v);
        o.push_back(s.vp.X);o.push_back(s.vp.Y);o.push_back(s.vp.Width);o.push_back(s.vp.Height);f(s.vp.MinZ);f(s.vp.MaxZ);
        o.push_back(s.scissor.left);o.push_back(s.scissor.top);o.push_back(s.scissor.right);o.push_back(s.scissor.bottom);
        for(auto& a:s.vsf)for(float v:a)f(v);for(auto& a:s.psf)for(float v:a)f(v);for(auto& a:s.vsi)for(int v:a)o.push_back(v);for(auto v:s.vsb)o.push_back(v);
        return o;
    }
    // Draws also check the RT/sampler alias: target 0 must not be a bound texture's level.
    void event(long long kind,std::initializer_list<long long> args){
        if(kind!=5)for(auto* t:s.tex)for(auto& o:arena){auto* texture=dynamic_cast<FakeTexture*>(o.get());if(texture&&static_cast<IDirect3DBaseTexture9*>(texture)==t&&texture->level==s.rt[0])++aliases;}
        auto e=snapshot();e.insert(e.begin(),args);e.insert(e.begin(),kind);events.push_back(std::move(e));}
    bool mutate(const char* name,long long arg=-1){++sets;if(trace)trace->push_back(arg<0?std::string(name):std::string(name)+"("+std::to_string(arg)+")");
        if(armed&&++mutations==failAt){++injected;return false;}return true;}
    bool getFails(GetFail kind,unsigned arg=0)const{
        if(getFail==kind)return kind!=TextureGet&&kind!=TargetGet?true:arg==failStage;
        return false;}
    ULONG AddRef()override{return ULONG(++refs);}ULONG Release()override{assert(refs>1);return ULONG(--refs);}
    template<class T> static HRESULT give(T* p,T** out){if(!out)return D3DERR_INVALIDCALL;*out=p;if(p)p->AddRef();return D3D_OK;}
#define MUTATE(...) if(!mutate(__VA_ARGS__))return D3DERR_INVALIDCALL
    HRESULT SetVertexShader(IDirect3DVertexShader9* p)override{MUTATE("SetVertexShader");s.vs=p;return D3D_OK;}
    HRESULT GetVertexShader(IDirect3DVertexShader9** p)override{++gets;return give(s.vs,p);}
    HRESULT SetPixelShader(IDirect3DPixelShader9* p)override{MUTATE("SetPixelShader");s.ps=p;return D3D_OK;}
    HRESULT GetPixelShader(IDirect3DPixelShader9** p)override{++gets;return give(s.ps,p);}
    HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9* p)override{MUTATE("SetVertexDeclaration");s.decl=p;s.fvf=0;return D3D_OK;}
    HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9** p)override{++gets;return give(s.decl,p);}
    HRESULT SetFVF(DWORD v)override{MUTATE("SetFVF");s.fvf=v;s.decl=nullptr;return D3D_OK;}
    HRESULT GetFVF(DWORD* p)override{++gets;if(!p)return D3DERR_INVALIDCALL;*p=s.fvf;return D3D_OK;}
    HRESULT SetStreamSource(UINT n,IDirect3DVertexBuffer9* p,UINT off,UINT stride)override{MUTATE("SetStreamSource",n);if(n>=16)return D3DERR_INVALIDCALL;s.vb[n]=p;s.off[n]=off;s.stride[n]=stride;return D3D_OK;}
    HRESULT GetStreamSource(UINT n,IDirect3DVertexBuffer9** p,UINT* off,UINT* stride)override{++gets;if(getFails(StreamGets)||n>=16||!p||!off||!stride)return D3DERR_INVALIDCALL;*off=s.off[n];*stride=s.stride[n];return give(s.vb[n],p);}
    HRESULT SetStreamSourceFreq(UINT n,UINT v)override{MUTATE("SetStreamSourceFreq",n);if(n>=16)return D3DERR_INVALIDCALL;s.freq[n]=v;return D3D_OK;}
    HRESULT GetStreamSourceFreq(UINT n,UINT* v)override{++gets;if(n>=16||!v)return D3DERR_INVALIDCALL;*v=s.freq[n];return D3D_OK;}
    HRESULT SetIndices(IDirect3DIndexBuffer9* p)override{MUTATE("SetIndices");s.ib=p;return D3D_OK;}
    HRESULT GetIndices(IDirect3DIndexBuffer9** p)override{++gets;if(getFails(StreamGets))return D3DERR_INVALIDCALL;return give(s.ib,p);}
    HRESULT SetTexture(DWORD n,IDirect3DBaseTexture9* p)override{MUTATE("SetTexture",n);if(n>=16)return D3DERR_INVALIDCALL;s.tex[n]=p;return D3D_OK;}
    HRESULT GetTexture(DWORD n,IDirect3DBaseTexture9** p)override{++gets;if(n>=16||getFails(TextureGet,n))return D3DERR_INVALIDCALL;return give(s.tex[n],p);}
    HRESULT SetSamplerState(DWORD n,D3DSAMPLERSTATETYPE t,DWORD v)override{MUTATE("SetSamplerState",n);if(n>=16||t>=16)return D3DERR_INVALIDCALL;s.samp[n][t]=v;return D3D_OK;}
    HRESULT GetSamplerState(DWORD n,D3DSAMPLERSTATETYPE t,DWORD* v)override{++gets;if(getFails(PureDevice)||n>=16||t>=16||!v)return D3DERR_INVALIDCALL;*v=s.samp[n][t];return D3D_OK;}
    HRESULT SetRenderState(D3DRENDERSTATETYPE t,DWORD v)override{MUTATE("SetRenderState",t);if(t>=256)return D3DERR_INVALIDCALL;s.rs[t]=v;return D3D_OK;}
    HRESULT GetRenderState(D3DRENDERSTATETYPE t,DWORD* v)override{++gets;if(getFails(PureDevice)||t>=256||!v)return D3DERR_INVALIDCALL;*v=s.rs[t];return D3D_OK;}
    HRESULT SetTextureStageState(DWORD n,D3DTEXTURESTAGESTATETYPE t,DWORD v)override{MUTATE("SetTextureStageState",n);if(n>=8||t>=33)return D3DERR_INVALIDCALL;s.tss[n][t]=v;return D3D_OK;}
    HRESULT GetTextureStageState(DWORD n,D3DTEXTURESTAGESTATETYPE t,DWORD* v)override{++gets;if(getFails(PureDevice)||n>=8||t>=33||!v)return D3DERR_INVALIDCALL;*v=s.tss[n][t];return D3D_OK;}
    HRESULT SetViewport(const D3DVIEWPORT9* p)override{MUTATE("SetViewport");if(!p)return D3DERR_INVALIDCALL;s.vp=*p;return D3D_OK;}
    HRESULT GetViewport(D3DVIEWPORT9* p)override{++gets;if(!p)return D3DERR_INVALIDCALL;*p=s.vp;return D3D_OK;}
    HRESULT SetScissorRect(const RECT* p)override{MUTATE("SetScissorRect");if(!p)return D3DERR_INVALIDCALL;s.scissor=*p;return D3D_OK;}
    HRESULT GetScissorRect(RECT* p)override{++gets;if(getFails(PureDevice)||getFails(ScissorGet)||!p)return D3DERR_INVALIDCALL;*p=s.scissor;return D3D_OK;}
    // D3D9: target 0 cannot be null, and binding it resets viewport and scissor rect.
    HRESULT SetRenderTarget(DWORD n,IDirect3DSurface9* p)override{MUTATE("SetRenderTarget",n);if(n>=4||(!n&&!p))return D3DERR_INVALIDCALL;s.rt[n]=p;
        if(!n){D3DSURFACE_DESC d;p->GetDesc(&d);s.vp={0,0,d.Width,d.Height,0,1};s.scissor={0,0,LONG(d.Width),LONG(d.Height)};}return D3D_OK;}
    HRESULT GetRenderTarget(DWORD n,IDirect3DSurface9** p)override{++gets;if(n>=4||!p)return D3DERR_INVALIDCALL;if(getFails(TargetGet,n)){*p=nullptr;return D3DERR_INVALIDCALL;}
        give(s.rt[n],p);return s.rt[n]?D3D_OK:D3DERR_NOTFOUND;}
    HRESULT SetDepthStencilSurface(IDirect3DSurface9* p)override{MUTATE("SetDepthStencilSurface",p?1:0);s.ds=p;return D3D_OK;}
    HRESULT GetDepthStencilSurface(IDirect3DSurface9** p)override{++gets;if(!p)return D3DERR_INVALIDCALL;give(s.ds,p);return s.ds?D3D_OK:D3DERR_NOTFOUND;}
#define CONSTANTS(Name,Type,field,N) \
    HRESULT Set##Name(UINT first,const Type* data,UINT count)override{MUTATE("Set" #Name);if(!data||first>N||count>N-first)return D3DERR_INVALIDCALL;std::memcpy(&s.field[first],data,count*sizeof s.field[0]);return D3D_OK;} \
    HRESULT Get##Name(UINT first,Type* data,UINT count)override{++gets;if(getFails(PureDevice)||!data||first>N||count>N-first)return D3DERR_INVALIDCALL;std::memcpy(data,&s.field[first],count*sizeof s.field[0]);return D3D_OK;}
    CONSTANTS(VertexShaderConstantF,float,vsf,256)
    CONSTANTS(PixelShaderConstantF,float,psf,224)
    CONSTANTS(VertexShaderConstantI,int,vsi,16)
    CONSTANTS(VertexShaderConstantB,WINBOOL,vsb,16)
#undef CONSTANTS
    HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE t,IDirect3DStateBlock9** p)override{assert(t==D3DSBT_ALL);if(!p)return D3DERR_INVALIDCALL;++blocks;*p=new Block(this);return D3D_OK;}
    HRESULT BeginStateBlock()override{return D3DERR_INVALIDCALL;}
    HRESULT EndStateBlock(IDirect3DStateBlock9**)override{return D3DERR_INVALIDCALL;}
    HRESULT Reset(D3DPRESENT_PARAMETERS*)override{return D3DERR_INVALIDCALL;}
    HRESULT SetSoftwareVertexProcessing(WINBOOL)override{return D3DERR_INVALIDCALL;}
    HRESULT ProcessVertices(UINT,UINT,UINT,IDirect3DVertexBuffer9*,IDirect3DVertexDeclaration9*,DWORD)override{return D3DERR_INVALIDCALL;}
    HRESULT drawResult(const char* name){const bool ok=mutate(name);if(armed)forwardCalls=mutations;armed=false;if(!ok)return D3DERR_INVALIDCALL;if(failDraw){--failDraw;return D3DERR_INVALIDCALL;}return D3D_OK;}
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE t,UINT a,UINT b)override{HRESULT hr=drawResult("DrawPrimitive");event(1,{t,a,b,hr});return hr;}
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT a,UINT b,UINT c,UINT d,UINT e)override{HRESULT hr=drawResult("DrawIndexedPrimitive");event(2,{t,a,b,c,d,e,hr});return hr;}
    // D3D9: user-pointer draws leave stream 0 (and the index buffer) unbound.
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void*,UINT stride)override{HRESULT hr=drawResult("DrawPrimitiveUP");event(3,{t,n,stride,hr});if(SUCCEEDED(hr)){s.vb[0]=nullptr;s.off[0]=s.stride[0]=0;}return hr;}
    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT a,UINT b,UINT n,const void*,D3DFORMAT f,const void*,UINT stride)override{HRESULT hr=drawResult("DrawIndexedPrimitiveUP");event(4,{t,a,b,n,f,stride,hr});if(SUCCEEDED(hr)){s.vb[0]=nullptr;s.off[0]=s.stride[0]=0;s.ib=nullptr;}return hr;}
    HRESULT Clear(DWORD n,const D3DRECT*,DWORD flags,D3DCOLOR color,float z,DWORD stencil)override{MUTATE("Clear");std::uint32_t zb;std::memcpy(&zb,&z,4);event(5,{n,flags,color,zb,stencil});return D3D_OK;}
#undef MUTATE
    HRESULT CreateTexture(UINT w,UINT h,UINT,DWORD usage,D3DFORMAT f,D3DPOOL,IDirect3DTexture9** out,HANDLE*)override{if(failCreate){*out=nullptr;return E_OUTOFMEMORY;}*out=texture(w,h,f,usage);return D3D_OK;}
    HRESULT CreateDepthStencilSurface(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE,DWORD,WINBOOL,IDirect3DSurface9** out,HANDLE*)override{if(failCreate){*out=nullptr;return E_OUTOFMEMORY;}*out=surface(w,h,f,D3DUSAGE_DEPTHSTENCIL);return D3D_OK;}
    HRESULT CreateVertexShader(const DWORD*,IDirect3DVertexShader9** out)override{*out=make(new Fake<IDirect3DVertexShader9>);return D3D_OK;}
    HRESULT CreatePixelShader(const DWORD*,IDirect3DPixelShader9** out)override{*out=make(new Fake<IDirect3DPixelShader9>);return D3D_OK;}
    HRESULT StretchRect(IDirect3DSurface9* a,const RECT*,IDirect3DSurface9* b,const RECT*,D3DTEXTUREFILTERTYPE)override{if(failStretch)return D3DERR_INVALIDCALL;event(7,{id(a),id(b)});return D3D_OK;}
    // 0.3.214 (water MRT)
    HRESULT CreateRenderTarget(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE ms,DWORD q,WINBOOL,IDirect3DSurface9** out,HANDLE*)override{
        if(failCreate){*out=nullptr;return E_OUTOFMEMORY;}auto* t=surface(w,h,f,D3DUSAGE_RENDERTARGET);t->desc.MultiSampleType=ms;t->desc.MultiSampleQuality=q;*out=t;return D3D_OK;}
    HRESULT ColorFill(IDirect3DSurface9* t,const RECT*,D3DCOLOR c)override{if(refuseColorFill)return D3DERR_INVALIDCALL;if(!mutate("ColorFill"))return D3DERR_INVALIDCALL;event(6,{id(t),c});return D3D_OK;}
};
Block::Block(Backend* b):owner(b),saved(b->s){owner->AddRef();}
HRESULT Block::GetDevice(IDirect3DDevice9** p){return Backend::give(static_cast<IDirect3DDevice9*>(owner),p);}
HRESULT Block::Capture(){++owner->captures;saved=owner->s;return D3D_OK;}
// D3DSBT_ALL: everything except render targets and depth-stencil.
HRESULT Block::Apply(){++owner->applies;auto* rt0=owner->s.rt[0];auto* rt1=owner->s.rt[1];auto* rt2=owner->s.rt[2];auto* rt3=owner->s.rt[3];auto* ds=owner->s.ds;
    owner->s=saved;owner->s.rt[0]=rt0;owner->s.rt[1]=rt1;owner->s.rt[2]=rt2;owner->s.rt[3]=rt3;owner->s.ds=ds;return D3D_OK;}

constexpr UINT W=1280,H=720;
enum class Op {Game,Capture,EndFrame,Handout,Recover,Reset,GameBlock,ApplyBlock};
struct Step {Op op;unsigned seed=0;unsigned kind=0;bool failDraw=false,failCreate=false,leak=false;unsigned failAt=0;};
static const std::uint64_t kVertexHash=[]{for(auto& v:kWaterShaderVariants)if(v.vertex)return v.hash;return std::uint64_t(0);}();
static const std::uint64_t kPixelHash=[]{for(auto& v:kWaterShaderVariants)if(!v.vertex)return v.hash;return std::uint64_t(0);}();
// 0.3.161: the current renderer patches the originals itself and must land on the
// same mask words the legacy table hands over (checked in main()).
static const NorthlightWaterShaderIdentity kTestIdentities[]={
    {kVertexHash,NorthlightWaterShaderPatch::fnv1a64(kWaterTestMaskVS,std::size(kWaterTestMaskVS)),unsigned(std::size(kWaterTestMaskVS)),true},
    {kPixelHash,NorthlightWaterShaderPatch::fnv1a64(kWaterTestMaskPS,std::size(kWaterTestMaskPS)),unsigned(std::size(kWaterTestMaskPS)),false}};

struct Run {
    Backend b;DeviceMirror m;MirrorDevice game{&b,&m};ExtensionDevice* ext=new ExtensionDevice(&b,&m);
    FakeSurface *mainRT=nullptr,*mainDS=nullptr,*extraRT[2]={},*otherDS=nullptr;FakeTexture* gameTex[3]={};
    Fake<IDirect3DVertexShader9>* vs[2]={};Fake<IDirect3DPixelShader9>* ps[2]={};
    Fake<IDirect3DVertexBuffer9>* vb[2]={};Fake<IDirect3DIndexBuffer9>* ib[2]={};
    unsigned forwardedProbe=0,captureCalls=0,readFailures=0;size_t gameObjects=0;
    std::vector<IDirect3DStateBlock9*> blocks; // game state blocks (through the game-facing mirror layer)
    Run(){
        mainRT=b.surface(W,H,D3DFMT_A8R8G8B8,D3DUSAGE_RENDERTARGET);mainDS=b.surface(W,H,D3DFMT_D24S8,D3DUSAGE_DEPTHSTENCIL);
        for(auto& t:extraRT)t=b.surface(W,H,D3DFMT_A8R8G8B8,D3DUSAGE_RENDERTARGET);otherDS=b.surface(W,H,D3DFMT_D24S8,D3DUSAGE_DEPTHSTENCIL);
        for(auto& t:gameTex)t=b.texture(64,64,D3DFMT_A8R8G8B8,0);
        for(auto& p:vs)p=b.make(new Fake<IDirect3DVertexShader9>);for(auto& p:ps)p=b.make(new Fake<IDirect3DPixelShader9>);
        for(auto& p:vb)p=b.make(new Fake<IDirect3DVertexBuffer9>);for(auto& p:ib)p=b.make(new Fake<IDirect3DIndexBuffer9>);
        gameObjects=b.arena.size();game.SetRenderTarget(0,mainRT);game.SetDepthStencilSurface(mainDS);
    }
    ~Run(){for(auto* b:blocks)b->Release();ext->Release();}
    // The game's own state before a draw, through the game-facing mirror layer.
    void randomizeGame(unsigned seed){
        std::mt19937 r(seed);auto pick=[&](unsigned n){return unsigned(r()%n);};
        game.SetRenderTarget(0,mainRT);
        for(unsigned i=1;i<4;++i)game.SetRenderTarget(i,pick(3)==0?extraRT[i&1]:nullptr);
        game.SetDepthStencilSurface(pick(12)==0?otherDS:mainDS);
        const float zs[][2]={{0,1},{0,1},{0,1},{.25f,.75f}};const auto z=zs[pick(4)];
        D3DVIEWPORT9 vp{0,0,W,H,z[0],z[1]};if(pick(16)==0)vp.X=4;game.SetViewport(&vp);
        RECT sc{LONG(pick(64)),LONG(pick(64)),LONG(W-pick(64)),LONG(H-pick(64))};if(pick(3))game.SetScissorRect(&sc);
        for(unsigned i=0;i<24;++i)game.SetRenderState(pick(210),r());
        for(D3DRENDERSTATETYPE t:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_COLORWRITEENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE})if(pick(2))game.SetRenderState(t,pick(16));
        for(unsigned i=0;i<16;++i)if(pick(2))game.SetTexture(i,pick(3)?static_cast<IDirect3DBaseTexture9*>(gameTex[pick(3)]):nullptr);
        for(unsigned i=0;i<6;++i)game.SetSamplerState(pick(16),pick(14),pick(8));
        for(unsigned i=0;i<4;++i)game.SetTextureStageState(pick(8),pick(33),pick(8));
        if(pick(2))game.SetStreamSource(0,vb[pick(2)],pick(4)*16,32);
        if(pick(2))game.SetIndices(ib[pick(2)]);
        game.SetVertexShader(pick(8)?vs[0]:vs[1]);game.SetPixelShader(pick(8)?ps[0]:ps[1]);
        float c[8][4];for(auto& row:c)for(auto& v:row)v=float(r()%1000)/7.f;game.SetVertexShaderConstantF(pick(248),&c[0][0],8);
        game.SetPixelShaderConstantF(pick(216),&c[0][0],8);
    }
    // World-capture style reads after a water draw: forwarded counts show mirror warmth.
    void probe(){
        const auto before=m.forwarded;IDirect3DVertexShader9* v=nullptr;IDirect3DPixelShader9* p=nullptr;IDirect3DBaseTexture9* t=nullptr;
        IDirect3DVertexDeclaration9* d=nullptr;DWORD x=0;float c[64][4];D3DVIEWPORT9 vp;
        ext->GetVertexShader(&v);ext->GetPixelShader(&p);ext->GetVertexDeclaration(&d);ext->GetTexture(0,&t);drop(v);drop(p);drop(d);drop(t);
        for(D3DRENDERSTATETYPE s:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ZENABLE,D3DRS_CULLMODE})ext->GetRenderState(s,&x);
        ext->GetSamplerState(0,D3DSAMP_ADDRESSU,&x);ext->GetSamplerState(0,D3DSAMP_ADDRESSV,&x);ext->GetViewport(&vp);ext->GetVertexShaderConstantF(0,&c[0][0],64);
        forwardedProbe+=unsigned(m.forwarded-before);
    }
};
static const float kVertices[64]={};static const unsigned short kIndices[6]={};
static HRESULT drawKind(IDirect3DDevice9* d,unsigned kind){
    switch(kind){case 0:return d->DrawPrimitive(D3DPT_TRIANGLELIST,3,2);case 1:return d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,1,0,4,6,2);
    case 2:return d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,kVertices,16);default:return d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,4,2,kIndices,D3DFMT_INDEX16,kVertices,16);}
}
// The production caller (renderer.cpp captureWater) gates on the main depth and
// fullViewport(); both versions sit behind the same gate.
template<class Water,bool Legacy> static void capture(Run& run,Water& water,const Step& step){
    run.b.forwardCalls=0;
    IDirect3DSurface9* depth=nullptr;const bool mainDepth=SUCCEEDED(run.ext->GetDepthStencilSurface(&depth))&&depth==run.mainDS;drop(depth);
    D3DVIEWPORT9 vp{};run.ext->GetViewport(&vp);
    if(!mainDepth||vp.X||vp.Y||vp.Width!=W||vp.Height!=H)return;
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;run.ext->GetVertexShader(&vs);run.ext->GetPixelShader(&ps);
    run.b.failCreate=step.failCreate;run.b.failDraw=step.failDraw;run.b.armed=true;run.b.failAt=step.failAt;run.b.mutations=0;
    auto* ext=run.ext;const unsigned kind=step.kind;auto draw=[&]{return drawKind(ext,kind);};
    if constexpr(Legacy)water.capture(vs,ps,W,H,draw);
    else{const unsigned before=water.frameReadFailures();
        water.capture(vs,ps,W,H,run.mainDS,vp,kind==2?NorthlightWaterRenderer::UserVertices:kind==3?NorthlightWaterRenderer::UserVerticesAndIndices:NorthlightWaterRenderer::NoUserPointer,run.m.invalidations,draw);
        run.readFailures+=water.frameReadFailures()-before;}
    ++run.captureCalls;run.b.failCreate=false;run.b.failDraw=0;run.b.armed=false;drop(vs);drop(ps);
}
template<class Water,bool Legacy> static void execute(Run& run,Water& water,const Step& step){
    switch(step.op){
    case Op::Game:run.randomizeGame(step.seed);break;
    case Op::Capture:capture<Water,Legacy>(run,water,step);run.probe();break;
    case Op::EndFrame:water.endFrame();break;
    case Op::Handout:{IDirect3DTexture9* mask=water.maskTextureForDepth(0,1);
        // A leaked host binding of the handed-out mask (the old scan's reason to exist).
        if(mask&&step.leak){run.game.SetTexture(step.seed%16,mask);run.game.SetTexture((step.seed/16)%16,mask);}break;}
    case Op::Recover:water.recover();break;
    case Op::Reset:water.reset();break;
    // A game ALL block captured now (possibly holding a leaked mask) and applied later.
    case Op::GameBlock:{IDirect3DStateBlock9* block=nullptr;if(SUCCEEDED(run.game.CreateStateBlock(D3DSBT_ALL,&block))&&block)run.blocks.push_back(block);break;}
    case Op::ApplyBlock:if(!run.blocks.empty())run.blocks[step.seed%run.blocks.size()]->Apply();break;
    }
}
static std::vector<Step> script(unsigned seed,unsigned steps){
    std::mt19937 r(seed);std::vector<Step> out;
    for(unsigned i=0;i<steps;++i){Step s;const unsigned p=r()%100;s.seed=r();
        s.op=p<30?Op::Game:p<80?Op::Capture:p<88?Op::EndFrame:p<94?Op::Handout:p<95?Op::Recover:p<96?Op::Reset:p<98?Op::GameBlock:Op::ApplyBlock;
        s.kind=r()%4;s.failDraw=r()%97==0;s.failCreate=r()%151==0;s.leak=r()%3==0;out.push_back(s);}
    return out;
}
struct Totals {unsigned steps=0,captures=0,maskDraws=0,leakRestores=0,oldApplies=0,oldForwarded=0,newForwarded=0,oldInvalidations=0,newInvalidations=0,newBlocks=0,injected=0,sweeps=0,gameApplies=0;};
// Runs one script on both versions and compares after every step.
// forward: per step, the mask-pass calls up to and including the draw (0: no draw).
static void compare(const std::vector<Step>& steps,unsigned getFail,unsigned failStage,Totals& total,std::vector<unsigned>* forward=nullptr){
    Run a,bRun;
    for(Run* r:{&a,&bRun}){r->b.getFail=getFail;r->b.failStage=failStage;}
    {legacy::NorthlightWaterRenderer oldWater(a.ext);NorthlightWaterRenderer newWater(bRun.ext,kTestIdentities,std::size(kTestIdentities),false);
    oldWater.registerVertex(a.vs[0],kVertexHash);oldWater.registerPixel(a.ps[0],kPixelHash);
    newWater.registerVertex(bRun.vs[0],kVertexHash,kWaterTestOriginalVS,std::size(kWaterTestOriginalVS));newWater.registerPixel(bRun.ps[0],kPixelHash,kWaterTestOriginalPS,std::size(kWaterTestOriginalPS));
    assert(a.b.snapshot()==bRun.b.snapshot());
    for(const auto& step:steps){
        const size_t eventsBefore=a.b.events.size();
        execute<legacy::NorthlightWaterRenderer,true>(a,oldWater,step);
        execute<NorthlightWaterRenderer,false>(bRun,newWater,step);
        // Full device state, every Clear/draw with the state it saw, identical objects.
        assert(a.b.snapshot()==bRun.b.snapshot());
        assert(a.b.events==bRun.b.events);
        assert(a.b.nextId==bRun.b.nextId&&a.b.aliases==bRun.b.aliases&&a.b.injected==bRun.b.injected);
        for(size_t i=0;i<a.b.arena.size();++i)assert(a.b.arena[i]->refs==bRun.b.arena[i]->refs);
        assert(oldWater.hasCapturedWater()==newWater.hasCapturedWater());
        assert(a.m.enabled==bRun.m.enabled);
        // Getters work: the new path's mirror is still exact without any Apply.
        if(getFail==GetsWork)assert(!bRun.ext->audit()&&bRun.m.enabled&&!a.ext->audit()&&a.m.enabled);
        ++total.steps;
        if(forward)forward->push_back(step.op==Op::Capture?a.b.forwardCalls:0);
        if(step.op==Op::Capture){assert(a.b.forwardCalls==bRun.b.forwardCalls);++total.captures;bool drew=false;
            for(size_t e=eventsBefore;e<a.b.events.size();++e)if(a.b.events[e][0]!=5){++total.maskDraws;drew=true;}
            // The draw saw no leaked mask; the host binding is back afterwards.
            const long long mask=a.b.id(oldWater.maskTextureForDepth(0,1));bool leaked=false;
            for(unsigned s=0;s<16;++s)leaked=leaked||(mask>0&&a.b.id(a.b.s.tex[s])==mask);
            total.leakRestores+=drew&&leaked;
        }
    }}
    // No draw ever aliases, except where both versions cannot see the leak: an
    // injected SetTexture failure, or a failing GetTexture on the leaked stage.
    if(!a.b.injected&&getFail!=TextureGet)assert(a.b.aliases==0);
    // Every game object is back to its creator's single reference.
    for(Run* r:{&a,&bRun})for(size_t i=0;i<r->gameObjects;++i)assert(r->b.arena[i]->refs==1);
    // Game blocks are the only state blocks the new path's backend ever sees.
    total.oldApplies+=a.b.applies-bRun.b.applies;total.newBlocks+=bRun.b.blocks-unsigned(bRun.blocks.size());total.gameApplies+=bRun.b.applies;total.injected+=a.b.injected;
    total.oldForwarded+=a.forwardedProbe;total.newForwarded+=bRun.forwardedProbe;
    total.oldInvalidations+=unsigned(a.m.invalidations);total.newInvalidations+=unsigned(bRun.m.invalidations);
}
// Directed: a leaked mask on stages 2 and 11 is unbound for the mask draw and
// rebound afterwards; the scan then runs on every draw (flag stays set). With no
// leak only the first draw after a handout scans. Also the exact call sequence.
static void directed(){
    Run run;NorthlightWaterRenderer water(run.ext,kTestIdentities,std::size(kTestIdentities),false);water.registerVertex(run.vs[0],kVertexHash,kWaterTestOriginalVS,std::size(kWaterTestOriginalVS));water.registerPixel(run.ps[0],kPixelHash,kWaterTestOriginalPS,std::size(kWaterTestOriginalPS));
    run.randomizeGame(7);run.game.SetVertexShader(run.vs[0]);run.game.SetPixelShader(run.ps[0]);run.game.SetDepthStencilSurface(run.mainDS);
    D3DVIEWPORT9 vp{0,0,W,H,0,1};run.game.SetViewport(&vp);
    Step step{Op::Capture};step.kind=1;capture<NorthlightWaterRenderer,false>(run,water,step);assert(water.frameCaptures()==1&&water.frameMaskScans()==1&&water.frameClears()==1);
    std::vector<std::string> trace;run.b.trace=&trace;capture<NorthlightWaterRenderer,false>(run,water,step);run.b.trace=nullptr;
    assert(water.frameCaptures()==2&&water.frameMaskScans()==1&&water.frameClears()==1);
    // Second draw of a frame (no Clear, no scan): the mask pass, then the exact restore.
    std::vector<std::string> expected={"SetDepthStencilSurface(0)","SetRenderTarget(1)","SetRenderTarget(2)","SetRenderTarget(3)","SetRenderTarget(0)","SetDepthStencilSurface(1)","SetViewport"};
    const unsigned states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_COLORWRITEENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE};
    for(unsigned s:states)expected.push_back("SetRenderState("+std::to_string(s)+")");
    for(const char* c:{"SetVertexShader","SetPixelShader","DrawIndexedPrimitive","SetDepthStencilSurface(0)","SetRenderTarget(1)","SetRenderTarget(2)","SetRenderTarget(3)","SetRenderTarget(0)","SetRenderTarget(1)","SetRenderTarget(2)","SetRenderTarget(3)","SetDepthStencilSurface(1)"})expected.push_back(c);
    for(unsigned s:states)expected.push_back("SetRenderState("+std::to_string(s)+")");
    for(const char* c:{"SetVertexShader","SetPixelShader","SetScissorRect","SetViewport"})expected.push_back(c);
    assert(trace==expected);
    std::printf("call sequence (%zu calls):",trace.size());for(auto& c:trace)std::printf(" %s",c.c_str());std::printf("\n");
    IDirect3DTexture9* mask=water.maskTextureForDepth(0,1);assert(mask);
    water.endFrame();run.game.SetTexture(2,mask);run.game.SetTexture(11,mask);run.game.SetViewport(&vp);
    const auto before=run.b.snapshot();const size_t events=run.b.events.size();step.kind=0;
    capture<NorthlightWaterRenderer,false>(run,water,step);
    assert(run.b.snapshot()==before&&run.b.s.tex[2]==mask&&run.b.s.tex[11]==mask);
    assert(run.b.events.size()==events+2&&run.b.events.back()[0]==1); // Clear + draw
    capture<NorthlightWaterRenderer,false>(run,water,step);assert(water.frameMaskScans()==2);
    run.game.SetTexture(2,nullptr);run.game.SetTexture(11,nullptr);
    capture<NorthlightWaterRenderer,false>(run,water,step);capture<NorthlightWaterRenderer,false>(run,water,step);assert(water.frameMaskScans()==3);
    assert(run.b.aliases==0);
    // A game block captured while the mask was leaked, applied after a clean scan
    // cleared the flag: the mirror invalidation forces a rescan, so no alias.
    run.game.SetTexture(5,mask);IDirect3DStateBlock9* block=nullptr;assert(SUCCEEDED(run.game.CreateStateBlock(D3DSBT_ALL,&block)));
    run.game.SetTexture(5,nullptr);capture<NorthlightWaterRenderer,false>(run,water,step);const unsigned scans=water.frameMaskScans();
    capture<NorthlightWaterRenderer,false>(run,water,step);assert(water.frameMaskScans()==scans); /* clean: flag cleared */
    block->Apply();assert(run.b.s.tex[5]==mask);capture<NorthlightWaterRenderer,false>(run,water,step);
    assert(water.frameMaskScans()==scans+1&&run.b.aliases==0&&run.b.s.tex[5]==mask);block->Release();
    std::printf("directed: game state block re-binding a leaked mask after a clean scan is rescanned (mirror invalidation), no alias\n");
    std::printf("directed: leaked mask on stages 2/11 unbound for both draws of a frame and restored; no RT/sampler alias; scans 1 per clean frame, every draw while leaked\n");
}
// UP draws: capture + the game's real UP draw ends in the same state as the real
// draw alone (stream 0 and, for indexed UP, the index buffer), also via fallback.
static void userPointerEndState(){
    for(unsigned getFail:{unsigned(GetsWork),unsigned(ScissorGet)})for(unsigned kind:{2u,3u})for(unsigned seed=1;seed<=50;++seed){
        Run alone,with;with.b.getFail=getFail;NorthlightWaterRenderer water(with.ext,kTestIdentities,std::size(kTestIdentities),false);water.registerVertex(with.vs[0],kVertexHash,kWaterTestOriginalVS,std::size(kWaterTestOriginalVS));water.registerPixel(with.ps[0],kPixelHash,kWaterTestOriginalPS,std::size(kWaterTestOriginalPS));
        for(Run* r:{&alone,&with}){r->randomizeGame(seed);r->game.SetVertexShader(r->vs[0]);r->game.SetPixelShader(r->ps[0]);r->game.SetDepthStencilSurface(r->mainDS);
            D3DVIEWPORT9 vp{0,0,W,H,0,1};r->game.SetViewport(&vp);r->game.SetStreamSource(0,r->vb[1],16,32);r->game.SetIndices(r->ib[1]);}
        Step step{Op::Capture};step.kind=kind;capture<NorthlightWaterRenderer,false>(with,water,step);assert(water.frameCaptures()==(getFail==GetsWork?1u:0u)); /* failed scissor read: skipped */
        assert(with.b.s.vb[0]==with.vb[1]&&(kind==2||with.b.s.ib==with.ib[1])); // restored before the real draw
        drawKind(&alone.game,kind);drawKind(&with.game,kind);
        assert(alone.b.snapshot()==with.b.snapshot()&&!with.b.s.vb[0]&&(kind==2||!with.b.s.ib));
    }
    std::printf("user-pointer draws: capture + real draw == real draw alone (stream 0 / indices), drawn and skipped\n");
}
// ---- 0.3.214 (water MRT) ----
static const DWORD kPs2Original[]={0xffff0200,0x0200001f,0x80000000,0xb00f0000,0x02000001,0x800f0800,0xb0e40000,0x0000ffff}; // ps_2_0: dcl t0; mov oC0, t0
static const std::uint64_t kPs2Hash=0x1234;
static std::vector<NorthlightWaterShaderIdentity> mrtIdentities(){
    std::vector<NorthlightWaterShaderIdentity> out(std::begin(kTestIdentities),std::end(kTestIdentities));
    std::vector<NorthlightWaterShaderPatch::Word> patched;assert(!NorthlightWaterShaderPatch::patch(kPs2Original,std::size(kPs2Original),patched));
    out.push_back({kPs2Hash,NorthlightWaterShaderPatch::fnv1a64(patched.data(),patched.size()),unsigned(patched.size()),false});return out;
}
struct MrtRun {
    Run run;std::vector<NorthlightWaterShaderIdentity> table=mrtIdentities();NorthlightWaterRenderer water;D3DVIEWPORT9 vp{0,0,W,H,0,1};
    Fake<IDirect3DPixelShader9>* ps2=nullptr;
    MrtRun():water(run.ext,table.data(),table.size(),true){
        water.registerVertex(run.vs[0],kVertexHash,kWaterTestOriginalVS,std::size(kWaterTestOriginalVS));water.registerPixel(run.ps[0],kPixelHash,kWaterTestOriginalPS,std::size(kWaterTestOriginalPS));
        ps2=run.b.make(new Fake<IDirect3DPixelShader9>);water.registerPixel(ps2,kPs2Hash,kPs2Original,std::size(kPs2Original));
        water.setMrtCaps(true);run.gameObjects=run.b.arena.size()-0; // the variants are the renderer's, not the game's: counted below
    }
    // The game's state for one water draw (blend, fog, sRGB), main target and depth, no extra targets.
    void game(DWORD blend=TRUE,DWORD src=D3DBLEND_SRCALPHA,DWORD dst=D3DBLEND_INVSRCALPHA,DWORD fog=FALSE,DWORD srgb=FALSE,bool useVs2=false){
        run.randomizeGame(5);run.game.SetRenderTarget(0,run.mainRT);for(unsigned i=1;i<4;++i)run.game.SetRenderTarget(i,nullptr);run.game.SetDepthStencilSurface(run.mainDS);run.game.SetViewport(&vp);
        run.game.SetVertexShader(run.vs[0]);run.game.SetPixelShader(useVs2?ps2:run.ps[0]);
        run.game.SetRenderState(D3DRS_ALPHABLENDENABLE,blend);run.game.SetRenderState(D3DRS_SRCBLEND,src);run.game.SetRenderState(D3DRS_DESTBLEND,dst);run.game.SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        run.game.SetRenderState(D3DRS_FOGENABLE,fog);run.game.SetRenderState(D3DRS_SRGBWRITEENABLE,srgb);run.game.SetRenderState(D3DRS_COLORWRITEENABLE1,15);
    }
    NorthlightWaterRenderer::Plan plan(){IDirect3DVertexShader9* v=nullptr;IDirect3DPixelShader9* p=nullptr;run.ext->GetVertexShader(&v);run.ext->GetPixelShader(&p);
        const auto r=water.plan(v,p,W,H,vp);drop(v);drop(p);return r;}
    bool begin(D3DMULTISAMPLE_TYPE ms=D3DMULTISAMPLE_4_SAMPLES){IDirect3DVertexShader9* v=nullptr;IDirect3DPixelShader9* p=nullptr;run.ext->GetVertexShader(&v);run.ext->GetPixelShader(&p);
        const bool r=water.beginMrt(v,p,W,H,ms,0,vp);drop(v);drop(p);return r;}
    size_t events(long long kind)const{size_t n=0;for(auto& e:run.b.events)n+=e[0]==kind;return n;}
};
static bool has(const std::vector<std::string>& trace,const std::string& name){for(auto& t:trace)if(t==name)return true;return false;}
static void mrtPath(){
    // 1. A frame of consecutive MRT draws on a 4x multisampled game target.
    {MrtRun m;m.game();assert(m.plan()==NorthlightWaterRenderer::Plan::Mrt&&!m.water.maskTextureForDepth(0,1));
    const auto before=m.run.b.snapshot();std::vector<std::string> trace;m.run.b.trace=&trace;
    assert(m.begin());
    assert((trace==std::vector<std::string>{"ColorFill","SetRenderTarget(1)","SetRenderState(190)","SetVertexShader","SetPixelShader"}));   // clear, bind, write mask, shaders: no target 0 / depth switch
    auto* twin=dynamic_cast<FakeSurface*>(m.run.b.s.rt[1]);assert(twin&&twin->desc.MultiSampleType==D3DMULTISAMPLE_4_SAMPLES&&twin->desc.Format==D3DFMT_G32R32F);
    assert(m.run.b.s.rt[0]==m.run.mainRT&&m.run.b.s.ds==m.run.mainDS&&m.run.b.s.rs[190]==15&&m.run.b.s.vs!=m.run.vs[0]&&m.run.b.s.ps!=m.run.ps[0]&&m.run.b.s.vs&&m.run.b.s.ps);
    const long long variantVs=m.run.b.id(m.run.b.s.vs),variantPs=m.run.b.id(m.run.b.s.ps);
    const size_t drawsBefore=m.run.b.events.size();assert(SUCCEEDED(drawKind(m.run.ext,1)));
    const auto& drawEvent=m.run.b.events[drawsBefore];assert(drawEvent[0]==2&&m.run.b.events.size()==drawsBefore+1);   // exactly the game's one draw, seen with the MRT bindings
    trace.clear();m.water.endMrt(true);
    assert((trace==std::vector<std::string>{"SetPixelShader","SetVertexShader","SetRenderState(190)"}));
    assert(m.run.b.s.vs==m.run.vs[0]&&m.run.b.s.ps==m.run.ps[0]&&m.water.isMrtBound()&&m.water.frameMrtDraws()==1);
    // Second and third consecutive water draws: the binding stays, nothing else is switched.
    for(unsigned k=0;k<2;++k){trace.clear();assert(m.begin());assert((trace==std::vector<std::string>{"SetRenderState(190)","SetVertexShader","SetPixelShader"}));
        assert(m.run.b.id(m.run.b.s.vs)==variantVs&&m.run.b.id(m.run.b.s.ps)==variantPs&&m.run.b.s.rt[1]==twin);assert(SUCCEEDED(drawKind(m.run.ext,k)));trace.clear();m.water.endMrt(true);}
    assert(m.water.frameMrtDraws()==3&&m.water.frameCaptures()==0&&m.events(6)==1&&m.events(5)==0);   // one clear, no second draw, no Clear call
    m.run.b.trace=nullptr;
    // Lazily unbound: the device is exactly the game's state again.
    m.water.unbindMrt();assert(!m.water.isMrtBound()&&m.run.b.snapshot()==before);
    // Before the resolve nothing is handed out; the resolve: twin -> data texture, mask clear, one decode pass, all state back.
    assert(!m.water.maskTextureForDepth(0,1));
    const size_t stretchBefore=m.events(7),fillsBefore=m.events(6),drawsAtResolve=m.run.b.events.size();trace.clear();m.run.b.trace=&trace;
    assert(m.water.resolveMask());m.run.b.trace=nullptr;
    assert(m.events(7)==stretchBefore+1&&m.events(6)==fillsBefore+1&&m.run.b.events.back()[0]==3);   // StretchRect(twin->raw), ColorFill(mask), the decode quad (DrawPrimitiveUP)
    assert(m.run.b.snapshot()==before&&m.run.b.s.rt[1]==nullptr);
    IDirect3DTexture9* mask=m.water.maskTextureForDepth(0,1);assert(mask&&!m.water.maskTextureForDepth(.25f,.75f));   // the min/max Z contract of the handout
    assert(m.water.hasCapturedWater());
    // The next frame starts empty and clears again before the first draw.
    m.water.endFrame();assert(!m.water.maskTextureForDepth(0,1)&&m.water.frameMrtDraws()==0);
    const size_t fills=m.events(6);m.game();assert(m.begin());assert(m.events(6)==fills+1);m.water.endMrt(true);m.water.unbindMrt();
    (void)drawsAtResolve;}
    // 2. Not multisampled: render target 1 is the data texture itself, no StretchRect.
    {MrtRun m;m.game();assert(m.begin(D3DMULTISAMPLE_NONE));auto* t=dynamic_cast<FakeSurface*>(m.run.b.s.rt[1]);assert(t&&t->desc.MultiSampleType==D3DMULTISAMPLE_NONE);
    drawKind(m.run.ext,0);m.water.endMrt(true);m.water.unbindMrt();assert(m.water.resolveMask()&&m.events(7)==0&&m.water.maskTextureForDepth(0,1));}
    // 3. Draws that must not write render target 1 are refused with the device untouched: additive and other blends, sRGB writes, table fog before ps_3_0.
    {MrtRun m;
    struct Case {DWORD blend,src,dst,fog,srgb;bool ps2,expect;};
    const Case cases[]={{FALSE,D3DBLEND_ONE,D3DBLEND_ONE,FALSE,FALSE,false,true},                    // no blend: whatever the factors
        {TRUE,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,FALSE,FALSE,false,true},{TRUE,D3DBLEND_ONE,D3DBLEND_INVSRCALPHA,FALSE,FALSE,false,true},
        {TRUE,D3DBLEND_ONE,D3DBLEND_ZERO,FALSE,FALSE,false,true},{TRUE,D3DBLEND_SRCALPHA,D3DBLEND_ZERO,FALSE,FALSE,false,true},
        {TRUE,D3DBLEND_SRCALPHA,D3DBLEND_ONE,FALSE,FALSE,false,false},{TRUE,D3DBLEND_ONE,D3DBLEND_ONE,FALSE,FALSE,false,false},{TRUE,D3DBLEND_ZERO,D3DBLEND_INVSRCALPHA,FALSE,FALSE,false,false},
        {TRUE,3,4,FALSE,FALSE,false,false},                                                          // SRCCOLOR / INVSRCCOLOR
        {FALSE,D3DBLEND_ONE,D3DBLEND_ONE,FALSE,TRUE,false,false},                                    // sRGB write
        {FALSE,D3DBLEND_ONE,D3DBLEND_ONE,TRUE,FALSE,false,true},                                     // fog with ps_3_0: the shader does its own
        {FALSE,D3DBLEND_ONE,D3DBLEND_ONE,TRUE,FALSE,true,false},{FALSE,D3DBLEND_ONE,D3DBLEND_ONE,FALSE,FALSE,true,true}};
    for(const auto& c:cases){
        m.game(c.blend,c.src,c.dst,c.fog,c.srgb,c.ps2);m.water.unbindMrt();const auto before=m.run.b.snapshot();const size_t events=m.run.b.events.size();
        const unsigned degraded=m.water.frameMrtDegraded();
        const bool got=m.begin();assert(got==c.expect);
        if(got){m.water.endMrt(false);m.water.unbindMrt();}
        else assert(m.run.b.snapshot()==before&&m.run.b.events.size()==events&&m.water.frameMrtDegraded()==degraded+1);
        assert(m.run.b.snapshot()==before);}
    // A separate blend of the alpha channel does not matter (G32R32F has none): not read at all.
    }
    // 4. A refused ColorFill falls back to a target swap that restores target 0, viewport and scissor.
    {MrtRun m;m.game();m.run.b.refuseColorFill=true;const auto before=m.run.b.snapshot();std::vector<std::string> trace;m.run.b.trace=&trace;
    assert(m.begin());m.run.b.trace=nullptr;assert(has(trace,"SetRenderTarget(0)")&&has(trace,"Clear")&&m.run.b.s.rt[0]==m.run.mainRT&&m.events(5)==1);
    m.water.endMrt(true);m.water.unbindMrt();assert(m.run.b.snapshot()==before);}
    // 4b. 0.3.215: the bind and run counters: a run is the MRT draws made under one bind of render target 1, the longest run is kept per frame.
    {MrtRun m;m.game();assert(m.water.frameMrtBinds()==0&&m.water.frameMrtLongestRun()==0);
    assert(m.begin());m.water.endMrt(true);assert(m.begin());m.water.endMrt(true);assert(m.begin());m.water.endMrt(true);m.water.unbindMrt();
    assert(m.water.frameMrtBinds()==1&&m.water.frameMrtLongestRun()==3&&m.water.frameMrtDraws()==3);
    assert(m.begin());m.water.endMrt(true);m.water.unbindMrt();
    assert(m.water.frameMrtBinds()==2&&m.water.frameMrtLongestRun()==3&&m.water.frameMrtDraws()==4);
    m.water.endFrame();assert(m.water.frameMrtBinds()==0&&m.water.frameMrtLongestRun()==0);}
    // 5. Failures leave the second-draw pass: no resources, then no MRT plan for the session; a failing resolve likewise; state untouched.
    {MrtRun m;m.game();m.run.b.failCreate=true;const auto before=m.run.b.snapshot();assert(!m.begin()&&m.run.b.snapshot()==before);m.run.b.failCreate=false;
    assert(m.plan()==NorthlightWaterRenderer::Plan::Redraw&&!m.begin());}
    {MrtRun m;m.game();assert(m.begin());drawKind(m.run.ext,0);m.water.endMrt(true);m.water.unbindMrt();const auto before=m.run.b.snapshot();m.run.b.failStretch=true;
    assert(!m.water.resolveMask()&&!m.water.maskTextureForDepth(0,1)&&m.run.b.snapshot()==before&&m.plan()==NorthlightWaterRenderer::Plan::Redraw);}
    // 6. Caps off, or variants off: every water draw is a second draw; an unrecognised shader or a viewport that is not the target's is skipped as capture() does.
    {MrtRun m;m.game();m.water.setMrtCaps(false);assert(m.plan()==NorthlightWaterRenderer::Plan::Redraw&&!m.begin());
    m.water.setMrtCaps(true);assert(m.plan()==NorthlightWaterRenderer::Plan::Mrt);m.vp.X=4;assert(m.plan()==NorthlightWaterRenderer::Plan::Skip);m.vp.X=0;
    m.run.game.SetPixelShader(m.run.ps[1]);assert(m.plan()==NorthlightWaterRenderer::Plan::Skip);
    Run plain;std::vector<NorthlightWaterShaderIdentity> t=mrtIdentities();NorthlightWaterRenderer off(plain.ext,t.data(),t.size(),false);
    off.registerVertex(plain.vs[0],kVertexHash,kWaterTestOriginalVS,std::size(kWaterTestOriginalVS));off.registerPixel(plain.ps[0],kPixelHash,kWaterTestOriginalPS,std::size(kWaterTestOriginalPS));off.setMrtCaps(true);
    D3DVIEWPORT9 vp{0,0,W,H,0,1};assert(off.plan(plain.vs[0],plain.ps[0],W,H,vp)==NorthlightWaterRenderer::Plan::Redraw);}
    // 7. Mixed frame: an MRT draw, then a second draw (another water draw the MRT conditions refused), then the resolve: the legacy clear stands, the decode adds to it.
    {MrtRun m;m.game();assert(m.begin());drawKind(m.run.ext,0);m.water.endMrt(true);m.water.unbindMrt();
    m.game(TRUE,D3DBLEND_ONE,D3DBLEND_ONE);assert(!m.begin());
    IDirect3DVertexShader9* v=nullptr;IDirect3DPixelShader9* p=nullptr;m.run.ext->GetVertexShader(&v);m.run.ext->GetPixelShader(&p);
    m.water.capture(v,p,W,H,m.run.mainDS,m.vp,NorthlightWaterRenderer::NoUserPointer,m.run.m.invalidations,[&]{return drawKind(m.run.ext,0);});drop(v);drop(p);
    assert(m.water.frameCaptures()==1&&m.water.frameClears()==1&&m.water.frameMrtDraws()==1&&m.water.frameMrtDegraded()==1);
    const size_t fills=m.events(6);assert(m.water.resolveMask()&&m.events(6)==fills&&m.water.maskTextureForDepth(0,1));   // no second mask clear over the second draw's data
    // the viewport range of the frame holds for both paths
    D3DVIEWPORT9 other{0,0,W,H,.25f,.75f};assert(m.water.plan(m.run.vs[0],m.run.ps[0],W,H,other)==NorthlightWaterRenderer::Plan::Skip);}
    // 8. A change of the game's multisampling drops the twin and re-creates it; a resolution change re-creates everything. Render target 1 is never left bound to a dropped target.
    {MrtRun m;m.game();assert(m.begin());m.water.endMrt(true);auto* first=m.run.b.s.rt[1];m.water.unbindMrt();m.water.endFrame();
    assert(m.begin(D3DMULTISAMPLE_NONE)&&m.run.b.s.rt[1]!=first);m.water.endMrt(true);m.water.unbindMrt();m.water.endFrame();
    assert(m.begin(D3DMULTISAMPLE_4_SAMPLES));dynamic_cast<FakeSurface*>(m.run.b.s.rt[1])->desc.MultiSampleType==D3DMULTISAMPLE_4_SAMPLES?void():abort();m.water.endMrt(true);m.water.unbindMrt();}
    std::printf("water MRT: draws write render target 1 with no target 0 / depth switch and no second draw, the binding is kept across draws and unbound to the exact game state, "
                "blend/fog/sRGB refusals leave the device untouched, the clear and resolve happen once per frame, failures fall back to the second-draw pass\n");
}

int main(){
    assert(kVertexHash&&kPixelHash);
    {std::vector<NorthlightWaterShaderPatch::Word> vsMask,psMask;   // runtime patch == legacy table words
    assert(!NorthlightWaterShaderPatch::patch(kWaterTestOriginalVS,std::size(kWaterTestOriginalVS),vsMask)&&vsMask==std::vector<NorthlightWaterShaderPatch::Word>(std::begin(kWaterTestMaskVS),std::end(kWaterTestMaskVS)));
    assert(!NorthlightWaterShaderPatch::patch(kWaterTestOriginalPS,std::size(kWaterTestOriginalPS),psMask)&&psMask==std::vector<NorthlightWaterShaderPatch::Word>(std::begin(kWaterTestMaskPS),std::end(kWaterTestMaskPS)));
    Run run;NorthlightWaterRenderer water(run.ext,kTestIdentities,std::size(kTestIdentities),false);
    water.registerVertex(run.vs[0],kVertexHash,kWaterTestOriginalVS,std::size(kWaterTestOriginalVS));water.registerPixel(run.ps[0],kPixelHash,kWaterTestOriginalPS,std::size(kWaterTestOriginalPS));
    assert(water.hasVertex(run.vs[0])&&water.usable());
    // A hash hit whose bytes do not patch to the recorded variant skips that shader only.
    water.registerVertex(run.vs[1],kVertexHash,kWaterTestOriginalPS,std::size(kWaterTestOriginalPS));
    assert(!water.hasVertex(run.vs[1])&&water.hasVertex(run.vs[0])&&water.usable());}
    directed();
    userPointerEndState();
    mrtPath();
    Totals total;
    for(unsigned seed=1;seed<=400;++seed)compare(script(seed,400),GetsWork,0,total);
    assert(total.maskDraws>1000&&total.leakRestores>10&&total.newBlocks==0&&total.oldApplies>0&&total.gameApplies>100);
    std::printf("equivalence: 400 seeds, %u steps, %u captures, %u mask draws (%u with a leaked mask restored): full backend state, draw/clear state and refcounts identical to 0.3.140\n",
        total.steps,total.captures,total.maskDraws,total.leakRestores);
    std::printf("0.3.140: %u state-block Applies, %u mirror invalidations, %u forwarded probe reads\n",total.oldApplies,total.oldInvalidations,total.oldForwarded);
    std::printf("0.3.141: %u state blocks created (%u game block Applies in the script), %u mirror invalidations, %u forwarded probe reads\n",total.newBlocks,total.gameApplies,total.newInvalidations,total.newForwarded);
    // Failing getters (not DXVK; e.g. a native pure device): every capture leaves
    // the full device state as it found it, and a failed save read draws nothing.
    {unsigned skipped=0,drawn=0;
    for(unsigned mode=PureDevice;mode<GetFailModes;++mode)for(unsigned seed=1;seed<=40;++seed){
        Run run;run.b.getFail=mode;run.b.failStage=seed%4;NorthlightWaterRenderer water(run.ext,kTestIdentities,std::size(kTestIdentities),false);water.registerVertex(run.vs[0],kVertexHash,kWaterTestOriginalVS,std::size(kWaterTestOriginalVS));water.registerPixel(run.ps[0],kPixelHash,kWaterTestOriginalPS,std::size(kWaterTestOriginalPS));
        for(const auto& step:script(1000+seed,300)){
            if(step.op!=Op::Capture||step.failDraw||step.failCreate){execute<NorthlightWaterRenderer,false>(run,water,step);continue;}
            const auto before=run.b.snapshot();const size_t events=run.b.events.size();const unsigned failures=run.readFailures;
            capture<NorthlightWaterRenderer,false>(run,water,step);
            if(step.kind<2||run.readFailures!=failures)assert(run.b.snapshot()==before); /* UP kinds: stream 0 restored for the game's own UP draw */
            if(run.readFailures!=failures){assert(run.b.events.size()==events);++skipped;}else drawn+=run.b.events.size()>events;}
        for(size_t i=0;i<run.gameObjects;++i)assert(run.b.arena[i]->refs==1||run.b.arena[i]->refs>1);}
    assert(skipped>1000&&drawn>100);
    std::printf("failing getters: %u modes x 40 seeds: %u captures skipped with state untouched, %u drawn and restored\n",unsigned(GetFailModes-1),skipped,drawn);}
    // A failure injected at each Set/Clear/draw of the mask pass in turn (all captures of 20 scripts).
    Totals sweep;
    for(unsigned seed=1;seed<=20;++seed){const auto base=script(2000+seed,60);const unsigned mode=GetsWork;
        Totals dry;std::vector<unsigned> forward;compare(base,mode,0,dry,&forward);
        for(size_t k=0;k<base.size();++k)for(unsigned n=1;n<=forward[k];++n){auto steps=base;steps[k].failAt=n;compare(steps,mode,0,sweep);++sweep.sweeps;}}
    assert(sweep.injected==sweep.sweeps&&sweep.sweeps>1000);
    std::printf("failure sweep: %u runs, %u injected Set/Clear/draw failures: state, draws and refcounts identical to 0.3.140\n",sweep.sweeps,sweep.injected);
    return 0;
}
