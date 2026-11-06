#pragma once
#include "patch_terrain_shadow_coverage.h"
#include "auto_bump_compiled_shaders.h"

namespace NorthlightParallaxPatch {
using Word=std::uint32_t;
using NorthlightWaterShaderPatch::dst;
using NorthlightWaterShaderPatch::src;
using NorthlightShadowShader::regType;
using NorthlightShadowShader::regIndex;
using NorthlightShadowShader::replaceReg;

// The caller proves the original material family. Only diffuse lookups change;
// the original shader's lighting, masks, alpha, fog and shadow coverage remain.
inline bool pixel(const Word* code,size_t count,bool dual,std::vector<Word>& result,unsigned* constantBase=nullptr,bool shared=false,unsigned* sharedResultRegister=nullptr) {
    NorthlightTerrainCoverage::Program native;
    if(!NorthlightTerrainCoverage::analyse(code,count,native)||native.vertex||native.slots>190)return false;
    const unsigned limit=shared?190:191;
    unsigned control=160;while(control<limit&&(native.constants[control]||native.constants[control+1]||(shared&&native.constants[control+2])))++control;if(control==limit)return false;
    int uv[2]={-1,-1};bool samplers[16]={},integers[16]={},position=false;size_t first=count-1;
    for(const auto& op:native.ops){
        if(op.code==31){
            const Word semantic=code[op.at+1],reg=code[op.at+2];
            if(regType(reg)==17)position=true;
            if(regType(reg)==1&&(semantic&15)==5){unsigned index=(semantic>>16)&15;if(index==7||(shared&&(index==6||(index<2&&(reg&0x00600000)))))return false;if(index<2)uv[index]=int(regIndex(reg));}
        }else if(op.code!=81&&op.code!=48&&op.code!=47)first=std::min(first,op.at);
        for(unsigned i=1;i<=op.size;++i){if((op.code==81||op.code==48||op.code==47)&&i>1)break;if(op.code==31&&i==1)continue;
            const Word t=code[op.at+i];if(regType(t)==10&&regIndex(t)<16)samplers[regIndex(t)]=true;if(regType(t)==7&&regIndex(t)<16)integers[regIndex(t)]=true;}
    }
    if(uv[0]<0||(dual&&uv[1]<0)||samplers[14]||(dual&&samplers[15])||(shared&&(samplers[12]||samplers[13])))return false;
    unsigned varying=0;while(varying<10&&native.inputs[varying])++varying;if(varying==10)return false;
    native.inputs[varying]=true;unsigned primitive=0;if(shared){while(primitive<10&&native.inputs[primitive])++primitive;if(primitive==10)return false;}
    unsigned integer=0;while(integer<16&&integers[integer])++integer;if(integer==16)return false;
    auto allocate=[&](){unsigned i=0;while(i<32&&native.temps[i])++i;if(i<32)native.temps[i]=true;return i;};
    const unsigned layers=dual?2:1;unsigned output[2],gx[2],gy[2];
    for(unsigned n=0;n<layers;++n){output[n]=allocate();gx[n]=allocate();gy[n]=allocate();if(gy[n]==32)return false;}
    unsigned temps[32];std::fill(std::begin(temps),std::end(temps),32u);
    unsigned constants[256];for(unsigned i=0;i<256;++i)constants[i]=i;
    constants[220]=control;constants[221]=control+1;constants[222]=control+2;
    const Word* prefix=reinterpret_cast<const Word*>(shared?kParallaxSharedUVShader:kParallaxUVShader);const size_t length=shared?sizeof(kParallaxSharedUVShader)/sizeof(DWORD):sizeof(kParallaxUVShader)/sizeof(DWORD);
    unsigned prefixInputs[16]={};for(size_t at=1;at+1<length;){unsigned op=prefix[at]&65535,n=op==65534?(prefix[at]>>16)&32767:(prefix[at]>>24)&15;
        if(op==31&&regType(prefix[at+2])==1)prefixInputs[regIndex(prefix[at+2])]=(prefix[at+1]>>16)&15;at+=n+1;}
    // Reserve the compiler's private DEF registers without touching native constants.
    for(size_t at=1;at+1<length;){unsigned op=prefix[at]&65535,n=op==65534?(prefix[at]>>16)&32767:(prefix[at]>>24)&15;
        if(op==81){unsigned c=192;while(c<220&&native.constants[c])++c;if(c==220)return false;constants[regIndex(prefix[at+1])]=c;native.constants[c]=true;}
        if(op!=65534)for(unsigned i=1;i<=n;++i){if((op==81||op==48||op==47)&&i>1)break;if(op==31&&i==1)continue;Word t=prefix[at+i];if(regType(t)==0&&temps[regIndex(t)]==32){unsigned r=allocate();if(r==32)return false;temps[regIndex(t)]=r;}}
        at+=n+1;
    }
    std::vector<Word> out(code,code+first);
    out.insert(out.end(),{0x0200001f,0x80070005,dst(1,varying),0x0200001f,0x90000000,dst(10,14)});
    if(dual)out.insert(out.end(),{0x0200001f,0x90000000,dst(10,15)});
    if(shared){out.insert(out.end(),{0x0200001f,0x80060005,dst(1,primitive,1),0x0200001f,0x90000000,dst(10,12)});if(dual)out.insert(out.end(),{0x0200001f,0x90000000,dst(10,13)});
        if(!position)for(size_t at=1;at+1<length;){unsigned op=prefix[at]&65535,n=op==65534?(prefix[at]>>16)&32767:(prefix[at]>>24)&15;if(op==31&&regType(prefix[at+2])==17)out.insert(out.end(),prefix+at,prefix+at+n+1);at+=n+1;}}
    auto mapped=[&](Word token,unsigned layer){unsigned type=regType(token),index=regIndex(token);
        if(type==0)return replaceReg(token,0,temps[index]);
        if(type==1)return replaceReg(token,1,prefixInputs[index]==0?unsigned(uv[layer]):prefixInputs[index]==6?primitive:varying);
        if(type==2)return replaceReg(token,2,constants[index]);
        if(type==7)return replaceReg(token,7,integer);
        if(type==8)return replaceReg(token,0,output[layer]);
        if(type==10)return replaceReg(token,10,index==12?12:index==13?12+layer:14+layer);
        return token;};
    std::vector<Word> defs;
    for(size_t at=1;at+1<length;){unsigned op=prefix[at]&65535,n=op==65534?(prefix[at]>>16)&32767:(prefix[at]>>24)&15;
        if(op==81||op==48||op==47){defs.push_back(prefix[at]);defs.push_back(mapped(prefix[at+1],0));defs.insert(defs.end(),prefix+at+2,prefix+at+n+1);}at+=n+1;}
    out.insert(out.begin()+1,defs.begin(),defs.end());
    for(unsigned layer=0;layer<layers;++layer){
        for(size_t at=1;at+1<length;){unsigned op=prefix[at]&65535,n=op==65534?(prefix[at]>>16)&32767:(prefix[at]>>24)&15;
            if(op!=65534&&op!=31&&op!=81&&op!=48&&op!=47){out.push_back(prefix[at]);for(unsigned i=1;i<=n;++i)out.push_back(mapped(prefix[at+i],layer));}at+=n+1;}
        out.insert(out.end(),{0x0200005b,dst(0,gx[layer],3),src(1,unsigned(uv[layer]),0x44),0x0200005c,dst(0,gy[layer],3),src(1,unsigned(uv[layer]),0x44)});
    }
    unsigned replaced[2]={};
    for(size_t at=first;at+1<count;){unsigned op=code[at]&65535,n=op==65534?(code[at]>>16)&32767:(code[at]>>24)&15;
        bool changed=false;
        if(op==66&&n==3&&regType(code[at+3])==10){unsigned layer=regIndex(code[at+3]);if(layer<layers){
            if(code[at]!=0x03000042||code[at+2]!=src(1,unsigned(uv[layer])))return false;
            out.insert(out.end(),{0x0500005d,code[at+1],src(0,output[layer]),code[at+3],src(0,gx[layer],0x44),src(0,gy[layer],0x44)});++replaced[layer];changed=true;}}
        if(!changed)out.insert(out.end(),code+at,code+at+n+1);at+=n+1;
    }
    if(!replaced[0]||(dual&&!replaced[1]))return false;
    out.push_back(65535);result.swap(out);if(constantBase)*constantBase=control;if(sharedResultRegister)*sharedResultRegister=output[0];return true;
}
}
