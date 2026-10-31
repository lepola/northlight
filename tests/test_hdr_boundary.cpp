// Exact production HDR boundary methods are extracted by test_hdr_boundary.py.
// No D3D runtime, window, game or GPU is created.
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include "shader_read_set.h"
#include "ui_boundary.h"
#include "world_draw_domain.h"
using HRESULT=int;using DWORD=unsigned;using UINT=unsigned;
using LONGLONG=long long;
struct CpuScope {
    inline static unsigned reads=0;LONGLONG* elapsed;
    explicit CpuScope(LONGLONG* value):elapsed(value){if(elapsed)reads+=2;}
    ~CpuScope(){if(elapsed)++*elapsed;}
};
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
constexpr HRESULT S_OK=0,E_FAIL=-1,D3DERR_INVALIDCALL=-2;
constexpr unsigned D3DRTYPE_TEXTURE=1,D3DRTYPE_CUBETEXTURE=2,D3DUSAGE_RENDERTARGET=1,D3DUSAGE_DEPTHSTENCIL=2;
constexpr DWORD D3DRS_ZWRITEENABLE=1,D3DRS_ZFUNC=2,D3DRS_ZENABLE=3,D3DCMP_ALWAYS=8,D3DVERTEXTEXTURESAMPLER0=257;
constexpr DWORD D3DFVF_POSITION_MASK=0x400e,D3DFVF_XYZRHW=4;
[[maybe_unused]] constexpr DWORD D3DFVF_XYZ=2;
constexpr unsigned MAXD3DDECLLENGTH=64,D3DDECLUSAGE_POSITIONT=9,D3DDECLTYPE_FLOAT4=3;
constexpr DWORD D3DTSS_COLOROP=1,D3DTSS_ALPHAOP=4,D3DTSS_COLORARG1=2,D3DTSS_COLORARG2=3,D3DTSS_ALPHAARG1=5,D3DTSS_ALPHAARG2=6;
constexpr DWORD D3DTOP_DISABLE=1,D3DTOP_SELECTARG1=2,D3DTOP_SELECTARG2=3,D3DTOP_MODULATE=4,D3DTOP_MODULATE2X=5,D3DTOP_MODULATE4X=6,D3DTOP_ADD=7;
constexpr DWORD D3DTA_SELECTMASK=15,D3DTA_TEXTURE=2,D3DTA_DIFFUSE=0;
struct D3DVERTEXELEMENT9{unsigned Stream=0,Usage=0,UsageIndex=0,Type=0;};
struct D3DVIEWPORT9{unsigned X=0,Y=0,Width=3840,Height=2160;float MinZ=0,MaxZ=1;};
struct D3DSURFACE_DESC{unsigned Usage=0;};
struct Ref{int refs=1;~Ref(){assert(refs==1);}void AddRef(){++refs;}void Release(){assert(refs>1);--refs;}};
struct IDirect3DBaseTexture9:Ref{unsigned kind=D3DRTYPE_TEXTURE;unsigned GetType(){return kind;}};
struct IDirect3DTexture9:IDirect3DBaseTexture9{
    unsigned usage=0;bool fail=false;
    HRESULT GetLevelDesc(unsigned,D3DSURFACE_DESC* desc){desc->Usage=usage;return fail?E_FAIL:S_OK;}
};
struct IDirect3DCubeTexture9:IDirect3DTexture9{IDirect3DCubeTexture9(){kind=D3DRTYPE_CUBETEXTURE;}};
struct IDirect3DVertexDeclaration9:Ref{
    bool transformed=true;
    HRESULT GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count){*count=2;out[0]={0,transformed?D3DDECLUSAGE_POSITIONT:0,0,D3DDECLTYPE_FLOAT4};out[1].Stream=0xff;return S_OK;}
};
struct IDirect3DVertexShader9:Ref{};struct IDirect3DPixelShader9:Ref{};
#include "hdr_boundary_texture.h"
struct Backend{
    IDirect3DVertexShader9 vs;IDirect3DPixelShader9 ps;IDirect3DVertexDeclaration9 declaration;
    IDirect3DVertexShader9* vertex=&vs;
    bool fixedVs=false,fixedPs=false,pixelProjection=false,scaledProjection=false;D3DVIEWPORT9 viewport;DWORD fvf=D3DFVF_XYZRHW,z=0,write=0,function=D3DCMP_ALWAYS;
    std::unordered_map<DWORD,IDirect3DBaseTexture9*> textures;
    bool failVs=false,failPs=false,failTexture=false,failViewport=false;unsigned textureReads=0;
    int legacy=11,presentation=7;bool hdrBound=false;
    bool peekVertexShader(IDirect3DVertexShader9*&){return false;}
    HRESULT GetViewport(D3DVIEWPORT9* out){*out=viewport;return failViewport?E_FAIL:S_OK;}
    HRESULT GetVertexShader(IDirect3DVertexShader9** out){if(failVs)return E_FAIL;*out=fixedVs?nullptr:vertex;if(*out)(*out)->AddRef();return S_OK;}
    HRESULT GetPixelShader(IDirect3DPixelShader9** out){if(failPs)return E_FAIL;*out=fixedPs?nullptr:&ps;if(*out)ps.AddRef();return S_OK;}
    HRESULT GetTexture(DWORD stage,IDirect3DBaseTexture9** out){++textureReads;if(failTexture)return E_FAIL;*out=textures[stage];if(*out)(*out)->AddRef();return S_OK;}
    HRESULT GetRenderState(DWORD state,DWORD* out){*out=state==D3DRS_ZFUNC?function:state==D3DRS_ZENABLE?z:write;return S_OK;}
    HRESULT GetFVF(DWORD* out){*out=fvf;return S_OK;}
    HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9** out){*out=&declaration;declaration.AddRef();return S_OK;}
    HRESULT GetTextureStageState(unsigned stage,DWORD state,DWORD* out){
        *out=state==D3DTSS_COLOROP||state==D3DTSS_ALPHAOP?(stage?D3DTOP_DISABLE:D3DTOP_MODULATE):
            state==D3DTSS_COLORARG1||state==D3DTSS_ALPHAARG1?D3DTA_TEXTURE:D3DTA_DIFFUSE;return S_OK;
    }
    HRESULT GetVertexShaderConstantF(unsigned,float* rows,unsigned){for(unsigned i=0;i<16;++i)rows[i]=i%5==0?1.f:0.f;if(pixelProjection||scaledProjection){rows[0]=scaledProjection?2.29469f:2.f/3840;rows[5]=scaledProjection?4.07946f:2.f/2160;rows[3]=scaledProjection?-1.00026f:-.99974f;rows[7]=scaledProjection?-.999537f:-1.00046f;rows[10]=scaledProjection?-.001f:.0001f;rows[11]=.5f;}return S_OK;}
    void nativeWrite(int color){(hdrBound?presentation:legacy)=color;}
    int logicalCopy()const{return legacy;} // Device::StretchRect deliberately uses the logical SDR source.
};
struct Fixture{
    Backend backend;Backend* ext=&backend;
    struct{bool active=true;struct{float uiWhite=160;}settings;}hdr;
    struct{float uiWhite=160;}hdrFrameSettings;
    struct{unsigned rejected=0,draws=0;std::uint64_t layers=0;template<class Draw>HRESULT draw(Backend*,float,Draw work){++draws;return work();}}hdrColor;
    std::unordered_map<void*,NorthlightShaderReads::ReadSet> shaderReads;
    bool hdrUiPhase=false,applied=false,world=false,failResolve=false,failBind=false,hdrAtUiBoundary=false,hdrSkyReplaying=false;
    unsigned begins=0,effects=0,hdrFailuresLogged=0,hdrDeferredLogs=0,hdrDeferredFrame=~0u;
    bool gateFrame=false,gateUntimed=false;
    bool hdrSkyValid=false,backbuffer=true,failSky=false;unsigned skyCaptures=0;
    float worldMinDepth=0,worldMaxDepth=.94f;
    LONGLONG cpuHdrAdmission=0,cpuHdrBoundary=0,cpuHdrTransfer=0;
    unsigned hdrAdmissionCalls=0,hdrTransferCalls=0;
    Fixture(){shaderReads[&backend.vs]={};shaderReads[&backend.vs].exact=true;shaderReads[&backend.vs].vertex=true;
        shaderReads[&backend.ps]={};shaderReads[&backend.ps].exact=true;shaderReads[&backend.ps].samplers=1;}
    ~Fixture(){assert(backend.vs.refs==1&&backend.ps.refs==1);}
    template<class T>static void drop(T*& value){if(value)value->Release();value=nullptr;}
    template<class Work>void extensionWork(const char*,Work work){work();}
    static constexpr int kTagMask=3,kWaterTag=4,kUiTextTag=256;
    std::unordered_map<IDirect3DVertexShader9*,int> vsTags;unsigned vsGeneration=0;
    struct VsClass{int entry=2;bool world=false,wmo=false,skinned=false;};VsClass vc;
    bool stockUi=true,hdrWorldSeen=false;bool& terrain=world;
    struct{bool isBoundary(std::uintptr_t,unsigned){return false;}bool armed()const{return false;}bool disarmed(){return false;}std::uintptr_t learnedVs()const{return 0;}}uiFallback;
    const VsClass& classifyVs(IDirect3DVertexShader9* shader){vc.entry=(stockUi&&shader?2:0)|vsTags[shader];return vc;}
    bool fullViewportOk=true;
    bool fullViewport(D3DSURFACE_DESC&,void*,bool,bool){return fullViewportOk;}
    bool hdrBackTarget(){return backbuffer;}
    bool hdrSkyDraw(){++skyCaptures;if(failSky)return false;hdrSkyValid=true;return true;}
    void renderEffects(){++effects;applied=world;}
    void spellReplay(){}
    void hdrLayerDiagnostic(){}
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
    // A font mod uses different VS identities for logical UI and physical-pixel
    // text. HDR completes before the old dominant-shader fallback can learn.
    {Fixture f;IDirect3DTexture9 atlas;IDirect3DVertexShader9 font;
        f.stockUi=false;f.world=true;f.backend.scaledProjection=true;f.backend.textures[0]=&atlas;
        assert(!f.uiTextVs(&f.backend.vs,0));
        assert(f.draw(101)==S_OK&&f.hdrUiPhase);
        assert(f.uiTextVs(&f.backend.vs,f.vsTags[&f.backend.vs]));
        f.backend.vertex=&font;f.shaderReads[&font]=f.shaderReads[&f.backend.vs];
        f.backend.scaledProjection=false;f.backend.pixelProjection=true;
        assert(f.draw(102)==S_OK&&f.begins==1);
        assert(f.uiTextVs(&font,f.vsTags[&font])&&f.vsGeneration==2);
        const auto reads=f.backend.textureReads;
        assert(f.draw(103)==S_OK&&f.vsGeneration==2&&f.backend.textureReads==reads);
        // Next-frame perspective names reuse both proven identities. A reused
        // shader address loses the proof when CreateVertexShader replaces tags.
        f.backend.pixelProjection=false;f.backend.viewport.MaxZ=.94f;f.backend.z=f.backend.write=1;f.backend.function=4;
        assert(f.uiTextVs(&font,f.vsTags[&font]));
        assert(f.uiTextVs(&f.backend.vs,f.vsTags[&f.backend.vs]));
        f.vsTags[&font]=0;++f.vsGeneration;assert(!f.uiTextVs(&font,f.vsTags[&font]));
        assert(font.refs==1);
    }
    for(unsigned reject=0;reject<11;++reject){
        Fixture f;IDirect3DTexture9 atlas;f.stockUi=false;f.world=true;f.hdrUiPhase=true;
        f.backend.pixelProjection=true;f.backend.textures[0]=&atlas;
        switch(reject){case 0:f.backend.write=1;break;case 1:f.backend.z=1;f.backend.function=4;break;
            case 2:f.backend.viewport.MaxZ=.94f;break;case 3:atlas.usage=D3DUSAGE_RENDERTARGET;break;
            case 4:f.vc.world=true;break;case 5:f.vc.wmo=true;break;case 6:f.vc.skinned=true;break;
            case 7:f.vsTags[&f.backend.vs]=Fixture::kWaterTag;break;case 8:f.fullViewportOk=false;break;
            case 9:f.backbuffer=false;break;case 10:f.backend.failVs=true;break;}
        assert(f.draw(104)==S_OK&&!f.uiTextVs(&f.backend.vs,f.vsTags[&f.backend.vs])&&f.vsGeneration==0);
    }
    // The first main sky layer starts linear capture even before a celestial
    // source is visible. Subsequent layers reuse it; offscreen targets cannot.
    {Fixture f;f.backend.viewport.MinZ=.9990234375f;
        assert(f.hdrSkyAdmission()&&f.hdrSkyValid&&f.skyCaptures==1);
        assert(f.hdrSkyAdmission()&&f.skyCaptures==1);
        f.backbuffer=false;assert(!f.hdrSkyAdmission()&&f.skyCaptures==1);}
    for(unsigned reject=0;reject<7;++reject){
        Fixture f;f.backend.viewport.MinZ=.9990234375f;
        switch(reject){case 0:f.hdr.active=false;break;case 1:f.hdrUiPhase=true;break;
            case 2:f.backbuffer=false;break;case 3:f.backend.failViewport=true;break;
            case 4:f.backend.viewport.MinZ=0;f.backend.viewport.MaxZ=.94f;break;
            case 5:f.backend.viewport.MinZ=.998046875f;f.backend.viewport.MaxZ=.9990234375f;break;
            case 6:f.backend.viewport.MinZ=0;break;}
        assert(!f.hdrSkyAdmission()&&!f.hdrSkyValid&&f.skyCaptures==0);
    }
    {Fixture f;f.backend.viewport.MinZ=.9990234375f;f.failSky=true;
        assert(!f.hdrSkyAdmission()&&!f.hdrSkyValid&&f.skyCaptures==1);
        f.failSky=false;assert(f.hdrSkyAdmission()&&f.hdrSkyValid&&f.skyCaptures==2);}
    // Execute the extracted production timer predicates, with a counting clock:
    // quiet/untimed/SDR calls retain native submission and open no clock scope.
    for(bool sample:{false,true})for(bool untimed:{false,true})for(bool active:{false,true}){
        Fixture f;f.gateFrame=sample;f.gateUntimed=untimed;f.hdr.active=active;CpuScope::reads=0;
        assert(f.draw(101)==S_OK);const bool timed=sample&&!untimed&&active;
        assert(CpuScope::reads==(timed?6u:0u));
        assert(f.hdrAdmissionCalls==unsigned(timed)&&f.hdrTransferCalls==unsigned(timed));
        assert(f.cpuHdrAdmission==int(timed)&&f.cpuHdrBoundary==int(timed)&&f.cpuHdrTransfer==int(timed));
        assert(active?f.backend.presentation==101:f.backend.legacy==101);
    }
    {Fixture f;f.stockUi=false;f.world=true;f.backend.pixelProjection=true;
        assert(std::strcmp(f.hdrBoundarySource(),"orthographic-ui")==0);
        assert(f.draw(71)==S_OK&&f.hdrUiPhase&&f.effects==1&&f.backend.presentation==71);}
    {Fixture f;f.stockUi=false;f.world=true;f.backend.pixelProjection=true;f.backend.viewport.MaxZ=.94f;
        assert(!f.hdrBoundarySource());assert(f.draw(72)==S_OK&&!f.hdrUiPhase&&f.effects==0);}
    for(bool sceneTexture:{false,true})for(bool worldDepth:{false,true}){
        Fixture f;IDirect3DTexture9 atlas;f.backend.textures[0]=&atlas;atlas.usage=sceneTexture?D3DUSAGE_RENDERTARGET:0;
        f.stockUi=false;f.world=true;f.backend.scaledProjection=true;f.backend.viewport.MaxZ=worldDepth?.94f:1.f;
        const bool accepted=!sceneTexture&&!worldDepth;
        assert(f.draw(201)==S_OK&&f.hdrUiPhase==accepted&&f.effects==unsigned(accepted));
        assert(accepted?f.backend.presentation==201:f.backend.legacy==201);
        if(accepted){f.backend.scaledProjection=false;f.backend.pixelProjection=true;
            assert(f.draw(202)==S_OK&&f.backend.presentation==202&&f.begins==1&&f.effects==1);}
    }
    // Native pretransformed UI has no programmable VS/PS and no VS matrix.
    for(bool declaration:{false,true}){
        Fixture f;IDirect3DTexture9 atlas;f.world=true;f.backend.fixedVs=f.backend.fixedPs=true;
        if(declaration)f.backend.fvf=0;
        f.backend.textures[0]=&atlas;assert(f.fixedUiGeometry()&&f.hdrUiInputs());
        assert(f.draw(73)==S_OK&&f.hdrUiPhase&&f.effects==1&&f.backend.presentation==73);
    }
    for(unsigned rejected=0;rejected<4;++rejected){
        Fixture f;IDirect3DTexture9 atlas;f.world=true;f.backend.fixedVs=f.backend.fixedPs=true;f.backend.textures[0]=&atlas;
        if(rejected==0)f.backend.fvf=D3DFVF_XYZ;
        if(rejected==1){f.backend.z=1;f.backend.write=1;}
        if(rejected==2){f.backend.z=1;f.backend.function=4;}
        if(rejected==3){f.backend.fvf=0;f.backend.declaration.transformed=false;}
        assert(!f.fixedUiGeometry()&&f.draw(74)==S_OK&&!f.hdrUiPhase);
    }
    {Fixture f;IDirect3DTexture9 scene;scene.usage=D3DUSAGE_RENDERTARGET;f.backend.fixedVs=f.backend.fixedPs=true;
        f.backend.textures[0]=&scene;assert(!f.hdrUiInputs()&&f.draw(75)==S_OK&&!f.hdrUiPhase);}
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
