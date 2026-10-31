#include "material_disk_cache.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace NorthlightMaterialDisk;
static Store::State wait(const Store::Handle& h){
    assert(h);const auto start=std::chrono::steady_clock::now();
    while(h->state.load(std::memory_order_acquire)==Store::State::Pending){assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(5));std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    return h->state.load(std::memory_order_acquire);
}
static Store::Handle request(Store& store,Bytes identity,Bytes source,unsigned w=64,unsigned h=32){
    const auto start=std::chrono::steady_clock::now();Store::Handle result;
    while(!(result=store.request(identity,source,w,h))){assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(5));std::this_thread::sleep_for(std::chrono::milliseconds(1));}return result;
}
static void save(Store& store,Key key,Bytes pixels){
    const auto start=std::chrono::steady_clock::now();while(!store.save(key,64,32,pixels)){assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(5));std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
static Bytes source(){Bytes b(4096);for(size_t i=0;i<b.size();++i)b[i]=std::uint8_t(i*37+i/11);return b;}
static Bytes pixels(){Bytes b(64*32*4);for(size_t i=0;i<b.size();++i)b[i]=std::uint8_t(i*19+i/23);return b;}
struct Gate {std::atomic<bool> entered{false},release{false};};
static void gated(void* p){auto& g=*static_cast<Gate*>(p);g.entered.store(true);while(!g.release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));}
int main(int argc,char** argv){
    assert(argc==3);std::filesystem::path root=std::filesystem::u8path(argv[1]);const std::string mode=argv[2];
    const Bytes identity={1,0,0,0,7,3,75,50};
    if(mode=="produce"){
        Store cache(root);auto read=request(cache,identity,source(),64,32);assert(wait(read)==Store::State::Miss);save(cache,read->key,pixels());
        std::puts("MATERIAL_DISK_PRODUCER_PASS coldMiss=1 queuedWrite=1");return 0; // Destruction drains and atomically commits the write.
    }
    if(mode=="consume"){
        Store cache(root);auto read=request(cache,identity,source(),64,32);assert(wait(read)==Store::State::Hit&&read->pixels==pixels());
        assert(cache.stats.hits==1&&cache.stats.writes==0);std::puts("MATERIAL_DISK_RESTART_PASS exactBytes=8192 generatorWork=0");return 0;
    }
    assert(mode=="checks");
    {
        Store cache(root);auto read=request(cache,identity,source(),64,32);assert(wait(read)==Store::State::Hit);const auto key=read->key;
        auto changed=source();changed[37]^=1;assert(wait(request(cache,identity,std::move(changed),64,32))==Store::State::Miss);
        auto settings=identity;settings.back()^=1;assert(wait(request(cache,std::move(settings),source(),64,32))==Store::State::Miss);
        assert(wait(request(cache,identity,source(),32,64))==Store::State::Miss); // Dimensions are independently validated, even for a forged key.
        {std::fstream f(root/filename(key),std::ios::binary|std::ios::in|std::ios::out);f.seekp(60);f.put('\x17');}
        assert(wait(request(cache,identity,source(),64,32))==Store::State::Miss&&cache.stats.invalid>=2);
        save(cache,key,pixels());
        assert(wait(request(cache,identity,source(),64,32))==Store::State::Hit);
        {std::fstream f(root/filename(key),std::ios::binary|std::ios::in|std::ios::out);f.seekp(101);f.put('\x17');}
        assert(wait(request(cache,identity,source(),64,32))==Store::State::Miss);
        {std::ofstream f(root/filename(key),std::ios::binary|std::ios::trunc);f<<"partial";}
        assert(wait(request(cache,identity,source(),64,32))==Store::State::Miss);
        save(cache,key,pixels());
    }
    {
        Store cache(root);assert(wait(request(cache,identity,source(),64,32))==Store::State::Hit);
        // A slow file operation cannot make the owner help or wait for it.
        Gate gate;Store slow(root,gated,&gate);std::vector<Store::Handle> requests;
        requests.push_back(request(slow,identity,source(),64,32));
        while(!gate.entered.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        for(unsigned i=1;i<Store::MaxJobs;++i){requests.push_back(slow.request(identity,source(),64,32));assert(requests.back());}
        assert(!slow.request(identity,source(),64,32));assert(slow.outstandingBytes()<=Store::MaxBytes);
        auto retry=pixels();assert(!slow.save(requests.front()->key,64,32,retry)&&retry==pixels());
        for(const auto& r:requests)assert(r->state.load()==Store::State::Pending);
        gate.release.store(true);for(const auto& r:requests)assert(wait(r)==Store::State::Hit);
        assert(slow.stats.refused>=1);
    }
    {
        const auto folder=root/"temporary";std::filesystem::create_directories(folder);
        const auto name=filename(hash(identity.data(),identity.size()));
        const auto stale=folder/(name+".tmp-1"),fresh=folder/(name+".tmp-2"),foreign=folder/"unrelated.tmp-1";
        for(const auto& file:{stale,fresh,foreign}){std::ofstream f(file);f<<"partial";}
        std::filesystem::last_write_time(stale,std::filesystem::file_time_type::clock::now()-std::chrono::hours(48));
        Store cache(folder);assert(wait(request(cache,identity,source()))==Store::State::Miss);
        assert(!std::filesystem::exists(stale)&&std::filesystem::exists(fresh)&&std::filesystem::exists(foreign));
    }
    {
        const auto file=root/"not-a-directory";{std::ofstream f(file);f<<"x";}
        Store unavailable(file);auto r=unavailable.request(identity,source(),64,32);
        for(unsigned i=0;i<1000&&unavailable.available();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        assert(!unavailable.available()&&!unavailable.request(identity,source(),64,32));
    }
    {
        const auto folder=root/"quota";Store quota(folder,nullptr,nullptr,16384);
        for(unsigned i=0;i<64;++i){auto id=identity;id.push_back(std::uint8_t(i));auto r=request(quota,id,source(),64,32);assert(wait(r)==Store::State::Miss);save(quota,r->key,pixels());}
    }
    std::uint64_t diskBytes=0;unsigned files=0;for(const auto& e:std::filesystem::directory_iterator(root/"quota"))if(e.path().extension()==L".nlm"){diskBytes+=e.file_size();++files;}
    assert(diskBytes<=16384&&files>0&&files<64);
    std::puts("MATERIAL_DISK_STORE_PASS revision settings dimensions checksum corruption truncation recovery slowIO boundedQueue retryOwnership unavailableDirectory atomicPublication diskQuota staleTemporaryFiles");
}
