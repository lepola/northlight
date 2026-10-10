#pragma once
#include "world_context.h"
#include "wmo_shader_signatures.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace NorthlightWmoContext {
struct Lighting {
    float lightDirection[3]={},ambient[3]={},direct[3]={};
    float dayFraction=0;
    // the game's 18 current light colours (sky +0xd4, see sun_hue.h for the slot order).
    // slotsConsistent: this read's slot 1 equals its raw copy at +0x17c (0x7ee892), a free
    // per-frame check of the slot layout. Not +0x1a8/+0x1ac: weather and lightning rescale
    // those after the copy (0x7f36c6/0x7f36da), which made overcast Tirisfal fail (0.3.163). slotsProven: that, and the sampler/copy code
    // matched lightSlotProofs() (Windows reads only).
    std::uint32_t slots[18]={};bool slotsConsistent=false,slotsProven=false;
};
inline const WmoShaderSignature* signature(std::uint64_t hash) {
    size_t low=0,high=sizeof(kWmoShaderSignatures)/sizeof(kWmoShaderSignatures[0]);
    while(low<high){const size_t middle=(low+high)/2;if(kWmoShaderSignatures[middle].hash<hash)low=middle+1;else high=middle;}
    if(low==sizeof(kWmoShaderSignatures)/sizeof(kWmoShaderSignatures[0])||kWmoShaderSignatures[low].hash!=hash)return nullptr;
    return &kWmoShaderSignatures[low];
}
inline constexpr size_t SkyBytes=0x1b8,LightSlotBytes=0xd4;
// 0.3.200: how far the sky block's own camera copy may be from the camera before the block counts as stale. The engine
// updates that copy at its own point of the frame and not every frame, so in motion (mounted, measured 4..6.5 units) it
// trails the camera by several frames of travel (0.25 used to reject it: the light then switched to the native fallback
// frame to frame, a visible flicker). A teleport or map change moves it by far more than this.
inline constexpr float SkyCameraTolerance=40.f;
inline float scalar(const unsigned char* bytes,size_t at){float f;std::memcpy(&f,bytes+at,4);return f;}
inline std::uint32_t packed(const unsigned char* bytes,size_t at){std::uint32_t n;std::memcpy(&n,bytes+at,4);return n;}
inline void rgb(std::uint32_t color,float* output){output[0]=float((color>>16)&255)/255.f;output[1]=float((color>>8)&255)/255.f;output[2]=float(color&255)/255.f;}

