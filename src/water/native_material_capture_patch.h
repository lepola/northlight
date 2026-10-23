#pragma once
#include "patch_terrain_shadow_coverage.h"
#include "water_shader_patch.h"
#include "auto_bump_compiled_shaders.h"

namespace NorthlightNativeMaterialCapture {
using namespace NorthlightWaterShaderPatch;
// Run the original combiner to retain its exact cutout alpha, then append the
// normal/occlusion capture. Original constants, UVs and samplers keep their ABI.
inline bool patch(const Word* code,size_t count,std::vector<Word>& out,unsigned& control){
    NorthlightTerrainCoverage::Program native,capture;if(!NorthlightTerrainCoverage::analyse(code,count,native)||native.vertex||!NorthlightTerrainCoverage::analyse(reinterpret_cast<const Word*>(kBumpNativeShader),sizeof(kBumpNativeShader)/sizeof(DWORD),capture)||native.slots+capture.slots>512)return false;
    int uv=-1;bool samplers[16]={},integers[16]={};
    for(const auto& op:native.ops){
        if(op.code==31&&regType(code[op.at+2])==1&&(code[op.at+1]&15)==5){unsigned index=(code[op.at+1]>>16)&15;if(index==7)return false;if(!index)uv=int(regIndex(code[op.at+2]));}
        for(unsigned i=op.code==31?2:1;i<=op.size;++i){if((op.code==81||op.code==48||op.code==47)&&i>1)break;Word t=code[op.at+i];unsigned type=regType(t),r=regIndex(t);if(type==10&&r<16)samplers[r]=true;if(type==7&&r<16)integers[r]=true;if(type==9||(type==8&&r))return false;}}
    if(uv<0||samplers[14])return false;
    unsigned varying=0;while(varying<10&&native.inputs[varying])++varying;if(varying==10)return false;
    auto temp=[&](){unsigned r=0;while(r<32&&native.temps[r])++r;if(r<32)native.temps[r]=true;return r;};
    const unsigned color=temp();if(color==32)return false;
    control=128;for(;;){bool free=true;for(unsigned i=0;i<5;++i)free&=!native.constants[control+i];if(free)break;if(++control>187)return false;}
    for(unsigned i=0;i<5;++i)native.constants[control+i]=true;
    unsigned constants[256],temps[32],inputs[16],integerMap[16];for(unsigned i=0;i<256;++i)constants[i]=i;std::fill(std::begin(temps),std::end(temps),32u);std::fill(std::begin(inputs),std::end(inputs),16u);std::fill(std::begin(integerMap),std::end(integerMap),16u);
    for(unsigned i=0;i<5;++i)constants[20+i]=control+i;
    const auto* prefix=reinterpret_cast<const Word*>(kBumpNativeShader);constexpr size_t length=sizeof(kBumpNativeShader)/sizeof(DWORD);
    unsigned nativeColorInput=16;
    for(size_t at=1;at+1<length;){unsigned op=prefix[at]&65535,n=op==65534?(prefix[at]>>16)&32767:(prefix[at]>>24)&15;
        if(op==31&&regType(prefix[at+2])==1){unsigned semantic=prefix[at+1]&15,index=(prefix[at+1]>>16)&15,r=regIndex(prefix[at+2]);if(semantic==10)nativeColorInput=r;else if(semantic==5)inputs[r]=index==7?varying:unsigned(uv);else return false;}
        if(op==81){unsigned c=192;while(c<256&&native.constants[c])++c;if(c==256)return false;native.constants[c]=true;constants[regIndex(prefix[at+1])]=c;}
        if(op!=65534)for(unsigned i=op==31?2:1;i<=n;++i){if((op==81||op==48||op==47)&&i>1)break;Word t=prefix[at+i];if(regType(t)==0&&temps[regIndex(t)]==32){unsigned r=temp();if(r==32)return false;temps[regIndex(t)]=r;}if(regType(t)==7&&integerMap[regIndex(t)]==16){unsigned r=0;while(r<16&&integers[r])++r;if(r==16)return false;integers[r]=true;integerMap[regIndex(t)]=r;}}at+=n+1;}
    if(nativeColorInput==16)return false;
    auto mapped=[&](Word t){unsigned type=regType(t),r=regIndex(t);if(type==0)return replaceReg(t,0,temps[r]);if(type==1)return replaceReg(t,r==nativeColorInput?0:1,r==nativeColorInput?color:inputs[r]);if(type==2)return replaceReg(t,2,constants[r]);if(type==7)return replaceReg(t,7,integerMap[r]);if(type==10)return replaceReg(t,10,14);return t;};
    out.assign(code,code+count-1);
    for(const auto& op:native.ops)if(op.code!=65534&&op.code!=31&&op.code!=81&&op.code!=48&&op.code!=47)for(unsigned i=1;i<=op.size;++i){Word& t=out[op.at+i];if(regType(t)==8&&regIndex(t)==0)t=replaceReg(t,0,color);}
    out.insert(out.begin()+native.declEnd,{0x0200001fu,0x80070005u,dst(1,varying),0x0200001fu,0x90000000u,dst(10,14)});
    std::vector<Word> defs;
    for(size_t at=1;at+1<length;){unsigned op=prefix[at]&65535,n=op==65534?(prefix[at]>>16)&32767:(prefix[at]>>24)&15;
        if(op==81||op==48||op==47){defs.push_back(prefix[at]);defs.push_back(mapped(prefix[at+1]));defs.insert(defs.end(),prefix+at+2,prefix+at+n+1);}
        else if(op!=65534&&op!=31){out.push_back(prefix[at]);for(unsigned i=1;i<=n;++i)out.push_back(mapped(prefix[at+i]));}at+=n+1;}
    out.insert(out.begin()+1,defs.begin(),defs.end());out.push_back(65535);return true;
}
}
