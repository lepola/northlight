// 0.3.161 analytic blob reference (src/shadows/shadow_blob_model.h). Without an
// argument: shape, a BGRA8 decode round trip through NorthlightActorTexture::decode
// (the filter's path after LockRect) and rejections. With the client's decoded
// shadowblob.blp (32x32 RGBA8 file, test_shadow_blob_client.py): the model's
// error against it and accept/reject parity between the model and that real
// texture as the reference. No D3D, game or Wine.
// 0.3.171: the 16-bit decodes (A1R5G5B5 is how the game uploads shadowblob.blp),
// the model and every variant through an A1R5G5B5 upload, and with the client
// file the real texture through that upload.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <vector>
#include "actor_texture.h"
#include "shadow_blob_model.h"

using Pixels=std::vector<std::uint8_t>;
static const std::size_t Bytes=std::size_t(NorthlightShadowBlobModel::Width)*NorthlightShadowBlobModel::Height*4;
static bool accepted(const Pixels& texture,const Pixels& reference){return NorthlightShadowBlobModel::matches(texture.data(),reference.data(),Bytes);}
static Pixels disc(double radius,double cx,double cy,std::uint8_t grey){
    Pixels p(Bytes);
    for(unsigned i=0;i<Bytes/4;++i){const bool in=std::hypot(i%32-cx,i/32-cy)<radius;p[i*4]=p[i*4+1]=p[i*4+2]=in?grey:255;p[i*4+3]=in?255:0;}
    return p;
}
// Variants a 32x32 game texture might be: the reference itself, shifted, rescaled,
// recoloured, inverted, flat and noise.
static std::vector<Pixels> variants(const Pixels& base){
    std::vector<Pixels> out{base};
    for(int dx=-3;dx<=3;++dx)for(int dy=-3;dy<=3;++dy){
        Pixels p(Bytes);
        for(int y=0;y<32;++y)for(int x=0;x<32;++x)for(int c=0;c<4;++c){int sx=x-dx,sy=y-dy;p[(y*32+x)*4+c]=sx<0||sy<0||sx>31||sy>31?(c==3?0:255):base[(sy*32+sx)*4+c];}
        out.push_back(p);
    }
    for(double r=8;r<=16;r+=.5)for(int g:{0,100,160,200,255})out.push_back(disc(r,15.5,15.5,std::uint8_t(g)));
    for(int shift:{-120,-60,-30,30,60}){Pixels p=base;for(std::size_t i=0;i<Bytes;i+=4)for(int c=0;c<3;++c)p[i+c]=std::uint8_t(std::max(0,std::min(255,p[i+c]+shift)));out.push_back(p);}
    {Pixels p=base;for(std::size_t i=3;i<Bytes;i+=4)p[i]=std::uint8_t(255-p[i]);out.push_back(p);}
    {Pixels p(Bytes,255);out.push_back(p);Pixels z(Bytes,0);out.push_back(z);}
    std::uint32_t seed=161;for(int n=0;n<64;++n){Pixels p(Bytes);for(auto& b:p){seed=seed*1664525u+1013904223u;b=std::uint8_t(seed>>24);}out.push_back(p);}
    return out;
}
using NorthlightActorTexture::Format;
static std::array<std::uint8_t,4> one(std::uint16_t value,Format format){
    Pixels out;assert(NorthlightActorTexture::decode(&value,2,1,1,2,format,out)&&out.size()==4);return {out[0],out[1],out[2],out[3]};
}
// RGBA8 pixels as a 16-bit D3D upload (BGRA-ordered little-endian words), 8-bit
// channels truncated (or rounded) to 5 or 4 bits, then back through the filter's decode.
static Pixels upload(const Pixels& rgba,Format format,bool round=false){
    std::vector<std::uint16_t> words(rgba.size()/4);
    auto bits=[&](std::uint8_t v,unsigned n){unsigned m=(1u<<n)-1;return round?(unsigned(v)*m+127)/255:unsigned(v)>>(8-n);};
    for(std::size_t i=0;i<words.size();++i){const std::uint8_t* p=&rgba[i*4];
        words[i]=std::uint16_t(format==Format::ARGB4444?bits(p[3],4)<<12|bits(p[0],4)<<8|bits(p[1],4)<<4|bits(p[2],4)
            :(format==Format::ARGB1555&&p[3]>=128?0x8000u:0u)|bits(p[0],5)<<10|bits(p[1],5)<<5|bits(p[2],5));}
    Pixels decoded;assert(NorthlightActorTexture::decode(words.data(),words.size()*2,32,32,64,format,decoded)&&decoded.size()==Bytes);return decoded;
}
static void sixteenBit(){
    using A=std::array<std::uint8_t,4>;
    assert((one(0xffff,Format::ARGB1555)==A{255,255,255,255}));
    assert((one(0x7c00,Format::ARGB1555)==A{255,0,0,0}));
    assert((one(0x7c00,Format::XRGB1555)==A{255,0,0,255}));
    assert((one(0x8000|1<<10|16<<5|2,Format::ARGB1555)==A{8,132,16,255}));  // 5-bit: (v<<3)|(v>>2)
    assert((one(0x8f31,Format::ARGB4444)==A{255,51,17,136}));
    assert((one(0x0000,Format::ARGB4444)==A{0,0,0,0}));
    for(Format f:{Format::ARGB1555,Format::XRGB1555,Format::ARGB4444,Format::RGB565})assert(NorthlightActorTexture::rowBytes(5,f)==10&&NorthlightActorTexture::rowCount(7,f)==7);
    assert(NorthlightActorTexture::rowBytes(5,Format::BGRA8)==20&&NorthlightActorTexture::rowBytes(5,Format::BC1)==16&&NorthlightActorTexture::rowBytes(5,Format::BC3)==32&&NorthlightActorTexture::rowCount(7,Format::BC2)==2);
    // Padded rows and short sources.
    std::uint16_t padded[6]={0x8000|31,0x1f,0,0x7fff,0xffff,0};Pixels out;
    assert(NorthlightActorTexture::decode(padded,sizeof padded,2,2,6,Format::ARGB1555,out)&&out.size()==16);
    assert(out[2]==255&&out[3]==255&&out[6]==255&&out[7]==0&&out[8]==255&&out[11]==0&&out[12]==255&&out[15]==255);
    assert(!NorthlightActorTexture::decode(padded,10,2,2,6,Format::ARGB1555,out)&&out.empty());
    assert(!NorthlightActorTexture::decode(padded,sizeof padded,2,2,3,Format::ARGB4444,out));
}
int main(int argc,char** argv){
    const Pixels model=NorthlightShadowBlobModel::referencePixels();
    assert(model.size()==Bytes);
    unsigned opaque=0;
    for(std::size_t i=0;i<Bytes;i+=4){assert(model[i]==model[i+1]&&model[i]==model[i+2]);assert(model[i+3]==0||model[i+3]==255);opaque+=model[i+3]==255;}
    assert(opaque==648);
    assert(model[(15*32+15)*4]==160&&model[0]==255&&model[3]==0);
    // The filter's decode path: the texture as the game's A8R8G8B8 upload (BGRA in memory).
    Pixels bgra(Bytes),decoded;
    for(std::size_t i=0;i<Bytes;i+=4){bgra[i]=model[i+2];bgra[i+1]=model[i+1];bgra[i+2]=model[i];bgra[i+3]=model[i+3];}
    assert(NorthlightActorTexture::decode(bgra.data(),bgra.size(),32,32,32*4,NorthlightActorTexture::Format::BGRA8,decoded));
    assert(decoded.size()==Bytes&&accepted(decoded,model));
    unsigned rejected=0;for(auto& v:variants(model))rejected+=!accepted(v,model);
    assert(rejected>=100);
    std::printf("model: 32x32, %u opaque, BGRA8 round trip accepted, %u of %zu variants rejected\n",opaque,rejected,variants(model).size());
    // 0.3.193 faint disc: s=100 is the reference, s=0 is white (no darkening) everywhere, alpha never changes, colour stays 255
    // where alpha is 0, the grey follows 255-round(s*(255-grey)/100) and only gets lighter as s falls; the mip chain is 32..1, colour box filtered, alpha thresholded to 0/255.
    {using namespace NorthlightShadowBlobModel;
     assert(faintPixels(100)==model&&faintPixels(250)==model);
     const Pixels white=faintPixels(0);unsigned whiteOpaque=0;
     for(std::size_t i=0;i<Bytes;i+=4){assert(white[i]==255&&white[i+1]==255&&white[i+2]==255&&white[i+3]==model[i+3]);whiteOpaque+=white[i+3]>0;}
     assert(whiteOpaque==648);
     unsigned previousSum=0;
     for(unsigned strength=0;strength<=100;++strength){const Pixels f=faintPixels(strength);assert(f.size()==Bytes);unsigned sum=0;
         for(std::size_t i=0;i<Bytes;i+=4){assert(f[i]==f[i+1]&&f[i]==f[i+2]&&f[i+3]==model[i+3]);
             if(model[i+3]==0)assert(f[i]==255);
             const int expected=255-int(std::lround(strength*(255-model[i])/100.0));assert(std::abs(int(f[i])-expected)<=0&&f[i]<=255);
             sum+=255-f[i];}
         assert(sum>=previousSum);previousSum=sum;}   // darker (more total darkness) as the strength rises
     assert(faintPixels(50)[(15*32+15)*4]==255-int(std::lround(50*95/100.0)));  // centre grey 160 -> 207 or 208
     for(unsigned strength:{0u,1u,25u,50u,99u,100u}){const auto chain=faintMipChain(strength);assert(chain.size()==6);
         const unsigned sizes[6]={32,16,8,4,2,1};
         for(unsigned level=0;level<6;++level)assert(chain[level].size()==std::size_t(sizes[level])*sizes[level]*4);
         assert(chain[0]==faintPixels(strength));
         for(unsigned level=1;level<6;++level){const unsigned size=sizes[level];
             for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x)for(unsigned c=0;c<4;++c){
                 unsigned sum=0;for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx)sum+=chain[level-1][(std::size_t(y*2+dy)*size*2+x*2+dx)*4+c];
                 const unsigned mean=(sum+2)/4;   // colour: straight box filter; alpha: thresholded to the game's 1-bit alpha
                 assert(chain[level][(std::size_t(y)*size+x)*4+c]==(c==3?(mean>=128?255u:0u):mean));}}
         for(unsigned level=0;level<6;++level)for(std::size_t i=3;i<chain[level].size();i+=4)assert(chain[level][i]==0||chain[level][i]==255);   // binary alpha at every level
         // Straight filtering: an edge texel never gets darker than its darkest source texel, and fully outside blocks stay 255.
         for(unsigned level=1;level<6;++level)for(std::size_t i=0;i<chain[level].size();i+=4)assert(chain[level][i]>=*std::min_element(chain[0].begin(),chain[0].end()));
         assert(chain[0][0]==255&&chain[1][0]==255&&chain[2][0]==255);   // the corner is outside the disc down to 4x4 texel blocks
         if(strength==0)for(unsigned level=0;level<6;++level)for(std::size_t i=0;i<chain[level].size();i+=4)assert(chain[level][i]==255);
         // The 1x1 level is the mean colour of the whole level 0 (within rounding of the 5 halvings).
         double mean=0;for(std::size_t i=0;i<Bytes;i+=4)mean+=chain[0][i];mean/=Bytes/4;assert(std::abs(double(chain[5][0])-mean)<=2.5);}
     std::printf("faint: s=100 is the reference, s=0 white, alpha unchanged, outside 255, mip chain 32..1 colour box filtered, binary alpha\n");}
    sixteenBit();
    // The model as A1R5G5B5 / A4R4G4B4 uploads is accepted. The filter's 16-bit formats
    // add no claims: through A1R5G5B5 every variant keeps its 8-bit verdict, and
    // X1R5G5B5 (no alpha) can only lose accepts. A4R4G4B4 is decoded (GI) but not
    // mapped by the filter: its 17-level colour steps pull a 30-darker disc under the
    // colour threshold, a new accept.
    for(bool round:{false,true})for(Format f:{Format::ARGB1555,Format::ARGB4444})assert(accepted(upload(model,f,round),model));
    const auto modelVariants=variants(model);
    for(Format f:{Format::ARGB1555,Format::ARGB4444,Format::XRGB1555}){
        const char* name=f==Format::ARGB1555?"A1R5G5B5":f==Format::ARGB4444?"A4R4G4B4":"X1R5G5B5";
        unsigned kept=0,added=0,accepts=0;
        for(auto& v:modelVariants){const bool a=accepted(v,model),b=accepted(upload(v,f),model);kept+=a==b;added+=b&&!a;accepts+=b;
            if(a!=b)std::printf("%s verdict differs: 8-bit=%d 16-bit=%d\n",name,a,b);}
        std::printf("16-bit %s: %u of %zu variants keep their verdict, %u accepted, %u new accepts%s\n",name,kept,modelVariants.size(),accepts,added,f==Format::ARGB4444?" (not a filter format)":"");
        if(f!=Format::ARGB4444)assert(!added&&(f==Format::XRGB1555||kept==modelVariants.size()));
    }
    std::puts("16-bit: A1R5G5B5/X1R5G5B5/A4R4G4B4 decode exact; model accepted as A1R5G5B5 and A4R4G4B4");
    if(argc<2)return 0;
    FILE* f=std::fopen(argv[1],"rb");assert(f);Pixels real(Bytes);assert(std::fread(real.data(),1,Bytes,f)==Bytes);std::fclose(f);
    double alpha=0,colour=0;
    for(std::size_t i=0;i<Bytes;i+=4){alpha+=std::abs(real[i+3]-model[i+3]);for(int c=0;c<3;++c)colour+=std::abs(real[i+c]-model[i+c]);}
    alpha/=Bytes/4;colour/=Bytes/4*3;
    std::printf("client shadowblob.blp vs model: mean alpha error %.3f, mean colour error %.3f (thresholds 6, 24)\n",alpha,colour);
    assert(alpha==0&&colour<4&&accepted(real,model));
    // The real texture as the game uploads it (1-bit alpha: A1R5G5B5), through the filter's decode.
    for(bool round:{false,true}){const Pixels uploaded=upload(real,Format::ARGB1555,round);double a=0,c=0;
        for(std::size_t i=0;i<Bytes;i+=4){a+=std::abs(uploaded[i+3]-model[i+3]);for(int k=0;k<3;++k)c+=std::abs(uploaded[i+k]-model[i+k]);}
        std::printf("client shadowblob.blp as A1R5G5B5 (%s): mean alpha error %.3f, mean colour error %.3f, accepted %d\n",round?"rounded":"truncated",a/(Bytes/4),c/(Bytes/4*3),int(accepted(uploaded,model)));
        assert(a==0&&accepted(uploaded,model));}
    // Parity: every variant gets the same verdict against the model as against the real texture.
    unsigned same=0,accepts=0;const auto all=[&]{auto a=variants(real);for(auto& v:variants(model))a.push_back(v);return a;}();
    for(auto& v:all){const bool a=accepted(v,real),b=accepted(v,model);if(a==b)++same;else std::printf("parity differs: real=%d model=%d\n",a,b);accepts+=b;}
    std::printf("parity: %u of %zu variants same verdict (%u accepted)\n",same,all.size(),accepts);
    assert(same==all.size());
    return 0;
}
