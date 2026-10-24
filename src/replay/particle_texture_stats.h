#pragma once
#include "actor_texture.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NorthlightParticleTextures {
inline std::atomic<bool> enabled{false}; // Set by the device's EquipmentLights quality setting.
// Replay-thread only. Views borrow a complete CPU-uploaded mip until the next
// cache mutation; no driver lock, readback, COM reference or game-side shadow.
struct View {
    const std::uint8_t* rgba=nullptr;
    unsigned width=0,height=0,mip=0,format=0;
    std::uint64_t lifetime=0,revision=0,rgbaHash=0;
    bool sample(float u,float v,bool srgb,unsigned addressU,unsigned addressV,float out[4],bool linear=true)const{
        if(!rgba||!width||!height||!out||!std::isfinite(u)||!std::isfinite(v)||addressU<1||addressU>3||addressV<1||addressV>3)return false;
        auto coord=[](float f,unsigned mode){
            if(mode==3)return std::max(0.f,std::min(1.f,f));
            if(mode==2){f=std::fmod(f,2.f);if(f<0)f+=2;return f<=1?f:2-f;}
            return f-std::floor(f);
        };
        const float x=coord(u,addressU)*width-.5f,y=coord(v,addressV)*height-.5f;
        const int ix=int(std::floor(x)),iy=int(std::floor(y));
        auto texel=[](int i,unsigned n,unsigned mode){
            if(mode==3)return unsigned(std::max(0,std::min(int(n)-1,i)));
            if(mode==2){const int period=int(n)*2;i%=period;if(i<0)i+=period;return unsigned(i<int(n)?i:period-1-i);}
            i%=int(n);if(i<0)i+=int(n);return unsigned(i);
        };
        static const std::array<float,256> linearRGB=[](){std::array<float,256> a{};for(unsigned i=0;i<256;++i){const float s=float(i)/255;a[i]=s<=.04045f?s/12.92f:std::pow((s+.055f)/1.055f,2.4f);}return a;}();
        for(unsigned c=0;c<4;++c){
            auto read=[&](int a,int b){const auto value=rgba[(std::size_t(texel(b,height,addressV))*width+texel(a,width,addressU))*4+c];return srgb&&c<3?linearRGB[value]:float(value)/255;};
            if(!linear)out[c]=read(int(std::floor(x+.5f)),int(std::floor(y+.5f)));
            else{const float fx=x-ix,fy=y-iy;out[c]=(read(ix,iy)*(1-fx)+read(ix+1,iy)*fx)*(1-fy)+(read(ix,iy+1)*(1-fx)+read(ix+1,iy+1)*fx)*fy;}
        }
        return true;
    }
};
// Diagnostics: what the cache last saw of a texture that a particle draw then missed.
enum Seen : std::uint8_t {Never,Held,Large,Evicted,Invalidated,Partial,Failed,Unsupported,Released,SeenKinds};
struct Counters {
    std::array<std::uint64_t,SeenKinds> missWhy{};
    std::uint64_t uploads=0,retained=0,decoded=0,hits=0,misses=0,partial=0,failed=0,unsupported=0,large=0,invalidations=0,evictions=0,releases=0;
};
class Cache {
public:
    // The game uploads a MANAGED texture once, so an evicted particle texture never
    // comes back: one play session evicted 2,049 with 512 entries and every texture
    // miss (88,612) was such an eviction. A 32-texel mip is enough for a light's
    // colour (0.5-4 KiB uploaded), so every small mip the game loads fits.
    static constexpr unsigned MaxDimension=32,MaxEntries=16384;
    static constexpr std::size_t PixelBudget=8u<<20;
private:
    struct Entry {
        const void* raw=nullptr;
        const void* owner=nullptr;
        std::uint64_t lifetime=0,revision=0,used=0,rgbaHash=0;
        unsigned width=0,height=0,mip=0,format=0;
        std::vector<std::uint8_t> pixels;
        // The upload's own bytes until a particle draw first asks for them:
        // most small mips are never sampled, so decoding waits for lookup().
        std::vector<std::uint8_t> source;std::size_t pitch=0;
        bool held()const{return !pixels.empty()||!source.empty();}
    };
    std::unordered_map<const void*,Entry> entries_;
    // Diagnostics only (PTEX misses=...): the last event per texture key, bounded.
    std::unordered_map<const void*,Seen> history_;Seen lastMiss_=Never;
    void note(const void* raw,Seen seen){if(!raw)return;if(history_.size()>=65536)history_.clear();history_[raw]=seen;}
    void evicted(const Entry& e){note(e.raw,Evicted);}
    std::size_t bytes_=0;
    std::uint64_t serial_=0,clock_=0;
    Entry* find(const void* raw){if(!raw)return nullptr;const auto it=entries_.find(raw);return it!=entries_.end()?&it->second:nullptr;}
    void drop(Entry& e){bytes_-=e.pixels.size()+e.source.size();std::vector<std::uint8_t>().swap(e.pixels);std::vector<std::uint8_t>().swap(e.source);e.width=e.height=0;}
    // Over a limit: evict the least recently used down to 7/8 of it, so a load burst scans the entries once, not per upload.
    void evict(std::size_t incoming,const Entry* keep){
        if(bytes_+incoming<=PixelBudget&&entries_.size()<=MaxEntries)return;
        std::vector<std::pair<std::uint64_t,const void*>> order;order.reserve(entries_.size());
        for(const auto& [raw,e]:entries_)if(&e!=keep)order.emplace_back(e.used,raw);
        std::sort(order.begin(),order.end());
        for(const auto& [used,raw]:order){
            if(bytes_+incoming<=PixelBudget-PixelBudget/8&&entries_.size()<=MaxEntries-MaxEntries/8)break;
            auto it=entries_.find(raw);evicted(it->second);drop(it->second);entries_.erase(it);++counts.evictions;
        }
    }
    Entry& slot(const void* raw,const void* owner){
        if(auto* e=find(raw)){if(e->owner==owner)return *e;drop(*e);*e={};}
        Entry& e=entries_[raw];e={};e.raw=raw;e.owner=owner;e.lifetime=++serial_;e.used=++clock_;return e;
    }
    static bool format(unsigned d3d,NorthlightActorTexture::Format& out){
        using F=NorthlightActorTexture::Format;
        switch(d3d){case 21:out=F::BGRA8;break;case 22:out=F::BGRX8;break;case 23:out=F::RGB565;break;
        case 24:out=F::XRGB1555;break;case 25:out=F::ARGB1555;break;case 26:out=F::ARGB4444;break;
        case 0x31545844:out=F::BC1;break;case 0x33545844:out=F::BC2;break;case 0x35545844:out=F::BC3;break;default:return false;}return true;
    }
public:
    Counters counts;
    Seen lastMiss()const{return lastMiss_;}
    std::size_t bytes()const{return bytes_;}
    unsigned size()const{unsigned n=0;for(const auto& [raw,e]:entries_)n+=e.held();return n;}
    void clear(){entries_.clear();bytes_=0;history_.clear();}
    void erase(const void* raw,const void* owner=nullptr){if(auto* e=find(raw))if(!owner||e->owner==owner){drop(*e);entries_.erase(raw);++counts.releases;note(raw,Released);}}
    void invalidate(const void* raw){note(raw,Invalidated);if(auto* e=find(raw)){drop(*e);++e->revision;++counts.invalidations;}}
    // Other full mips do not change this mip. Partial/failed writes invalidate
    // the texture conservatively. Only a later complete CPU upload can restore it.
    void upload(const void* raw,const void* owner,unsigned width,unsigned height,unsigned mip,unsigned d3dFormat,
                const void* source,std::size_t sourceBytes,std::size_t pitch,bool whole,bool success)noexcept{
        if(!enabled.load(std::memory_order_relaxed)){invalidate(raw);return;}
        ++counts.uploads;
        try{
        if(!success){++counts.failed;invalidate(raw);note(raw,Failed);return;}
        if(!whole){++counts.partial;invalidate(raw);note(raw,Partial);return;}
        if(auto* old=find(raw))if(old->owner!=owner)erase(raw);
        NorthlightActorTexture::Format decodedFormat;
        if(!raw||!owner||!format(d3dFormat,decodedFormat)){++counts.unsupported;invalidate(raw);note(raw,Unsupported);return;}
        if(!width||!height||width>MaxDimension||height>MaxDimension){++counts.large;if(!find(raw))note(raw,Large);return;}
        auto* previous=find(raw);
        if(previous&&previous->owner==owner&&previous->held()&&mip>previous->mip){++previous->revision;return;}
        if(!source||!sourceBytes||sourceBytes>std::size_t(MaxDimension)*MaxDimension*16){++counts.failed;invalidate(raw);note(raw,Failed);return;}
        Entry& e=slot(raw,owner);drop(e);evict(sourceBytes,&e);
        e.source.assign(static_cast<const std::uint8_t*>(source),static_cast<const std::uint8_t*>(source)+sourceBytes);e.pitch=pitch;
        e.width=width;e.height=height;e.mip=mip;e.format=d3dFormat;++e.revision;e.used=++clock_;bytes_+=e.source.size();++counts.retained;note(raw,Held);
        }catch(...){++counts.failed;invalidate(raw);}
    }
    bool lookup(const void* raw,View& out){
        out={};if(!enabled.load(std::memory_order_relaxed))return false;auto* e=find(raw);if(!e||!e->held()){++counts.misses;
            const auto seen=history_.find(raw);lastMiss_=seen!=history_.end()?seen->second:Never;++counts.missWhy[lastMiss_];
            return false;}
        if(e->pixels.empty()){
            NorthlightActorTexture::Format decodedFormat;std::vector<std::uint8_t> decoded;
            try{if(!format(e->format,decodedFormat)||!NorthlightActorTexture::decode(e->source.data(),e->source.size(),e->width,e->height,e->pitch,decodedFormat,decoded))decoded.clear();}
            catch(...){decoded.clear();}
            if(decoded.empty()){++counts.failed;++counts.misses;invalidate(raw);note(raw,Failed);lastMiss_=Failed;++counts.missWhy[Failed];return false;}
            bytes_-=e->source.size();std::vector<std::uint8_t>().swap(e->source);
            evict(decoded.size(),e);
            e->rgbaHash=14695981039346656037ull;for(auto byte:decoded){e->rgbaHash^=byte;e->rgbaHash*=1099511628211ull;}
            e->pixels=std::move(decoded);bytes_+=e->pixels.size();++counts.decoded;
        }
        e->used=++clock_;out={e->pixels.data(),e->width,e->height,e->mip,e->format,e->lifetime,e->revision,e->rgbaHash};++counts.hits;return true;
    }
};
static_assert(Cache::PixelBudget+Cache::MaxDimension*Cache::MaxDimension*4<(9u<<20),"particle texture bytes plus decode scratch stay below 9 MiB");
inline Cache& cache(){static thread_local Cache value;return value;}
} // namespace NorthlightParticleTextures

