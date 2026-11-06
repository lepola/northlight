#include "northlight_mem.h"
extern "C" __attribute__((no_builtin("memcmp"),no_builtin("bcmp"))) int memcmp(const void* a,const void* b,size_t n){return NorthlightMem::compare(a,b,n);}
#include "replay_fixture.h"
#include "scene_native_store.h"
#include "mesh_buffer_store.h"
#include "snapshot_store.h"
#include "cpu_retirement.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <vector>

namespace NorthlightDrawSnapshot {
struct SnapshotTestAccess {
    using Key=Frame::CacheKey;
    static void hashes(){Key a;for(unsigned i=0;i<5;++i)a.version[i]=123+i;a.tracked=31;a.vb[2]=456;a.minimum=12;a.vertices=93;
        Key b=a;std::fill(std::begin(b.version),std::end(b.version),0);b.tracked=0;
        assert(Frame::hashKey<true>(a)==Frame::hashKey(b));}
    static void history(){Frame f;Key k;k.vb[0]=7;k.vertices=4;f.sampleMaintenance_=true;
        f.noteMiss(k,Frame::hashKey(k),false);assert(f.maintenance_.missUnknown==1);
        auto logical=Frame::logicalHash(k);auto& h=f.history_[logical%f.history_.size()];h={logical,Frame::hashKey(k),true,false,f.historyEpoch_};
        f.historyEpoch_=0xffff;h.epoch=0xffff;f.clearSnapshotCache();assert(f.historyEpoch_==1&&!h.valid);}
    static void decode(Frame& f,const std::vector<UINT>& input,bool wide){
        Draw draw{};draw.minimum=0;draw.vertices=1000000;Mesh mesh;UINT lo=0,hi=0;Diagnostics why{};
        std::vector<uint16_t> narrow;for(auto i:input)narrow.push_back(uint16_t(i));
        auto wideBytes=input;void* bytes=wide?static_cast<void*>(wideBytes.data()):static_cast<void*>(narrow.data());
        auto format=wide?D3DFMT_INDEX32:D3DFMT_INDEX16;
        assert(f.decode(bytes,UINT(input.size()),format,draw,mesh,lo,hi,&why));
        auto unique=input;std::sort(unique.begin(),unique.end());unique.erase(std::unique(unique.begin(),unique.end()),unique.end());
        assert(lo==unique.front()&&hi==unique.back()&&mesh.vertexCount==unique.size());
        for(size_t i=0;i<input.size();++i)assert(mesh.indices[i]==unsigned(std::lower_bound(unique.begin(),unique.end(),input[i])-unique.begin()));
        std::vector<uint8_t> source((size_t(hi-lo)+1)*8),dest(size_t(mesh.vertexCount)*8,0xa5);
        for(size_t i=0;i<source.size();++i)source[i]=uint8_t(i*17+3);
        f.copyVertices(dest.data(),source.data(),8,5,lo,mesh.vertexCount);
        for(size_t i=0;i<unique.size();++i){size_t n=i+1==unique.size()?5:8;assert(!std::memcmp(dest.data()+i*8,source.data()+size_t(unique[i]-lo)*8,n));}
        for(size_t i=(unique.size()-1)*8+5;i<dest.size();++i)assert(dest[i]==0);
        Mesh again;const auto hit=f.indexHits_;assert(f.decode(bytes,UINT(input.size()),format,draw,again,lo,hi,&why));assert(again.indices==mesh.indices&&f.indexHits_==hit+1);if(wide)wideBytes[0]=wideBytes[1];else narrow[0]=narrow[1];const auto miss=f.indexMisses_;Mesh changed;assert(f.decode(bytes,UINT(input.size()),format,draw,changed,lo,hi,&why));assert(f.indexMisses_==miss+1&&changed.indices[0]==changed.indices[1]);
    }
};}
using namespace NorthlightDrawSnapshot;
struct Clock {using rep=long long;using period=std::nano;using duration=std::chrono::nanoseconds;using time_point=std::chrono::time_point<Clock>;static constexpr bool is_steady=true;static rep tick;static time_point now(){return time_point(duration(tick++));}}; Clock::rep Clock::tick=0;
static void meter(){for(unsigned count:{0u,1u,2u,3u,512u}){SnapshotMeter<Clock> m;for(unsigned f=0;f<count;++f){m.beginFrame(true,0);for(unsigned n=0;n<113;++n)if(m.sample()){auto t=m.start();Clock::tick+=f+1;m.stop(t);}assert(m.spans()==8);m.endFrame(1,10,5,3,2);}
    unsigned visits=0;m.report([&](unsigned c,const auto& s){++visits;assert(c==2&&s.frames[1]==count&&s.spans==8ull*count&&s.predictTried==5ull*count&&s.predictHits==3ull*count&&s.predictMisses==2ull*count&&s.fastHits==10ull*count);assert(s.p25[1]==1+(count-1)/4&&s.median[1]==1+(count-1)/2&&s.p75[1]==1+3*(count-1)/4&&s.pairNs==1);});assert(visits==(count?1u:0u));}SnapshotMeter<Clock> m;for(unsigned mode=0;mode<3;++mode){m.beginFrame(true,0);for(unsigned n=0;n<113;++n)if(m.sample()){auto t=m.start();Clock::tick+=mode+2;m.stop(t);}m.endFrame(mode,220,9,6,3);}unsigned visits=0;m.report([&](unsigned c,const auto& s){++visits;assert(c==1&&s.frames[0]==1&&s.frames[1]==1&&s.frames[2]==1&&s.median[0]==2&&s.median[1]==3&&s.median[2]==4&&s.spans==24&&s.predictTried==18&&s.predictHits==12&&s.predictMisses==6&&s.fastHits==440);});assert(visits==1);}
