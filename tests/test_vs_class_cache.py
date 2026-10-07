#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.196 (task 12): per-draw overhead removal that must not change a rendering decision.

Native: the real VsClass/classifyVs (renderer.cpp) and the real WorldRenderer::lookupShader (world_renderer.h) compiled over mocks -
the same shader consecutively costs one lookup, a re-registration (generation bump) at the same address with a new tag invalidates the
entry, world == null classifies as non-world; the real NorthlightShadowBlobFilter::claim() body over a mock device - a successful peek
takes no GetTexture and AddRefs only for a Faint verdict, a failed peek falls back to the old GetTexture path with identical results.
Source: the peek-with-fallback and the borrowed/owned release in prepareDrawImpl, captureWater, fullViewport and beforeDraw.
Native clang++, plain -O2 and ASan/UBSan. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile

r=fp.src('renderer.cpp').read_text();w=fp.src('world_renderer.h').read_text();f=fp.src('shadow_blob_filter.h').read_text()
def member(text,signature):
    a=text.index(signature);b=text.index('{',a);depth=1;end=b+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[text.rfind('\n',0,a)+1:end]
cls_block=r[r.index('    struct VsClass {'):r.index('    std::unordered_map<IDirect3DPixelShader9*, uint64_t> psHashes;')]
lookup_block=w[w.index('    std::uint32_t worldShaderGen=0;'):w.index('    // 0.3.179 (T1): programs erased by registerShader')]
claim=member(f,'    Result claim(UINT primitiveCount)')

HARNESS=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
using uint64_t=std::uint64_t;
struct IDirect3DVertexShader9{};
struct World{unsigned wmoCalls=0;std::unordered_map<IDirect3DVertexShader9*,int> wmo;
    bool recognizesWmo(IDirect3DVertexShader9* s){++wmoCalls;return wmo.count(s)!=0;}
    bool isWorldShader(IDirect3DVertexShader9* s){return wmo.count(s)!=0;}
    bool isSkinnedShader(IDirect3DVertexShader9*){return false;}};
struct Cls{
    std::unordered_map<IDirect3DVertexShader9*,int> vsTags;std::unordered_map<IDirect3DVertexShader9*,uint64_t> vsHashes;
    World* world=nullptr;
@CLS@
    void registerVS(IDirect3DVertexShader9* s,int tag){++vsGeneration;vsTags[s]=tag;}
};
struct CaptureShader{int kind=0;};
struct Look{
    std::unordered_set<IDirect3DVertexShader9*> terrainShaders;std::unordered_map<IDirect3DVertexShader9*,CaptureShader> captureShaders;
@LOOK@
    void registerShader(IDirect3DVertexShader9* s,bool terrain,bool capture,int kind){++worldShaderGen;terrainShaders.erase(s);if(terrain)terrainShaders.insert(s);captureShaders.erase(s);if(capture)captureShaders.emplace(s,CaptureShader{kind});}
};
// claim() over a mock device
typedef int HRESULT;typedef unsigned UINT;typedef unsigned DWORD;typedef int INT;
#define FAILED(h) ((h)<0)
#define SUCCEEDED(h) ((h)>=0)
enum D3DRESOURCETYPE{D3DRTYPE_TEXTURE=3,D3DRTYPE_CUBETEXTURE=5};
enum D3DFORMAT{D3DFMT_UNKNOWN=0};
enum D3DRENDERSTATETYPE{D3DRS_ALPHABLENDENABLE=27,D3DRS_BLENDOP=171,D3DRS_SRCBLEND=19,D3DRS_DESTBLEND=20};
enum{D3DBLENDOP_ADD=1,D3DBLEND_ZERO=1,D3DBLEND_DESTCOLOR=9,D3DBLEND_SRCCOLOR=3};
struct D3DSURFACE_DESC{UINT Width=0,Height=0;D3DFORMAT Format=D3DFMT_UNKNOWN;};
struct IUnknown{int refs=1;void AddRef(){++refs;}void Release(){--refs;}};
struct IDirect3DBaseTexture9:IUnknown{D3DRESOURCETYPE GetType(){return D3DRTYPE_TEXTURE;}};
struct IDirect3DTexture9:IDirect3DBaseTexture9{HRESULT GetLevelDesc(UINT,D3DSURFACE_DESC* d){d->Width=32;d->Height=32;return 0;}};
struct Dev{IDirect3DTexture9* bound=nullptr;unsigned getTextures=0;
    HRESULT GetTexture(DWORD stage,IDirect3DBaseTexture9** out){++getTextures;(void)stage;if(!bound)return -1;bound->AddRef();*out=bound;return 0;}
    HRESULT GetRenderState(D3DRENDERSTATETYPE t,DWORD* v){*v=t==D3DRS_ALPHABLENDENABLE?1:t==D3DRS_BLENDOP?D3DBLENDOP_ADD:t==D3DRS_SRCBLEND?D3DBLEND_DESTCOLOR:D3DBLEND_ZERO;return 0;}};
