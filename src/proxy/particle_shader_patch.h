#pragma once
#include <bitset>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <unordered_map>
#include <vector>
#include "patch_shadow_shader.h"

// Particle mask variant of one of the game's own pixel shaders (0.3.203), built at the first
// eligible draw from the bytecode the game supplied (the DLL carries no shader bytes). A
// word-for-word port of patch() in renderer/particle_shader_patch.py, which stays the reference
// oracle: oC0 is renamed to a spare temporary rT and before END come `mov oC0, rT` (RT0 is exactly
// the game's) and the mask write to oC1 for one blend kind: 0 alpha over (1,1,1,sat(a)), 1 additive
// by alpha (1,1,1,sat(a)*sat(max rgb)), 2 additive by colour sat(max rgb).xxxx; the 1s come from a
// def of a free constant. Same rejection set as the water patch (no spare temporary, oC1 or oDepth
// written, oC0 written inside flow control, unknown opcodes, predication, ...).
// tests/test_particle_shader_patch.py cross-checks both. patch() is pure: no D3D, no logging.
namespace NorthlightParticleShaderPatch {
using Word=std::uint32_t;
using NorthlightShadowShader::regType;
using NorthlightShadowShader::regIndex;
using NorthlightShadowShader::replaceReg;
// The order and the texts are renderer/particle_shader_patch.py's REASONS; the last three are the cache's own.
enum Reason:unsigned {
    Ok,Empty,UnsupportedModel,BadEnd,Truncated,MissingEnd,Predicated,BadDcl,BadDef,UnsupportedOpcode,UnbalancedFlow,OperandCount,ParameterMarker,ConditionalOutput,MrtDepthWrite,
    RelativeDest,BadRelative,ExtraOperands,IncompleteOutput,NoTemporary,RelativeConstant,NoConstant,UnknownKind,
    NoBytecode,CreateFailed,CacheFull,ReasonCount};
inline const char* reasonName(unsigned r) {
    static const char* const names[ReasonCount]={"ok","empty","unsupported shader model","bad END","truncated","missing END","predicated/coissued","bad DCL","bad DEF","unsupported control/opcode","unbalanced control flow",
        "operand count","parameter marker","conditional output","MRT/depth write","relative dest","bad relative","extra operands","incomplete output","no free temporary",
        "relative constant","no free constant","unknown kind","no bytecode","create failed","cache full"};
    return r<ReasonCount?names[r]:"?";
}
inline int arity(unsigned op,unsigned major) {
    if(op==37)return major==3?2:4;
    switch(op) {
    case 0:case 29:case 42:case 43:return 0;
    case 40:case 65:return 1;
    case 1:case 6:case 7:case 14:case 15:case 16:case 19:case 27:case 35:case 36:case 41:case 46:case 78:case 79:case 91:case 92:return 2;
    case 2:case 3:case 5:case 8:case 9:case 10:case 11:case 12:case 13:case 17:case 20:case 21:case 22:case 23:case 24:
    case 32:case 33:case 66:case 89:case 94:case 95:return 3;
    case 4:case 18:case 34:case 88:case 90:return 4;
    case 93:return 5;
    default:return -1;
    }
}
inline Word dst(unsigned type,unsigned index,unsigned mask=15) {return replaceReg(0x80000000u|(mask<<16),type,index);}
inline Word src(unsigned type,unsigned index,unsigned swizzle=0xe4) {return replaceReg(0x80000000u|(swizzle<<16),type,index);}
constexpr Word kSat=0x00100000u;
// kind: 0 alpha over, 1 additive by alpha, 2 additive by colour. Ok and the patched words in output, else the Python ValueError's reason.
inline Reason patch(const Word* w,std::size_t count,unsigned kind,std::vector<Word>& output) {
    if(kind>2)return UnknownKind; // (the oracle checks the kind before the bytecode; an empty program is rejected first here and there)
    if(!w||!count)return Empty;
    if(w[0]!=0xffff0200u&&w[0]!=0xffff0300u)return UnsupportedModel;
    struct Op {std::size_t p;unsigned op;std::size_t n;};
    std::vector<Op> ops;std::size_t end=0;
    for(std::size_t p=1;;) {
        if(p>=count)return MissingEnd;
        const unsigned op=w[p]&65535;
        if(op==65535) {
            if(w[p]!=65535||p+1!=count)return BadEnd;
            end=p;break;
        }
        const std::size_t n=op==65534?(w[p]>>16)&32767:(w[p]>>24)&15;
        if(p+n>=count)return Truncated;
        ops.push_back({p,op,n});p+=n+1;
    }
    const unsigned major=(w[0]>>8)&255;
    std::bitset<256> used[32];
    auto use=[&](unsigned type,unsigned index){if(index<256)used[type&31].set(index);};
    auto isUsed=[&](unsigned type,unsigned index){return index<256&&used[type&31].test(index);};
    std::vector<std::size_t> params;
    int depth=0;unsigned mask=0;std::size_t firstOp=0;bool haveFirst=false,relativeConst=false;
    for(const Op& o:ops) {
        const std::size_t p=o.p;const Word* a=w+p+1;
        if(o.op==65534)continue;
        if(w[p]&0xf0000000u)return Predicated;
        if(!haveFirst){haveFirst=true;firstOp=p;}
        if(o.op==31) {
            if(o.n!=2)return BadDcl;
            use(regType(a[1]),regIndex(a[1]));continue;
        }
        if(o.op==81) {
            if(o.n!=5)return BadDef;
            use(regType(a[0]),regIndex(a[0]));continue;
        }
        if(o.op==48||o.op==47)continue;
        const int formal=arity(o.op,major);
        if(formal<0)return UnsupportedOpcode;
        if(o.op==27||o.op==40||o.op==41)++depth;
        if(o.op==29||o.op==43) {if(--depth<0)return UnbalancedFlow;}
        std::size_t q=0;
        for(int operand=0;operand<formal;++operand) {
            if(q>=o.n)return OperandCount;
            const Word t=a[q];const unsigned k=regType(t),i=regIndex(t);
            if(!(t&0x80000000u))return ParameterMarker;
            use(k,i);params.push_back(p+1+q);
            if(operand==0&&o.op!=27&&o.op!=40&&o.op!=41&&o.op!=65) {
                if(k==8&&i==0) {
                    if(depth)return ConditionalOutput;
                    mask|=(t>>16)&15;
                } else if(k==8||k==9)return MrtDepthWrite;
                if(t&0x2000)return RelativeDest;
            }
            if(operand>0&&k==2&&(t&0x2000))relativeConst=true;
            if(operand==2&&o.op>=20&&o.op<=24) {
                const unsigned rows=o.op==20||o.op==22?4:o.op==24?2:3;
                for(unsigned r=0;r<rows;++r)use(k,i+r);
            }
            ++q;
            if(t&0x2000) {
                if(q>=o.n)return BadRelative;
                const unsigned rk=regType(a[q]);
                if(rk!=3&&rk!=15)return BadRelative;
                use(rk,regIndex(a[q]));++q;
            }
        }
        if(q!=o.n)return ExtraOperands;
    }
    if(depth)return UnbalancedFlow;
    if(mask!=15)return IncompleteOutput;
    const unsigned limit=major==3?32:12;
    unsigned spare=0,spare2=0;
    while(spare<limit&&isUsed(0,spare))++spare;
    if(spare==limit)return NoTemporary;
    if(kind) {
        spare2=spare+1;while(spare2<limit&&isUsed(0,spare2))++spare2;
        if(spare2==limit)return NoTemporary;
    }
    unsigned constant=0;
    if(kind<2) {
        if(relativeConst)return RelativeConstant;
        const unsigned climit=major==3?224:32;
        unsigned c=climit;bool found=false;
        while(c>0&&!found){--c;found=!isUsed(2,c);}
        if(!found)return NoConstant;
        constant=c;
    }
    std::vector<Word> out(w,w+count);
    for(std::size_t p:params)
        if(regType(out[p])==8&&regIndex(out[p])==0)out[p]=replaceReg(out[p],0,spare);
    auto T=[&](unsigned swizzle){return src(0,spare,swizzle);};
    auto S2=[&](unsigned swizzle){return src(0,spare2,swizzle);};
    std::vector<Word> suffix={0x02000001,dst(8,0),src(0,spare)};
    auto add=[&](std::initializer_list<Word> words){suffix.insert(suffix.end(),words);};
    if(kind<2)add({0x02000001,dst(8,1,7),src(2,constant)});
    if(kind==0)add({0x02000001,dst(8,1,8)|kSat,T(0xff)});
    else {
        add({0x0300000b,dst(0,spare2,1)|kSat,T(0x00),T(0x55),0x0300000b,dst(0,spare2,1)|kSat,S2(0x00),T(0xaa)});
        if(kind==1)add({0x02000001,dst(0,spare2,2)|kSat,T(0xff),0x03000005,dst(8,1,8),S2(0x00),S2(0x55)});
        else add({0x02000001,dst(8,1),S2(0x00)});
    }
    suffix.push_back(0x0000ffff);
    out.erase(out.begin()+std::ptrdiff_t(end),out.end());
    out.insert(out.end(),suffix.begin(),suffix.end());
    if(kind<2) {
        const Word def[]={0x05000051,dst(2,constant),0x3f800000,0x3f800000,0x3f800000,0x3f800000};
        out.insert(out.begin()+std::ptrdiff_t(firstOp),def,def+6);
    }
    output.swap(out);return Ok;
}

// The patched variants of the game's pixel shaders, by the original's (raw) address: one lazily built shader per blend kind, kept until the
// original's address is registered again (the game freed it, nothing is ever erased otherwise), the device resets or the renderer releases its resources.
// Ext: the extension device (CreatePixelShader); Shader: its pixel shader type (GetFunction, Release).
template<class Ext,class Shader> class Cache {
public:
    static constexpr std::size_t kMaxShaders=48,kMaxLogged=64;
    struct Result {Shader* shader=nullptr;Reason reason=Ok;bool log=false;std::uint64_t hash=0;};
    ~Cache(){clear();}
    // The patched shader for `original` and `kind`: built on the first ask, a rejection is remembered too (so each draw costs one lookup).
    // log: the first time this (bytecode hash, kind) is seen, for the caller to write one line.
    Result get(Ext* ext,Shader* original,unsigned kind) {
        Result r;if(!original||kind>2){r.reason=UnknownKind;return r;}
        auto it=entries_.find(original);
        if(it==entries_.end()) {
            if(entries_.size()>=kMaxShaders){r.reason=CacheFull;return r;}
            it=entries_.emplace(original,Entry()).first;
        }
        Entry& e=it->second;
        if(e.state[kind]){r.shader=e.variant[kind];r.reason=e.reason[kind];r.hash=e.hash;return r;}
        e.state[kind]=1;
        std::vector<Word> words;UINT size=0;
        if(FAILED(original->GetFunction(nullptr,&size))||size<8||size>1024*1024||size%4)e.reason[kind]=NoBytecode;
        else {
            words.assign(size/4,0);
            if(FAILED(original->GetFunction(words.data(),&size)))e.reason[kind]=NoBytecode;
        }
        if(!words.empty()){
            e.hash=hashOf(words);
            std::vector<Word> patched;
            e.reason[kind]=patch(words.data(),words.size(),kind,patched);
            if(e.reason[kind]==Ok&&FAILED(ext->CreatePixelShader(Code{patched.data()},&e.variant[kind]))){e.variant[kind]=nullptr;e.reason[kind]=CreateFailed;}
        }
        r.shader=e.variant[kind];r.reason=e.reason[kind];r.hash=e.hash;
        const std::uint64_t key=e.hash*4+kind;
        bool seen=false;for(std::uint64_t k:logged_)seen=seen||k==key;
        if(!seen&&logged_.size()<kMaxLogged){logged_.push_back(key);r.log=true;}
        return r;
    }
    // The game registered a pixel shader at this address: whatever was built for a freed shader there is stale.
    void forget(Shader* original) {
        auto it=entries_.find(original);if(it==entries_.end())return;
        release(it->second);entries_.erase(it);
    }
    void clear() {for(auto& e:entries_)release(e.second);entries_.clear();}
    std::size_t size()const{return entries_.size();}
    std::size_t variants()const{std::size_t n=0;for(auto& e:entries_)for(auto* v:e.second.variant)n+=v!=nullptr;return n;}
private:
    struct Code { // the words as whatever const pointer type the device's CreatePixelShader takes (DWORD is not uint32_t on Windows)
        const Word* words;
        template<class T> operator const T*()const{return reinterpret_cast<const T*>(words);}
    };
    struct Entry {Shader* variant[3]={};unsigned char state[3]={};Reason reason[3]={Ok,Ok,Ok};std::uint64_t hash=0;};
    static std::uint64_t hashOf(const std::vector<Word>& words) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(words.data());
        std::uint64_t h=14695981039346656037ULL;
        for(std::size_t i=0;i<words.size()*4;++i)h=(h^bytes[i])*1099511628211ULL;
        return h;
    }
    static void release(Entry& e){for(auto*& v:e.variant)if(v){v->Release();v=nullptr;}}
    std::unordered_map<Shader*,Entry> entries_;std::vector<std::uint64_t> logged_;
};
} // namespace NorthlightParticleShaderPatch
