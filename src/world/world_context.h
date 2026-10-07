#pragma once
// Camera/light readback only. All game-dependent reads are signature gated.
#include <cmath>
#include <cstdint>
#include <atomic>
#include <cstring>
#include "snapshot_store.h"
#ifdef _WIN32
#include <windows.h>
#endif

namespace NorthlightWorldContext {
struct TerrainContext {
    float view[16] = {}, inverseView[16] = {};
    float camera[3] = {}, lightDirection[3] = {};
    float ambient[3] = {}, direct[3] = {};
};

// c0..3 are ROWS for p_view = float4(p_world,1) * view.
// c24 is the surface-to-light direction in view coordinates. c25/c26 are
// ambient/direct shader RGB inputs, NOT a calibrated physical radiance unit.
inline bool decodeTerrain(const float* view, const float* light, TerrainContext& out) {
    for (int i=0;i<16;++i) if (!std::isfinite(view[i])) return false;
    for (int i=0;i<12;++i) if (!std::isfinite(light[i])) return false;
    if (std::fabs(view[3])>.001f || std::fabs(view[7])>.001f ||
        std::fabs(view[11])>.001f || std::fabs(view[15]-1)>.001f) return false;
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
        float dot=0;
        for(int k=0;k<3;++k)dot+=view[4*i+k]*view[4*j+k];
        if(std::fabs(dot-(i==j?1.f:0.f))>.004f)return false;
    }
    float lightLength=std::sqrt(light[0]*light[0]+light[1]*light[1]+light[2]*light[2]);
    if(lightLength<.5f || lightLength>1.5f)return false;
    TerrainContext value;
    std::memcpy(value.view,view,64);
    value.inverseView[15]=1;
    for(int i=0;i<3;++i) {
        for(int k=0;k<3;++k) {
            value.inverseView[4*i+k]=view[4*k+i];
            value.camera[i]-=view[12+k]*view[4*i+k];
            value.lightDirection[i]+=light[k]*view[4*i+k]/lightLength;
        }
        if(std::fabs(value.camera[i])>100000.f)return false;
        value.inverseView[12+i]=value.camera[i];
        if(light[4+i]<0||light[4+i]>8||light[8+i]<0||light[8+i]>8)return false;
        value.ambient[i]=light[4+i];value.direct[i]=light[8+i];
    }
    out=value; return true;
}

// Use this independent camera readback to establish that Terrain's affine
// transform is global view, rather than a chunk-local/model-view transform.
inline bool cameraAgrees(const TerrainContext& context,const float* camera,float tolerance=.25f) {
    float distanceSquared=0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(camera[i]))return false;
        const float d=context.camera[i]-camera[i];distanceSquared+=d*d;
    }
    return distanceSquared<=tolerance*tolerance;
}

#ifdef _WIN32
// 0.3.160: self-read accounting for RenderProfile. Calls/bytes/failures are always counted
// (relaxed atomics); the clock is read only while `timed` is set (RenderProfile=1).
struct SelfReadStats {
    static inline std::atomic<unsigned long long> calls{0},bytes{0},failures{0};
    static inline std::atomic<long long> ticks{0};
    static inline std::atomic<bool> timed{false};
};
inline bool readSelfLive(uintptr_t address,void* output,size_t size) {
    SelfReadStats::calls.fetch_add(1,std::memory_order_relaxed);
    SelfReadStats::bytes.fetch_add(size,std::memory_order_relaxed);
    const bool timed=SelfReadStats::timed.load(std::memory_order_relaxed);
    LARGE_INTEGER begin{},end{};if(timed)QueryPerformanceCounter(&begin);
    SIZE_T copied=0;
    const bool ok=address>=0x10000 && ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<LPCVOID>(address),output,size,&copied) && copied==size;
    if(timed){QueryPerformanceCounter(&end);SelfReadStats::ticks.fetch_add(end.QuadPart-begin.QuadPart,std::memory_order_relaxed);}
    if(!ok)SelfReadStats::failures.fetch_add(1,std::memory_order_relaxed);
    return ok;
}
// 0.3.192 (CS): with a snapshot active on this thread (capture on the game thread, replay of a snapshot command on the
// replay thread) the read goes through it (snapshot_store.h); otherwise exactly the live read above, one null check apart.
inline bool readSelf(uintptr_t address,void* output,size_t size) {
    if(NorthlightStream::GameSnapshot* snapshot=NorthlightStream::activeSnapshot)
        return NorthlightStream::snapshotRead(*snapshot,address,output,size,readSelfLive,NorthlightStream::codeRange());
    return readSelfLive(address,output,size);
}

inline bool supportedClient() {
    // This client is a fixed-base PE32. Reject another image/base outright.
    if(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))!=0x400000)return false;
    static const unsigned char cameraCode[]={
        0x55,0x8b,0xec,0xa1,0x6c,0x43,0xb7,0x00,
        0x8b,0x80,0x20,0x7e,0x00,0x00,0x8b,0x50,0x08};
    static const unsigned char mapCode[]={
        0x56,0x68,0xd0,0x06,0xce,0x00,0xe8,0xf9,0xef,0xfa,0xff,
        0x68,0xd0,0x06,0xce,0x00,0x68,0xd0,0x07,0xce,0x00};
    unsigned char actualCamera[sizeof(cameraCode)],actualMap[sizeof(mapCode)];
    return readSelf(0x4f6650,actualCamera,sizeof actualCamera) &&
        readSelf(0x7bfd1c,actualMap,sizeof actualMap) &&
        !std::memcmp(actualCamera,cameraCode,sizeof cameraCode) &&
        !std::memcmp(actualMap,mapCode,sizeof mapCode);
}

// Basename corresponds directly to Map.dbc Directory (Azeroth, Kalimdor,...).
// On failure no partially read result is published to the caller.
inline bool readMapAndCamera(char (&map)[64],float (&camera)[3]) {
    static const bool supported=supportedClient();
    if(!supported)return false;
    uint32_t worldFrame=0,cameraPointer=0;float candidateCamera[3];char name[64];
    if(!readSelf(0xb7436c,&worldFrame,4) || !worldFrame ||
        !readSelf(uintptr_t(worldFrame)+0x7e20,&cameraPointer,4) || !cameraPointer ||
        !readSelf(uintptr_t(cameraPointer)+8,candidateCamera,sizeof candidateCamera) ||
        !readSelf(0xce06d0,name,sizeof name))return false;
    unsigned count=0;
    for(;count<sizeof name && name[count];++count) {
        const char c=name[count];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return false;
    }
    if(count==0 || count==sizeof name)return false;
    for(float coordinate:candidateCamera)
        if(!std::isfinite(coordinate)||std::fabs(coordinate)>100000.f)return false;
    std::memcpy(map,name,count+1);std::memcpy(camera,candidateCamera,sizeof candidateCamera);
    return true;
}
#endif
} // namespace NorthlightWorldContext
