#include "terrain-compact-fixture.h"
#include <atomic>
#include <thread>
#include <type_traits>

static uint64_t identities[2]={11,12},revisions[2]={21,22};
static uint64_t identity(void*,bool index){return identities[index];}
static uint64_t version(void*,bool index){return revisions[index];}
static unsigned width(const NorthlightTerrainIndices::Indices& indices){return indices.visit([](const auto* p,size_t n){assert(p||!n);return unsigned(sizeof(*p));});}
static void container(){
    NorthlightTerrainIndices::Indices a;a.reserve(4);a.push_back(0);a.push_back(65535);assert(width(a)==2&&a[1]==65535);
    auto b=a;assert(b==a&&b.storageIdentity()!=a.storageIdentity());b.push_back(65536);assert(width(b)==4&&b[0]==0&&b[1]==65535&&b[2]==65536&&width(a)==2);
    auto c=a;c.set(0,UINT32_MAX);assert(width(c)==4&&c[0]==UINT32_MAX&&a[0]==0);
    c.reserve(300000);assert(c[1]==65535);a.reserve(10,65536);assert(width(a)==4&&a[1]==65535);
    uint64_t sum=0;for(auto i:b)sum+=i;assert(sum==131071);auto moved=std::move(b);assert(moved.size()==3&&moved[2]==65536);
}
static void wideCaptures(){
    Device d;d.vb.bytes.resize(16+65538*24);d.ib.bytes.resize(65538*4);d.ib.desc.Format=D3DFMT_INDEX32;
    const Position triangle[]={{-10200,-1180,3},{-10198,-1180,4},{-10200,-1178,5}};
    for(unsigned i=0;i<65538;++i)d.put(i,triangle[i%3]);
    Limits limits;limits.maxTrianglesPerDraw=22000;FrameCache cache(limits);cache.setIdentityProvider(identity);cache.setVersionProvider(version);
    for(unsigned unique:{65535u,65536u,65537u,65538u}){
        std::vector<uint32_t> source(65538);for(unsigned i=0;i<source.size();++i)source[i]=i<unique?i:(i-unique)%3;
        std::memcpy(d.ib.bytes.data(),source.data(),source.size()*4);++revisions[1];cache.clearFrame();
        std::shared_ptr<const MeshSnapshot> output,again;Diagnostics why;
        assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,unique,0,21846,d.view,output,&why));assert(output->positions.size()==unique&&output->indices.size()==source.size());
        assert(width(output->indices)==(unique<=65536?2u:4u));for(size_t i=0;i<source.size();++i)assert(output->indices[i]==source[i]);
        cache.clearFrame();assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,unique,0,21846,d.view,again,&why)&&why.trackedCacheHit&&output==again&&!cache.bytesRead());
        MeshSnapshot up;cache.clearFrame();assert(cache.readMeshUP(&d,D3DPT_TRIANGLELIST,0,unique,21846,source.data(),D3DFMT_INDEX32,d.vb.bytes.data()+16,24,d.view,up));assert(up.indices==output->indices&&up.positions.size()==output->positions.size());
        // Independent mutable value and typed iteration never change an old snapshot.
        auto value=*output;value.indices.set(0,65537);assert(output->indices[0]==0&&value.indices[0]==65537);const float previousZ=output->positions[0].z;
        std::atomic<bool> stop=false;std::thread reader([&]{while(!stop.load()){assert(output->positions[0].z==previousZ&&output->indices[0]==0&&output->indices[unique-1]==unique-1);}});
        d.put(0,{-10200,-1180,previousZ+1});++revisions[0];cache.clearFrame();assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,unique,0,21846,d.view,again,&why));assert(!why.trackedCacheHit&&again!=output&&again->positions[0].z==previousZ+1);
        cache.clearPersistent();++identities[0];revisions[0]=0;cache.clearFrame();assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,unique,0,21846,d.view,again,&why)&&!why.trackedCacheHit);stop=true;reader.join();
        d.put(0,triangle[0]);++revisions[0];
    }
    // Large native indices still compact after the exact unique-vertex remap.
    const uint32_t sparse[]={65535,65536,65537};std::memcpy(d.ib.bytes.data(),sparse,sizeof sparse);++revisions[1];cache.clearFrame();std::shared_ptr<const MeshSnapshot> output;
    assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,0,65535,3,0,1,d.view,output)&&output->positions.size()==3&&width(output->indices)==2&&output->indices==std::vector<uint32_t>({0,1,2}));
    assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
}
int main(){container();wideCaptures();std::puts("PASS terrain16/32 boundaries65535/65536/65537, custom wide limits, typed views, GPU index values, source32 sparse remap, mutations, lifetime/unknown revisions, reset and concurrent immutable readers");}
