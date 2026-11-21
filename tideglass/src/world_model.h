#pragma once
#include "world_bytes.h"
#include "world_mesh.h"
#include <array>
#include <string>
#include <vector>

namespace water::world
{
enum class ModelError { None, Header, Layout, Geometry, Pose, Material, Budget };

struct ModelMotion
{
    struct Translation {uint32_t time=0;std::array<float,3> value{};};
    struct Rotation {uint32_t time=0;std::array<float,4> value{};};
    struct Bone {std::array<float,3> pivot{};std::vector<Translation> translations;std::vector<Rotation> rotations;};
    struct Influence {std::array<uint8_t,4> bones{},weights{};};
    uint32_t duration=0;
    std::vector<Bone> bones;
    std::vector<Influence> vertices; // expanded SKIN order, parallel to mesh vertices
    std::size_t Bytes() const;
};
using ModelPose = std::array<std::array<float,12>,256>; // independent rigid bone transforms
// Embedded, single looping clip (including aliases), independent bones, linear
// translation/rotation only. No camera-dependent bones or guessed external clips.
// An independently drawable batch survives an unsupported bone elsewhere in the model.
// Mesh and motion publish together; rejection preserves both outputs.
ModelError DecodeLoopingModel(Bytes model, Bytes skin, std::size_t limit, StaticModel& mesh, ModelMotion& motion);
bool SampleModelPose(const ModelMotion&, uint32_t milliseconds, ModelPose&);
ModelVertex DeformModelVertex(const ModelVertex&, const ModelMotion::Influence&, const ModelPose&);

// MD20 version 264 + explicit SKIN profile. Static root-zero geometry only;
// animation, billboard bones, colour/UV tracks and richer combiners are rejected.
// Material IDs/flags are retained for the GPU consumer, not baked into vertices.
// CPU only. Limit includes retained vectors/strings/metadata, not inputs, an old
// output, allocator overhead or the enclosing scene. Failure preserves output.
ModelError DecodeStaticModel(Bytes model, Bytes skin, std::size_t limit, StaticModel& output);
// A world placement seen only in a reflection: its solid and cut-out layers at rest, whatever its bones, animation and other layers do.
// Fails only when the file is malformed or no such layer remains.
ModelError DecodeRestModel(Bytes model, Bytes skin, std::size_t limit, StaticModel& output);
}
