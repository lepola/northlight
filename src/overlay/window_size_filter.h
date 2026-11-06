#pragma once
#include <atomic>
#include <cstdint>

// No window/device calls: the renderer publishes successful primary Presents;
// the window thread alone owns DeliveredSize and finalizes forwarded messages.
namespace NorthlightWindowSize {
struct Geometry {
    unsigned width=0,height=0;
    std::intptr_t style=0,extendedStyle=0;
    int left=0,top=0,right=0,bottom=0;
    bool usable=false;
    bool operator==(const Geometry& b)const{
        return width==b.width&&height==b.height&&style==b.style&&extendedStyle==b.extendedStyle&&
            left==b.left&&top==b.top&&right==b.right&&bottom==b.bottom&&usable==b.usable;
    }
};
struct Context {std::uint64_t session=0,barrier=0;Geometry geometry;bool attached=false,ready=false;};
class DeliveredSize {
    Geometry previous{};
    std::uint64_t session=0,barrier=0,serial=0;
    bool known=false;
public:
    struct Decision {bool suppress=false;std::uint64_t token=0;};
    void invalidate(){known=false;++serial;}
    Decision begin(unsigned state,const Context& c){
        if(session!=c.session||barrier!=c.barrier)known=false;
        session=c.session;barrier=c.barrier;++serial;
        if(state==0&&c.attached&&c.ready&&c.geometry.usable&&known&&previous==c.geometry)return {true,serial};
        known=false;return {false,serial};
    }
    bool delivered(std::uint64_t token,unsigned state,std::uint64_t startedSession,const Context& after){
        if(token!=serial||state!=0||!after.attached||after.session!=startedSession||!after.geometry.usable)return false;
        // The forwarded handler may synchronously Reset. Its new barrier is
        // adopted only after delivery; a later successful Present must match it.
        previous=after.geometry;session=after.session;barrier=after.barrier;known=true;return true;
    }
};

class Publication {
    static constexpr std::uintptr_t Closing=~std::uintptr_t(0);
    static constexpr std::uintptr_t Destroyed=Closing-1;
    std::atomic<std::uintptr_t> owner_{0},window_{0};
    std::atomic<std::uint64_t> session_{0},barrier_{1},shape_{0},presentBarrier_{0},presentSession_{0};
    std::atomic<unsigned> sequence_{0};
    std::atomic<bool> windowed_{false};
public:
    struct Snapshot {
        std::uintptr_t owner=0,window=0;std::uint64_t session=0,barrier=0;
        unsigned width=0,height=0;bool ready=false;
    };
    struct PresentStamp {std::uintptr_t owner,window;std::uint64_t session,barrier;};
    std::uintptr_t owner()const{const auto value=owner_.load(std::memory_order_acquire);return value>=Destroyed?0:value;}
    bool closing()const{return owner_.load(std::memory_order_acquire)>=Destroyed;}
    bool acquire(std::uintptr_t owner,std::uintptr_t window){
        if(!owner||owner>=Destroyed||!window)return false;
        std::uintptr_t expected=0;
        if(!owner_.compare_exchange_strong(expected,owner,std::memory_order_acq_rel))
            return expected==owner&&window_.load(std::memory_order_acquire)==window;
        barrier_.fetch_add(1,std::memory_order_acq_rel);window_.store(window,std::memory_order_release);
        session_.fetch_add(1,std::memory_order_release);return true;
    }
    void invalidate(std::uintptr_t owner){if(owner&&owner_.load(std::memory_order_acquire)==owner)barrier_.fetch_add(1,std::memory_order_acq_rel);}
    void windowChanged(){barrier_.fetch_add(1,std::memory_order_acq_rel);}
    bool beginRelease(std::uintptr_t owner){
        if(!owner||owner>=Destroyed)return false;
        auto expected=owner;
        if(!owner_.compare_exchange_strong(expected,Closing,std::memory_order_acq_rel))return false;
        barrier_.fetch_add(1,std::memory_order_acq_rel);session_.fetch_add(1,std::memory_order_acq_rel);
        window_.store(0,std::memory_order_release);return true;
    }
    bool beginWindowDestruction(){
        auto expected=owner_.load(std::memory_order_acquire);
        if(expected==Closing&&owner_.compare_exchange_strong(expected,Destroyed,std::memory_order_acq_rel))return false;
        if(expected==Destroyed)return false;
        if(!owner_.compare_exchange_strong(expected,Closing,std::memory_order_acq_rel)){
            if(expected==Closing)owner_.compare_exchange_strong(expected,Destroyed,std::memory_order_acq_rel);
            return false;
        }
        barrier_.fetch_add(1,std::memory_order_acq_rel);session_.fetch_add(1,std::memory_order_acq_rel);
        window_.store(0,std::memory_order_release);return true;
    }
    bool beginMaintenance(std::uintptr_t expectedOwner,std::uintptr_t& retainedOwner){
        auto current=owner_.load(std::memory_order_acquire);
        if(current&&current!=expectedOwner)return false;
        if(!owner_.compare_exchange_strong(current,Closing,std::memory_order_acq_rel))return false;
        retainedOwner=current;barrier_.fetch_add(1,std::memory_order_acq_rel);return true;
    }
    bool finishMaintenance(std::uintptr_t retainedOwner){
        auto expected=Closing;
        return owner_.compare_exchange_strong(expected,retainedOwner,std::memory_order_acq_rel);
    }
    // Only the successful claimant calls this, after shared hook cleanup.
    void finishRelease(){window_.store(0,std::memory_order_release);owner_.store(0,std::memory_order_release);}
    void release(std::uintptr_t owner){if(beginRelease(owner))finishRelease();}
    PresentStamp stamp(std::uintptr_t owner,std::uintptr_t window)const{
        const auto session=session_.load(std::memory_order_acquire),barrier=barrier_.load(std::memory_order_acquire);
        return {owner,window,session,barrier};
    }
    void complete(const PresentStamp& stamp,unsigned width,unsigned height,bool windowed,bool success){
        const auto owner=stamp.owner;
        if(!owner||owner_.load(std::memory_order_acquire)!=owner||session_.load(std::memory_order_acquire)!=stamp.session)return;
        if(window_.load(std::memory_order_acquire)!=stamp.window||!success){invalidate(owner);return;}
        if(barrier_.load(std::memory_order_acquire)!=stamp.barrier)return;
        unsigned sequence=sequence_.load(std::memory_order_acquire);
        if((sequence&1)||!sequence_.compare_exchange_strong(sequence,sequence+1,std::memory_order_acq_rel))return;
        // Bounded single-writer claim: teardown never waits for this publisher.
        // Its captured epoch remains attached to every field if teardown races.
        shape_.store(std::uint64_t(width)|(std::uint64_t(height)<<32),std::memory_order_relaxed);
        windowed_.store(windowed,std::memory_order_relaxed);presentBarrier_.store(stamp.barrier,std::memory_order_relaxed);
        presentSession_.store(stamp.session,std::memory_order_relaxed);sequence_.store(sequence+2,std::memory_order_release);
    }
    void presented(std::uintptr_t owner,std::uintptr_t window,unsigned width,unsigned height,bool windowed,bool success){complete(stamp(owner,window),width,height,windowed,success);}
    Snapshot snapshot()const{
        Snapshot s;s.session=session_.load(std::memory_order_acquire);s.barrier=barrier_.load(std::memory_order_acquire);
        const auto before=sequence_.load(std::memory_order_acquire);if(before&1)return s;
        s.owner=owner();s.window=window_.load(std::memory_order_acquire);
        const auto shape=shape_.load(std::memory_order_relaxed);s.width=unsigned(shape);s.height=unsigned(shape>>32);
        const bool windowed=windowed_.load(std::memory_order_relaxed);
        s.ready=s.owner&&s.window&&s.width&&s.height&&windowed&&presentBarrier_.load(std::memory_order_relaxed)==s.barrier&&presentSession_.load(std::memory_order_relaxed)==s.session;
        if(before!=sequence_.load(std::memory_order_acquire)||s.session!=session_.load(std::memory_order_acquire)||s.barrier!=barrier_.load(std::memory_order_acquire))s.ready=false;
        return s;
    }
};
}
