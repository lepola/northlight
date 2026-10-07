#pragma once
// Exact native center-texture identity (observation, and since 0.3.165 the
// suppression of a claimed native body draw by the renderer).
// Runtime follows signature-gated client handle fields and compares the opaque
// numeric identity with a valid GetTexture result; it never invokes that address.
// Portable mip helpers remain only for offline original-artifact verification.
#include "actor_texture.h"
#include <fstream>
#include <string>
#include <array>
namespace NorthlightCelestialDisc {
struct MatchMip {unsigned width=0,height=0,format=0;std::vector<std::uint8_t> bytes,rgba;};
inline bool decodeMip(MatchMip& mip){
    using F=NorthlightActorTexture::Format;F fmt;unsigned pitch=0;
    switch(mip.format){case 21:fmt=F::BGRA8;pitch=mip.width*4;break;case 22:fmt=F::BGRX8;pitch=mip.width*4;break;
    case 0x31545844:fmt=F::BC1;pitch=((mip.width+3)/4)*8;break;
    case 0x33545844:fmt=F::BC2;pitch=((mip.width+3)/4)*16;break;
    case 0x35545844:fmt=F::BC3;pitch=((mip.width+3)/4)*16;break;default:return false;}
    return NorthlightActorTexture::decode(mip.bytes.data(),mip.bytes.size(),mip.width,mip.height,pitch,fmt,mip.rgba);
}
inline bool loadMatch(const std::string& path,std::vector<MatchMip>& out){
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f||f.tellg()>200000)return false;f.seekg(0);
    std::uint32_t header[3]={};f.read(reinterpret_cast<char*>(header),12);
    if(!f||header[0]!=0x314d4346||header[1]!=1||!header[2]||header[2]>16)return false;
    std::vector<MatchMip> candidate;
    for(unsigned i=0;i<header[2];++i){
        std::uint32_t h[4]={};f.read(reinterpret_cast<char*>(h),16);
        if(!f||!h[0]||!h[1]||h[0]>128||h[1]>128||!h[3]||h[3]>128*128*4)return false;
        MatchMip m;m.width=h[0];m.height=h[1];m.format=h[2];m.bytes.resize(h[3]);f.read(reinterpret_cast<char*>(m.bytes.data()),h[3]);
        if(!f||!decodeMip(m))return false;candidate.push_back(std::move(m));
    }
    if(f.peek()!=std::char_traits<char>::eof())return false;out=std::move(candidate);return true;
}
inline bool matches(MatchMip& candidate,const std::vector<MatchMip>& reference){
    for(const auto& r:reference)if(candidate.width==r.width&&candidate.height==r.height){
        if(candidate.format==r.format&&candidate.bytes==r.bytes)return true;
        if(candidate.rgba.empty()&&!decodeMip(candidate))return false;
        if(candidate.rgba==r.rgba)return true;
    }
    return false;
}
} // namespace NorthlightCelestialDisc
namespace NorthlightCelestialDisc {
struct IdentitySignature {std::uintptr_t address;const unsigned char* bytes;std::size_t size;};
inline const IdentitySignature* identitySignatures(std::size_t& count){
    static const unsigned char bodyHandle[]={0x8b,0x47,0x10,0x53,0x53,0x50,0xe8,0x15,0xa5,0xb0,0xff};
    static const unsigned char variantFlag[]={0xf6,0x46,0x28,0x04};
    static const unsigned char variantPointer[]={0x8b,0x4e,0x5c,0x85,0xc9};
    static const unsigned char variantGx[]={0x8b,0x46,0x5c,0x8b,0x40,0x18};
    static const unsigned char ordinaryGx[]={0x8b,0x46,0x44,0x5f,0x5b,0x5e,0x5d,0xc3};
    static const unsigned char d3dBind[]={0x8b,0x53,0x38,0x8b,0x86,0x7c,0x39,0x00,0x00,0x8b,0x08,0x52,0x57,0x50,0x8b,0x81,0x04,0x01,0x00,0x00,0xff,0xd0};
    static const IdentitySignature table[]={
        {0x9ac790,bodyHandle,sizeof bodyHandle},{0x4b6cd4,variantFlag,sizeof variantFlag},
        {0x4b6d1f,variantPointer,sizeof variantPointer},{0x4b6d31,variantGx,sizeof variantGx},
        {0x4b6d7a,ordinaryGx,sizeof ordinaryGx},{0x6a492e,d3dBind,sizeof d3dBind},{0x6a88d6,d3dBind,sizeof d3dBind}};
    count=sizeof table/sizeof table[0];return table;
}
template<class Read>inline bool verifyIdentityCode(Read read){
    std::size_t count=0;auto* sig=identitySignatures(count);unsigned char actual[32];
    for(std::size_t i=0;i<count;++i)if(!read(sig[i].address,actual,sig[i].size)||std::memcmp(actual,sig[i].bytes,sig[i].size))return false;
    return true;
}
inline bool clientPointer(std::uint32_t p,unsigned tail){return p>=0x10000&&!(p&3)&&p<=0xffffffffu-tail;}
template<class Read>inline bool readTextureIdentityAt(Read read,std::uint32_t handleAddress,std::uint32_t& output){
    std::uint32_t handle=0,gx=0,variant=0,result=0;
    if(!read(handleAddress,&handle,4))return false;
    if(!handle){output=0;return true;}
    if(!clientPointer(handle,0x60))return false;
    unsigned char fields[0x40];if(!read(std::uintptr_t(handle)+0x20,fields,sizeof fields))return false;
    std::memcpy(&gx,fields+0x24,4);std::memcpy(&variant,fields+0x3c,4);
    if((fields[8]&4)&&variant){
        if(!clientPointer(variant,0x1c)||!read(std::uintptr_t(variant)+0x18,&gx,4))return false;
    }
    if(!gx){output=0;return true;}
    if(!clientPointer(gx,0x3c)||!read(std::uintptr_t(gx)+0x38,&result,4))return false;
    if(result&&!clientPointer(result,4))return false;
    // Opaque numeric identity only: never dereference or call this COM address.
    output=result;return true;
}
template<class Read>inline bool readBodyIdentity(Read read,std::uint32_t bodyRecord,std::uint32_t& output){
    return readTextureIdentityAt(read,bodyRecord+0x10,output);
}
// Sky body records (celestial_context.h): sun, primary moon, secondary moon (moon02.blp).
constexpr std::uint32_t BodyRecords[3]={0xd38e28,0xd38e48,0xd38e68};
struct Identities {std::uint32_t texture[3]={};unsigned valid=0;};
// The game stores the EXPOSED (proxy) texture pointer; the extension device's
// GetTexture returns the RAW one (since the 0.3.126 device mirror). Map the
// identities read from game memory to raw once per read before comparing.
template<class Map>inline Identities mapIdentities(Identities ids,Map map){
    // A map miss (0) leaves that body unidentified: its native draw stays (fail open).
    for(unsigned i=0;i<3;++i)if(ids.texture[i])ids.texture[i]=std::uint32_t(map(std::uintptr_t(ids.texture[i])));
    return ids;
}
template<class Read>inline Identities readIdentities(Read read){
    Identities result;
    for(unsigned body=0;body<3;++body){std::uint32_t a=0,b=0,address=BodyRecords[body];
        if(readBodyIdentity(read,address,a)&&readBodyIdentity(read,address,b)&&a==b){result.valid|=1u<<body;result.texture[body]=a;}}
    return result;
}
class IdentityFrame {
    Identities initial_;unsigned observed_=0,uncertain_=0;bool begun_=false,finished_=false;
public:
    void reset(){initial_={};observed_=uncertain_=0;begun_=finished_=false;}
    bool begun()const{return begun_;}
    void begin(const Identities& ids){initial_=ids;observed_=0;uncertain_=(~ids.valid)&3;begun_=true;finished_=false;}
    void observe(std::uintptr_t bound){if(!begun_||!bound)return;for(unsigned i=0;i<2;++i)if((initial_.valid&(1u<<i))&&bound==initial_.texture[i])observed_|=1u<<i;}
    // The body (0 sun, 1 primary moon, 2 secondary moon) whose texture is bound. Does
    // not record an observation: a claimed native draw is skipped; the renderer draws
    // the sun and primary moon itself with world-depth occlusion, and never the
    // secondary moon (0.3.162: no transparent moon02.blp needed in the client's MPQs).
    // Observation, uncertainty and fallback concern bodies 0 and 1 only.
    bool claims(std::uintptr_t bound)const{return claimIndex(bound)>=0;}
    int claimIndex(std::uintptr_t bound)const{if(!begun_||!bound)return -1;for(unsigned i=0;i<3;++i)if((initial_.valid&(1u<<i))&&bound==initial_.texture[i])return int(i);return -1;}
    void finish(const Identities& ids){
        if(!begun_){begin(ids);uncertain_=3;} // No observations: caller should begin before examining world draws.
        for(unsigned i=0;i<2;++i)if(!(ids.valid&(1u<<i))||ids.texture[i]!=initial_.texture[i])uncertain_|=1u<<i;
        finished_=true;
    }
    bool canFallback(unsigned body)const{return finished_&&body<2&&!((observed_|uncertain_)&(1u<<body));}
    // a body whose native draw was SUPPRESSED this frame must get our
    // late disc even if its identity changed later in the frame (streaming):
    // uncertainty alone must not leave a frame with no sun or moon.
    bool lateDisc(unsigned body,unsigned suppressedBodies)const{
        return canFallback(body)||(finished_&&body<2&&(suppressedBodies&(1u<<body))&&!(observed_&(1u<<body)));
    }
    unsigned mask()const{return observed_;}unsigned uncertainMask()const{return uncertain_;}
};
} // namespace NorthlightCelestialDisc
#ifdef _WIN32
#include <d3d9.h>
#include <functional>
#include "world_context.h"
#include "celestial_context.h"
#include "stream_hooks.h"
namespace NorthlightCelestialDisc {
class NativeObserver {
    IdentityFrame frame_;bool ready_=false,secondary_=false;unsigned reads_=0;
    std::function<std::uintptr_t(std::uintptr_t)> map_;
    // 0.3.192 (CS): the game stores a stream proxy; innerOf (null when the stream is inactive: identity) gives the
    // Device-level pointer the registry's rawOf knows, so the proxy is unwrapped before rawOf.
    static std::uintptr_t inner(std::uintptr_t exposed){return reinterpret_cast<std::uintptr_t>(NorthlightStream::inner(reinterpret_cast<const void*>(exposed)));}
    Identities read(){auto ids=readIdentities(NorthlightWorldContext::readSelf);return map_?mapIdentities(ids,[&](std::uintptr_t exposed){return map_(inner(exposed));}):ids;}
public:
    void setIdentityMap(std::function<std::uintptr_t(std::uintptr_t)> map){map_=std::move(map);}
    std::uintptr_t mapIdentity(std::uintptr_t exposed)const{return map_&&exposed?map_(inner(exposed)):exposed;}
    bool load(const std::string&){
        static const bool supported=NorthlightWorldContext::supportedClient()&&verifyIdentityCode(NorthlightWorldContext::readSelf);
        // moon02's record address comes from the sky code signatures (moon2Init, moon2Position).
        static const bool secondary=supported&&NorthlightCelestial::verifyCode(NorthlightWorldContext::readSelf);
        ready_=supported;secondary_=secondary;return ready_;
    }
    void reset(){frame_.reset();reads_=0;}
    void beginFrame(){frame_.reset();reads_=0;}
    // The frame gets one initial double-read and one final double-read. A
    // streaming change suppresses only that body's fallback for this frame,
    // preventing stale negative IDs without scanning particle texture content.
    void beginObservation(){if(ready_&&!frame_.begun()){frame_.begin(read());++reads_;}}
    void finishObservations(){if(!ready_)return;beginObservation();frame_.finish(read());++reads_;}
    void observe(IDirect3DBaseTexture9* bound){if(!ready_)return;beginObservation();frame_.observe(reinterpret_cast<std::uintptr_t>(bound));}
    bool claims(IDirect3DBaseTexture9* bound){return claimIndex(bound)>=0;}
    int claimIndex(IDirect3DBaseTexture9* bound){if(!ready_)return -1;beginObservation();const int body=frame_.claimIndex(reinterpret_cast<std::uintptr_t>(bound));return body==2&&!secondary_?-1:body;}
    bool canFallback(unsigned body)const{return ready_&&frame_.canFallback(body);}
    bool lateDisc(unsigned body,unsigned suppressedBodies)const{return ready_&&frame_.lateDisc(body,suppressedBodies);}
    unsigned mask()const{return frame_.mask();}unsigned attempts()const{return reads_;}
    unsigned ambiguities()const{return frame_.uncertainMask();}unsigned skipped()const{return 0;}
};
} // namespace NorthlightCelestialDisc
#endif
