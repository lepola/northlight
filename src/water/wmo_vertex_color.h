#pragma once
#include "actor_deformation.h"
#include "water_shader_patch.h"

// Subdivision must interpolate the original WMO vertex lighting, including
// native point-light attenuation, MOCV multiplication and output clamps.
// Re-evaluating that nonlinear expression at new vertices changes illumination
// whenever geometry admission changes, even with zero displacement.
namespace NorthlightWmoVertexColor {
using namespace NorthlightWaterShaderPatch;
struct Layout {
    NorthlightActorDeformation::Program color;
    std::array<unsigned,10> semantic{};
    unsigned count=0;
    unsigned attributes()const{return count*3+1;}
    unsigned type(unsigned input)const{return color.inputs[input].usage==10?D3DDECLTYPE_D3DCOLOR:D3DDECLTYPE_FLOAT3;}
    unsigned inputBytes(unsigned input)const{return color.inputs[input].usage==10?4:12;}
    unsigned cornerBytes()const{unsigned n=0;for(unsigned i=0;i<count;++i)n+=inputBytes(i);return n;}
    unsigned offset(unsigned attribute)const{unsigned n=(attribute/count)*cornerBytes();for(unsigned i=0;i<attribute%count;++i)n+=inputBytes(i);return n;}
    unsigned bytes()const{return cornerBytes()*3+12;}
};
inline bool analyse(const Word* words,size_t count,Layout& layout){
    layout={};
    if(!NorthlightActorDeformation::compile(words,count,layout.color,true,2,10,0,15)||layout.color.skinned||
       layout.color.inputs.empty()||layout.color.inputs.size()>3)return false;
    unsigned usages=0;
    for(const auto& input:layout.color.inputs){
        if(input.index||(input.usage!=0&&input.usage!=3&&input.usage!=10)||(usages&(1u<<input.usage)))return false;
        usages|=1u<<input.usage;
    }
    for(const auto& op:layout.color.operations){
        if(NorthlightShadowShader::regType(op.destination)!=0)return false;
        for(const auto& source:op.source)if(source.token&0x2000u)return false;
    }
    bool used[16]={};used[6]=used[7]=true; // CPU/GPU height variants share one declaration.
    for(size_t at=1;at<count;){const unsigned op=words[at]&65535,n=op==65534?(words[at]>>16)&32767:(words[at]>>24)&15;
        if(op==65535)break;
        if(op==31&&n==2&&regType(words[at+2])==1&&(words[at+1]&15)==5)used[(words[at+1]>>16)&15]=true;
        at+=n+1;
    }
    layout.count=unsigned(layout.color.inputs.size());
    for(unsigned i=0;i<layout.attributes();++i){unsigned slot=0;while(slot<16&&used[slot])++slot;if(slot==16)return false;used[slot]=true;layout.semantic[i]=slot;}
    return true;
}
// Retain precisely the original position/fog/UV output dependencies. The old
// COLOR0 expression is replaced below, not executed a fourth time.
inline bool withoutColor(const std::vector<Word>& words,unsigned colorType,unsigned colorIndex,std::vector<Word>& out){
    struct Op{size_t at;unsigned code,n;};std::vector<Op> ops;
    for(size_t at=1;at<words.size();){const unsigned op=words[at]&65535,n=op==65534?(words[at]>>16)&32767:(words[at]>>24)&15;
        if(op==65535)break;if(at+n>=words.size())return false;ops.push_back({at,op,n});at+=n+1;}
    unsigned needed[32]={};std::vector<bool> keep(ops.size());
    for(size_t i=ops.size();i--;){const auto& op=ops[i];
        if(op.code==65534||op.code==31||op.code==81){keep[i]=true;continue;}
        switch(op.code){case 1:case 2:case 3:case 4:case 5:case 6:case 7:case 8:case 9:case 10:case 11:
        case 12:case 13:case 14:case 15:case 16:case 17:case 18:case 19:case 32:case 33:case 35:case 36:case 95:break;
        default:return false;}
        const int operands=op.code==95?3:NorthlightShadowShader::arithmeticOperands(op.code,(words[0]>>8)&255);
        if(operands<2||unsigned(operands)!=op.n)return false;
        const auto destination=words[op.at+1];const unsigned type=regType(destination),r=regIndex(destination),mask=(destination>>16)&15;
        if(type==colorType&&r==colorIndex)continue;
        unsigned observed=mask;
        if(type==0){if(r>=32)return false;observed&=needed[r];if(!observed)continue;needed[r]&=~mask;}
        else if(type<4||type>6)return false;
        keep[i]=true;
        // Retained instructions keep their complete destination mask. Preserve
        // sources for every written lane, including otherwise dead lanes, so
        // no retained vector read becomes undefined after pruning.
        unsigned channels=mask;
        if(op.code==8||op.code==36||op.code==33)channels=7;
        else if(op.code==9||op.code==16||op.code==17||op.code==95)channels=15;
        else if(op.code==6||op.code==7||op.code==14||op.code==15||op.code==32)channels=1;
        for(unsigned s=2;s<=op.n;++s){const auto token=words[op.at+s];if(token&0x2000u)return false;
            if(regType(token)!=0)continue;const unsigned source=regIndex(token);if(source>=32)return false;
            for(unsigned lane=0;lane<4;++lane)if(channels&(1u<<lane))needed[source]|=1u<<((token>>(16+lane*2))&3);
        }
    }
    for(unsigned mask:needed)if(mask)return false;
    out.assign(words.begin(),words.begin()+1);for(size_t i=0;i<ops.size();++i)if(keep[i])out.insert(out.end(),words.begin()+ops[i].at,words.begin()+ops[i].at+ops[i].n+1);
    out.push_back(65535);return true;
}
inline bool patch(const std::vector<Word>& displaced,const Layout& layout,std::vector<Word>& output){
    output.clear();if(!layout.count||layout.count>3||displaced.empty()||displaced.back()!=65535)return false;
    bool inputs[16]={},temps[32]={};unsigned colorType=layout.color.major<3?5:6,colorIndex=layout.color.major<3?0:12;size_t declarationEnd=1;
    for(size_t at=1;at<displaced.size();){const unsigned op=displaced[at]&65535,n=op==65534?(displaced[at]>>16)&32767:(displaced[at]>>24)&15;
        if(op==65535)break;if(at+n>=displaced.size())return false;
        if(op==31){declarationEnd=at+n+1;
            if(regType(displaced[at+2])==1){const auto r=regIndex(displaced[at+2]);if(r>=16)return false;inputs[r]=true;}
            if(layout.color.major==3&&regType(displaced[at+2])==6&&(displaced[at+1]&15)==10&&((displaced[at+1]>>16)&15)==0)colorIndex=regIndex(displaced[at+2]);
        }
        if(op!=65534&&op!=81&&op!=48&&op!=47&&op!=31)for(unsigned k=1;k<=n;++k){const auto t=displaced[at+k];if(regType(t)==0){const auto r=regIndex(t);if(r>=32)return false;temps[r]=true;}}
        at+=n+1;
    }
    if(colorIndex>=12)return false;
    // All other outputs are complete before the appended slice. Its saved
    // colour can reuse displacement/specular scratch that the slice never
    // reads, staying within VS2's twelve temporary registers.
    const unsigned tempLimit=layout.color.major==2?12:32;
    unsigned result=0,accumulated=0;
    const auto allocate=[&]{result=0;while(result<tempLimit&&temps[result])++result;if(result==tempLimit)return false;temps[result]=true;
        accumulated=0;while(accumulated<tempLimit&&temps[accumulated])++accumulated;return accumulated<tempLimit;};
    if(!allocate()){
        std::fill(std::begin(temps),std::end(temps),false);
        for(const auto& op:layout.color.operations){if(regIndex(op.destination)<32)temps[regIndex(op.destination)]=true;
            for(const auto& source:op.source)if(source.token&&regType(source.token)==0&&regIndex(source.token)<32)temps[regIndex(source.token)]=true;}
        if(!allocate())return false;
    }
    std::array<unsigned,10> registers{};std::vector<Word> declarations;
    for(unsigned i=0;i<layout.attributes();++i){unsigned r=0;while(r<16&&inputs[r])++r;if(r==16)return false;inputs[r]=true;registers[i]=r;
        declarations.insert(declarations.end(),{0x0200001fu,0x80000005u|(layout.semantic[i]<<16),dst(1,r)});
    }
    if(!withoutColor(displaced,colorType,colorIndex,output))return false;
    declarationEnd=1;for(size_t at=1;at<output.size();){const unsigned op=output[at]&65535,n=op==65534?(output[at]>>16)&32767:(output[at]>>24)&15;
        if(op==65535)break;if(op==31)declarationEnd=at+n+1;at+=n+1;}
    output.insert(output.begin()+declarationEnd,declarations.begin(),declarations.end());output.pop_back();
    for(unsigned corner=0;corner<3;++corner){
        for(const auto& op:layout.color.operations){
            const int operands=NorthlightShadowShader::arithmeticOperands(op.code,layout.color.major);
            if(operands<2||operands>4)return false;
            auto destination=op.destination;if(regIndex(destination)==32)destination=replaceReg(destination,0,result);
            output.push_back(op.code|(unsigned(operands)<<24));output.push_back(destination);
            for(int i=0;i<operands-1;++i){auto source=op.source[i].token;
                if(regType(source)==1){unsigned input=0;while(input<layout.count&&layout.color.inputs[input].reg!=regIndex(source))++input;if(input==layout.count)return false;source=replaceReg(source,1,registers[corner*layout.count+input]);}
                else if(regType(source)==0&&regIndex(source)==32)source=replaceReg(source,0,result);
                output.push_back(source);
            }
        }
        const unsigned swizzle=corner==0?0:corner==1?0x55:0xaa;
        if(!corner)output.insert(output.end(),{0x03000005u,dst(0,accumulated),src(0,result),src(1,registers[layout.count*3],swizzle)});
        else output.insert(output.end(),{0x04000004u,dst(0,accumulated),src(0,result),src(1,registers[layout.count*3],swizzle),src(0,accumulated)});
    }
    output.insert(output.end(),{0x02000001u,dst(colorType,colorIndex),src(0,accumulated),65535});
    // VS2 has the stricter static budget. Reject rather than drawing a variant
    // that would restore the admission-dependent native lighting defect.
    unsigned slots=0;for(size_t at=1;at<output.size();){const unsigned op=output[at]&65535,n=op==65534?(output[at]>>16)&32767:(output[at]>>24)&15;if(op==65535)break;
        if(op!=65534&&op!=31&&op!=81&&op!=48&&op!=47)slots+=NorthlightShadowShader::instructionSlots(op);at+=n+1;}
    return slots<=(layout.color.major==2?256u:512u);
}
}
