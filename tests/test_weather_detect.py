#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.198 (rain): NorthlightWeatherDetect::Detector (src/sky/weather_detect.h), the real header compiled natively,
and the real per-draw comparison extracted from renderer.cpp's drawHook over a mock mirror.
Signatures (format, aspect 1:16 / 1:2, widths 8/16/32), address reuse (a re-created address with another signature stops being a
candidate, generation bumps, hot cleared), volume/cube forget, reset, per-kind table slots (rain 2, snow 4; never-drawn first, lookalike flood), hot
rotation on every frame without draws, the tall-texture log, and the hook: stage 0 unknown / UI phase (applied) / no hot = no count, a match
accumulates primitives and draws. Native clang++, plain -O2 and ASan/UBSan. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile

r=fp.src('renderer.cpp').read_text()
hook=[l for l in r.split('\n') if l.strip().startswith('if(weatherDetect.hot&&mirrorState')]
assert len(hook)==1,'the draw hook comparison must exist exactly once'
mistline=[l for l in r.split('\n') if l.strip().startswith('else if(weatherDetect.mistArmed)')]
assert len(mistline)==1,'0.3.199 (rain mist): the mist test follows the hot comparison exactly once'
assert r.count('rainBlendDraw(rainBlend,claimed||mist,draw)')==2,'0.3.199 (rain mist): both gate paths skip a mist draw'
rl=r.split('\n');j0=next(i for i,l in enumerate(rl) if l.strip().startswith('bool mistDraw(UINT count){'));j1=next(i for i in range(j0,len(rl)) if rl[i]=='    }')
mistfn='\n'.join(rl[j0:j1+1])
rl=r.split('\n');i0=next(i for i,l in enumerate(rl) if 'template<class Draw> HRESULT rainBlendDraw(' in l);i1=next(i for i in range(i0,len(rl)) if rl[i]=='    }')
rainfn='\n'.join(rl[i0:i1+1])
rl=r.split('\n');k0=next(i for i,l in enumerate(rl) if l.strip().startswith('void rainMrtUnbind(){'));k1=next(i for i,l in enumerate(rl) if 'template<class Draw> HRESULT rainBlendDraw(' in l)
maskfn='\n'.join(rl[k0:k1]) # 0.3.202 (rain mask MRT): rainMrtUnbind, rainMaskEligible, rainMaskCaps, rainMaskCreate, the swap clear, rainMaskStart, rainMrtBegin/End/Draw, rainMaskPrepare
unbinds=[l for l in rl if l.strip().startswith('if(rainMrtBound&&!rainBlend)rainMrtUnbind();')];assert len(unbinds)==1,'0.3.202 (rain mask MRT): the hook line that unbinds RT1 for every other draw'
assert 'rainScrubDraw' not in r and 'rainMaskPass' not in r
SRC=r'''
#include "weather_detect.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
using namespace NorthlightWeatherDetect;using NorthlightWeather::Kind;
typedef unsigned UINT;typedef unsigned DWORD;typedef long HRESULT;
#define TRUE 1
#define SUCCEEDED(h) ((h)>=0)
#define FALSE 0
#define FAILED(h) ((h)<0)
enum D3DRENDERSTATETYPE{D3DRS_ALPHABLENDENABLE=27,D3DRS_SRCBLEND=19,D3DRS_DESTBLEND=20,D3DRS_BLENDOP=171,D3DRS_ZWRITEENABLE=14,D3DRS_COLORWRITEENABLE=168,D3DRS_COLORWRITEENABLE1=190,D3DRS_SEPARATEALPHABLENDENABLE=206,D3DRS_SRCBLENDALPHA=207,D3DRS_DESTBLENDALPHA=208,D3DRS_BLENDOPALPHA=209,D3DRS_SPECULARENABLE=29,D3DRS_SCISSORTESTENABLE=174};
enum D3DTEXTURESTAGESTATETYPE{D3DTSS_COLOROP=1,D3DTSS_COLORARG1=2,D3DTSS_COLORARG2=3,D3DTSS_ALPHAOP=4,D3DTSS_ALPHAARG1=5,D3DTSS_ALPHAARG2=6,D3DTSS_TEXCOORDINDEX=11,D3DTSS_TEXTURETRANSFORMFLAGS=24};
enum D3DSAMPLERSTATETYPE{D3DSAMP_MAGFILTER=5,D3DSAMP_MINFILTER=6,D3DSAMP_MIPFILTER=7};
enum{D3DTOP_DISABLE=1,D3DTOP_SELECTARG1=2,D3DTOP_MODULATE=4,D3DTA_DIFFUSE=0,D3DTA_CURRENT=1,D3DTA_TEXTURE=2,D3DTTFF_DISABLE=0,D3DTEXF_POINT=1};
struct RECT{long left=0,top=0,right=0,bottom=0;};
enum D3DMULTISAMPLE_TYPE{D3DMULTISAMPLE_NONE=0,D3DMULTISAMPLE_4_SAMPLES=4};enum D3DFORMAT:unsigned{D3DFMT_A8R8G8B8=21};enum D3DPOOL{D3DPOOL_DEFAULT=0};typedef DWORD D3DCOLOR;struct D3DRECT{long a,b,c,d;};
#define MAKEFOURCC(a,b,c,d) ((DWORD)(unsigned char)(a)|((DWORD)(unsigned char)(b)<<8)|((DWORD)(unsigned char)(c)<<16)|((DWORD)(unsigned char)(d)<<24))
#define D3DPS_VERSION(a,b) (0xFFFF0000|((a)<<8)|(b))
struct D3DCAPS9{DWORD NumSimultaneousRTs=0,PrimitiveMiscCaps=0,PixelShaderVersion=0;};
struct D3DSURFACE_DESC{UINT Width=0,Height=0;D3DMULTISAMPLE_TYPE MultiSampleType=D3DMULTISAMPLE_NONE;DWORD MultiSampleQuality=0;};struct D3DVIEWPORT9{DWORD X=0,Y=0,Width=0,Height=0;float MinZ=0,MaxZ=1;};
enum{D3DTEXF_NONE=0,D3DBLEND_ZERO=1,D3DBLENDOP_MAX=5,D3DCOLORWRITEENABLE_ALPHA=8,D3DCOLORWRITEENABLE_RED=1,D3DCLEAR_TARGET=1,D3DUSAGE_RENDERTARGET=1,D3DUSAGE_DEPTHSTENCIL=2,D3DBLEND_ONE=2,D3DBLEND_SRCCOLOR=3,D3DBLEND_SRCALPHA=5,D3DBLEND_INVSRCALPHA=6,D3DBLEND_DESTCOLOR=9,D3DBLENDOP_ADD=1,D3DPMISCCAPS_INDEPENDENTWRITEMASKS=0x4000,D3DPMISCCAPS_SEPARATEALPHABLEND=0x20000,D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING=0x80000};
static const DWORD kRainMaskMrtShader[]={1},kRainScrubShader[]={2};
static std::vector<std::string> lines;
static void sink(const char* l){lines.push_back(l);}
static int A=21,X8=22,A1=25,A4=26,R5G6B5=23,DXT5=0x35545844;
static int cell[64];static const void* P(int i){return &cell[i];}
static unsigned count(const char* prefix){unsigned n=0;for(auto& l:lines)if(l.rfind(prefix,0)==0)++n;return n;}
// The hook: the line extracted from Device::drawHook over a mock mirror.
struct Mirror{bool textureKnown[16]={};void* textures[16]={};};
struct Sample{unsigned primitives=0,draws=0;};
struct Surface{D3DSURFACE_DESC desc;void GetDesc(D3DSURFACE_DESC* d){*d=desc;}};
struct IDirect3DBaseTexture9{};
struct IDirect3DTexture9{Surface s;HRESULT GetSurfaceLevel(UINT,Surface** o){*o=&s;return 0;}};
typedef Surface IDirect3DSurface9;
struct IDirect3DPixelShader9{int id;};struct IDirect3DVertexShader9{int id;};
template<class T> static void drop(T*& p){p=nullptr;}
// One game draw as the device saw it: both targets, the pixel shader, every state the mask touches.
struct DrawRec{Surface* rt;Surface* rt1;IDirect3DPixelShader9* ps;DWORD colorWrite,colorWrite1,blend,sep,srcA,dstA,opA,src,dst,op;};
struct Ext{DWORD rs[256]={};DWORD tss[2][32]={};std::vector<std::pair<int,DWORD>> sets;
    Surface gameRT,gameDS;bool hasDS=true,peekDS=true;Surface* rt=&gameRT;Surface* rt1=nullptr;D3DVIEWPORT9 vp;IDirect3DTexture9 maskTex,depthMock;Surface msTarget;bool createFails=false,createMsFails=false,createDepthFails=false,stretchFails=false,colorFillFails=false,capsGetFails=false;unsigned msCreates=0,stretches=0,creates=0,depthCreates=0,colorFills=0,clears=0,rt1Binds=0,rt1Unbinds=0,rtSets=0,shaderCreates=0;D3DMULTISAMPLE_TYPE msCreatedType=D3DMULTISAMPLE_NONE;DWORD msCreatedQuality=0;
    D3DCAPS9 caps;IDirect3DPixelShader9 psMrt{1},psScrub{2},gamePs{9};IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexShader9 gameVs{7};IDirect3DVertexShader9* vsBound=nullptr;bool borrowOk=true;
    Mirror* mirror=nullptr;void* stage0=nullptr;unsigned textureReads=0; /* GetTexture: the device's stage 0, and the mirror learns it back */
    HRESULT GetTexture(DWORD st,IDirect3DBaseTexture9** o){++textureReads;*o=nullptr;if(st==0&&mirror&&stage0){mirror->textures[0]=stage0;mirror->textureKnown[0]=true;}return 0;}
    IDirect3DTexture9* tex[4]={};unsigned samplerSets=0;DWORD minFilter0=0,magFilter0=0;
    std::vector<DrawRec> draws;
    Ext(){caps.NumSimultaneousRTs=4;caps.PrimitiveMiscCaps=D3DPMISCCAPS_INDEPENDENTWRITEMASKS|D3DPMISCCAPS_SEPARATEALPHABLEND|D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING;caps.PixelShaderVersion=D3DPS_VERSION(3,0);}
    HRESULT GetDeviceCaps(D3DCAPS9* c){if(capsGetFails)return -1;*c=caps;return 0;}
    HRESULT GetRenderState(D3DRENDERSTATETYPE t,DWORD* v){*v=rs[t];return 0;}
    HRESULT SetRenderState(D3DRENDERSTATETYPE t,DWORD v){rs[t]=v;sets.push_back({int(t),v});return 0;}
    HRESULT GetTextureStageState(DWORD st,D3DTEXTURESTAGESTATETYPE t,DWORD* v){*v=tss[st][t];return 0;}
    HRESULT GetRenderTarget(DWORD,Surface** o){*o=rt;return 0;}
    HRESULT SetRenderTarget(DWORD i,Surface* s){++rtSets;if(i==1){if(s)++rt1Binds;else ++rt1Unbinds;rt1=s;return 0;}rt=s;vp=D3DVIEWPORT9();vp.Width=s->desc.Width;vp.Height=s->desc.Height;sr=RECT();sr.right=long(s->desc.Width);sr.bottom=long(s->desc.Height);return 0;} /* the viewport and the scissor rect reset to the whole target (RT0 only) */
    RECT sr;HRESULT GetScissorRect(RECT* o){*o=sr;return 0;}HRESULT SetScissorRect(const RECT* r){sr=*r;return 0;}
    HRESULT GetDepthStencilSurface(Surface** o){if(!hasDS)return -1;*o=&gameDS;return 0;}
    bool peekDepthStencilSurface(Surface*& o){if(!hasDS||!peekDS)return false;o=&gameDS;return true;}
    bool peekVertexShader(IDirect3DVertexShader9*& o){if(!borrowOk||!vsBound)return false;o=vsBound;return true;}
    bool peekPixelShader(IDirect3DPixelShader9*& o){if(!borrowOk||!ps)return false;o=ps;return true;}
    HRESULT GetVertexShader(IDirect3DVertexShader9** o){*o=vsBound;return 0;}HRESULT GetPixelShader(IDirect3DPixelShader9** o){*o=ps;return 0;}
    HRESULT SetPixelShader(IDirect3DPixelShader9* p){ps=p;return 0;}
    HRESULT SetTexture(DWORD st,IDirect3DTexture9* t){if(st<4)tex[st]=t;return 0;}
    HRESULT SetSamplerState(DWORD st,D3DSAMPLERSTATETYPE t,DWORD v){++samplerSets;if(st==0&&t==D3DSAMP_MINFILTER)minFilter0=v;if(st==0&&t==D3DSAMP_MAGFILTER)magFilter0=v;return 0;}
    HRESULT GetViewport(D3DVIEWPORT9* o){*o=vp;return 0;}HRESULT SetViewport(const D3DVIEWPORT9* v){vp=*v;return 0;}
    HRESULT Clear(DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD){++clears;return 0;}
    HRESULT ColorFill(Surface*,const RECT*,D3DCOLOR){++colorFills;return colorFillFails?-1:0;}
    HRESULT CreateTexture(UINT w,UINT h,UINT,DWORD usage,D3DFORMAT,D3DPOOL,IDirect3DTexture9** o,void*){
        if(usage&D3DUSAGE_DEPTHSTENCIL){++depthCreates;if(createDepthFails)return -2005530516;depthMock.s.desc.Width=w;depthMock.s.desc.Height=h;*o=&depthMock;return 0;}
        ++creates;if(createFails)return -2005530516;maskTex.s.desc.Width=w;maskTex.s.desc.Height=h;*o=&maskTex;return 0;}
    HRESULT CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** o){++shaderCreates;*o=code[0]==1?&psMrt:&psScrub;return 0;}
    HRESULT CreateRenderTarget(UINT w,UINT h,D3DFORMAT,D3DMULTISAMPLE_TYPE t,DWORD q,int,Surface** o,void*){++msCreates;if(createMsFails)return -2005530516;msCreatedType=t;msCreatedQuality=q;msTarget.desc.Width=w;msTarget.desc.Height=h;msTarget.desc.MultiSampleType=t;msTarget.desc.MultiSampleQuality=q;*o=&msTarget;return 0;}
    HRESULT StretchRect(Surface* src,const RECT*,Surface* dst,const RECT*,int){++stretches;(void)src;(void)dst;return stretchFails?-1:0;}};
struct ExtensionDevice{struct RawScope{explicit RawScope(Ext&){}};};
struct VsClass{bool world=false,skinned=false;};
struct World{bool on=true;bool rainBlendSetting()const{return on;}};
namespace NorthlightWeather{}
struct SavedState{Ext* d;Surface* rt;D3DVIEWPORT9 vp;bool ok=true;static inline bool fail=false;SavedState(Ext* e,void*):d(e),rt(e->rt),vp(e->vp){ok=!fail;}~SavedState(){d->rt=rt;d->vp=vp;}};
struct Hook{
    Mirror mirrorState;Detector weatherDetect;Sample weatherSample;bool applied=false,terrain=true,enabled=true,failed=false;UINT width=0,height=0;IDirect3DTexture9* rainMask=nullptr;Surface* rainMaskSurface=nullptr;Surface* rainMaskMS=nullptr;D3DMULTISAMPLE_TYPE rainMaskMsType=D3DMULTISAMPLE_NONE,rainMaskWantType=D3DMULTISAMPLE_NONE;DWORD rainMaskMsQuality=0,rainMaskWantQuality=0;bool rainMaskFailed=false,rainMaskCleared=false,rainMaskDrawn=false,rainMaskFrame=false,rainMaskOk=false,rainMaskMismatchLogged=false;unsigned rainMaskDraws=0;
    IDirect3DTexture9* rainDepth=nullptr;IDirect3DPixelShader9 *rainMrtPS=nullptr,*rainScrubPS=nullptr;bool rainMrtBound=false,rainDepthOk=false,rainCapsChecked=false,rainCapsOk=false,rainSkipLogged=false;unsigned rainMrtRuns=0,rainMaskSkips=0;DWORD rainMrtPrev[5]={};bool rainMrtPrevKnown[5]={};
    std::unordered_map<IDirect3DVertexShader9*,unsigned> vsMajor;
    HRESULT rainMaskResolveHr=0;IDirect3DTexture9 depthTexObj;IDirect3DTexture9* depthTex=&depthTexObj;Surface* worldDepth=nullptr;int stateBlocks=0;
    unsigned snapshots=0,effectStates=0,quads=0;bool snapshotOk=true;IDirect3DTexture9* snapshotTarget=nullptr;bool snapshotFatal=true;DrawRec quadRec{};IDirect3DTexture9* quadTex0=nullptr;IDirect3DTexture9* quadTex1=nullptr;bool quadFails=false;
    bool resolveDepthInto(IDirect3DTexture9* target,bool fatal){++snapshots;snapshotTarget=target;snapshotFatal=fatal;ext->stage0=mirrorState.textures[0];ext->mirror=&mirrorState;mirrorState.textureKnown[0]=false;return snapshotOk;} /* its state-block Apply forgets the mirror's stage 0 */
    void effectState(){++effectStates;}
    HRESULT quad(UINT,UINT){++quads;quadRec={ext->rt,ext->rt1,ext->ps,ext->rs[D3DRS_COLORWRITEENABLE],0,0,0,0,0,0,0,0,0};quadTex0=ext->tex[0];quadTex1=ext->tex[1];return quadFails?-1:0;}
    VsClass vcMock;const VsClass& classifyVs(int){return vcMock;}std::vector<std::string> logs;template<class F> void extensionWork(const char*,F f){f();}
    void newFrame(){rainMrtUnbind();rainMaskCleared=rainMaskDrawn=rainMaskFrame=rainMaskOk=rainDepthOk=false;applied=false;} /* clearFrame's part */
    Ext extObj;Ext* ext=&extObj;World worldObj;World* world=&worldObj;bool claimedSkip=false;bool gateFrame=true;
    template<class Draw> HRESULT blobFaintDraw(bool claimed,Draw draw){return claimed?0:draw();}
    unsigned blendAtDraw[4]={};unsigned drawn=0;unsigned weatherMistSkips=0,weatherMistUnknown=0,weatherMistOtherStage=0,weatherMistReports=0,weatherStateReports=0,logged=0;
    template<class... A> void logf(const char* f,A...){++logged;logs.push_back(f);}
    void weatherDrawStates(UINT){++weatherStateReports;}
@MISTFN@
@MASKFN@
@RAINFN@
    HRESULT draw(UINT count){
        bool claimed=claimedSkip,rainBlend=false,mist=false;
@HOOK@
@MISTLINE@
@UNBINDLINE@
        auto draw=[&]{++drawn;blendAtDraw[0]=ext->rs[D3DRS_ALPHABLENDENABLE];blendAtDraw[1]=ext->rs[D3DRS_SRCBLEND];blendAtDraw[2]=ext->rs[D3DRS_DESTBLEND];blendAtDraw[3]=ext->rs[D3DRS_BLENDOP];
            ext->draws.push_back({ext->rt,ext->rt1,ext->ps,ext->rs[D3DRS_COLORWRITEENABLE],ext->rs[D3DRS_COLORWRITEENABLE1],ext->rs[D3DRS_ALPHABLENDENABLE],ext->rs[D3DRS_SEPARATEALPHABLENDENABLE],ext->rs[D3DRS_SRCBLENDALPHA],ext->rs[D3DRS_DESTBLENDALPHA],ext->rs[D3DRS_BLENDOPALPHA],ext->rs[D3DRS_SRCBLEND],ext->rs[D3DRS_DESTBLEND],ext->rs[D3DRS_BLENDOP]});return HRESULT(0);};
        const HRESULT hr=rainBlendDraw(rainBlend,claimed||mist,draw);
        return hr;
    }
    void bind(unsigned stage,int i,bool known=true){mirrorState.textures[stage]=const_cast<void*>(P(i));mirrorState.textureKnown[stage]=known;}
};
int main(){
    {   // classification: A8R8G8B8 (21) only, rain 1:16, snow 1:2, widths 8/16/32 only
        assert(classify(32,512,A)==Kind::Rain&&classify(16,256,A)==Kind::Rain&&classify(8,128,A)==Kind::Rain);
        assert(classify(32,64,A)==Kind::Snow&&classify(16,32,A)==Kind::Snow&&classify(8,16,A)==Kind::Snow);
        assert(classify(32,128,A)==Kind::None&&classify(16,64,A)==Kind::None&&classify(32,256,A)==Kind::None&&classify(32,32,A)==Kind::None&&classify(32,1024,A)==Kind::None); /* 1:4, 1:8, 1:1, old 1:32 */
        assert(classify(32,512,X8)==Kind::None&&classify(32,512,A1)==Kind::None&&classify(32,512,A4)==Kind::None&&classify(32,64,X8)==Kind::None&&classify(32,64,A1)==Kind::None&&classify(32,64,A4)==Kind::None&&classify(16,32,A4)==Kind::None);
        assert(classify(32,512,DXT5)==Kind::None&&classify(32,512,R5G6B5)==Kind::None&&classify(32,64,DXT5)==Kind::None&&classify(32,512,0)==Kind::None&&classify(32,512,20)==Kind::None&&classify(32,512,27)==Kind::None);
        assert(shapeOf(32,512)==Kind::Rain&&shapeOf(64,1024)==Kind::Rain&&shapeOf(1,16)==Kind::Rain&&shapeOf(32,64)==Kind::Snow&&shapeOf(64,128)==Kind::None&&shapeOf(32,128)==Kind::None&&shapeOf(0,0)==Kind::None);
        assert(classify(64,128,A)==Kind::None&&classify(33,66,A)==Kind::None&&classify(0,0,A)==Kind::None&&classify(32,0,A)==Kind::None);
        assert(classify(4,8,A)==Kind::None&&classify(24,48,A)==Kind::None&&classify(4,64,A)==Kind::None&&classify(64,1024,A)==Kind::None);
    }
    Detector d;d.sink=&sink;
    {   // candidates, generation, hot
        d.noteCreate(P(0),32,512,1,A);assert(d.count()==1&&d.hot==P(0)&&d.hotKind==Kind::Rain&&d.generation==1&&count("WEATHER candidate kind=rain 32x512")==1);
        d.noteCreate(P(1),32,64,1,A);assert(d.count()==2&&d.hot==P(0)&&d.generation==2&&count("WEATHER candidate kind=snow 32x64")==1);
        d.noteCreate(P(2),256,256,1,A);d.noteCreate(P(3),32,512,1,DXT5);d.noteCreate(nullptr,32,512,1,A);d.noteCreate(P(6),32,128,1,A);assert(d.count()==2&&d.generation==2);
    }
    {   // address reuse: P(0) re-created with another signature stops being a candidate; hot cleared
        d.noteCreate(P(0),256,256,1,A);assert(d.count()==1&&d.hot==nullptr&&d.hotKind==Kind::None&&d.generation==3&&!d.isCandidate(P(0))&&d.isCandidate(P(1)));
        d.rotate();assert(d.hot==P(1)&&d.hotKind==Kind::Snow);
        d.noteCreate(P(1),32,64,1,A);assert(d.count()==1&&d.isCandidate(P(1))&&d.hot==P(1)&&d.generation==5); /* removed (hot cleared), re-added: nothing was hot, so it is hot again */
    }
    {   // volume / cube: forget
        d.forget(P(1));assert(d.count()==0&&d.hot==nullptr);const unsigned g=d.generation;d.forget(P(1));d.forget(nullptr);assert(d.generation==g);
        d.noteCreate(P(4),32,512,1,A);assert(d.hot==P(4));d.forget(P(4));assert(d.count()==0&&d.hot==nullptr&&d.generation==g+2);
    }
    {   // reset
        d.noteCreate(P(5),32,512,1,A);d.reset();assert(d.count()==0&&d.hot==nullptr&&d.hotKind==Kind::None);d.rotate();assert(d.hot==nullptr);
    }
    {   // lookalike flood: ARGB textures of other aspects (1:4, 1:8, 1:1, DXT) never evict the rain or snow textures
        Detector o;o.noteCreate(P(10),32,512,1,A);o.noteCreate(P(11),32,512,1,A);o.noteCreate(P(12),32,64,1,A);
        const unsigned g=o.generation;
        for(int i=0;i<200;++i){o.noteCreate(P(13+i%40),32,128,1,A);o.noteCreate(P(13+i%40),16,64,1,A);o.noteCreate(P(13+i%40),32,32,1,A);o.noteCreate(P(13+i%40),32,512,1,DXT5);o.noteCreate(P(13+i%40),64,1024,1,A);}
        assert(o.count()==3&&o.overflows==0&&o.generation==g&&o.isCandidate(P(10))&&o.isCandidate(P(11))&&o.isCandidate(P(12)));
    }
    {   // 1:2 ARGB lookalikes (common) flood the snow slots only: rain is never evicted, the snow table stays at kSnowSlots
        Detector o;o.noteCreate(P(10),32,512,1,A);o.noteCreate(P(11),32,512,1,A);
        for(int i=0;i<100;++i)o.noteCreate(P(13+i%40),i&1?16:32,i&1?32:64,1,A);
        unsigned rain=0,snow=0;for(unsigned i=0;i<o.count();++i)(o.candidate(i).kind==Kind::Rain?rain:snow)++;
        assert(rain==2&&snow==kSnowSlots&&o.isCandidate(P(10))&&o.isCandidate(P(11))&&o.overflows==100-kSnowSlots);
    }
    {   // per-kind slots: a rain candidate replaces only rain, snow only snow; never-drawn first, else the oldest
        Detector o;o.noteCreate(P(10),32,512,1,A);o.noteCreate(P(11),32,512,1,A);
        for(int i=0;i<4;++i)o.noteCreate(P(12+i),32,64,1,A); /* snow P12..P15 */
        assert(o.count()==6&&o.overflows==0);
        // snow flood: all snow entries never drew, so the oldest goes each time; the rain pair stays
        o.noteCreate(P(16),32,64,1,A);assert(o.overflows==1&&!o.isCandidate(P(12))&&o.isCandidate(P(13))&&o.isCandidate(P(16))&&o.isCandidate(P(10))&&o.isCandidate(P(11)));
        // the oldest snow entry (P13) drew: a lookalike evicts the next never-drawn P14 instead
        while(o.hot!=P(13))o.rotate();o.endFrame(true);
        o.noteCreate(P(17),32,64,1,A);assert(o.overflows==2&&o.isCandidate(P(13))&&!o.isCandidate(P(14))&&o.isCandidate(P(17))&&o.isCandidate(P(10))&&o.isCandidate(P(11)));
        // all snow drew: the oldest of the kind goes
        for(unsigned k=0;k<o.count();++k)if(o.candidate(k).kind==Kind::Snow){while(o.hot!=o.candidate(k).raw)o.rotate();o.endFrame(true);}
        const void* oldest=nullptr;for(unsigned k=0;k<o.count();++k)if(o.candidate(k).kind==Kind::Snow){oldest=o.candidate(k).raw;break;}
        o.noteCreate(P(18),32,64,1,A);assert(!o.isCandidate(oldest)&&o.isCandidate(P(18)));
        // rain: P10 drew, P11 did not: a new rain candidate replaces P11
        while(o.hot!=P(10))o.rotate();o.endFrame(true);
        o.noteCreate(P(19),32,512,1,A);assert(o.isCandidate(P(10))&&!o.isCandidate(P(11))&&o.isCandidate(P(19))&&o.count()==6);
        // the hot candidate evicted: hot cleared
        Detector q;q.noteCreate(P(20),32,512,1,A);q.noteCreate(P(21),32,512,1,A);assert(q.hot==P(20));q.noteCreate(P(22),32,512,1,A);assert(!q.isCandidate(P(20))&&q.hot==P(22)&&q.count()==2);
    }
    {   // rotation: every candidate is hot within <= 4 frames, whether or not the tracker is active (endFrame(false) is all that matters)
        Detector o;for(int i=0;i<4;++i)o.noteCreate(P(30+i),32,i&1?64:512,1,A);bool seen[4]={};
        for(int f=0;f<4;++f){for(int i=0;i<4;++i)if(o.hot==P(30+i))seen[i]=true;o.endFrame(false);}
        assert(seen[0]&&seen[1]&&seen[2]&&seen[3]);o.endFrame(false);assert(o.hot==P(31)&&o.hotKind==Kind::Snow);
        o.setOff(true);assert(o.hot==nullptr);o.endFrame(false);assert(o.hot==nullptr);o.noteCreate(P(40),32,512,1,A);assert(o.hot==nullptr);
        o.setOff(false);o.endFrame(false);assert(o.hot!=nullptr);
    }
    {   // hot stays while it draws, leaves the frame it draws nothing
        Detector o;o.noteCreate(P(50),32,512,1,A);o.noteCreate(P(51),32,512,1,A);o.noteCreate(P(52),32,64,1,A);assert(o.hot==P(50));
        for(int f=0;f<10;++f){o.endFrame(true);assert(o.hot==P(50)&&o.hotKind==Kind::Rain);}
        o.endFrame(false);assert(o.hot==P(51));
        o.endFrame(false);assert(o.hot==P(52)&&o.hotKind==Kind::Snow);
        o.endFrame(false);assert(o.hot==P(50)); /* wraps */
        Detector e;e.endFrame(true);e.endFrame(false);assert(e.hot==nullptr); /* no candidates */
    }
    {   // tall log: any format with h>=4w, the first 8 only
        lines.clear();Detector t;t.sink=&sink;
        for(int i=0;i<12;++i)t.noteCreate(P(i),16,64+i,1,DXT5);t.noteCreate(P(20),64,64,1,A);t.noteCreate(P(21),32,128,1,A);
        assert(t.tallSeen==13&&count("WEATHER tall ")==8&&count("WEATHER tall w=16 h=64 fmt=894720068 levels=1")==1&&t.count()==0);
        Detector n;n.noteCreate(P(1),32,512,1,A);assert(n.count()==1); /* no sink: silent */
    }
    {   // shape log: every rain/snow aspect create in any format, capped per kind, independent of the tall log
        lines.clear();Detector t;t.sink=&sink;
        t.noteCreate(P(0),32,512,1,DXT5);t.noteCreate(P(1),32,512,1,X8);t.noteCreate(P(2),64,1024,1,A);t.noteCreate(P(3),32,64,1,DXT5);t.noteCreate(P(4),128,256,1,A);t.noteCreate(P(5),32,128,1,A);
        assert(count("WEATHER shape kind=rain w=32 h=512 fmt=894720068 levels=1 matched=0")==1&&count("WEATHER shape kind=rain w=32 h=512 fmt=22 levels=1 matched=0")==1);
        assert(count("WEATHER shape kind=rain w=64 h=1024 fmt=21 levels=1 matched=0")==1&&count("WEATHER shape kind=snow w=32 h=64 fmt=894720068 levels=1 matched=0")==1);
        assert(count("WEATHER shape kind=snow")==1&&count("WEATHER shape ")==4); /* 128x256 is 1:2 but wider than 32; 32x128 is neither */
        assert(count("WEATHER tall ")==4&&t.tallSeen==4); /* the 3 rain-aspect creates and 32x128 are tall (h>=4w) */
        lines.clear();Detector c;c.sink=&sink;
        for(int i=0;i<100;++i){c.noteCreate(P(i%60),16,256,1,DXT5);c.noteCreate(P(i%60),16,32,1,DXT5);}
        assert(count("WEATHER shape kind=rain")==kShapeLogs&&count("WEATHER shape kind=snow")==kShapeLogs&&c.rainShapeSeen==100&&c.snowShapeSeen==100);
        lines.clear();Detector k;k.sink=&sink; /* tall flood does not eat the shape logs */
        for(int i=0;i<20;++i)k.noteCreate(P(i),16,100+i,1,DXT5);k.noteCreate(P(30),32,512,1,A);
        assert(count("WEATHER tall ")==kTallLogs&&count("WEATHER shape kind=rain")==1);
    }
    {   // A4R4G4B4 / X8R8G8B8 / A1R5G5B5 lookalikes of the snow and rain shapes never enter the table nor churn it
        Detector o;o.noteCreate(P(10),32,64,1,A);
        for(int i=0;i<300;++i){o.noteCreate(P(11+i%40),32,64,1,A4);o.noteCreate(P(11+i%40),16,32,1,A1);o.noteCreate(P(11+i%40),32,512,1,X8);}
        assert(o.count()==1&&o.overflows==0&&o.isCandidate(P(10))&&o.generation==1);
    }
    {   // the draw hook
        Hook h;h.weatherDetect.noteCreate(P(50),32,512,1,A);h.weatherDetect.noteCreate(P(51),32,64,1,A);
        h.draw(100);assert(h.weatherSample.draws==0); /* nothing bound, textureKnown false */
        h.bind(0,50,false);h.draw(100);assert(h.weatherSample.draws==0);
        h.bind(0,50);h.draw(100);h.draw(40);assert(h.weatherSample.draws==2&&h.weatherSample.primitives==140);
        h.bind(1,50);h.bind(0,51);h.draw(7);assert(h.weatherSample.draws==2); /* stage 0 only; the other candidate is not hot */
        h.bind(0,50);h.applied=true;h.draw(100);assert(h.weatherSample.draws==3&&h.weatherSample.primitives==240); /* 0.3.199: after the boundary the rest of the rain still counts */
        h.applied=false;h.weatherSample={};h.weatherSample.draws=2;h.weatherSample.primitives=140;h.weatherDetect.rotate();h.bind(0,51);h.draw(9);assert(h.weatherSample.draws==3&&h.weatherSample.primitives==149);
        h.weatherDetect.setOff(true);h.draw(9);assert(h.weatherSample.draws==3); /* mirror inactive: hot is null */
        Hook none;none.bind(0,50);none.draw(10);assert(none.weatherSample.draws==0); /* no candidates: hot null */
    }
    {   // 0.3.202 (rain mask MRT): the mask is written by the rain draw itself through render target 1. A mask-eligible Hook: effect size 100x50, game target and viewport set, RainBlend on,
        // the game's stage setup (stage 0 MODULATE texture x diffuse, nothing on stage 1), no vertex or pixel shader, the bound depth is the world depth
        auto mk=[](Hook& h){h.weatherDetect.noteCreate(P(50),32,512,1,A);h.weatherDetect.noteCreate(P(51),32,64,1,A);h.width=100;h.height=50;h.ext->gameRT.desc.Width=100;h.ext->gameRT.desc.Height=50;h.worldDepth=&h.ext->gameDS;
            h.ext->vp.X=3;h.ext->vp.Y=4;h.ext->vp.Width=90;h.ext->vp.Height=40;h.ext->sr.left=5;h.ext->sr.top=6;h.ext->sr.right=70;h.ext->sr.bottom=30;
            h.ext->rs[D3DRS_COLORWRITEENABLE]=15;h.ext->rs[D3DRS_ALPHABLENDENABLE]=1;h.ext->rs[D3DRS_SRCBLEND]=D3DBLEND_DESTCOLOR;h.ext->rs[D3DRS_DESTBLEND]=D3DBLEND_SRCCOLOR;h.ext->rs[D3DRS_BLENDOP]=D3DBLENDOP_ADD;h.ext->rs[D3DRS_ZWRITEENABLE]=0;
            h.ext->rs[D3DRS_COLORWRITEENABLE1]=15;h.ext->rs[D3DRS_SEPARATEALPHABLENDENABLE]=0;h.ext->rs[D3DRS_SRCBLENDALPHA]=D3DBLEND_SRCALPHA;h.ext->rs[D3DRS_DESTBLENDALPHA]=D3DBLEND_ZERO;h.ext->rs[D3DRS_BLENDOPALPHA]=D3DBLENDOP_ADD;h.ext->rs[D3DRS_SCISSORTESTENABLE]=1;
            h.ext->tss[0][D3DTSS_COLOROP]=D3DTOP_MODULATE;h.ext->tss[0][D3DTSS_COLORARG1]=D3DTA_TEXTURE;h.ext->tss[0][D3DTSS_COLORARG2]=D3DTA_CURRENT;h.ext->tss[0][D3DTSS_ALPHAOP]=D3DTOP_MODULATE;h.ext->tss[0][D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;h.ext->tss[0][D3DTSS_ALPHAARG2]=D3DTA_DIFFUSE;h.ext->tss[1][D3DTSS_COLOROP]=D3DTOP_DISABLE; /* the logged setup: color=4(2,1) alpha=4(2,1) stage1 color=1 */
            h.bind(0,50);};
        auto snap=[](Hook& h){std::vector<DWORD> v(h.ext->rs,h.ext->rs+256);return v;};
        {Hook h;mk(h);h.draw(10);h.draw(10);h.draw(10);assert(h.weatherSample.draws==3&&h.rainMaskDraws==3&&h.drawn==3&&h.snapshots==1&&h.ext->textureReads==1&&h.mirrorState.textureKnown[0]);} /* the snapshot's Apply forgets stage 0: relearnt, so every rain draw of the frame still matches */
        {Hook h;mk(h);const auto before=snap(h);const D3DVIEWPORT9 vp0=h.ext->vp;
            h.draw(10);assert(h.drawn==1&&h.ext->draws.size()==1&&h.rainMaskDraws==1&&h.rainMaskDrawn&&h.rainMaskCleared&&h.rainDepthOk&&h.rainMrtRuns==1&&h.rainMaskSkips==0&&h.logs.empty()); /* exactly ONE game draw */
            const DrawRec& g=h.ext->draws[0];
            assert(g.rt==&h.ext->gameRT&&g.rt1==&h.ext->maskTex.s&&g.ps==&h.ext->psMrt); /* RT0 untouched, RT1 = the mask target, rainMrtPS during the draw */
            assert(g.colorWrite==15&&g.colorWrite1==D3DCOLORWRITEENABLE_RED&&g.sep==0&&g.srcA==D3DBLEND_SRCALPHA&&g.dstA==D3DBLEND_ZERO&&g.opA==D3DBLENDOP_ADD&&g.blend==1&&g.src==D3DBLEND_SRCALPHA&&g.dst==D3DBLEND_INVSRCALPHA&&g.op==D3DBLENDOP_ADD); /* RainBlend's blend (colour and alpha) and the game's separate-alpha states stay; only RT1 is masked to red */
            assert(snap(h)==before&&h.ext->ps==nullptr&&h.ext->rt==&h.ext->gameRT&&h.ext->vp.X==vp0.X&&h.ext->vp.Width==vp0.Width&&h.ext->sr.right==70); /* every state back, no game target or viewport change */
            assert(h.rainMrtBound&&h.ext->rt1==&h.ext->maskTex.s&&h.ext->rt1Binds==1&&h.ext->rt1Unbinds==0); /* RT1 stays bound (lazy) */
            assert(h.snapshots==1&&h.snapshotTarget==h.rainDepth&&!h.snapshotFatal&&h.ext->colorFills==1&&h.ext->clears==0&&h.ext->creates==1&&h.ext->depthCreates==1&&h.ext->shaderCreates==2); /* one depth snapshot (non-fatal), ColorFill clear, no RT0 swap */
            h.draw(10);h.draw(10);assert(h.drawn==3&&h.rainMaskDraws==3&&h.rainMrtRuns==1&&h.ext->rt1Binds==1&&h.snapshots==1&&h.ext->colorFills==1&&h.ext->draws[2].rt1==&h.ext->maskTex.s); /* consecutive rain draws: no rebind, no second snapshot or clear */
            h.bind(0,3);h.draw(10);assert(h.drawn==4&&h.ext->draws[3].rt1==nullptr&&h.ext->draws[3].ps==nullptr&&!h.rainMrtBound&&h.ext->rt1Unbinds==1&&h.rainMaskDraws==3); /* a non-rain draw unbinds RT1 BEFORE it draws */
            h.bind(0,50);h.draw(10);assert(h.drawn==5&&h.rainMrtRuns==2&&h.ext->rt1Binds==2&&h.ext->draws[4].rt1==&h.ext->maskTex.s&&h.snapshots==1&&h.ext->colorFills==1); /* rebinds; the frame's snapshot and clear stay */
            h.newFrame();assert(!h.rainMrtBound&&h.ext->rt1==nullptr); /* clearFrame unbinds */
            h.draw(10);assert(h.snapshots==2&&h.ext->colorFills==2&&h.rainMaskDraws==5&&h.rainMrtRuns==3&&h.ext->creates==1&&h.ext->depthCreates==1); /* a new frame: one snapshot again */
            h.newFrame();h.applied=true;const unsigned n=h.drawn;h.draw(10);assert(h.drawn==n+1&&h.rainMaskDraws==5&&h.ext->draws.back().rt1==nullptr&&h.ext->draws.back().ps==nullptr&&h.rainMaskSkips==0); /* applied: counted and blended, no mask, not a skip */
        }
        {   // the mask-eligibility checks that make the same draw a plain one
            struct Case{const char* name;void (*apply)(Hook&);unsigned reason;};
            const Case cases[]={
                {"color op",[](Hook& h){h.ext->tss[0][D3DTSS_COLOROP]=D3DTOP_SELECTARG1;},16},{"color arg1",[](Hook& h){h.ext->tss[0][D3DTSS_COLORARG1]=D3DTA_DIFFUSE;},16},{"color arg2",[](Hook& h){h.ext->tss[0][D3DTSS_COLORARG2]=D3DTA_TEXTURE;},16},
                {"alpha op",[](Hook& h){h.ext->tss[0][D3DTSS_ALPHAOP]=D3DTOP_SELECTARG1;},16},{"alpha arg1",[](Hook& h){h.ext->tss[0][D3DTSS_ALPHAARG1]=D3DTA_CURRENT;},16},{"alpha arg2",[](Hook& h){h.ext->tss[0][D3DTSS_ALPHAARG2]=D3DTA_TEXTURE;},16},
                {"stage 1 on",[](Hook& h){h.ext->tss[1][D3DTSS_COLOROP]=D3DTOP_MODULATE;},16},{"texcoord index",[](Hook& h){h.ext->tss[0][D3DTSS_TEXCOORDINDEX]=1;},16},{"texture transform",[](Hook& h){h.ext->tss[0][D3DTSS_TEXTURETRANSFORMFLAGS]=2;},16},
                {"specular",[](Hook& h){h.ext->rs[D3DRS_SPECULARENABLE]=1;},16},{"game vertex shader",[](Hook& h){h.ext->vsBound=&h.ext->gameVs;},4},{"game pixel shader",[](Hook& h){h.ext->ps=&h.ext->gamePs;},8},
                {"game pixel shader (not borrowable)",[](Hook& h){h.ext->ps=&h.ext->gamePs;h.ext->borrowOk=false;},8},{"game vertex shader (not borrowable)",[](Hook& h){h.ext->vsBound=&h.ext->gameVs;h.ext->borrowOk=false;},4},
                {"another depth buffer",[](Hook& h){h.worldDepth=nullptr;},32},{"depth snapshot fails",[](Hook& h){h.snapshotOk=false;},32}};
            for(const Case& c:cases){Hook h;mk(h);c.apply(h);const auto before=snap(h);IDirect3DPixelShader9* ps0=h.ext->ps;
                h.draw(10);h.draw(10);
                if(!(h.drawn==2&&h.ext->draws.size()==2&&h.ext->draws[0].rt1==nullptr&&h.ext->draws[1].rt1==nullptr&&h.ext->draws[0].ps==ps0&&h.rainMaskDraws==0&&h.rainMaskSkips==2&&h.ext->rt1Binds==0&&!h.rainMaskDrawn&&snap(h)==before)){std::fprintf(stderr,"case %s\n",c.name);assert(false);}
                unsigned skipLogs=0;for(auto& l:h.logs)skipLogs+=l.rfind("WEATHER rain mask skip: reason=",0)==0;
                if(!(skipLogs==1)){std::fprintf(stderr,"case %s logs\n",c.name);assert(false);}
                if(c.reason==32&&c.name[0]=='a'){assert(h.snapshots==0);}else if(c.reason==32){assert(h.snapshots==1&&h.ext->colorFills==0);} /* a failed snapshot is tried once a frame; no clear */
                assert(h.ext->draws[0].colorWrite1==15&&h.ext->draws[0].sep==0&&h.ext->draws[0].colorWrite==15); /* plain draw: the game's states */
            }
            {Hook h;mk(h);h.draw(10);h.ext->tss[0][D3DTSS_COLOROP]=D3DTOP_SELECTARG1;h.draw(10);assert(h.drawn==2&&h.ext->draws[1].rt1==nullptr&&!h.rainMrtBound&&h.rainMaskSkips==1&&h.rainMaskDraws==1&&h.ext->rt1Unbinds==1&&h.rainMaskDrawn); /* eligible, then a rain draw with another setup: RT1 comes off before that draw */
                h.ext->tss[0][D3DTSS_COLOROP]=D3DTOP_MODULATE;h.draw(10);assert(h.rainMrtRuns==2&&h.snapshots==1&&h.ext->draws[2].rt1==&h.ext->maskTex.s);}
            for(unsigned model:{1u,2u}){Hook h;mk(h);h.vsMajor[&h.ext->gameVs]=model;h.ext->vsBound=&h.ext->gameVs;h.draw(10);h.draw(10);assert(h.drawn==2&&h.rainMaskDraws==2&&h.rainMaskSkips==0&&h.rainMrtRuns==1&&h.ext->draws[0].rt1==&h.ext->maskTex.s&&h.ext->draws[0].ps==&h.ext->psMrt&&h.ext->vsBound==&h.ext->gameVs&&h.ext->ps==nullptr);} /* the game's rain: a vs_1_x/vs_2_x vertex shader and the fixed-function pixel stages */
            {Hook h;mk(h);h.vsMajor[&h.ext->gameVs]=3;h.ext->vsBound=&h.ext->gameVs;h.draw(10);assert(h.drawn==1&&h.rainMaskDraws==0&&h.rainMaskSkips==1&&h.logs.size()==1&&h.logs[0].find("vsModel=%u")!=std::string::npos);} /* vs_3_0: no ps_2_0 behind it */
            {Hook h;mk(h);h.ext->vsBound=&h.ext->gameVs;h.draw(10);assert(h.logs.size()==1&&h.logs[0].rfind("WEATHER rain mask skip: reason=",0)==0);h.ext->vsBound=nullptr;h.ext->ps=nullptr;h.draw(10);assert(h.rainMaskDraws==1&&h.logs.size()==1);} /* the one-shot log; the next eligible draw masks */
        }
        {Hook h;mk(h);h.ext->colorFillFails=true;h.draw(10);assert(h.drawn==1&&h.rainMaskDraws==1&&h.ext->colorFills==1&&h.ext->clears==1&&h.ext->rt==&h.ext->gameRT&&h.ext->vp.X==3&&h.ext->vp.Width==90&&h.ext->sr.right==70&&h.ext->rs[D3DRS_SCISSORTESTENABLE]==1&&h.ext->rt1==&h.ext->maskTex.s);} /* ColorFill refused: the RT0-swap clear, the game's target, viewport and scissor back, then the same single draw */
        {Hook h;mk(h);h.ext->caps.NumSimultaneousRTs=1;h.draw(10);h.newFrame();h.draw(10);assert(h.drawn==2&&h.rainMaskDraws==0&&h.rainMaskSkips==2&&h.ext->rt1Binds==0&&count("x")==0&&h.logs.size()==2&&h.logs[0].rfind("WEATHER rain mask disabled: caps",0)==0&&h.logs[1].rfind("WEATHER rain mask skip: reason=",0)==0&&h.ext->creates==0&&h.snapshots==0);} /* caps: logged once, never retried, plain draws */
        {Hook h;mk(h);h.ext->caps.PrimitiveMiscCaps=D3DPMISCCAPS_SEPARATEALPHABLEND;h.draw(10);assert(h.rainMaskDraws==0&&h.logs[0].rfind("WEATHER rain mask disabled: caps",0)==0);}
        {Hook h;mk(h);h.ext->caps.PrimitiveMiscCaps=D3DPMISCCAPS_INDEPENDENTWRITEMASKS;h.draw(10);assert(h.rainMaskDraws==0);}
        {Hook h;mk(h);h.ext->caps.PrimitiveMiscCaps=D3DPMISCCAPS_INDEPENDENTWRITEMASKS|D3DPMISCCAPS_SEPARATEALPHABLEND;h.draw(10);assert(h.rainMaskDraws==0);} // no RT1 blending
        {Hook h;mk(h);h.ext->caps.PrimitiveMiscCaps=D3DPMISCCAPS_INDEPENDENTWRITEMASKS|D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING;h.draw(10);assert(h.rainMaskDraws==1);} // separate alpha blend not needed
        {Hook h;mk(h);h.ext->caps.PixelShaderVersion=D3DPS_VERSION(1,4);h.draw(10);assert(h.rainMaskDraws==0);}
        {Hook h;mk(h);h.ext->capsGetFails=true;h.draw(10);assert(h.rainMaskDraws==0&&h.drawn==1);}
        {Hook h;mk(h);h.terrain=false;h.draw(10);assert(h.drawn==1&&h.rainMaskDraws==0&&h.rainMaskSkips==0&&h.ext->colorFills==0&&h.ext->rt1Binds==0);}
        {Hook h;mk(h);h.world->on=false;h.draw(10);assert(h.drawn==1&&h.rainMaskDraws==0&&h.ext->draws[0].ps==nullptr);}
        {Hook h;mk(h);h.claimedSkip=true;h.draw(10);assert(h.drawn==0&&h.rainMaskDraws==0&&h.ext->colorFills==0&&h.ext->rt1Binds==0);}
        {Hook h;mk(h);h.weatherDetect.noteCreate(P(52),128,512,10,A);h.weatherDetect.mistArmed=true;h.bind(0,52);h.draw(10);assert(h.drawn==0&&h.rainMaskDraws==0);} /* mist: skipped */
        {Hook h;mk(h);h.enabled=false;h.draw(10);assert(h.drawn==1&&h.rainMaskDraws==0&&h.rainMaskSkips==0);}
        {Hook h;mk(h);h.ext->gameRT.desc.Width=99;h.draw(10);h.draw(10);assert(h.drawn==2&&h.rainMaskDraws==0&&h.ext->creates==0&&h.rainMaskSkips==2&&h.logs.size()==2&&h.logs[0].rfind("WEATHER rain mask skipped",0)==0&&h.logs[1].rfind("WEATHER rain mask skip: reason=%u",0)==0&&h.ext->draws[1].rt1==nullptr);} /* wrong size: skipped for the frame, each log once */
        {Hook h;mk(h);h.ext->gameRT.desc.MultiSampleType=D3DMULTISAMPLE_4_SAMPLES;h.draw(10);assert(h.drawn==1&&h.rainMaskDraws==0&&h.ext->creates==0&&h.ext->msCreates==0&&h.logs.size()==2);} /* RT 4x, depth none: skipped, logged once */
        {Hook h;mk(h);h.ext->gameRT.desc.MultiSampleType=D3DMULTISAMPLE_4_SAMPLES;h.ext->gameDS.desc.MultiSampleType=D3DMULTISAMPLE_4_SAMPLES;h.ext->gameDS.desc.MultiSampleQuality=1;h.draw(10);assert(h.drawn==1&&h.rainMaskDraws==0&&h.ext->msCreates==0);} /* same type, other quality: skipped */
        {Hook h;mk(h);h.ext->gameRT.desc.MultiSampleType=D3DMULTISAMPLE_4_SAMPLES;h.ext->gameRT.desc.MultiSampleQuality=2;h.ext->gameDS.desc.MultiSampleType=D3DMULTISAMPLE_4_SAMPLES;h.ext->gameDS.desc.MultiSampleQuality=2;
            h.draw(10);assert(h.drawn==1&&h.rainMaskDraws==1&&h.rainMaskMS==&h.ext->msTarget&&h.ext->msCreates==1&&h.ext->creates==1&&h.ext->msCreatedType==D3DMULTISAMPLE_4_SAMPLES&&h.ext->msCreatedQuality==2&&h.logs.empty()); /* RT 4x + depth 4x: MS twin made with the RT's type and quality */
            assert(h.ext->draws[0].rt1==&h.ext->msTarget&&h.ext->rt==&h.ext->gameRT&&h.ext->colorFills==1); /* the mask went to the MS surface through RT1, the MS target cleared */
            h.draw(10);h.newFrame();h.draw(10);assert(h.rainMaskDraws==3&&h.ext->msCreates==1&&h.ext->creates==1&&h.ext->colorFills==2);
            h.ext->gameRT.desc.MultiSampleType=D3DMULTISAMPLE_NONE;h.ext->gameDS.desc.MultiSampleType=D3DMULTISAMPLE_NONE;h.newFrame();h.draw(10);assert(h.ext->creates==2&&h.rainMaskMS==nullptr&&h.ext->draws.back().rt1==&h.ext->maskTex.s&&h.ext->rt1Unbinds>=1);} /* MSAA turned off without a release: recreated as a plain mask */
        {Hook h;mk(h);h.ext->gameRT.desc.MultiSampleType=D3DMULTISAMPLE_4_SAMPLES;h.ext->gameDS.desc.MultiSampleType=D3DMULTISAMPLE_4_SAMPLES;h.ext->createMsFails=true;h.draw(10);assert(h.drawn==1&&h.rainMaskFailed&&h.rainMaskMS==nullptr&&h.rainMaskSurface==nullptr&&h.logs.size()==2&&h.logs[0].rfind("WEATHER rain mask disabled",0)==0&&h.ext->rt1Binds==0);} /* MS target refused: all dropped, mask disabled */
        {Hook h;mk(h);h.ext->gameDS.desc.MultiSampleType=D3DMULTISAMPLE_4_SAMPLES;h.draw(10);assert(h.drawn==1&&h.rainMaskDraws==0&&h.ext->msCreates==0);} /* RT none, depth 4x: skipped */
        {Hook h;mk(h);h.ext->hasDS=false;h.worldDepth=nullptr;h.draw(10);assert(h.drawn==1&&h.rainMaskDraws==0&&h.rainMaskSkips==1);} /* no depth bound: no snapshot of the world depth, no mask */
        {Hook h;mk(h);h.ext->peekDS=false;h.draw(10);assert(h.rainMaskDraws==1);} /* the depth identity by Get when the mirror cannot answer */
        {Hook h;mk(h);h.ext->createFails=true;h.draw(10);assert(h.drawn==1&&h.rainMaskFailed&&h.logs.size()==2&&h.logs[0].rfind("WEATHER rain mask disabled",0)==0&&h.ext->rt1Binds==0);
            h.newFrame();h.draw(10);assert(h.drawn==2&&h.ext->creates==1&&h.logs.size()==2&&h.rainMaskDraws==0);} /* rain still draws, no second attempt, no second log */
        {Hook h;mk(h);h.ext->createDepthFails=true;h.draw(10);assert(h.drawn==1&&h.rainMaskFailed&&h.rainMaskDraws==0&&h.rainDepth==nullptr&&h.rainMaskSurface==nullptr&&h.logs.size()==2&&h.logs[0].rfind("WEATHER rain mask disabled (rain depth",0)==0);} /* the rain depth texture refused: the mask is off, rain draws as before */
        {   // the full-screen scrub in renderEffects (rainMaskPrepare)
            Hook h;mk(h);h.draw(10);h.draw(10);assert(h.rainMrtBound);
            IDirect3DTexture9* m=h.rainMaskPrepare();
            assert(m==h.rainMask&&!h.rainMrtBound&&h.ext->rt1==nullptr&&h.quads==1&&h.effectStates==1&&h.ext->stretches==0); /* RT1 off first; no MSAA: no resolve */
            assert(h.quadRec.rt==&h.ext->maskTex.s&&h.quadRec.ps==&h.ext->psScrub&&h.quadTex0==h.depthTex&&h.quadTex1==h.rainDepth&&h.quadRec.colorWrite==D3DCOLORWRITEENABLE_RED&&h.ext->minFilter0==D3DTEXF_POINT&&h.ext->magFilter0==D3DTEXF_POINT&&h.quadRec.rt1==nullptr); /* final depth (s0) vs the snapshot (s1), POINT, alpha only */
            assert(h.ext->rt==&h.ext->gameRT&&h.ext->vp.X==3); /* its own SavedState brought the game's target back */
        }
        {Hook h;mk(h);h.ext->gameRT.desc.MultiSampleType=D3DMULTISAMPLE_4_SAMPLES;h.ext->gameRT.desc.MultiSampleQuality=2;h.ext->gameDS.desc.MultiSampleType=D3DMULTISAMPLE_4_SAMPLES;h.ext->gameDS.desc.MultiSampleQuality=2;h.draw(10);
            assert(h.rainMaskPrepare()==h.rainMask&&h.ext->stretches==1&&h.quads==1);
            h.ext->stretchFails=true;h.quads=0;assert(h.rainMaskPrepare()==nullptr&&h.rainMaskResolveHr<0&&h.quads==0);} /* MSAA twin resolved once; a refused resolve = no mask */
        {Hook h;mk(h);h.draw(10);h.quadFails=true;assert(h.rainMaskPrepare()==nullptr&&h.rainMaskResolveHr<0);}
        {Hook h;mk(h);h.draw(10);SavedState::fail=true;assert(h.rainMaskPrepare()==nullptr);SavedState::fail=false;}
        {Hook h;mk(h);h.bind(0,3);h.draw(10);assert(h.rainMaskPrepare()==nullptr&&h.quads==0);} /* no rain this frame: no pass */
        {Hook h;mk(h);h.ext->tss[0][D3DTSS_COLOROP]=D3DTOP_SELECTARG1;h.draw(10);assert(h.rainMaskPrepare()==nullptr&&h.quads==0);} /* nothing drawn into the mask */
        {Hook h;mk(h);h.draw(10);h.rainDepthOk=false;assert(h.rainMaskPrepare()==nullptr);} /* no depth snapshot: no mask (streaks stay in the fog as before) */
        {   // dry frame: no mask draw, nothing touched, RT1 never set
            Hook h;mk(h);h.vcMock.world=true;h.bind(0,3);h.draw(10);h.draw(10);assert(h.drawn==2&&h.ext->sets.empty()&&h.ext->colorFills==0&&h.ext->creates==0&&h.ext->rtSets==0&&h.snapshots==0&&h.ext->draws[1].rt1==nullptr);
        }
    }
    {   // 0.3.199 (rain): RainBlend - rain draws run with SrcAlpha/InvSrcAlpha/Add and the game's previous states return; snow, non-weather, setting off and claimed draws are untouched
        auto game=[](Hook& h){h.ext->rs[D3DRS_ALPHABLENDENABLE]=1;h.ext->rs[D3DRS_SRCBLEND]=D3DBLEND_DESTCOLOR;h.ext->rs[D3DRS_DESTBLEND]=D3DBLEND_SRCCOLOR;h.ext->rs[D3DRS_BLENDOP]=D3DBLENDOP_ADD;h.ext->rs[D3DRS_ZWRITEENABLE]=0;};
        Hook h;h.weatherDetect.noteCreate(P(50),32,512,1,A);game(h);h.bind(0,50);
        h.draw(10);assert(h.drawn==1&&h.blendAtDraw[0]==1&&h.blendAtDraw[1]==D3DBLEND_SRCALPHA&&h.blendAtDraw[2]==D3DBLEND_INVSRCALPHA&&h.blendAtDraw[3]==D3DBLENDOP_ADD);
        assert(h.ext->rs[D3DRS_ALPHABLENDENABLE]==1&&h.ext->rs[D3DRS_SRCBLEND]==D3DBLEND_DESTCOLOR&&h.ext->rs[D3DRS_DESTBLEND]==D3DBLEND_SRCCOLOR&&h.ext->rs[D3DRS_BLENDOP]==D3DBLENDOP_ADD&&h.ext->rs[D3DRS_ZWRITEENABLE]==0);
        assert(h.ext->sets.size()==4); /* only the two differing states, set and restored */
        h.ext->rs[D3DRS_ALPHABLENDENABLE]=0;h.draw(10);assert(h.blendAtDraw[0]==1&&h.ext->rs[D3DRS_ALPHABLENDENABLE]==0); /* blend off before: on during, off after */
        h.ext->sets.clear();h.world->on=false;h.draw(10);assert(h.ext->sets.empty()&&h.blendAtDraw[1]==D3DBLEND_DESTCOLOR); /* setting off / Weather=0 */
        h.world->on=true;h.claimedSkip=true;const unsigned n=h.drawn;h.draw(10);assert(h.drawn==n&&h.ext->sets.empty()); /* claimed: the game's draw does not run, nothing touched */
        h.claimedSkip=false;h.weatherDetect.noteCreate(P(51),32,64,1,A);h.weatherDetect.reset();h.weatherDetect.noteCreate(P(51),32,64,1,A);h.bind(0,51);h.draw(10);assert(h.weatherDetect.hotKind==Kind::Snow&&h.ext->sets.empty()&&h.blendAtDraw[1]==D3DBLEND_DESTCOLOR); /* snow untouched */
        h.bind(0,3);h.draw(10);assert(h.ext->sets.empty()); /* not the hot texture */
        Hook nw;nw.weatherDetect.noteCreate(P(50),32,512,1,A);game(nw);nw.bind(0,50);nw.world=nullptr;nw.draw(10);assert(nw.ext->sets.empty()); /* no world */
    }
    {   // 0.3.199 (rain mist): signature 1:4 ARGB at widths 32/64/128 only; a separate table (never a rain/snow candidate, never hot)
        assert(isMistShape(128,512,A)&&isMistShape(64,256,A)&&isMistShape(32,128,A));
        assert(!isMistShape(256,1024,A)&&!isMistShape(16,64,A)&&!isMistShape(128,512,DXT5)&&!isMistShape(128,512,X8)&&!isMistShape(256,256,A)&&!isMistShape(128,256,A)&&!isMistShape(32,512,A));
        lines.clear();Detector m;m.sink=&sink;m.noteCreate(P(60),128,512,10,A);m.noteCreate(P(61),128,512,10,A);
        assert(m.mistCount()==2&&m.isMist(P(60))&&m.isMist(P(61))&&m.count()==0&&m.hot==nullptr&&m.generation==0&&count("WEATHER mist candidate 128x512 fmt=21 levels=10")==2);
        m.noteCreate(P(60),256,256,1,A);assert(m.mistCount()==1&&!m.isMist(P(60))); /* address reuse */
        m.forget(P(61));assert(m.mistCount()==0);
        m.noteCreate(P(62),128,512,10,A);m.mistArmed=true;m.reset();assert(m.mistCount()==0&&!m.mistArmed&&!m.isMist(P(62)));
        m.noteCreate(P(63),128,512,10,A);m.mistArmed=true;m.setOff(true);assert(!m.mistArmed);
        // a full table evicts the oldest unproven entry; proven mists stay
        Detector f;f.noteCreate(P(0),128,512,10,A);f.noteCreate(P(1),128,512,10,A);f.proveMist(P(0));f.proveMist(P(1));
        for(int i=0;i<100;++i)f.noteCreate(P(2+i%40),32,128,8,A);
        assert(f.mistCount()==kMistSlots&&f.isMist(P(0))&&f.isMist(P(1))&&f.mistOverflows==100-(kMistSlots-2)&&f.overflows==0);
        Detector g;for(unsigned i=0;i<kMistSlots;++i){g.noteCreate(P(i),128,512,10,A);g.proveMist(P(i));}
        g.noteCreate(P(20),128,512,10,A);assert(!g.isMist(P(0))&&g.isMist(P(1))&&g.isMist(P(20))); /* all proven: the oldest */
    }
    {   // 0.3.199 (rain mist): the draw hook - armed and the game's 2x modulate: skipped (not drawn, not counted as weather); otherwise drawn
        auto game=[](Hook& h){h.ext->rs[D3DRS_ALPHABLENDENABLE]=1;h.ext->rs[D3DRS_SRCBLEND]=D3DBLEND_DESTCOLOR;h.ext->rs[D3DRS_DESTBLEND]=D3DBLEND_SRCCOLOR;h.ext->rs[D3DRS_BLENDOP]=D3DBLENDOP_ADD;};
        Hook h;h.weatherDetect.noteCreate(P(50),32,512,10,A);h.weatherDetect.noteCreate(P(52),128,512,10,A);game(h);
        h.bind(0,52);h.draw(10);assert(h.drawn==1&&h.weatherMistSkips==0); /* unarmed (not raining): drawn */
        h.weatherDetect.mistArmed=true;h.draw(10);assert(h.drawn==1&&h.weatherMistSkips==1&&h.weatherSample.draws==0&&h.ext->sets.empty()); /* armed: skipped, nothing touched */
        h.bind(0,52,false);h.draw(10);assert(h.drawn==2&&h.weatherMistSkips==1); /* stage 0 unknown: drawn */
        h.bind(0,7);h.draw(10);assert(h.drawn==3&&h.weatherMistSkips==1); /* another texture */
        assert(h.weatherMistUnknown==1&&h.weatherMistReports==1&&h.weatherStateReports==0); /* the first match logs its states without using the rain report budget */
        h.bind(0,52);h.ext->rs[D3DRS_SRCBLEND]=D3DBLEND_SRCALPHA;h.draw(10);assert(h.drawn==3&&h.weatherMistSkips==2); /* any blend */
        h.bind(0,7);h.bind(1,52);h.draw(10);assert(h.drawn==4&&h.weatherMistSkips==2&&h.weatherMistOtherStage==1); /* mist on stage 1: drawn, counted */
        game(h);h.bind(1,7);h.bind(0,50);h.draw(10);assert(h.drawn==5&&h.weatherSample.draws==1&&h.weatherMistSkips==2); /* rain: drawn with RainBlend */
        for(int i=0;i<6;++i){h.bind(0,52);h.draw(10);}assert(h.weatherMistReports==4&&h.weatherMistSkips==8);
    }
    std::printf("PASS weather detect\n");
}
'''.replace('@MASKFN@',maskfn).replace('@UNBINDLINE@',unbinds[0]).replace('@HOOK@',hook[0]).replace('@MISTLINE@',mistline[0]).replace('@MISTFN@',mistfn).replace('@RAINFN@',rainfn)
with tempfile.TemporaryDirectory() as tmp:
    (Path(tmp)/'t.cpp').write_text(SRC)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/label
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,*fp.test_include_flags(),str(Path(tmp)/'t.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
