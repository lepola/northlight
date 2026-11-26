#pragma once

#include "frame/frame_inputs.h"
#include "client/ground.h"
#include <memory>
#include <vector>

namespace water
{
// Capture on the input owner's thread, then publish as const to the device thread.
// No game address, worker, or COM object is retained by this packet.
class FrameSnapshot
{
public:
    static std::unique_ptr<const FrameSnapshot> Capture(const FrameInputs& in)
    {
        if (in.moverCount < 0 || in.moverCount > kMaxMovers ||
            in.boatCount < 0 || in.boatCount > kMaxBoats ||
            in.splashCount < 0 || in.splashCount > kMaxSplashes ||
            in.nativeLiquids.textures.size()>NativeLiquidFrame::MaxTextures ||
            (in.splashCount && !in.splashes))
            return nullptr;
        for (int i = 0; i < in.boatCount; ++i)
            if (in.boats[i].trailCount < 0 || in.boats[i].trailCount > kBoatTrailPoints)
                return nullptr;
        return std::unique_ptr<const FrameSnapshot>(new FrameSnapshot(in));
    }

    FrameSnapshot(const FrameSnapshot&) = delete;
    FrameSnapshot& operator=(const FrameSnapshot&) = delete;
    const FrameInputs& Inputs() const { return m_inputs; }

private:
    explicit FrameSnapshot(const FrameInputs& in) : m_inputs(in)
    {
        if (in.splashCount)
            m_splashes.assign(in.splashes, in.splashes + in.splashCount);
        m_ground = in.ground ? in.ground->Snapshot() : nullptr;
        m_inputs.splashes = m_splashes.empty() ? nullptr : m_splashes.data();
        m_inputs.ground = m_ground.get();
        m_farGround = in.farGround ? in.farGround->Snapshot() : nullptr;
        m_inputs.farGround = m_farGround.get();
    }

    FrameInputs m_inputs;
    std::vector<Splash> m_splashes;
    std::shared_ptr<const GroundMap> m_ground;
    std::shared_ptr<const GroundMap> m_farGround;
};
}
