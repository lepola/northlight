#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Pure CPU helpers. Call only while the original, recognized Terrain VS/PS and
// constants are bound. No D3D object, game address or renderer mutation here.
namespace NorthlightLegacyFog {
struct Constants {
    float parameters[4]={0,1,1,0}; // VS c12.xyz; validated enabled in w
    float color[4]={0,0,0,0};      // Proven PS3 color constant or fixed fog RGB
};
inline unsigned type(uint32_t t){return ((t>>28)&7)|((t>>8)&24);}
inline bool reg(uint32_t t,unsigned kind,unsigned n){return type(t)==kind&&(t&2047)==n;}
inline bool plainSource(uint32_t t,unsigned kind,unsigned n,unsigned swizzle=0xe4){
    return reg(t,kind,n)&&((t>>16)&255)==swizzle&&((t>>24)&15)==0&&!(t&0x2000);
}

// Fail-closed bytecode proof for the original Terrain PS3 fog epilogue:
//   add rN.xyz, surface, -cF       (or mad rN, surface, factor, -cF)
//   mad oC0.xyz, vFog.x, rN, cF
// Base archive commonly uses c6; the active patch also uses c2. Return the
// proven constant index, or -1 for unknown shaders. Fog must be an input,
// cF must not be locally defined, and no
// later RGB write/control flow may bypass the epilogue. Alpha MOVs may follow.
// This guards the actual shader, including patched archives, without assuming
// that every shader using PS3 stores its fog color in c6.
inline int ps3FogColorRegister(const uint32_t* code,size_t count){
    if(!code||count<3||code[0]!=0xffff0300||code[count-1]!=0xffff)return -1;
    unsigned fogInput=UINT32_MAX,previousOp=0,previousSize=0;
    const uint32_t* previous=nullptr;int colorRegister=-1;bool defined[224]={};
    for(size_t at=1;at<count;){
        uint32_t token=code[at];unsigned op=token&65535;
        if(op==65535)return at==count-1?colorRegister:-1;
        if(op==65534){size_t words=1+((token>>16)&32767);if(words>count-at)return -1;at+=words;continue;}
        unsigned size=(token>>24)&15;
        const bool zeroOperand=op==0||op==28||op==29||op==39||op==42||op==43||op==44;
        if((!size&&!zeroOperand)||size+1>count-at||(token&0x10000000))return -1;
        const uint32_t* a=code+at+1;
        if(op==31&&size==2&&(a[0]&31)==11&&type(a[1])==1){
            if(fogInput!=UINT32_MAX)return -1;fogInput=a[1]&2047;
        }
        if(op==81){unsigned index=a[0]&2047;if(type(a[0])!=2||index>=224)return -1;defined[index]=true;}
        if(colorRegister>=0){
            if(!(op==1&&size==2&&reg(a[0],8,0)&&((a[0]>>16)&15)==8))return -1;
        }else if(op==4&&size==4&&reg(a[0],8,0)&&((a[0]>>16)&15)==7&&
                 !(a[0]&0x0ff02000)&&
                 plainSource(a[1],1,fogInput,0)&&plainSource(a[3],2,a[3]&2047)&&
                 (a[3]&2047)<224&&!defined[a[3]&2047]&&
                 previous&&((previousOp==2&&previousSize==3)||(previousOp==4&&previousSize==4))&&type(previous[0])==0&&
                 !(previous[0]&0x0ff02000)&&
                 ((previous[0]>>16)&15)==7&&plainSource(a[2],0,previous[0]&2047)&&
                 reg(previous[previousSize-1],2,a[3]&2047)&&((previous[previousSize-1]>>16)&255)==0xe4&&
                 ((previous[previousSize-1]>>24)&15)==1&&!(previous[previousSize-1]&0x2000)){
            if((token&0x00ff0000)||(previous[-1]&0x00ff0000))return -1;
            colorRegister=int(a[3]&2047);
        }
        previous=a;previousOp=op;previousSize=size;at+=1+size;
    }
    return -1;
}
inline bool ps3FogColor6(const uint32_t* code,size_t count){return ps3FogColorRegister(code,count)==6;}

// PS3 explicitly mixes fog in shader; FOGENABLE is irrelevant on that path.
// PS1/2 use vertex fog only when enabled and FOGTABLEMODE==D3DFOG_NONE (0).
// Unknown/invalid modes leave an identity result. projectedForward is c6.w of
// Terrain's projection, +/-1. Original fog uses signed camera z, NEVER abs(z).
inline bool decode(unsigned pixelMajor,bool verifiedPS3,bool fogEnabled,unsigned fogTableMode,
                   const float* vertexC12,const float* pixelFogColor,uint32_t fixedFogARGB,
                   float projectedForward,Constants& output){
    output=Constants{};
    if(pixelMajor<1||pixelMajor>3)return false;
    if(pixelMajor==3&&!verifiedPS3)return false;
    if(pixelMajor<3&&!fogEnabled)return true;
    if(pixelMajor<3&&fogTableMode!=0)return false;
    if(!vertexC12||!std::isfinite(projectedForward)||std::fabs(std::fabs(projectedForward)-1)>.001f)return false;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(vertexC12[i]))return false;
    if(vertexC12[2]<=0||vertexC12[2]>64||vertexC12[0]*projectedForward>0)return false;
    Constants candidate;
    for(unsigned i=0;i<3;++i)candidate.parameters[i]=vertexC12[i];
    candidate.parameters[3]=1;
    if(pixelMajor==3){
        if(!pixelFogColor)return false;
        for(unsigned i=0;i<3;++i){
            if(!std::isfinite(pixelFogColor[i])||pixelFogColor[i]<0||pixelFogColor[i]>8)return false;
            candidate.color[i]=pixelFogColor[i];
        }
    }else{
        candidate.color[0]=float((fixedFogARGB>>16)&255)/255;
        candidate.color[1]=float((fixedFogARGB>>8)&255)/255;
        candidate.color[2]=float(fixedFogARGB&255)/255;
    }
    output=candidate;return true;
}
inline float visibility(const Constants& c,float signedViewZ){
    if(c.parameters[3]<.5f)return 1;
    return std::min(1.f,std::pow(std::max(signedViewZ*c.parameters[0]+c.parameters[1],0.f),c.parameters[2]));
}
using RGB=std::array<float,3>;
inline RGB relight(RGB original,RGB oldLight,RGB lightDelta,float transmittance,RGB fogColor){
    RGB result;
    for(unsigned i=0;i<3;++i){
        float transported=std::max(original[i]-(1-transmittance)*fogColor[i],0.f);
        float albedoT=std::min(transported/std::max(oldLight[i],.15f),transmittance);
        result[i]=std::max(original[i]+albedoT*lightDelta[i],original[i]-.45f*transported);
    }
    return result;
}
inline RGB composeVolume(RGB corrected,RGB original,float legacyT,RGB legacyColor,float volumeT,RGB scatter){
    RGB result;
    for(unsigned i=0;i<3;++i){
        float fogPart=std::min((1-legacyT)*legacyColor[i],original[i]);
        result[i]=volumeT*corrected[i]+legacyT*scatter[i]+(1-volumeT)*fogPart;
    }
    return result;
}
// 0.3.203 (particle fog): WorldComposite inverts the game's fog factor f = sat(z x X + Y) that a particle shader received into the particle's view distance z = (f - 1) / (X x projectionZ),
// which needs a validated linear fog (exponent 1) that starts at the camera (Y = 1). Returns 1 / (X x projectionZ) (negative), else 0: no usable fog, particles keep the plain composite.
inline float particleDistanceScale(const float* parameters,float projectionZ){
    const float x=parameters[0]*projectionZ;
    const bool usable=parameters[3]>0&&parameters[2]==1.f&&std::fabs(parameters[1]-1.f)<=1e-4f&&x<-1e-9f&&std::isfinite(x);
    return usable?1.f/x:0.f;
}
} // namespace NorthlightLegacyFog
