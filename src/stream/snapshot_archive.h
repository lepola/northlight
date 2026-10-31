#pragma once
// Data-only transport for recorded self reads. Address fields are lookup keys,
// never pointers to dereference in the importing process. No file I/O or live
// capture is enabled here; the existing runtime fallback remains unchanged.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "snapshot_store.h"

namespace NorthlightStream::SnapshotArchive {
constexpr std::size_t HeaderBytes=40,RecordBytes=16,ChecksumBytes=4;
constexpr std::size_t MaxBytes=HeaderBytes+GameSnapshot::MaxCopies*RecordBytes+GameSnapshot::ArenaBytes+ChecksumBytes;
constexpr unsigned Version=1;
enum class Error {None,Unusable,Buffer,Magic,Version,Bounds,Checksum,Record,Counts,Address};
inline const char* name(Error error){
    switch(error){case Error::None:return "none";case Error::Unusable:return "unusable";case Error::Buffer:return "buffer";
    case Error::Magic:return "magic";case Error::Version:return "version";case Error::Bounds:return "bounds";
    case Error::Checksum:return "checksum";case Error::Record:return "record";case Error::Counts:return "counts";default:return "address";}
}
inline std::uint32_t u32(const unsigned char* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
inline std::uint64_t u64(const unsigned char* p){return u32(p)|(std::uint64_t(u32(p+4))<<32);}
inline void put32(unsigned char* p,std::uint32_t x){for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(x>>(8*i));}
inline void put64(unsigned char* p,std::uint64_t x){put32(p,std::uint32_t(x));put32(p+4,std::uint32_t(x>>32));}
inline std::uint32_t checksum(const unsigned char* p,std::size_t size){
    std::uint32_t value=0xffffffffu;
    for(std::size_t i=0;i<size;++i){value^=p[i];for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(0xedb88320u&(0u-(value&1u)));}
    return ~value;
}
inline bool encode(const GameSnapshot& snapshot,unsigned char* output,std::size_t capacity,std::size_t& written,Error& error){
    written=0;error=Error::None;
    if(!snapshot.usable()||snapshot.triggerKind>3){error=Error::Unusable;return false;}
    const auto size=HeaderBytes+snapshot.copies()*RecordBytes+snapshot.bytes()+ChecksumBytes;
    if(!output||size>capacity||size>MaxBytes){error=Error::Buffer;return false;}
    const unsigned char magic[8]={'N','L','S','N','A','P','1',0};std::memcpy(output,magic,sizeof magic);
    put32(output+8,Version);put32(output+12,std::uint32_t(size));put32(output+16,snapshot.triggerKind);
    put32(output+20,std::uint32_t(snapshot.keys()));put32(output+24,std::uint32_t(snapshot.copies()));put32(output+28,std::uint32_t(snapshot.bytes()));put64(output+32,snapshot.triggerDraw);
    std::size_t offset=HeaderBytes;
    const bool ok=snapshot.visitRecorded([&](std::uintptr_t address,std::uint32_t count,bool success,const void* data){
        const auto payload=success?count:0;
        if(offset>size-ChecksumBytes||RecordBytes+payload>size-ChecksumBytes-offset)return false;
        put64(output+offset,std::uint64_t(address));put32(output+offset+8,count);put32(output+offset+12,success?1u:0u);offset+=RecordBytes;
        if(payload){std::memcpy(output+offset,data,payload);offset+=payload;}return true;});
    if(!ok||offset!=size-ChecksumBytes){error=Error::Counts;return false;}
    put32(output+offset,checksum(output,offset));written=size;return true;
}
inline bool decode(const unsigned char* input,std::size_t size,GameSnapshot& snapshot,Error& error){
    snapshot.clear();error=Error::None;
    auto fail=[&](Error why){snapshot.clear();error=why;return false;};
    if(!input||size<HeaderBytes+ChecksumBytes||size>MaxBytes)return fail(Error::Bounds);
    const unsigned char magic[8]={'N','L','S','N','A','P','1',0};if(std::memcmp(input,magic,sizeof magic))return fail(Error::Magic);
    if(u32(input+8)!=Version)return fail(Error::Version);
    const auto trigger=u32(input+16),keys=u32(input+20),copies=u32(input+24),payload=u32(input+28);
    if(u32(input+12)!=size||trigger>3||!keys||keys>GameSnapshot::MaxKeys||!copies||copies>GameSnapshot::MaxCopies||keys>copies||payload>GameSnapshot::ArenaBytes||HeaderBytes+std::size_t(copies)*RecordBytes+payload+ChecksumBytes!=size)return fail(Error::Bounds);
    if(u32(input+size-ChecksumBytes)!=checksum(input,size-ChecksumBytes))return fail(Error::Checksum);
    std::size_t offset=HeaderBytes;
    for(std::uint32_t i=0;i<copies;++i){
        if(offset>size-ChecksumBytes||RecordBytes>size-ChecksumBytes-offset)return fail(Error::Record);
        const auto address=u64(input+offset);const auto count=u32(input+offset+8),flags=u32(input+offset+12);offset+=RecordBytes;
        if(address>(std::numeric_limits<std::uintptr_t>::max)())return fail(Error::Address);
        if(flags>1||count>GameSnapshot::ArenaBytes||(flags&&count>size-ChecksumBytes-offset))return fail(Error::Record);
        if(!snapshot.record(std::uintptr_t(address),input+offset,count,flags!=0))return fail(Error::Counts);
        if(flags)offset+=count;
    }
    if(offset!=size-ChecksumBytes||snapshot.keys()!=keys||snapshot.bytes()!=payload||snapshot.copies()!=copies||!snapshot.usable())return fail(Error::Counts);
    snapshot.triggerKind=trigger;snapshot.triggerDraw=u64(input+32);snapshot.mode=GameSnapshot::Mode::Playing;snapshot.rewind();return true;
}

class Reader {
    GameSnapshot& snapshot_;
public:
    std::uint64_t reads=0,missing=0,recordedFailures=0;
    explicit Reader(GameSnapshot& snapshot):snapshot_(snapshot){snapshot_.rewind();}
    bool operator()(std::uintptr_t address,void* output,std::size_t size){
        ++reads;if(!output&&size){++missing;return false;}
        switch(snapshot_.serveRecorded(address,output,size)){
        case GameSnapshot::Serve::Ok:return true;
        case GameSnapshot::Serve::Failed:++recordedFailures;return false;
        default:++missing;return false;}
    }
};
}
