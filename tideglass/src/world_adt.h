#pragma once
#include "world_bytes.h"
#include "world_placement.h"
#include <array>
#include <string>
#include <vector>

namespace water::world
{
enum class AdtAlphaFormat { Packed4, Byte8 }; // selected from map metadata, never inferred from file size
struct TerrainVertex
{
    std::array<float,3> position{}, normal{};
    std::array<float,2> uv{}; // unit cell coordinates; material owns texture/alpha sampling scale
};
struct TerrainLayer
{
    uint32_t texture = 0, flags = 0, effect = 0;
    std::vector<uint8_t> alpha; // 64x64 independent layer coverage; base is one minus their sum
};
struct TerrainCell
{
    uint32_t x = 0, y = 0, flags = 0;
    uint16_t holes = 0;
    std::vector<TerrainVertex> vertices=std::vector<TerrainVertex>(145); // source decode: 145; prepared distance detail: variable
    unsigned alphaSize=64; // source decode: 64; prepared cells retain their admitted resolution
    std::vector<uint16_t> indices; // upward winding, omitting source holes
    std::vector<TerrainLayer> layers; // source order; coverage resolved before filtering/distance reduction
    std::vector<uint8_t> shadow; // 512 packed MCSH bytes when present; consumed into the render atlas
    std::vector<uint32_t> colours; // parallel to vertices when the map paints its terrain (MCCV, BGRA bytes); else empty
    std::array<float,3> minimum{}, maximum{};
    // The consumer must respect the source's edge-fix policy when sampling alpha.
    bool FixAlphaEdges() const { return !(flags&0x8000); }
};
// A cell's liquid in the format before MH2O: Outland's. Heights are absolute; a tile with its bit 3 set is not drawn and its
// corners may hold no height at all.
struct LegacyLiquid
{
    uint16_t type=1; // LiquidType: 1 river, 2 ocean, 3 magma, 4 slime
    std::array<float,81> heights{};
    std::array<uint8_t,64> tiles{};
    bool Drawn(unsigned x,unsigned y)const{return !(tiles[y*8+x]&8);}
};
struct AdtModelPlacement
{
    uint32_t model = 0, uid = 0;
    PlacementPose pose;
    uint16_t flags = 0;
};
struct AdtWmoPlacement
{
    uint32_t model=0,uid=0;
    PlacementPose pose;
    uint16_t flags=0,doodadSet=0,nameSet=0;
    std::array<float,3> minimum{},maximum{}; // native MODF extent converted to world axes
};
struct AdtTile
{
    std::vector<std::string> textures, models, wmoModels;
    std::vector<TerrainCell> cells; // y*16+x; all 256 required
    std::vector<AdtModelPlacement> placements;
    std::vector<AdtWmoPlacement> wmoInstances;
    uint32_t wmoPlacements = 0; // source count, including unsupported roots
    std::size_t Bytes() const;
};
enum class AdtError { None, Header, Layout, Geometry, Material, Placement, Unsupported, Budget };

// CPU-only Wrath ADT. Preserves terrain normals/layers/shadows and MDDF source
// poses. Animated/glowing layers retain their flags; compressed alpha and vertex colours
// still require explicit support.
// Limit covers retained payload/capacity/metadata, excluding inputs, prior output
// and allocator overhead. Failed decoding never replaces accepted output.
AdtError DecodeAdt(Bytes input, AdtAlphaFormat alpha, std::size_t limit, AdtTile& output);
// cell: a map cell's bytes after its chunk header. False on a layout not understood; true with nothing for a cell with no liquid.
bool DecodeLegacyLiquid(Bytes cell, std::vector<LegacyLiquid>& output);
}