// Equipment atlases retain their own bounded mip and exact upload revisions.
namespace NorthlightMaterialTextures {
inline std::atomic<bool> enabled{true};
using View=NorthlightParticleTextures::View;
using Counters=NorthlightParticleTextures::Counters;
class Cache {
public:
    static constexpr unsigned MaxDimension=128,DecodeDimension=1024,MaxEntries=128;
    static constexpr std::size_t PixelBudget=4u<<20;
private:
    struct Entry {
        const void* raw=nullptr;
        const void* owner=nullptr;
        std::uint64_t lifetime=0,revision=0,used=0,rgbaHash=0;
        unsigned width=0,height=0,mip=0,format=0;
        std::vector<std::uint8_t> pixels;
    };
    std::array<Entry,MaxEntries> entries_{};
    std::size_t bytes_=0;
    std::uint64_t serial_=0,clock_=0;
    Entry* find(const void* raw){if(raw)for(auto& e:entries_)if(e.raw==raw)return &e;return nullptr;}
    void drop(Entry& e){bytes_-=e.pixels.size();std::vector<std::uint8_t>().swap(e.pixels);e.width=e.height=0;}
    Entry* oldest(const Entry* except=nullptr){Entry* result=nullptr;for(auto& e:entries_)if(e.raw && &e!=except&&(!result||e.used<result->used))result=&e;return result;}
    Entry& slot(const void* raw,const void* owner){
        if(auto* e=find(raw)){if(e->owner==owner)return *e;drop(*e);*e={};}
        Entry* selected=nullptr;for(auto& e:entries_)if(!e.raw){selected=&e;break;}
        if(!selected){selected=oldest();drop(*selected);++counts.evictions;}
        *selected={};selected->raw=raw;selected->owner=owner;selected->lifetime=++serial_;selected->used=++clock_;return *selected;
    }
    static bool format(unsigned d3d,NorthlightActorTexture::Format& out){
        using F=NorthlightActorTexture::Format;
        switch(d3d){case 21:out=F::BGRA8;break;case 22:out=F::BGRX8;break;case 23:out=F::RGB565;break;
        case 24:out=F::XRGB1555;break;case 25:out=F::ARGB1555;break;case 26:out=F::ARGB4444;break;
        case 0x31545844:out=F::BC1;break;case 0x33545844:out=F::BC2;break;case 0x35545844:out=F::BC3;break;default:return false;}return true;
    }
    static bool decodeImage(const void* source,std::size_t bytes,unsigned width,unsigned height,std::size_t pitch,NorthlightActorTexture::Format format,std::vector<std::uint8_t>& output){
        if(width<=128&&height<=128)return NorthlightActorTexture::decode(source,bytes,width,height,pitch,format,output);
        if(!source)return false;output.resize(size_t(width)*height*4);std::vector<std::uint8_t> tile,packed;
        for(unsigned y=0;y<height;y+=128)for(unsigned x=0;x<width;x+=128){const unsigned w=std::min(128u,width-x),h=std::min(128u,height-y);const auto offset=NorthlightActorTexture::rowCount(y,format)*pitch+NorthlightActorTexture::rowBytes(x,format);
            const auto rowBytes=NorthlightActorTexture::rowBytes(w,format);const auto rows=NorthlightActorTexture::rowCount(h,format);packed.resize(rowBytes*rows);
            for(unsigned r=0;r<rows;++r){const auto start=offset+size_t(r)*pitch;if(start>bytes||rowBytes>bytes-start)return false;std::copy_n(static_cast<const unsigned char*>(source)+start,rowBytes,packed.data()+size_t(r)*rowBytes);}
            if(!NorthlightActorTexture::decode(packed.data(),packed.size(),w,h,rowBytes,format,tile))return false;
            for(unsigned r=0;r<h;++r)std::copy_n(tile.data()+size_t(r)*w*4,w*4,output.data()+(size_t(y+r)*width+x)*4);}
        return true;
    }
public:
    Counters counts;
    std::size_t bytes()const{return bytes_;}
    unsigned size()const{unsigned n=0;for(const auto& e:entries_)n+=!e.pixels.empty();return n;}
    void clear(){for(auto& e:entries_){drop(e);e={};}}
    void erase(const void* raw,const void* owner=nullptr){if(auto* e=find(raw))if(!owner||e->owner==owner){drop(*e);*e={};++counts.releases;}}
    void invalidate(const void* raw){if(auto* e=find(raw)){drop(*e);++e->revision;++counts.invalidations;}}
    std::uint64_t version(const void* raw){if(!enabled.load(std::memory_order_relaxed))return 0;auto* e=find(raw);if(e)e->used=++clock_;return e?e->lifetime*UINT64_C(0x9e3779b97f4a7c15)^e->revision:0;}
    bool copy(const void* source,const void* destination,const void* owner)noexcept{try{
        if(source==destination)return false;auto* original=find(source);if(!original||original->pixels.empty()){invalidate(destination);return false;}
        // Copy before slot/eviction can invalidate the borrowed source.
        auto pixels=original->pixels;const auto w=original->width,h=original->height,mip=original->mip,fmt=original->format;const auto hash=original->rgbaHash;
        if(auto* same=find(destination))if(same->width==w&&same->height==h&&same->mip==mip&&same->format==fmt&&same->rgbaHash==hash&&same->pixels==pixels){same->used=++clock_;return true;}
        Entry& e=slot(destination,owner);drop(e);while(bytes_+pixels.size()>PixelBudget){auto* victim=oldest(&e);if(!victim)break;drop(*victim);*victim={};++counts.evictions;}
        e.pixels=std::move(pixels);e.width=w;e.height=h;e.mip=mip;e.format=fmt;e.rgbaHash=hash;++e.revision;e.used=++clock_;bytes_+=e.pixels.size();return true;
    }catch(...){invalidate(destination);++counts.failed;return false;}}
    bool updateRect(const void* raw,const void* owner,unsigned width,unsigned height,unsigned mip,unsigned d3dFormat,
            const void* source,std::size_t sourceBytes,std::size_t pitch,unsigned left,unsigned top,unsigned right,unsigned bottom,bool success)noexcept{try{
        auto fail=[&](){invalidate(raw);return false;};auto* e=find(raw);NorthlightActorTexture::Format decodedFormat;
        if(!success||!source||!e||e->pixels.empty()||(e->owner&&e->owner!=owner)||e->mip!=mip||!width||!height||width>DecodeDimension||height>DecodeDimension||width%e->width||height%e->height||!format(d3dFormat,decodedFormat)||left>=right||top>=bottom||right>width||bottom>height)return fail();
        const unsigned sx=width/e->width,sy=height/e->height;
        // Atlas rectangles aligned to the retained mip are exact. An unaligned
        // edit invalidates it instead of inventing the missing old subpixels.
        if(left%sx||right%sx||top%sy||bottom%sy)return fail();std::vector<std::uint8_t> patch;
        if(!decodeImage(source,sourceBytes,right-left,bottom-top,pitch,decodedFormat,patch))return fail();
        bool changed=false;for(unsigned y=top/sy;y<bottom/sy;++y)for(unsigned x=left/sx;x<right/sx;++x){unsigned sum[4]={};for(unsigned j=0;j<sy;++j)for(unsigned i=0;i<sx;++i)for(unsigned c=0;c<4;++c)sum[c]+=patch[((size_t(y*sy-top+j))*(right-left)+x*sx-left+i)*4+c];for(unsigned c=0;c<4;++c){auto& old=e->pixels[(size_t(y)*e->width+x)*4+c];auto value=std::uint8_t((sum[c]+sx*sy/2)/(sx*sy));changed|=old!=value;old=value;}}
        e->owner=owner;if(!changed){e->used=++clock_;return true;}e->rgbaHash=14695981039346656037ull;for(auto b:e->pixels)e->rgbaHash=(e->rgbaHash^b)*1099511628211ull;++e->revision;e->used=++clock_;return true;
    }catch(...){invalidate(raw);++counts.failed;return false;}}
    // Other full mips do not change this mip. Partial/failed writes invalidate
    // the texture conservatively. Only a later complete CPU upload can restore it.
    void upload(const void* raw,const void* owner,unsigned width,unsigned height,unsigned mip,unsigned d3dFormat,
                const void* source,std::size_t sourceBytes,std::size_t pitch,bool whole,bool success)noexcept{
        if(!enabled.load(std::memory_order_relaxed)){invalidate(raw);return;}
        ++counts.uploads;
        try{
        if(!success){++counts.failed;invalidate(raw);return;}
        if(!whole){++counts.partial;invalidate(raw);return;}
        if(auto* old=find(raw))if(old->owner!=owner)erase(raw);
        NorthlightActorTexture::Format decodedFormat;
        if(!raw||!owner||!format(d3dFormat,decodedFormat)){++counts.unsupported;invalidate(raw);return;}
        if(!width||!height||width>DecodeDimension||height>DecodeDimension){++counts.large;return;}
        auto* previous=find(raw);
        if(previous&&previous->owner==owner&&!previous->pixels.empty()&&mip>previous->mip){++previous->revision;return;}
        std::vector<std::uint8_t> decoded;
        if(!decodeImage(source,sourceBytes,width,height,pitch,decodedFormat,decoded)){++counts.failed;invalidate(raw);return;}
        if constexpr(DecodeDimension>MaxDimension){if(width>MaxDimension||height>MaxDimension){const unsigned w=std::min(width,MaxDimension),h=std::min(height,MaxDimension);std::vector<std::uint8_t> small(size_t(w)*h*4);
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){unsigned sum[4]={},n=0;for(unsigned sy=y*height/h;sy<(y+1)*height/h;++sy)for(unsigned sx=x*width/w;sx<(x+1)*width/w;++sx){for(unsigned c=0;c<4;++c)sum[c]+=decoded[(size_t(sy)*width+sx)*4+c];++n;}for(unsigned c=0;c<4;++c)small[(size_t(y)*w+x)*4+c]=std::uint8_t((sum[c]+n/2)/n);}decoded=std::move(small);width=w;height=h;}}
        std::uint64_t hash=14695981039346656037ull;for(auto byte:decoded)hash=(hash^byte)*1099511628211ull;
        if constexpr(DecodeDimension>MaxDimension){if(previous&&previous->owner==owner&&previous->width==width&&previous->height==height&&previous->mip==mip&&previous->format==d3dFormat&&previous->rgbaHash==hash&&previous->pixels==decoded){previous->used=++clock_;return;}}
        Entry& e=slot(raw,owner);drop(e);
        while(bytes_+decoded.size()>PixelBudget){auto* victim=oldest(&e);if(!victim)break;drop(*victim);*victim={};++counts.evictions;}
        e.rgbaHash=hash;
        e.pixels=std::move(decoded);e.width=width;e.height=height;e.mip=mip;e.format=d3dFormat;++e.revision;e.used=++clock_;bytes_+=e.pixels.size();++counts.retained;
        }catch(...){++counts.failed;invalidate(raw);}
    }
    bool lookup(const void* raw,View& out){
        out={};if(!enabled.load(std::memory_order_relaxed))return false;auto* e=find(raw);if(!e||e->pixels.empty()){++counts.misses;return false;}
        e->used=++clock_;out={e->pixels.data(),e->width,e->height,e->mip,e->format,e->lifetime,e->revision,e->rgbaHash};++counts.hits;return true;
    }
};
inline Cache& cache(){static thread_local Cache value;return value;}
}
