#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

namespace NorthlightEmission {
inline constexpr unsigned Glow=1,Lightning=2,SkyLightning=4;
struct Entry {
    std::uint64_t hash;std::uint32_t width,height,format,kind;
    auto key()const{return std::make_tuple(hash,width,height,format);}
};
static_assert(sizeof(Entry)==24,"NLE1 record");
class Catalog {
    std::vector<Entry> entries_;
public:
    unsigned size()const{return unsigned(entries_.size());}
    bool read(const std::filesystem::path& path){
        entries_.clear();std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return false;
        const auto size=file.tellg();if(size<16||size>16+65536*24)return false;file.seekg(0);
        char magic[4];std::uint32_t h[3];file.read(magic,4);file.read(reinterpret_cast<char*>(h),12);
        if(!file||std::memcmp(magic,"NLE1",4)||h[0]!=1||h[1]!=24||h[2]>65536||static_cast<std::uint64_t>(size)!=16+std::uint64_t(h[2])*24)return false;
        std::vector<Entry> next(h[2]);file.read(reinterpret_cast<char*>(next.data()),next.size()*24);
        if(!file)return false;
        for(unsigned i=0;i<next.size();++i){const auto& e=next[i];
            if(!e.width||!e.height||e.width>32||e.height>32||!(e.kind&Glow)||(e.kind&~7u)||
               (e.format!=0&&e.format!=21&&e.format!=0x31545844&&e.format!=0x33545844&&e.format!=0x35545844)||
               (i&&!(next[i-1].key()<e.key())))return false;}
        entries_.swap(next);return true;
    }
    unsigned identify(std::uint64_t hash,unsigned w,unsigned h,unsigned format)const{
        const auto key=std::make_tuple(hash,w,h,format);
        const auto at=std::lower_bound(entries_.begin(),entries_.end(),key,[](const Entry& e,const auto& k){return e.key()<k;});
        return at!=entries_.end()&&at->key()==key?at->kind:0;
    }
};
}
