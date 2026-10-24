// Batched replay residency against the per-mesh cache (0.3.142), frame by frame. Two
// fake devices hold real buffer bytes; every replay draw fetches its indices and
// vertices only through the bound IB/VB, StartIndex and BaseVertexIndex, and is
// rasterized into R32F + D24 LESSEQUAL. Both images must match bit for bit while
// meshes expire, repeat, are evicted (memory-guard limit), fail to upload, are
// compacted and the cache is cleared (device reset). Built by test_replay_gpu_batches.py.
#include "replay_gpu_batches.h"
#include "replay_draw_state.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <set>
using Mesh=NorthlightDrawSnapshot::Mesh;using Owner=std::shared_ptr<const Mesh>;
static size_t alive=0;static unsigned buffersAlive=0;static std::mt19937 lockFaults(11);
struct Range {UINT first,end;};
template<class T,class D>struct Buffer final:T{
    unsigned refs=1;std::vector<unsigned char> data;D desc;const double* lockFailure=nullptr;std::vector<Range> written;
    explicit Buffer(size_t n):data(n){desc.Size=UINT(n);alive+=n;++buffersAlive;}~Buffer(){alive-=data.size();--buffersAlive;}
    unsigned AddRef()override{return ++refs;}unsigned Release()override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT GetDesc(D* d)override{*d=desc;return D3D_OK;}
    HRESULT Lock(UINT o,UINT n,void** p,DWORD flags)override{
        if((lockFailure&&*lockFailure>0&&std::uniform_real_distribution<double>(0,1)(lockFaults)<*lockFailure)||size_t(o)+n>data.size())return E_POINTER;
        // Write-once, fresh: every lock is NOOVERWRITE (upload_lock.h) and never overlaps an earlier region.
        assert(flags==D3DLOCK_NOOVERWRITE);for(auto r:written)assert(o+n<=r.first||o>=r.end);
        written.push_back({o,o+n});*p=data.data()+o;return D3D_OK;}
    HRESULT Unlock()override{return D3D_OK;}
};
using VB=Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>;using IB=Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>;
struct Decl:IDirect3DVertexDeclaration9 {bool multi;explicit Decl(bool m):multi(m){}unsigned AddRef()override{return 2;}unsigned Release()override{return 1;}HRESULT GetDeclaration(D3DVERTEXELEMENT9*,UINT*)override{return E_POINTER;}};
constexpr int N=64;
struct Image {std::vector<float> colour=std::vector<float>(N*N,1.f);std::vector<uint32_t> depth=std::vector<uint32_t>(N*N,0xffffffu);
    bool operator==(const Image& o)const{return !std::memcmp(colour.data(),o.colour.data(),colour.size()*4)&&depth==o.depth;}};
