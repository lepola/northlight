// Native fake-D3D test. No game, Wine, GPU device, or graphics process starts.
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <cstdio>
#include <new>
#include <random>
using HRESULT=int32_t;using UINT=uint32_t;using DWORD=uint32_t;
constexpr HRESULT S_OK=0,S_FALSE=1,E_FAIL=-1,E_OUTOFMEMORY=-2;
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
constexpr DWORD D3DUSAGE_DYNAMIC=1,D3DUSAGE_WRITEONLY=2,D3DLOCK_DISCARD=1,D3DLOCK_NOOVERWRITE=2;
constexpr unsigned D3DPOOL_DEFAULT=0,D3DQUERYTYPE_EVENT=0,D3DISSUE_END=1;
struct Driver {
    uint64_t submitted=0,completed=0;unsigned locks=0,discards=0,queries=0,waits=0;
    size_t uploaded=0;std::vector<DWORD> flagLog;bool querySupport=true,failIssue=false,failLock=false,failUnlock=false;
};
struct IDirect3DQuery9 {
    Driver* driver;uint64_t serial=0;
    explicit IDirect3DQuery9(Driver* d):driver(d){}
    HRESULT Issue(unsigned flags){assert(flags==D3DISSUE_END);serial=driver->submitted;return driver->failIssue?E_FAIL:S_OK;}
    HRESULT GetData(void* out,UINT bytes,DWORD flags){assert(!out&&!bytes&&!flags);return driver->completed>=serial?S_OK:S_FALSE;}
    void Release(){delete this;}
};
struct IDirect3DVertexBuffer9 {
    Driver* driver;std::vector<unsigned char> data;
    struct Read {UINT first,bytes;uint64_t serial;};std::vector<Read> readers;
    IDirect3DVertexBuffer9(Driver* d,UINT bytes):driver(d),data(bytes,0xEE){}
    HRESULT Lock(UINT first,UINT bytes,void** out,DWORD flags){
        assert(size_t(first)+bytes<=data.size());assert(flags==D3DLOCK_DISCARD||flags==D3DLOCK_NOOVERWRITE);
        driver->flagLog.push_back(flags);
        if(driver->failLock)return E_FAIL;
        if(flags==D3DLOCK_DISCARD){std::fill(data.begin(),data.end(),0xEE);readers.clear();++driver->discards;}
        else for(auto r:readers)assert(r.serial<=driver->completed||first+bytes<=r.first||r.first+r.bytes<=first);
        *out=data.data()+first;++driver->locks;driver->uploaded+=bytes;return S_OK;
    }
    HRESULT Unlock(){return driver->failUnlock?E_FAIL:S_OK;}
    void Release(){delete this;}
    void read(UINT first,UINT bytes){readers.push_back({first,bytes,++driver->submitted});}
};
struct IDirect3DDevice9 {
    Driver driver;
    HRESULT CreateVertexBuffer(UINT bytes,DWORD usage,UINT fvf,unsigned pool,IDirect3DVertexBuffer9** out,void*){
        assert(usage==(D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY)&&!fvf&&pool==D3DPOOL_DEFAULT);*out=new IDirect3DVertexBuffer9(&driver,bytes);return S_OK;
    }
    HRESULT CreateQuery(unsigned kind,IDirect3DQuery9** out){assert(kind==D3DQUERYTYPE_EVENT);if(!driver.querySupport)return E_FAIL;*out=new IDirect3DQuery9(&driver);++driver.queries;return S_OK;}
};
#include "live_terrain_gpu.h"
struct Position {float x,y,z;};
struct Vertex {Position position;float normal[3]={};float uv[2]={};};
struct Snapshot {std::vector<Position> positions;std::vector<uint32_t> indices;};
using Owner=std::shared_ptr<const Snapshot>;using Cache=NorthlightLiveTerrainGPU::Cache<Snapshot,Vertex>;
Owner triangle(float value){auto s=std::make_shared<Snapshot>();s->positions={{value,1,2},{value+1,2,3},{value+2,3,4}};s->indices={0,1,2};return s;}
Vertex convert(Position p){Vertex v;v.position=p;return v;}
void allIndices(const Snapshot& s,uint32_t offset,std::vector<uint32_t>& out){for(auto i:s.indices)out.push_back(i+offset);}
bool admit(size_t){return true;}
void verify(Cache& cache,const std::vector<Owner>& active,const std::vector<uint32_t>& indices){
    size_t next=0;for(const auto& s:active)for(auto i:s->indices){assert(next<indices.size());const uint32_t actual=indices[next++];assert(actual<cache.vertexCapacity());
        Vertex v;std::memcpy(&v,cache.vertices()->data.data()+size_t(actual)*sizeof(Vertex),sizeof v);const auto p=s->positions[i];assert(!std::memcmp(&v.position,&p,sizeof p));
        for(float n:v.normal)assert(n==0);for(float uv:v.uv)assert(uv==0);
    }assert(next==indices.size());
}
void draw(Cache& cache,const std::vector<uint32_t>& indices){for(auto i:indices)cache.vertices()->read(i*sizeof(Vertex),sizeof(Vertex));}
void allocator(){
    NorthlightLiveTerrainGPU::FreeRanges free;NorthlightLiveTerrainGPU::Range a,b,c,d;
    free.reset(12);assert(free.take(3,a)&&a.first==0);assert(free.take(3,b)&&b.first==3);assert(free.take(6,c)&&c.first==6);assert(!free.take(1,d));
    free.release(a);free.release(c);free.release(b);assert(free.take(12,d)&&d.first==0);free.reset(0);assert(!free.take(1,d));
}
void partialAndOrdering(){
    IDirect3DDevice9 d;Cache cache(30*sizeof(Vertex));Owner a=triangle(10),b=triangle(20),c=triangle(30);
    std::vector<Owner> active={a,b};std::vector<uint32_t> point,directional;
    cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.uploadedBytes==6*sizeof(Vertex));assert(d.driver.locks==1&&d.driver.discards==0); /* 0.3.192: a fresh arena's first lock is NOOVERWRITE */
    assert(cache.indices(active,1,allIndices,point,directional));assert(point==directional);verify(cache,active,point);draw(cache,point);
    cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(!cache.uploadedBytes&&cache.reusedVertices==6);assert(d.driver.locks==1);
    active={c,b};cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.uploadedBytes==3*sizeof(Vertex)&&cache.reusedVertices==3);
    assert(cache.indices(active,1,allIndices,point,directional));verify(cache,active,point);assert(point[0]==6&&point[3]==3);draw(cache,point);
    active={b,c};cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(!cache.uploadedBytes);
    assert(cache.indices(active,1,allIndices,point,directional));verify(cache,active,point);assert(point[0]==3&&point[3]==6);
    // Generation/filter changes alter indices only, including point originals.
    auto filter=[](const Snapshot& s,uint32_t offset,std::vector<uint32_t>& out){if(s.positions[0].x==30)allIndices(s,offset,out);};
    assert(cache.indices(active,2,filter,point,directional));assert(point.size()==6&&directional.size()==3);verify(cache,{c},directional);assert(d.driver.locks==2);
    // LOD/topology owner replacement uploads just the new immutable capture.
    auto lod=std::make_shared<Snapshot>();lod->positions=b->positions;lod->positions[1].z+=2;lod->indices={2,1,0};active={lod,c};
    cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.uploadedBytes==3*sizeof(Vertex));assert(cache.indices(active,3,allIndices,point,directional));verify(cache,active,point);
    cache.clear();assert(!cache.vertices()&&!cache.bytes());cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.indices(active,3,allIndices,point,directional));verify(cache,active,point);
}
void retirement(bool queries,bool issue=true){
    IDirect3DDevice9 d;d.driver.querySupport=queries;d.driver.failIssue=!issue;Cache cache(9*sizeof(Vertex));
    Owner a=triangle(1),b=triangle(2),c=triangle(3);std::vector<Owner> active={a,b};std::vector<uint32_t> p,q;
    cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.indices(active,1,allIndices,p,q));draw(cache,p);
    // Expired captures enter retirement. Their NOOVERWRITE slots are unavailable
    // while the fake GPU deliberately remains busy.
    active={b,c};a.reset();cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.indices(active,1,allIndices,p,q));verify(cache,active,p);assert(p[3]==6);draw(cache,p);
    d.driver.completed=d.driver.submitted;Owner e=triangle(4);active={b,e};c.reset();
    cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.indices(active,1,allIndices,p,q));verify(cache,active,p);
    if(queries&&issue){assert(!cache.rollovers);assert(p[3]==0);assert(cache.uploadedBytes==3*sizeof(Vertex));}
    else{assert(cache.rollovers==1&&d.driver.discards==1);assert(cache.uploadedBytes==6*sizeof(Vertex));}
}
void busyRolloverAndFailure(){
    IDirect3DDevice9 d;Cache cache(9*sizeof(Vertex));Owner a=triangle(1),b=triangle(2),c=triangle(3),e=triangle(4);std::vector<Owner> active={a,b};std::vector<uint32_t> p,q;
    cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.indices(active,1,allIndices,p,q));draw(cache,p);
    active={b,c};a.reset();cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.indices(active,1,allIndices,p,q));draw(cache,p);
    active={b,e};c.reset();cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.rollovers==1&&d.driver.discards==1);assert(cache.indices(active,1,allIndices,p,q));verify(cache,active,p);
    Owner f=triangle(5);active={b,f};d.driver.failLock=true;cache.beginFrame();assert(cache.update(&d,active,admit,convert)==E_FAIL);d.driver.failLock=false;
    cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.uploadedBytes==6*sizeof(Vertex));assert(cache.indices(active,1,allIndices,p,q));verify(cache,active,p);
    Owner g=triangle(6);active={b,g};d.driver.failUnlock=true;cache.beginFrame();assert(cache.update(&d,active,admit,convert)==E_FAIL);d.driver.failUnlock=false;
    cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.indices(active,1,allIndices,p,q));verify(cache,active,p);
}
void admissionAndBounds(){
    IDirect3DDevice9 d;Cache cache(12*sizeof(Vertex));auto a=triangle(1);std::vector<Owner> active={a};
    cache.beginFrame();assert(cache.update(&d,active,[](size_t){return false;},convert)==S_FALSE);assert(!cache.vertices());
    assert(cache.update(&d,active,[](size_t n){return n<=6*sizeof(Vertex);},convert)==S_OK);assert(cache.vertexCapacity()==6);
    std::weak_ptr<const Snapshot> weak=a;active.clear();a.reset();assert(weak.expired());cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);
    Cache tiny(2*sizeof(Vertex));active={triangle(3)};tiny.beginFrame();assert(tiny.update(&d,active,admit,convert)==E_OUTOFMEMORY);assert(!tiny.vertices());
}
void longWalk(){
    IDirect3DDevice9 d;Cache cache(128*3*sizeof(Vertex));std::vector<Owner> owners;for(unsigned i=0;i<600;++i)owners.push_back(triangle(float(i*3)));
    std::vector<uint32_t> p,q;uint64_t previous=0;
    for(unsigned frame=0;frame<500;++frame){
        // Keep old CPU owners alive: reclamation must still work under arena
        // pressure, not depend on the terrain-capture cache evicting them.
        std::vector<Owner> active(owners.begin()+frame,owners.begin()+frame+64);
        d.driver.completed=previous;previous=d.driver.submitted;
        cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);
        assert(cache.uploadedBytes==(frame?3:64*3)*sizeof(Vertex));
        assert(cache.indices(active,1,allIndices,p,q));verify(cache,active,p);draw(cache,p);
        assert(!cache.rollovers&&cache.bytes()==128*3*sizeof(Vertex));
    }
    assert(d.driver.discards==0&&d.driver.queries>0);
}
// 0.3.176 (U1a/U1b): prepare()+write() against the 0.3.175 indices() concatenation, on twin caches
// driven identically: new owners, retirements, order changes, repeated owners, generation changes,
// arena rollovers, Lock/Unlock failures, owners without an entry, unrecorded lists and a throwing
// directional build. Return values, counts, bytes, arena contents and counters must all match.
struct Twin {IDirect3DDevice9 d;Cache cache;explicit Twin(size_t bytes):cache(bytes){}};
Owner randomSnapshot(std::mt19937& rng){
    auto s=std::make_shared<Snapshot>();const unsigned vertices=3+rng()%6,triangles=1+rng()%4;
    for(unsigned i=0;i<vertices;++i)s->positions.push_back({float(rng()%1000),float(rng()%1000),float(rng()%1000)});
    for(unsigned i=0;i<3*triangles;++i)s->indices.push_back(uint32_t(rng()%vertices));
    return s;
}
void prepareMatchesIndices(){
    size_t frames=0,prepared=0,missing=0,unrecorded=0,thrown=0,failedUpdates=0;unsigned rollovers=0;
    for(unsigned run=0;run<24;++run){
        std::mt19937 rng(176+run);const size_t arena=(run%3==0?40:120)*sizeof(Vertex);
        Twin a(arena),b(arena);std::vector<Owner> pool;for(unsigned i=0;i<32;++i)pool.push_back(randomSnapshot(rng));
        uint64_t generation=1;unsigned buildCalls=0,throwAt=0;
        auto build=[&](const Snapshot& s,uint32_t offset,std::vector<uint32_t>& out){
            if(throwAt&&++buildCalls==throwAt)throw std::bad_alloc();
            for(size_t i=0;i+2<s.indices.size();i+=3)if((uint32_t(s.positions[s.indices[i]].x)+generation+i/3)%3)
                out.insert(out.end(),{s.indices[i]+offset,s.indices[i+1]+offset,s.indices[i+2]+offset});
        };
        for(unsigned frame=0;frame<150;++frame,++frames){
            for(auto& o:pool)if(rng()%12==0)o=randomSnapshot(rng); /* retire and replace */
            std::vector<Owner> active;const unsigned n=rng()%14;
            for(unsigned i=0;i<n;++i)active.push_back(pool[rng()%pool.size()]); /* random order, repeats */
            if(rng()%8==0)++generation;
            const bool failLock=rng()%29==0,failUnlock=rng()%31==0;
            for(Twin* t:{&a,&b}){t->d.driver.failLock=failLock;t->d.driver.failUnlock=failUnlock;}
            a.cache.beginFrame();b.cache.beginFrame();
            const HRESULT ha=a.cache.update(&a.d,active,admit,convert),hb=b.cache.update(&b.d,active,admit,convert);
            for(Twin* t:{&a,&b}){t->d.driver.failLock=t->d.driver.failUnlock=false;}
            assert(ha==hb&&a.cache.uploadedBytes==b.cache.uploadedBytes&&a.cache.reusedVertices==b.cache.reusedVertices&&a.cache.rollovers==b.cache.rollovers);
            assert(a.cache.newOwners==b.cache.newOwners&&a.cache.collectVisited==b.cache.collectVisited);
            assert(!a.cache.vertices()==!b.cache.vertices());if(a.cache.vertices())assert(a.cache.vertices()->data==b.cache.vertices()->data);
            if(ha!=S_OK){++failedUpdates;continue;}
            // The renderer passes the list it updated; also a copy (unrecorded: looked up) and a stranger.
            std::vector<Owner> other=active;const unsigned mode=rng()%10;
            if(mode==0)other.insert(other.begin()+(other.empty()?0:rng()%other.size()),randomSnapshot(rng));
            const auto& list=mode<3?other:active;missing+=mode==0;unrecorded+=mode>0&&mode<3;
            throwAt=rng()%23==0?1+rng()%3:0;buildCalls=0;
            size_t points=7,directionals=7;const bool okA=a.cache.prepare(list,generation,build,points,directionals);
            const unsigned callsA=buildCalls;buildCalls=0;
            std::vector<uint32_t> p={1,2},q={3};const bool okB=b.cache.indices(list,generation,build,p,q);
            assert(callsA==buildCalls);thrown+=throwAt&&callsA>=throwAt;throwAt=0;
            assert(okA==okB&&points==p.size()&&directionals==q.size());
            assert(a.cache.directionalBuilds==b.cache.directionalBuilds&&a.cache.directionalTriangles==b.cache.directionalTriangles);
            if(!okA)continue;
            ++prepared;
            std::vector<uint32_t> written(points+directionals+1,0xDEADBEEFu);a.cache.write(written.data());
            assert(written.back()==0xDEADBEEFu);written.pop_back();
            std::vector<uint32_t> joined=p;joined.insert(joined.end(),q.begin(),q.end());assert(written==joined);
            verify(a.cache,list,p);draw(a.cache,p);draw(b.cache,p);
            if(rng()%3){a.d.driver.completed=a.d.driver.submitted;b.d.driver.completed=b.d.driver.submitted;}
        }
        rollovers+=a.cache.rollovers;
    }
    assert(prepared>1000&&missing>50&&unrecorded>100&&thrown>5&&failedUpdates>20&&rollovers>0);
    std::printf("prepare/write == indices(): frames=%zu prepared=%zu missingOwner=%zu unrecorded=%zu thrownBuilds=%zu failedUpdates=%zu rollovers=%u\n",
        frames,prepared,missing,unrecorded,thrown,failedUpdates,rollovers);
}
// 0.3.192 (DXVK3): a freshly created arena has nothing in flight, so its first Lock is NOOVERWRITE (no full-buffer DISCARD charge);
// a rollover and every failure reset keep DISCARD (the contents are unknown), and clear() makes the next creation fresh again.
std::uint64_t arenaCharge(){return NorthlightLockMeter::state().frameSite[NorthlightLockMeter::Arena].exchange(0);}
void freshArenaFlags(){
    arenaCharge();
    const size_t arena=9*sizeof(Vertex);
    {IDirect3DDevice9 d;Cache cache(arena);Owner a=triangle(1),b=triangle(2),c=triangle(3),e=triangle(4);std::vector<Owner> active={a,b};std::vector<uint32_t> p,q;
     // First upload after creation: NOOVERWRITE, nothing charged.
     cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(d.driver.flagLog.size()==1&&d.driver.flagLog[0]==D3DLOCK_NOOVERWRITE&&!arenaCharge());
     assert(cache.indices(active,1,allIndices,p,q));draw(cache,p);
     // Later appends stay NOOVERWRITE.
     active={b,c};a.reset();cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(d.driver.flagLog.back()==D3DLOCK_NOOVERWRITE&&!arenaCharge());assert(cache.indices(active,1,allIndices,p,q));draw(cache,p);
     // Rollover while the fake GPU is busy: DISCARD, charged at the whole arena.
     active={b,e};c.reset();cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(cache.rollovers==1&&d.driver.flagLog.back()==D3DLOCK_DISCARD&&d.driver.discards==1);
     assert(arenaCharge()==arena);assert(cache.indices(active,1,allIndices,p,q));draw(cache,p);
     // A failed Lock resets the contents: the retry is a DISCARD again.
     Owner f=triangle(5);active={b,f};d.driver.failLock=true;cache.beginFrame();assert(cache.update(&d,active,admit,convert)==E_FAIL);d.driver.failLock=false;arenaCharge();
     cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(d.driver.flagLog.back()==D3DLOCK_DISCARD&&arenaCharge()==arena);
     // A failed Unlock likewise.
     Owner g=triangle(6);active={b,g};d.driver.failUnlock=true;cache.beginFrame();assert(cache.update(&d,active,admit,convert)==E_FAIL);d.driver.failUnlock=false;arenaCharge();
     cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(d.driver.flagLog.back()==D3DLOCK_DISCARD&&arenaCharge()==arena);
     // clear() resets fresh_: the next creation's first Lock is NOOVERWRITE again, even though the previous arena ended in DISCARD state.
     cache.clear();assert(!cache.vertices());const size_t before=d.driver.flagLog.size();
     cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(d.driver.flagLog.size()==before+1&&d.driver.flagLog.back()==D3DLOCK_NOOVERWRITE&&!arenaCharge());
    }
    // A failure on the very first Lock of a fresh arena also forgets fresh_: the retry is a DISCARD.
    {IDirect3DDevice9 d;Cache cache(arena);Owner a=triangle(1);std::vector<Owner> active={a};
     d.driver.failLock=true;cache.beginFrame();assert(cache.update(&d,active,admit,convert)==E_FAIL);d.driver.failLock=false;assert(d.driver.flagLog.size()==1&&d.driver.flagLog[0]==D3DLOCK_NOOVERWRITE);arenaCharge();
     cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);assert(d.driver.flagLog.back()==D3DLOCK_DISCARD&&arenaCharge()==arena);
    }
    // A pressure-refused creation (S_FALSE) never locks.
    {IDirect3DDevice9 d;Cache cache(arena);Owner a=triangle(1);std::vector<Owner> active={a};cache.beginFrame();assert(cache.update(&d,active,[](size_t){return false;},convert)==S_FALSE);assert(d.driver.flagLog.empty()&&!arenaCharge());}
}
int main(){allocator();freshArenaFlags();partialAndOrdering();retirement(true);retirement(false);retirement(true,false);busyRolloverAndFailure();admissionAndBounds();longWalk();prepareMatchesIndices();std::puts("live terrain GPU: geometry/order/LOD, incremental writes, fence-safe reuse, 500-frame bounded walk, busy/unsupported rollover, pressure/failure/reset, prepare/write == indices() passed");}
