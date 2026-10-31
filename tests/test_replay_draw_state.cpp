// Replay draw-state equivalence: the 0.3.142 binding cache (verbatim below) against
// NorthlightReplayDrawState::Cache on one recording fake device. Every draw is rasterized
// in software (R32F colour + D24 LESSEQUAL depth, ShadowReplayPS and LocalShadowPS
// semantics) from the state the device actually holds, and both images are compared bit
// for bit. Also: per-draw effective state, call counts and a microbenchmark.
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <tuple>
#include <type_traits>
#include <vector>
using UINT=unsigned;using DWORD=uint32_t;using HRESULT=int32_t;
constexpr HRESULT D3D_OK=0;
#define FAILED(x) ((x)<0)
constexpr unsigned D3DSAMP_ADDRESSU=1,D3DSAMP_ADDRESSV=2;
constexpr DWORD WRAP=1,MIRROR=2,CLAMP=3;
struct IDirect3DVertexDeclaration9{int id;};struct IDirect3DVertexShader9{int id;};
struct IDirect3DVertexBuffer9{int id;};struct IDirect3DIndexBuffer9{int id;};
enum class Alpha {Unorm,Snorm,Float};
struct IDirect3DBaseTexture9 {Alpha kind=Alpha::Unorm;unsigned size=8;std::vector<float> alpha;};
struct Calls {unsigned declarations=0,streams=0,indices=0,shaders=0,textures=0,samplers=0,constants=0;unsigned total()const{return declarations+streams+indices+shaders+textures+samplers+constants;}};
// COM-like: virtual, out of line, one recorded state per setter.
struct IDirect3DDevice9 {
    Calls calls;unsigned failAt=0,seen=0;
    IDirect3DVertexDeclaration9* decl=nullptr;IDirect3DVertexShader9* vs=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
    IDirect3DVertexBuffer9* vb[4]={};UINT offsets[4]={},strides[4]={};IDirect3DBaseTexture9* texture=nullptr;DWORD u=WRAP,v=WRAP;float c0[4]={};
    bool fail(){return failAt&&++seen==failAt;}
    virtual ~IDirect3DDevice9()=default;
    __attribute__((noinline)) virtual HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9* p){++calls.declarations;if(fail())return -1;decl=p;return 0;}
    __attribute__((noinline)) virtual HRESULT SetVertexShader(IDirect3DVertexShader9* p){++calls.shaders;if(fail())return -1;vs=p;return 0;}
    __attribute__((noinline)) virtual HRESULT SetIndices(IDirect3DIndexBuffer9* p){++calls.indices;if(fail())return -1;ib=p;return 0;}
    __attribute__((noinline)) virtual HRESULT SetStreamSource(unsigned s,IDirect3DVertexBuffer9* p,UINT o,UINT stride){++calls.streams;if(fail())return -1;vb[s]=p;offsets[s]=o;strides[s]=stride;return 0;}
    __attribute__((noinline)) virtual HRESULT SetTexture(unsigned s,IDirect3DBaseTexture9* p){assert(s==0);++calls.textures;if(fail())return -1;texture=p;return 0;}
    __attribute__((noinline)) virtual HRESULT SetSamplerState(unsigned s,unsigned state,DWORD value){assert(s==0);++calls.samplers;if(fail())return -1;(state==D3DSAMP_ADDRESSU?u:v)=value;return 0;}
    __attribute__((noinline)) virtual HRESULT SetPixelShaderConstantF(unsigned r,const float* p,unsigned n){assert(r==0&&n==1);++calls.constants;if(fail())return -1;std::memcpy(c0,p,16);return 0;}
};
#include "replay_draw_state.h"
// 0.3.142 replay_draw_state.h, verbatim apart from the namespace.
namespace Legacy {
class Cache {
    IDirect3DDevice9* d_;
    bool geometry_=false,material_=false;
    IDirect3DVertexDeclaration9* declaration_=nullptr;
    IDirect3DVertexShader9* shader_=nullptr;
    IDirect3DIndexBuffer9* index_=nullptr;
    IDirect3DVertexBuffer9* streams_[4]={};UINT offsets_[4]={},strides_[4]={};
    IDirect3DBaseTexture9* texture_=nullptr;DWORD u_=0,v_=0;float cutoff_=0;
public:
    /* Diagnostic only: successful setter calls issued by this pass. */
    struct Counts {unsigned declarations=0,streams=0,indices=0,shaders=0,textures=0,samplers=0,cutoffs=0;} counts;
    explicit Cache(IDirect3DDevice9* device):d_(device){}
    template<class Replay> HRESULT geometry(const Replay& p){
        HRESULT hr;
        if(!geometry_||declaration_!=p.decl){hr=d_->SetVertexDeclaration(p.decl);if(FAILED(hr)){geometry_=false;return hr;}declaration_=p.decl;++counts.declarations;}
        for(unsigned s=0;s<4;++s)if(!geometry_||streams_[s]!=p.stream[s]||offsets_[s]!=p.offset[s]||strides_[s]!=p.stride[s]){
            hr=d_->SetStreamSource(s,p.stream[s],p.offset[s],p.stride[s]);if(FAILED(hr)){geometry_=false;return hr;}
            streams_[s]=p.stream[s];offsets_[s]=p.offset[s];strides_[s]=p.stride[s];++counts.streams;
        }
        if(!geometry_||index_!=p.index){hr=d_->SetIndices(p.index);if(FAILED(hr)){geometry_=false;return hr;}index_=p.index;++counts.indices;}
        if(!geometry_||shader_!=p.shader){hr=d_->SetVertexShader(p.shader);if(FAILED(hr)){geometry_=false;return hr;}shader_=p.shader;++counts.shaders;}
        geometry_=true;return D3D_OK;
    }
    template<class Replay> HRESULT material(const Replay& p){
        HRESULT hr;
        if(!material_||texture_!=p.texture){hr=d_->SetTexture(0,p.texture);if(FAILED(hr)){material_=false;return hr;}texture_=p.texture;++counts.textures;}
        if(!material_||u_!=p.addressU){hr=d_->SetSamplerState(0,D3DSAMP_ADDRESSU,p.addressU);if(FAILED(hr)){material_=false;return hr;}u_=p.addressU;++counts.samplers;}
        if(!material_||v_!=p.addressV){hr=d_->SetSamplerState(0,D3DSAMP_ADDRESSV,p.addressV);if(FAILED(hr)){material_=false;return hr;}v_=p.addressV;++counts.samplers;}
        if(!material_||std::memcmp(&cutoff_,&p.cutoff,sizeof(float))){float c[]={1,1,1,p.cutoff};hr=d_->SetPixelShaderConstantF(0,c,1);if(FAILED(hr)){material_=false;return hr;}cutoff_=p.cutoff;++counts.cutoffs;}
        material_=true;return D3D_OK;
    }
};
}
struct Vertex {float x,y,z,w,u,v;};
struct Mesh {std::vector<std::array<Vertex,3>> triangles;};
struct Replay {
    IDirect3DVertexDeclaration9* decl=nullptr;IDirect3DVertexShader9* shader=nullptr;IDirect3DIndexBuffer9* index=nullptr;
    IDirect3DVertexBuffer9* stream[4]={};UINT offset[4]={},stride[4]={};IDirect3DBaseTexture9* texture=nullptr;DWORD addressU=WRAP,addressV=WRAP;float cutoff=-1;unsigned alphaMode=0;
    UINT start=0;int base=0;float dx=0,dy=0,dz=0; /* base: BaseVertexIndex (0.3.211, task 31); the vertices start at offset+base*stride */
};
// Geometry is found only through the bound stream/index state, never through the replay.
using GeometryKey=std::tuple<const void*,UINT,UINT,const void*,UINT,const void*>;
struct Scene {std::vector<Replay> replays;std::map<GeometryKey,const Mesh*> geometry;};
constexpr int N=64;
struct Image {std::vector<float> colour=std::vector<float>(N*N,1.f);std::vector<uint32_t> depth=std::vector<uint32_t>(N*N,0xffffffu);
    bool operator==(const Image& o)const{return !std::memcmp(colour.data(),o.colour.data(),colour.size()*4)&&depth==o.depth;}};
