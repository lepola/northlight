#pragma once
#include <cstddef>
#include <cstdint>
#include "replay_bounds.h"
#include "shader_tags.h"
#include "patch_shadow_shader.h"

// Stock 3.3.5a DetailDoodad VS2/VS3, six lighting permutations each.
// The complete bytecode identities are distinct from Terrain. Their c4..7
// projection layout alone does not make their alpha-cut geometry terrain.
namespace NorthlightDetailShadow {
inline bool shader(std::uint64_t hash){
    constexpr std::uint64_t hashes[]={
        0x7f2707e33c6c97beULL,0x6bee6c0fc22ac5b2ULL,0x279074f2d7f6ea1eULL,
        0x87aacbe7c33bb84fULL,0x8e0b9bf5d6eff0ffULL,0xb4cb4906153b6683ULL,
        0xd8f18d8080e6bb3aULL,0x4963565c635e7d6dULL,0x28d133339d7839aeULL,
        0x723e54bfc91c3bffULL,0xe2e26d9aa7c0d59cULL,0xc8e41181431a84c7ULL};
    for(auto value:hashes)if(value==hash)return true;
    return false;
}
// The stock DetailDoodad pixel shader multiplies texture alpha by COLOR1.x
// (the c9 distance fade), not COLOR0.a (the native lighting/shadow factor).
// Swap those two scalar output destinations in the SHADOW-ONLY VS copy so
// ShadowReplayPS can consume the right alpha. No extra instruction/register;
// native bytecode, RGB, UVs and position are untouched.
inline bool shadowFade(const std::vector<std::uint32_t>& input,std::vector<std::uint32_t>& output){
    using namespace NorthlightShadowShader;
    if(input.size()<2||input.size()>16384||!shader(NorthlightShaderTags::fnv1a(input.data(),input.size()*4)))return false;
    const unsigned major=(input[0]>>8)&255;if(major!=2&&major!=3)return false;
    unsigned color[2]={0,1};bool declared[2]={major==2,major==2};
    std::size_t alpha=0,fade=0;
    for(std::size_t at=1;at<input.size();){
        const unsigned op=input[at]&65535;
        if(op==65535){if(at+1!=input.size())return false;break;}
        const unsigned n=op==65534?(input[at]>>16)&32767:(input[at]>>24)&15;
        if(n>input.size()-at-1)return false;
        if(op==31&&major==3&&n==2&&regType(input[at+2])==6&&(input[at+1]&15)==10){
            const unsigned index=(input[at+1]>>16)&15;
            if(index<2){if(declared[index])return false;color[index]=regIndex(input[at+2]);declared[index]=true;}
        }else if(op!=65534&&op!=31&&op!=81&&n){
            const Word destination=input[at+1];const unsigned type=major==3?6:5,mask=(destination>>16)&15;
            if(regType(destination)==type&&regIndex(destination)==color[0]&&(mask&8)){
                if(alpha||mask!=8||op!=1||n!=2||input[at+2]!=0x90ff0002u)return false;
                alpha=at+1;
            }
            if(regType(destination)==type&&regIndex(destination)==color[1]&&(mask&1)){
                if(fade||mask!=1)return false;fade=at+1;
            }
        }
        at+=n+1;
    }
    if(!declared[0]||!declared[1]||color[0]==color[1]||!alpha||!fade)return false;
    auto result=input;
    const unsigned type=major==3?6:5;
    result[fade]=(replaceReg(result[fade],type,color[0])&~Word(15u<<16))|(8u<<16);
    result[alpha]=(replaceReg(result[alpha],type,color[1])&~Word(15u<<16))|(1u<<16);
    output.swap(result);return true;
}
// Project the signature-proved world POSITION0 directly with c4..7. Keep
// c0..3 and the original view-position temporary for the native c9 fade.
inline bool shadowVertex(const std::vector<std::uint32_t>& input,std::vector<std::uint32_t>& output){
    using namespace NorthlightShadowShader;
    std::vector<Word> result;if(!shadowFade(input,result))return false;
    std::size_t arithmetic[8]={};unsigned count=0;
    for(std::size_t at=1;at<input.size();){
        const unsigned op=input[at]&65535;if(op==65535)break;
        const unsigned n=op==65534?(input[at]>>16)&32767:(input[at]>>24)&15;
        if(op!=31&&op!=81&&op!=65534&&count<8)arithmetic[count++]=at;
        at+=n+1;
    }
    if(count!=8)return false;
    const unsigned view=regIndex(input[arithmetic[3]+1]);
    const unsigned components[]={1,0,2,3},constants[]={5,4,6,7};
    for(unsigned step=0;step<4;++step){
        const auto at=arithmetic[4+step];const unsigned op=input[at]&65535,n=(input[at]>>24)&15;
        if(op!=(step?4u:5u)||n!=(step?4u:3u))return false;
        unsigned found=0;bool matrix=false;
        for(unsigned i=2;i<=n;++i){const Word token=input[at+i];
            if(regType(token)==2&&regIndex(token)==constants[step])matrix=true;
            if(regType(token)!=0||regIndex(token)!=view)continue;
            if(((token>>16)&255)!=components[step]*0x55u||token&0x0f002000u)return false;
            if(step==3){
                if(i>3)return false;
                result[at]=(result[at]&~Word(0x0f00ffffu))|(3u<<24)|2u; // ADD c7, accumulator; implicit world w=1.
                result.erase(result.begin()+at+i);
            }else result[at+i]=replaceReg(token,1,0);
            ++found;
        }
        if(found!=1||!matrix)return false;
    }
    output.swap(result);return true;
}
// Stock DetailDoodad PS2/PS3: texture0 alpha times the VS distance fade.
inline bool pixelShader(std::uint64_t hash){
    constexpr std::uint64_t hashes[]={0x4c4e8c78e53fb551ULL,0x57d8def2328eda4fULL,0x6ef54f064e5141ddULL,
        0xa2a90e82b194e2a9ULL,0xa8a0f298ca161763ULL,0xc98a355a07714c0fULL,0xe56e7f7bd7a96a88ULL,
        0xf2d7a148799ec7d0ULL,0xf40c49694a66147dULL,0xfa3d15fb0c97ba0cULL};
    for(auto value:hashes)if(value==hash)return true;
    return false;
}
// Only the native straight-alpha material gets a blended shadow exception.
// Numeric values are the stable D3DBLEND/D3DBLENDOP wire values.
inline bool blendedMaterial(bool knownPixel,unsigned source,unsigned destination,unsigned operation){
    return knownPixel&&source==5&&destination==6&&operation==1;
}
struct Budget {
    // 8,192 is a per-draw safety limit, not a whole visible meadow.
    // Storage remains split across the ordinary guarded replay snapshots.
    static constexpr unsigned MaxDraws=128,MaxDrawVertices=8192,MaxVertices=131072;
    static constexpr std::size_t MaxBytes=8u*1024u*1024u;
    unsigned attempts=0,readVertices=0,draws=0,vertices=0;std::size_t bytes=0,readBytes=0;
    bool room(unsigned count)const{return count&&count<=MaxDrawVertices&&attempts<MaxDraws&&count<=MaxVertices-readVertices&&readBytes<MaxBytes;}
    bool reserve(unsigned count,std::size_t size){if(!room(count)||size>MaxBytes-readBytes)return false;++attempts;readVertices+=count;readBytes+=size;return true;}
    bool take(unsigned count,std::size_t size){
        if(!count||count>MaxDrawVertices||draws>=MaxDraws||count>MaxVertices-vertices||size>MaxBytes-bytes)return false;
        ++draws;vertices+=count;bytes+=size;return true;
    }
};
// DetailDoodad's signature-proved prefix is affine and consumes POSITION0
// only. Enclose its whole source box once instead of evaluating every vertex
// through the generic interpreter. Input uncertainty is propagated by the
// existing conservative interval evaluator, including matrix rounding/FTZ.
inline bool bounds(const NorthlightActorDeformation::Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
                   const D3DVERTEXELEMENT9* elements,unsigned count,const float* constants,const float* inverse,
                   NorthlightReplayBounds::Bounds& output){
    output={};
    if(!elements||!constants||!inverse||!mesh.vertexCount||mesh.vertexCount>Budget::MaxDrawVertices||
       program.operations.size()!=4||program.inputs.size()!=1||program.inputs[0].reg!=0||program.inputs[0].usage!=0||program.inputs[0].index)return false;
    const D3DVERTEXELEMENT9* position=nullptr;
    for(unsigned i=0;i<count;++i){const auto& e=elements[i];if(e.Stream==0xff)break;if(e.Usage==0&&e.UsageIndex==0){if(position)return false;position=&e;}}
    if(!position||position->Stream!=0||position->Type!=D3DDECLTYPE_FLOAT3||position->Method!=D3DDECLMETHOD_DEFAULT)return false;
    const auto& stream=mesh.streams[0];
    if(unsigned(position->Offset)+12>stream.stride||stream.bytes.size()<size_t(mesh.vertexCount)*stream.stride)return false;
    float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
    for(unsigned i=0;i<mesh.vertexCount;++i){float xyz[3];std::memcpy(xyz,stream.bytes.data()+size_t(i)*stream.stride+position->Offset,12);
        for(unsigned axis=0;axis<3;++axis){if(!std::isfinite(xyz[axis]))return false;low[axis]=std::min(low[axis],xyz[axis]);high[axis]=std::max(high[axis],xyz[axis]);}}
    NorthlightReplayBounds::detail::Vector inputs[16]={},view;
    for(unsigned axis=0;axis<3;++axis){const float middle=float((double(low[axis])+high[axis])*.5);
        inputs[0][axis]={middle,std::max(double(middle)-low[axis],double(high[axis])-middle)};}
    inputs[0][3]={1,0};
    if(!NorthlightReplayBounds::detail::evaluateIntervals(program,inputs,constants,view))return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(inverse[i]))return false;
    if(inverse[3]!=0||inverse[7]!=0||inverse[11]!=0||inverse[15]!=1)return false;
    for(unsigned axis=0;axis<3;++axis){using namespace NorthlightReplayBounds::detail;
        Value world=mul(view[3],{inverse[12+axis],0});double magnitude=std::fabs(world.value)+world.error;
        for(unsigned k=0;k<3;++k){const auto term=mul(view[k],{inverse[4*k+axis],0});magnitude+=std::fabs(term.value)+term.error;world=add(world,term);}
        world.error+=roundError(magnitude)*8+.001;
        if(!std::isfinite(world.value)||!std::isfinite(world.error)||std::fabs(world.value)+world.error>1000000)return false;
        output.low[axis]=std::nextafter(float(double(world.value)-world.error),-INFINITY);
        output.high[axis]=std::nextafter(float(double(world.value)+world.error),INFINITY);
    }
    output.valid=true;return true;
}
// Read-only sampled-frame evidence for the receiver path. Depth-writing grass
// drawn before WorldComposite already receives the normal depth-based effects;
// a late or non-depth-writing draw needs a separate path, never a guessed mask.
struct ReceiverCensus {
    unsigned before=0,after=0,depthWrites=0,blended=0,alphaCut=0,alreadyResolved=0,worldDepth=0,stateFailures=0;
    void observe(bool late,bool states,bool zwrite,bool blend,bool alpha,bool resolved,bool mainDepth){
        ++(late?after:before);
        if(!states){++stateFailures;return;}
        depthWrites+=zwrite;blended+=blend;alphaCut+=alpha;alreadyResolved+=resolved;worldDepth+=mainDepth;
    }
};
}
