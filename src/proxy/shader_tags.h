#pragma once
// Vertex-shader identity from bytecode, shared by Device::CreateVertexShader and the command stream's
// snapshot triggers (0.3.192 CS). Pure and portable: static tables only, no device, no game memory.
#include <cstddef>
#include <cstdint>
#include "signatures.h"
#include "world_shader_signatures.h"
#include "wmo_shader_signatures.h"

namespace NorthlightShaderTags {
// Low bits are the Device's vsTags values (1 terrain, 2 UI). kWorld: any shader of kWorldShaderSignatures
// (terrain, WMO and model VS). kWmo: a WMO shader of kWmoShaderSignatures. The last two exist for triggers only.
constexpr int kTerrain=1,kUi=2;
constexpr unsigned kWorld=8,kWmo=16,kTriggerWorld=kTerrain|kWmo;
// FNV-1a over the bytecode bytes: the identity every table above is keyed by (renderer.cpp shaderHash).
inline std::uint64_t fnv1a(const void* data,std::size_t size){
    const unsigned char* bytes=static_cast<const unsigned char*>(data);std::uint64_t h=14695981039346656037ULL;
    for(std::size_t i=0;i<size;++i)h=(h^bytes[i])*1099511628211ULL;
    return h;
}
template<std::size_t N> inline bool inTable(const std::uint64_t (&a)[N],std::uint64_t value){for(auto x:a)if(x==value)return true;return false;}
// The Device's own tag, byte for byte what CreateVertexShader computed inline before 0.3.192.
inline int deviceTag(std::uint64_t hash){return inTable(kTerrainVS,hash)?kTerrain:inTable(kUiVS,hash)?kUi:0;}
inline bool inWorldSignatures(std::uint64_t hash){
    std::size_t low=0,high=sizeof(kWorldShaderSignatures)/sizeof(kWorldShaderSignatures[0]);
    while(low<high){const std::size_t middle=(low+high)/2;if(kWorldShaderSignatures[middle].hash<hash)low=middle+1;else high=middle;}
    return low<sizeof(kWorldShaderSignatures)/sizeof(kWorldShaderSignatures[0])&&kWorldShaderSignatures[low].hash==hash;
}
inline bool inWmoSignatures(std::uint64_t hash){
    std::size_t low=0,high=sizeof(kWmoShaderSignatures)/sizeof(kWmoShaderSignatures[0]);
    while(low<high){const std::size_t middle=(low+high)/2;if(kWmoShaderSignatures[middle].hash<hash)low=middle+1;else high=middle;}
    return low<sizeof(kWmoShaderSignatures)/sizeof(kWmoShaderSignatures[0])&&kWmoShaderSignatures[low].hash==hash;
}
// deviceTag plus the world/WMO bits, from the hash.
inline unsigned triggerTags(std::uint64_t hash){
    return unsigned(deviceTag(hash))|(inWorldSignatures(hash)?kWorld:0u)|(inWmoSignatures(hash)?kWmo:0u);
}
// From the bytecode the game passed to CreateVertexShader (the bytes the shader reports back later); bytes = its length
// up to and including the 0x0000FFFF end token, so the stream can tag a shader before the replay thread creates it.
inline unsigned triggerTagsOfBytecode(const void* code,std::size_t bytes){return triggerTags(fnv1a(code,bytes));}
}
