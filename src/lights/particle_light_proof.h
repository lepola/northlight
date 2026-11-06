#pragma once
#include "actor_deformation.h"
#include "m2_shader_families.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

// Registration-time native identities + output semantics, never VB layout guesses.
// Only the sampled triangle's actual interpolated COLOR0 and transformed UV0 are
// inputs to shade(); blend factors and triangle/world placement belong to callers.
namespace NorthlightParticleLightProof {
using Word=std::uint32_t;
using Four=NorthlightActorDeformation::Four;
struct Vertex {
    NorthlightActorDeformation::Program color,uv;
    bool valid=false;
    NorthlightActorDeformation::Program uv1;bool hasUv1=false;
};
struct Pixel {
    float rgbScale=0,alphaScale=0;
    unsigned sampler=0;
    bool textureAlpha=false,fog=false,valid=false;
    unsigned secondSampler=0,uv=0,secondUv=0,samples=1;
    bool secondRGB=false,secondRGBAlpha=false,secondAlpha=false;
};
inline bool initialized(const NorthlightActorDeformation::Program& p){
    using namespace NorthlightShadowShader;
    if(!p.textureCoordinates||(p.outputMask!=3&&p.outputMask!=15))return false;
    unsigned temp[33]={},address=0,inputs=0;
    for(const auto& i:p.inputs){if(i.reg>=16)return false;inputs|=1u<<i.reg;}
    for(const auto& o:p.operations){
        const unsigned mask=(o.destination>>16)&15;
        if(!mask)continue;
        unsigned lanes=mask;
        if(o.code==8||o.code==36||o.code==33)lanes=7;
        else if(o.code==9||o.code==16||o.code==17)lanes=15;
        else if(o.code==6||o.code==7||o.code==14||o.code==15||o.code==32)lanes=1;
        const int count=arithmeticOperands(o.code,p.major)-1;if(count<1||count>3)return false;
        for(int j=0;j<count;++j){
            const auto& s=o.source[j];const unsigned type=regType(s.token),index=regIndex(s.token);
            unsigned needed=0;for(unsigned c=0;c<4;++c)if(lanes&(1u<<c))needed|=1u<<((s.token>>(16+2*c))&3);
            if(type==0){if(index>=33||(temp[index]&needed)!=needed)return false;}
            else if(type==1){if(index>=16||!(inputs&(1u<<index)))return false;}
            else if(type==3){if(index||(address&needed)!=needed)return false;}
            else if(type!=2||index>=256)return false;
            if(s.token&0x2000u){const unsigned c=p.major>=2?((s.address>>16)&3):0;if(!(address&(1u<<c)))return false;}
        }
        const unsigned type=regType(o.destination),index=regIndex(o.destination);
        if(type==0){if(index>=33)return false;temp[index]|=mask;}
        else if(type==3){if(index)return false;address|=mask;}else return false;
    }
    return p.positionRegister<33&&(temp[p.positionRegister]&p.outputMask)==p.outputMask;
}
inline bool compileVertex(const Word* code,size_t words,unsigned family,Vertex& out){
    out={};try{
    if(!code||words<2||words>65536||(code[0]!=0xfffe0200u&&code[0]!=0xfffe0300u))return false;
    if(family!=NorthlightM2Family::kParticle&&family!=NorthlightM2Family::kParticleUnlit&&family!=NorthlightM2Family::kModel)return false;
    std::uint64_t hash=14695981039346656037ull;const auto* bytes=reinterpret_cast<const unsigned char*>(code);
    for(size_t i=0;i<words*4;++i)hash=(hash^bytes[i])*1099511628211ull;
    if(m2ShaderFamily(hash)!=family)return false;
    Vertex next;
    if(!NorthlightActorDeformation::compile(code,words,next.color,true,2,10)||
       !NorthlightActorDeformation::compile(code,words,next.uv,true,2,5)||
       !initialized(next.color)||!initialized(next.uv))return false;
    next.hasUv1=NorthlightActorDeformation::compile(code,words,next.uv1,true,2,5,1)&&initialized(next.uv1);
    next.valid=true;out=std::move(next);return true;
    }catch(...){out={};return false;}
}
inline bool evaluateVertex(const Vertex& p,const Four inputs[16],const float* constants,Four& color,float (&uv)[2],float* uv1=nullptr){
    color={};uv[0]=uv[1]=0;if(!p.valid||!inputs||!constants)return false;Four c,t;
    if(!NorthlightActorDeformation::evaluateFast<true>(p.color,inputs,constants,c)||
       !NorthlightActorDeformation::evaluateFast<true>(p.uv,inputs,constants,t))return false;
    // A shader-model-dependent COLOR clamp must never create an inferred tint.
    for(float v:c)if(!std::isfinite(v)||v<0||v>1)return false;
    color=c;uv[0]=t[0];uv[1]=t[1];
    if(uv1){if(!p.hasUv1||!NorthlightActorDeformation::evaluateFast<true>(p.uv1,inputs,constants,t))return false;uv1[0]=t[0];uv1[1]=t[1];}
    return true;
}
namespace detail {
enum Op:unsigned {Literal=1,Color,Texture,Fog,Uniform,Add,Mul};
struct Key {unsigned op,a,b;bool operator==(const Key& x)const{return op==x.op&&a==x.a&&b==x.b;}};
struct Hash {size_t operator()(const Key& k)const{return (size_t(k.op)*16777619u+k.a)*16777619u+k.b;}};
struct Expressions {
    std::vector<Key> nodes{{}};std::unordered_map<Key,unsigned,Hash> intern;
    unsigned node(unsigned op,unsigned a,unsigned b=0){
        if((op==Add||op==Mul)&&a>b)std::swap(a,b);
        const Key k{op,a,b};const auto found=intern.find(k);if(found!=intern.end())return found->second;
        if(nodes.size()>=8192)return 0;const unsigned id=unsigned(nodes.size());nodes.push_back(k);intern.emplace(k,id);return id;
    }
    unsigned literal(float v){Word bits;std::memcpy(&bits,&v,4);return std::isfinite(v)?node(Literal,bits):0;}
    bool value(unsigned id,float& v)const {if(!id||nodes[id].op!=Literal)return false;std::memcpy(&v,&nodes[id].a,4);return true;}
    unsigned mul(unsigned a,unsigned b){
        if(!a||!b)return 0;float x=0,y=0;const bool ax=value(a,x),by=value(b,y);
        if(ax&&by)return literal(x*y);if(ax&&x==1)return b;if(by&&y==1)return a;return node(Mul,a,b);
    }
    unsigned add(unsigned a,unsigned b){
        if(!a||!b)return 0;float x=0,y=0;const bool ax=value(a,x),by=value(b,y);
        if(ax&&by)return literal(x+y);if(ax&&x==0)return b;if(by&&y==0)return a;if(a==b)return mul(literal(2),a);return node(Add,a,b);
    }
    unsigned intrinsic(unsigned id,bool& fogged){
        fogged=false;if(!id||nodes[id].op!=Add)return id;
        const auto outer=nodes[id];
        for(unsigned side=0;side<2;++side){
            const unsigned k=side?outer.a:outer.b,product=side?outer.b:outer.a;
            if(nodes[k].op!=Uniform||nodes[product].op!=Mul)continue;const auto m=nodes[product];
            const unsigned diff=nodes[m.a].op==Fog?m.b:nodes[m.b].op==Fog?m.a:0;
            if(!diff||nodes[diff].op!=Add)continue;
            const auto d=nodes[diff];const unsigned negative=mul(literal(-1),k);
            if(d.a==negative||d.b==negative){fogged=true;return d.a==negative?d.b:d.a;}
        }return id;
    }
    bool factors(unsigned id,unsigned channel,float& scale,unsigned& color,unsigned (&texture)[2][4],unsigned& visits)const{
        if(!id||++visits>256)return false;const auto n=nodes[id];float v=0;
        if(value(id,v)){scale*=v;return std::isfinite(scale);}
        if(n.op==Color){if(n.a!=channel)return false;++color;return true;}
        if(n.op==Texture){if(n.a>=8)return false;++texture[n.a/4][n.a%4];return true;}
        if(n.op==Mul)return factors(n.a,channel,scale,color,texture,visits)&&factors(n.b,channel,scale,color,texture,visits);
        return false;
    }
};
} // namespace detail
inline bool compilePixel(const Word* code,size_t words,Pixel& out){
    out={};try{
    if(!code||words<2||words>4096||(code[0]!=0xffff0200u&&code[0]!=0xffff0300u))return false;
    using namespace NorthlightShadowShader;using namespace detail;
    const unsigned major=(code[0]>>8)&255;Expressions e;
    std::array<unsigned,4> temp[32]{},output{},definitions[224]{};
    bool defined[224]={},samplers[16]={};unsigned inputMask[16]={},legacyUvMask[2]={};int color=major==2?0:-1,uv[2]={major==2?0:-1,major==2?1:-1},fog=-1;
    unsigned samples=0,sampler[2]={},coordinates[2]={};
    const auto source=[&](Word t,unsigned lane)->unsigned{
        const unsigned type=regType(t),index=regIndex(t),modifier=(t>>24)&15,c=(t>>(16+2*lane))&3;
        if(!(t&0x80000000u)||(t&0x2000u)||modifier>1)return 0;unsigned id=0;
        if(type==0&&index<32)id=temp[index][c];
        else if(type==2&&index<224)id=defined[index]?definitions[index][c]:e.node(Uniform,index*4+c);
        else if(type==1&&int(index)==color&&(inputMask[index]&(1u<<c)))id=e.node(Color,c);
        else if(major==3&&type==1&&int(index)==fog&&c==0&&(inputMask[index]&1))id=e.node(Fog,0);
        return modifier?e.mul(e.literal(-1),id):id;
    };
    for(size_t at=1;at<words;){
        const Word token=code[at];const unsigned op=token&65535;
        if(op==65535){
            if(token!=0xffffu||at+1!=words||!samples||samples>2)return false;
            Pixel next;next.sampler=sampler[0];next.uv=coordinates[0];next.samples=samples;next.secondSampler=sampler[1];next.secondUv=coordinates[1];
            for(unsigned c=0;c<4;++c){
                bool fogged=false;const unsigned id=c<3?e.intrinsic(output[c],fogged):output[c];
                float scale=1;unsigned colors=0,textures[2][4]={},visits=0;
                if(!e.factors(id,c,scale,colors,textures,visits)||colors!=1||(scale!=1&&scale!=2))return false;
                unsigned totals[2]={};for(unsigned t=0;t<2;++t)for(unsigned k=0;k<4;++k)totals[t]+=textures[t][k];
                if(c<3){
                    if(totals[0]!=1||textures[0][c]!=1||totals[1]>1||
                       (totals[1]&&!textures[1][c]&&!textures[1][3])||(c&&next.rgbScale!=scale)||(c&&next.fog!=fogged))return false;
                    const bool mask=totals[1]!=0,alphaMask=textures[1][3]!=0;
                    if(c&&(next.secondRGB!=mask||next.secondRGBAlpha!=alphaMask))return false;
                    next.rgbScale=scale;next.fog=fogged;next.secondRGB=mask;next.secondRGBAlpha=alphaMask;
                }else{
                    if(totals[0]>1||totals[1]>1||totals[0]!=textures[0][3]||totals[1]!=textures[1][3])return false;
                    next.textureAlpha=totals[0]!=0;next.alphaScale=scale;next.secondAlpha=totals[1]!=0;
                }
            }
            if(samples==2&&!next.secondRGB&&!next.secondAlpha)return false;
            next.valid=true;out=next;return true;
        }
        if(op==65534){const size_t n=(token>>16)&32767;if(n>=words-at)return false;at+=n+1;continue;}
        const unsigned n=(token>>24)&15;if((token&0xf0000000u)||!n||n>=words-at)return false;const Word* a=code+at+1;at+=n+1;
        if(op==31){
            if(n!=2)return false;const unsigned type=regType(a[1]),index=regIndex(a[1]);
            if(type==10){if(index>=16||((a[0]>>27)&15)!=2||samplers[index])return false;samplers[index]=true;continue;}
            const unsigned mask=(a[1]>>16)&15;if(!mask)return false;
            if(major==2){
                if(type==1&&index==0&&!inputMask[0])inputMask[0]=mask;
                else if(type==3&&index<2&&!legacyUvMask[index])legacyUvMask[index]=mask;else return false;continue;
            }
            if(type!=1||index>=16)return false;
            if(inputMask[index])return false;inputMask[index]=mask;
            const unsigned usage=a[0]&31;
            const unsigned semantic=(a[0]>>16)&15;if(semantic>1||(semantic&&usage!=5))return false;
            if(usage==10){if(color>=0)return false;color=int(index);}
            else if(usage==5){if(uv[semantic]>=0)return false;uv[semantic]=int(index);}
            else if(usage==11){if(fog>=0)return false;fog=int(index);}else return false;
            continue;
        }
        if(op==81){
            if(n!=5||regType(a[0])!=2||regIndex(a[0])>=224)return false;const unsigned r=regIndex(a[0]);if(defined[r])return false;defined[r]=true;
            for(unsigned c=0;c<4;++c){float v;std::memcpy(&v,a+1+c,4);if(!(definitions[r][c]=e.literal(v)))return false;}continue;
        }
        const unsigned type=regType(a[0]),index=regIndex(a[0]),mask=(a[0]>>16)&15;
        if((a[0]&0x0ff02000u)||!mask||!((type==0&&index<32)||(type==8&&index==0)))return false;
        auto& target=type==0?temp[index]:output;
        if(op==66){
            const unsigned coord=uv[0]>=0&&int(regIndex(a[1]))==uv[0]?0:uv[1]>=0&&int(regIndex(a[1]))==uv[1]?1:2;
            if(n!=3||(token&0x00ff0000u)||type!=0||mask!=15||samples>=2||coord>1||color<0||regType(a[1])!=(major==2?3u:1u)||
               ((a[1]>>16)&255)!=0xe4||((a[1]>>24)&15)||(a[1]&0x2000u)||((major==2?legacyUvMask[coord]:inputMask[unsigned(uv[coord])])&3)!=3||
               regType(a[2])!=10||regIndex(a[2])>=16||!samplers[regIndex(a[2])]||((a[2]>>16)&255)!=0xe4||((a[2]>>24)&15)||(a[2]&0x2000u))return false;
            sampler[samples]=regIndex(a[2]);coordinates[samples]=coord;for(unsigned c=0;c<4;++c)target[c]=e.node(Texture,samples*4+c);++samples;continue;
        }
        if(!((op==1&&n==2)||((op==2||op==5)&&n==3)||((op==4||op==90)&&n==4)))return false;
        std::array<unsigned,4> result{};
        for(unsigned c=0;c<4;++c)if(mask&(1u<<c)){
            const unsigned x=source(a[1],c);unsigned made=0;
            if(op==1)made=x;
            else if(op==2)made=e.add(x,source(a[2],c));
            else if(op==5)made=e.mul(x,source(a[2],c));
            else if(op==4)made=e.add(e.mul(x,source(a[2],c)),source(a[3],c));
            else made=e.add(e.add(e.mul(source(a[1],0),source(a[2],0)),e.mul(source(a[1],1),source(a[2],1))),source(a[3],0));
            if(!made)return false;result[c]=made;
        }
        for(unsigned c=0;c<4;++c)if(mask&(1u<<c))target[c]=result[c];
    }return false;
    }catch(...){out={};return false;}
}
// Intrinsic shader RGBA before the proven native fog epilogue. Fog colour is
// neither emitted light nor sampled texture; PS constants therefore do not enter.
inline bool shade(const Pixel& p,const Four& color,const Four& texture,const float*,Four& out,const Four* second=nullptr){
    out={};if(!p.valid)return false;
    for(unsigned c=0;c<4;++c)if(!std::isfinite(color[c])||!std::isfinite(texture[c])||color[c]<0||color[c]>1||texture[c]<0||texture[c]>1)return false;
    if(p.samples==2){if(!second)return false;for(float v:*second)if(!std::isfinite(v)||v<0||v>1)return false;}
    for(unsigned c=0;c<3;++c)out[c]=color[c]*texture[c]*p.rgbScale*(p.secondRGB?(*second)[p.secondRGBAlpha?3:c]:1.f);
    out[3]=color[3]*(p.textureAlpha?texture[3]:1.f)*p.alphaScale*(p.secondAlpha?(*second)[3]:1.f);return true;
}
} // namespace NorthlightParticleLightProof
