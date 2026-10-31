// Frozen 51f0107 decode/reduction oracle; preserve the reference algorithm.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace FrozenActorTexture {
// ARGB1555/XRGB1555/ARGB4444 (0.3.171): 16-bit uploads such as the game's 1-bit
// alpha shadowblob.blp. Appended so earlier values keep their numbers.
enum class Format { BGRA8,BGRX8,RGB565,BC1,BC2,BC3,ARGB1555,XRGB1555,ARGB4444 };
inline bool compressed(Format format){return format==Format::BC1||format==Format::BC2||format==Format::BC3;}
inline std::size_t rowBytes(unsigned width,Format format){
    if(compressed(format))return std::size_t((width+3)/4)*(format==Format::BC1?8:16);
    return std::size_t(width)*(format==Format::BGRA8||format==Format::BGRX8?4:2);
}
inline unsigned rowCount(unsigned height,Format format){return compressed(format)?(height+3)/4:height;}
inline std::array<std::uint8_t,4> rgb565(std::uint16_t value){unsigned r=value>>11,g=(value>>5)&63,b=value&31;return {std::uint8_t((r<<3)|(r>>2)),std::uint8_t((g<<2)|(g>>4)),std::uint8_t((b<<3)|(b>>2)),255};}
inline std::array<std::uint8_t,4> argb1555(std::uint16_t value,bool alpha){unsigned r=(value>>10)&31,g=(value>>5)&31,b=value&31;return {std::uint8_t((r<<3)|(r>>2)),std::uint8_t((g<<3)|(g>>2)),std::uint8_t((b<<3)|(b>>2)),std::uint8_t(!alpha||(value&0x8000)?255:0)};}
inline std::array<std::uint8_t,4> argb4444(std::uint16_t value){return {std::uint8_t(((value>>8)&15)*17),std::uint8_t(((value>>4)&15)*17),std::uint8_t((value&15)*17),std::uint8_t((value>>12)*17)};}
// Identical preflight for capture and decode: rejecting malformed alpha data
// before queueing preserves the render thread's packet and vertex budgets.
inline bool readable(const void* source,std::size_t sourceBytes,unsigned width,unsigned height,std::size_t pitch,Format format){
    if(!source||!width||!height||width>128||height>128)return false;
    return pitch>=rowBytes(width,format)&&rowCount(height,format)<=sourceBytes/pitch;
}
struct Snapshot {
    // Owns copied mip bytes only. No texture, locked pointer or D3D interface
    // may be retained by the GI worker.
    std::vector<std::uint8_t> bytes;
    unsigned width=0,height=0;
    std::size_t pitch=0;
    Format format=Format::BGRA8;
    bool valid()const{return readable(bytes.data(),bytes.size(),width,height,pitch,format);}
};
inline bool decode(const void* source,std::size_t sourceBytes,unsigned width,unsigned height,std::size_t pitch,Format format,std::vector<std::uint8_t>& output){
    output.clear();if(!readable(source,sourceBytes,width,height,pitch,format))return false;
    std::vector<std::uint8_t> rgba(std::size_t(width)*height*4);
    auto* bytes=static_cast<const std::uint8_t*>(source);
    if(!compressed(format)){for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto* dst=rgba.data()+(y*width+x)*4;
        if(format!=Format::BGRA8&&format!=Format::BGRX8){std::uint16_t v;std::memcpy(&v,bytes+y*pitch+x*2,2);
            auto c=format==Format::RGB565?rgb565(v):format==Format::ARGB4444?argb4444(v):argb1555(v,format==Format::ARGB1555);std::memcpy(dst,c.data(),4);}
        else{auto* src=bytes+y*pitch+x*4;dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=format==Format::BGRA8?src[3]:255;}}
    }else for(unsigned by=0;by<(height+3)/4;++by)for(unsigned bx=0;bx<(width+3)/4;++bx){
        auto* block=bytes+by*pitch+bx*(format==Format::BC1?8:16);auto* color=block+(format==Format::BC1?0:8);
        std::uint16_t c0,c1;std::uint32_t selectors;std::memcpy(&c0,color,2);std::memcpy(&c1,color+2,2);std::memcpy(&selectors,color+4,4);
        std::array<std::uint8_t,4> colors[4]={rgb565(c0),rgb565(c1),{}, {}};bool four=c0>c1||format!=Format::BC1;
        for(unsigned c=0;c<3;++c){colors[2][c]=std::uint8_t(four?(2u*colors[0][c]+colors[1][c])/3:(unsigned(colors[0][c])+colors[1][c])/2);colors[3][c]=four?std::uint8_t((unsigned(colors[0][c])+2u*colors[1][c])/3):0;}
        colors[2][3]=255;colors[3][3]=four?255:0;
        std::uint8_t alpha[8]={block[0],block[1]};std::uint64_t alphaBits=0;
        if(format==Format::BC3){for(unsigned j=0;j<6;++j)alphaBits|=std::uint64_t(block[2+j])<<(8*j);
            if(alpha[0]>alpha[1])for(unsigned j=1;j<=6;++j)alpha[j+1]=std::uint8_t(((7-j)*unsigned(alpha[0])+j*unsigned(alpha[1]))/7);
            else{for(unsigned j=1;j<=4;++j)alpha[j+1]=std::uint8_t(((5-j)*unsigned(alpha[0])+j*unsigned(alpha[1]))/5);alpha[6]=0;alpha[7]=255;}}
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){unsigned px=bx*4+x,py=by*4+y,i=y*4+x;if(px>=width||py>=height)continue;
            auto value=colors[(selectors>>(2*i))&3];if(format==Format::BC2)value[3]=std::uint8_t(((block[i/2]>>((i&1)*4))&15)*17);
            else if(format==Format::BC3)value[3]=alpha[(alphaBits>>(3*i))&7];std::memcpy(rgba.data()+(py*width+px)*4,value.data(),4);}
    }
    output=std::move(rgba);return true;
}
} // namespace NorthlightActorTexture

