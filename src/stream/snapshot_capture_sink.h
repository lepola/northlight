#pragma once
// Explicit capture only: never constructed or connected by the normal renderer.
#include "snapshot_archive.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

namespace NorthlightStream {
class SnapshotCaptureSink {
public:
    static constexpr unsigned Slots=8,MaxRecords=20000;
    static constexpr std::uint64_t MaxSessionBytes=256ull<<20;
    enum Failure:unsigned {None=0,Destination=1,Overflow=2,Budget=4,Archive=8,Io=16,Producer=32};
    struct Limits {unsigned records=MaxRecords;std::uint64_t bytes=MaxSessionBytes;};
    struct Status {unsigned accepted,retired,failures;std::uint64_t budgetBytes;bool joined,traceJoinValidated;};
    struct Receipt {
        bool accepted=false;unsigned record=0,bytes=0;std::uint32_t crc32=0;
        std::uintptr_t device=0;std::uint64_t frame=0,draw=0;
        std::uint32_t epoch=0,thread=0,trigger=0;
    };
    SnapshotCaptureSink()=default;
    ~SnapshotCaptureSink(){finish();}
    SnapshotCaptureSink(const SnapshotCaptureSink&)=delete;
    SnapshotCaptureSink& operator=(const SnapshotCaptureSink&)=delete;
    bool start(const std::filesystem::path& destination,const std::string& session,Limits limits){
        return open(destination,session,limits,true);
    }
    bool start(const std::filesystem::path& destination,const std::string& session){return start(destination,session,Limits{});}
    // Producer: fixed storage, bounded codec work; no allocation, file IO,
    // mutex acquisition or waiting. A full session is permanently incomplete.
    bool submit(const GameSnapshot& snapshot,std::uintptr_t device,std::uint64_t frame,std::uint32_t epoch,std::uint32_t thread,Receipt* receipt=nullptr) noexcept {
        if(receipt)*receipt={};
        if(!enabled_.load(std::memory_order_acquire))return false;
        if(producerBusy_.test_and_set(std::memory_order_acquire)){fail(Producer);return false;}
        struct Release {std::atomic_flag& flag;~Release(){flag.clear(std::memory_order_release);}} release{producerBusy_};
        if(!device||!thread||(producerThread_&&producerThread_!=thread)){fail(Producer);return false;}
        producerThread_=thread;
        const auto w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
        if(w-r>=Slots){fail(Overflow);return false;}
        if(w>=limits_.records){fail(Budget);return false;}
        Slot& slot=slots_[w%Slots];SnapshotArchive::Error error;std::size_t size=0;
        if(!SnapshotArchive::encode(snapshot,slot.bytes.data(),slot.bytes.size(),size,error)){fail(Archive);return false;}
        const auto charge=std::uint64_t(size)+RecordLineBytes;
        if(budget_>limits_.bytes||charge>limits_.bytes-budget_){fail(Budget);return false;}
        slot.size=unsigned(size);slot.device=device;slot.frame=frame;slot.epoch=epoch;slot.thread=thread;
        slot.draw=snapshot.triggerDraw;slot.trigger=snapshot.triggerKind;budget_+=charge;
        budgetBytes_.store(budget_,std::memory_order_relaxed);
        if(receipt)*receipt={true,w,unsigned(size),SnapshotArchive::u32(slot.bytes.data()+size-4),device,frame,snapshot.triggerDraw,epoch,thread,snapshot.triggerKind};
        write_.store(w+1,std::memory_order_release);wake_.notify_one();return true;
    }
    static void completed(void* sink,const GameSnapshot& snapshot,std::uintptr_t device,std::uint64_t frame,std::uint32_t epoch,std::uint32_t thread) noexcept {
        if(sink)static_cast<SnapshotCaptureSink*>(sink)->submit(snapshot,device,frame,epoch,thread);
    }
    // Owner only, after the producer/device can no longer call completed().
    // This is the only API that waits; normal rendering never invokes it.
    bool finish(){
        enabled_.store(false,std::memory_order_release);stopping_.store(true,std::memory_order_release);wake_.notify_one();
        if(writer_.joinable())writer_.join();
        joined_.store(true,std::memory_order_release);return started_&&failures_.load(std::memory_order_acquire)==None&&read_.load()==write_.load();
    }
    Status status()const{
        const auto retired=read_.load(std::memory_order_acquire),accepted=write_.load(std::memory_order_acquire);
        return {accepted,retired,failures_.load(),budgetBytes_.load(),joined_.load(std::memory_order_acquire),false};
    }
private:
    friend struct SnapshotCaptureSinkTest;
    friend class NativeSnapshotCapture;
    static constexpr unsigned RecordLineBytes=512;
    struct Slot {
        std::array<unsigned char,SnapshotArchive::MaxBytes> bytes;
        unsigned size=0;std::uintptr_t device=0;std::uint64_t frame=0,draw=0;
        std::uint32_t epoch=0,thread=0,trigger=0;
    };
    class File {
#ifdef _WIN32
        HANDLE value_=INVALID_HANDLE_VALUE;
#else
        std::FILE* value_=nullptr;
#endif
    public:
        ~File(){close();}
        bool create(const std::filesystem::path& path){
#ifdef _WIN32
            value_=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);return value_!=INVALID_HANDLE_VALUE;
#else
            value_=std::fopen(path.c_str(),"wbx");return value_!=nullptr;
#endif
        }
        bool write(const void* bytes,std::size_t size){
#ifdef _WIN32
            DWORD written=0;return value_!=INVALID_HANDLE_VALUE&&size<=MAXDWORD&&WriteFile(value_,bytes,DWORD(size),&written,nullptr)&&written==size;
#else
            return value_&&std::fwrite(bytes,1,size,value_)==size;
#endif
        }
        bool close(){
#ifdef _WIN32
            if(value_==INVALID_HANDLE_VALUE)return true;const auto value=value_;value_=INVALID_HANDLE_VALUE;return CloseHandle(value)!=FALSE;
#else
            if(!value_)return true;auto* value=value_;value_=nullptr;return std::fclose(value)==0;
#endif
        }
    };
    std::array<Slot,Slots> slots_;
    std::atomic<unsigned> write_{0},read_{0},failures_{None};
    std::atomic<bool> enabled_{false},stopping_{false};std::atomic_flag producerBusy_=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> budgetBytes_{0};
    std::uint64_t budget_=RecordLineBytes*2;std::uint32_t producerThread_=0;
    Limits limits_;std::filesystem::path destination_;std::string session_;
    std::mutex waitMutex_;std::condition_variable wake_;std::thread writer_;
    File journal_;bool started_=false;std::atomic<bool> joined_{false};
    void fail(unsigned why)noexcept {failures_.fetch_or(why,std::memory_order_relaxed);enabled_.store(false,std::memory_order_release);wake_.notify_one();}
    bool open(const std::filesystem::path& destination,const std::string& session,Limits limits,bool launch){
        if(started_||joined_.load()||failures_.load()!=None||!destination.is_absolute()||session.empty()||session.size()>64||
           !limits.records||limits.records>MaxRecords||limits.bytes<RecordLineBytes*2||limits.bytes>MaxSessionBytes){fail(Destination);return false;}
        for(char c:session)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-')){fail(Destination);return false;}
        std::error_code error;
        if(!std::filesystem::create_directory(destination,error)){fail(Destination);return false;}
        destination_=destination;session_=session;limits_=limits;
        if(!journal_.create(destination_/"capture.journal.jsonl")){fail(Io);return false;}
        char line[RecordLineBytes];const int n=std::snprintf(line,sizeof line,"{\"format\":1,\"kind\":\"snapshot-journal\",\"session\":\"%s\",\"trace_join_validated\":false}\n",session_.c_str());
        if(n<0||unsigned(n)>=sizeof line||!journal_.write(line,std::size_t(n))){fail(Io);return false;}
        started_=true;budgetBytes_.store(budget_);enabled_.store(true,std::memory_order_release);
        if(launch){try{writer_=std::thread([this]{consume();});}catch(...){fail(Io);return false;}}
        return true;
    }
    void consume(){
        try{
            for(;;){
                auto r=read_.load(std::memory_order_relaxed);
                if(r==write_.load(std::memory_order_acquire)){
                    if(stopping_.load(std::memory_order_acquire))break;
                    std::unique_lock<std::mutex> lock(waitMutex_);
                    // Timed worker-side wait also covers a notify just before
                    // wait entry; the producer never acquires this mutex.
                    wake_.wait_for(lock,std::chrono::milliseconds(10),[this]{return stopping_.load()||read_.load()!=write_.load();});
                    continue;
                }
                const auto& slot=slots_[r%Slots];char filename[32];std::snprintf(filename,sizeof filename,"%06u.nlsnap",r);
                File output;bool ok=output.create(destination_/filename)&&output.write(slot.bytes.data(),slot.size);ok=output.close()&&ok;
                char line[RecordLineBytes];const int n=std::snprintf(line,sizeof line,
                    "{\"record\":%u,\"device_key\":%llu,\"frame\":%llu,\"frame_domain\":\"stream-producer\",\"draw_ordinal\":%llu,\"epoch\":%u,\"thread\":%u,\"trigger\":%u,\"path\":\"%s\",\"bytes\":%u,\"archive_crc32\":%u}\n",
                    r,(unsigned long long)slot.device,(unsigned long long)slot.frame,(unsigned long long)slot.draw,slot.epoch,slot.thread,slot.trigger,filename,slot.size,SnapshotArchive::u32(slot.bytes.data()+slot.size-4));
                if(n<0||unsigned(n)>=sizeof line||!ok||!journal_.write(line,std::size_t(n)))fail(Io);
                read_.store(r+1,std::memory_order_release);
            }
            char line[RecordLineBytes];const int n=std::snprintf(line,sizeof line,"{\"writer_drained\":true,\"accepted\":%u,\"retired\":%u,\"failures\":%u,\"trace_join_validated\":false}\n",write_.load(),read_.load(),failures_.load());
            if(n<0||unsigned(n)>=sizeof line||!journal_.write(line,std::size_t(n)))fail(Io);
            if(!journal_.close())fail(Io);
        }catch(...){fail(Io);}
    }
};
} // namespace NorthlightStream