struct Calls {unsigned declarations=0,streams=0,indices=0,creates=0,draws=0,bulk=0;};
struct Device:IDirect3DDevice9{
    Calls calls;std::mt19937 faults{7};double createFailure=0,lockFailure=0;
    IDirect3DVertexDeclaration9* decl=nullptr;IDirect3DVertexBuffer9* vb[4]={};UINT offsets[4]={},strides[4]={};IDirect3DIndexBuffer9* ib=nullptr;float c0[4]={};
    bool fault(double p){return p>0&&std::uniform_real_distribution<double>(0,1)(faults)<p;}
    HRESULT CreateVertexBuffer(UINT n,DWORD usage,UINT,unsigned pool,IDirect3DVertexBuffer9** out,void*)override{assert(usage==D3DUSAGE_WRITEONLY&&pool==D3DPOOL_DEFAULT);++calls.creates;if(fault(createFailure))return E_POINTER;auto p=new VB(n);p->lockFailure=&lockFailure;*out=p;return D3D_OK;}
    HRESULT CreateIndexBuffer(UINT n,DWORD usage,D3DFORMAT format,unsigned pool,IDirect3DIndexBuffer9** out,void*)override{assert(usage==D3DUSAGE_WRITEONLY&&pool==D3DPOOL_DEFAULT&&format==D3DFMT_INDEX32);++calls.creates;if(fault(createFailure))return E_POINTER;auto p=new IB(n);p->lockFailure=&lockFailure;*out=p;return D3D_OK;}
    HRESULT GetVertexShaderConstantF(UINT,float*,UINT)override{return E_POINTER;}HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9**)override{return E_POINTER;}HRESULT GetStreamSourceFreq(UINT,UINT*)override{return E_POINTER;}HRESULT GetStreamSource(UINT,IDirect3DVertexBuffer9**,UINT*,UINT*)override{return E_POINTER;}HRESULT GetIndices(IDirect3DIndexBuffer9**)override{return E_POINTER;}
    HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9* p)override{++calls.declarations;decl=p;return D3D_OK;}
    HRESULT SetStreamSource(UINT s,IDirect3DVertexBuffer9* p,UINT o,UINT stride)override{++calls.streams;vb[s]=p;offsets[s]=o;strides[s]=stride;return D3D_OK;}
    HRESULT SetIndices(IDirect3DIndexBuffer9* p)override{++calls.indices;ib=p;return D3D_OK;}
    HRESULT SetVertexShader(IDirect3DVertexShader9*)override{return D3D_OK;}
    HRESULT SetTexture(DWORD,IDirect3DBaseTexture9*)override{return D3D_OK;}
    HRESULT SetSamplerState(DWORD,DWORD,DWORD)override{return D3D_OK;}
    HRESULT SetPixelShaderConstantF(UINT,const float* p,UINT)override{std::memcpy(c0,p,16);return D3D_OK;}
    // Everything below reads only the bound device state.
    std::array<float,5> vertex(UINT index)const{
        const bool multi=static_cast<const Decl*>(decl)->multi;std::array<float,5> v;
        auto fetch=[&](unsigned s,size_t at,float* out,unsigned n){auto* b=static_cast<const VB*>(vb[s]);const size_t address=offsets[s]+size_t(index)*strides[s]+at;
            assert(b&&address+4*n<=b->data.size());std::memcpy(out,b->data.data()+address,4*n);};
        fetch(0,0,v.data(),3);fetch(multi?1:0,multi?0:12,v.data()+3,2);return v;
    }
    void triangle(const std::array<float,5>* t,Image& image)const{
        auto edge=[](const std::array<float,5>& a,const std::array<float,5>& b,float x,float y){return (b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]);};
        const float area=edge(t[0],t[1],t[2][0],t[2][1]);if(area==0)return;
        auto lo=[&](int k){return std::max(0,int(std::floor(std::min({t[0][k],t[1][k],t[2][k]}))));};auto hi=[&](int k){return std::min(N-1,int(std::ceil(std::max({t[0][k],t[1][k],t[2][k]}))));};
        for(int y=lo(1);y<=hi(1);++y)for(int x=lo(0);x<=hi(0);++x){const float px=x+.5f,py=y+.5f;
            const float w0=edge(t[1],t[2],px,py)/area,w1=edge(t[2],t[0],px,py)/area,w2=edge(t[0],t[1],px,py)/area;if(w0<0||w1<0||w2<0)continue;
            const float z=w0*t[0][2]+w1*t[1][2]+w2*t[2][2],u=w0*t[0][3]+w1*t[1][3]+w2*t[2][3],v=w0*t[0][4]+w1*t[1][4]+w2*t[2][4];
            if(c0[3]>=0&&(u-std::floor(u))+(v-std::floor(v))*.5f-c0[3]<0)continue; /* procedural alpha test */
            const uint32_t d=uint32_t(std::lround(double(z)*16777215.0));if(d<=image.depth[y*N+x]){image.depth[y*N+x]=d;image.colour[y*N+x]=z;}}
    }
    void DrawIndexedPrimitive(INT base,UINT,UINT,UINT start,UINT primitives,Image& image){++calls.draws;auto* b=static_cast<const IB*>(ib);assert(b&&size_t(start+3*primitives)*4<=b->data.size());
        for(UINT k=0;k<primitives;++k){std::array<float,5> t[3];for(unsigned j=0;j<3;++j){uint32_t index;std::memcpy(&index,b->data.data()+4*(start+3*k+j),4);t[j]=vertex(UINT(INT(index)+base));}triangle(t,image);}}
    void DrawPrimitive(UINT start,UINT primitives,Image& image){++calls.draws;for(UINT k=0;k<primitives;++k){std::array<float,5> t[3];for(unsigned j=0;j<3;++j)t[j]=vertex(start+3*k+j);triangle(t,image);}}
};
struct Replay {
    Owner shared;const Mesh& mesh()const{return *shared;}
    bool gpuCached=false,indexed=true;IDirect3DVertexBuffer9* stream[4]={};IDirect3DIndexBuffer9* index=nullptr;UINT offset[4]={},stride[4]={},start=0;INT base=0;
    IDirect3DVertexDeclaration9* decl=nullptr;IDirect3DVertexShader9* shader=nullptr;IDirect3DBaseTexture9* texture=nullptr;DWORD addressU=1,addressV=1;float cutoff=-1;unsigned alphaMode=0;
    Replay()=default;Replay(const Replay& o):shared(o.shared),indexed(o.indexed),decl(o.decl),cutoff(o.cutoff){}
    ~Replay(){for(auto& p:stream)if(p)p->Release();if(index)index->Release();}
};
static Decl singleDecl(false),multiDecl(true);
// layout: the model's vertex layout; few layouts, as in the logged passes (2-4 declarations).
static Owner makeMesh(std::mt19937& rng,float layout,bool big=false,unsigned maxTriangles=900){
    std::uniform_real_distribution<float> unit(0,1);auto m=std::make_shared<Mesh>();
    const bool multi=layout>=.95f,indexed=unit(rng)<.9f;
    const unsigned triangles=big?9000:20+unsigned(unit(rng)*unit(rng)*float(maxTriangles));const unsigned vertices=indexed?std::max(3u,triangles*2/3+2):3*triangles;
    m->streams[0].stride=multi?16:layout<.8f?48:layout<.9f?32:56;if(multi)m->streams[1].stride=8;
    m->streams[0].bytes.resize(size_t(vertices)*m->streams[0].stride);if(multi)m->streams[1].bytes.resize(size_t(vertices)*m->streams[1].stride);
    for(auto& b:m->streams[0].bytes)b=uint8_t(rng());for(auto& b:m->streams[1].bytes)b=uint8_t(rng());
    const float cx=unit(rng)*N,cy=unit(rng)*N,cz=.2f+.6f*unit(rng);
    for(unsigned i=0;i<vertices;++i){float p[5]={cx+(unit(rng)-.5f)*6,cy+(unit(rng)-.5f)*6,cz+(unit(rng)-.5f)*.1f,unit(rng)*2,unit(rng)*2};
        std::memcpy(m->streams[0].bytes.data()+size_t(i)*m->streams[0].stride,p,12);
        std::memcpy((multi?m->streams[1].bytes.data()+size_t(i)*m->streams[1].stride:m->streams[0].bytes.data()+size_t(i)*m->streams[0].stride+12),p+3,8);}
    if(indexed){m->indices.resize(size_t(triangles)*3);for(auto& i:m->indices)i=uint32_t(unit(rng)*vertices)%vertices;}
    m->vertexCount=vertices;m->primitiveCount=triangles;m->indexed=indexed;return m;
}
template<class C> struct World {
    Device device;C cache;Image image;Calls pass,last;bool raster=true;size_t maxCommitted=0,maxLive=0,uploads=0;
    std::vector<std::unique_ptr<Replay>> replays;IDirect3DVertexBuffer9* bulk[4]={};IDirect3DIndexBuffer9* bulkIndices=nullptr;
    void frame(const std::vector<Replay>& order){
        image=Image{};cache.beginFrame();replays.clear();
        auto admit=[](size_t){return true;};auto bind=[&](Replay& p){NorthlightReplayGPU::bindResident(cache,&device,p,true,admit);};
        for(const auto& r:order){replays.push_back(std::make_unique<Replay>(r));bind(*replays.back());}
        NorthlightReplayGPU::commitAdmissions(cache,replays,bind); /* as uploadReplay after its bind loop */
        uploads+=cache.uploaded();
        // Misses: the per-frame bulk layout of uploadReplay (0.3.211: single-stream replays at a stride multiple, bound at the slot start
        // and selected by BaseVertexIndex/start; multi-stream replays 16-byte padded with base 0), INDEX32 start.
        UINT totals[4]={},indices=0;
        auto single=[](const Replay& p){const auto& m=p.mesh();return !m.streams[0].bytes.empty()&&m.streams[0].stride&&m.streams[1].bytes.empty()&&m.streams[2].bytes.empty()&&m.streams[3].bytes.empty();};
        for(auto& p:replays)if(!p->gpuCached){const bool one=single(*p);
            for(unsigned s=0;s<4;++s){const auto& m=p->mesh().streams[s];p->stride[s]=m.stride;
                if(s==0&&one){const UINT at=(totals[0]+m.stride-1)/m.stride*m.stride;p->offset[0]=at;totals[0]=at+UINT(m.bytes.size());}
                else{p->offset[s]=totals[s];totals[s]+=UINT((m.bytes.size()+15)&~size_t(15));}}
            p->start=p->indexed?indices:0;p->base=0;indices+=UINT(p->mesh().indices.size());
            if(one){const UINT first=p->offset[0]/p->stride[0];if(p->indexed)p->base=INT(first);else p->start=first;}}
        for(unsigned s=0;s<4;++s)if(totals[s]){auto* b=new VB(totals[s]);bulk[s]=b;for(auto& p:replays)if(!p->gpuCached&&!p->mesh().streams[s].bytes.empty()){std::memcpy(b->data.data()+p->offset[s],p->mesh().streams[s].bytes.data(),p->mesh().streams[s].bytes.size());p->stream[s]=b;b->AddRef();}}
        for(auto& p:replays)if(!p->gpuCached&&single(*p))p->offset[0]=0; /* ring slot start; the copy above used rel */
        if(indices){auto* b=new IB(size_t(indices)*4);bulkIndices=b;for(auto& p:replays)if(!p->gpuCached&&p->indexed){std::memcpy(b->data.data()+4*p->start,p->mesh().indices.data(),p->mesh().indices.size()*4);p->index=b;b->AddRef();}}
        // The replay loop: the production binding cache, then the draw from device state only.
        last={};for(auto& p:replays)last.bulk+=!p->gpuCached;
        const Calls before=device.calls;NorthlightReplayDrawState::Cache bindings(&device);
        for(auto& p:replays){assert(!bindings.geometry(*p));assert(!bindings.material(*p));
            if(!raster){++device.calls.draws;continue;}
            if(p->indexed)device.DrawIndexedPrimitive(p->base,0,p->mesh().vertexCount,p->start,p->mesh().primitiveCount,image);else device.DrawPrimitive(p->start,p->mesh().primitiveCount,image);}
        last.streams=device.calls.streams-before.streams;last.indices=device.calls.indices-before.indices;last.draws=device.calls.draws-before.draws;
        pass.streams+=last.streams;pass.indices+=last.indices;pass.draws+=last.draws;pass.bulk+=last.bulk;
        replays.clear();for(auto& b:bulk)if(b){b->Release();b=nullptr;}if(bulkIndices){bulkIndices->Release();bulkIndices=nullptr;}
        maxCommitted=std::max(maxCommitted,cache.bytes());
    }
};
int main(){
    std::mt19937 rng(20260923);std::uniform_real_distribution<float> unit(0,1);
    // Models: consecutive submesh draws sharing one vertex layout; mostly 48-byte M2-like vertices.
    auto makeModel=[&](int i){std::vector<Owner> model;const float layout=unit(rng);const unsigned submeshes=1+unsigned(unit(rng)*9);
        for(unsigned k=0;k<submeshes;++k)model.push_back(makeMesh(rng,layout,i%40==0&&k==0));return model;};
    std::vector<std::vector<Owner>> models;for(int i=0;i<150;++i)models.push_back(makeModel(i));
    // Legacy per-mesh cache in one world, batched cache in the other; same meshes, same order.
    auto legacy=std::make_unique<World<NorthlightReplayGPU::Cache>>();auto paged=std::make_unique<World<NorthlightReplayGPU::BatchedCache>>();
    Calls steadyCalls[2];unsigned steady=0;
    unsigned frames=0,identical=0,compactions=0;size_t maxWaste=0,maxLivePaged=0;double wasteRatioSum=0;unsigned wasteSamples=0;unsigned maxPages=0,maxBuffersLegacy=0,maxBuffersPaged=0;
    for(int frame=0;frame<240;++frame,++frames){
        // Churn: models leave and arrive; a quarter of the draws come from a moving window.
        if(frame%25==24)for(int k=0;k<5;++k){const size_t i=size_t(unit(rng)*models.size());models[i]=makeModel(int(i));}
        if(frame==110){legacy->cache.clear();paged->cache.clear();} /* device reset */
        if(frame==150){paged->device.createFailure=.1;paged->device.lockFailure=.1;}if(frame==170){paged->device.createFailure=paged->device.lockFailure=0;}
        if(frame==190)paged->cache.setLimit(12u<<20); /* memory guard: halved-and-more cap, LRU trim of committed batches */
        if(frame==215)paged->cache.setLimit(64u<<20);
        // A moving window of about 70 visible models in a jittered order; 6% drawn twice (instances: mesh repeats).
        std::vector<Replay> order;const size_t window=size_t(frame*2)%models.size();
        for(size_t visible=0;visible<70;++visible){const auto& model=models[(window+visible*3+size_t(unit(rng)*3))%models.size()];
            for(int instance=unit(rng)<.06f?2:1;instance>0;--instance)for(const auto& mesh:model){Replay r;r.shared=mesh;r.indexed=mesh->indexed;
                r.decl=mesh->streams[1].bytes.empty()?&singleDecl:&multiDecl;r.cutoff=unit(rng)<.2f?.25f:-1.f;order.push_back(r);}}
        legacy->frame(order);paged->frame(order);
        if(!legacy->last.bulk&&!paged->last.bulk){++steady;for(int w=0;w<2;++w){const Calls& c=w?paged->last:legacy->last;steadyCalls[w].streams+=c.streams;steadyCalls[w].indices+=c.indices;steadyCalls[w].draws+=c.draws;}}
        assert(legacy->image==paged->image);++identical;
        // Accounting: every live cache buffer byte is committed; committed within the limit.
        assert(alive==legacy->cache.bytes()+paged->cache.bytes());
        const auto s=paged->cache.batchStats();assert(paged->cache.bytes()<=paged->cache.limit()&&s.batchBytes+s.separateBytes==paged->cache.bytes());
        maxWaste=std::max(maxWaste,paged->cache.bytes()-s.liveBytes);maxLivePaged=std::max(maxLivePaged,s.liveBytes);compactions=s.compactions;maxPages=std::max(maxPages,unsigned(s.batches));
        if(s.liveBytes>(8u<<20)){wasteRatioSum+=double(paged->cache.bytes())/double(s.liveBytes);++wasteSamples;}
        maxBuffersPaged=std::max(maxBuffersPaged,unsigned(s.batches+s.separateUploads));(void)maxBuffersLegacy;(void)maxLivePaged;
    }
    const auto s=paged->cache.batchStats();
    std::printf("PASS %u frames bit-identical (R32F+D24 raster from bound buffers): expiry churn, repeats, multi-stream/oversize per-mesh layout, device reset, 10%% create/lock faults, 12 MiB guard trim\n",identical);
    std::printf("calls per pass: legacy streams=%.1f indices=%.1f  batched streams=%.1f indices=%.1f  (draws=%.1f bulk legacy=%.1f batched=%.1f)\n",double(legacy->pass.streams)/frames,double(legacy->pass.indices)/frames,double(paged->pass.streams)/frames,double(paged->pass.indices)/frames,double(paged->pass.draws)/frames,double(legacy->pass.bulk)/frames,double(paged->pass.bulk)/frames);
    std::printf("creates: legacy=%u batched=%u  uploads MiB: legacy=%.1f batched=%.1f  committed max MiB: legacy=%.1f batched=%.1f  batched committed/live mean=%.3f maxWasteMiB=%.1f batches<=%u compactions=%u compactedMiB=%.1f batchFailures=%u batchedUploads=%u separateUploads=%u\n",
        legacy->device.calls.creates,paged->device.calls.creates,legacy->uploads/1048576.0,paged->uploads/1048576.0,legacy->maxCommitted/1048576.0,paged->maxCommitted/1048576.0,wasteSamples?wasteRatioSum/wasteSamples:0.,maxWaste/1048576.0,maxPages,compactions,s.compactedBytes/1048576.0,s.batchFailures,s.batchedUploads,s.separateUploads);
    std::printf("steady frames (every draw resident in both): %u  legacy streams=%.1f indices=%.1f  batched streams=%.1f indices=%.1f  draws=%.1f\n",steady,
        double(steadyCalls[0].streams)/steady,double(steadyCalls[0].indices)/steady,double(steadyCalls[1].streams)/steady,double(steadyCalls[1].indices)/steady,double(steadyCalls[1].draws)/steady);
    assert(steady>20&&steadyCalls[1].streams*2<steadyCalls[0].streams&&steadyCalls[1].indices*2<steadyCalls[0].indices&&paged->pass.indices<legacy->pass.indices);
    assert(s.batchFailures>0&&s.separateUploads>0&&paged->device.calls.creates<legacy->device.calls.creates);
    legacy.reset();paged.reset();assert(alive==0&&buffersAlive==0);
    // Scale (no raster): about 1800 meshes of 3-60 KB (the logged ~1700 entries / ~50 MB resident),
    // ~580 draws per pass from a slowly moving view, city churn. Residency cost of the batches.
    // Demand below the 64 MiB cap (as logged) and above it (eviction behaviour).
    for(unsigned maxTriangles:{2400u,3600u}){std::vector<std::vector<Owner>> scene;auto sceneModel=[&]{std::vector<Owner> model;const float layout=unit(rng);const unsigned n=1+unsigned(unit(rng)*8);
         for(unsigned k=0;k<n;++k)model.push_back(makeMesh(rng,layout,false,maxTriangles));return model;};
     for(int i=0;i<400;++i)scene.push_back(sceneModel());
     auto a=std::make_unique<World<NorthlightReplayGPU::Cache>>();auto b=std::make_unique<World<NorthlightReplayGPU::BatchedCache>>();a->raster=b->raster=false;
     size_t live=0,liveMax=0,committedMax[2]={};double ratio=0;unsigned samples=0,evictions[2]={},bulk[2]={},steadyFrames=0;Calls steadyScale[2];
     for(int frame=0;frame<700;++frame){
         if(frame%10==9)for(int k=0;k<3;++k)scene[size_t(unit(rng)*scene.size())]=sceneModel();
         std::vector<Replay> order;const size_t view=size_t(frame/3)%scene.size();
         for(size_t visible=0;visible<130;++visible)for(const auto& mesh:scene[(view+visible*2+size_t(unit(rng)*2))%scene.size()]){Replay r;r.shared=mesh;r.indexed=mesh->indexed;r.decl=mesh->streams[1].bytes.empty()?&singleDecl:&multiDecl;order.push_back(r);}
         a->frame(order);b->frame(order);evictions[0]+=a->cache.stats().evictions;evictions[1]+=b->cache.stats().evictions;
         if(frame>=100){bulk[0]+=a->last.bulk;bulk[1]+=b->last.bulk;}
         if(frame>=100&&!a->last.bulk&&!b->last.bulk){++steadyFrames;for(int w=0;w<2;++w){const Calls& c=w?b->last:a->last;steadyScale[w].streams+=c.streams;steadyScale[w].indices+=c.indices;steadyScale[w].draws+=c.draws;}}
         const auto t=b->cache.batchStats();live=t.liveBytes;liveMax=std::max(liveMax,live);committedMax[0]=std::max(committedMax[0],a->cache.bytes());committedMax[1]=std::max(committedMax[1],b->cache.bytes());
         assert(b->cache.bytes()<=b->cache.limit()&&alive==a->cache.bytes()+b->cache.bytes());if(frame>=100){ratio+=double(b->cache.bytes())/double(std::max<size_t>(live,1));++samples;}}
     const auto t=b->cache.batchStats();
     std::printf("scale maxTriangles=%u: live<=%.1f MiB committed max legacy=%.1f batched=%.1f MiB, batched committed/live mean=%.3f batches=%zu compactions=%u compactedMiB=%.1f evictions legacy=%u batched=%u bulk draws/frame legacy=%.2f batched=%.2f uploads MiB legacy=%.1f batched=%.1f creates legacy=%u batched=%u\n",
         maxTriangles,liveMax/1048576.0,committedMax[0]/1048576.0,committedMax[1]/1048576.0,ratio/samples,t.batches,t.compactions,t.compactedBytes/1048576.0,evictions[0],evictions[1],bulk[0]/600.0,bulk[1]/600.0,a->uploads/1048576.0,b->uploads/1048576.0,a->device.calls.creates,b->device.calls.creates);
     if(steadyFrames)std::printf("scale steady frames=%u: legacy streams=%.1f indices=%.1f  batched streams=%.1f indices=%.1f  draws=%.1f\n",steadyFrames,double(steadyScale[0].streams)/steadyFrames,double(steadyScale[0].indices)/steadyFrames,double(steadyScale[1].streams)/steadyFrames,double(steadyScale[1].indices)/steadyFrames,double(steadyScale[1].draws)/steadyFrames);}
    assert(alive==0&&buffersAlive==0);
    // Memory guard (test_memory_guard.py's MODEL GPU cache trim, for batches): a halved cap evicts whole
    // batches at once and holds while refilling; a replay's held batch stays valid; bytes() is the
    // committed buffer bytes; clear() frees everything and a rebuild uploads identical bytes.
    {NorthlightReplayGPU::createBudgetMs()=1e9; /* exact counts: no time bound */
     Device d;NorthlightReplayGPU::BatchedCache cache;IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;NorthlightReplayGPU::Placement at;
     auto release=[&]{for(auto& p:vb)if(p){p->Release();p=nullptr;}if(ib){ib->Release();ib=nullptr;}};auto admit=[](size_t){return true;};
     auto sized=[](unsigned bytes,unsigned seed){auto m=std::make_shared<Mesh>();m->streams[0].stride=24;m->streams[0].bytes.resize(bytes);m->indices={0,1,2};m->indexed=true;
         for(unsigned i=0;i<bytes;++i)m->streams[0].bytes[i]=std::uint8_t(i*7+seed);return Owner(m);};
     std::vector<Owner> many;for(unsigned i=0;i<80;++i)many.push_back(sized(24*(43690-(i%5)),i)); /* whole 24-byte vertices: batched */
     auto frame=[&]{cache.beginFrame();for(auto& x:many){cache.bind(&d,x,vb,ib,admit,at);release();}cache.commitPending();};
     for(unsigned f=0;f<40;++f)frame();
     const size_t full=cache.bytes();assert(full>32u*1024u*1024u&&full<=64u*1024u*1024u&&cache.limit()==64u*1024u*1024u&&alive==full);
     cache.beginFrame();bool bound=false;for(auto& x:many)if(cache.bind(&d,x,vb,ib,admit,at)){bound=true;break;}assert(bound);
     auto* held=static_cast<VB*>(vb[0]);held->AddRef();const size_t heldBytes=held->data.size();release();
     cache.setLimit(32u*1024u*1024u);const size_t trimmed=cache.bytes();assert(trimmed<=32u*1024u*1024u&&cache.limit()==32u*1024u*1024u);
     assert(held->refs>=1&&held->data.size()==heldBytes&&held->written.size()==1); /* evicted or not, the holder's batch stays valid and unwritten (ASan: no use after free) */
     for(unsigned f=0;f<40;++f){frame();assert(cache.bytes()<=32u*1024u*1024u);}
     assert(alive>=cache.bytes()&&alive<=cache.bytes()+heldBytes);held->Release();assert(alive==cache.bytes());
     cache.clear();assert(cache.bytes()==0&&alive==0);
     cache.beginFrame();assert(!cache.bind(&d,many[5],vb,ib,admit,at));cache.beginFrame();assert(!cache.bind(&d,many[5],vb,ib,admit,at));
     assert(cache.commitPending()&&cache.bind(&d,many[5],vb,ib,admit,at));
     assert(!std::memcmp(static_cast<VB*>(vb[0])->data.data()+size_t(at.vertexBase)*24,many[5]->streams[0].bytes.data(),many[5]->streams[0].bytes.size()));release();
     cache.setLimit(size_t(1)<<40);assert(cache.limit()==64u*1024u*1024u);
     many.clear();cache.beginFrame();assert(cache.bytes()==0&&alive==0);
     // Trim cost and over-eviction at ~1.8k resident meshes (log: 1806 resident); fake buffers, DXVK Release cost not modelled.
     NorthlightReplayGPU::BatchedCache bench;std::vector<Owner> set;for(unsigned i=0;i<1806;++i)set.push_back(sized(24*(958+(i%64)),i));
     for(unsigned f=0;f<200;++f){bench.beginFrame();for(auto& x:set){bench.bind(&d,x,vb,ib,admit,at);release();}bench.commitPending();}
     const auto population=bench.population();const size_t bytes=bench.bytes(),batches=bench.batchStats().batches;
     auto t0=std::chrono::steady_clock::now();bench.setLimit(32u*1024u*1024u);auto t1=std::chrono::steady_clock::now();const size_t after=bench.bytes();
     bench.clear();auto t2=std::chrono::steady_clock::now();assert(bench.bytes()==0&&after<=32u*1024u*1024u);
     std::printf("BENCH guard trim: resident=%zu bytes=%zu batches=%zu halve->%zu bytes (under cap by %.1f%%) halveMs=%.3f clearMs=%.3f\n",population.resident,bytes,batches,after,
         100.0*(32.0*1048576-double(after))/(32.0*1048576),std::chrono::duration<double,std::milli>(t1-t0).count(),std::chrono::duration<double,std::milli>(t2-t1).count());
     set.clear();NorthlightReplayGPU::createBudgetMs()=NorthlightReplayGPU::CreateBudgetMs;assert(alive==0);
     std::printf("PASS guard trim: full=%zu B; halved cap evicts whole batches now (%zu B) and holds while refilling; held batch intact; alive==bytes(); clear frees all; rebuild identical; cap clamped\n",full,trimmed);}
    // Unit: reservation, one exact-size batch at the next frame boundary, stride-aligned placement,
    // release with the last entry, trim to zero, clear.
    {Device d;NorthlightReplayGPU::BatchedCache c;IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;NorthlightReplayGPU::Placement at;auto admit=[](size_t){return true;};
     auto release=[&]{for(auto& p:vb)if(p){p->Release();p=nullptr;}if(ib){ib->Release();ib=nullptr;}};
     std::vector<Owner> ms;for(int i=0;i<20;++i)ms.push_back(makeMesh(rng,unit(rng)*.95f));
     c.beginFrame();for(auto& m:ms)assert(!c.bind(&d,m,vb,ib,admit,at));             /* probation */
     c.beginFrame();for(auto& m:ms)assert(!c.bind(&d,m,vb,ib,admit,at));assert(c.bytes()==0&&d.calls.creates==0); /* 16 reserved, bulk this frame */
     c.beginFrame();assert(d.calls.creates==2&&c.batchStats().batches==1);
     IDirect3DVertexBuffer9* batch=nullptr;size_t end=0,live=0;
     for(size_t i=0;i<16;++i){assert(c.bind(&d,ms[i],vb,ib,admit,at));if(!batch)batch=vb[0];assert(vb[0]==batch&&!vb[1]&&(ib!=nullptr)==ms[i]->indexed);
         const size_t offset=size_t(at.vertexBase)*ms[i]->streams[0].stride;assert(offset>=end&&offset-end<ms[i]->streams[0].stride);
         assert(!std::memcmp(static_cast<VB*>(vb[0])->data.data()+offset,ms[i]->streams[0].bytes.data(),ms[i]->streams[0].bytes.size()));
         if(ib)assert(!std::memcmp(static_cast<IB*>(ib)->data.data()+4*size_t(at.indexStart),ms[i]->indices.data(),ms[i]->indices.size()*4));
         end=offset+ms[i]->streams[0].bytes.size();live+=ms[i]->byteSize();release();}
     assert(static_cast<VB*>(batch)->data.size()==end&&c.batchStats().liveBytes==live&&c.bytes()>=live&&c.bytes()-live<16*56);
     for(size_t i=16;i<20;++i)assert(!c.bind(&d,ms[i],vb,ib,admit,at)); /* the remaining four: reserved now */
     c.beginFrame();assert(c.batchStats().batches==2);
     for(auto& m:ms)m.reset();c.beginFrame();assert(c.bytes()==0&&alive==0&&c.batchStats().batches==0);
     auto m=makeMesh(rng,.5f);for(int f=0;f<3;++f){c.beginFrame();c.bind(&d,m,vb,ib,admit,at);release();}c.beginFrame();assert(c.bind(&d,m,vb,ib,admit,at));release();
     c.setLimit(0);assert(c.bytes()==0&&alive==0);c.setLimit(64u<<20);c.clear();assert(alive==0);}
    std::puts("PASS units: reservation, exact-size batch at the frame boundary, stride-aligned placement, release with the last entry, trim to zero, clear");
}
