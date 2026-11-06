#pragma once
// Diagnostic correspondence for actual self-read results. Never reads a live
// process, changes the source result, or treats a raw file as loaded memory.
#include "client_image.h"
#include "snapshot_archive.h"
#include <array>
#include <atomic>
#include <limits>

namespace NorthlightStream::ClientCodeObservation {
constexpr std::size_t MaxSpans=128,MaxSpanBytes=512,MaxPayloadBytes=64u*1024u;
constexpr std::size_t HeaderBytes=48,RecordBytes=16,ChecksumBytes=4;
constexpr std::size_t MaxEncodedBytes=HeaderBytes+MaxSpans*RecordBytes+MaxPayloadBytes+ChecksumBytes;
static_assert(MaxEncodedBytes<=96u*1024u,"Bounded diagnostic allowance");
constexpr unsigned Version=1;
enum class Error {None,State,Buffer,Magic,Version,Bounds,Checksum,Record,Counts,Duplicate,Address,Image,Correspondence};
inline const char* name(Error error){
    switch(error){case Error::None:return "none";case Error::State:return "state";case Error::Buffer:return "buffer";
    case Error::Magic:return "magic";case Error::Version:return "version";case Error::Bounds:return "bounds";
    case Error::Checksum:return "checksum";case Error::Record:return "record";case Error::Counts:return "counts";
    case Error::Duplicate:return "duplicate";case Error::Address:return "address";case Error::Image:return "image";
    default:return "correspondence";}
}
class Capture;
class Proof;
inline bool encode(const Capture&,unsigned char*,std::size_t,std::size_t&,Error&)noexcept;
inline bool decode(const unsigned char*,std::size_t,const ClientImage::View&,Proof&,Error&)noexcept;

class Proof {
    struct Span {std::uint32_t address=0,size=0,offset=0,observations=0;};
    std::array<Span,MaxSpans> spans_{};
    std::array<unsigned char,MaxPayloadBytes> bytes_{};
    std::size_t count_=0,used_=0;
    std::uint64_t observations_=0;
    bool valid_=false;
    void clear()noexcept{valid_=false;count_=used_=0;observations_=0;}
    const Span* find(std::uintptr_t address,std::size_t size)const noexcept{
        for(std::size_t i=0;i<count_;++i)if(spans_[i].address==address&&spans_[i].size==size)return &spans_[i];
        return nullptr;
    }
    friend class Capture;
    friend bool encode(const Capture&,unsigned char*,std::size_t,std::size_t&,Error&)noexcept;
    friend bool decode(const unsigned char*,std::size_t,const ClientImage::View&,Proof&,Error&)noexcept;
public:
    bool valid()const noexcept{return valid_;}
    std::size_t spans()const noexcept{return count_;}
    std::size_t payloadBytes()const noexcept{return used_;}
    std::uint64_t observations()const noexcept{return observations_;}
    bool covers(std::uintptr_t address,std::size_t size)const noexcept{return valid_&&find(address,size);}
    bool read(std::uintptr_t address,void* output,std::size_t size)const noexcept{
        if(!valid_||!output)return false;
        const auto* span=find(address,size);if(!span)return false;
        std::memcpy(output,bytes_.data()+span->offset,size);return true;
    }
};

class Capture {
    Proof proof_;
    std::atomic_flag busy_=ATOMIC_FLAG_INIT;
    std::atomic<bool> failed_{true};
    bool begun_=false;
    friend bool encode(const Capture&,unsigned char*,std::size_t,std::size_t&,Error&)noexcept;
public:
    // Owner only, before installing the observer. A previous self read may have
    // latched a signature result; clearing a counter cannot prove that prefix.
    bool begin(std::uint64_t prefixCalls)noexcept{
        if(begun_){failed_.store(true,std::memory_order_release);return false;}
        proof_.clear();begun_=true;failed_.store(prefixCalls!=0,std::memory_order_relaxed);
        return prefixCalls==0;
    }
    void record(const ClientImage::View& image,std::uintptr_t address,const void* bytes,std::size_t size,bool ok)noexcept{
        if(!begun_||failed_.load(std::memory_order_acquire))return;
        const auto region=image.region(address,size);
        if(region==ClientImage::Region::Runtime||region==ClientImage::Region::Outside)return;
        if(busy_.test_and_set(std::memory_order_acquire)){failed_.store(true,std::memory_order_release);return;}
        struct Release {std::atomic_flag& flag;~Release(){flag.clear(std::memory_order_release);}} release{busy_};
        auto fail=[&]{failed_.store(true,std::memory_order_release);};
        if(failed_.load(std::memory_order_acquire))return;
        if(region!=ClientImage::Region::Static||!ok||!bytes||!size||size>MaxSpanBytes){fail();return;}
        unsigned char raw[MaxSpanBytes];
        // Compare on EVERY read, including duplicate keys. A later patch must
        // not inherit an earlier successful observation.
        if(!image.readStatic(address,raw,size)||std::memcmp(raw,bytes,size)){fail();return;}
        for(std::size_t i=0;i<proof_.count_;++i){auto& span=proof_.spans_[i];
            if(span.address==address&&span.size==size){
                if(span.observations==UINT32_MAX||proof_.observations_==UINT64_MAX){fail();return;}
                ++span.observations;++proof_.observations_;return;
            }
        }
        if(proof_.count_==MaxSpans||size>MaxPayloadBytes-proof_.used_||proof_.observations_==UINT64_MAX){fail();return;}
        proof_.spans_[proof_.count_++]={std::uint32_t(address),std::uint32_t(size),std::uint32_t(proof_.used_),1};
        std::memcpy(proof_.bytes_.data()+proof_.used_,bytes,size);proof_.used_+=size;++proof_.observations_;
    }
    // Owner only after disconnecting the observer and quiescing all callers.
    bool complete()const noexcept{return begun_&&!failed_.load(std::memory_order_acquire)&&proof_.count_&&proof_.observations_>=proof_.count_;}
};

// Owner only after observation is quiescent. Output is caller-owned; there is
// no allocation, file IO, implicit finalization, or fallback to live memory.
inline bool encode(const Capture& capture,unsigned char* output,std::size_t capacity,std::size_t& written,Error& error)noexcept{
    using SnapshotArchive::put32;using SnapshotArchive::put64;using SnapshotArchive::checksum;
    written=0;error=Error::None;
    if(!capture.complete()){error=Error::State;return false;}
    const auto& proof=capture.proof_;
    const auto total=HeaderBytes+proof.count_*RecordBytes+proof.used_+ChecksumBytes;
    if(!output||capacity<total){error=Error::Buffer;return false;}
    std::memcpy(output,"NLCODE1\0",8);put32(output+8,Version);put32(output+12,std::uint32_t(total));
    put32(output+16,std::uint32_t(proof.count_));put32(output+20,std::uint32_t(proof.used_));put64(output+24,proof.observations_);
    put64(output+32,0);put32(output+40,0);put32(output+44,0);
    for(std::size_t i=0;i<proof.count_;++i){const auto& span=proof.spans_[i];auto* record=output+HeaderBytes+i*RecordBytes;
        put64(record,span.address);put32(record+8,span.size);put32(record+12,span.observations);
    }
    std::memcpy(output+HeaderBytes+proof.count_*RecordBytes,proof.bytes_.data(),proof.used_);
    put32(output+total-ChecksumBytes,checksum(output,total-ChecksumBytes));written=total;return true;
}

inline bool decode(const unsigned char* input,std::size_t size,const ClientImage::View& image,Proof& proof,Error& error)noexcept{
    using SnapshotArchive::u32;using SnapshotArchive::u64;using SnapshotArchive::checksum;
    proof.clear();error=Error::None;
    auto fail=[&](Error why){proof.clear();error=why;return false;};
    if(!input||size<HeaderBytes+ChecksumBytes||size>MaxEncodedBytes)return fail(Error::Bounds);
    if(std::memcmp(input,"NLCODE1\0",8))return fail(Error::Magic);
    if(u32(input+8)!=Version)return fail(Error::Version);
    const auto count=u32(input+16),payload=u32(input+20);const auto observations=u64(input+24);
    if(u32(input+12)!=size||!count||count>MaxSpans||!payload||payload>MaxPayloadBytes||
       HeaderBytes+std::size_t(count)*RecordBytes+payload+ChecksumBytes!=size)return fail(Error::Bounds);
    if(u64(input+32)||u32(input+40)||u32(input+44))return fail(Error::Record);
    if(observations<count)return fail(Error::Counts);
    if(u32(input+size-ChecksumBytes)!=checksum(input,size-ChecksumBytes))return fail(Error::Checksum);
    if(!image.valid())return fail(Error::Image);
    const auto* bytes=input+HeaderBytes+std::size_t(count)*RecordBytes;
    for(std::size_t i=0;i<count;++i){const auto* record=input+HeaderBytes+i*RecordBytes;
        const auto address=u64(record);const auto length=u32(record+8),reads=u32(record+12);
        if(address>UINT32_MAX||address>(std::numeric_limits<std::uintptr_t>::max)())return fail(Error::Address);
        if(!length||length>MaxSpanBytes||length>payload-proof.used_)return fail(Error::Record);
        if(!reads)return fail(Error::Counts);
        if(proof.find(std::uintptr_t(address),length))return fail(Error::Duplicate);
        unsigned char raw[MaxSpanBytes];
        if(image.region(std::uintptr_t(address),length)!=ClientImage::Region::Static||!image.readStatic(std::uintptr_t(address),raw,length))return fail(Error::Image);
        if(std::memcmp(raw,bytes+proof.used_,length))return fail(Error::Correspondence);
        proof.spans_[proof.count_++]={std::uint32_t(address),length,std::uint32_t(proof.used_),reads};
        std::memcpy(proof.bytes_.data()+proof.used_,bytes+proof.used_,length);proof.used_+=length;proof.observations_+=reads;
    }
    if(proof.used_!=payload||proof.observations_!=observations)return fail(Error::Counts);
    proof.valid_=true;return true;
}
} // namespace NorthlightStream::ClientCodeObservation