static NorthlightMeshPageKey::Key key(unsigned i){NorthlightMeshPageKey::Key k;k.lo=i;k.hi=~uint64_t(i);k.vertexBytes=1000+i;k.indexBytes=128+i;return k;}
static void capacity(const NorthlightMeshBufferStore::Store& s){uint64_t pool=0,active=0,idle=0;for(const auto& e:s.entries()){pool+=e.capacity();if(e.use==NorthlightMeshBufferStore::Use::Active)active+=e.capacity();if(e.use==NorthlightMeshBufferStore::Use::Idle)idle+=e.capacity();}assert(s.poolBytes()==pool&&s.activeBytes()==active&&s.idleBytes()==idle);for(auto id:s.table())assert(id<s.entries().size()&&s.entries()[id].use==NorthlightMeshBufferStore::Use::Active);}
static void meshStore(){using namespace NorthlightMeshBufferStore;Store s;std::mt19937 rng(187);for(unsigned round=0;round<400;++round){std::vector<Key> keys;for(unsigned i=0,n=rng()%18;i<n;++i)keys.push_back(key(rng()%12));assert(s.beginStaging(keys));capacity(s);auto uploads=s.staging().uploadBuffer;for(unsigned i=0;i<uploads.size();++i){if(round%5==0&&i>=uploads.size()/2)break;assert(s.markWriting(uploads[i]));assert(s.setCapacity(uploads[i],true,16384));assert(s.setCapacity(uploads[i],false,16384));capacity(s);}if(round%5==0)s.cancel();else s.commit();capacity(s);if(round%4==0){const uint64_t budget=rng()%100000;std::vector<uint32_t> order;for(uint32_t i=0;i<s.entries().size();++i)if(s.entries()[i].use==Use::Idle&&!s.entries()[i].empty())order.push_back(i);std::sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b){const auto& x=s.entries()[a];const auto& y=s.entries()[b];if(x.keyValid!=y.keyValid)return !x.keyValid;if(!x.keyValid)return a>b;return x.stamp!=y.stamp?x.stamp<y.stamp:a<b;});uint64_t expected=0;std::vector<uint32_t> evicted;for(auto i:order){auto bytes=s.entries()[i].capacity();if(expected&&expected+bytes>budget)break;expected+=bytes;evicted.push_back(i);}std::vector<uint32_t> actual;assert(s.trimIdle(budget,[&](uint32_t i){actual.push_back(i);})==expected&&actual==evicted);capacity(s);}if(round%9==0){Store copy=s;capacity(copy);Store moved=std::move(copy);capacity(moved);capacity(copy);}if(round%71==0){s.reset();capacity(s);}}}
static void snapshots(){using S=NorthlightStream::GameSnapshot;alignas(S) unsigned char bytes[sizeof(S)];std::memset(bytes,0xa5,sizeof bytes);auto* s=new(bytes) S;assert(!s->usable()&&!s->keys()&&!s->copies()&&!s->bytes()&&!s->overflowed());unsigned a=11,b=22,out=99;assert(s->record(1,&a,4,true));assert(s->record(1,&b,4,false));assert(s->record(1,&b,4,true));assert(s->record(2,&a,4,true));s->rewind();assert(s->serve(1,&out,4)==S::Serve::Ok&&out==11);out=99;assert(s->serve(1,&out,4)==S::Serve::Failed&&out==99);assert(s->serve(1,&out,4)==S::Serve::Ok&&out==22);assert(s->serve(1,&out,4)==S::Serve::Ok&&out==22);assert(s->serve(3,&out,4)==S::Serve::Miss);s->rewind();assert(s->serveRecorded(1,&out,4)==S::Serve::Ok&&out==11);out=77;assert(s->serveRecorded(1,&out,4)==S::Serve::Failed&&out==77);assert(s->serveRecorded(1,&out,4)==S::Serve::Ok&&out==22);assert(s->serveRecorded(1,&out,4)==S::Serve::Miss&&s->serveRecorded(3,&out,4)==S::Serve::Miss);assert(s->keys()==2&&s->copies()==4&&s->bytes()==12);s->clear();assert(!s->usable()&&!s->keys()&&!s->copies()&&!s->bytes());s->~S();}
static void retirement(){using namespace NorthlightStreaming;CpuRetirement q(false);CpuRetirementBatch empty(&q);assert(!empty.finish());auto p=std::make_shared<int>(3);std::weak_ptr<int> weak=p;CpuRetirementBatch b(&q);assert(b.retire(p,8)&&!p&&b.count()==1&&b.bytes()==8);assert(b.finish()&&b.queuedBytes()==8&&q.waitIdle(1000)&&weak.expired());assert(!b.finish());auto huge=std::make_shared<int>(4);std::weak_ptr<int> rejected=huge;CpuRetirementBatch refused(&q);assert(refused.retire(huge,129u<<20)&&!huge);assert(!refused.finish()&&refused.queuedBytes()==0&&rejected.expired());}
static void owners(){using S=NorthlightGpuScene::NativeStore;using C=NorthlightGpuScene::Consumer;S s;Device d;IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;NorthlightReplayGPU::Placement at;auto m=std::const_pointer_cast<Mesh>(mesh());m->sourceIdentity=NorthlightSourceIdentity::make({8});s.setFrame(1);auto bind=[&](const std::shared_ptr<const Mesh>& x){return s.bind(C::Material,&d,x,vb,ib,[](size_t){return true;},at);};assert(!bind(m));for(unsigned i=0;i<10;++i)assert(!bind(m));assert(s.stats(C::Material).identityHits==10);std::shared_ptr<const Mesh> other(m.get(),[](const Mesh*){});assert(!bind(other)&&!s.stats(C::Material).reconstructedHits);auto rebuilt=std::make_shared<Mesh>(*m);assert(!bind(rebuilt)&&s.stats(C::Material).reconstructedHits==1&&s.population(C::Material).entries==1);s.clearAll();assert(!s.bytes()&&!s.chargedBytes());auto backing=std::make_unique<Mesh>(*m);backing->sourceIdentity.reset();std::shared_ptr<const Mesh> a(backing.get(),[](const Mesh*){}),b(backing.get(),[](const Mesh*){});assert(!bind(a)&&!bind(b)&&s.population(C::Material).entries==1);a.reset();assert(!bind(b)&&s.population(C::Material).entries==2);s.setFrame(2);assert(s.population(C::Material).entries==1&&s.stats(C::Material).expiredEntries>=1);s.clearAll();assert(!s.bytes()&&!s.chargedBytes());}
int main(){SnapshotTestAccess::hashes();SnapshotTestAccess::history();Frame f;for(bool wide:{false,true}){SnapshotTestAccess::decode(f,{7,8,9,7,9,8},wide);SnapshotTestAccess::decode(f,{2,9,2,6,9,6},wide);}SnapshotTestAccess::decode(f,{2,400000,2,500000,400000,500000},true);std::vector<UINT> large;for(unsigned i=0;i<300;++i)large.push_back((i*37)%251);SnapshotTestAccess::decode(f,large,true);SnapshotTestAccess::decode(f,large,false);meter();meshStore();snapshots();retirement();owners();puts("PIPELINE_WAVE_CPU_PASS");}
