#pragma once
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace NorthlightBcComparison {
// Diagnostic envelope, not a hardware conformance test. D3D11.3 19.5.2 permits
// 1/255 + 3% of the endpoint range for BC1/2/3. The extra half UNORM8 step
// accounts for converting a sampled value into our A8R8G8B8 render target.
// https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#19.5.2%20Error%20Tolerance
inline bool channel(double reference,unsigned actual,double range){
    if(reference==0||reference==255)return double(actual)==reference;
    return std::abs(double(actual)-reference)<1.5+0.03*range;
}
// block is one complete DXT1/3/5 block; pixel is its row-major index (0..15).
inline bool pixel(unsigned format,const unsigned char* block,unsigned pixel,const unsigned char* bgra){
    const bool bc1=format==0x31545844,bc2=format==0x33545844;
    if(!block||!bgra||pixel>=16||(!bc1&&!bc2&&format!=0x35545844))return false;
    const auto* color=block+(bc1?0:8);
    std::uint16_t c0,c1;std::uint32_t bits;
    std::memcpy(&c0,color,2);std::memcpy(&c1,color+2,2);std::memcpy(&bits,color+4,4);
    const unsigned index=bits>>(2*pixel)&3;const bool four=!bc1||c0>c1;
    for(unsigned c=0;c<3;++c){
        const unsigned shift=c==0?11:c==1?5:0,mask=c==1?63:31;
        const unsigned a=(c0>>shift)&mask,b=(c1>>shift)&mask;
        const unsigned ae=c==1?(a<<2)|(a>>4):(a<<3)|(a>>2);
        const unsigned be=c==1?(b<<2)|(b>>4):(b<<3)|(b>>2);
        const unsigned reference=index==0?ae:index==1?be:index==2?(four?(2*ae+be)/3:(ae+be)/2):four?(ae+2*be)/3:0;
        const double range=std::max(std::abs(double(ae)-be),std::abs(double(a)-b)*255/mask);
        if(!channel(reference,bgra[2-c],range))return false;
    }
    if(bc1)return bgra[3]==(four||index!=3?255:0);
    if(bc2)return channel(((block[pixel/2]>>((pixel&1)*4))&15)*17,bgra[3],0);
    std::uint64_t alphaBits=0;for(unsigned n=0;n<6;++n)alphaBits|=std::uint64_t(block[n+2])<<(8*n);
    const unsigned alpha=alphaBits>>(3*pixel)&7,a=block[0],b=block[1];
    const double reference=alpha==0?a:alpha==1?b:a>b?((8-alpha)*double(a)+(alpha-1)*double(b))/7:
        alpha<6?((6-alpha)*double(a)+(alpha-1)*double(b))/5:alpha==6?0:255;
    return channel(reference,bgra[3],std::abs(double(a)-b));
}
}
