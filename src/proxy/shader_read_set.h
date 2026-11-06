#pragma once
// Task 34: what a D3D9 shader can read of the API state, scanned once from its bytecode when it is created: the float constant registers (minus the ones the
// shader defines itself with def), the int and bool registers, the samplers. The spell layer records and replays only these for a deferred draw.
// Conservative by construction: a version, opcode or operand the scanner does not understand, a truncated stream, an extended constant bank or a relative
// int/bool operand makes the whole result "reads everything" (exact=false). A relative float operand c[a0.x+n] (or c[aL+n]) reads registers n .. last
// (the bytecode does not say how far: the constant table is a comment the compiler may strip). Control flow is not followed: every operand of every
// instruction counts as read, branches included. No D3D headers, no allocation.
#include <cstddef>
#include <cstdint>

namespace NorthlightShaderReads {
struct ReadSet {
    bool exact=false;                // false: reads everything (the other fields are meaningless)
    bool vertex=false;               // vertex shader (its samplers are the vertex texture samplers)
    uint64_t f[4]={};                // float constant registers (VS 0..255, PS 0..223)
    uint16_t i=0,b=0;                // int and bool constant registers
    uint16_t samplers=0;             // sampler registers s0..s15 (declared with dcl or used by a texture instruction)
    uint16_t sampledSamplers=0;      // executable sampler reads; declarations alone never fetch a texture
};
namespace Detail {
constexpr unsigned kTypeConst=2,kTypeSampler=10,kTypeConst2=11,kTypeConst3=12,kTypeConst4=13,kTypeConstInt=7,kTypeConstBool=14;
inline unsigned regType(uint32_t token){return ((token>>28)&7)|((token>>8)&24);}
inline unsigned regNum(uint32_t token){return token&0x7ff;}
// Operand counts (words after the opcode token) of the vertex shader model 1.x instructions, which do not encode their length. -1: not a vs_1_x instruction.
inline int vs1Length(unsigned op){
    switch(op){
    case 0:return 0;                                                                   // nop
    case 1:case 6:case 7:case 14:case 15:case 16:case 19:case 78:case 79:return 2;     // mov rcp rsq exp log lit frc expp logp
    case 2:case 3:case 5:case 8:case 9:case 10:case 11:case 12:case 13:case 17:case 20:case 21:case 22:case 23:case 24:return 3;
    case 4:case 18:return 4;                                                           // mad lrp
    case 31:return 2;                                                                  // dcl
    case 81:return 5;                                                                  // def
    default:return -1;
    }
}
// Instructions without a destination operand (SM2+): call callnz loop ret endloop label rep endrep if ifc else endif break breakc breakp.
inline bool noDestination(unsigned op){
    switch(op){case 25:case 26:case 27:case 28:case 29:case 30:case 38:case 39:case 40:case 41:case 42:case 43:case 44:case 45:case 96:return true;default:return false;}
}
// Known SM2+ opcodes (their length is in the opcode token; the layout is destination, then sources).
inline bool knownOpcode(unsigned op){
    return op<=48||(op>=64&&op<=96);
}
inline unsigned matrixRows(unsigned op){return (op==20||op==22)?4u:op==24?2u:3u;} // m4x4 m4x3 m3x4 m3x3 m3x2: the consecutive registers the second source names
}

// words: the shader token stream (version token first). kind is read from the version token.
inline ReadSet scan(const uint32_t* words,std::size_t count){
    using namespace Detail;
    ReadSet bad;ReadSet r;
    if(!words||count<2||count>65536)return bad;
    const uint32_t version=words[0];const bool vertex=(version>>16)==0xfffe,pixel=(version>>16)==0xffff;
    const unsigned major=(version>>8)&255,minor=version&255;
    if(!vertex&&!pixel)return bad;
    if(major<1||major>3||minor>9)return bad;
    if(pixel&&major<2)return bad; // ps_1_x: texture instructions with implicit samplers; never deferred (the layer admits ps_2_0 and newer), so: reads everything
    r.vertex=vertex;
    const unsigned floatRegs=vertex?256u:224u;
    uint64_t defF[4]={};uint16_t defI=0,defB=0;
    std::size_t at=1;bool ended=false;
    while(at<count){
        const uint32_t token=words[at];const unsigned op=token&0xffff;
        if(op==0xffff){ended=true;break;}
        if(op==0xfffe){const std::size_t n=(token>>16)&0x7fff;if(n>count-at-1)return bad;at+=n+1;continue;} // comment (constant table): skipped
        if(op==0xfffd){++at;continue;}                                                                     // phase marker (ps_1_4)
        int length;
        if(major>=2){if(!knownOpcode(op))return bad;length=int((token>>24)&15);}
        else{length=vs1Length(op);if(length<0)return bad;}
        if(std::size_t(length)>count-at-1)return bad;
        const uint32_t* args=words+at+1;at+=std::size_t(length)+1;
        int n=length;
        // predicated (SM2+, bit 28): the predicate register token follows the destination operand (and its address token), not the end. Forms with no
        // destination (or def/dcl) cannot be placed: reads everything.
        const bool predicated=major>=2&&(token&0x10000000u);
        if(predicated&&(noDestination(op)||op==81||op==48||op==47||op==31||op==0))return bad;
        if(op==81){                                                     // def: float literal, not a read
            if(n<1||!(args[0]&0x80000000u)||regType(args[0])!=kTypeConst||regNum(args[0])>=floatRegs)return bad;
            defF[regNum(args[0])>>6]|=1ull<<(regNum(args[0])&63);continue;
        }
        if(op==48){if(n<1||regType(args[0])!=kTypeConstInt||regNum(args[0])>=16)return bad;defI|=uint16_t(1u<<regNum(args[0]));continue;}
        if(op==47){if(n<1||regType(args[0])!=kTypeConstBool||regNum(args[0])>=16)return bad;defB|=uint16_t(1u<<regNum(args[0]));continue;}
        if(op==31){                                                     // dcl usage, register
            if(n<2)return bad;
            if(regType(args[1])==kTypeSampler){if(regNum(args[1])>=16)return bad;r.samplers|=uint16_t(1u<<regNum(args[1]));}
            continue;
        }
        if(op==0)continue;
        // operands
        int idx=0,operand=0;
        const bool dst=!noDestination(op);
        while(idx<n){
            const uint32_t t=args[idx++];
            if(!(t&0x80000000u))return bad;
            const bool relative=(t&0x2000u)!=0;
            const unsigned type=regType(t),num=regNum(t);
            if(relative&&major>=2){if(idx>=n)return bad;const uint32_t a=args[idx++];if(!(a&0x80000000u))return bad;} // address register token
            const bool isDst=dst&&operand==0;
            ++operand;
            if(isDst&&predicated){if(idx>=n)return bad;++idx;}              // skip the predicate token after the destination
            if(isDst){if(type==kTypeConst||type==kTypeConstInt||type==kTypeConstBool||type==kTypeSampler||(type>=kTypeConst2&&type<=kTypeConst4))return bad;continue;}
            switch(type){
            case kTypeConst:{
                unsigned rows=1;
                if(op>=20&&op<=24&&operand==3)rows=matrixRows(op);       // the matrix source of m4x4.. names consecutive registers
                if(num>=floatRegs||rows>floatRegs-num)return bad;
                const unsigned last=relative?floatRegs-1:num+rows-1;        // relative: from the base to the end of the bank
                for(unsigned k=num;k<=last;++k)r.f[k>>6]|=1ull<<(k&63);
                break;}
            case kTypeConstInt:if(relative||num>=16)return bad;r.i|=uint16_t(1u<<num);break;
            case kTypeConstBool:if(relative||num>=16)return bad;r.b|=uint16_t(1u<<num);break;
            case kTypeConst2:case kTypeConst3:case kTypeConst4:return bad;  // extended banks (c2048..) are outside what the API state carries here
            case kTypeSampler:if(num>=16)return bad;r.samplers|=uint16_t(1u<<num);r.sampledSamplers|=uint16_t(1u<<num);break;
            default:break;
            }
        }
    }
    if(!ended)return bad;
    for(unsigned w=0;w<4;++w)r.f[w]&=~defF[w];
    r.i&=uint16_t(~defI);r.b&=uint16_t(~defB);
    if(!vertex){for(unsigned k=floatRegs;k<256;++k)r.f[k>>6]&=~(1ull<<(k&63));}
    r.exact=true;
    return r;
}
}
