#pragma once
#include "patch_terrain_shadow.h"
#include "patch_shadow_shader.h"
#include <algorithm>
#include <array>

// Keep the authored terrain shadow outside the maps that replaced it. The
// original terrain shader already scales both diffuse and specular by S;
// substitute lerp(S, 1, coverage), rather than neutralising S everywhere.
namespace NorthlightTerrainCoverage {
using Word=std::uint32_t;
using NorthlightTerrainShadow::sourceType;
inline Word dst(unsigned type,unsigned index,unsigned mask=15){return 0x80000000u|((type&7)<<28)|((type&24)<<8)|index|(mask<<16);}
inline Word src(unsigned type,unsigned index,unsigned swizzle=0xe4){return (dst(type,index)&~0xf0000u)|(swizzle<<16);}
inline Word bits(float v){Word w;std::memcpy(&w,&v,4);return w;}
struct Op{std::size_t at;unsigned code,size;};
struct Program{
    unsigned major=0,slots=0;bool vertex=false;
    std::vector<Op> ops;std::array<bool,256> constants{};
    std::array<bool,32> temps{};std::array<bool,12> outputs{};std::array<bool,10> inputs{};
    std::size_t declEnd=1;
};
inline bool analyse(const Word* w,std::size_t count,Program& p){
    if(!w||count<3||count>16384||w[count-1]!=65535)return false;
    p.major=(w[0]>>8)&255;p.vertex=(w[0]>>16)==65534;
    if(w[0]!=0xfffe0300u&&w[0]!=0xffff0300u)return false;
    // Northlight's world path requires SM3. Older client shader paths retain
    // the existing neutralisation patch, without new interpolators/constants.
    if(p.major!=3)return false;
    for(std::size_t at=1;at<count;){
        unsigned op=w[at]&65535;if(op==65535)return at+1==count;
        if(op==65534){std::size_t n=1+((w[at]>>16)&32767);if(n>count-at)return false;at+=n;continue;}
        if(w[at]&0xf0000000u)return false;
        unsigned n=(w[at]>>24)&15;if(n+1>count-at)return false;
        if(op==31){if(n!=2)return false;p.declEnd=at+3;}
        if(op==81&&(n!=5||sourceType(w[at+1])!=2))return false;
        // Matrix macros can implicitly read further constants; these original
        // terrain programs expand them. Relative constants cannot be reserved.
        if(op>=20&&op<=24)return false;
        for(unsigned i=(op==31?2:1);i<=n;++i){
            if((op==81||op==48||op==47)&&i>1)break;
            Word t=w[at+i];if(!(t&0x80000000u))continue;
            if(t&0x2000)return false;
            unsigned type=sourceType(t),index=t&2047;
            if(type==2){if(index>=256)return false;p.constants[index]=true;}
            if(type==0){if(index>=32)return false;p.temps[index]=true;}
            if(p.vertex&&type==6){if(index>=12)return false;p.outputs[index]=true;}
            if(!p.vertex&&type==(p.major==3?1u:3u)){if(index>=10)return false;p.inputs[index]=true;}
        }
        p.ops.push_back({at,op,n});
        if(op!=31&&op!=81&&op!=48&&op!=47)p.slots+=NorthlightShadowShader::instructionSlots(op);
        at+=n+1;
    }
    return false;
}
inline bool vertex(const Word* w,std::size_t count,std::vector<Word>& out){
    out.clear();Program p;if(!analyse(w,count,p)||!p.vertex)return false;
    std::vector<Op> executable;int one=-1;unsigned lane=0;
    int varying=-1;std::size_t declaration=0;
    for(const auto& op:p.ops){
        if(op.code==81){for(unsigned c=0;c<4;++c)if(NorthlightTerrainShadow::wordFloat(w[op.at+2+c])==1.f){one=int(w[op.at+1]&2047);lane=c;}}
        else if(op.code==31){if(sourceType(w[op.at+2])==6&&(w[op.at+1]&0xfffff)==0x50005){
            if(varying>=0)return false;varying=int(w[op.at+2]&2047);declaration=op.at+2;}}
        else if(op.code!=48&&op.code!=47)executable.push_back(op);
    }
    if(one<0||executable.size()<4||p.slots>508)return false;
    const unsigned temp=w[executable[0].at+1]&2047;
    if(sourceType(w[executable[0].at+1])!=0)return false;
    // The signature-gated terrain input is world position. Prove the original
    // four-instruction affine view transform before exporting its xyz.
    const std::vector<std::vector<Word>> prefix={
        {5u|(3u<<24),dst(0,temp),src(2,1),src(1,0,0x55)},
        {4u|(4u<<24),dst(0,temp),src(1,0,0x00),src(2,0),src(0,temp)},
        {4u|(4u<<24),dst(0,temp),src(1,0,0xaa),src(2,2),src(0,temp)},
        {2u|(3u<<24),dst(0,temp),src(0,temp),src(2,3)}};
    for(unsigned i=0;i<4;++i){const auto& op=executable[i];auto expected=prefix[i];
        if(i==0&&w[op.at+2]==expected[3]&&w[op.at+3]==expected[2])std::swap(expected[2],expected[3]);
        if(op.size+1!=expected.size()||!std::equal(expected.begin(),expected.end(),w+op.at))return false;}
    std::size_t insert=executable[3].at+1+executable[3].size;bool live=true;unsigned writes=0;
    for(std::size_t i=4;i<executable.size();++i){const auto& op=executable[i];if(!op.size)continue;
        Word d=w[op.at+1];unsigned type=sourceType(d),index=d&2047,mask=(d>>16)&15;
        if(varying>=0&&type==6&&index==unsigned(varying)){
            if(!live||op.code!=1||op.size!=2||mask!=7||w[op.at+2]!=src(0,temp)||++writes>1)return false;
            insert=op.at+3;
        }
        if(type==0&&index==temp&&(mask&7))live=false;
        if(NorthlightShadowShader::arithmeticOperands(op.code,p.major)<0)return false;
    }
    if(varying>=0&&!writes)return false;
    const bool existing=varying>=0;
    if(!existing){varying=0;while(varying<12&&p.outputs[varying])++varying;if(varying==12)return false;}
    std::vector<Word> extra;
    if(!existing)extra={1u|(2u<<24),dst(6,unsigned(varying),7),src(0,temp)};
    extra.insert(extra.end(),{1u|(2u<<24),dst(6,unsigned(varying),8),src(2,unsigned(one),lane*0x55u)});
    out.assign(w,w+count);
    if(declaration)out[declaration]|=0xf0000u;
    out.insert(out.begin()+std::ptrdiff_t(insert),extra.begin(),extra.end());
    if(!existing){const Word decl[]={31u|(2u<<24),0x80050005u,dst(6,unsigned(varying))};out.insert(out.begin()+std::ptrdiff_t(p.declEnd),decl,decl+3);}
    return true;
}
struct PixelInfo{unsigned constants=0,addedSlots=0;};
inline bool pixel(const Word* w,std::size_t count,std::vector<Word>& out,PixelInfo& info,unsigned sources=2){
    out.clear();Program p;if(!analyse(w,count,p)||p.vertex)return false;
    if(sources<1||sources>2)return false;
    std::vector<Word> neutral;NorthlightTerrainShadow::PatchInfo proof;
    if(!NorthlightTerrainShadow::patch(w,count,neutral,&proof))return false;
    int varying=-1;std::size_t declaration=0;
    for(const auto& op:p.ops)if(op.code==31&&(w[op.at+1]&0xfffff)==0x50005){
        if(varying>=0)return false;varying=int(w[op.at+2]&2047);declaration=op.at+2;}
    const bool existing=varying>=0;
    if(!existing){varying=0;while(varying<10&&p.inputs[varying])++varying;if(varying==10)return false;}
    unsigned a=0;while(a<32&&p.temps[a])++a;
    if(a==32)return false;p.temps[a]=true;
    unsigned b=0;while(b<32&&p.temps[b])++b;if(b==32)return false;
    unsigned base=224-sources*3;
    for(;;){bool free=true;for(unsigned i=0;i<sources*3;++i)free&=!p.constants[base+i];if(free)break;if(!base)return false;--base;}
    for(unsigned i=0;i<sources*3;++i)p.constants[base+i]=true;
    unsigned k[2];for(unsigned j=0;j<2;++j){unsigned i=0;while(i<224&&p.constants[i])++i;if(i==224)return false;k[j]=i;p.constants[i]=true;}
    std::vector<std::size_t> reads;std::size_t insert=0;unsigned depth=0;
    for(const auto& op:p.ops){
        if(op.code==40||op.code==41||op.code==27||op.code==38)++depth;
        if(op.code==43||op.code==29||op.code==39){if(!depth)return false;--depth;}
        if(op.code==81||op.code==31||op.code==48||op.code==47)continue;
        const unsigned first=NorthlightTerrainShadow::firstOperandIsSource(op.code)?0:1;
        for(unsigned i=first;i<op.size;++i){std::size_t pos=op.at+1+i;Word t=w[pos];unsigned component;
            if(w[pos]!=neutral[pos]&&sourceType(t)==0&&(t&2047)==proof.shadowRegister&&NorthlightTerrainShadow::replicated(t,component)&&component==proof.shadowComponent){
                if(!insert){if(depth)return false;insert=op.at;}reads.push_back(pos);}}
    }
    if(reads.size()!=proof.replaced||!insert||depth)return false;
    std::vector<Word> extra;
    auto emit=[&](unsigned op,std::initializer_list<Word> args){extra.push_back(op|(unsigned(args.size())<<24));extra.insert(extra.end(),args);};
    emit(1,{dst(0,a,1),src(2,k[0],0xaa)});
    for(unsigned source=0;source<sources;++source){
        for(unsigned lane=0;lane<3;++lane)emit(9,{dst(0,b,1u<<lane),src(1,unsigned(varying)),src(2,base+source*3+lane)});
        emit(35,{dst(0,a,6),src(0,b,0x50)});
        emit(11,{dst(0,b,8),src(0,a,0x55),src(0,a,0xaa)});
        emit(2,{dst(0,b,8),src(2,k[0],0xff),src(0,b,0xff)|0x01000000u});
        emit(5,{dst(0,b,8)|0x00100000u,src(0,b,0xff),src(2,k[1],0xff)});
        // ADD with a negated source also works on native runtimes that reject
        // the raw SUB opcode, although the assembly subtraction is equivalent.
        emit(2,{dst(0,a,14),src(0,b,0x90),src(2,k[0],0x90)|0x01000000u});
        emit(2,{dst(0,b,7),src(2,k[1]),src(0,b)|0x01000000u});
        // All six strict bounds hold exactly when their minimum is positive.
        // Keep a.x (the source union) and b.w (the original edge fade) intact.
        emit(10,{dst(0,a,14),src(0,a),src(0,b,0x90)});
        emit(10,{dst(0,a,2),src(0,a,0x55),src(0,a,0xaa)});
        emit(10,{dst(0,a,2),src(0,a,0x55),src(0,a,0xff)});
        emit(88,{dst(0,b,8),src(0,a,0x55)|0x01000000u,src(2,k[0],0xaa),src(0,b,0xff)});
        emit(11,{dst(0,a,1),src(0,a,0x00),src(0,b,0xff)});
    }
    emit(18,{dst(0,b,8),src(0,a,0x00),src(2,k[0],0xff),src(0,proof.shadowRegister,proof.shadowComponent*0x55u)});
    info.constants=base;info.addedSlots=3+14*sources;
    if(p.slots+info.addedSlots>512)return false;
    out.assign(w,w+count);for(auto i:reads)out[i]=src(0,b,0xff);
    if(declaration)out[declaration]|=0xf0000u;
    out.insert(out.begin()+std::ptrdiff_t(insert),extra.begin(),extra.end());
    if(!existing){const Word decl[]={31u|(2u<<24),0x80050005u,dst(1,unsigned(varying))};out.insert(out.begin()+std::ptrdiff_t(p.declEnd),decl,decl+3);}
    // D3D9 half-texel offset and the same strict light-depth bounds as shadowMap.
    const float values[2][4]={{-.9949765625f,-.9930234375f,0,1},{.9930234375f,.9949765625f,1,1.f/.12f}};
    std::vector<Word> defs;for(unsigned j=0;j<2;++j){defs.insert(defs.end(),{81u|(5u<<24),dst(2,k[j])});for(float v:values[j])defs.push_back(bits(v));}
    out.insert(out.begin()+1,defs.begin(),defs.end());return true;
}
} // namespace NorthlightTerrainCoverage
