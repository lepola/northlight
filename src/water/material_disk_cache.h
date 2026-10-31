#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "backend_loader.h"
#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall MoveFileExW(const wchar_t*,const wchar_t*,unsigned long);
#endif

// CPU bytes only. The owner never opens files, waits for the worker or helps run
// its jobs. A slow/full/unavailable store leaves the material's native fallback.
namespace NorthlightMaterialDisk {
using Bytes=std::vector<std::uint8_t>;
struct Key {
    std::array<std::uint8_t,32> bytes{};
    bool operator==(const Key& k)const{return bytes==k.bytes;}
    bool operator!=(const Key& k)const{return !(*this==k);}
};
// Reuse the backend identity implementation. Domain prefixes and payloads are
// hashed on the worker; resource addresses and installation paths are excluded.
inline Key hash(const void* data,size_t size,Key prefix={}){
    NorthlightBackendLoader::Sha256 digest;digest.update(prefix.bytes.data(),prefix.bytes.size());digest.update(data,size);
    const auto hex=digest.hex();Key key;auto nibble=[](char c){return unsigned(c<='9'?c-'0':c-'a'+10);};
    for(unsigned i=0;i<32;++i)key.bytes[i]=std::uint8_t(nibble(hex[i*2])*16+nibble(hex[i*2+1]));return key;
}
inline std::string filename(Key k){
    const char digits[]="0123456789abcdef";std::string out;out.reserve(68);
    for(auto b:k.bytes){out+=digits[b>>4];out+=digits[b&15];}return out+".nlm";
}
class Store {
public:
    static constexpr size_t MaxMapBytes=512u*512u*4u,MaxSourceBytes=8u<<20,MaxBytes=12u<<20,MaxJobs=8;
    static constexpr std::uint64_t DiskBytes=4ull<<30;
    enum class State {Pending,Hit,Miss,Stored,Error};
    struct Budget {std::atomic<size_t> bytes{0},jobs{0};};
    struct Ticket {
        std::atomic<State> state{State::Pending};Key key;unsigned width=0,height=0;Bytes identity,source,pixels;
        std::shared_ptr<Budget> budget;size_t charge=0;bool write=false,jobReserved=false;
        ~Ticket(){if(budget){budget->bytes.fetch_sub(charge);if(jobReserved)budget->jobs.fetch_sub(1);}}
    };
    using Handle=std::shared_ptr<Ticket>;
    struct Stats {std::atomic<std::uint64_t> hits{0},misses{0},writes{0},invalid{0},errors{0},refused{0},evicted{0};};
private:
    struct Header {std::uint64_t magic=0x314d4c4e00000001ull;Key key;std::uint32_t width=0,height=0,bytes=0,reserved=0;Key checksum;};
    static_assert(sizeof(Header)==88,"Disk header has a fixed layout");
    std::filesystem::path root_;
    std::shared_ptr<Budget> budget_=std::make_shared<Budget>();
    std::mutex mutex_;std::condition_variable wake_;std::deque<Handle> queue_;bool stop_=false;
    std::atomic<bool> available_{true};std::thread worker_;std::uint64_t serial_=0,diskBytes_=DiskBytes;unsigned writes_=0;
    void(*beforeIO_)(void*)=nullptr;void* ioContext_=nullptr;
    Handle enqueue(Bytes& identity,Bytes& source,unsigned width,unsigned height,Key key,bool write){
        if(!available_.load(std::memory_order_acquire)||!width||!height||width>512||height>512||source.size()>MaxSourceBytes||identity.size()>1024)return {};
        const size_t charge=source.size()+MaxMapBytes;
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock||stop_||!available_.load()||budget_->jobs.load()>=MaxJobs||budget_->bytes.load()>MaxBytes-charge){++stats.refused;return {};}
        try{
            auto t=std::make_shared<Ticket>();queue_.push_back(t); // Allocate before consuming retryable input.
            t->identity=std::move(identity);t->source=std::move(source);t->width=width;t->height=height;t->key=key;t->write=write;
            t->budget=budget_;t->charge=charge;t->jobReserved=true;budget_->bytes.fetch_add(charge);budget_->jobs.fetch_add(1);
            lock.unlock();wake_.notify_one();return t;
        }catch(...){++stats.refused;return {};}
    }
    bool read(Ticket& t){
        std::ifstream f(root_/filename(t.key),std::ios::binary|std::ios::ate);if(!f)return false;
        const auto length=f.tellg();Header h{};f.seekg(0);f.read(reinterpret_cast<char*>(&h),sizeof h);
        const size_t bytes=size_t(t.width)*t.height*4;
        if(!f||h.magic!=Header{}.magic||h.key!=t.key||h.reserved||h.width!=t.width||h.height!=t.height||h.bytes!=bytes||length!=std::streamoff(sizeof h+bytes)){++stats.invalid;return false;}
        Bytes pixels(bytes);f.read(reinterpret_cast<char*>(pixels.data()),std::streamsize(bytes));
        if(!f||hash(pixels.data(),pixels.size())!=h.checksum){++stats.invalid;return false;}
        t.pixels=std::move(pixels);std::error_code ec;std::filesystem::last_write_time(root_/filename(t.key),std::filesystem::file_time_type::clock::now(),ec);return true;
    }
    bool write(Ticket& t){
        if(t.source.size()!=size_t(t.width)*t.height*4)return false;
        Header h;h.key=t.key;h.width=t.width;h.height=t.height;h.bytes=std::uint32_t(t.source.size());h.checksum=hash(t.source.data(),t.source.size());
        const auto final=root_/filename(t.key);auto temporary=final;temporary+=std::string(".tmp-")+std::to_string(++serial_);
        bool written=false;
        {std::ofstream f(temporary,std::ios::binary|std::ios::trunc);if(f){f.write(reinterpret_cast<const char*>(&h),sizeof h);f.write(reinterpret_cast<const char*>(t.source.data()),std::streamsize(t.source.size()));f.close();written=bool(f);}}
        std::error_code ec;
        if(written){
#ifdef _WIN32
            written=MoveFileExW(temporary.c_str(),final.c_str(),1)!=0;
#else
            std::filesystem::rename(temporary,final,ec);written=!ec;
#endif
        }
        if(!written)std::filesystem::remove(temporary,ec);return written;
    }
    void trim(){
        struct File {std::filesystem::path path;std::filesystem::file_time_type used;std::uint64_t bytes;};
        std::vector<File> files;std::uint64_t bytes=0;std::error_code ec;
        for(std::filesystem::directory_iterator it(root_,ec),end;!ec&&it!=end;it.increment(ec)){
            const auto& e=*it;if(e.is_symlink(ec)||!e.is_regular_file(ec))continue;
            const auto name=e.path().filename().native();if(name.size()<68||!std::all_of(name.begin(),name.begin()+64,[](auto c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))continue;
            const auto suffix=e.path().filename().generic_u8string().substr(64);
            if(suffix.size()>9&&suffix.substr(0,9)==".nlm.tmp-"&&std::all_of(suffix.begin()+9,suffix.end(),[](char c){return c>='0'&&c<='9';})){
                const auto used=e.last_write_time(ec);if(!ec&&used<std::filesystem::file_time_type::clock::now()-std::chrono::hours(24))std::filesystem::remove(e.path(),ec);continue;
            }
            if(name.size()!=68||e.path().extension()!=L".nlm")continue;
            const auto size=e.file_size(ec);if(ec)break;const auto used=e.last_write_time(ec);if(ec)break;
            files.push_back({e.path(),used,size});bytes+=size;if(files.size()>=65536)break;
        }
        if(bytes<=diskBytes_&&files.size()<16384)return;
        std::sort(files.begin(),files.end(),[](const File& a,const File& b){return a.used<b.used;});
        auto remaining=files.size();for(const auto& f:files){if(bytes<=diskBytes_-diskBytes_/8&&remaining<=14336)break;if(std::filesystem::remove(f.path,ec)){bytes-=f.bytes;--remaining;++stats.evicted;}}
    }
    void complete(Ticket& t,State state){
        Bytes().swap(t.source);Bytes().swap(t.identity);
        const size_t retained=state==State::Hit?MaxMapBytes:0;
        t.budget->bytes.fetch_sub(t.charge-retained);t.charge=retained;
        t.budget->jobs.fetch_sub(1);t.jobReserved=false;t.state.store(state,std::memory_order_release);
    }
    void failPending(){
        available_.store(false);std::deque<Handle> pending;
        {std::lock_guard<std::mutex> lock(mutex_);queue_.swap(pending);}
        for(auto& t:pending)complete(*t,State::Error);
    }
    void loop(){
        try{std::error_code ec;std::filesystem::create_directories(root_,ec);if(ec){failPending();return;}trim();}catch(...){failPending();return;}
        for(;;){
            Handle t;
            {std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[&]{return stop_||!queue_.empty();});if(queue_.empty()&&stop_)return;t=std::move(queue_.front());queue_.pop_front();}
            State state=State::Error;
            try{
                if(beforeIO_)beforeIO_(ioContext_);
                if(t->write){if(write(*t)){++stats.writes;state=State::Stored;if(++writes_%32==0)trim();}else ++stats.errors;}
                else {t->key=hash(t->source.data(),t->source.size(),hash(t->identity.data(),t->identity.size()));if(read(*t)){++stats.hits;state=State::Hit;}else{++stats.misses;state=State::Miss;}}
            }catch(...){++stats.errors;}
            complete(*t,state);
        }
    }
public:
    Stats stats;
    explicit Store(std::filesystem::path root,void(*beforeIO)(void*)=nullptr,void* context=nullptr,std::uint64_t diskBytes=DiskBytes):root_(std::move(root)),diskBytes_(diskBytes),beforeIO_(beforeIO),ioContext_(context){
        serial_=std::uint64_t(std::chrono::high_resolution_clock::now().time_since_epoch().count())^std::uint64_t(reinterpret_cast<std::uintptr_t>(this));
        worker_=std::thread([this]{loop();});
    }
    Store(const Store&)=delete;Store& operator=(const Store&)=delete;
    ~Store(){{std::lock_guard<std::mutex> lock(mutex_);stop_=true;}wake_.notify_one();if(worker_.joinable())worker_.join();}
    bool available()const{return available_.load(std::memory_order_acquire);}
    Handle request(Bytes identity,Bytes source,unsigned width,unsigned height){return enqueue(identity,source,width,height,{},false);}
    bool save(Key key,unsigned width,unsigned height,Bytes& pixels){Bytes identity;return bool(enqueue(identity,pixels,width,height,key,true));}
    size_t outstandingBytes()const{return budget_->bytes.load();}
};
}
