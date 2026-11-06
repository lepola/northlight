#pragma once
#include "hdr_ui_patch.h"

namespace NorthlightSkyComposition {
// Clamp output as the native UNORM target does before blending into FP16.
// Function zero preserves alpha and the hardware alpha test.
// An opaque material ignores shader alpha for colour blending. Its background
// transmission must therefore be one even when a texture's alpha is zero.
// Preserve shader kills and perform the native alpha test before replacing it.
inline bool patchOutput(const std::uint32_t* code,std::size_t count,unsigned function,
                        std::vector<std::uint32_t>& output,unsigned& control){
    using namespace NorthlightHdrUi;
    std::vector<Instruction> ops;unsigned cost=0;if(function>8||!parse(code,count,ops,cost))return false;
    const unsigned major=(code[0]>>8)&255,nt=major==2?12:32,nc=major==2?32:224;
    bool temps[32]{},constants[224]{};unsigned mask=0;
    for(const auto& op:ops)for(unsigned i=op.op==31?2:1;i<op.count;++i){
        if((op.op==81||op.op==48||op.op==47)&&i>1)break;
        const auto w=code[op.at+i];const auto t=type(w),r=index(w);
        if(t==0){if(r>=nt)return false;temps[r]=true;}
        if(t==2){if(r>=nc)return false;constants[r]=true;}
        if(t==8){if(r||i!=1)return false;mask|=(w>>16)&15;}
        if(t==9)return false;
    }
    if(mask!=15||cost+(function?10:2)>(major==2?64u:512u))return false;
    unsigned color=0;while(color<nt&&temps[color])++color;if(color==nt)return false;temps[color]=true;
    unsigned test=0;while(test<nt&&temps[test])++test;if(test==nt)return false;
    control=0;while(control<nc&&constants[control])++control;if(control==nc)return false;
    output.assign(1,code[0]);
    for(const auto& op:ops){output.push_back(code[op.at]);for(unsigned i=1;i<op.count;++i){auto w=code[op.at+i];if(i==1&&type(w)==8)w=reg(w|0x00100000u,0,color);output.push_back(w);}}
    if(!function){output.insert(output.end(),{0x02000001u,0x800f0800u,reg(0x80e40000u,0,color),65535});return true;}
    const auto dst=reg(0x800f0000u,0,test),value=reg(0x80ff0000u,0,color),result=reg(0x80e40000u,0,test);
    const auto one=reg(0xa0000000u,2,control),ref=reg(0xa0550000u,2,control),zero=reg(0xa0aa0000u,2,control),half=reg(0xa0ff0000u,2,control);
    if(function!=8){
        if(function==1)output.insert(output.end(),{0x02000001u,dst,zero});
        else{
            output.insert(output.end(),{0x03000002u,dst,value,ref|0x01000000u});
            const bool negative=function==3||function==4||function==5||function==6;
            if(function==3||function==6)output.insert(output.end(),{0x0300000bu,dst,result,result|0x01000000u});
            const auto condition=result|(negative?0x01000000u:0u);
            const bool onPositive=function==3||function==4||function==7;
            // CMP is available in pixel shader 2.0. SLT/SGE are vertex-only on
            // native D3D9 even though translation backends may accept them.
            output.insert(output.end(),{0x04000058u,dst,condition,onPositive?one:zero,onPositive?zero:one});
        }
        output.insert(output.end(),{0x03000002u,dst,result,half|0x01000000u,0x01000041u,dst});
    }
    output.insert(output.end(),{0x02000001u,reg(0x80080000u,0,color),one,0x02000001u,0x800f0800u,reg(0x80e40000u,0,color),65535});
    return true;
}
}
