#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace NorthlightNativeFixtures {
struct Record {std::uint64_t name=0,vertices=0,shape=0,sources=0;std::uint32_t count=0,shellFirst=0,shellCount=0,reserved=0;};
struct Shell {std::uint32_t source=0;float point[3]{};std::array<float,9> triangle{};};
static_assert(sizeof(Record)==48&&sizeof(Shell)==52,"NLF1 record layout");
struct Catalog {std::vector<Record> records;std::vector<Shell> shells;};
struct ShellView {const Shell* data=nullptr;std::size_t count=0;bool empty()const{return !count;}std::size_t size()const{return count;}const Shell& operator[](std::size_t i)const{return data[i];}};
inline std::uint64_t hash(const void* data,std::size_t size,std::uint64_t value=14695981039346656037ull){
    const auto* bytes=static_cast<const unsigned char*>(data);for(std::size_t i=0;i<size;++i)value=(value^bytes[i])*1099511628211ull;return value;
}
inline std::shared_ptr<const Catalog> load(const std::string& path){
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);if(!file)return {};
    const auto size=file.tellg();if(size<24)return {};file.seekg(0);char magic[4];std::uint32_t header[5];file.read(magic,4);file.read(reinterpret_cast<char*>(header),20);
    if(!file||std::memcmp(magic,"NLF1",4)||header[0]!=2||header[1]!=sizeof(Record)||header[2]>4096||header[3]!=sizeof(Shell)||header[4]>262144||std::uint64_t(size)!=24+std::uint64_t(header[2])*sizeof(Record)+std::uint64_t(header[4])*sizeof(Shell))return {};
    auto result=std::make_shared<Catalog>();result->records.resize(header[2]);result->shells.resize(header[4]);
    if(!result->records.empty())file.read(reinterpret_cast<char*>(result->records.data()),result->records.size()*sizeof(Record));
    if(!result->shells.empty())file.read(reinterpret_cast<char*>(result->shells.data()),result->shells.size()*sizeof(Shell));if(!file)return {};
    std::size_t next=0;
    for(std::size_t i=0;i<result->records.size();++i){const auto& r=result->records[i];if(!r.sources||!r.count||r.count>4096||r.reserved||r.shellFirst!=next||r.shellCount<4||r.shellCount>4096||r.shellCount>result->shells.size()-next||(i&&result->records[i-1].name>r.name))return {};
        for(std::size_t j=i;j&&result->records[j-1].name==r.name;--j){const auto& p=result->records[j-1];if(p.vertices==r.vertices&&p.shape==r.shape)return {};}
        std::uint64_t sources=0;for(std::size_t j=next;j<next+r.shellCount;++j){const auto& s=result->shells[j];if(s.source>=64)return {};sources|=1ull<<s.source;
            for(float v:s.point)if(!std::isfinite(v)||std::fabs(v)>=100000)return {};
            for(float v:s.triangle)if(!std::isfinite(v)||std::fabs(v)>=100000)return {};}
        if(sources!=r.sources)return {};next+=r.shellCount;}
    if(next!=result->shells.size())return {};
    return result;
}
template<class Header,class Bones,class Lights> std::uint64_t shape(const Header& header,const Bones& bones,const Lights& lights){
    auto value=hash(header.data()+4,4);value=hash(header.data()+16,4,value);
    for(unsigned at:{20u,28u,44u,60u,80u,112u,120u,128u,240u,264u,288u,296u})value=hash(header.data()+at,4,value);
    value=hash(header.data()+160,52,value);
    for(const auto& bone:bones){value=hash(bone.data(),16,value);value=hash(bone.data()+76,12,value);}
    for(const auto& light:lights)value=hash(light.data(),16,value);return value;
}
// Only a cold, already bounded asset load reads these bytes. No game pointer
// escapes the reader, and a missing/stale catalog leaves native light intact.
template<class Read,class Header,class Bones,class Lights> std::uint64_t match(Read& read,const Catalog* catalog,const Header& header,const Bones& bones,const Lights& lights,std::size_t remaining){
    if(!catalog||catalog->records.empty())return 0;std::uint32_t n=0,p=0,count=0,vertices=0;
    const auto& records=catalog->records;
    std::memcpy(&n,header.data()+8,4);std::memcpy(&p,header.data()+12,4);std::memcpy(&count,header.data()+60,4);std::memcpy(&vertices,header.data()+64,4);
    if(n<2||n>256||!count||count>4096)return 0;const auto signature=shape(header,bones,lights);
    if(std::none_of(records.begin(),records.end(),[&](const auto& r){return r.count==count&&r.shape==signature;}))return 0;
    const auto prefix=p&3u;std::array<unsigned char,260> name{};if(remaining<n+prefix||!read(p-prefix,name.data(),n+prefix))return 0;
    if(name[prefix+n-1]||std::memchr(name.data()+prefix,0,n-1))return 0;
    for(unsigned i=prefix;i<prefix+n-1;++i)if(name[i]>='A'&&name[i]<='Z')name[i]+=32;
    const auto nameHash=hash(name.data()+prefix,n-1);
    if(std::none_of(records.begin(),records.end(),[&](const auto& r){return r.name==nameHash&&r.count==count&&r.shape==signature;}))return 0;
    if(remaining-n-prefix<count*48)return 0;std::vector<unsigned char> data(count*48);if(!read(vertices,data.data(),data.size()))return 0;const auto vertexHash=hash(data.data(),data.size());
    const auto found=std::find_if(records.begin(),records.end(),[&](const auto& r){return r.name==nameHash&&r.count==count&&r.shape==signature&&r.vertices==vertexHash;});
    return found==records.end()?0:unsigned(found-records.begin())+1;
}
}
