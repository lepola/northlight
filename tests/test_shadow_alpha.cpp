#include "shadow_alpha.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <random>
using namespace NorthlightShadowAlpha;
using V=std::array<float,4>;
static bool nativeKill(const std::vector<uint32_t>& w,float color,float tex,float ref){
    V temp[32]={},constants[224]={},inputs[16]={};for(auto& c:constants)c.fill(ref);
    for(auto& v:inputs)v={.3f,.5f,.7f,color};
    auto read=[&](uint32_t token){V out{};unsigned r=token&2047,k=type(token);V value=k==0?temp[r]:k==2?constants[r]:inputs[r];
        for(unsigned c=0;c<4;++c){out[c]=value[(token>>(16+c*2))&3];if(((token>>24)&15)==1)out[c]=-out[c];}return out;};
    for(size_t p=1;p<w.size();){unsigned op=w[p]&65535;if(op==65535)break;if(op==65534){p+=1+((w[p]>>16)&32767);continue;}
        unsigned n=(w[p]>>24)&15;const auto* a=w.data()+p+1;p+=1+n;
        if(op==31)continue;if(op==81){for(unsigned c=0;c<4;++c){union {uint32_t u;float f;} x{a[c+1]};constants[a[0]&2047][c]=x.f;}continue;}
        if(op==65){const auto v=read((a[0]&~0x00ff0000u)|0x00e40000u);return v[0]<0||v[1]<0||v[2]<0;}
        V out{};
        if(op==66)out={.2f,.4f,.6f,tex};
        else if(op==1||op==2||op==3||op==4||op==5){
            V x=read(a[1]),y=n>=3?read(a[2]):V{},z=n>=4?read(a[3]):V{};
            for(unsigned c=0;c<4;++c)out[c]=op==1?x[c]:op==2?x[c]+y[c]:op==3?x[c]-y[c]:op==5?x[c]*y[c]:x[c]*y[c]+z[c];}
        else out.fill(NAN);
        assert(type(a[0])==0);for(unsigned c=0;c<4;++c)if(a[0]&(1u<<(16+c)))temp[a[0]&2047][c]=out[c];
    }assert(false);return false;
}
int main(int argc,char** argv){
    assert(argc==2);FILE* f=std::fopen(argv[1],"rb");assert(f);unsigned total=0,discard=0,known=0,product=0,color=0;uint32_t bytes;std::mt19937 rng(71);
    while(std::fread(&bytes,4,1,f)==1){std::vector<uint32_t>w(bytes/4);assert(std::fread(w.data(),1,bytes,f)==bytes);auto info=analyze(w.data(),w.size());++total;discard+=info.discard;
        if(!info.known)continue;++known;product+=info.mode==TextureColor;color+=info.mode==Color;
        for(unsigned i=0;i<200;++i){float c=float(rng()%1001)/1000,t=float(rng()%1001)/1000,r=float(rng()%1001)/1000;
            assert(nativeKill(w,c,t,r)==((info.mode==TextureColor?c*t:c)-r<0));}
        auto broken=w; // A second discard or a different sampler is never silently accepted.
        broken.insert(broken.end()-1,{0x01000041u,0x800f0000u});assert(!analyze(broken.data(),broken.size()).known);
        if(info.mode==TextureColor){for(size_t p=1;p+3<broken.size();){unsigned op=broken[p]&65535;if(op==65534){p+=1+((broken[p]>>16)&32767);continue;}unsigned n=(broken[p]>>24)&15;if(op==66){broken[p+3]=(broken[p+3]&~2047u)|1;break;}p+=1+n;}assert(!analyze(broken.data(),broken.size()).known);}
    }std::fclose(f);std::fprintf(stderr,"census %u %u %u %u\n",total,known,product,color);assert(total>100&&known>100&&product>20&&color>20);
    std::printf("PASS shader corpus: %u shaders, %u discard, %u proved (%u texture*color, %u color); 200 randomized mask checks each; unknown/multiple/sampler changes rejected\n",total,discard,known,product,color);
}
