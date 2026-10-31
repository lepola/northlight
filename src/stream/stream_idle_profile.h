#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#endif

namespace NorthlightStream {
struct IdleCpuSample {std::uint64_t kernelNs=0,userNs=0,readBeforeNs=0,readAfterNs=0;bool known=false;};
// A real query-only handle to the creating thread; never a pseudo-handle read
// from the replay thread. Failed/unsupported reads remain unknown.
class IdleProducerCpu {
public:
    struct Api {void* (*open)();bool (*read)(void*,IdleCpuSample&);void (*close)(void*);};
private:
    void* handle_=nullptr;const Api* api_=nullptr;
    static const Api& native(){
        static const Api api{
            []()->void*{
#ifdef _WIN32
                return OpenThread(THREAD_QUERY_INFORMATION,FALSE,GetCurrentThreadId());
#else
                return nullptr;
#endif
            },
            [](void* handle,IdleCpuSample& out)->bool{
#ifdef _WIN32
                FILETIME created{},exited{},kernel{},user{};
                if(!GetThreadTimes(static_cast<HANDLE>(handle),&created,&exited,&kernel,&user))return false;
                out.kernelNs=((std::uint64_t(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime)*100;
                out.userNs=((std::uint64_t(user.dwHighDateTime)<<32)|user.dwLowDateTime)*100;return true;
#else
                (void)handle;(void)out;return false;
#endif
            },
            [](void* handle){
#ifdef _WIN32
                CloseHandle(static_cast<HANDLE>(handle));
#else
                (void)handle;
#endif
            }};
        return api;
    }
public:
    IdleProducerCpu()=default;~IdleProducerCpu(){reset();}
    IdleProducerCpu(const IdleProducerCpu&)=delete;IdleProducerCpu& operator=(const IdleProducerCpu&)=delete;
    void start(bool enabled,const Api* api=nullptr){reset();if(enabled){api_=api?api:&native();handle_=api_->open();}}
    void reset(){if(handle_)api_->close(handle_);handle_=nullptr;api_=nullptr;}
    IdleCpuSample sample()const{IdleCpuSample out;out.known=handle_&&api_->read(handle_,out);return out;}
};

struct IdleImageLock {
    std::uint64_t id=0,key=0,entryNs=0,returnedNs=0,unlockNs=0,caller=0,frame=0;
    std::uint32_t fmt=0,w=0,h=0,d=0,level=0,face=0,pool=0,usage=0,flags=0,route=0,mode=0,epoch=0;
    // mode 0: inside Lock; 1..3: returned staged/pass/shadow; 4: failed Lock.
    std::uint64_t overlap(std::uint64_t start,std::uint64_t end)const{
        if(!id||!entryNs||end<start)return 0;
        const auto a=std::max(start,entryNs),b=std::min(end,unlockNs?unlockNs:end);return b>a?b-a:0;
    }
};
struct IdleImageSelection {IdleImageLock lock;std::uint64_t overlapNs=0,overflow=0;bool stable=true;};
// One producer, one observer. Every mirrored field is atomic: a revision check
// alone around ordinary C++ fields would still be a data race. No retries/locks.
class IdleImageLocks {
    static constexpr unsigned Fields=19;
    struct Slot {
        std::atomic<std::uint64_t> serial{0},retired{0};std::array<std::atomic<std::uint64_t>,Fields> data{};
        void put(const IdleImageLock& p){
            const std::array<std::uint64_t,Fields> values{{p.id,p.key,p.entryNs,p.returnedNs,p.unlockNs,p.caller,p.frame,p.fmt,p.w,p.h,p.d,p.level,p.face,p.pool,p.usage,p.flags,p.route,p.mode,p.epoch}};
            serial.fetch_add(1);for(unsigned i=0;i<Fields;++i)data[i].store(values[i]);serial.fetch_add(1);
        }
        bool get(IdleImageLock& p)const{
            const auto before=serial.load();if(before&1)return false;
            std::array<std::uint64_t,Fields> a{};for(unsigned i=0;i<Fields;++i)a[i]=data[i].load();
            if(before!=serial.load())return false;
            p.id=a[0];p.key=a[1];p.entryNs=a[2];p.returnedNs=a[3];p.unlockNs=a[4];p.caller=a[5];p.frame=a[6];
            p.fmt=std::uint32_t(a[7]);p.w=std::uint32_t(a[8]);p.h=std::uint32_t(a[9]);p.d=std::uint32_t(a[10]);p.level=std::uint32_t(a[11]);p.face=std::uint32_t(a[12]);p.pool=std::uint32_t(a[13]);p.usage=std::uint32_t(a[14]);p.flags=std::uint32_t(a[15]);p.route=std::uint32_t(a[16]);p.mode=std::uint32_t(a[17]);p.epoch=std::uint32_t(a[18]);return true;
        }
    };
public:
    static constexpr unsigned Active=8,Completed=4;
private:
    std::array<IdleImageLock,Active> active_{};std::array<Slot,Active> live_;std::array<Slot,Completed> completed_;
    std::atomic<std::uint64_t> overflow_{0},revision_{0};std::uint64_t next_=0;unsigned completedNext_=0;bool enabled_=false;
    void archive(const IdleImageLock& p){
        if(p.unlockNs>=p.entryNs&&p.unlockNs-p.entryNs>=20000000)completed_[completedNext_++%Completed].put(p);
    }
public:
    // Creating thread only, before starting/restarting the observer.
    void enable(bool on){if(!on&&!enabled_)return;enabled_=on;active_={};next_=0;completedNext_=0;overflow_.store(0);revision_.store(0);for(auto& s:live_){s.put({});s.retired.store(0);}for(auto& s:completed_)s.put({});}
    bool enabled()const{return enabled_;}
    std::uint64_t begin(IdleImageLock p){
        if(!enabled_)return 0;
        revision_.fetch_add(1);
        for(unsigned i=0;i<Active;++i)if(active_[i].id&&(live_[i].retired.load()==active_[i].id||active_[i].key==p.key)){active_[i]={};live_[i].put({});overflow_.fetch_add(1);}
        for(unsigned i=0;i<Active;++i)if(!active_[i].id){p.id=++next_;active_[i]=p;live_[i].put(p);revision_.fetch_add(1);return p.id;}
        overflow_.fetch_add(1);revision_.fetch_add(1);return 0;
    }
    void returned(std::uint64_t id,std::uint64_t ns,unsigned mode){
        if(!id)return;
        revision_.fetch_add(1);
        for(unsigned i=0;i<Active;++i)if(active_[i].id==id){auto& p=active_[i];if(live_[i].retired.load()==id){p={};live_[i].put(p);revision_.fetch_add(1);return;}p.returnedNs=ns;p.mode=mode;
            if(!mode){p.mode=4;p.unlockNs=ns;archive(p);p={};}live_[i].put(p);revision_.fetch_add(1);return;}
        revision_.fetch_add(1);
    }
    IdleImageLock unlock(std::uint64_t key,std::uint64_t ns){
        IdleImageLock result;if(!enabled_)return result;
        revision_.fetch_add(1);
        for(unsigned i=0;i<Active;++i)if(active_[i].id&&active_[i].key==key){auto& p=active_[i];p.unlockNs=ns;if(live_[i].retired.load()!=p.id){archive(p);if(p.unlockNs>=p.entryNs&&p.unlockNs-p.entryNs>=20000000)result=p;}p={};live_[i].put(p);break;}
        revision_.fetch_add(1);return result;
    }
    // Resource destruction can be on replay. It invalidates only the matching
    // published token, never producer-private state or a later reused address.
    void retire(std::uint64_t key){
        if(!enabled_)return;
        for(auto& slot:live_){IdleImageLock p;if(slot.get(p)&&p.id&&p.key==key)slot.retired.store(p.id);}
    }
    IdleImageSelection select(std::uint64_t start,std::uint64_t end)const{
        IdleImageSelection result;const auto before=revision_.load();result.overflow=overflow_.load();
        auto observe=[&](const Slot& slot){IdleImageLock p;if(!slot.get(p)){result.stable=false;return;}if(p.id&&slot.retired.load()==p.id){result.stable=false;return;}const auto overlap=p.overlap(start,end);
            if(overlap>result.overlapNs){result.lock=p;result.overlapNs=overlap;}};
        for(const auto& s:live_)observe(s);for(const auto& s:completed_)observe(s);
        result.stable=result.stable&&!(before&1)&&before==revision_.load();return result;
    }
};
// Boundary observations, not an attribution of the entire interval to a thread
// or a blocking call. Producer counters can include work outside the interval.
struct IdlePoint {
    std::uint64_t recorded=0,published=0,replayed=0,waitSeq=0;
    std::uint64_t syncNs=0,presentNs=0,backpressureNs=0,recordNs=0,lockNs=0,snapshotNs=0,publishes=0;
    std::uint64_t frame=0,presents=0;std::uint32_t epoch=0;std::int32_t cooperative=0;
    bool known=false,consistent=false,backpressure=false,express=false,contextKnown=false;
    unsigned waits()const{return (waitSeq?1u:0u)|(backpressure?2u:0u)|(express?4u:0u);}
    const char* queueState()const{
        if(!known||!consistent||replayed>published||published>recorded)return "unknown";
        return published>replayed?"published":recorded>published?"unpublished":"empty";
    }
};
struct IdleGap {
    IdlePoint first,last;
    std::uint64_t startNs=0,endNs=0,waitNs=0;unsigned segments=0,waitsSeen=0;
    std::uint16_t before=0xffff,after=0xffff;bool interrupted=false;
    IdleCpuSample cpuFirst,cpuLast;IdleImageSelection image;
    std::uint64_t wallNs()const{return endNs>=startNs?endNs-startNs:0;}
};
struct IdleReport {unsigned frames=0,gaps=0,count=0;std::array<IdleGap,4> worst{};};
class IdleGapProfile {
    bool enabled_=false,open_=false;IdleGap current_;IdleReport window_;
public:
    static constexpr std::uint64_t ThresholdNs=20000000;
    static constexpr unsigned WindowFrames=120;
    void enable(bool on){if(enabled_==on)return;enabled_=on;open_=false;current_={};window_={};}
    bool open()const{return enabled_&&open_;}
    std::uint64_t startNs()const{return current_.startNs;}
    void wait(std::uint64_t start,std::uint64_t end,const IdlePoint& first,const IdlePoint& last,std::uint16_t before,const IdleCpuSample& cpu={}){
        if(!enabled_)return;
        if(end<start||(open_&&start<current_.endNs)){open_=false;current_={};return;}
        if(!open_){current_={};current_.first=first;current_.startNs=start;current_.before=before;current_.cpuFirst=cpu;open_=true;}
        current_.last=last;current_.endNs=end;current_.waitNs+=end-start;++current_.segments;
        current_.waitsSeen|=first.waits()|last.waits();
    }
    void finish(std::uint64_t end,const IdlePoint& last,std::uint16_t after,bool interrupted=false,const IdleCpuSample& cpu={},const IdleImageSelection& image={}){
        if(!open())return;
        open_=false;
        if(end<current_.endNs)return;
        current_.last=last;current_.endNs=end;current_.after=after;current_.interrupted=interrupted;current_.waitsSeen|=last.waits();
        if(current_.wallNs()<ThresholdNs)return;
        current_.cpuLast=cpu;current_.image=image;
        ++window_.gaps;
        if(window_.count<window_.worst.size())window_.worst[window_.count++]=current_;
        else{
            auto least=std::min_element(window_.worst.begin(),window_.worst.end(),[](const IdleGap& a,const IdleGap& b){return a.wallNs()<b.wallNs();});
            if(current_.wallNs()>least->wallNs())*least=current_;
        }
    }
    bool presented(){return enabled_&&++window_.frames>=WindowFrames;}
    IdleReport take(){auto result=window_;window_={};return result;}
    static long long delta(std::uint64_t first,std::uint64_t last){
        return last>=first&&last-first<=std::uint64_t(std::numeric_limits<long long>::max())?static_cast<long long>(last-first):-1;
    }
};
}