struct Blob{
    struct Verdict{bool blob=true;UINT width=32,height=32;D3DFORMAT format=D3DFMT_UNKNOWN;unsigned checkedFrame=0;};
    enum class Claim{None,Skip,Faint};struct Result{Claim claim=Claim::None;IDirect3DBaseTexture9* original=nullptr;};
    Dev* d;bool (*peekTexture)(void*,DWORD,IDirect3DBaseTexture9*&)=nullptr;void* peekContext=nullptr;
    std::unordered_map<IDirect3DBaseTexture9*,Verdict> verdicts;unsigned frame=0,skippedThisFrame=0,skippedTotal=0,faintThisFrame=0,faintTotal=0,statesLogged=2;unsigned strength;
    IDirect3DTexture9 faintTex;
    static void release(IUnknown* p){if(p)p->Release();}
    Verdict evaluate(IDirect3DBaseTexture9*){return Verdict();}
    IDirect3DTexture9* ensureFaint(){return &faintTex;}
    void logStates(UINT,bool,const char*){}
@CLAIM@
};
static bool peekBound(void* c,DWORD stage,IDirect3DBaseTexture9*& out){auto* t=*static_cast<IDirect3DTexture9**>(c);if(stage||!t)return false;out=t;return true;}
int main(){
    {   // classifyVs
        IDirect3DVertexShader9 a,b;World world;Cls c;c.world=&world;c.registerVS(&a,1);c.registerVS(&b,2);world.wmo[&b]=1;
        assert(c.classifyVs(&a).entry==1&&c.vsCacheMisses==1&&c.vsCacheHits==0);
        for(int i=0;i<5;++i)assert(c.classifyVs(&a).entry==1);
        assert(c.vsCacheMisses==1&&c.vsCacheHits==5&&world.wmoCalls==1);                       // one lookup for six consecutive draws
        assert(c.classifyVs(&b).entry==2&&c.classifyVs(&b).wmo&&c.classifyVs(&b).world&&c.vsCacheMisses==2);
        assert(c.classifyVs(&a).entry==1&&!c.lastVs.wmo&&c.vsCacheMisses==3);                    // alternating shaders: correct, one entry only
        c.registerVS(&a,2);                                                                      // freed address registered again with a new tag
        assert(c.classifyVs(&a).entry==2&&c.vsCacheMisses==4);
        c.world=nullptr;c.registerVS(&b,1);const auto& v=c.classifyVs(&b);assert(v.entry==1&&!v.wmo&&!v.world&&!v.skinned);   // no world: all false
        IDirect3DVertexShader9 unknown;assert(c.classifyVs(&unknown).entry==0);
    }
    {   // lookupShader
        IDirect3DVertexShader9 a,b;Look l;l.registerShader(&a,true,false,0);l.registerShader(&b,false,true,7);
        assert(l.lookupShader(&a).terrain&&!l.lookupShader(&a).capture&&l.shaderLookupMisses==1&&l.shaderLookupHits==1);
        assert(!l.lookupShader(&b).terrain&&l.lookupShader(&b).capture&&l.lookupShader(&b).capture->kind==7&&l.shaderLookupMisses==2);
        const CaptureShader* p=l.lookupShader(&b).capture;
        for(int i=0;i<100;++i)l.captureShaders.emplace(reinterpret_cast<IDirect3DVertexShader9*>(std::uintptr_t(0x1000+i*16)),CaptureShader{});   // rehash: node pointers stay valid
        assert(l.lookupShader(&b).capture==p&&p->kind==7);
        l.registerShader(&b,true,false,0);                                                       // same address re-registered: the entry must not survive
        assert(l.lookupShader(&b).terrain&&!l.lookupShader(&b).capture);
        IDirect3DVertexShader9 none;assert(!l.lookupShader(&none).terrain&&!l.lookupShader(&none).capture);
    }
    for(int mode=0;mode<3;++mode){   // claim: 0 = peek hit, 1 = peek missing (old Get path), 2 = no peek installed
        IDirect3DTexture9 tex;Dev dev;dev.bound=&tex;IDirect3DTexture9* bound=&tex;Blob blob;blob.d=&dev;blob.strength=50;
        if(mode==0){blob.peekTexture=peekBound;blob.peekContext=&bound;}
        if(mode==1){bound=nullptr;blob.peekTexture=peekBound;blob.peekContext=&bound;}
        const auto res=blob.claim(10);
        assert(res.claim==Blob::Claim::Faint&&res.original==&tex);
        assert(tex.refs==2);                                                                     // the caller owns exactly one reference (peek: AddRef'd; Get: handed over)
        assert(dev.getTextures==(mode==0?0u:1u));
        tex.refs=1;dev.getTextures=0;blob.strength=0;                                            // Skip: nothing held
        const auto skip=blob.claim(10);assert(skip.claim==Blob::Claim::Skip&&tex.refs==1&&dev.getTextures==(mode==0?0u:1u));
        blob.strength=50;dev.getTextures=0;blob.verdicts.clear();blob.verdicts.emplace(&tex,Blob::Verdict()).first->second.blob=false;
        assert(blob.claim(10).claim==Blob::Claim::None&&tex.refs==1);                            // not a blob: nothing held
        dev.bound=nullptr;bound=nullptr;assert(blob.claim(10).claim==Blob::Claim::None);         // nothing bound anywhere
    }
    std::printf("PASS vs class cache, shader lookup cache, borrowed blob texture\n");
}
'''
src=HARNESS.replace('@CLS@',cls_block).replace('@LOOK@',lookup_block).replace('@CLAIM@','    '+claim.strip())
with tempfile.TemporaryDirectory(prefix='northlight-vs-cache-') as tmp:
    (Path(tmp)/'t.cpp').write_text(src)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-private-field',*flags,str(Path(tmp)/'t.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)

dm=member(r,'template<class Capture> void prepareDrawImpl(Capture capture)');bd=member(r,'    void beforeDraw(IDirect3DVertexShader9* vs)');cw=member(r,'template<class Draw> void captureWater(');fv=member(r,'bool fullViewport(D3DSURFACE_DESC& desc')
checks={
 'prepareDrawImpl: peekVertexShader with the GetVertexShader fallback, released only when not borrowed':
    'const bool borrowedVS=ext->peekVertexShader(vs);' in dm and 'if(!borrowedVS&&(FAILED(ext->GetVertexShader(&vs))||!vs))' in dm
    and '~ShaderRelease(){if(!borrowed)drop(p);}' in dm and 'if(!borrowedVS)drop(vs);' in dm and 'drop(vs);' not in dm.replace('if(!borrowedVS)drop(vs);','').replace('{++missingVS;drop(vs);return;}','').replace('if(!borrowed)drop(p);',''),
 'prepareDrawImpl post-effect sampled path: peek with fallback, classifyVs':
    'const bool borrowed=ext->peekVertexShader(late);' in dm and 'classifyVs(late)' in dm and 'if(!borrowed)drop(late);' in dm and 'world->isWorldShader' not in dm and 'world->isSkinnedShader' not in dm,
 'draw path classification through classifyVs (skinned lambda, non-world reject, shadow swap plan, beforeDraw)':
    'classifyVs(vs).skinned' in dm and 'classifyVs(vs).world' in dm and '(classifyVs(vs).entry&kTagMask)!=1' in member(r,'void planTerrainShadowSwap(')
    and 'const VsClass& vc=classifyVs(vs);' in bd and 'bool wmo=tag!=1&&vc.wmo;' in bd and 'vsTags.find' not in bd,
 'generation bumped before the maps change in CreateVertexShader registration':
    '[&]{++vsGeneration;std::vector<DWORD> words;auto h=shaderHash(*out,words);' in r and r.count('++vsGeneration')==1 and 'void registerShader(IDirect3DVertexShader9* shader,uint64_t hash){\n        ++worldShaderGen;' in w,
 'captureWater: peekDepthStencilSurface first, GetDepthStencilSurface fallback':
    'ext->peekDepthStencilSurface(currentDepth)' in cw and 'ext->GetDepthStencilSurface(&currentDepth)' in cw,
 'world capture paths use the shader lookup cache':
    'lookupShader(shader).terrain' in w and 'lookupShader(current).terrain' in w and 'lookupShader(current).capture' in w and 'terrainShaders.count(current)' not in w and 'terrainShaders.count(shader)!=0;' in w,
 'fullViewport: back buffer desc reused only when the render target is the back buffer itself; cleared in Reset':
    'rt==backSurface' in fv and 'backSurface=back;drop(back);' in fv and 'backDescKnown=false;backSurface=nullptr;' in r,
 'beforeDraw: cached world depth desc reused only for the held surface; invalidated when it is dropped':
    'worldDepth&&worldDepthDescKnown&&ext->peekDepthStencilSurface(ds)&&ds==worldDepth' in bd and 'if(!sameDepth){drop(worldDepth);worldDepthDescKnown=false;worldDepth=ds;worldDepthDesc=dd;worldDepthDescKnown=true;}' in bd
    and 'drop(worldDepth);worldDepthDescKnown=false;' in r and r.count('drop(worldDepth)')==2,
 'blob filter: borrowed peek, AddRef only for a Faint verdict, per-draw level desc guard kept':
    'peekTexture(peekContext,0,bound)' in claim and 'if(borrowed)bound->AddRef();guard.p=nullptr;' in claim and 'GetLevelDesc(0,&desc)' in claim and 'shadowBlobs->setTexturePeek(' in r,
 'cache counters on the DRAWGATE line, no per-draw clock reads': 'vsCacheHits=%llu vsCacheMisses=%llu' in r and 'QueryPerformanceCounter' not in member(r,'const VsClass& classifyVs('),
}
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
