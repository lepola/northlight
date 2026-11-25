#pragma once
#include "world_tone_curves.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace NorthlightGradeLut {
namespace Pack {
// The canonical key: four curves of five 16-bit values, strength, vibrance, then three 16-bit values per colour range.
constexpr size_t HeaderBytes=104,KeyBytes=44+6*NorthlightTone::HueBandCount;
// An index record: key hash (8 bytes), the canonical key, reserved (2), table offset (8), table length (4), SHA-256 (32).
constexpr size_t RecordReservedAt=8+KeyBytes,RecordOffsetAt=RecordReservedAt+2,RecordLengthAt=RecordOffsetAt+8,RecordDigestAt=RecordLengthAt+4,
                 RecordBytes=RecordDigestAt+32;
constexpr size_t TableBytes=size_t(NorthlightTone::CubeSize)*NorthlightTone::CubeSize*NorthlightTone::CubeSize*4*sizeof(uint16_t);
constexpr uint32_t MaxEntries=4096;
constexpr uint16_t FormatVersion=1,VoxelRGBA16UnormLE=1;

class Sha256 {
    static constexpr uint32_t K[64]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t state_[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint8_t block_[64]={};size_t used_=0;uint64_t bytes_=0;
    static uint32_t rotate(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
    void transform(const uint8_t* block){
        uint32_t w[64];
        for(unsigned i=0;i<16;++i)w[i]=(uint32_t(block[i*4])<<24)|(uint32_t(block[i*4+1])<<16)|(uint32_t(block[i*4+2])<<8)|block[i*4+3];
        for(unsigned i=16;i<64;++i){const uint32_t a=w[i-15],b=w[i-2];const uint32_t s0=rotate(a,7)^rotate(a,18)^(a>>3),s1=rotate(b,17)^rotate(b,19)^(b>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
        uint32_t a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for(unsigned i=0;i<64;++i){const uint32_t s1=rotate(e,6)^rotate(e,11)^rotate(e,25),ch=(e&f)^(~e&g),t1=h+s1+ch+K[i]+w[i];
            const uint32_t s0=rotate(a,2)^rotate(a,13)^rotate(a,22),maj=(a&b)^(a&c)^(b&c),t2=s0+maj;
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
public:
    void update(const uint8_t* data,size_t size){
        bytes_+=size;
        while(size){const size_t take=std::min(size,sizeof block_-used_);std::memcpy(block_+used_,data,take);used_+=take;data+=take;size-=take;
            if(used_==sizeof block_){transform(block_);used_=0;}}
    }
    std::array<uint8_t,32> finish(){
        const uint64_t bitCount=bytes_*8;block_[used_++]=0x80;
        if(used_>56){std::fill(block_+used_,block_+64,0);transform(block_);used_=0;}
        std::fill(block_+used_,block_+56,0);
        for(unsigned i=0;i<8;++i)block_[63-i]=uint8_t(bitCount>>(i*8));
        transform(block_);std::array<uint8_t,32> out{};
        for(unsigned i=0;i<8;++i)for(unsigned b=0;b<4;++b)out[i*4+b]=uint8_t(state_[i]>>(24-b*8));
        return out;
    }
    static std::array<uint8_t,32> digest(const uint8_t* data,size_t size){Sha256 h;h.update(data,size);return h.finish();}
};

inline uint16_t read16(const uint8_t* p){return uint16_t(p[0])|uint16_t(uint16_t(p[1])<<8);}
inline uint32_t read32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline uint64_t read64(const uint8_t* p){return uint64_t(read32(p))|(uint64_t(read32(p+4))<<32);}
inline void write16(uint8_t* p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
inline void write32(uint8_t* p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(i*8));}
inline void write64(uint8_t* p,uint64_t v){write32(p,uint32_t(v));write32(p+4,uint32_t(v>>32));}

inline std::array<uint8_t,KeyBytes> encodeKey(const NorthlightTone::BakeKey& key){
    std::array<uint8_t,KeyBytes> out{};size_t p=0;
    auto word=[&](uint16_t v){write16(out.data()+p,v);p+=2;};
    for(const auto& channel:key.curves)for(uint16_t v:channel)word(v);
    word(key.strength);word(key.vibrance);
    for(int16_t v:key.hueShift)word(uint16_t(v));
    for(int16_t v:key.saturation)word(uint16_t(v));
    for(int16_t v:key.lightness)word(uint16_t(v));
    return out;
}
inline NorthlightTone::BakeKey decodeKey(const uint8_t* bytes){
    NorthlightTone::BakeKey key;size_t p=0;
    auto word=[&](){const uint16_t v=read16(bytes+p);p+=2;return v;};
    for(auto& channel:key.curves)for(auto& v:channel)v=word();
    key.strength=word();key.vibrance=word();
    for(auto& v:key.hueShift)v=int16_t(word());
    for(auto& v:key.saturation)v=int16_t(word());
    for(auto& v:key.lightness)v=int16_t(word());
    return key;
}

struct Record {
    NorthlightTone::BakeKey key{};
    std::array<uint8_t,KeyBytes> canonical{};
    std::array<uint8_t,32> digest{};
    uint64_t hash=0,offset=0;
    uint32_t length=0;
    bool rejected=false;
};
enum class Status : uint8_t {Untried,Missing,Ready,Invalid};
struct Index {
    std::filesystem::path path;
    std::vector<Record> records;
    std::array<uint8_t,32> sourceSha{};
    Status status=Status::Untried;
    uint64_t bytes=0;
    std::string error;

    bool load(const std::filesystem::path& file){
        path=file;records.clear();sourceSha={};error.clear();status=Status::Invalid;
        std::error_code ec;const uint64_t size=std::filesystem::file_size(path,ec);
        if(ec){status=ec==std::errc::no_such_file_or_directory?Status::Missing:Status::Invalid;error=ec.message();return false;}
        if(size<HeaderBytes){error="truncated header";return false;}
        std::ifstream in(path,std::ios::binary);if(!in){error="open failed";return false;}
        std::array<uint8_t,HeaderBytes> header{};in.read(reinterpret_cast<char*>(header.data()),header.size());
        static constexpr uint8_t magic[8]={'N','L','L','U','T','P','K',0};
        if(!in||std::memcmp(header.data(),magic,sizeof magic)){error="bad magic/header";return false;}
        const uint16_t format=read16(header.data()+8),schema=read16(header.data()+10),cube=read16(header.data()+12),voxel=read16(header.data()+14);
        const uint16_t keyBytes=read16(header.data()+16),recordBytes=read16(header.data()+18);
        const uint32_t count=read32(header.data()+20);const uint64_t indexOffset=read64(header.data()+24),tablesOffset=read64(header.data()+32);
        if(format!=FormatVersion||schema!=NorthlightTone::BakeMathSchemaVersion||cube!=NorthlightTone::CubeSize||
           voxel!=VoxelRGBA16UnormLE||keyBytes!=KeyBytes||recordBytes!=RecordBytes||count>MaxEntries||indexOffset!=HeaderBytes){error="unsupported format/schema or entry count";return false;}
        const uint64_t expectedTables=indexOffset+uint64_t(count)*RecordBytes;
        if(tablesOffset!=expectedTables||tablesOffset>size||uint64_t(count)>((std::numeric_limits<uint64_t>::max()-tablesOffset)/TableBytes)||
           size!=tablesOffset+uint64_t(count)*TableBytes){error="invalid index/table extents";return false;}
        std::copy_n(header.data()+40,32,sourceSha.begin());
        const size_t indexSize=size_t(count)*RecordBytes;std::vector<uint8_t> index(indexSize);
        if(indexSize){in.read(reinterpret_cast<char*>(index.data()),std::streamsize(indexSize));if(!in){error="truncated index";return false;}}
        const auto digest=Sha256::digest(index.data(),index.size());
        if(!std::equal(digest.begin(),digest.end(),header.begin()+72)){error="index digest mismatch";return false;}
        try{records.reserve(count);}catch(...){error="index allocation failed";return false;}
        for(uint32_t i=0;i<count;++i){
            const uint8_t* row=index.data()+size_t(i)*RecordBytes;Record record;
            record.hash=read64(row);std::copy_n(row+8,KeyBytes,record.canonical.begin());record.key=decodeKey(record.canonical.data());
            const uint16_t reserved=read16(row+RecordReservedAt);record.offset=read64(row+RecordOffsetAt);record.length=read32(row+RecordLengthAt);
            std::copy_n(row+RecordDigestAt,32,record.digest.begin());
            if(reserved||!record.key.strength||NorthlightTone::stableHash(record.key)!=record.hash||
               record.offset!=tablesOffset+uint64_t(i)*TableBytes||record.length!=TableBytes){error="invalid index record";records.clear();return false;}
            if(!records.empty()){
                const auto& prior=records.back();
                if(record.hash<prior.hash||(record.hash==prior.hash&&!(prior.canonical<record.canonical))){error="unsorted or duplicate key index";records.clear();return false;}
            }
            records.push_back(std::move(record));
        }
        bytes=size;status=Status::Ready;return true;
    }
    Record* find(const NorthlightTone::BakeKey& key){
        if(status!=Status::Ready||!key.strength)return nullptr;
        const auto canonical=encodeKey(key);const uint64_t hash=NorthlightTone::stableHash(key);
        auto it=std::lower_bound(records.begin(),records.end(),std::pair<uint64_t,const std::array<uint8_t,KeyBytes>*>(hash,&canonical),
            [](const Record& row,const auto& target){return row.hash<target.first||(row.hash==target.first&&row.canonical<*target.second);});
        return it!=records.end()&&it->hash==hash&&it->canonical==canonical&&!it->rejected?&*it:nullptr;
    }
    bool decode(Record& record,std::vector<uint16_t>& words,std::string& reason){
        reason.clear();if(status!=Status::Ready||record.rejected){reason="entry unavailable";return false;}
        std::ifstream in(path,std::ios::binary);if(!in){reason="reopen failed";record.rejected=true;return false;}
        in.seekg(std::streamoff(record.offset));if(!in){reason="seek failed";record.rejected=true;return false;}
        std::vector<uint8_t> bytesIn;
        try{bytesIn.resize(TableBytes);}catch(...){reason="table allocation failed";return false;}
        in.read(reinterpret_cast<char*>(bytesIn.data()),std::streamsize(bytesIn.size()));
        if(!in){reason="truncated table";record.rejected=true;return false;}
        const auto digest=Sha256::digest(bytesIn.data(),bytesIn.size());
        if(digest!=record.digest){reason="table digest mismatch";record.rejected=true;return false;}
        try{words.resize(TableBytes/sizeof(uint16_t));}catch(...){reason="decoded table allocation failed";return false;}
        for(size_t i=0;i<words.size();++i)words[i]=read16(bytesIn.data()+i*2);
        for(size_t i=3;i<words.size();i+=4)if(words[i]!=65535){words.clear();reason="invalid alpha channel";record.rejected=true;return false;}
        return true;
    }
};
inline const char* statusName(Status status){
    switch(status){case Status::Untried:return "untried";case Status::Missing:return "missing";case Status::Ready:return "ready";case Status::Invalid:return "invalid";}
    return "invalid";
}
} // namespace Pack
} // namespace NorthlightGradeLut
