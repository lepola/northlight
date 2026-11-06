#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// Preserve the client's complete skinning/deformation calculation and copy its
// final clip position into TEXCOORD7 for an orthographic shadow replay PS.
// This does not identify world shaders or replace projection constants: caller
// must first match an audited original shader signature and projection layout.
namespace NorthlightShadowShader {
using Word=std::uint32_t;
inline unsigned regType(Word token) {return ((token>>28)&7)|((token>>8)&24);}
inline unsigned regIndex(Word token) {return token&2047;}
inline Word replaceReg(Word token,unsigned type,unsigned index) {
    return (token&~Word(0x70001fff))|((type&7)<<28)|((type&24)<<8)|index;
}
inline int arithmeticOperands(unsigned op,unsigned major) {
    switch(op) {
    case 1:case 6:case 7:case 14:case 15:case 16:case 19:
    case 35:case 36:case 46:case 78:case 79:return 2;
    case 2:case 3:case 5:case 8:case 9:case 10:case 11:case 12:
    case 13:case 17:case 20:case 21:case 22:case 23:case 24:
    case 32:case 33:return 3;
    case 4:case 18:case 34:return 4;
    case 37:return major==3?2:4;
    default:return -1;
    }
}
inline unsigned instructionSlots(unsigned op) {
    // Microsoft vs_2_0/vs_3_0 instruction-slot tables; reserve four free slots
    // before adding our two MOVs. DCL/DEF do not consume arithmetic slots.
    switch(op) {
    case 16:case 21:case 23:case 32:case 34:case 36:return 3;
    case 20:case 22:return 4;
    case 18:case 24:case 33:return 2;
    case 37:return 8;
    default:return 1;
    }
}
struct PatchInfo {
    unsigned major=0, tempRegister=0, texcoordRegister=0;
    unsigned originalInstructions=0, positionWrites=0;
    bool texcoord0XY=false,color0W=false;
};

inline bool patch(const Word* input,std::size_t wordCount,std::vector<Word>& output,
                  PatchInfo* info=nullptr) {
    if(!input||wordCount<4||wordCount>65536)return false;
    const unsigned major=(input[0]>>8)&255;
    if(input[0]!=(major==1?0xfffe0101u:major==2?0xfffe0200u:0xfffe0300u)||major<1||major>3)return false;
    bool temps[32]={},outputs[12]={};
    unsigned posType=major<3?4:99,posIndex=0,positionDeclarations=0;
    int uvIndex=major<3?0:-1;
    unsigned instructions=0,slots=0,writeMask=0,writes=0,uvMask=0,colorMask=0;
    int colorIndex=major<3?0:-1;
    std::vector<std::size_t> parameters, destinations;
    std::size_t cursor=1,end=0;
    bool executableSeen=false;
    auto mark=[&](Word t)->bool {
        if(!(t&0x80000000))return false;
        unsigned type=regType(t),index=regIndex(t);
        if(type==0) {if(index>=(major<3?12u:32u))return false;temps[index]=true;}
        if(type==6) {if(index>=(major<3?8u:12u))return false;outputs[index]=true;}
        return true;
    };
    while(cursor<wordCount) {
        const Word instruction=input[cursor];const unsigned op=instruction&65535;
        if(op==65535) {
            if(instruction!=0xffff||cursor+1!=wordCount)return false;
            end=cursor;break;
        }
        if(op==65534) {
            const std::size_t count=(instruction>>16)&32767;
            if(count>wordCount-cursor-1)return false;
            cursor+=count+1;continue;
        }
        // Predication/coissue and unknown flow control can bypass the appended
        // MOVs or leave channels unwritten. Explicitly reject them.
        if(instruction&0xf0000000u)return false;
        // VS1.1 encodes operand counts implicitly; relative c[a0.x+n]
        // consumes no address token (verified against D9VK dxso_decoder.cpp).
        int implicit=op==0?0:op==31?2:op==81?5:arithmeticOperands(op,major);
        if(major==1&&implicit<0)return false;
        const unsigned count=major==1?unsigned(implicit):(instruction>>24)&15;
        if(count>wordCount-cursor-1)return false;
        const std::size_t next=cursor+count+1;
        if(op==31) {
            if(count!=2||executableSeen||!mark(input[cursor+2]))return false;
            Word usage=input[cursor+1],dest=input[cursor+2];
            if(dest&0x2000)return false;
            if(major==3&&regType(dest)==6) {
                unsigned semantic=usage&15,index=(usage>>16)&15;
                if(semantic==0&&index==0) {
                    if(++positionDeclarations!=1)return false;
                    posType=6;posIndex=regIndex(dest);
                }
                if(semantic==10&&index==0)colorIndex=int(regIndex(dest));
                if(semantic==5&&index==7)return false;
                if(semantic==5&&index==0) {
                    if(uvIndex!=-1)return false;
                    uvIndex=int(regIndex(dest));
                }
            }
        } else if(op==81||op==48||op==47) {
            if(executableSeen||count!=(op==47?2u:5u)||!mark(input[cursor+1]))return false;
            const Word dest=input[cursor+1];
            if(regType(dest)!=(op==81?2u:op==48?7u:14u)||dest&0x2000)return false;
        } else if(op==0) {
            if(count)return false;
            executableSeen=true;++instructions;++slots;
        } else {
            executableSeen=true;++instructions;slots+=instructionSlots(op);
            const int formalCount=arithmeticOperands(op,major);
            if(formalCount<0)return false;
            std::size_t param=cursor+1;
            for(int n=0;n<formalCount;++n) {
                if(param>=next||!mark(input[param]))return false;
                const Word token=input[param];
                parameters.push_back(param);
                // Legacy matrix operations implicitly read consecutive source
                // registers. They must also reserve those temporary registers.
                if(n==2&&op>=20&&op<=24) {
                    unsigned rows=(op==20||op==22)?4u:op==24?2u:3u;
                    for(unsigned row=1;row<rows;++row)
                        if(regIndex(token)+row>=2048||!mark(replaceReg(token,regType(token),regIndex(token)+row)))return false;
                }
                if(n==0) {
                    if(token&0x2000)return false;
                    destinations.push_back(param);
                }
                ++param;
                if(major>=2&&n>0&&(token&0x2000)) {
                    if(param>=next||!mark(input[param]))return false;
                    unsigned type=regType(input[param]);
                    if(type!=3&&type!=15)return false;
                    if(input[param]&0x2000)return false;
                    ++param;
                }
            }
            if(param!=next)return false;
        }
        cursor=next;
    }
    if(!end||posType==99||slots>(major==1?124u:major==2?252u:508u))return false;
    if(major<3&&outputs[7])return false;
    for(std::size_t offset:destinations) {
        const Word token=input[offset];
        if(regType(token)==posType&&regIndex(token)==posIndex) {
            writeMask|=(token>>16)&15;++writes;
        }
        if(regType(token)==6&&int(regIndex(token))==uvIndex)uvMask|=(token>>16)&15;
        if(regType(token)==(major<3?5u:6u)&&int(regIndex(token))==colorIndex)colorMask|=(token>>16)&15;
    }
    if(writeMask!=15)return false;
    unsigned temp=0;while(temp<(major<3?12u:32u)&&temps[temp])++temp;
    if(temp==(major<3?12u:32u))return false;
    unsigned tex=7;
    if(major==3) {
        tex=0;while(tex<12&&outputs[tex])++tex;
        if(tex==12)return false;
    }
    std::vector<Word> patched(input,input+end);
    for(std::size_t offset:parameters) {
        const Word token=patched[offset];
        if(regType(token)==posType&&regIndex(token)==posIndex)
            patched[offset]=replaceReg(token,0,temp);
    }
    // Add declaration before all original declarations/definitions. Comments
    // such as CTAB remain untouched and do not change shader execution.
    if(major==3) {
        const Word decl[]={0x0200001f,0x80070005,replaceReg(0x800f0000,6,tex)};
        patched.insert(patched.begin()+1,decl,decl+3);
    }
    const Word suffix[]={
        (major==1?1u:0x02000001u),replaceReg(0x800f0000,posType,posIndex),replaceReg(0x80e40000,0,temp),
        (major==1?1u:0x02000001u),replaceReg(0x800f0000,6,tex),replaceReg(0x80e40000,0,temp),0x0000ffff};
    patched.insert(patched.end(),suffix,suffix+7);
    if(info)*info={major,temp,tex,instructions,writes,(uvMask&3)==3,(colorMask&8)!=0};
    output.swap(patched);return true;
}
} // namespace NorthlightShadowShader