// Fresh engine sky/environment source, independent of WMO VS constant usage.
// Prelit WMO variants do NOT read c10..12 and cannot supply those values.
// The light source is the outdoor world environment; WMO vertex baking and
// indoor group overrides remain distinct material/local lighting contributions.
inline bool decodeGlobalLighting(const unsigned char* bytes,size_t size,const float* expectedCamera,Lighting& out) {
    if(!bytes||size<SkyBytes||!expectedCamera)return false;
    Lighting value;value.dayFraction=scalar(bytes,4);
    if(!std::isfinite(value.dayFraction)||value.dayFraction<0||value.dayFraction>=1)return false;
    float cameraError=0,lengthSquared=0;
    for(unsigned i=0;i<3;++i){
        float camera=scalar(bytes,0x18+i*4),direction=scalar(bytes,0x19c+i*4);
        if(!std::isfinite(camera)||!std::isfinite(expectedCamera[i])||!std::isfinite(direction)||
           std::fabs(camera)>100000||std::fabs(expectedCamera[i])>100000)return false;
        float delta=camera-expectedCamera[i];cameraError+=delta*delta;
        value.lightDirection[i]=-direction;lengthSquared+=direction*direction;
    }
    if(cameraError>SkyCameraTolerance*SkyCameraTolerance||lengthSquared<.25f||lengthSquared>2.25f)return false;
    const float inverseLength=1/std::sqrt(lengthSquared);
    for(auto& component:value.lightDirection)component*=inverseLength;
    rgb(packed(bytes,0x1a8),value.direct);rgb(packed(bytes,0x1ac),value.ambient);
    std::memcpy(value.slots,bytes+LightSlotBytes,sizeof value.slots);
    value.slotsConsistent=value.slots[1]==packed(bytes,0x17c);
    out=value;return true;
}
inline bool decodeCoherentGlobalLighting(const unsigned char* first,const unsigned char* second,size_t size,
                                         const float* expectedCamera,Lighting& out) {
    return first&&second&&size>=SkyBytes&&!std::memcmp(first,second,SkyBytes)&&decodeGlobalLighting(first,size,expectedCamera,out);
}
// 0.3.207: the game copies the band colours into the sky block and only then rescales its direct
// (+0x1a8, 0x7f36c6) and ambient (+0x1ac, 0x7f36da) for the weather. Since the command stream the
// block is read on the replay thread while the game thread may be between the copy and the rescale:
// a coherent read then holds the unscaled band colours, equal to their own slots (direct = slot 1,
// ambient = slot 0). In Duskwood (scale ~.8-.93) the sun light and its fog glow flashed brighter for
// that single frame. A read whose colour equals its slot while the last accepted one was rescaled
// keeps the last accepted colour; HoldMs of only unscaled reads (the weather ended) accept them again.
// The direction and the slots of the read are used as read.
struct LightingHold {
    static constexpr std::uint32_t HoldMs=500;
    struct Colour {float value[3]={};bool scaled=false;std::uint32_t at=0;bool valid=false;};
    Colour direct,ambient;std::string map;unsigned held=0;
    static bool equalsSlot(const float* colour,std::uint32_t slot){
        float raw[3];rgb(slot,raw);return colour[0]==raw[0]&&colour[1]==raw[1]&&colour[2]==raw[2];
    }
    static bool keep(Colour& c,float* colour,bool unscaled,bool force,std::uint32_t now){
        const bool hold=c.valid&&c.scaled&&(unscaled||force)&&now-c.at<=HoldMs;
        if(hold){std::memcpy(colour,c.value,sizeof c.value);return true;}
        std::memcpy(c.value,colour,sizeof c.value);c.scaled=!unscaled;c.at=now;c.valid=true;return false;
    }
    // Returns true when a colour of this read was replaced by the last accepted one.
    bool filter(Lighting& light,const char* nextMap,std::uint32_t now){
        if(!nextMap||map!=nextMap){direct=Colour{};ambient=Colour{};map=nextMap?nextMap:"";}
        if(!light.slotsConsistent)return false; /* the slot layout is unconfirmed: nothing to compare against */
        const bool heldDirect=keep(direct,light.direct,equalsSlot(light.direct,light.slots[1]),false,now);
        // The ambient is rescaled after the direct: an unscaled direct means an unscaled ambient.
        const bool heldAmbient=keep(ambient,light.ambient,equalsSlot(light.ambient,light.slots[0]),heldDirect,now);
        held+=heldDirect||heldAmbient;
        return heldDirect||heldAmbient;
    }
};
// Reuse rigid-view validation and coordinate conversion from TerrainContext.
// view MUST come from the independently verified current camera, never WMO's
// c31..33 model-view transform.
inline bool context(const float* view,const Lighting& lighting,NorthlightWorldContext::TerrainContext& out) {
    if(!view)return false;float constants[12]={};
    for(unsigned i=0;i<3;++i){
        for(unsigned j=0;j<3;++j)constants[i]+=lighting.lightDirection[j]*view[j*4+i];
        constants[4+i]=lighting.ambient[i];constants[8+i]=lighting.direct[i];
    }
    return NorthlightWorldContext::decodeTerrain(view,constants,out);
}
// Terrain variants need not populate the same native light constants on every
// draw. Prefer the independently validated environment, but still require this
// draw's rigid world view to agree with the current game camera.
inline bool terrainContext(const float* view,const float* nativeLight,const float* camera,
                           const Lighting* global,NorthlightWorldContext::TerrainContext& out) {
    NorthlightWorldContext::TerrainContext candidate;
    if(global&&context(view,*global,candidate)&&NorthlightWorldContext::cameraAgrees(candidate,camera)){
        out=candidate;return true;
    }
    if(nativeLight&&NorthlightWorldContext::decodeTerrain(view,nativeLight,candidate)&&
       NorthlightWorldContext::cameraAgrees(candidate,camera)){out=candidate;return true;}
    return false;
}
// Optional exact-hash SM3 lit-WMO proof. c10 ambient, c11 direct, -c12 view
// source direction. Baked vertex-color and local-point-light terms are not
// represented by this global directional/ambient pair.
inline bool decodeLitShader(std::uint64_t hash,const float* view,const float* c10to12,
                            NorthlightWorldContext::TerrainContext& out) {
    const auto* known=signature(hash);if(!known||!known->lighting||!view||!c10to12)return false;
    float constants[12]={};
    for(unsigned i=0;i<3;++i){constants[i]=-c10to12[8+i];constants[4+i]=c10to12[i];constants[8+i]=c10to12[4+i];}
    return NorthlightWorldContext::decodeTerrain(view,constants,out);
}

