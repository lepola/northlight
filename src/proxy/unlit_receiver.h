#pragma once
#include "actor_deformation.h"
#include "name_stencil.h"

namespace NorthlightUnlitReceiver {
constexpr unsigned long Bit=0x40;
constexpr unsigned long PrelitBit=0x20;
// D24X4S4 and D15S1 cannot represent bit 6.
inline bool stencilFormat(unsigned long format,unsigned long intz){return format==75||format==81||format==intz;}
// The caller proves the complete shader identity belongs to an M2 model.
// Slice RGB only: fog, alpha and environment UVs may still read a normal.
inline bool shader(const uint32_t* words,size_t count,bool model){
    if(!model)return false;
    NorthlightActorDeformation::Program color;
    if(!NorthlightActorDeformation::compile(words,count,color,true,2,10,0,7))return false;
    for(const auto& input:color.inputs)if(input.usage==3)return false;
    return true;
}
// WMO identity alone does not imply prelit: failed outdoor-light decoding is
// also represented by lighting=false in the context catalog. Prove the RGB
// dependency slice contains vertex colours and no normal/global-light inputs.
inline bool prelitShader(const uint32_t* words,size_t count,bool wmo){
    if(!wmo)return false;
    NorthlightActorDeformation::Program color;
    if(!NorthlightActorDeformation::compile(words,count,color,true,2,10,0,7))return false;
    bool vertexColor=false;
    for(const auto& input:color.inputs){if(input.usage==3)return false;vertexColor|=input.usage==10;}
    for(const auto& op:color.kernel.ops)for(int i=0;i<op.operands;++i){const auto& s=op.source[i];
        if(s.kind==2&&(s.relative||(s.index>=10&&s.index<=12&&s.definition<0)))return false;}
    return vertexColor;
}
using namespace NorthlightNameStencil;
inline constexpr Set Mark[]={{StencilEnable,1},{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,Bit},{StencilWriteMask,Bit|PrelitBit|kBit},{TwoSidedStencilMode,0}};
inline constexpr Set MarkPrelit[]={{StencilEnable,1},{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,PrelitBit},{StencilWriteMask,Bit|PrelitBit|kBit},{TwoSidedStencilMode,0}};
inline constexpr Set Unmark[]={{StencilEnable,1},{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,0},{StencilWriteMask,Bit|PrelitBit|kBit},{TwoSidedStencilMode,0}};
}
