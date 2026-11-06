// Explicitly opt-in, independent hidden D3D9 device. Never loads the game/proxy.
#include <windows.h>
#include <d3d9.h>
#include "water_shader_patch.h"
#include "ao_actor_stencil.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace NorthlightWaterShaderPatch;
constexpr UINT W=96,H=64;
using ActorMarker=NorthlightAOActorStencil;
constexpr bool StencilMarker=true;
static void markerCaps(ActorMarker&,bool,bool){}
static void failure(const char* what,unsigned line){std::fprintf(stderr,"FAIL line %u: %s\n",line,what);std::exit(2);}
static void checked(HRESULT hr,const char* call,unsigned line){if(FAILED(hr)){std::fprintf(stderr,"HRESULT 0x%08lx ",static_cast<unsigned long>(hr));failure(call,line);}}
#define ok(call) checked((call),#call,__LINE__)
#undef assert
#define assert(condition) do{if(!(condition))failure(#condition,__LINE__);}while(false)
template<class T> struct Com {
    T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}operator T*()const{return p;}
    T** out(){assert(!p);return &p;}Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
};
static std::vector<Word> read(const std::string& path) {
    std::ifstream f(path,std::ios::binary);assert(f);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
    assert(bytes.size()>=4&&bytes.size()%4==0);std::vector<Word> out(bytes.size()/4);std::memcpy(out.data(),bytes.data(),bytes.size());return out;
}
static void instruction(std::vector<Word>& code,Word op,std::initializer_list<Word> operands) {
    code.push_back(op|(Word(operands.size())<<24));code.insert(code.end(),operands);
}
static std::vector<Word> pixel(unsigned major,bool kill=false,bool depth=false) {
    std::vector<Word> out={major==3?0xffff0300u:0xffff0200u};
    if(kill)instruction(out,81,{dst(2,0),0xbf000000,0x3f800000,0x3f800000,0x3f800000});
    else if(depth)instruction(out,81,{dst(2,0),0x3e800000,0,0,0});
    instruction(out,31,{major==3?0x8000000au:0x80000000u,dst(1,0)});
    if(kill) {
        instruction(out,31,{major==3?0x80000005u:0x80000000u,dst(major==3?1:3,major==3?1:0)});
        instruction(out,1,{dst(0,0),src(2,0)});
        instruction(out,2,{dst(0,0,1),src(major==3?1:3,major==3?1:0,0),src(2,0,0)});
        instruction(out,65,{dst(0,0)});
    }
    if(depth)instruction(out,1,{dst(9,0),src(2,0,0)});
    instruction(out,1,{dst(8,0),src(1,0)});out.push_back(0xffff);return out;
}
// Match every stock PS input semantic. Texture coordinates and colours come
// from the synthetic geometry; the original pixel shader remains untouched.
static std::vector<Word> vertex(const std::vector<Word>& ps) {
    const unsigned major=(ps[0]>>8)&255;std::vector<Word> out={major==3?0xfffe0300u:0xfffe0200u};
    instruction(out,81,{dst(2,0),0x3f000000,0x3f000000,0x3f000000,0x3f800000});
    instruction(out,31,{0x80000000,dst(1,0)}); // POSITION float4
    instruction(out,31,{0x8000000a,dst(1,1)}); // COLOR D3DCOLOR
    instruction(out,31,{0x80000005,dst(1,2)}); // TEXCOORD float4
    if(major==3) {
        instruction(out,31,{0x80000000,dst(6,0)});
        unsigned output=1;
        for(std::size_t p=1;p<ps.size()&&(ps[p]&65535)!=65535;) {
            const unsigned op=ps[p]&65535,n=op==65534?(ps[p]>>16)&32767:(ps[p]>>24)&15;
            if(op==31&&n==2&&regType(ps[p+2])==1)instruction(out,31,{ps[p+1],dst(6,output++)});p+=n+1;
        }
        instruction(out,1,{dst(6,0),src(1,0)});output=1;
        for(std::size_t p=1;p<ps.size()&&(ps[p]&65535)!=65535;) {
            const unsigned op=ps[p]&65535,n=op==65534?(ps[p]>>16)&32767:(ps[p]>>24)&15;
            if(op==31&&n==2&&regType(ps[p+2])==1)instruction(out,1,{dst(6,output++),src(1,(ps[p+1]&15)==10?1:2)});p+=n+1;
        }
    } else {
        instruction(out,1,{dst(4,0),src(1,0)});
        instruction(out,1,{dst(4,1),src(2,0,0)}); // scalar oFog uses the canonical full write-mask token
        for(unsigned i=0;i<2;++i)instruction(out,1,{dst(5,i),src(1,1)});
        for(unsigned i=0;i<8;++i)instruction(out,1,{dst(6,i),src(1,2)});
    }
    out.push_back(0xffff);return out;
}
struct Vertex {float x,y,z,w;DWORD colour;float u,v,q,r;};
static std::array<Vertex,4> quad(float left,float top,float right,float bottom,float z,DWORD low,DWORD high) {
    return {{{left,top,z,1,low,0,0,.5f,1},{right,top,z,1,high,1,0,.5f,1},
             {left,bottom,z,1,low,0,1,.5f,1},{right,bottom,z,1,high,1,1,.5f,1}}};
}
// Snapshot all bindings/constants plus the states the marker touches. Extra
// sampler/stream/depth/alpha/fog state catches unintended backend side effects.
struct State {
    std::vector<unsigned char> bytes;
    template<class T> void value(const T& v){const auto* p=reinterpret_cast<const unsigned char*>(&v);bytes.insert(bytes.end(),p,p+sizeof v);}
    template<class T,class F> void object(F get){T* p=nullptr;const HRESULT hr=get(&p);assert(SUCCEEDED(hr)||(hr==D3DERR_NOTFOUND&&!p));value(p);if(p)p->Release();}
    explicit State(IDirect3DDevice9* d) {
        D3DCAPS9 caps{};ok(d->GetDeviceCaps(&caps));
        for(unsigned n=0;n<std::min(4u,unsigned(caps.NumSimultaneousRTs));++n)object<IDirect3DSurface9>([&](auto p){return d->GetRenderTarget(n,p);});
        object<IDirect3DSurface9>([&](auto p){return d->GetDepthStencilSurface(p);});
        object<IDirect3DPixelShader9>([&](auto p){return d->GetPixelShader(p);});
        object<IDirect3DVertexShader9>([&](auto p){return d->GetVertexShader(p);});
        object<IDirect3DVertexDeclaration9>([&](auto p){return d->GetVertexDeclaration(p);});
        object<IDirect3DIndexBuffer9>([&](auto p){return d->GetIndices(p);});
        D3DVIEWPORT9 vp{};RECT scissor{};DWORD fvf=0;ok(d->GetViewport(&vp));ok(d->GetScissorRect(&scissor));ok(d->GetFVF(&fvf));value(vp);value(scissor);value(fvf);
        const D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,D3DRS_ALPHAFUNC,
            D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_SEPARATEALPHABLENDENABLE,
            D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3,
            D3DRS_FOGENABLE,D3DRS_FOGCOLOR,D3DRS_FOGTABLEMODE,D3DRS_FOGVERTEXMODE,D3DRS_FOGSTART,D3DRS_FOGEND,
            D3DRS_CULLMODE,D3DRS_FILLMODE,D3DRS_SCISSORTESTENABLE,D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,
            D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_MULTISAMPLEANTIALIAS,D3DRS_MULTISAMPLEMASK,
            D3DRS_SRGBWRITEENABLE,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS,D3DRS_DITHERENABLE,D3DRS_CLIPPLANEENABLE};
        for(auto state:states){DWORD v=0;ok(d->GetRenderState(state,&v));value(v);}
        const D3DSAMPLERSTATETYPE samplers[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_BORDERCOLOR,D3DSAMP_MAGFILTER,D3DSAMP_MINFILTER,
            D3DSAMP_MIPFILTER,D3DSAMP_MIPMAPLODBIAS,D3DSAMP_MAXMIPLEVEL,D3DSAMP_MAXANISOTROPY,D3DSAMP_SRGBTEXTURE};
        for(unsigned i=0;i<20;++i) {
            const unsigned stage=i<16?i:D3DVERTEXTEXTURESAMPLER0+i-16;
            object<IDirect3DBaseTexture9>([&](auto p){return d->GetTexture(stage,p);});
            for(auto state:samplers){DWORD v=0;ok(d->GetSamplerState(stage,state,&v));value(v);}
        }
        for(unsigned i=0;i<std::min(16u,unsigned(caps.MaxStreams));++i) {
            UINT offset=0,stride=0,frequency=0;
            object<IDirect3DVertexBuffer9>([&](auto p){return d->GetStreamSource(i,p,&offset,&stride);});ok(d->GetStreamSourceFreq(i,&frequency));value(offset);value(stride);value(frequency);
        }
        float ps[224][4]{},vs[256][4]{};int psi[16][4]{},vsi[16][4]{};BOOL psb[16]{},vsb[16]{};
        ok(d->GetPixelShaderConstantF(0,ps[0],224));ok(d->GetVertexShaderConstantF(0,vs[0],256));
        ok(d->GetPixelShaderConstantI(0,psi[0],16));ok(d->GetVertexShaderConstantI(0,vsi[0],16));
        ok(d->GetPixelShaderConstantB(0,psb,16));ok(d->GetVertexShaderConstantB(0,vsb,16));
        value(ps);value(vs);value(psi);value(vsi);value(psb);value(vsb);
    }
    bool operator==(const State& other)const{return bytes==other.bytes;}
};
static std::vector<DWORD> pixels(IDirect3DDevice9* d,IDirect3DSurface9* source) {
    D3DSURFACE_DESC desc{};ok(source->GetDesc(&desc));Com<IDirect3DSurface9> resolved,staging;
    IDirect3DSurface9* readable=source;
    if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE) {
        ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,resolved.out(),nullptr));
        ok(d->StretchRect(source,nullptr,resolved,nullptr,D3DTEXF_NONE));readable=resolved;
    }
    ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,staging.out(),nullptr));ok(d->GetRenderTargetData(readable,staging));
    D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<DWORD> out(W*H);
    for(unsigned y=0;y<H;++y)std::memcpy(out.data()+y*W,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*4);ok(staging->UnlockRect());return out;
}
struct Case {std::string name;unsigned major=3;bool alpha=false,kill=false,depth=false,fog=false,blend=false,foreground=false,occupied=false,names=false,noAA=false,unknown=false,noDepth=false;unsigned samples=1;DWORD actorSamples=~0u,foregroundSamples=~0u;};
struct Result {std::vector<DWORD> colour,mask,depthFingerprint,names;std::vector<HRESULT> results;bool captured=false,stencilPreserved=true;unsigned draws=0;};
struct Fixture {
    IDirect3DDevice9* d;bool independentBits,allowMultisample;Com<IDirect3DVertexDeclaration9> declaration;Com<IDirect3DTexture9> tex2d;Com<IDirect3DCubeTexture9> cube;Com<IDirect3DVolumeTexture9> volume;
    explicit Fixture(IDirect3DDevice9* device,bool bits,bool msaa):d(device),independentBits(bits),allowMultisample(msaa) {
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,16,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,20,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        ok(d->CreateVertexDeclaration(elements,declaration.out()));
        ok(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,tex2d.out(),nullptr));D3DLOCKED_RECT lock{};ok(tex2d->LockRect(0,&lock,nullptr,0));
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)*reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4)=0xffbfcfdf;ok(tex2d->UnlockRect(0));
        ok(d->CreateCubeTexture(2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,cube.out(),nullptr));
        for(unsigned face=0;face<6;++face){ok(cube->LockRect(D3DCUBEMAP_FACES(face),0,&lock,nullptr,0));for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)*reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4)=0xffbfcfdf;ok(cube->UnlockRect(D3DCUBEMAP_FACES(face),0));}
        ok(d->CreateVolumeTexture(2,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,volume.out(),nullptr));D3DLOCKED_BOX box{};ok(volume->LockBox(0,&box,nullptr,0));
        for(unsigned z=0;z<2;++z)for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)*reinterpret_cast<DWORD*>(static_cast<char*>(box.pBits)+z*box.SlicePitch+y*box.RowPitch+x*4)=0xffbfcfdf;ok(volume->UnlockBox(0));
    }
    Result run(const Case& c,bool marked,const std::vector<Word>* shipped=nullptr) {
        Result result;auto code=shipped?*shipped:pixel(c.major,c.kill,c.depth);
        if(c.occupied) {
            const Word definition[]={0x05000051,dst(2,0),0x3e800000,0x3f000000,0x3f400000,0x3f800000};
            code.insert(code.begin()+1,definition,definition+6);
            code.insert(code.end()-1,{0x02000001,dst(8,1),src(2,0)});
        }
        const auto vsCode=vertex(code);
        Com<IDirect3DPixelShader9> ps,probe;Com<IDirect3DVertexShader9> vs,probeVS;Com<IDirect3DSurface9> target,depth,occupied;
        ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),ps.out()));ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(vsCode.data()),vs.out()));
        const auto probeCode=pixel(c.major),probeVSCode=vertex(probeCode);
        ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(probeCode.data()),probe.out()));
        ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(probeVSCode.data()),probeVS.out()));
        const auto ms=c.samples==1?D3DMULTISAMPLE_NONE:D3DMULTISAMPLE_TYPE(c.samples);
        ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,ms,0,FALSE,target.out(),nullptr));ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,ms,0,TRUE,depth.out(),nullptr));
        D3DCAPS9 caps{};ok(d->GetDeviceCaps(&caps));
        for(unsigned i=0;i<20;++i)ok(d->SetTexture(i<16?i:D3DVERTEXTEXTURESAMPLER0+i-16,nullptr));for(unsigned i=1;i<std::min(4u,unsigned(caps.NumSimultaneousRTs));++i)ok(d->SetRenderTarget(i,nullptr));
        ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(depth));ok(d->SetVertexShader(vs));ok(d->SetVertexDeclaration(declaration));ok(d->SetPixelShader(ps));
        if(c.occupied){ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,ms,0,FALSE,occupied.out(),nullptr));ok(d->SetRenderTarget(1,occupied));}
        ok(d->SetIndices(nullptr));ok(d->SetStreamSource(0,nullptr,0,0));
        D3DVIEWPORT9 viewport{0,0,W,H,0,.94f};ok(d->SetViewport(&viewport));RECT scissor{2,3,LONG(W-2),LONG(H-3)};ok(d->SetScissorRect(&scissor));
        for(auto pair:{std::array<DWORD,2>{D3DRS_ZENABLE,TRUE},{D3DRS_ZWRITEENABLE,TRUE},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},
                      {D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_ALPHABLENDENABLE,c.blend?1u:0u},
                      {D3DRS_SRCBLEND,D3DBLEND_SRCALPHA},{D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA},{D3DRS_BLENDOP,D3DBLENDOP_ADD},
                      {D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_ALPHATESTENABLE,c.alpha?1u:0u},{D3DRS_ALPHAFUNC,D3DCMP_GREATER},{D3DRS_ALPHAREF,127},
                      {D3DRS_FOGENABLE,c.fog?1u:0u},{D3DRS_FOGCOLOR,0xff004000},{D3DRS_FOGTABLEMODE,D3DFOG_NONE},{D3DRS_FOGVERTEXMODE,D3DFOG_NONE},
                      {D3DRS_COLORWRITEENABLE,15},{D3DRS_COLORWRITEENABLE1,5},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},
                      {D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_MULTISAMPLEANTIALIAS,c.noAA?0u:1u},{D3DRS_MULTISAMPLEMASK,~0u},{D3DRS_DITHERENABLE,FALSE},
                      {D3DRS_CLIPPLANEENABLE,0}})ok(d->SetRenderState(D3DRENDERSTATETYPE(pair[0]),pair[1]));
        float constants[224][4];for(auto& row:constants){row[0]=.5f;row[1]=1;row[2]=.25f;row[3]=1;}ok(d->SetPixelShaderConstantF(0,constants[0],224));
        for(std::size_t p=1;p<code.size()&&(code[p]&65535)!=65535;) {
            const unsigned op=code[p]&65535,n=op==65534?(code[p]>>16)&32767:(code[p]>>24)&15;
            if(op==31&&n==2&&regType(code[p+2])==10) {
                const unsigned stage=regIndex(code[p+2]),type=(code[p+1]>>27)&15;
                ok(d->SetTexture(stage,type==3?static_cast<IDirect3DBaseTexture9*>(cube.p):type==4?static_cast<IDirect3DBaseTexture9*>(volume.p):tex2d.p));
                for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})ok(d->SetSamplerState(stage,state,D3DTADDRESS_CLAMP));
                for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(stage,state,D3DTEXF_POINT));ok(d->SetSamplerState(stage,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
            }
            p+=n+1;
        }
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff000000,1,c.unknown?0x25:c.names?0x65:0xe5));
        ActorMarker marker(d,allowMultisample);markerCaps(marker,independentBits,allowMultisample);marker.beginFrame(marked);
        auto draw=[&](bool actor,const std::array<Vertex,4>& vertices,DWORD sampleMask) {
            ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,sampleMask));const State before(d);
            DWORD depthWrite=0;ok(d->GetRenderState(D3DRS_ZWRITEENABLE,&depthWrite));marker.beginDraw(actor,depthWrite!=FALSE,true,target,depth);
            const HRESULT native=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices.data(),sizeof(Vertex));
            assert(marker.endDraw(native));ok(native);result.results.push_back(native);++result.draws;assert(before==State(d));
        };
        ok(d->BeginScene());
        draw(false,quad(-1,1,1,-1,.9f,0xff0000ff,0xff0000ff),~0u);
        if(c.names) {
            ok(d->SetRenderState(D3DRS_STENCILENABLE,TRUE));ok(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS));ok(d->SetRenderState(D3DRS_STENCILREF,0x80));ok(d->SetRenderState(D3DRS_STENCILWRITEMASK,0x80));
            ok(d->SetRenderState(D3DRS_STENCILMASK,0x80));ok(d->SetRenderState(D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP));ok(d->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP));ok(d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_REPLACE));ok(d->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE,FALSE));
        }
        const DWORD low=(c.alpha||c.blend)?0x40ff0000:0xffff0000;
        draw(true,quad(-.8f,.8f,.8f,-.8f,.6f,low,0xffff0000),c.actorSamples);
        if(c.unknown) {
            ok(d->SetRenderState(D3DRS_STENCILENABLE,TRUE));ok(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL));ok(d->SetRenderState(D3DRS_STENCILREF,0));ok(d->SetRenderState(D3DRS_STENCILMASK,0x40));
            ok(d->SetRenderState(D3DRS_STENCILWRITEMASK,0));for(auto state:{D3DRS_STENCILFAIL,D3DRS_STENCILZFAIL,D3DRS_STENCILPASS})ok(d->SetRenderState(state,D3DSTENCILOP_KEEP));
            if(c.noDepth)ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
        }
        if(c.foreground)draw(false,quad(-.3f,.5f,.3f,-.5f,.3f,0xff0000ff,0xff0000ff),c.foregroundSamples);
        if(StencilMarker)result.captured=marker.freeze(depth);
        ok(d->EndScene());
        if(!StencilMarker)result.captured=marker.freeze(depth);result.colour=pixels(d,target);
        if(auto* mask=marker.textureForDepth(depth)){Com<IDirect3DSurface9> surface;ok(mask->GetSurfaceLevel(0,surface.out()));result.mask=pixels(d,surface);}
        // Fingerprint each individual depth sample with EQUAL/LESS tests at
        // every plane used above and on either side. ZWRITE stays disabled.
        if(c.occupied){ok(d->SetRenderTarget(1,nullptr));ok(d->SetViewport(&viewport));}
        if(StencilMarker) {
            const DWORD whiteCode[]={0xffff0200,0x05000051,0xa00f0000,0x3f800000,0x3f800000,0x3f800000,0x3f800000,0x02000001,0x800f0800,0xa0e40000,0xffff};
            Com<IDirect3DPixelShader9> white;ok(d->CreatePixelShader(whiteCode,white.out()));
            ok(d->SetPixelShader(white));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW));
            for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE})ok(d->SetRenderState(state,FALSE));
            ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,~0u));ok(d->SetRenderState(D3DRS_STENCILENABLE,TRUE));
            ok(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL));ok(d->SetRenderState(D3DRS_STENCILREF,0x25));ok(d->SetRenderState(D3DRS_STENCILMASK,~0xc0u));ok(d->SetRenderState(D3DRS_STENCILWRITEMASK,0));
            for(auto state:{D3DRS_STENCILFAIL,D3DRS_STENCILZFAIL,D3DRS_STENCILPASS})ok(d->SetRenderState(state,D3DSTENCILOP_KEEP));ok(d->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE,FALSE));
            struct Screen {float x,y,z,w;};const Screen vertices[]={{-.5f,-.5f,.5f,1},{W-.5f,-.5f,.5f,1},{-.5f,H-.5f,.5f,1},{W-.5f,H-.5f,.5f,1}};
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Screen)));ok(d->EndScene());
            const auto preserved=pixels(d,target);for(DWORD value:preserved)result.stencilPreserved&=(value&0x00ffffff)==0x00ffffff;
            if(marked&&(result.captured||c.unknown)) {
                ok(d->SetRenderState(D3DRS_STENCILREF,0));ok(d->SetRenderState(D3DRS_STENCILMASK,0x40));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
                ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Screen)));ok(d->EndScene());
                const auto cleared=pixels(d,target);for(DWORD value:cleared)result.stencilPreserved&=(value&0x00ffffff)==0x00ffffff;
            }
            ok(d->SetRenderState(D3DRS_STENCILREF,0x80));ok(d->SetRenderState(D3DRS_STENCILMASK,0x80));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Screen)));ok(d->EndScene());result.names=pixels(d,target);
            ok(d->SetRenderState(D3DRS_STENCILENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));
            ok(d->SetVertexDeclaration(declaration));
        }
        ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        ok(d->SetRenderState(D3DRS_FOGENABLE,FALSE));ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,~0u));ok(d->SetPixelShader(probe));ok(d->SetVertexShader(probeVS));
        for(unsigned sample=0;sample<c.samples;++sample)for(auto compare:{D3DCMP_EQUAL,D3DCMP_LESS})for(float plane:{.25f/.94f,.3f,.6f,.9f,.599f,.601f}) {
            ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,c.samples==1?~0u:1u<<sample));
            ok(d->SetRenderState(D3DRS_ZFUNC,compare));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0));
            const auto vertices=quad(-1,1,1,-1,plane,0xffffffff,0xffffffff);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices.data(),sizeof(Vertex)));ok(d->EndScene());
            const auto image=pixels(d,target);result.depthFingerprint.insert(result.depthFingerprint.end(),image.begin(),image.end());
        }
        for(unsigned i=0;i<16;++i)ok(d->SetTexture(i,nullptr));ok(d->SetPixelShader(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetVertexDeclaration(nullptr));ok(d->SetDepthStencilSurface(nullptr));
        return result;
    }
};
static std::string json(const std::string& input){std::string out;for(char c:input){if(c=='"'||c=='\\')out+='\\';out+=c;}return out;}
static void dump(const std::string& path,const std::vector<DWORD>& image){std::ofstream f(path,std::ios::binary);assert(f);f.write(reinterpret_cast<const char*>(image.data()),std::streamsize(image.size()*4));}
static void waitQuery(IDirect3DQuery9* query,void* result,DWORD bytes) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    for(;;){const HRESULT hr=query->GetData(result,bytes,D3DGETDATA_FLUSH);if(hr==S_OK)return;ok(hr);assert(std::chrono::steady_clock::now()<deadline);Sleep(0);}
}
static double median(std::vector<double> values){std::sort(values.begin(),values.end());return (values[(values.size()-1)/2]+values[values.size()/2])*.5;}
static void benchmark(Fixture& fixture,FILE* report,const D3DADAPTER_IDENTIFIER9& adapter) {
    auto* d=fixture.d;constexpr unsigned Width=640,Height=360,Pairs=6;
    Com<IDirect3DSurface9> target,depth;Com<IDirect3DPixelShader9> ps;Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DQuery9> frequency,disjoint,done;
    ok(d->CreateRenderTarget(Width,Height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_4_SAMPLES,0,FALSE,target.out(),nullptr));
    ok(d->CreateDepthStencilSurface(Width,Height,D3DFMT_D24S8,D3DMULTISAMPLE_4_SAMPLES,0,TRUE,depth.out(),nullptr));
    const auto code=pixel(3),vsCode=vertex(code);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),ps.out()));ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(vsCode.data()),vs.out()));
    for(unsigned i=0;i<20;++i)ok(d->SetTexture(i<16?i:D3DVERTEXTEXTURESAMPLER0+i-16,nullptr));
    for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(depth));
    ok(d->SetVertexDeclaration(fixture.declaration));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(ps));ok(d->SetStreamSource(0,nullptr,0,0));ok(d->SetIndices(nullptr));
    const D3DVIEWPORT9 viewport{0,0,Width,Height,0,.94f};ok(d->SetViewport(&viewport));
    for(auto state:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_DITHERENABLE,D3DRS_CLIPPLANEENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,5));ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,~0u));
    ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,frequency.out()));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,disjoint.out()));ok(d->CreateQuery(D3DQUERYTYPE_EVENT,done.out()));
    ActorMarker marker(d,fixture.allowMultisample);markerCaps(marker,fixture.independentBits,true);
    std::array<std::array<Vertex,4>,256> actors,foreground;
    for(unsigned i=0;i<actors.size();++i) {
        const float left=-.95f+float(i%32)*.06f,top=.85f-float(i/32)*.2f;
        actors[i]=quad(left,top,left+.025f,top-.08f,.6f,0xffff0000,0xffff0000);
        foreground[i]=quad(left+.012f,top-.025f,left+.03f,top-.1f,.3f,0xff0000ff,0xff0000ff);
    }
    const auto background=quad(-1,1,1,-1,.9f,0xff0000ff,0xff0000ff);
    auto frame=[&](bool marked,unsigned actorCount) {
        marker.beginFrame(marked);ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0));ok(d->BeginScene());
        auto draw=[&](bool actor,const std::array<Vertex,4>& vertices) {
            if(marked)assert(marker.beginDraw(actor,true,true,target,depth)||(!actor&&!marker.valid()));
            const HRESULT hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices.data(),sizeof(Vertex));
            if(marked)assert(marker.endDraw(hr));ok(hr);
        };
        draw(false,background);for(unsigned i=0;i<actorCount;++i)draw(true,actors[i]);for(unsigned i=0;i<actorCount;++i)draw(false,foreground[i]);
        if(marked&&StencilMarker)assert(marker.freeze(depth));ok(d->EndScene());if(marked&&!StencilMarker)assert(marker.freeze(depth));
    };
    std::fprintf(report,"{\"benchmark\":true,\"game_launched\":false,\"window_visible\":false,\"adapter\":\"%s\",\"width\":%u,\"height\":%u,\"samples\":4,\"pairs\":%u,\"per_frame\":\"one background, N actors, N nonactor depth writes; marker includes sampler audit, clear and resolve\",\"benchmarks\":[",json(adapter.Description).c_str(),Width,Height,Pairs);
    unsigned series=0;
    for(unsigned count:{64u,256u}) {
        for(unsigned warm=0;warm<4;++warm)frame(warm%2!=0,count);ok(done->Issue(D3DISSUE_END));waitQuery(done,nullptr,0);
        struct Sample {Com<IDirect3DQuery9> start,end;double cpu=0,gpu=0;bool marked=false;};std::array<Sample,Pairs*2> samples;
        for(auto& sample:samples){ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,sample.start.out()));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,sample.end.out()));}
        ok(disjoint->Issue(D3DISSUE_BEGIN));ok(frequency->Issue(D3DISSUE_END));
        for(unsigned i=0;i<samples.size();++i) {
            auto& sample=samples[i];sample.marked=((i/2)%2==0)?i%2!=0:i%2==0;
            ok(sample.start->Issue(D3DISSUE_END));const auto before=std::chrono::steady_clock::now();frame(sample.marked,count);
            sample.cpu=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-before).count();ok(sample.end->Issue(D3DISSUE_END));
        }
        ok(disjoint->Issue(D3DISSUE_END));ok(done->Issue(D3DISSUE_END));waitQuery(done,nullptr,0);BOOL changed=FALSE;UINT64 ticksPerSecond=0;
        waitQuery(disjoint,&changed,sizeof changed);waitQuery(frequency,&ticksPerSecond,sizeof ticksPerSecond);assert(!changed&&ticksPerSecond);
        std::vector<double> cpuNative,cpuMarked,gpuNative,gpuMarked,cpuDelta,gpuDelta;
        for(auto& sample:samples) {
            UINT64 begin=0,end=0;waitQuery(sample.start,&begin,sizeof begin);waitQuery(sample.end,&end,sizeof end);assert(end>=begin);
            sample.gpu=double(end-begin)*1e6/double(ticksPerSecond);
            (sample.marked?cpuMarked:cpuNative).push_back(sample.cpu);(sample.marked?gpuMarked:gpuNative).push_back(sample.gpu);
        }
        for(unsigned i=0;i<samples.size();i+=2) {
            const auto& native=samples[samples[i].marked?i+1:i];const auto& marked=samples[samples[i].marked?i:i+1];
            cpuDelta.push_back(marked.cpu-native.cpu);gpuDelta.push_back(marked.gpu-native.gpu);
        }
        std::fprintf(report,"%s{\"actor_draws\":%u,\"nonactor_draws\":%u,\"cpu_native_us\":%.3f,\"cpu_marked_us\":%.3f,\"cpu_paired_delta_us\":%.3f,\"gpu_native_us\":%.3f,\"gpu_marked_us\":%.3f,\"gpu_paired_delta_us\":%.3f,\"raw\":[",
            series?",":"",count,count,median(cpuNative),median(cpuMarked),median(cpuDelta),median(gpuNative),median(gpuMarked),median(gpuDelta));
        for(unsigned i=0;i<samples.size();++i)std::fprintf(report,"%s{\"marked\":%s,\"cpu_us\":%.3f,\"gpu_us\":%.3f}",i?",":"",samples[i].marked?"true":"false",samples[i].cpu,samples[i].gpu);
        std::fprintf(report,"]}");++series;std::fflush(report);
        std::printf("BENCH actors=%u clear=%u CPU native/marked/delta=%.3f/%.3f/%.3f us GPU=%.3f/%.3f/%.3f us\n",count,count,median(cpuNative),median(cpuMarked),median(cpuDelta),median(gpuNative),median(gpuMarked),median(gpuDelta));
    }
    std::fprintf(report,"],\"failures\":0,\"timing_scope\":\"paired warm synthetic submission and timestamp intervals, not in-game FPS\"}\n");
    ok(d->SetPixelShader(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetVertexDeclaration(nullptr));ok(d->SetDepthStencilSurface(nullptr));
}
int main(int argc,char** argv) {
    // Refuse before loading any graphics library unless explicitly requested.
    if(argc<4||std::string(argv[1])!="--run-gpu"){std::puts("Refusing GPU execution: use the Python runner with --run-gpu.");return 64;}
    SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    const std::string backend=argv[2];HMODULE module=nullptr;
    if(backend=="system") {
        wchar_t directory[MAX_PATH];const UINT n=GetSystemDirectoryW(directory,MAX_PATH);assert(n&&n<MAX_PATH);
        module=LoadLibraryW((std::wstring(directory,n)+L"\\d3d9.dll").c_str());
    } else module=LoadLibraryA(backend.c_str());
    assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    unsigned cases=0,failures=0,unavailable=0;FILE* report=std::fopen(argv[3],"w");assert(report);
    {
        Com<IDirect3D9> api;api.p=create(D3D_SDK_VERSION);assert(api.p);D3DCAPS9 caps{};ok(api->GetDeviceCaps(0,D3DDEVTYPE_HAL,&caps));
        D3DADAPTER_IDENTIFIER9 adapter{};ok(api->GetAdapterIdentifier(0,0,&adapter));

        assert(caps.PixelShaderVersion>=D3DPS_VERSION(3,0)&&caps.NumSimultaneousRTs>=2);
        ok(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_4_SAMPLES,nullptr));
        ok(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_D24S8,TRUE,D3DMULTISAMPLE_4_SAMPLES,nullptr));
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
        Com<IDirect3DDevice9> device;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,device.out()));
        const char* msaa=std::getenv("NORTHLIGHT_AO_ACTOR_MASK_GPU_MSAA");const bool allowMultisample=msaa&&std::string(msaa)=="1";
        Fixture fixture(device,(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)!=0,allowMultisample);
        const char* bench=std::getenv("NORTHLIGHT_AO_ACTOR_MASK_GPU_BENCH");
        if(bench&&std::string(bench)=="1")benchmark(fixture,report,adapter);
        else {
        std::fprintf(report,"{\"game_launched\":false,\"window_visible\":false,\"adapter\":\"%s\",\"vendor_id\":%lu,\"device_id\":%lu,\"multisample_mrt_enabled\":%s,\"cases\":[",
            json(adapter.Description).c_str(),static_cast<unsigned long>(adapter.VendorId),static_cast<unsigned long>(adapter.DeviceId),allowMultisample?"true":"false");
        auto compare=[&](const Case& c,const std::vector<Word>* stock=nullptr) {
            const char* selection=std::getenv("NORTHLIGHT_AO_ACTOR_MASK_GPU_CASE");
            if(selection&&c.name.find(selection)==std::string::npos)return;
            const Result native=fixture.run(c,false,stock),control=fixture.run(c,false,stock),marked=fixture.run(c,true,stock);
            const bool stable=native.colour==control.colour&&native.depthFingerprint==control.depthFingerprint;
            const bool unsupported=c.occupied&&c.samples>1&&!allowMultisample;
            const bool expected=!unsupported&&!c.unknown;
            unsigned maskErrors=0,fractional=0,full=0,nonblack=0;
            for(DWORD pixel:native.colour)nonblack+=(pixel&0x00ffffffu)!=0;
            const bool depthOwnership=c.blend||(c.fog&&c.major==2);
            if(expected&&!stock&&!depthOwnership)for(std::size_t i=0;i<marked.mask.size();++i) {
                const unsigned ownership=(marked.mask[i]>>16)&255,red=(marked.colour[i]>>16)&255;
                maskErrors+=std::abs(int(ownership)-int(red))>1;fractional+=ownership>0&&ownership<255;full+=ownership==255;
            }
            if(expected&&!stock&&depthOwnership)for(std::size_t i=0;i<marked.mask.size();++i) {
                unsigned covered=0;for(unsigned sample=0;sample<c.samples;++sample)covered+=((native.depthFingerprint[((sample*12+2)*W*H)+i]>>16)&255)!=0;
                const unsigned ownership=(marked.mask[i]>>16)&255;
                if(!covered)maskErrors+=ownership!=0;else if(covered==c.samples)maskErrors+=ownership!=255;
                full+=ownership==255;fractional+=ownership>0&&ownership<255;
            }
            const bool needsPartial=c.samples>1&&(c.actorSamples!=~0u||c.foregroundSamples!=~0u);
            bool bad=!stable||!native.stencilPreserved||!control.stencilPreserved||!marked.stencilPreserved||native.names!=marked.names||native.colour!=marked.colour||native.depthFingerprint!=marked.depthFingerprint||native.results!=marked.results||native.draws!=marked.draws||
                marked.captured!=expected||(expected&&marked.mask.empty())||maskErrors||(expected&&needsPartial&&!fractional)||(!stock&&!c.blend&&expected&&!needsPartial&&!full);
            if(unsupported){++unavailable;bad=marked.captured||native.results!=marked.results||native.draws!=marked.draws;}
            failures+=bad;
            std::fprintf(report,"%s{\"name\":\"%s\",\"stock\":%s,\"ps_model\":%u,\"samples\":%u,\"captured\":%s,\"native_control_equal\":%s,\"colour_equal\":%s,\"depth_tests_equal\":%s,\"native_draws\":%u,\"mask_errors\":%u,\"fractional_pixels\":%u,\"full_actor_pixels\":%u,\"nonblack_native_pixels\":%u,\"other_stencil_bits_preserved\":%s,\"status\":\"%s\",\"unavailable_reason\":\"%s\",\"pass\":%s}",
                cases?",":"",json(c.name).c_str(),stock?"true":"false",c.major,c.samples,marked.captured?"true":"false",stable?"true":"false",native.colour==marked.colour?"true":"false",
                native.depthFingerprint==marked.depthFingerprint?"true":"false",marked.draws,maskErrors,fractional,full,nonblack,marked.stencilPreserved?"true":"false",bad?"FAIL":unsupported?"UNAVAILABLE":"PASS",
                unsupported?"Native MRT+MSAA is unsupported by D3D9: https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets":"",bad||unsupported?"false":"true");++cases;std::fflush(report);
            if(bad) {
                const std::string prefix=std::string(argv[3])+".case"+std::to_string(cases);
                dump(prefix+".native.argb",native.colour);dump(prefix+".control.argb",control.colour);dump(prefix+".marked.argb",marked.colour);dump(prefix+".mask.argb",marked.mask);
            }
            std::printf("%s %s\n",bad?"FAIL":unsupported?"UNAVAILABLE":"PASS",c.name.c_str());
        };
        for(unsigned major:{2u,3u})for(unsigned samples:{1u,4u}) {
            auto base=[&](const char* label){Case c;c.name="ps"+std::to_string(major)+"-"+std::to_string(samples)+"x-"+label;c.major=major;c.samples=samples;return c;};
            compare(base("opaque"));auto c=base("alpha-test");c.alpha=true;compare(c);c=base("native-kill");c.kill=true;compare(c);
            c=base("native-depth");c.depth=true;compare(c);c=base("foreground-nonactor");c.foreground=true;compare(c);
            c=base("fog");c.fog=true;compare(c);c=base("blend-rejected");c.blend=true;compare(c);
            c=base("native-mrt1-occupied");c.occupied=true;compare(c);
            if(StencilMarker){c=base("name-mask-compose");c.names=true;c.kill=true;c.foreground=true;compare(c);}
            if(StencilMarker){c=base("unknown-stencil-after-actor");c.unknown=true;c.foreground=true;compare(c);c=base("unknown-stencil-no-depth-write");c.unknown=true;c.noDepth=true;c.foreground=true;compare(c);}
            if(samples==4){c=base("partial-actor-samples");c.actorSamples=1;compare(c);c=base("partial-nonactor-clear");c.foreground=true;c.foregroundSamples=1;compare(c);
                if(StencilMarker){c=base("native-aa-disabled");c.noAA=true;compare(c);}}
        }
        // Optional prepared stock bytecode, read-only. Every accepted original
        // is drawn unchanged and through the production lazy patch/cache path.
        if(argc>=5) {
            std::ifstream list(argv[4]);assert(list);std::string path;
            while(std::getline(list,path)) {
                if(!path.empty()&&path.back()=='\r')path.pop_back();if(path.empty())continue;
                auto code=read(path);if(code[0]!=0xffff0200&&code[0]!=0xffff0300)continue;
                // Native stock pixel shader is drawn unchanged; stencil needs no bytecode transform.
                Case c;c.major=(code[0]>>8)&255;c.name=path.substr(path.find_last_of("/\\")+1);compare(c,&code);
            }
        }
        std::fprintf(report,"],\"case_count\":%u,\"passed\":%u,\"unavailable\":%u,\"failures\":%u,\"depth_proof\":\"per-sample EQUAL/LESS probe equivalence at synthetic scene planes; not direct depth readback\"}\n",cases,cases-failures-unavailable,unavailable,failures);
        }
    }
    std::fclose(report);DestroyWindow(window);FreeLibrary(module);std::printf("AO actor mask GPU: %u cases, %u failures\n",cases,failures);return failures?1:0;
}
