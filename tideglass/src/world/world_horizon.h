#pragma once
#include "world/world_bytes.h"
#include "world/world_mesh.h"
#include "world/owned_world_streaming.h"
#include <atomic>

namespace water::world
{
constexpr float HorizonReach=4096;
constexpr std::size_t HorizonInputLimit=4*1024*1024,HorizonMeshLimit=3*1024*1024;
enum class HorizonError { None, Missing, Layout, Budget, Cancelled, Archive, Unsupported };
// Wrath WDL heightfield, sampled every other outer vertex (66 2/3 yards).
// World axes match ADT: row decreases X, column decreases Y. Keep source holes.
// No textures, cache files or detailed placements. Transactional, bounded output.
HorizonError DecodeHorizon(Bytes,const OwnedWorldRegion&,const std::atomic<bool>&,
                           std::size_t limit,StaticModel&,unsigned& tiles);
}
