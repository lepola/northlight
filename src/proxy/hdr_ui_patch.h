#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

// Keep the complete original UI program, including font filtering and loops.
// Only its oC0 destination is redirected; the compiled transfer runs after it.
namespace NorthlightHdrUi {
using Word=std::uint32_t;
inline unsigned type(Word w){return ((w>>28)&7)|((w>>8)&24);}
inline unsigned index(Word w){return w&2047;}
inline Word reg(Word w,unsigned t,unsigned n){return (w&~Word(0x70001fff))|((t&7)<<28)|((t&24)<<8)|n;}
struct Instruction {std::size_t at,count;unsigned op;};
inline int operands(unsigned op,unsigned major){
    switch(op){
    case 0:case 29:case 39:case 42:case 43:case 44:return 0;
    case 38:case 40:case 65:case 96:return 1;
    case 1:case 6:case 7:case 14:case 15:case 19:case 27:case 31:case 35:case 36:case 41:case 45:case 47:case 91:case 92:return 2;
    case 2:case 3:case 5:case 8:case 9:case 10:case 11:case 12:case 13:case 17:case 20:case 21:case 22:case 23:case 24:case 32:case 33:case 66:case 94:case 95:return 3;
    case 4:case 18:case 34:case 88:case 90:return 4;
    case 48:case 81:case 93:return 5;
    case 37:return major==3?2:4;
    default:return -1; // Includes CALL, CALLNZ, RET, LABEL and legacy SM1 ops.
    }
}
inline unsigned slots(unsigned op){
    switch(op){
    case 31:case 47:case 48:case 81:return 0;
    case 18:case 22:case 24:case 29:case 33:case 39:case 90:case 91:case 92:case 95:return 2;
    case 21:case 23:case 27:case 32:case 36:case 38:case 40:case 41:case 45:case 96:return 3;
    case 20:return 4;
    case 37:return 8;
    case 93:return 3;
    default:return 1;
    }
}
inline bool parse(const Word* words,std::size_t count,std::vector<Instruction>& instructions,unsigned& cost){
    if(!words||count<3||count>65536||(words[0]!=0xffff0200&&words[0]!=0xffff0300))return false;
    const unsigned major=(words[0]>>8)&255;cost=0;instructions.clear();
    for(std::size_t at=1;at<count;){
        Word w=words[at];unsigned op=w&65535;
        if(op==65535)return w==65535&&at+1==count;
        if(op==65534){std::size_t n=(w>>16)&32767;if(n>count-at-1)return false;at+=n+1;continue;}
        if(w&0xf0000000)return false; // predication/coissue are not audited here
        int arity=operands(op,major);unsigned n=(w>>24)&15;
        if(arity<0||n!=unsigned(arity)||n>count-at-1)return false;
        if(op==31&&n!=2)return false;
        // DEF payloads are immediate bit patterns, not register tokens.
        unsigned first=op==31?2:1,last=(op==47||op==48||op==81)?1:n;
        for(unsigned j=first;j<=last;++j)if(!(words[at+j]&0x80000000)||(words[at+j]&0x2000))return false;
        instructions.push_back({at,n+1,op});cost+=slots(op);at+=n+1;
    }
    return false;
}
struct Info {unsigned major=0,captureTemp=0,scaleConstant=0,originalSlots=0,addedSlots=0;};
inline bool patch(const Word* original,std::size_t count,const Word* transfer,std::size_t transferCount,
                  float whiteNits,bool premultiplied,std::vector<Word>& output,Info* info=nullptr){
    output.clear();
    if(!(whiteNits>=1&&whiteNits<=10000)||!original||!transfer||count<3||transferCount<3||original[0]!=transfer[0])return false;
    std::vector<Instruction> old,tail;unsigned originalSlots=0,addedSlots=0;
    if(!parse(original,count,old,originalSlots)||!parse(transfer,transferCount,tail,addedSlots))return false;
    const unsigned major=(original[0]>>8)&255,temps=major==2?12:32,constants=major==2?32:224;
    std::array<bool,32> usedTemps{};std::array<bool,224> usedConstants{};unsigned mask=0;
    for(const auto& ins:old){
        unsigned n=unsigned(ins.count-1),first=ins.op==31?2:1,last=(ins.op==47||ins.op==48||ins.op==81)?1:n;
        for(unsigned j=first;j<=last;++j){Word w=original[ins.at+j];unsigned t=type(w),r=index(w);
            if(t==0){if(r>=temps)return false;usedTemps[r]=true;}
            if(t==2){if(r>=constants)return false;usedConstants[r]=true;}
            if(t==8){if(r||j!=1)return false;mask|=(w>>16)&15;}
            if(t==9)return false; // oDepth is not part of the UI transfer contract
        }
    }
    if(mask!=15||originalSlots+addedSlots>(major==2?64u:512u))return false;
    int capture=-1;for(unsigned i=temps;i-->0;)if(!usedTemps[i]){capture=int(i);break;}if(capture<0)return false;
    std::array<int,224> constantMap;constantMap.fill(-1);
    std::array<int,32> tempMap;tempMap.fill(-1);unsigned nextTemp=0;
    auto constant=[&](unsigned r)->int{
        if(r>=constants)return -1;if(constantMap[r]>=0)return constantMap[r];
        for(unsigned i=constants;i-->0;)if(!usedConstants[i]){usedConstants[i]=true;return constantMap[r]=int(i);}return -1;
    };
    auto remap=[&](Word w,Word& result)->bool{
        unsigned t=type(w),r=index(w);int mapped=-1;
        if(t==1){if(r)return false;result=reg(w,0,unsigned(capture));return true;}
        if(t==2){mapped=constant(r);if(mapped<0)return false;result=reg(w,2,unsigned(mapped));return true;}
        if(t==0){if(r>=32)return false;if(tempMap[r]<0){while(nextTemp<temps&&int(nextTemp)==capture)++nextTemp;if(nextTemp>=temps)return false;tempMap[r]=int(nextTemp++);}result=reg(w,0,unsigned(tempMap[r]));return true;}
        if(t==8&&r==0){result=w;return true;}return false;
    };
    const int scale=constant(0);if(scale<0)return false;
    std::vector<Word> definitions,body;
    Word scaleValue[4];const float values[]={whiteNits/80,whiteNits/80,whiteNits/80,premultiplied?1.f:0.f};std::memcpy(scaleValue,values,sizeof values);
    definitions={0x05000051u,reg(0xa00f0000,2,unsigned(scale)),scaleValue[0],scaleValue[1],scaleValue[2],scaleValue[3]};
    for(const auto& ins:tail){
        if(ins.op==31)continue;
        if(ins.op==47||ins.op==48)return false;
        auto& destination=ins.op==81?definitions:body;destination.push_back(transfer[ins.at]);
        for(unsigned j=1;j<ins.count;++j){Word w=transfer[ins.at+j],mapped;
            if(ins.op==81&&j>1){destination.push_back(w);continue;}
            if(!remap(w,mapped))return false;destination.push_back(mapped);
        }
    }
    std::vector<Word> result;result.reserve(count+definitions.size()+body.size());result.push_back(original[0]);result.insert(result.end(),definitions.begin(),definitions.end());
    for(const auto& ins:old){
        result.push_back(original[ins.at]);
        for(unsigned j=1;j<ins.count;++j){Word w=original[ins.at+j];
            if(j==1&&type(w)==8)w=reg(w,0,unsigned(capture));result.push_back(w);
        }
    }
    result.insert(result.end(),body.begin(),body.end());result.push_back(65535);output.swap(result);
    if(info)*info={major,unsigned(capture),unsigned(scale),originalSlots,addedSlots};return true;
}
}
