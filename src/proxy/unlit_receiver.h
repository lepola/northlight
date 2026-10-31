#pragma once
#include "actor_deformation.h"
#include "name_stencil.h"

namespace NorthlightUnlitReceiver {
constexpr unsigned long Bit=0x40;
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
using namespace NorthlightNameStencil;
inline constexpr Set Mark[]={{StencilEnable,1},{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,Bit},{StencilWriteMask,Bit},{TwoSidedStencilMode,0}};
inline constexpr Set Unmark[]={{StencilEnable,1},{StencilFunc,kCmpAlways},{StencilPass,kOpReplace},{StencilFail,kOpKeep},{StencilZFail,kOpKeep},{StencilRef,0},{StencilWriteMask,Bit|kBit},{TwoSidedStencilMode,0}};
}
