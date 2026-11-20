#pragma once
#include "world_bytes.h"
#include "world_mesh.h"

namespace water::world
{
struct WmoMaterial
{
    ModelTexture texture;
    uint32_t flags=0,shader=0,blend=0,nightEmission=0; // retain original material policy
};
struct WmoDoodad
{
    std::string name;
    std::array<float,16> transform{}; // local row-vector pose, composed with the root instance
    uint32_t colour=0;
    uint16_t set=0xffff; // first containing MODS range; unowned entries are not instantiated
    uint8_t flags=0;
};
struct WmoRoot
{
    uint32_t groups=0,doodads=0,flags=0;
    std::vector<WmoMaterial> materials;
    std::size_t Bytes() const;
};
struct WmoAttachments
{
    std::vector<WmoDoodad> models;
    std::size_t Bytes() const;
};
// Validated, borrowed MLIQ grid. It lives only while the archive entry does;
// liquid geometry is independent of the group's drawable material batches.
struct WmoLiquid
{
    Bytes vertices,tiles;
    unsigned width=0,height=0;
    uint16_t type=0;
    std::array<float,3> origin{};
    bool Quad(unsigned x,unsigned y,std::array<std::array<double,3>,4>& points) const;
};
enum class WmoError { None,Header,Layout,Geometry,Material,Unsupported,Budget };

// Version 17, opaque/alpha-key base-texture geometry. Render only MOBA ranges,
// never collision-only triangles. Embedded attachment poses/sets and group references
// are retained; their model animation/material admission belongs to the scene consumer.
// Group portals, local lights and authored vertex-lighting remain later appearance scope.
// CPU only; both functions validate/admit before allocation and preserve prior
// output on failure. Limits exclude inputs, prior output and allocator overhead.
WmoError DecodeWmoRoot(Bytes input,std::size_t limit,WmoRoot& output);
WmoError DecodeWmoGroup(Bytes input,const WmoRoot& root,std::size_t limit,StaticModel& output);
// Whether a group is only ever seen from inside: marked interior and not exterior.
bool WmoGroupOnlyIndoors(Bytes input);
WmoError DecodeWmoLiquid(Bytes input,WmoLiquid& output);
// Optional attachments have their own failure boundary: malformed/unsupported
// attachment records never invalidate the containing root/group geometry.
WmoError DecodeWmoAttachments(Bytes input,std::size_t limit,WmoAttachments& output);
WmoError DecodeWmoReferences(Bytes input,uint32_t doodadCount,std::size_t limit,std::vector<uint16_t>& output);
}
