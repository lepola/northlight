#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace NorthlightActorTexture {
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
inline constexpr auto SelectorCounts=[](){std::array<std::uint32_t,256> table{};for(unsigned value=0;value<256;++value)for(unsigned i=0;i<4;++i)table[value]+=1u<<(((value>>(i*2))&3)*8);return table;}();
template<Format FormatValue,bool BlockSums=false,class Write>inline void decodeCompressed(const std::uint8_t* bytes,unsigned width,unsigned height,std::size_t pitch,Write&& write){
    for(unsigned by=0;by<(height+3)/4;++by)for(unsigned bx=0;bx<(width+3)/4;++bx){
        auto* block=bytes+by*pitch+bx*(FormatValue==Format::BC1?8:16);auto* color=block+(FormatValue==Format::BC1?0:8);
        std::uint16_t c0,c1;std::uint32_t selectors;std::memcpy(&c0,color,2);std::memcpy(&c1,color+2,2);std::memcpy(&selectors,color+4,4);
        std::array<std::uint8_t,4> colors[4]={rgb565(c0),rgb565(c1),{}, {}};bool four=c0>c1||FormatValue!=Format::BC1;
        for(unsigned c=0;c<3;++c){colors[2][c]=std::uint8_t(four?(2u*colors[0][c]+colors[1][c])/3:(unsigned(colors[0][c])+colors[1][c])/2);colors[3][c]=four?std::uint8_t((unsigned(colors[0][c])+2u*colors[1][c])/3):0;}
        colors[2][3]=255;colors[3][3]=four?255:0;
        std::uint8_t alpha[8]={block[0],block[1]};std::uint64_t alphaBits=0;
        if(FormatValue==Format::BC3){for(unsigned j=0;j<6;++j)alphaBits|=std::uint64_t(block[2+j])<<(8*j);
            if(alpha[0]>alpha[1])for(unsigned j=1;j<=6;++j)alpha[j+1]=std::uint8_t(((7-j)*unsigned(alpha[0])+j*unsigned(alpha[1]))/7);
            else{for(unsigned j=1;j<=4;++j)alpha[j+1]=std::uint8_t(((5-j)*unsigned(alpha[0])+j*unsigned(alpha[1]))/5);alpha[6]=0;alpha[7]=255;}}
        std::array<unsigned,4> sums{};
        if constexpr(BlockSums){if((bx+1)*4<=width&&(by+1)*4<=height){
            // Four byte histograms add without carries (each count is <=16).
            const auto counts=SelectorCounts[selectors&255]+SelectorCounts[(selectors>>8)&255]+SelectorCounts[(selectors>>16)&255]+SelectorCounts[selectors>>24];
            for(unsigned index=0;index<4;++index)for(unsigned c=0;c<4;++c)sums[c]+=colors[index][c]*((counts>>(index*8))&255);
            if constexpr(FormatValue!=Format::BC1){sums[3]=0;for(unsigned i=0;i<16;++i){
                if constexpr(FormatValue==Format::BC2)sums[3]+=((block[i/2]>>((i&1)*4))&15)*17;
                else sums[3]+=alpha[(alphaBits>>(3*i))&7];
            }}
            write(bx*4,by*4,sums);continue;
        }}
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){unsigned px=bx*4+x,py=by*4+y,i=y*4+x;if(px>=width||py>=height)continue;
            auto value=colors[(selectors>>(2*i))&3];if(FormatValue==Format::BC2)value[3]=std::uint8_t(((block[i/2]>>((i&1)*4))&15)*17);
            else if(FormatValue==Format::BC3)value[3]=alpha[(alphaBits>>(3*i))&7];
            if constexpr(BlockSums){for(unsigned c=0;c<4;++c)sums[c]+=value[c];}else write(px,py,value);}
        if constexpr(BlockSums)write(bx*4,by*4,sums);
    }
}
inline bool decode(const void* source,std::size_t sourceBytes,unsigned width,unsigned height,std::size_t pitch,Format format,std::vector<std::uint8_t>& output){
    if(!readable(source,sourceBytes,width,height,pitch,format)){output.clear();return false;}
    std::vector<std::uint8_t> rgba;
    const auto src=reinterpret_cast<std::uintptr_t>(source),dst=reinterpret_cast<std::uintptr_t>(output.data());
    const bool alias=output.data()&&(src<=dst?dst-src<sourceBytes:src-dst<output.capacity());
    // Every destination texel is overwritten. Preserve an existing tile's
    // storage, but keep the original separate-buffer behaviour for aliases.
    if(!alias)rgba.swap(output);
    output.clear();rgba.resize(std::size_t(width)*height*4);
    auto* bytes=static_cast<const std::uint8_t*>(source);
    if(!compressed(format)){
        auto decodeColor=[&](auto tag){constexpr Format f=decltype(tag)::value;for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto* dst=rgba.data()+(y*width+x)*4;
            if constexpr(f!=Format::BGRA8&&f!=Format::BGRX8){std::uint16_t v;std::memcpy(&v,bytes+y*pitch+x*2,2);
                auto c=f==Format::RGB565?rgb565(v):f==Format::ARGB4444?argb4444(v):argb1555(v,f==Format::ARGB1555);std::memcpy(dst,c.data(),4);}
            else{auto* src=bytes+y*pitch+x*4;dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=f==Format::BGRA8?src[3]:255;}}};
        switch(format){case Format::BGRA8:decodeColor(std::integral_constant<Format,Format::BGRA8>{});break;case Format::BGRX8:decodeColor(std::integral_constant<Format,Format::BGRX8>{});break;
        case Format::RGB565:decodeColor(std::integral_constant<Format,Format::RGB565>{});break;case Format::ARGB4444:decodeColor(std::integral_constant<Format,Format::ARGB4444>{});break;
        case Format::ARGB1555:decodeColor(std::integral_constant<Format,Format::ARGB1555>{});break;default:decodeColor(std::integral_constant<Format,Format::XRGB1555>{});break;}
    }else{auto write=[&](unsigned x,unsigned y,const std::array<std::uint8_t,4>& value){std::memcpy(rgba.data()+(size_t(y)*width+x)*4,value.data(),4);};
        if(format==Format::BC1)decodeCompressed<Format::BC1>(bytes,width,height,pitch,write);
        else if(format==Format::BC2)decodeCompressed<Format::BC2>(bytes,width,height,pitch,write);
        else decodeCompressed<Format::BC3>(bytes,width,height,pitch,write);
    }
    output=std::move(rgba);return true;
}
} // namespace NorthlightActorTexture
