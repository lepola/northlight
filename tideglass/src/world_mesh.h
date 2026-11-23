#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace water::world
{
// CPU geometry shared by independently decoded M2 and WMO sources. Source
// animation/material policy belongs to each decoder, not to GPU residency.
struct ModelVertex
{
    std::array<float,3> position{}, normal{};
    std::array<float,2> uv{};
};
struct ModelTexture { std::string name; uint32_t flags=0; };
struct ModelDraw
{
    uint32_t firstIndex=0,indexCount=0;
    uint16_t texture=0,materialFlags=0,blend=0,shader=0;
    uint32_t nightEmission=0; // authored RGB, independent of diffuse lighting; zero means none
    uint32_t uvTrack=UINT32_MAX;
};
struct ScrollingLayer
{
    struct Key {uint32_t time;float v;};
    uint32_t period=0;
    bool usesModelTime=false;
    std::array<float,3> centre{};
    std::vector<Key> keys;
    // Native draw identity for independently scaled SKIN submeshes.
    uint32_t vertices=0, paletteSize=0;
    std::array<uint8_t,48> firstVertex{};
    std::array<uint8_t,12> palette{};
    float Offset(uint32_t clock) const
    {
        const auto t=clock%period;
        const auto end=std::upper_bound(keys.begin(),keys.end(),t,[](uint32_t time,const Key& key){return time<key.time;});
        const auto& start=*(end-1);
        return start.v+(end->v-start.v)*(float(t-start.time)/float(end->time-start.time));
    }
};
struct StaticModel
{
    std::vector<ModelVertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<ModelTexture> textures;
    std::vector<ModelDraw> draws;
    std::vector<ScrollingLayer> scroll; // indexed by ModelDraw::uvTrack for animated vertical texture coordinates
    std::array<float,3> minimum{},maximum{};
    std::size_t Bytes() const
    {
        std::size_t n=sizeof(*this)+vertices.capacity()*sizeof(ModelVertex)+indices.capacity()*sizeof(uint16_t)+
            textures.capacity()*sizeof(ModelTexture)+draws.capacity()*sizeof(ModelDraw)+scroll.capacity()*sizeof(ScrollingLayer);
        for (const auto& layer:scroll)n+=layer.keys.capacity()*sizeof(ScrollingLayer::Key);
        for (const auto& t:textures) n+=t.name.capacity()+1;
        return n;
    }
};
}
