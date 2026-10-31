// Exact production HDR boundary methods are extracted by test_hdr_boundary.py.
// No D3D runtime, window, game or GPU is created.
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <unordered_map>
#include "shader_read_set.h"
#include "ui_boundary.h"
using HRESULT=int;using DWORD=unsigned;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
constexpr HRESULT S_OK=0,E_FAIL=-1,D3DERR_INVALIDCALL=-2;
constexpr unsigned D3DRTYPE_TEXTURE=1,D3DRTYPE_CUBETEXTURE=2,D3DUSAGE_RENDERTARGET=1,D3DUSAGE_DEPTHSTENCIL=2;
constexpr DWORD D3DRS_ZWRITEENABLE=1,D3DRS_ZFUNC=2,D3DRS_ZENABLE=3,D3DCMP_ALWAYS=8,D3DVERTEXTEXTURESAMPLER0=257;
struct D3DSURFACE_DESC{unsigned Usage=0;};
struct Ref{int refs=1;~Ref(){assert(refs==1);}void AddRef(){++refs;}void Release(){assert(refs>1);--refs;}};
struct IDirect3DBaseTexture9:Ref{unsigned kind=D3DRTYPE_TEXTURE;unsigned GetType(){return kind;}};
struct IDirect3DTexture9:IDirect3DBaseTexture9{
    unsigned usage=0;bool fail=false;
    HRESULT GetLevelDesc(unsigned,D3DSURFACE_DESC* desc){desc->Usage=usage;return fail?E_FAIL:S_OK;}
};
struct IDirect3DCubeTexture9:IDirect3DTexture9{IDirect3DCubeTexture9(){kind=D3DRTYPE_CUBETEXTURE;}};
struct IDirect3DVertexShader9:Ref{};struct IDirect3DPixelShader9:Ref{};
#include "hdr_boundary_texture.h"
struct Backend{
    IDirect3DVertexShader9 vs;IDirect3DPixelShader9 ps;
    std::unordered_map<DWORD,IDirect3DBaseTexture9*> textures;
    bool failVs=false,failPs=false,failTexture=false;unsigned textureReads=0;
    int legacy=11,presentation=7;bool hdrBound=false;
    HRESULT GetVertexShader(IDirect3DVertexShader9** out){if(failVs)return E_FAIL;*out=&vs;vs.AddRef();return S_OK;}
    HRESULT GetPixelShader(IDirect3DPixelShader9** out){if(failPs)return E_FAIL;*out=&ps;ps.AddRef();return S_OK;}
    HRESULT GetTexture(DWORD stage,IDirect3DBaseTexture9** out){++textureReads;if(failTexture)return E_FAIL;*out=textures[stage];if(*out)(*out)->AddRef();return S_OK;}
    HRESULT GetRenderState(DWORD state,DWORD* out){*out=state==D3DRS_ZFUNC?D3DCMP_ALWAYS:0;return S_OK;}
    HRESULT GetVertexShaderConstantF(unsigned,float* rows,unsigned){for(unsigned i=0;i<16;++i)rows[i]=i%5==0?1.f:0.f;return S_OK;}
    void nativeWrite(int color){(hdrBound?presentation:legacy)=color;}
    int logicalCopy()const{return legacy;} // Device::StretchRect deliberately uses the logical SDR source.
};
struct Fixture{
    Backend backend;Backend* ext=&backend;
    struct{bool active=true;struct{float uiWhite=160;}settings;}hdr;
    struct{unsigned rejected=0,draws=0;template<class Draw>HRESULT draw(Backend*,float,Draw work){++draws;return work();}}hdrColor;
    std::unordered_map<void*,NorthlightShaderReads::ReadSet> shaderReads;
    bool hdrUiPhase=false,applied=false,world=false,failResolve=false,failBind=false;
    unsigned begins=0,effects=0,hdrFailuresLogged=0,hdrDeferredLogs=0,hdrDeferredFrame=~0u;
    Fixture(){shaderReads[&backend.vs]={};shaderReads[&backend.vs].exact=true;shaderReads[&backend.vs].vertex=true;
        shaderReads[&backend.ps]={};shaderReads[&backend.ps].exact=true;shaderReads[&backend.ps].samplers=1;}
    ~Fixture(){assert(backend.vs.refs==1&&backend.ps.refs==1);}
    template<class T>static void drop(T*& value){if(value)value->Release();value=nullptr;}
    template<class Work>void extensionWork(const char*,Work work){work();}
    const char* hdrBoundarySource(){return "learned-ui";}
    bool fullViewport(D3DSURFACE_DESC&,void*,bool,bool){return true;}
    bool hdrBackTarget(){return true;}
    void renderEffects(){++effects;applied=world;}
    void spellReplay(){}
    HRESULT hdrBegin(bool,const char*){++begins;if(failResolve)return E_FAIL;backend.presentation=backend.legacy;
        if(failBind)return E_FAIL;backend.hdrBound=hdrUiPhase=true;return S_OK;}
    void logf(const char*,...){}
    unsigned frame=0;
#include "hdr_boundary_methods.h"
    HRESULT draw(int color){return hdrDraw([&]{backend.nativeWrite(color);return S_OK;});}
};
int main(){
    // A full-screen scene effect looks exactly like an ortho UI draw, before
    // either initial world rendering or a later phase of an already drawn scene.
    for(bool world:{false,true}){
        Fixture f;IDirect3DTexture9 scene;scene.usage=D3DUSAGE_RENDERTARGET;f.backend.textures[0]=&scene;f.world=world;
        if(world)f.backend.legacy=31;
        assert(f.draw(41)==S_OK);f.backend.nativeWrite(53);
#ifdef NORTHLIGHT_HDR_BOUNDARY_COUNTERFACTUAL
        assert(f.hdrUiPhase&&f.begins==1&&f.backend.logicalCopy()!=53);
        assert(f.backend.presentation==53); // The game advanced, but its copy still reads an older scene.
#else
        assert(!f.hdrUiPhase&&f.begins==0&&f.backend.logicalCopy()==53);
        scene.usage=0; // The actual atlas UI follows all the scene draws/copies.
        assert(f.draw(67)==S_OK&&f.hdrUiPhase&&f.backend.presentation==67);
        assert(f.begins==1&&f.hdrColor.draws==1&&f.backend.legacy==53);
        // Explicit policy boundary: after real atlas UI, the logical legacy
        // source remains the fresh pre-UI scene, not the HDR UI presentation.
        assert(f.backend.logicalCopy()==53);
#endif
    }
#ifndef NORTHLIGHT_HDR_BOUNDARY_COUNTERFACTUAL
    // Login/loading atlas UI keeps its immediate HDR boundary; no world required.
    {Fixture f;IDirect3DTexture9 atlas,unused;unused.usage=D3DUSAGE_RENDERTARGET;f.backend.textures[0]=&atlas;f.backend.textures[7]=&unused;
        assert(f.draw(79)==S_OK&&f.hdrUiPhase&&f.backend.presentation==79&&f.backend.textureReads==1);}
    for(unsigned usage:{D3DUSAGE_RENDERTARGET,D3DUSAGE_DEPTHSTENCIL}){
        Fixture f;IDirect3DTexture9 scene;scene.usage=usage;f.backend.textures[0]=&scene;
        assert(!f.hdrUiInputs());f.shaderReads[&f.backend.ps].samplers=0;assert(f.hdrUiInputs());
        f.shaderReads[&f.backend.vs].samplers=4;f.backend.textures[D3DVERTEXTEXTURESAMPLER0+2]=&scene;assert(!f.hdrUiInputs());}
    {Fixture f;IDirect3DCubeTexture9 cube;cube.usage=D3DUSAGE_RENDERTARGET;f.backend.textures[0]=&cube;assert(!f.hdrUiInputs());}
    for(unsigned failure=0;failure<7;++failure){
        Fixture f;IDirect3DTexture9 atlas;f.backend.textures[0]=&atlas;
        switch(failure){case 0:f.backend.failVs=true;break;case 1:f.backend.failPs=true;break;case 2:f.backend.failTexture=true;break;
            case 3:atlas.fail=true;break;case 4:f.shaderReads[&f.backend.ps].exact=false;break;
            case 5:f.shaderReads.erase(&f.backend.ps);break;case 6:f.shaderReads[&f.backend.vs].vertex=false;break;}
        assert(f.draw(83)==S_OK&&!f.hdrUiPhase&&f.backend.legacy==83&&f.begins==0);}
    for(bool bind:{false,true}){Fixture f;f.failResolve=!bind;f.failBind=bind;
        assert(f.draw(89)==S_OK&&!f.hdrUiPhase&&!f.backend.hdrBound&&f.backend.legacy==89&&f.hdrColor.draws==0);}
    {Fixture f;f.hdr.active=false;assert(f.draw(97)==S_OK&&!f.hdrUiPhase&&f.backend.legacy==97&&f.begins==0);}
    std::puts("HDR_BOUNDARY_PASS scene generations current; world/death and login/UI; exact samplers; failure fallback; post-UI legacy policy explicit");
#else
    std::puts("HDR_BOUNDARY_COUNTERFACTUAL_PASS old boundary leaves stale legacy copy while presentation advances");
#endif
}
