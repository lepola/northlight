#pragma once
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
};
struct StaticModel
{
    std::vector<ModelVertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<ModelTexture> textures;
    std::vector<ModelDraw> draws;
    std::array<float,3> minimum{},maximum{};
    std::size_t Bytes() const
    {
        std::size_t n=sizeof(*this)+vertices.capacity()*sizeof(ModelVertex)+indices.capacity()*sizeof(uint16_t)+
            textures.capacity()*sizeof(ModelTexture)+draws.capacity()*sizeof(ModelDraw);
        for (const auto& t:textures) n+=t.name.capacity()+1;
        return n;
    }
};
}
