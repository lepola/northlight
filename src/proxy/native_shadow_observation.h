#pragma once
#include "patch_shadow_shader.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

// Observation only. These programs expose a native shadow input domain, not a
// proved world transform. They must never authorize the main-view capture gate.
namespace NorthlightNativeShadow {
using Word=std::uint32_t;
enum class Skin:unsigned {Unknown,Rigid,Single,Weighted};
struct Range {unsigned first=0,count=0;};
struct Proof {unsigned major=0;Skin skin=Skin::Unknown;bool affine=false,relativePalette=false;std::array<Range,2> ranges{};};
namespace Detail {
inline Word dst(unsigned type,unsigned index,unsigned mask=15){return NorthlightShadowShader::replaceReg(0x80000000u|(mask<<16),type,index);}
inline Word src(unsigned type,unsigned index,unsigned swizzle=0xe4,bool relative=false){return NorthlightShadowShader::replaceReg(0x80000000u|(swizzle<<16)|(relative?0x2000u:0),type,index);}
inline Word bits(float value){Word out;std::memcpy(&out,&value,4);return out;}
struct Cursor {
    const Word* words;std::size_t count,at=1;unsigned major;
    bool comments(){while(at<count&&(words[at]&65535)==65534){const std::size_t n=(words[at]>>16)&32767;if(n>count-at-1)return false;at+=n+1;}return true;}
    bool op(unsigned code,std::initializer_list<Word> args){
        if(!comments()||at>=count||args.size()>count-at-1||words[at]!=(major==1?Word(code):Word(code)|(Word(args.size())<<24)))return false;
        ++at;for(auto value:args)if(words[at++]!=value)return false;return true;
    }
    bool end(){return comments()&&at+1==count&&words[at]==65535;}
};
inline bool matches(const Word* words,std::size_t count,unsigned major,Skin skin){
    Cursor c{words,count,1,major};const bool rigid=skin==Skin::Rigid,weighted=skin==Skin::Weighted;
    const unsigned uv=rigid?1:weighted?3:2;
    if(!c.op(81,{dst(2,0),bits(rigid?1.f:major==1?765.005859375f:3.f),bits(rigid?0.f:1.f),0,0})||
       !c.op(31,{0x80000000u,dst(1,0)}))return false;
    if(weighted&&!c.op(31,{0x80000001u,dst(1,1)}))return false;
    if(!rigid&&!c.op(31,{0x80000002u,dst(1,weighted?2:1)}))return false;
    if(!c.op(31,{0x80000005u,dst(1,uv)}))return false;
    if(major==3&&(!c.op(31,{0x80000000u,dst(6,0)})||!c.op(31,{0x80000005u,dst(6,1,3)})||!c.op(31,{0x80010005u,dst(6,2,7)})))return false;
    if(rigid){
        if(!c.op(1,{dst(0,0,8),src(2,0,0)}))return false;
        for(unsigned row=0;row<3;++row)if(!c.op(9,{dst(0,0,1u<<row),src(2,31+row),src(1,0)}))return false;
    }else if(!weighted){
        if(major==1){
            if(!c.op(1,{dst(0,0,8),src(2,0,0x55)})||!c.op(5,{dst(0,1,1),src(1,1,0xaa),src(2,0,0)})||!c.op(1,{dst(3,0,1),src(0,1,0)}))return false;
            for(unsigned row=0;row<3;++row)if(!c.op(9,{dst(0,0,1u<<row),src(2,31+row,0xe4,true),src(1,0)}))return false;
        }else{
            const Word index=src(1,1,0),scale=src(2,0,0);
            if(!c.op(5,{dst(0,0,1),major==3?scale:index,major==3?index:scale})||!c.op(46,{dst(3,0,1),src(0,0,0)}))return false;
            for(unsigned row=0;row<3;++row)if(!c.op(9,{dst(0,0,1u<<row),src(2,31+row,0xe4,true),src(3,0,0),src(1,0)}))return false;
            if(!c.op(1,{dst(0,0,8),src(2,0,0x55)}))return false;
        }
    }else if(major==1){
        if(!c.op(1,{dst(0,0,8),src(2,0,0x55)})||!c.op(5,{dst(0,1),src(1,2,0xc6),src(2,0,0)}))return false;
        for(unsigned row=0;row<3;++row){const Word palette=src(2,31+row,0xe4,true);
            if(!c.op(1,{dst(3,0,1),src(0,1,0x55)})||!c.op(5,{dst(0,2),src(1,1,0x55),palette})||
               !c.op(1,{dst(3,0,1),src(0,1,0)})||!c.op(4,{dst(0,2),palette,src(1,1,0xaa),src(0,2)})||
               !c.op(1,{dst(3,0,1),src(0,1,0xaa)})||!c.op(4,{dst(0,2),palette,src(1,1,0),src(0,2)})||
               !c.op(1,{dst(3,0,1),src(0,1,0xff)})||!c.op(4,{dst(0,row==2?1:2),palette,src(1,1,0xff),src(0,2)})||
               !c.op(9,{dst(0,0,1u<<row),src(0,row==2?1:2),src(1,0)}))return false;
        }
    }else{
        const Word indices=src(1,2),scale=src(2,0,0);
        if(!c.op(5,{dst(0,0),major==3?scale:indices,major==3?indices:scale})||!c.op(46,{dst(3,0),src(0,0)}))return false;
        for(unsigned row=0;row<3;++row){const unsigned temp=row?1:0;const Word palette=src(2,31+row,0xe4,true);
            if(!c.op(5,{dst(0,temp),src(1,1,0x55),palette,src(3,0,0x55)}))return false;
            for(unsigned channel:{0u,2u,3u})if(!c.op(4,{dst(0,temp),palette,src(3,0,channel*0x55),src(1,1,channel*0x55),src(0,temp)}))return false;
            if(!c.op(9,{dst(0,0,1u<<row),src(0,temp),src(1,0)}))return false;
        }
        if(!c.op(1,{dst(0,0,8),src(2,0,0x55)}))return false;
    }
    if(!rigid){
        for(unsigned row=0;row<3;++row)if(!c.op(9,{dst(0,1,1u<<row),src(2,14+row),src(0,0)}))return false;
        if(!c.op(1,{dst(0,1,8),src(2,0,0x55)}))return false;
    }
    const Word position=src(0,rigid?0:1);
    for(unsigned row=0;row<4;++row)if(!c.op(9,{dst(major==3?6:4,0,1u<<row),src(2,2+row),position}))return false;
    return c.op(1,{dst(6,major==3?2:1,7),position})&&
        c.op(9,{dst(6,major==3?1:0,1),src(2,6),src(1,uv)})&&
        c.op(9,{dst(6,major==3?1:0,2),src(2,7),src(1,uv)})&&c.end();
}
}
// Complete straight-line register math, declarations, DEF and suffix proof.
// Unknown permutations stay untagged; bytecode analysis occurs only at creation.
inline bool analyze(const Word* words,std::size_t count,Proof& output){
    output={};if(!words||count<4||count>512)return false;
    const unsigned major=(words[0]>>8)&255;
    if(major<1||major>3||words[0]!=(major==1?0xfffe0101u:major==2?0xfffe0200u:0xfffe0300u))return false;
    for(Skin skin:{Skin::Rigid,Skin::Single,Skin::Weighted}){
        if(Detail::matches(words,count,major,skin)){
            output.major=major;output.skin=skin;output.affine=output.relativePalette=skin!=Skin::Rigid;
            output.ranges={Range{2,6},Range{output.affine?14u:31u,3}};return true;
        }
    }
    return false;
}
inline bool valid(const Proof& p){return p.major>=1&&p.major<=3&&p.skin>=Skin::Rigid&&p.skin<=Skin::Weighted&&
    p.affine==(p.skin!=Skin::Rigid)&&p.relativePalette==p.affine&&p.ranges[0].first==2&&p.ranges[0].count==6&&
    p.ranges[1].first==(p.affine?14u:31u)&&p.ranges[1].count==3;}
class Limiter {
    std::array<unsigned,9> counts_{};std::array<std::uint32_t,9> previous_{};
    unsigned total_=0,inFrame_=0;std::uint32_t frame_=0;bool seenFrame_=false;
public:
    static constexpr unsigned SessionLimit=64,PerFrame=2,PerProfile=8,FrameGap=120;
    unsigned total()const{return total_;}
    bool take(bool diagnostics,bool profile,std::uint32_t frame,const Proof& proof){
        if(!diagnostics||!profile||!valid(proof)||total_>=SessionLimit)return false;
        const unsigned slot=(proof.major-1)*3+unsigned(proof.skin)-1;
        if(counts_[slot]>=PerProfile||(counts_[slot]&&frame-previous_[slot]<FrameGap))return false;
        if(!seenFrame_||frame_!=frame){seenFrame_=true;frame_=frame;inFrame_=0;}
        if(inFrame_==PerFrame)return false;
        ++inFrame_;++counts_[slot];++total_;previous_[slot]=frame;return true;
    }
};
struct Sample {
    Proof proof;std::uint32_t frame=0,draw=0;std::uint64_t shaderHash=0;
    unsigned knownRows=0,finiteRows=0;std::array<std::array<float,4>,9> rows{};
};
// Caller holds the existing mirror guard. No getters, locks, GPU readback or
// relative-palette reads: absent authoritative mirror rows remain unknown.
inline Sample sample(const Proof& proof,const float (*values)[4],const bool* known,bool active,
                     std::uint32_t frame,std::uint32_t draw,std::uint64_t hash){
    Sample out;out.proof=proof;out.frame=frame;out.draw=draw;out.shaderHash=hash;
    if(!valid(proof)||!active||!values||!known)return out;
    unsigned row=0;
    for(auto range:proof.ranges){
        for(unsigned i=0;i<range.count;++i,++row){
            if(!known[range.first+i])continue;
            std::memcpy(out.rows[row].data(),values[range.first+i],16);out.knownRows|=1u<<row;
            bool finite=true;for(float value:out.rows[row])finite=finite&&std::isfinite(value);if(finite)out.finiteRows|=1u<<row;
        }
    }
    return out;
}
// NSH1, 46 little-endian words: size, frame/draw, hash, proof flags, two
// first/count ranges, known/finite masks, then nine raw float4 bit patterns.
inline constexpr std::size_t SerializedBytes=46*4;
inline bool serialize(const Sample& s,unsigned char* out,std::size_t capacity,std::size_t& written){
    static_assert(sizeof(float)==4);written=0;
    if(!out||capacity<SerializedBytes||!valid(s.proof)||(s.knownRows&~511u)||(s.finiteRows&~s.knownRows))return false;
    auto word=[&](Word value){for(unsigned byte=0;byte<4;++byte)out[written++]=static_cast<unsigned char>(value>>(8*byte));};
    word(0x3148534e);word(SerializedBytes);word(s.frame);word(s.draw);word(Word(s.shaderHash));word(Word(s.shaderHash>>32));
    word(s.proof.major|(unsigned(s.proof.skin)<<8)|(unsigned(s.proof.affine)<<16)|(unsigned(s.proof.relativePalette)<<17));
    for(auto range:s.proof.ranges)word(range.first|(range.count<<16));word(s.knownRows|(s.finiteRows<<16));
    for(auto row:s.rows)for(float value:row)word(Detail::bits(value));return true;
}
}