static float wrapCoord(float t,DWORD mode){
    if(mode==CLAMP)return std::fmin(std::fmax(t,0.f),1.f);
    if(mode==MIRROR){float f=t-2*std::floor(t*.5f);return f>1?2-f:f;}
    return t-std::floor(t);
}
// Nearest texel alpha; a null texture reads (0,0,0,1).
static float sampleAlpha(const IDirect3DBaseTexture9* t,float u,float v,DWORD au,DWORD av){
    if(!t)return 1;const unsigned n=t->size;
    unsigned x=std::min(n-1,unsigned(wrapCoord(u,au)*n)),y=std::min(n-1,unsigned(wrapCoord(v,av)*n));
    return t->alpha[y*n+x];
}
enum class Shader {Directional,Local};
struct Draw {const void* decl;const void* vs;std::array<const void*,4> vb;std::array<UINT,4> offsets,strides;const void* ib;const IDirect3DBaseTexture9* texture;DWORD u,v;float cutoff;};
static bool orderedLess(float a,float b){return a<b;} /* FOrdLessThan: NaN never kills */
static void rasterize(const IDirect3DDevice9& d,const Scene& scene,const Replay& p,Shader shader,Image& image){
    auto found=scene.geometry.find({d.vb[0],d.offsets[0]+UINT(p.base)*d.strides[0],d.strides[0],d.ib,p.start,d.decl});assert(found!=scene.geometry.end());
    for(auto tri:found->second->triangles){
        for(auto& q:tri){q.x+=p.dx;q.y+=p.dy;q.z+=p.dz*q.w;}
        auto edge=[](const Vertex& a,const Vertex& b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);};
        const float area=edge(tri[0],tri[1],tri[2].x,tri[2].y);if(area==0)continue;
        for(int y=0;y<N;++y)for(int x=0;x<N;++x){
            const float px=x+.5f,py=y+.5f;float w0=edge(tri[1],tri[2],px,py)/area,w1=edge(tri[2],tri[0],px,py)/area,w2=edge(tri[0],tri[1],px,py)/area;
            if(w0<0||w1<0||w2<0)continue;
            const float z=w0*tri[0].z+w1*tri[1].z+w2*tri[2].z,w=w0*tri[0].w+w1*tri[1].w+w2*tri[2].w;
            const float clipZ=z/w;if(clipZ<0||clipZ>1)continue;
            const float u=w0*tri[0].u+w1*tri[1].u+w2*tri[2].u,v=w0*tri[0].v+w1*tri[1].v+w2*tri[2].v;
            // ShadowReplayPS: texld; add r0.x,r0.w,-c0.w; texkill r0.x; oC0=v1.z.
            // LocalShadowPS: if(c0.w>=0){texld; texkill a-c0.w} oC0=saturate(z/w).
            if(shader==Shader::Directional||d.c0[3]>=0){if(orderedLess(sampleAlpha(d.texture,u,v,d.u,d.v)-d.c0[3],0))continue;}
            const float colour=shader==Shader::Directional?z:std::fmin(std::fmax(z/w,0.f),1.f);
            const uint32_t depth=uint32_t(std::lround(double(clipZ)*16777215.0));
            if(depth<=image.depth[y*N+x]){image.depth[y*N+x]=depth;image.colour[y*N+x]=colour;} /* LESSEQUAL, no blend, colour write 15 */
        }
    }
}
template<class C> struct Run {Image image;std::vector<Draw> draws;Calls calls;typename std::decay<decltype(C(nullptr).counts)>::type counts;};
template<class C> static Run<C> replay(const Scene& scene,Shader shader,IDirect3DBaseTexture9* entryTexture=nullptr){
    IDirect3DDevice9 d;d.texture=entryTexture;C cache(&d);Run<C> run;
    for(const auto& p:scene.replays){
        assert(!cache.geometry(p));assert(!cache.material(p));
        run.draws.push_back({d.decl,d.vs,{d.vb[0],d.vb[1],d.vb[2],d.vb[3]},{d.offsets[0],d.offsets[1],d.offsets[2],d.offsets[3]},{d.strides[0],d.strides[1],d.strides[2],d.strides[3]},d.ib,d.texture,d.u,d.v,d.c0[3]});
        rasterize(d,scene,p,shader,run.image);
    }
    run.calls=d.calls;run.counts=cache.counts;return run;
}
struct World {
    std::vector<IDirect3DVertexDeclaration9> decls=std::vector<IDirect3DVertexDeclaration9>(4);std::vector<IDirect3DVertexShader9> shaders=std::vector<IDirect3DVertexShader9>(24);
    std::vector<IDirect3DVertexBuffer9> vbs=std::vector<IDirect3DVertexBuffer9>(700);std::vector<IDirect3DIndexBuffer9> ibs=std::vector<IDirect3DIndexBuffer9>(700);
    std::vector<IDirect3DBaseTexture9> textures=std::vector<IDirect3DBaseTexture9>(160);std::vector<Mesh> meshes=std::vector<Mesh>(700);
};
// A pass shaped like the logged ones: mostly per-mesh cached buffers, a shared bulk buffer,
// models of consecutive submeshes, opaque/alpha runs, textures reused within a model.
static Scene makeScene(World& w,std::mt19937& rng,unsigned draws,double opaqueShare,bool perspective,bool signedTextures){
    std::uniform_real_distribution<float> unit(0,1);Scene scene;
    for(size_t i=0;i<w.textures.size();++i){auto& t=w.textures[i];t.kind=signedTextures&&i%3==0?Alpha::Snorm:Alpha::Unorm;t.alpha.resize(t.size*t.size);
        for(auto& a:t.alpha)a=t.kind==Alpha::Snorm?std::round((unit(rng)*2-1)*127)/127:std::round(unit(rng)*255)/255;
        if(i%5==0)for(size_t k=0;k<t.alpha.size();k+=3)t.alpha[k]=t.kind==Alpha::Snorm?-1.f:0.f;} /* extreme alphas: -1 and 0 */
    for(auto& m:w.meshes){m.triangles.clear();const float cx=unit(rng)*N,cy=unit(rng)*N,cz=.2f+.6f*unit(rng);const int n=1+int(unit(rng)*4);
        for(int k=0;k<n;++k){std::array<Vertex,3> t;for(auto& q:t){q.w=perspective?.5f+unit(rng):1.f;q.x=cx+(unit(rng)-.5f)*24;q.y=cy+(unit(rng)-.5f)*24;q.z=(cz+(unit(rng)-.5f)*.2f)*q.w;q.u=unit(rng)*3-1;q.v=unit(rng)*3-1;}m.triangles.push_back(t);}}
    unsigned meshCursor=0,bulkOffset=0,bulkStart=0;
    while(scene.replays.size()<draws){
        const unsigned submeshes=1+unsigned(unit(rng)*9);const bool bulk=unit(rng)<.1f;const auto decl=&w.decls[unsigned(unit(rng)*2)+(unit(rng)<.1f?2:0)];
        const auto shader=&w.shaders[unsigned(unit(rng)*w.shaders.size())];const unsigned textureBase=unsigned(unit(rng)*(w.textures.size()-8));
        const bool opaqueModel=unit(rng)<opaqueShare;
        for(unsigned k=0;k<submeshes&&scene.replays.size()<draws;++k){
            Replay p;p.decl=decl;p.shader=shader;const unsigned mesh=meshCursor++%w.meshes.size();
            if(bulk){p.stream[0]=&w.vbs[0];p.offset[0]=bulkOffset;bulkOffset+=4096;p.index=&w.ibs[0];p.start=bulkStart;bulkStart+=300;}
            else {p.stream[0]=&w.vbs[1+mesh%(w.vbs.size()-1)];p.index=&w.ibs[1+mesh%(w.ibs.size()-1)];}
            p.stride[0]=decl==&w.decls[0]?32:48;
            if(decl>=&w.decls[2]){p.stream[1]=&w.vbs[1+(mesh+7)%(w.vbs.size()-1)];p.stride[1]=16;}
            const bool opaque=opaqueShare<=0?false:opaqueShare>=1?true:opaqueModel?unit(rng)<.9f:unit(rng)<.2f; /* 0 and 1: no opaque / only opaque draws */
            p.cutoff=opaque?-1.f:std::array<float,3>{{.5f/255,(127+.5f)/255,128.f/255}}[unsigned(unit(rng)*3)];
            p.texture=opaque&&unit(rng)<.1f?nullptr:&w.textures[textureBase+unsigned(unit(rng)*(unit(rng)<.5f?2:8))];
            p.addressU=unit(rng)<.7f?WRAP:unit(rng)<.5f?CLAMP:MIRROR;p.addressV=unit(rng)<.7f?WRAP:unit(rng)<.5f?CLAMP:MIRROR;
            p.dx=(unit(rng)-.5f)*2;p.dy=(unit(rng)-.5f)*2;p.dz=(unit(rng)-.5f)*.01f;
            scene.geometry[{p.stream[0],p.offset[0],p.stride[0],p.index,p.start,p.decl}]=&w.meshes[mesh];
            scene.replays.push_back(p);
            if(unit(rng)<.06f&&scene.replays.size()<draws){Replay q=p;q.dx+=.25f;scene.replays.push_back(q);} /* mesh repeat, another pose */
        }
    }
    return scene;
}
static unsigned materialCalls(const Calls& c){return c.textures+c.samplers;}
int main(){
    {IDirect3DDevice9 d;NorthlightReplayDrawState::Cache cache(&d);Replay p;p.cutoff=.5f;
        for(unsigned mode:{0u,1u,2u,0u}){p.alphaMode=mode;const auto before=d.calls.constants;assert(!cache.material(p));assert(d.calls.constants==before+1&&d.c0[0]==float(mode)&&d.c0[3]==.5f);}
        const auto before=d.calls.constants;assert(!cache.material(p));assert(d.calls.constants==before);
        std::puts("PASS alpha source: equal cutoffs rebind when texture/color mask changes");}
    using New=NorthlightReplayDrawState::Cache;using Old=Legacy::Cache;
    static_assert(NorthlightReplayDrawState::SkipOpaqueMaterial,"shipped switch");
    assert(NorthlightReplayDrawState::skipOpaque());
    std::mt19937 rng(1423);World world;
    unsigned scenes=0,pixelsCompared=0,covered=0,opaqueDraws=0,keptDifferent=0;Calls oldTotal,newTotal;
    for(double share:{0.,.25,.5,.75,.9,1.})for(bool perspective:{false,true})for(bool snorm:{false,true})for(int repeat=0;repeat<6;++repeat){
        const Scene scene=makeScene(world,rng,579,share,perspective,snorm);
        const Shader shader=perspective?Shader::Local:Shader::Directional;
        const auto a=replay<Old>(scene,shader);const auto b=replay<New>(scene,shader);
        // (b) bit-identical colour and depth, same draw order.
        assert(a.image==b.image);++scenes;pixelsCompared+=N*N;for(auto z:a.image.depth)covered+=z!=0xffffffu;
        // (a) per draw: identical geometry, shader and cutoff; identical texture/address state
        // wherever the shader can read it; otherwise a texture this pass captured.
        std::set<const IDirect3DBaseTexture9*> passTextures;
        for(size_t i=0;i<a.draws.size();++i){const auto& x=a.draws[i];const auto& y=b.draws[i];const auto& p=scene.replays[i];passTextures.insert(p.texture);
            assert(x.decl==y.decl&&x.vs==y.vs&&x.vb==y.vb&&x.offsets==y.offsets&&x.strides==y.strides&&x.ib==y.ib&&!std::memcmp(&x.cutoff,&y.cutoff,4));
            assert(x.texture==p.texture&&x.u==p.addressU&&x.v==p.addressV&&x.cutoff==p.cutoff);
            if(p.cutoff>=0)assert(y.texture==x.texture&&y.u==x.u&&y.v==x.v);
            else {assert(y.cutoff==-1.f&&passTextures.count(y.texture));++opaqueDraws;keptDifferent+=y.texture!=x.texture||y.u!=x.u||y.v!=x.v;}
        }
        // (c) never more calls; geometry/cutoff calls unchanged; diagnostics reconstruct the old counts.
        assert(a.calls.declarations==b.calls.declarations&&a.calls.streams==b.calls.streams&&a.calls.indices==b.calls.indices&&a.calls.shaders==b.calls.shaders&&a.calls.constants==b.calls.constants);
        assert(b.calls.textures<=a.calls.textures&&b.calls.samplers<=a.calls.samplers);
        assert(b.counts.legacyTextures==a.counts.textures&&b.counts.legacySamplers==a.counts.samplers&&b.counts.textures==b.calls.textures&&b.counts.samplers==b.calls.samplers);
        if(share==0)assert(b.calls.textures==a.calls.textures&&b.calls.samplers==a.calls.samplers);
        if(share>=.5)assert(materialCalls(b.calls)<materialCalls(a.calls));
        auto add=[](Calls& t,const Calls& c){t.declarations+=c.declarations;t.streams+=c.streams;t.indices+=c.indices;t.shaders+=c.shaders;t.textures+=c.textures;t.samplers+=c.samplers;t.constants+=c.constants;};
        add(oldTotal,a.calls);add(newTotal,b.calls);
        if(repeat==0&&!snorm&&!perspective)std::printf("opaqueShare=%.2f draws=579 old: textures=%u samplers=%u total=%u new: textures=%u samplers=%u total=%u saved=%u (%.1f%% of binding calls)\n",
            share,a.calls.textures,a.calls.samplers,a.calls.total(),b.calls.textures,b.calls.samplers,b.calls.total(),a.calls.total()-b.calls.total(),100.0*(a.calls.total()-b.calls.total())/a.calls.total());
    }
    assert(keptDifferent>1000); /* the kept state really differs, and the image still matches */
    // The raster does see stage 0 where it is read: another texture or address mode on alpha draws changes the image.
    {Scene scene=makeScene(world,rng,579,.5,false,false);const auto reference=replay<Old>(scene,Shader::Directional);
     Scene textures=scene,modes=scene;for(auto& p:textures.replays)if(p.cutoff>=0)p.texture=&world.textures[(p.texture-&world.textures[0]+1)%world.textures.size()];
     for(auto& p:modes.replays)if(p.cutoff>=0)p.addressU=p.addressU==WRAP?MIRROR:WRAP;
     assert(!(replay<Old>(textures,Shader::Directional).image==reference.image)&&!(replay<Old>(modes,Shader::Directional).image==reference.image));}
    std::printf("PASS %u passes x 579 draws bit-identical R32F+D24 (directional and cube shaders, UNORM/SNORM/null, alpha extremes 0/-1): %u pixels, %u covered, opaque draws %u (kept other state %u)\n",scenes,pixelsCompared,covered,opaqueDraws,keptDifferent);
    std::printf("PASS calls old=%u new=%u (textures %u->%u samplers %u->%u; geometry/constants identical)\n",oldTotal.total(),newTotal.total(),oldTotal.textures,newTotal.textures,oldTotal.samplers,newTotal.samplers);
    // The first draw of a pass binds even when opaque: a foreign entry texture is never kept.
    {IDirect3DBaseTexture9 foreign;foreign.alpha.assign(64,0);Scene scene=makeScene(world,rng,40,1,false,false);scene.replays[0].cutoff=-1;
     IDirect3DDevice9 d;d.texture=&foreign;New cache(&d);assert(!cache.geometry(scene.replays[0])&&!cache.material(scene.replays[0]));assert(d.texture==scene.replays[0].texture&&d.calls.textures==1&&d.calls.samplers==2);}
    // Failure: a failed setter leaves the pass unbound; the next material binds everything, opaque or not.
    {Scene scene=makeScene(world,rng,60,.5,false,false);IDirect3DDevice9 d;New cache(&d);
     for(size_t i=0;i<scene.replays.size();++i){auto& p=scene.replays[i];assert(!cache.geometry(p));
         if(i==20){d.failAt=d.seen+1;p.cutoff=.25f;p.texture=&world.textures[3];assert(FAILED(cache.material(p)));d.failAt=0;
             Replay q=p;q.cutoff=-1;q.texture=&world.textures[4];q.addressU=CLAMP;const Calls before=d.calls;assert(!cache.material(q));
             assert(d.calls.textures==before.textures+1&&d.calls.samplers==before.samplers+2&&d.calls.constants==before.constants+1&&d.texture==q.texture&&d.u==CLAMP);continue;}
         assert(!cache.material(p));}}
    // Float alpha textures (the proof's only exception): once the game creates one, a new pass binds every draw again.
    {IDirect3DBaseTexture9 hdr;hdr.kind=Alpha::Float;hdr.alpha.assign(64,-5.f);Scene scene=makeScene(world,rng,200,.8,false,false);
     for(size_t i=0;i<scene.replays.size();i+=7)scene.replays[i].texture=&hdr;
     NorthlightReplayDrawState::noteTextureFormat(21);NorthlightReplayDrawState::noteTextureFormat(894720068);assert(NorthlightReplayDrawState::skipOpaque()); /* A8R8G8B8, DXT5 */
     const auto unsafe=replay<New>(scene,Shader::Directional);const auto reference=replay<Old>(scene,Shader::Directional);
     assert(!(unsafe.image==reference.image)); /* why the guard exists: a kept float texture kills opaque fragments */
     NorthlightReplayDrawState::noteTextureFormat(113);assert(!NorthlightReplayDrawState::skipOpaque());
     const auto guarded=replay<New>(scene,Shader::Directional);assert(guarded.image==reference.image&&guarded.calls.total()==reference.calls.total());
     {IDirect3DDevice9 d;New cache(&d);assert(!cache.skipping());}
     NorthlightReplayDrawState::floatAlphaTextures=false;std::puts("PASS guard: first draw binds, failure rebinds, float-alpha textures disable the skip (and would change the image without it)");}
    // Why draws keep their order: the colour is a separate interpolant from the D24 depth, so two
    // draws whose depths round to the same D24 value leave the colour of whichever came last.
    {Mesh near,nearer;near.triangles.push_back({{{0,0,.5f,1,0,0},{64,0,.5f,1,0,0},{0,64,.5f,1,0,0}}});nearer.triangles.push_back({{{0,0,.5f+1e-8f*5,1,0,0},{64,0,.5f+1e-8f*5,1,0,0},{0,64,.5f+1e-8f*5,1,0,0}}});
     IDirect3DVertexBuffer9 vb[2];IDirect3DIndexBuffer9 ib[2];IDirect3DVertexDeclaration9 decl;IDirect3DVertexShader9 vs;Scene forward;
     for(int k=0;k<2;++k){Replay p;p.decl=&decl;p.shader=&vs;p.stream[0]=&vb[k];p.stride[0]=32;p.index=&ib[k];forward.geometry[{&vb[k],0,32,&ib[k],0,&decl}]=k?&nearer:&near;forward.replays.push_back(p);}
     Scene sorted=forward;std::swap(sorted.replays[0],sorted.replays[1]);
     const auto x=replay<New>(forward,Shader::Directional),y=replay<New>(sorted,Shader::Directional);
     assert(x.image.depth==y.image.depth&&!(x.image==y.image));
     std::puts("PASS order dependence: equal D24 depth, different R32F colour when two draws swap (so replay draws are not sorted)");}
    // 0.3.211 (task 31): same-stride single-stream bulk replays share one stream binding (slot start; BaseVertexIndex selects the
    // vertices) and rasterize exactly like the old per-replay offsets with base 0.
    /* streams: the first draw binds all four (4); the old offsets rebind stream 0 for each of the two later draws (6). */
    {Mesh m[3];for(int k=0;k<3;++k)m[k].triangles.push_back({{{0,0,.3f+.1f*k,1,0,0},{64,0,.3f+.1f*k,1,0,0},{0,64,.3f+.1f*k,1,0,0}}});
     IDirect3DVertexBuffer9 vb;IDirect3DIndexBuffer9 ib;IDirect3DVertexDeclaration9 decl;IDirect3DVertexShader9 vs;Scene oldLayout,newLayout;
     for(int k=0;k<3;++k){Replay p;p.decl=&decl;p.shader=&vs;p.stream[0]=&vb;p.stride[0]=32;p.index=&ib;p.start=UINT(3*k);p.dx=.5f*k;
         const GeometryKey key{&vb,UINT(96*k),32,&ib,p.start,&decl};oldLayout.geometry[key]=&m[k];newLayout.geometry[key]=&m[k]; /* 3 vertices * 32 bytes each */
         p.offset[0]=UINT(96*k);oldLayout.replays.push_back(p);
         p.offset[0]=0;p.base=3*k;newLayout.replays.push_back(p);}
     const auto a=replay<New>(oldLayout,Shader::Directional),b=replay<New>(newLayout,Shader::Directional);
     assert(a.image==b.image&&a.counts.streams==6&&b.counts.streams==4&&b.calls.total()<a.calls.total());
     std::puts("PASS base layout: three same-stride bulk replays bind stream 0 once and rasterize identically to per-replay offsets");}
    // Microbenchmark: binding work per 579-draw pass on a virtual (COM-like) device.
    {Scene scene=makeScene(world,rng,579,.75,false,false);IDirect3DDevice9 d;const int iterations=4000;
     auto time=[&](auto tag){using C=decltype(tag);auto start=std::chrono::steady_clock::now();unsigned calls=0;
         for(int it=0;it<iterations;++it){d.calls={};C cache(&d);for(const auto& p:scene.replays){if(cache.geometry(p)||cache.material(p))std::abort();}calls+=d.calls.total();}
         return std::make_pair(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/iterations,calls/iterations);};
     for(int warm=0;warm<2;++warm){auto o=time(Old(nullptr));auto n=time(New(nullptr));
         if(warm)std::printf("BENCH opaqueShare=0.75 579 draws: old %.2f us/pass %u calls, new %.2f us/pass %u calls (proxy side only; DXVK front-end cost per call not modelled)\n",o.first,o.second,n.first,n.second);}}
}
