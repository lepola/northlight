#pragma once
#include "water_shader_patch.h"
#include <array>
namespace NorthlightDisplacementPatch {
using namespace NorthlightWaterShaderPatch;
struct Binding {unsigned constant=0,count=4,sampler=0;};
inline bool patch(const Word* w,size_t count,std::vector<Word>& out,Binding& binding,bool skinned=false,bool gpuHeight=false){
    out.clear();if(!w||count<2||(w[0]!=0xfffe0200u&&w[0]!=0xfffe0300u))return false;
    if(gpuHeight&&w[0]!=0xfffe0300u)return false;
    bool usedTemp[32]={},usedInput[16]={},usedConst[256]={},usedSampler[4]={};unsigned position=16,palette=256;size_t declarationEnd=1,inputEnd=0;bool ended=false;
    for(size_t at=1;at<count;){const unsigned op=w[at]&65535,n=op==65534?(w[at]>>16)&32767:(w[at]>>24)&15;if(op==65535){ended=at+1==count;break;}if(at+n>=count)return false;
        if(op==31){if(n!=2)return false;declarationEnd=at+3;auto type=regType(w[at+2]),index=regIndex(w[at+2]);if(type==10&&index<4)usedSampler[index]=true;if(type==1){inputEnd=at+3;if(index>=16)return false;usedInput[index]=true;if((w[at+1]&15)==0&&((w[at+1]>>16)&15)==0)position=index;if((w[at+1]&15)==5&&(((w[at+1]>>16)&15)==7||(gpuHeight&&((w[at+1]>>16)&15)==6)))return false;}}
        else if(op==81){if(n!=5||regIndex(w[at+1])>=256)return false;usedConst[regIndex(w[at+1])]=true;}
        else if(op!=65534)for(unsigned k=1;k<=n;++k){auto a=w[at+k];if(!(a&0x80000000u))continue;const auto type=regType(a),index=regIndex(a);if(a&0x2000){if(!skinned||type!=2)return false;palette=std::min(palette,index);}if(type==0&&index<32)usedTemp[index]=true;if(type==2&&index<256){usedConst[index]=true;if(op>=20&&op<=24)for(unsigned r=1;r<4&&index+r<256;++r)usedConst[index+r]=true;}}
        at+=n+1;
    }
    if(!ended||position>=16)return false;
    unsigned input=0;while(input<16&&usedInput[input])++input;unsigned a=0,b=0;while(a<12&&usedTemp[a])++a;if(a==12||input==16)return false;usedTemp[a]=true;while(b<12&&usedTemp[b])++b;if(b==12)return false;
    unsigned uvInput=0,sample=0,sampler=0;
    if(gpuHeight){usedInput[input]=true;usedTemp[b]=true;while(uvInput<16&&usedInput[uvInput])++uvInput;while(sample<12&&usedTemp[sample])++sample;while(sampler<4&&usedSampler[sampler])++sampler;if(uvInput==16||sample==12||sampler==4)return false;binding.sampler=sampler;}
    for(unsigned i=palette;i<256;++i)usedConst[i]=true;
    binding.count=(skinned?1:4)+unsigned(gpuHeight);unsigned c=0;for(;;){bool free=true;for(unsigned i=0;i<binding.count;++i)free&=!usedConst[c+i];if(free)break;if(++c>256-binding.count)return false;}
    binding.constant=c;
    out.assign(w,w+declarationEnd);out.insert(out.begin()+inputEnd,{0x0200001fu,0x80070005u,dst(1,input)});
    if(gpuHeight){out.insert(out.begin()+inputEnd,{0x0200001fu,0x80060005u,dst(1,uvInput)});out.insert(out.end(),{0x0200001fu,0x90000000u,dst(10,sampler)});}
    auto emit=[&](std::initializer_list<Word> op){out.insert(out.end(),op);};
    if(!skinned){for(unsigned i=0;i<3;++i)emit({0x03000009u,dst(0,b,1u<<i),src(1,position),src(2,c+i)});
    emit({0x03000008u,dst(0,b,1),src(0,b),src(0,b)});
    emit({0x04000004u,dst(0,b,1),src(0,b,0),src(2,c+3,0x55),src(2,c+3,0xaa)});
    emit({0x02000001u,dst(0,a,1),src(2,c+3,0xff)});
    emit({0x0300000du,dst(0,a,2),src(0,a,0),src(0,a,0)});
    emit({0x0300000bu,dst(0,b,1),src(0,b,0),src(0,a,0)});
    emit({0x0300000au,dst(0,b,1),src(0,b,0),src(0,a,0x55)});
    emit({0x03000005u,dst(0,b,1),src(0,b,0),src(2,c+3,0)});
    }
    if(gpuHeight){
        const unsigned midpoint=c+(skinned?1:4);
        emit({0x02000001u,dst(0,sample),src(2,midpoint,0x55)});
        emit({0x02000001u,dst(0,sample,3),src(1,uvInput,0x44)});
        emit({0x0300005fu,dst(0,sample),src(0,sample),src(10,sampler)});
        emit({0x03000002u,dst(0,sample,1),src(0,sample,0),src(2,midpoint,0)^0x01000000u});
        emit({0x03000005u,dst(0,sample,7),src(1,input),src(0,sample,0)});
    }
    emit({0x02000001u,dst(0,a),src(1,position)});
    emit({0x04000004u,dst(0,a,7),gpuHeight?src(0,sample):src(1,input),skinned?src(2,c,0):src(0,b,0),src(0,a)});
    for(size_t at=declarationEnd;at<count;){const unsigned op=w[at]&65535,n=op==65534?(w[at]>>16)&32767:(w[at]>>24)&15;out.push_back(w[at]);
        for(unsigned k=1;k<=n;++k){Word token=w[at+k];if(op!=65534&&op!=81&&op!=31&&k>1&&regType(token)==1&&regIndex(token)==position)token=replaceReg(token,0,a);out.push_back(token);}if(op==65535)break;at+=n+1;}
    return true;
}
} // namespace NorthlightDisplacementPatch
