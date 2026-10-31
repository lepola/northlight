#pragma once
#include <cstdint>

namespace NorthlightAutoNormalMemory {
inline std::uint64_t budget(std::uint64_t available,std::uint64_t current){
    // Leave the world's 768 MiB build floor plus room for native streaming.
    // Separate recovery keeps the cache from growing back into the same stall.
    if(available<(896ull<<20))return 32ull<<20;
    return available>=(1024ull<<20)?96ull<<20:current;
}
inline std::uint64_t mipBytes(D3DFORMAT format,unsigned width,unsigned height,unsigned levels){
    unsigned block=0,texel=16;
    switch(format){
    case D3DFMT_DXT1:block=8;break;
    case D3DFMT_DXT2:case D3DFMT_DXT3:case D3DFMT_DXT4:case D3DFMT_DXT5:block=16;break;
    case D3DFMT_A8:case D3DFMT_L8:case D3DFMT_P8:texel=1;break;
    case D3DFMT_R5G6B5:case D3DFMT_X1R5G5B5:case D3DFMT_A1R5G5B5:case D3DFMT_A4R4G4B4:case D3DFMT_A8L8:case D3DFMT_V8U8:case D3DFMT_L16:texel=2;break;
    case D3DFMT_R8G8B8:texel=3;break;
    case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:case D3DFMT_A8B8G8R8:case D3DFMT_X8B8G8R8:case D3DFMT_G16R16:case D3DFMT_R32F:texel=4;break;
    case D3DFMT_A16B16G16R16:case D3DFMT_A16B16G16R16F:case D3DFMT_G32R32F:texel=8;break;
    default:break; // Unknown static formats retain a conservative 128-bit estimate.
    }
    if(!width||!height)return 0;
    std::uint64_t bytes=0;unsigned level=0;
    do{
        bytes+=block?((std::uint64_t(width)+3)/4)*((std::uint64_t(height)+3)/4)*block:std::uint64_t(width)*height*texel;
        if((levels&&++level>=levels)||(width==1&&height==1))break;
        width=width>1?width/2:1;height=height>1?height/2:1;
    }while(true);
    return bytes;
}
}
