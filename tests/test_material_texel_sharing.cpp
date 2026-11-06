#include "world_gi.h"
#include "static_shadow_scene.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
using namespace NorthlightGI;
namespace {
bool counting=false;int allocation=-1,calls=0;
}
void* operator new(size_t n){if(counting){const int site=calls++;if(site==allocation)throw std::bad_alloc();}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
static void same(Vec3 a,Vec3 b){assert(a.x==b.x&&a.y==b.y&&a.z==b.z);}
static WorldScene fixture(){
    WorldScene s;
    for(unsigned i=0;i<4;++i){WorldMaterial m;m.width=m.height=64;m.alphaCutoff=.25f+float(i)*.125f;m.addressU=1+i%3;m.addressV=1+(i+1)%3;
        m.albedo={.25f+float(i)*.125f,.5f,.75f};m.terrain=i==1;m.wmo=i==2;m.pointOnly=i==3;m.lampLocalId=31+i;m.lampSourceId=0x123456789abcdef0ull+i;
        m.rgba.resize(64*64*4);for(size_t p=0;p<m.rgba.size();p+=4){m.rgba[p]=uint8_t(p*7);m.rgba[p+1]=uint8_t(p*3+23);m.rgba[p+2]=uint8_t(p+67);m.rgba[p+3]=uint8_t(p/4%4*85);}if(i==2)m.rgba[0]^=1;s.materials.push_back(std::move(m));
        const uint32_t n=uint32_t(s.vertices.size());const float x=float(i)*4;
        s.vertices.push_back({{x,0,1},{0,0,1},-.5f,-.5f});s.vertices.push_back({{x+2,0,1},{0,0,1},2.5f,-.5f});
        s.vertices.push_back({{x+2,2,1},{0,0,1},2.5f,2.5f});s.vertices.push_back({{x,2,1},{0,0,1},-.5f,2.5f});
        s.triangles.push_back({n,n+1,n+2,i});s.triangles.push_back({n,n+2,n+3,i});
    }
    s.materials.push_back({});return s;
}
static BVH build(WorldScene s){BVH b;std::string error;assert(b.build(std::move(s),error));assert(error.empty());return b;}
static void materials(const WorldScene& a,const WorldScene& b){
    assert(a.materials.size()==b.materials.size()&&a.vertices.size()==b.vertices.size()&&a.triangles.size()==b.triangles.size());
    for(size_t i=0;i<a.materials.size();++i){const auto& x=a.materials[i];const auto& y=b.materials[i];assert(x.texels()==y.texels());same(x.albedo,y.albedo);
        assert(x.width==y.width&&x.height==y.height&&x.alphaCutoff==y.alphaCutoff&&x.addressU==y.addressU&&x.addressV==y.addressV&&x.terrain==y.terrain&&x.wmo==y.wmo&&x.pointOnly==y.pointOnly&&x.lampLocalId==y.lampLocalId&&x.lampSourceId==y.lampSourceId);
    }
    for(size_t i=0;i<a.triangles.size();++i){const auto& x=a.triangles[i];const auto& y=b.triangles[i];assert(x.v0==y.v0&&x.v1==y.v1&&x.v2==y.v2&&x.material==y.material);}
}
static void rays(const BVH& a,const BVH& b){
    unsigned hits=0,misses=0;
    for(unsigned m=0;m<4;++m)for(unsigned y=0;y<17;++y)for(unsigned x=0;x<17;++x)for(int side:{-1,1}){
        Vec3 origin{float(m)*4+(float(x)+.25f)/8,(float(y)+.25f)/8,1.f+float(side)*2};Vec3 direction{0,0,float(-side)};
        const auto p=a.trace(origin,direction),q=b.trace(origin,direction);assert(p.hit==q.hit&&p.backFace==q.backFace&&p.distance==q.distance&&p.triangle==q.triangle);
        same(p.position,q.position);same(p.normal,q.normal);same(p.geometricNormal,q.geometricNormal);same(p.albedo,q.albedo);
        assert(a.occluded(origin,direction,5)==b.occluded(origin,direction,5));if(p.hit)++hits;else ++misses;
    }
    assert(hits&&misses);
}
static void shadows(const WorldScene& raw,const WorldScene& shared){
    StaticShadow::Model fallback;fallback.materials=shared.materials;
    for(size_t m=0;m<4;++m)for(size_t p=0;p<64*64;++p)assert(fallback.alphaAt(m,p)==raw.materials[m].rgba[p*4+3]);
    for(bool own:{false,true}){StaticShadow::Model a,b;std::string error;auto x=raw,y=shared;
        assert(own?StaticShadow::packModel(std::move(x),"fixture",a,error):StaticShadow::packSharedModel(x,"fixture",a,error));
        assert(own?StaticShadow::packModel(std::move(y),"fixture",b,error):StaticShadow::packSharedModel(y,"fixture",b,error));
        assert(a.contentRevision==b.contentRevision&&a.materialKeys==b.materialKeys&&a.uploadAlpha.size()==b.uploadAlpha.size());
        assert(a.bytes==b.bytes&&b.bytes<1000000);
        for(size_t m=0;m<a.uploadAlpha.size();++m){assert(*a.uploadAlpha[m]==*b.uploadAlpha[m]);assert(b.materials[m].texels().empty()&&!b.materials[m].sharedTexels);}
    }
}
int main(){
    const auto raw=fixture();auto reference=build(raw),subject=build(raw);const size_t before=subject.retainedBytes();
    counting=true;subject.shareMaterialTexels();counting=false;const int sites=calls;assert(sites>=2);
    materials(raw,subject.scene());rays(reference,subject);
    const auto& m=subject.scene().materials;assert(m[0].sharedTexels&&m[0].sharedTexels==m[1].sharedTexels&&m[0].sharedTexels==m[3].sharedTexels);assert(!m[2].sharedTexels);
    assert(m[0].rgba.empty()&&m[1].rgba.empty()&&m[3].rgba.empty());
    assert(before-subject.retainedBytes()==2*64*64*4-64);
    NorthlightMemoryFootprint::Counter usage;subject.accountMemory(usage);assert(usage.complete()&&usage.bytes(NorthlightMemoryFootprint::Texels)==2*64*64*4);const auto bytes=usage.total();subject.accountMemory(usage);subject.scene().accountMemory(usage);assert(usage.total()==bytes);
    const auto retained=subject.retainedBytes();auto owner=m[0].sharedTexels;subject.shareMaterialTexels();assert(subject.retainedBytes()==retained&&m[0].sharedTexels==owner);shadows(raw,subject.scene());
    WorldScene held=subject.scene();subject=BVH{};owner.reset();materials(raw,held);auto rebuilt=build(std::move(held));rays(reference,rebuilt);shadows(raw,rebuilt.scene());
    for(int site=0;site<=sites;++site){auto candidate=build(raw);allocation=site;calls=0;counting=true;candidate.shareMaterialTexels();counting=false;allocation=-1;materials(raw,candidate.scene());rays(reference,candidate);shadows(raw,candidate.scene());materials(raw,reference.scene());}
    std::printf("PASS immutable material texels: byte/metadata/ray/shadow parity, exact shared accounting, independent lifetime, idempotence and %d optional allocation failures\n",sites);
}