struct CodeProof {uintptr_t address;const unsigned char* bytes;size_t size;};
inline const CodeProof* proofs(size_t& count) {
    static const unsigned char sky[]={0xb8,0x00,0x8b,0xd3,0x00,0xc3};
    static const unsigned char directionCopy[]={0x8b,0x3d,0xa8,0x04,0xce,0x00,0x83,0xc7,0x58,0x8d,0x86,0x9c,0x01,0x00,0x00,0x50,0x8b,0xcf,0xe8,0x13,0x32,0x0b,0x00};
    static const unsigned char directionSetter[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x10,0x89,0x51,0x24,0x8b,0x50,0x04,0x89,0x51,0x28,0x8b,0x40,0x08,0x89,0x41,0x2c};
    static const unsigned char directSource[]={0x0f,0xb6,0x88,0xaa,0x01,0x00,0x00,0x0f,0xb6,0x90,0xa9,0x01,0x00,0x00,0x89,0x4d,0x0c,0x0f,0xb6,0x88,0xa8,0x01,0x00,0x00};
    static const unsigned char ambientSource[]={0x0f,0xb6,0x88,0xae,0x01,0x00,0x00,0x0f,0xb6,0x90,0xad,0x01,0x00,0x00,0x0f,0xb6,0x80,0xac,0x01,0x00,0x00};
    static const unsigned char ambientUpload[]={0x53,0x8d,0x55,0xf4,0x52,0x6a,0x0a,0x6a,0x00,0xff,0xd0};
    static const unsigned char directUpload[]={0x53,0x8d,0x45,0xe8,0x50,0x6a,0x0b,0x6a,0x00,0xff,0xd2};
    static const unsigned char directionUpload[]={0x6a,0x0c,0x6a,0x00,0xff,0xd0};
    static const CodeProof values[]={{0x7ecef0,sky,sizeof sky},{0x7818b6,directionCopy,sizeof directionCopy},
        {0x834ae0,directionSetter,sizeof directionSetter},{0x7a8c44,directSource,sizeof directSource},
        {0x7a8c94,ambientSource,sizeof ambientSource},{0x7a8db8,ambientUpload,sizeof ambientUpload},
        {0x7a8dd1,directUpload,sizeof directUpload},{0x7a8def,directionUpload,sizeof directionUpload}};
    count=sizeof(values)/sizeof(values[0]);return values;
}
// slot order and destination of the light colours. The sampler stores band 9 and
// band 10 in slots 9/10 (+0x24/+0x28); the frame copy writes the 18 colours to 0xd38bd4;
// band 9 feeds the native sun/moon billboards and glare records; slot 1 is copied unscaled to
// +0x17c (the per-frame layout check).
inline const CodeProof* lightSlotProofs(size_t& count) {
    static const unsigned char bandStores[]={0x6a,0x0a,0x57,0x8d,0x55,0x10,0x53,0x52,0x89,0x4e,0x24,0xe8,0x68,0xfe,0xff,0xff,
        0x8b,0x00,0x6a,0x0b,0x57,0x8d,0x4d,0x10,0x53,0x51,0x89,0x46,0x28};
    static const unsigned char frameCopy[]={0x8d,0x85,0x50,0xff,0xff,0xff,0x50,0xb9,0xd4,0x8b,0xd3,0x00,0xe8,0x92,0xa3,0xff,0xff};
    static const unsigned char sunToGlare[]={0xa1,0xf8,0x8b,0xd3,0x00,0x89,0x4d,0xfc,0xa3,0x34,0x8e,0xd3,0x00,0xdb,0x45,0xfc,0xa3,0xc0,0x8e,0xd3,0x00};
    static const unsigned char rawDirectCopy[]={0x8b,0x15,0xd8,0x8b,0xd3,0x00,0xa3,0x94,0x8c,0xd3,0x00,0x89,0x15,0x7c,0x8c,0xd3,0x00};
    static const CodeProof values[]={{0x7ec0b8,bandStores,sizeof bandStores},{0x7f356d,frameCopy,sizeof frameCopy},{0x7f36f6,sunToGlare,sizeof sunToGlare},
        {0x7ee887,rawDirectCopy,sizeof rawDirectCopy}};
    count=sizeof(values)/sizeof(values[0]);return values;
}
#ifdef _WIN32
inline bool proven(const CodeProof* proof,size_t count) {
    for(size_t i=0;i<count;++i){unsigned char actual[64];if(proof[i].size>sizeof actual||
        !NorthlightWorldContext::readSelf(proof[i].address,actual,proof[i].size)||std::memcmp(actual,proof[i].bytes,proof[i].size))return false;}
    return true;
}
inline bool supportedClient() {
    if(!NorthlightWorldContext::supportedClient())return false;
    size_t count;const auto* proof=proofs(count);
    for(size_t i=0;i<count;++i){unsigned char actual[64];if(proof[i].size>sizeof actual||
        !NorthlightWorldContext::readSelf(proof[i].address,actual,proof[i].size)||std::memcmp(actual,proof[i].bytes,proof[i].size))return false;}
    return true;
}
inline bool readGlobalLighting(const float* expectedCamera,Lighting& out) {
    static const bool supported=supportedClient();if(!supported)return false;
    unsigned char first[SkyBytes],second[SkyBytes];
    static const bool slots=[]{size_t count;const auto* proof=lightSlotProofs(count);return proven(proof,count);}();
    if(!(NorthlightWorldContext::readSelf(0xd38b00,first,sizeof first)&&
         NorthlightWorldContext::readSelf(0xd38b00,second,sizeof second)&&
         decodeCoherentGlobalLighting(first,second,SkyBytes,expectedCamera,out)))return false;
    out.slotsProven=slots&&out.slotsConsistent;return true;
}
#endif
} // namespace NorthlightWmoContext
