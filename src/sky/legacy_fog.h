#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

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

// Fail-closed bytecode proof for the original PS3 fog epilogue (Terrain and WMO/MapObj):
//   mad oC0.xyz, vFog.x, P, cF   with   P.c = X - cF.c   (so oC0.rgb = lerp(cF, X, vFog) whatever X is)
// The negated colour of the instruction that builds P is either the constant itself (add/mad ..., -cF) or a temp component that
// provably holds a plain copy of cF.c (mov rK.xyz, cF ; mad rN.xyz, X, Y, -rK), including the .yzw-packed form (mad r0.yzw, ..., -r1.xxyz
// ... mad oC0.xyz, vFog.x, r0.yzww, cF). Unrelated instructions (alpha mov, texkill, ...) may sit between. Terrain uses c6 (base) or c2
// (patch), MapObj c2 (patch) or c16 (common-2). Return the proven constant index, or -1 for unknown shaders. Fog must be an input, cF a plain
// cN with no def, and the epilogue the only RGB write to oC0; after it only oC0.w writes. Tracked writes must have no sat/pp/shift/relative
// bits; sources are read before the destination state is replaced; if/loop/rep blocks clear the tracking and never contain the epilogue;
// call/ret/label/break are rejected. Guards the actual shader, including patched archives, without assuming a fixed constant.
inline int ps3FogColorRegister(const uint32_t* code,size_t count){
    if(!code||count<3||code[0]!=0xffff0300||code[count-1]!=0xffff)return -1;
    struct Src{int16_t reg=-1;int8_t comp=0;}; // reg<0: unknown
    Src copy[32][4],neg[32][4]; // copy: temp component holds cF.k ; neg: temp component = X - cF.k
    unsigned fogInput=UINT32_MAX,depth=0;bool defined[224]={};int colorRegister=-1;
    const auto comp=[](uint32_t t,unsigned c){return int((t>>(16+2*c))&3);};
    const auto tracked=[](uint32_t d){return !(d&0x0ff02000);}; // no sat/pp/shift/relative on the destination
    for(size_t at=1;at<count;){
        uint32_t token=code[at];unsigned op=token&65535;
        if(op==65535)return at==count-1&&depth==0?colorRegister:-1;
        if(op==65534){size_t words=1+((token>>16)&32767);if(words>count-at)return -1;at+=words;continue;}
        if(op==0){++at;continue;} // nop
        unsigned size=(token>>24)&15;
        if(op==25||op==26||op==28||op==30||op==44||op==45||op==96)return -1; // call/callnz/ret/label/break/breakc/breakp
        if(op==27||op==29||op==38||op==39||op==40||op==41||op==42||op==43){ // loop/endloop/rep/endrep/if/ifc/else/endif
            if(colorRegister>=0||size+1>count-at)return -1;
            if(op==27||op==38||op==40||op==41)++depth;else if(op!=42){if(!depth)return -1;--depth;}
            for(auto& r:copy)for(auto& e:r)e=Src{};for(auto& r:neg)for(auto& e:r)e=Src{};
            at+=1+size;continue;
        }
        if(!size||size+1>count-at||(token&0x10000000))return -1;
        const uint32_t* a=code+at+1;at+=1+size;
        if(op==31){if(size==2&&(a[0]&31)==11&&type(a[1])==1){if(fogInput!=UINT32_MAX)return -1;fogInput=a[1]&2047;}continue;}
        if(op==81){if(colorRegister>=0)return -1;unsigned i=a[0]&2047;if(type(a[0])!=2||i>=224)return -1;defined[i]=true;continue;}
        const uint32_t d=a[0];const unsigned dk=type(d),dn=d&2047,m=(d>>16)&15;
        if(colorRegister>=0&&!(dk==8&&dn==0&&m==8))return -1; // after the epilogue: only oC0.w
        if(dk==8){
            if(dn!=0)return -1;
            if(op==4&&size==4&&m==7&&fogInput!=UINT32_MAX){
                const uint32_t f=a[1],r=a[2],k=a[3];
                if((d&0x0ff02000)||!plainSource(f,1,fogInput,0))return -1;
                if(!plainSource(k,2,k&2047)||(k&2047)>=224||defined[k&2047])return -1;
                if(type(r)!=0||((r>>24)&15)||(r&0x2000)||(r&2047)>=32)return -1;
                for(unsigned c=0;c<3;++c){const Src& e=neg[r&2047][comp(r,c)];if(e.reg!=int16_t(k&2047)||e.comp!=int8_t(c))return -1;}
                colorRegister=int(k&2047);
            }else if(m&7)return -1; // any other RGB output write
            continue;
        }
        if(dk!=0||dn>=32)continue; // only temps carry state
        Src newCopy[4],newNeg[4]; // computed from the old state: sources are read before the write
        if(tracked(d)&&!depth){
            if(op==1&&size==2&&type(a[1])==2&&!((a[1]>>24)&15)&&!(a[1]&0x2000)&&(a[1]&2047)<224&&!defined[a[1]&2047]){
                for(unsigned c=0;c<4;++c)if(m>>c&1)newCopy[c]={int16_t(a[1]&2047),int8_t(comp(a[1],c))};
            }else if((op==4&&size==4)||(op==2&&size==3)){
                const uint32_t t=a[size-1];
                if(((t>>24)&15)==1&&!(t&0x2000)){
                    for(unsigned c=0;c<4;++c){if(!(m>>c&1))continue;
                        if(type(t)==2&&(t&2047)<224&&!defined[t&2047])newNeg[c]={int16_t(t&2047),int8_t(comp(t,c))};
                        else if(type(t)==0&&(t&2047)<32)newNeg[c]=copy[t&2047][comp(t,c)];}
                }
            }
        }
        for(unsigned c=0;c<4;++c)if(m>>c&1){copy[dn][c]=newCopy[c];neg[dn][c]=newNeg[c];}
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
// Per-map hold of the measured fog, so a frame whose fog could not be read (draw order: WMO context without a proven PS3 epilogue and no terrain
// draw yet) does not step the relighting/haze/clouds. Readable frames pass through unchanged (the framebuffer holds the game's fog of this very
// frame, so the removal must match it at once); only unreadable frames hold the last value and then fade w out. Pure CPU; ticks are uint32 ms
// (GetTickCount), differences wrap-safe.
constexpr std::uint32_t HoldMs=2000;   // an unreadable frame returns the last readable value for this long
constexpr float FadeSeconds=.5f;       // time constant of the post-hold fade of w toward 0
constexpr std::uint32_t MaxStepMs=250; // dt clamp for the fade: one long frame must not collapse it
inline bool finite(const Constants& c){
    for(unsigned i=0;i<4;++i)if(!std::isfinite(c.parameters[i])||!std::isfinite(c.color[i]))return false;
    return true;
}
struct Hold {
    Constants held;std::string map;bool has=false;std::uint32_t lastKnown=0,lastUpdate=0;
    void reset(){*this=Hold{};}
    // known = decode() succeeded this frame (a known fog-off result, w=0, passes through and is then held as fog-off).
    Constants update(const std::string& mapName,const Constants& measured,bool known,std::uint32_t nowMs){
        known=known&&finite(measured);
        if(has&&mapName!=map)reset(); // map change: nothing carries over
        if(known){held=measured;map=mapName;has=true;lastKnown=lastUpdate=nowMs;return held;}
        if(!has)return Constants{};
        const std::uint32_t step=std::min<std::uint32_t>(std::uint32_t(nowMs-lastUpdate),MaxStepMs);
        lastUpdate=nowMs;
        if(std::uint32_t(nowMs-lastKnown)<=HoldMs)return held;
        held.parameters[3]+=(0-held.parameters[3])*(1-std::exp(-float(step)*.001f/FadeSeconds)); // x,y,z and colour stay
        if(held.parameters[3]<1.f/256){reset();return Constants{};}
        return held;
    }
};
} // namespace NorthlightLegacyFog
