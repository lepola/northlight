#pragma once
#include "patch_terrain_shadow_coverage.h"
#include "water_shader_patch.h"
#include "auto_bump_compiled_shaders.h"

namespace NorthlightNativeMaterialCapture {
using namespace NorthlightWaterShaderPatch;
inline bool floatOnlyConstants(const Word* code,size_t count){
    NorthlightTerrainCoverage::Program p;if(!NorthlightTerrainCoverage::analyse(code,count,p)||p.vertex)return false;
    bool integers[16]={},booleans[16]={};
    for(const auto& op:p.ops)if(op.code==48||op.code==47){const unsigned r=regIndex(code[op.at+1]);if(r>=16)return false;(op.code==48?integers:booleans)[r]=true;}
    for(const auto& op:p.ops){if(op.code==31||op.code==81||op.code==48||op.code==47||op.code==65534)continue;
        int operands=NorthlightShadowShader::arithmeticOperands(op.code,p.major);
        if(operands<0)switch(op.code){case 0:case 29:case 39:case 42:case 43:case 44:operands=0;break;case 38:case 40:case 65:case 96:operands=1;break;case 27:case 41:case 45:operands=2;break;case 66:case 95:operands=3;break;case 88:case 90:operands=4;break;case 93:operands=5;break;default:return false;}
        if(op.size!=unsigned(operands))return false;
        for(unsigned i=1;i<=op.size;++i){const auto t=code[op.at+i];const unsigned type=regType(t),r=regIndex(t);
            if((type==7||type==14)&&((t&0x2000u)||r>=16||!(type==7?integers:booleans)[r]))return false;}}
    return true;
}
// Keep only the original alpha/kill dependencies. Unknown instructions retain
// the complete native program; coverage must never depend on this optimization.
inline bool alphaSlice(const Word* code,const NorthlightTerrainCoverage::Program& p,bool alpha,std::vector<Word>& out){
    unsigned needed[33]={};needed[32]=alpha?8:0;std::vector<std::vector<Word>> reverse;
    struct Branch {std::array<unsigned,33> after{},other{};size_t begin=0;bool hasElse=false;};std::vector<Branch> branches;
    unsigned dimensions[16]={};for(const auto& op:p.ops)if(op.code==31&&regType(code[op.at+2])==10){const unsigned sampler=regIndex(code[op.at+2]);if(sampler>=16)return false;dimensions[sampler]=(code[op.at+1]>>27)&15;}
    for(auto it=p.ops.rbegin();it!=p.ops.rend();++it){const auto& op=*it;
        if(op.code==31||op.code==81||op.code==48||op.code==47)continue;
        const auto mark=[&](Word source,unsigned channels){if(regType(source)!=0)return true;const unsigned r=regIndex(source);if(r>=32)return false;
            const unsigned modifier=(source>>24)&15;if(modifier==9)channels|=4;else if(modifier==10)channels|=8;
            for(unsigned c=0;c<4;++c)if(channels&(1u<<c))needed[r]|=1u<<((source>>(16+2*c))&3);return true;};
        if(op.code==43){if(op.size)return false;Branch b;b.begin=reverse.size();std::copy(needed,needed+33,b.after.begin());branches.push_back(b);reverse.emplace_back(code+op.at,code+op.at+1);continue;}
        if(op.code==42){if(op.size||branches.empty()||branches.back().hasElse)return false;auto& b=branches.back();std::copy(needed,needed+33,b.other.begin());std::copy(b.after.begin(),b.after.end(),needed);b.hasElse=true;reverse.emplace_back(code+op.at,code+op.at+1);continue;}
        if(op.code==40||op.code==41){if(branches.empty()||op.size!=(op.code==40?1u:2u))return false;const auto b=branches.back();branches.pop_back();
            if(reverse.size()==b.begin+1+unsigned(b.hasElse)){reverse.resize(b.begin);std::copy(b.after.begin(),b.after.end(),needed);continue;}
            for(unsigned i=0;i<33;++i)needed[i]|=(b.hasElse?b.other:b.after)[i];
            for(unsigned i=1;i<=op.size;++i)if(!mark(code[op.at+i],1))return false;reverse.emplace_back(code+op.at,code+op.at+op.size+1);continue;}
        if(op.code==65){if(op.size!=1)return false;const Word r=code[op.at+1];if(regType(r)==0&&regIndex(r)<32)needed[regIndex(r)]|=15;else if(regType(r)!=1)return false;reverse.emplace_back(code+op.at,code+op.at+op.size+1);continue;}
        const bool texture=(op.code==66||op.code==95)&&op.size==3;
        const int operands=op.code==88||op.code==90?4:NorthlightShadowShader::arithmeticOperands(op.code,p.major);
        if(!texture&&(operands<2||unsigned(operands)!=op.size))return false;
        const Word destination=code[op.at+1];const unsigned type=regType(destination),reg=regIndex(destination);
        if(!((type==0&&reg<32)||(type==8&&!reg)))return false;
        const unsigned slot=type==8?32:reg,written=(destination>>16)&15,observed=needed[slot]&written;
        if(!observed)continue;needed[slot]&=~written;
        // Scalar operations read their x swizzle; dot products couple lanes.
        unsigned channels=observed;if(op.code==8||op.code==33||op.code==36)channels=7;else if(op.code==9||op.code==16||op.code==17)channels=15;
        else if(op.code==6||op.code==7||op.code==14||op.code==15||op.code==32||op.code==37||op.code==78||op.code==79)channels=1;
        if(texture){const Word sampler=code[op.at+3];const unsigned index=regIndex(sampler);if(regType(sampler)!=10||index>=16)return false;const auto dimension=dimensions[index];if(dimension<2||dimension>4)return false;
            const unsigned channels=(dimension==2?3u:7u)|((op.code==95||(code[op.at]&0xf0000))?8u:0u);if(!mark(code[op.at+2],channels))return false;}
        else if(op.code==90){if(!mark(code[op.at+2],3)||!mark(code[op.at+3],3)||!mark(code[op.at+4],1))return false;}
        else for(unsigned i=2;i<=op.size;++i)if(!mark(code[op.at+i],channels))return false;
        reverse.emplace_back(code+op.at,code+op.at+op.size+1);reverse.back()[1]=(destination&~0xf0000u)|(observed<<16);
    }
    if(!branches.empty())return false;for(unsigned i=0;i<33;++i)if(needed[i])return false;
    out.assign(code,code+1);for(const auto& op:p.ops)if(op.code==31||op.code==81||op.code==48||op.code==47)out.insert(out.end(),code+op.at,code+op.at+op.size+1);
    for(auto it=reverse.rbegin();it!=reverse.rend();++it)out.insert(out.end(),it->begin(),it->end());out.push_back(65535);return true;
}
// Append normal/occlusion capture to the coverage slice. Original constants,
// UVs and samplers keep their ABI; native lighting is not a capture output.
inline bool patch(const Word* code,size_t count,std::vector<Word>& out,unsigned& control,bool alpha=true,bool shared=false,bool receiver=false){
    const auto* prefix=reinterpret_cast<const Word*>(receiver?kModelNormalCaptureShader:shared?kBumpSharedNativeShader:kBumpNativeShader);const size_t length=receiver?sizeof(kModelNormalCaptureShader)/sizeof(DWORD):shared?sizeof(kBumpSharedNativeShader)/sizeof(DWORD):sizeof(kBumpNativeShader)/sizeof(DWORD);
    NorthlightTerrainCoverage::Program native,capture;if(!NorthlightTerrainCoverage::analyse(code,count,native)||native.vertex||!NorthlightTerrainCoverage::analyse(prefix,length,capture)||native.slots+capture.slots>512)return false;
    std::vector<Word> sliced;if(alphaSlice(code,native,alpha,sliced)){code=sliced.data();count=sliced.size();native={};if(!NorthlightTerrainCoverage::analyse(code,count,native))return false;}
    int uv=-1;bool samplers[16]={},integers[16]={};
    for(const auto& op:native.ops){
        if(op.code==31&&regType(code[op.at+2])==1&&(code[op.at+1]&15)==5){unsigned index=(code[op.at+1]>>16)&15;if(index==7||(shared&&index==6)||(receiver&&index==5))return false;if(!index)uv=int(regIndex(code[op.at+2]));}
        for(unsigned i=op.code==31?2:1;i<=op.size;++i){if((op.code==81||op.code==48||op.code==47)&&i>1)break;Word t=code[op.at+i];unsigned type=regType(t),r=regIndex(t);if(type==10&&r<16)samplers[r]=true;if(type==7&&r<16)integers[r]=true;if(type==9||(type==8&&r))return false;}}
    if(uv<0||(!receiver&&samplers[14]))return false;
    unsigned varying=0;while(varying<10&&native.inputs[varying])++varying;if(varying==10)return false;
    native.inputs[varying]=true;unsigned primitive=0;if(shared){while(primitive<10&&native.inputs[primitive])++primitive;if(primitive==10)return false;}
    unsigned normal=0;if(receiver){while(normal<10&&native.inputs[normal])++normal;if(normal==10)return false;}
    auto temp=[&](){unsigned r=0;while(r<32&&native.temps[r])++r;if(r<32)native.temps[r]=true;return r;};
    const unsigned color=temp();if(color==32)return false;
    const unsigned bankCount=receiver?1:9; // receiver control, or material controls and view-to-world rows
    control=128;for(;;){bool free=true;for(unsigned i=0;i<bankCount;++i)free&=!native.constants[control+i];if(free)break;if(++control>192-bankCount)return false;}
    for(unsigned i=0;i<bankCount;++i)native.constants[control+i]=true;
    unsigned constants[256],temps[32],inputs[16],integerMap[16];for(unsigned i=0;i<256;++i)constants[i]=i;std::fill(std::begin(temps),std::end(temps),32u);std::fill(std::begin(inputs),std::end(inputs),16u);std::fill(std::begin(integerMap),std::end(integerMap),16u);
    for(unsigned i=0;i<bankCount;++i)constants[20+i]=control+i;
    unsigned nativeColorInput=16;
    for(size_t at=1;at+1<length;){unsigned op=prefix[at]&65535,n=op==65534?(prefix[at]>>16)&32767:(prefix[at]>>24)&15;
        if(op==31&&regType(prefix[at+2])==1){unsigned semantic=prefix[at+1]&15,index=(prefix[at+1]>>16)&15,r=regIndex(prefix[at+2]);if(semantic==10)nativeColorInput=r;else if(semantic==5)inputs[r]=index==7?varying:index==6?primitive:receiver&&index==5?normal:unsigned(uv);else return false;}
        if(op==81){unsigned c=192;while(c<256&&native.constants[c])++c;if(c==256)return false;native.constants[c]=true;constants[regIndex(prefix[at+1])]=c;}
        if(op!=65534)for(unsigned i=op==31?2:1;i<=n;++i){if((op==81||op==48||op==47)&&i>1)break;Word t=prefix[at+i];if(regType(t)==0&&temps[regIndex(t)]==32){unsigned r=temp();if(r==32)return false;temps[regIndex(t)]=r;}if(regType(t)==7&&integerMap[regIndex(t)]==16){unsigned r=0;while(r<16&&integers[r])++r;if(r==16)return false;integers[r]=true;integerMap[regIndex(t)]=r;}}at+=n+1;}
    if(nativeColorInput==16)return false;
    auto mapped=[&](Word t){unsigned type=regType(t),r=regIndex(t);if(type==0)return replaceReg(t,0,temps[r]);if(type==1)return replaceReg(t,r==nativeColorInput?0:1,r==nativeColorInput?color:inputs[r]);if(type==2)return replaceReg(t,2,constants[r]);if(type==7)return replaceReg(t,7,integerMap[r]);if(type==10)return replaceReg(t,10,14);return t;};
    out.assign(code,code+count-1);
    for(const auto& op:native.ops)if(op.code!=65534&&op.code!=31&&op.code!=81&&op.code!=48&&op.code!=47)for(unsigned i=1;i<=op.size;++i){Word& t=out[op.at+i];if(regType(t)==8&&regIndex(t)==0)t=replaceReg(t,0,color);}
    out.insert(out.begin()+native.declEnd,{0x0200001fu,0x80070005u,dst(1,varying)});
    if(receiver)out.insert(out.begin()+native.declEnd,{0x0200001fu,0x80050005u,dst(1,normal,7)});
    else out.insert(out.begin()+native.declEnd,{0x0200001fu,0x90000000u,dst(10,14)});
    if(shared)out.insert(out.begin()+native.declEnd,{0x0200001fu,0x80060005u,dst(1,primitive,1)});
    // Opaque coverage needs no original alpha; define all lanes even when the
    // native program had no live output. The capture's alpha selector is zero.
    if(!alpha)out.insert(out.end(),{0x02000001u,dst(0,color),src(2,control)});
    std::vector<Word> defs;
    for(size_t at=1;at+1<length;){unsigned op=prefix[at]&65535,n=op==65534?(prefix[at]>>16)&32767:(prefix[at]>>24)&15;
        if(op==81||op==48||op==47){defs.push_back(prefix[at]);defs.push_back(mapped(prefix[at+1]));defs.insert(defs.end(),prefix+at+2,prefix+at+n+1);}
        else if(op!=65534&&op!=31){out.push_back(prefix[at]);for(unsigned i=1;i<=n;++i)out.push_back(mapped(prefix[at+i]));}at+=n+1;}
    out.insert(out.begin()+1,defs.begin(),defs.end());out.push_back(65535);return true;
}
}
