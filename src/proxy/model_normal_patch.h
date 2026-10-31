#pragma once
#include "water_shader_patch.h"

namespace NorthlightModelNormal {
// Called only for a signature-recognized M2 family. Copy the normal the
// native lighting actually consumes, after its complete bone transform/NRM.
inline bool vertex(const std::uint32_t* code,size_t count,std::vector<std::uint32_t>& out){
    using namespace NorthlightWaterShaderPatch;
    Analysis a;if(analyse(code,count,a)||!a.vertex||(a.isUsed(6,5)&&a.major==2))return false;
    bool normalized[32]={};unsigned normal=32,hits=0;
    for(size_t p=1;p<count;){unsigned op=code[p]&65535,n=op==65534?(code[p]>>16)&32767:(code[p]>>24)&15;
        if(op==65535)break;
        if(op==31&&n==2&&regType(code[p+2])==6&&(code[p+1]&15)==5&&((code[p+1]>>16)&15)==5)return false;
        if(op==8&&n==3)for(unsigned s=2;s<=3;++s){
            const auto light=code[p+s],value=code[p+5-s];
            if(regType(light)==2&&regIndex(light)==12&&((light>>24)&15)==1&&((light>>16)&255)==0xe4&&
               regType(value)==0&&regIndex(value)<32&&((value>>16)&255)==0xe4&&normalized[regIndex(value)]){normal=regIndex(value);++hits;}
        }
        if(op!=31&&op!=81&&op!=48&&op!=47&&op!=65534&&n&&regType(code[p+1])==0&&regIndex(code[p+1])<32&&((code[p+1]>>16)&7))
            normalized[regIndex(code[p+1])]=op==36&&((code[p+1]>>16)&7)==7;
        p+=n+1;
    }
    if(hits!=1||patch(code,count,out))return false;
    unsigned varying=5;
    if(a.major==3){varying=0;while(varying<12&&(a.isUsed(6,varying)||varying==a.tex))++varying;if(varying==12)return false;}
    for(size_t p=1;p<out.size();){unsigned op=out[p]&65535,n=op==65534?(out[p]>>16)&32767:(out[p]>>24)&15;
        if(op==8&&n==3){
            bool match=false;for(unsigned s=2;s<=3;++s)match|=regType(out[p+s])==2&&regIndex(out[p+s])==12&&((out[p+s]>>24)&15)==1;
            if(match){out.insert(out.begin()+p,{0x02000001u,dst(6,varying,7),src(0,normal)});break;}
        }p+=n+1;
    }
    if(a.major==3)out.insert(out.begin()+a.declEnd,{0x0200001fu,0x80050005u,dst(6,varying,7)});
    return true;
}
}