namespace Frozen {
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
    static bool format(unsigned d3d,FrozenActorTexture::Format& out){
        using F=FrozenActorTexture::Format;
        switch(d3d){case 21:out=F::BGRA8;break;case 22:out=F::BGRX8;break;case 23:out=F::RGB565;break;
        case 24:out=F::XRGB1555;break;case 25:out=F::ARGB1555;break;case 26:out=F::ARGB4444;break;
        case 0x31545844:out=F::BC1;break;case 0x33545844:out=F::BC2;break;case 0x35545844:out=F::BC3;break;default:return false;}return true;
    }
    static bool decodeImage(const void* source,std::size_t bytes,unsigned width,unsigned height,std::size_t pitch,FrozenActorTexture::Format format,std::vector<std::uint8_t>& output){
        if(width<=128&&height<=128)return FrozenActorTexture::decode(source,bytes,width,height,pitch,format,output);
        if(!source)return false;output.resize(size_t(width)*height*4);std::vector<std::uint8_t> tile,packed;
        for(unsigned y=0;y<height;y+=128)for(unsigned x=0;x<width;x+=128){const unsigned w=std::min(128u,width-x),h=std::min(128u,height-y);const auto offset=FrozenActorTexture::rowCount(y,format)*pitch+FrozenActorTexture::rowBytes(x,format);
            const auto rowBytes=FrozenActorTexture::rowBytes(w,format);const auto rows=FrozenActorTexture::rowCount(h,format);packed.resize(rowBytes*rows);
            for(unsigned r=0;r<rows;++r){const auto start=offset+size_t(r)*pitch;if(start>bytes||rowBytes>bytes-start)return false;std::copy_n(static_cast<const unsigned char*>(source)+start,rowBytes,packed.data()+size_t(r)*rowBytes);}
            if(!FrozenActorTexture::decode(packed.data(),packed.size(),w,h,rowBytes,format,tile))return false;
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
        auto fail=[&](){invalidate(raw);return false;};auto* e=find(raw);FrozenActorTexture::Format decodedFormat;
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
        FrozenActorTexture::Format decodedFormat;
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
