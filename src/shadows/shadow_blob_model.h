#pragma once
// The game's unit blob shadow texture, described analytically for
// shadow_blob_filter.h (0.3.161; the DLL carries no game texture), and the
// filter's content comparison. Pure: no D3D. tests/test_shadow_blob_model.py.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>
namespace NorthlightShadowBlobModel {
// A 32x32 grey disc centred at (15.5,15.5): alpha 255 inside radius 14.4 and 0
// outside; grey 160 up to r=6, rising linearly to 255 at r=12.5; colour 255
// where alpha is 0. Against the client's shadowblob.blp: mean alpha error 0,
// mean colour error 1.6 (the thresholds below are 6 and 24).
constexpr unsigned Width=32,Height=32;
inline std::vector<std::uint8_t> referencePixels(){
    std::vector<std::uint8_t> rgba(std::size_t(Width)*Height*4);
    for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){
        const double r=std::hypot(x-15.5,y-15.5);
        const bool inside=r<14.4;
        const double grey=!inside||r>=12.5?255.:r<=6.?160.:160.+95.*(r-6.)/(12.5-6.);
        std::uint8_t* p=&rgba[(std::size_t(y)*Width+x)*4];
        p[0]=p[1]=p[2]=std::uint8_t(grey+.5);p[3]=inside?255:0;
    }
    return rgba;
}
// 0.3.192 BlobShadowStrength: the same disc, lighter. The game draws the blob with a modulate blend
// (destination * texture colour), so only the colour matters and a lighter blob is a lighter colour:
// grey' = 255 - round(strength*(255-grey)/100), alpha unchanged, colour 255 where alpha is 0.
// strength 100 = the reference, 0 = white everywhere (no darkening). Bytes are B,G,R,A (A8R8G8B8 on
// little-endian); the grey is the same in all three colour channels, so they equal R,G,B,A.
inline std::vector<std::uint8_t> faintPixels(unsigned strength){
    if(strength>100)strength=100;
    std::vector<std::uint8_t> out=referencePixels();
    for(std::size_t i=0;i<out.size();i+=4){
        const unsigned darkness=255u-out[i];   // 0 where the colour is 255, so the outside stays 255
        const std::uint8_t grey=std::uint8_t(255u-(strength*darkness*2u+100u)/200u);
        out[i]=out[i+1]=out[i+2]=grey;
    }
    return out;
}
// Full mip chain (32,16,8,4,2,1), each level a 2x2 box filter of the one above (rounded). Straight
// (not alpha-premultiplied) filtering: under modulate the texel's colour is what darkens the
// target, and the outside is colour 255 (no darkening), so averaging the colour channels as
// stored gives exactly the average of what the full-size texels would draw; premultiplying would
// drop the white outside and darken every edge texel at distance. Alpha is averaged the same way
// (it is not used by the modulate blend, so mip alpha above 0 at the edge is harmless).
inline std::vector<std::vector<std::uint8_t>> faintMipChain(unsigned strength){
    std::vector<std::vector<std::uint8_t>> levels{faintPixels(strength)};
    for(unsigned size=Width/2;size>=1;size/=2){
        const std::vector<std::uint8_t>& above=levels.back();const unsigned aboveSize=size*2;
        std::vector<std::uint8_t> level(std::size_t(size)*size*4);
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x)for(unsigned c=0;c<4;++c){
            unsigned sum=0;for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx)sum+=above[(std::size_t(y*2+dy)*aboveSize+x*2+dx)*4+c];
            level[(std::size_t(y)*size+x)*4+c]=std::uint8_t((sum+2)/4);
        }
        levels.push_back(std::move(level));
    }
    return levels;
}
// Decoded pixels of a Width x Height texture against the reference. The blob
// is a flat grey disc with a hard alpha edge: alpha must match almost exactly;
// colour is compared loosely (channel order irrelevant).
inline bool matches(const std::uint8_t* decoded,const std::uint8_t* reference,std::size_t bytes){
    std::uint64_t alphaError=0,colorError=0;
    for(std::size_t i=0;i<bytes;i+=4){
        alphaError+=std::abs(int(decoded[i+3])-int(reference[i+3]));
        for(int c=0;c<3;++c)colorError+=std::abs(int(decoded[i+c])-int(reference[i+c]));
    }
    const std::size_t pixels=bytes/4;
    return alphaError/pixels<6&&colorError/(pixels*3)<24;
}
}
