#pragma once
#include "water_shader_patch.h"

namespace NorthlightAutoBumpVertex {
// Capture the complete referenced bank. Relative and matrix operands retain
// the full bank because their register reach is not explicit in every token.
inline void constantRange(const std::uint32_t* code,size_t size,unsigned& first,unsigned& count){
    using namespace NorthlightWaterShaderPatch;
    first=0;count=256;Analysis a;if(analyse(code,size,a)||!a.vertex||a.relative)return;
    unsigned low=256,high=0;
    for(size_t p=1;p<size;){const unsigned op=code[p]&65535;if(op==65535)break;
        const unsigned n=op==65534?(code[p]>>16)&32767:(code[p]>>24)&15;
        if(op>=20&&op<=24)return;
        if(op!=65534&&op!=31&&op!=81&&op!=48&&op!=47)for(unsigned i=1;i<=n;++i)if(regType(code[p+i])==2){
            const unsigned index=regIndex(code[p+i]);if(index>=256)return;low=std::min(low,index);high=std::max(high,index);}
        p+=n+1;
    }
    first=low<256?low:0;count=low<256?high-low+1:1;
}
// Terrain's native c0..c2 rows transform world vectors into view space.
// Forward the authored smooth normal, without touching position or native colour.
inline bool terrainNormal(const std::uint32_t* code,size_t count,std::vector<std::uint32_t>& patched){
    using namespace NorthlightWaterShaderPatch;
    Analysis a;if(analyse(code,count,a)||!a.vertex)return false;
    unsigned normal=~0u;bool occupied=false;
    for(size_t p=1;p<count;){const unsigned op=code[p]&65535;if(op==65535)break;
        const unsigned n=op==65534?(code[p]>>16)&32767:(code[p]>>24)&15;
        if(op==31&&n==2){const unsigned usage=code[p+1]&15,index=(code[p+1]>>16)&15,type=regType(code[p+2]);
            if(type==1&&usage==3&&index==0)normal=regIndex(code[p+2]);
            if(type==6&&usage==5&&index==5)occupied=true;
        }p+=n+1;
    }
    if(normal==~0u||occupied||(a.major==2&&a.isUsed(6,5)))return false;
    unsigned output=5;
    if(a.major==3){output=0;while(output<12&&(a.isUsed(6,output)||output==a.tex))++output;if(output==12)return false;}
    std::vector<Word> out=patched;if(out.empty()||out.back()!=65535)return false;
    if(a.major==3){const Word decl[]={0x0200001f,0x80050005u,dst(6,output,7)};out.insert(out.begin()+a.declEnd,decl,decl+3);}
    out.pop_back();
    const Word suffix[]={0x03000005,dst(0,a.spare,7),src(1,normal,0x55),src(2,1),
        0x04000004,dst(0,a.spare,7),src(1,normal,0),src(2,0),src(0,a.spare),
        0x04000004,dst(6,output,7),src(1,normal,0xaa),src(2,2),src(0,a.spare),65535};
    out.insert(out.end(),std::begin(suffix),std::end(suffix));patched.swap(out);return true;
}
}
