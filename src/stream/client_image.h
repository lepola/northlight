#pragma once
// Borrowed, immutable raw-file bytes only. This never maps or executes an image,
// follows client pointers, or supplies initial file data as runtime game state.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace NorthlightStream::ClientImage {
enum class Error {None,Bounds,Dos,Pe,Machine,Optional,Base,Sections,Overlap};
enum class Region {Missing,Outside,Static,Runtime};
class View {
    struct Section {std::uint32_t rva=0,virtualBytes=0,offset=0,rawBytes=0,flags=0;};
    const unsigned char* file_=nullptr;
    std::uint32_t imageBytes_=0;
    unsigned count_=0;
    std::array<Section,96> sections_{};
    static std::uint16_t u16(const unsigned char* p){return std::uint16_t(p[0])|(std::uint16_t(p[1])<<8);}
    static std::uint32_t u32(const unsigned char* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
    static bool fits(std::size_t offset,std::size_t bytes,std::size_t size){return offset<=size&&bytes<=size-offset;}
    static bool overlaps(std::uint32_t a,std::uint32_t an,std::uint32_t b,std::uint32_t bn){return an&&bn&&std::uint64_t(a)<std::uint64_t(b)+bn&&std::uint64_t(b)<std::uint64_t(a)+an;}
    const Section* section(std::uintptr_t address,std::size_t bytes)const{
        if(!file_||!bytes||address<Base||bytes>UINT32_MAX)return nullptr;
        const std::uint64_t rva=std::uint64_t(address)-Base;
        if(rva>=imageBytes_||bytes>imageBytes_-rva)return nullptr;
        for(unsigned i=0;i<count_;++i){const auto& s=sections_[i];
            if(rva>=s.rva&&rva-s.rva<s.virtualBytes&&bytes<=s.virtualBytes-(rva-s.rva))return &s;
        }
        return nullptr;
    }
public:
    static constexpr std::uint32_t Base=0x400000;
    void clear(){file_=nullptr;imageBytes_=0;count_=0;}
    bool valid()const{return file_!=nullptr;}
    std::uint32_t imageBase()const{return valid()?Base:0;}
    std::uint32_t imageBytes()const{return imageBytes_;}
    // The caller owns the complete file buffer and keeps it immutable and alive
    // for every read. The stand separately binds its whole-file SHA-256.
    bool open(const unsigned char* file,std::size_t bytes,Error& error){
        clear();error=Error::None;
        auto fail=[&](Error why){clear();error=why;return false;};
        if(!file||bytes<64||bytes>UINT32_MAX)return fail(Error::Bounds);
        if(u16(file)!=0x5a4d)return fail(Error::Dos);
        const std::size_t nt=u32(file+60);
        if(nt<64||!fits(nt,24,bytes)||u32(file+nt)!=0x4550)return fail(Error::Pe);
        if(u16(file+nt+4)!=0x14c||(u16(file+nt+22)&0x2002)!=2)return fail(Error::Machine);
        const unsigned count=u16(file+nt+6),optional=u16(file+nt+20);
        const std::size_t opt=nt+24,table=opt+optional;
        if(optional<96||!fits(opt,optional,bytes)||u16(file+opt)!=0x10b)return fail(Error::Optional);
        if(u32(file+opt+28)!=Base)return fail(Error::Base);
        const auto image=u32(file+opt+56),headers=u32(file+opt+60);
        if(!count||count>sections_.size()||!fits(table,std::size_t(count)*40,bytes))return fail(Error::Sections);
        if(!image||std::uint64_t(Base)+image>UINT64_C(0x100000000)||headers>bytes||headers>image||headers<table+std::size_t(count)*40)return fail(Error::Bounds);
        for(unsigned i=0;i<count;++i){const auto* entry=file+table+std::size_t(i)*40;
            Section s{u32(entry+12),u32(entry+8),u32(entry+20),u32(entry+16),u32(entry+36)};
            if(!s.virtualBytes||s.rva<headers||std::uint64_t(s.rva)+s.virtualBytes>image||
               (s.rawBytes&&(s.offset<headers||!fits(s.offset,s.rawBytes,bytes))))return fail(Error::Sections);
            for(unsigned j=0;j<i;++j){const auto& before=sections_[j];
                if(overlaps(s.rva,s.virtualBytes,before.rva,before.virtualBytes)||overlaps(s.offset,s.rawBytes,before.offset,before.rawBytes))return fail(Error::Overlap);
            }
            sections_[i]=s;
        }
        imageBytes_=image;count_=count;file_=file;return true;
    }
    Region region(std::uintptr_t address,std::size_t bytes)const{
        if(!valid()||!bytes||bytes>UINT32_MAX||std::uint64_t(address)>UINT32_MAX||bytes>UINT64_C(0x100000000)-address)return Region::Missing;
        if(const auto* s=section(address,bytes)){
            if(!(s->flags&0x40000000u))return Region::Missing;
            return s->flags&0x80000000u?Region::Runtime:Region::Static;
        }
        const std::uint64_t end=std::uint64_t(address)+bytes;
        return end<=Base||address>=std::uint64_t(Base)+imageBytes_?Region::Outside:Region::Missing;
    }
    bool readStatic(std::uintptr_t address,void* output,std::size_t bytes)const{
        if(!output||region(address,bytes)!=Region::Static)return false;
        const auto* s=section(address,bytes);
        const auto delta=std::uint64_t(address)-Base-s->rva;
        // Neither uninitialized virtual tails nor raw alignment padding are data.
        if(delta>s->rawBytes||bytes>s->rawBytes-delta)return false;
        std::memcpy(output,file_+s->offset+std::size_t(delta),bytes);return true;
    }
};
}
