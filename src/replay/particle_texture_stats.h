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
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

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
            if(mode==2){if(f>=0&&f<=1)return f;f=std::fmod(f,2.f);if(f<0)f+=2;return f<=1?f:2-f;}
            return f>0&&f<1?f:f-std::floor(f);
        };
        const float x=coord(u,addressU)*width-.5f,y=coord(v,addressV)*height-.5f;
        auto texel=[](int i,unsigned n,unsigned mode){
            if(i>=0&&unsigned(i)<n)return unsigned(i);
            if(mode==3)return unsigned(std::max(0,std::min(int(n)-1,i)));
            if(mode==2){const int period=int(n)*2;i%=period;if(i<0)i+=period;return unsigned(i<int(n)?i:period-1-i);}
            i%=int(n);if(i<0)i+=int(n);return unsigned(i);
        };
        static const std::array<float,256> linearRGB=[](){std::array<float,256> a{};for(unsigned i=0;i<256;++i){const float s=float(i)/255;a[i]=s<=.04045f?s/12.92f:std::pow((s+.055f)/1.055f,2.4f);}return a;}();
        if(!linear){const auto* p=rgba+(std::size_t(texel(int(std::floor(y+.5f)),height,addressV))*width+texel(int(std::floor(x+.5f)),width,addressU))*4;
            for(unsigned c=0;c<4;++c)out[c]=srgb&&c<3?linearRGB[p[c]]:float(p[c])/255;return true;}
        const int ix=int(std::floor(x)),iy=int(std::floor(y));
        const unsigned x0=texel(ix,width,addressU),y0=texel(iy,height,addressV);
        const unsigned x1=texel(ix+1,width,addressU),y1=texel(iy+1,height,addressV);
        const auto* p00=rgba+(std::size_t(y0)*width+x0)*4;const auto* p10=rgba+(std::size_t(y0)*width+x1)*4;
        const auto* p01=rgba+(std::size_t(y1)*width+x0)*4;const auto* p11=rgba+(std::size_t(y1)*width+x1)*4;
        for(unsigned c=0;c<4;++c){
            auto read=[&](const std::uint8_t* p){const auto value=p[c];return srgb&&c<3?linearRGB[value]:float(value)/255;};
            const float fx=x-ix,fy=y-iy;out[c]=(read(p00)*(1-fx)+read(p10)*fx)*(1-fy)+(read(p01)*(1-fx)+read(p11)*fx)*fy;
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
    Entry* recent_=nullptr;
    // Diagnostics only (PTEX misses=...): the last event per texture key, bounded.
    std::unordered_map<const void*,Seen> history_;Seen lastMiss_=Never;
    void note(const void* raw,Seen seen){if(!raw)return;if(history_.size()>=65536)history_.clear();history_[raw]=seen;}
    void evicted(const Entry& e){note(e.raw,Evicted);}
    std::size_t bytes_=0;
    std::uint64_t serial_=0,clock_=0;
    Entry* find(const void* raw){if(!raw)return nullptr;if(recent_&&recent_->raw==raw)return recent_;const auto it=entries_.find(raw);return it!=entries_.end()?(recent_=&it->second):nullptr;}
    void drop(Entry& e){bytes_-=e.pixels.size()+e.source.size();std::vector<std::uint8_t>().swap(e.pixels);std::vector<std::uint8_t>().swap(e.source);e.width=e.height=0;}
    // Over a limit: evict the least recently used down to 7/8 of it, so a load burst scans the entries once, not per upload.
    void evict(std::size_t incoming,const Entry* keep){
        if(bytes_+incoming<=PixelBudget&&entries_.size()<=MaxEntries)return;
        std::vector<std::pair<std::uint64_t,const void*>> order;order.reserve(entries_.size());
        for(const auto& [raw,e]:entries_)if(&e!=keep)order.emplace_back(e.used,raw);
        std::sort(order.begin(),order.end());
        for(const auto& [used,raw]:order){
            if(bytes_+incoming<=PixelBudget-PixelBudget/8&&entries_.size()<=MaxEntries-MaxEntries/8)break;
            auto it=entries_.find(raw);evicted(it->second);if(recent_==&it->second)recent_=nullptr;drop(it->second);entries_.erase(it);++counts.evictions;
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
    Cache()=default;
    Cache(const Cache&)=delete;
    Cache& operator=(const Cache&)=delete;
    Seen lastMiss()const{return lastMiss_;}
    std::size_t bytes()const{return bytes_;}
    unsigned size()const{unsigned n=0;for(const auto& [raw,e]:entries_)n+=e.held();return n;}
    void clear(){recent_=nullptr;entries_.clear();bytes_=0;history_.clear();}
    void erase(const void* raw,const void* owner=nullptr){if(auto* e=find(raw))if(!owner||e->owner==owner){recent_=nullptr;drop(*e);entries_.erase(raw);++counts.releases;note(raw,Released);}}
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
        auto* previous=find(raw);if(previous&&previous->owner!=owner){erase(raw);previous=nullptr;}
        NorthlightActorTexture::Format decodedFormat;
        if(!raw||!owner||!format(d3dFormat,decodedFormat)){++counts.unsupported;invalidate(raw);note(raw,Unsupported);return;}
        if(!width||!height||width>MaxDimension||height>MaxDimension){++counts.large;if(!previous)note(raw,Large);return;}
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
    Entry* recent_=nullptr;
    std::size_t bytes_=0;
    std::uint64_t serial_=0,clock_=0;
    Entry* find(const void* raw){if(raw){if(recent_&&recent_->raw==raw)return recent_;for(auto& e:entries_)if(e.raw==raw){recent_=&e;return &e;}}return nullptr;}
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
#if defined(__SSE2__)
    // For n<=64, (255*n+n/2)*n is below 2^32, so the ceiling
    // reciprocal produces the same integer quotient for every channel sum.
    // Keep the two-texel loop separate so larger box paths stay compact.
    template<NorthlightActorTexture::Format Format>__attribute__((noinline)) static void reduceTwo(const void* source,std::size_t pitch,unsigned tw,unsigned th,unsigned scaleY,std::vector<std::uint8_t>& output){
        const auto* data=static_cast<const unsigned char*>(source);const auto zero=_mm_setzero_si128();const unsigned n=scaleY*2;const auto reciprocal=std::uint32_t(((UINT64_C(1)<<32)+n-1)/n);
        for(unsigned y=0;y<th;++y)for(unsigned x=0;x<tw;++x){__m128i sum=zero;
            for(unsigned dy=0;dy<scaleY;++dy){const auto* row=data+size_t(y*scaleY+dy)*pitch+size_t(x)*8;const auto bytes=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(row));const auto lo=_mm_unpacklo_epi8(bytes,zero);sum=_mm_add_epi16(sum,_mm_add_epi16(lo,_mm_srli_si128(lo,8)));}
            alignas(16) std::uint16_t channels[8];_mm_store_si128(reinterpret_cast<__m128i*>(channels),sum);auto* dst=output.data()+(size_t(y)*tw+x)*4;
            dst[0]=std::uint8_t((std::uint64_t(channels[2]+n/2)*reciprocal)>>32);dst[1]=std::uint8_t((std::uint64_t(channels[1]+n/2)*reciprocal)>>32);dst[2]=std::uint8_t((std::uint64_t(channels[0]+n/2)*reciprocal)>>32);dst[3]=Format==NorthlightActorTexture::Format::BGRX8?255:std::uint8_t((std::uint64_t(channels[3]+n/2)*reciprocal)>>32);
        }
    }
#endif
    template<NorthlightActorTexture::Format Format>static bool reduceColor(const void* source,std::size_t bytes,unsigned width,unsigned height,std::size_t pitch,std::vector<std::uint8_t>& output){
        using F=NorthlightActorTexture::Format;
        constexpr bool wide=Format==F::BGRA8||Format==F::BGRX8;
        constexpr unsigned pixelBytes=wide?4:2;
        const size_t rowBytes=size_t(width)*pixelBytes;
        if(pitch<rowBytes||bytes<rowBytes||height-1>(bytes-rowBytes)/pitch)return false;
        const unsigned tw=std::min(width,MaxDimension),th=std::min(height,MaxDimension);output.resize(size_t(tw)*th*4);
        const bool evenX=width%tw==0,evenY=height%th==0;const unsigned scaleX=width/tw,scaleY=height/th;
#if defined(__SSE2__)
        if constexpr(wide){if(evenX&&evenY&&scaleX==2&&scaleY<=8){reduceTwo<Format>(source,pitch,tw,th,scaleY,output);return true;}}
        // At most 64 texels per box: each 16-bit channel sum stays below 16321.
        if constexpr(wide){if(evenX&&evenY&&(scaleX%4)==0&&scaleX<=8&&scaleY<=8){
            const auto* data=static_cast<const unsigned char*>(source);const auto zero=_mm_setzero_si128();const unsigned n=scaleX*scaleY;const auto reciprocal=std::uint32_t(((UINT64_C(1)<<32)+n-1)/n);
            for(unsigned y=0;y<th;++y)for(unsigned x=0;x<tw;++x){__m128i sum=zero;
                for(unsigned dy=0;dy<scaleY;++dy){const auto* row=data+size_t(y*scaleY+dy)*pitch+size_t(x*scaleX)*4;
                    for(unsigned dx=0;dx<scaleX;dx+=4){const auto bytes=_mm_loadu_si128(reinterpret_cast<const __m128i*>(row+dx*4));const auto lo=_mm_unpacklo_epi8(bytes,zero),hi=_mm_unpackhi_epi8(bytes,zero);const auto pair=_mm_add_epi16(lo,hi);sum=_mm_add_epi16(sum,_mm_add_epi16(pair,_mm_srli_si128(pair,8)));}}
                alignas(16) std::uint16_t channels[8];_mm_store_si128(reinterpret_cast<__m128i*>(channels),sum);auto* dst=output.data()+(size_t(y)*tw+x)*4;
                dst[0]=std::uint8_t((std::uint64_t(channels[2]+n/2)*reciprocal)>>32);dst[1]=std::uint8_t((std::uint64_t(channels[1]+n/2)*reciprocal)>>32);dst[2]=std::uint8_t((std::uint64_t(channels[0]+n/2)*reciprocal)>>32);dst[3]=Format==F::BGRX8?255:std::uint8_t((std::uint64_t(channels[3]+n/2)*reciprocal)>>32);
            }return true;
        }}
#endif

        for(unsigned y=0;y<th;++y){const unsigned y0=evenY?y*scaleY:y*height/th,y1=evenY?(y+1)*scaleY:(y+1)*height/th;
        for(unsigned x=0;x<tw;++x){unsigned sum[4]={},n=0;
            const unsigned x0=evenX?x*scaleX:x*width/tw,x1=evenX?(x+1)*scaleX:(x+1)*width/tw;
            for(unsigned sy=y0;sy<y1;++sy)for(unsigned sx=x0;sx<x1;++sx){
                const auto* pixel=static_cast<const unsigned char*>(source)+size_t(sy)*pitch+sx*pixelBytes;
                if constexpr(wide){for(unsigned c=0;c<4;++c)sum[c]+=pixel[c];}
                else{std::uint16_t value;std::memcpy(&value,pixel,2);
                    const auto rgba=Format==F::RGB565?NorthlightActorTexture::rgb565(value):Format==F::ARGB4444?NorthlightActorTexture::argb4444(value):NorthlightActorTexture::argb1555(value,Format==F::ARGB1555);
                    for(unsigned c=0;c<4;++c)sum[c]+=rgba[c];}
                ++n;
            }
            auto* pixel=output.data()+(size_t(y)*tw+x)*4;
            pixel[0]=std::uint8_t((sum[wide?2:0]+n/2)/n);pixel[1]=std::uint8_t((sum[1]+n/2)/n);pixel[2]=std::uint8_t((sum[wide?0:2]+n/2)/n);
            pixel[3]=Format==F::BGRX8?255:std::uint8_t((sum[3]+n/2)/n);
        }}
        return true;
    }
    static bool decodeImage(const void* source,std::size_t bytes,unsigned width,unsigned height,std::size_t pitch,NorthlightActorTexture::Format format,std::vector<std::uint8_t>& output,bool reduce=false){
        if(width<=128&&height<=128)return NorthlightActorTexture::decode(source,bytes,width,height,pitch,format,output);
        if(!source)return false;
        const unsigned targetWidth=reduce?std::min(width,MaxDimension):width,targetHeight=reduce?std::min(height,MaxDimension):height;
        using F=NorthlightActorTexture::Format;
        if(reduce)switch(format){
        case F::BGRA8:return reduceColor<F::BGRA8>(source,bytes,width,height,pitch,output);
        case F::BGRX8:return reduceColor<F::BGRX8>(source,bytes,width,height,pitch,output);
        case F::RGB565:return reduceColor<F::RGB565>(source,bytes,width,height,pitch,output);
        case F::XRGB1555:return reduceColor<F::XRGB1555>(source,bytes,width,height,pitch,output);
        case F::ARGB1555:return reduceColor<F::ARGB1555>(source,bytes,width,height,pitch,output);
        case F::ARGB4444:return reduceColor<F::ARGB4444>(source,bytes,width,height,pitch,output);
        default:break;
        }
        output.resize(size_t(targetWidth)*targetHeight*4);std::vector<std::uint8_t> tile,packed;
        std::vector<unsigned> sums(reduce?size_t(targetWidth)*targetHeight*4:0),columns(reduce?width:0),rowOffsets(reduce?height:0);
        if(reduce)for(unsigned x=0;x<width;++x)columns[x]=((x+1)*targetWidth-1)/width;
        if(reduce)for(unsigned y=0;y<height;++y)rowOffsets[y]=(((y+1)*targetHeight-1)/height)*targetWidth*4;
        if(reduce&&NorthlightActorTexture::compressed(format)){
            const auto rowBytes=NorthlightActorTexture::rowBytes(width,format),rows=NorthlightActorTexture::rowCount(height,format);
            if(pitch<rowBytes||bytes<rowBytes||rows-1>(bytes-rowBytes)/pitch)return false;
            auto write=[&](unsigned x,unsigned y,const std::array<std::uint8_t,4>& rgba){
                const auto at=rowOffsets[y]+columns[x]*4;
                for(unsigned c=0;c<4;++c)sums[at+c]+=rgba[c];
            };
            const auto* data=static_cast<const std::uint8_t*>(source);
            // A complete BC block belongs to one box only for these exact
            // integer scales. Other dimensions keep the per-texel path.
            const bool wholeBlocks=width%targetWidth==0&&height%targetHeight==0&&(width/targetWidth)%4==0&&(height/targetHeight)%4==0;
            if(wholeBlocks){
                auto block=[&](unsigned x,unsigned y,const std::array<unsigned,4>& rgba){const auto at=rowOffsets[y]+columns[x]*4;for(unsigned c=0;c<4;++c)sums[at+c]+=rgba[c];};
                if(format==F::BC1)NorthlightActorTexture::decodeCompressed<F::BC1,true>(data,width,height,pitch,block);
                else if(format==F::BC2)NorthlightActorTexture::decodeCompressed<F::BC2,true>(data,width,height,pitch,block);
                else NorthlightActorTexture::decodeCompressed<F::BC3,true>(data,width,height,pitch,block);
            }
            else if(format==F::BC1)NorthlightActorTexture::decodeCompressed<F::BC1>(data,width,height,pitch,write);
            else if(format==F::BC2)NorthlightActorTexture::decodeCompressed<F::BC2>(data,width,height,pitch,write);
            else NorthlightActorTexture::decodeCompressed<F::BC3>(data,width,height,pitch,write);
        }else{
        for(unsigned y=0;y<height;y+=128)for(unsigned x=0;x<width;x+=128){const unsigned w=std::min(128u,width-x),h=std::min(128u,height-y);const auto offset=NorthlightActorTexture::rowCount(y,format)*pitch+NorthlightActorTexture::rowBytes(x,format);
            const auto rowBytes=NorthlightActorTexture::rowBytes(w,format);const auto rows=NorthlightActorTexture::rowCount(h,format);
            // Reuse the upload's row stride when the existing decoder's full
            // footprint is available. Edge tiles retain the packed fallback.
            if(offset<=bytes&&pitch&&rows<=(bytes-offset)/pitch){
                if(!NorthlightActorTexture::decode(static_cast<const unsigned char*>(source)+offset,bytes-offset,w,h,pitch,format,tile))return false;
            }else{
                packed.resize(rowBytes*rows);
                for(unsigned r=0;r<rows;++r){const auto start=offset+size_t(r)*pitch;if(start>bytes||rowBytes>bytes-start)return false;std::copy_n(static_cast<const unsigned char*>(source)+start,rowBytes,packed.data()+size_t(r)*rowBytes);}
                if(!NorthlightActorTexture::decode(packed.data(),packed.size(),w,h,rowBytes,format,tile))return false;
            }
            if(reduce){
                // Integer box sums commute across tiles. This is the original
                // box filter without an intermediate full-size RGBA image.
                for(unsigned r=0;r<h;++r){const auto row=size_t(((y+r+1)*targetHeight-1)/height)*targetWidth;
                    for(unsigned col=0;col<w;++col){const auto target=(row+columns[x+col])*4,from=(size_t(r)*w+col)*4;
                        for(unsigned c=0;c<4;++c)sums[target+c]+=tile[from+c];}}
            }else for(unsigned r=0;r<h;++r)std::copy_n(tile.data()+size_t(r)*w*4,w*4,output.data()+(size_t(y+r)*width+x)*4);}
        }
        if(reduce)for(unsigned y=0;y<targetHeight;++y)for(unsigned x=0;x<targetWidth;++x){
            const unsigned n=((x+1)*width/targetWidth-x*width/targetWidth)*((y+1)*height/targetHeight-y*height/targetHeight);
            const auto at=(size_t(y)*targetWidth+x)*4;for(unsigned c=0;c<4;++c)output[at+c]=std::uint8_t((sums[at+c]+n/2)/n);
        }
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
        const auto w=original->width,h=original->height,mip=original->mip,fmt=original->format;const auto hash=original->rgbaHash;
        if(auto* same=find(destination))if(same->width==w&&same->height==h&&same->mip==mip&&same->format==fmt&&same->rgbaHash==hash&&same->pixels==original->pixels){same->used=++clock_;return true;}
        auto pixels=original->pixels;
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
        if(left%sx||right%sx||top%sy||bottom%sy)return fail();
        using F=NorthlightActorTexture::Format;const auto fromAddress=reinterpret_cast<std::uintptr_t>(source),toAddress=reinterpret_cast<std::uintptr_t>(e->pixels.data());
        const bool overlap=fromAddress<=toAddress?toAddress-fromAddress<sourceBytes:fromAddress-toAddress<e->pixels.size();
        const bool direct=(sx!=1||sy!=1)&&!overlap&&(decodedFormat==F::BGRA8||decodedFormat==F::BGRX8)&&NorthlightActorTexture::readable(source,sourceBytes,right-left,bottom-top,pitch,decodedFormat);
        bool changed=false;std::vector<std::uint8_t> patch;
        if(direct){const auto* bytes=static_cast<const std::uint8_t*>(source);const unsigned n=sx*sy;
            for(unsigned y=top/sy;y<bottom/sy;++y)for(unsigned x=left/sx;x<right/sx;++x){unsigned sum[4]={};
                for(unsigned j=0;j<sy;++j)for(unsigned i=0;i<sx;++i){const auto* pixel=bytes+size_t(y*sy-top+j)*pitch+size_t(x*sx-left+i)*4;sum[0]+=pixel[2];sum[1]+=pixel[1];sum[2]+=pixel[0];sum[3]+=decodedFormat==F::BGRX8?255:pixel[3];}
                for(unsigned c=0;c<4;++c){auto& old=e->pixels[(size_t(y)*e->width+x)*4+c];const auto value=std::uint8_t((sum[c]+n/2)/n);changed|=old!=value;old=value;}}
        }else{
        if(!decodeImage(source,sourceBytes,right-left,bottom-top,pitch,decodedFormat,patch))return fail();
        if(sx==1&&sy==1){const size_t rowBytes=size_t(right-left)*4;for(unsigned y=top;y<bottom;++y){auto* dest=e->pixels.data()+(size_t(y)*e->width+left)*4;const auto* from=patch.data()+size_t(y-top)*rowBytes;if(std::memcmp(dest,from,rowBytes)){std::memcpy(dest,from,rowBytes);changed=true;}}}
        else for(unsigned y=top/sy;y<bottom/sy;++y)for(unsigned x=left/sx;x<right/sx;++x){unsigned sum[4]={};for(unsigned j=0;j<sy;++j)for(unsigned i=0;i<sx;++i)for(unsigned c=0;c<4;++c)sum[c]+=patch[((size_t(y*sy-top+j))*(right-left)+x*sx-left+i)*4+c];for(unsigned c=0;c<4;++c){auto& old=e->pixels[(size_t(y)*e->width+x)*4+c];auto value=std::uint8_t((sum[c]+sx*sy/2)/(sx*sy));changed|=old!=value;old=value;}}
        }
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
        // Prove byte equality before allocating/decoding/hashing a repeated
        // compact colour upload. XRGB's unused alpha is deliberately ignored.
        using F=NorthlightActorTexture::Format;
        if(previous&&previous->owner==owner&&width<=MaxDimension&&height<=MaxDimension&&previous->width==width&&previous->height==height&&previous->mip==mip&&previous->format==d3dFormat&&
           (decodedFormat==F::BGRA8||decodedFormat==F::BGRX8)&&NorthlightActorTexture::readable(source,sourceBytes,width,height,pitch,decodedFormat)){
            bool same=true;
            for(unsigned y=0;y<height&&same;++y)for(unsigned x=0;x<width;++x){const auto* from=static_cast<const unsigned char*>(source)+size_t(y)*pitch+x*4;const auto* to=previous->pixels.data()+(size_t(y)*width+x)*4;
                if(to[0]!=from[2]||to[1]!=from[1]||to[2]!=from[0]||to[3]!=(decodedFormat==F::BGRX8?255:from[3])){same=false;break;}}
            if(same){previous->used=++clock_;return;}
        }
        std::vector<std::uint8_t> decoded;
        if(!decodeImage(source,sourceBytes,width,height,pitch,decodedFormat,decoded,true)){++counts.failed;invalidate(raw);return;}
        width=std::min(width,MaxDimension);height=std::min(height,MaxDimension);
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
