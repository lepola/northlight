#pragma once
#include "frame_values.h"
#include <vector>

namespace water
{
// Only camera data is retained; native draw constants remain draw-local.
struct LiquidDrawCamera
{
    float cameraRelativeView[16]{}, glProjection[16]{}, cameraPosition[3]{};
    void Set(const WaterFrameValues& frame);
};

// Narrow proofs for ordinary affine liquid shaders. Unsupported programs,
// declarations and transforms fail open to conservative reflection coverage.
bool LiquidAffinePosition(const DWORD* words, std::size_t count);
bool LiquidWorldBounds(const LiquidDrawCamera&, const float* rawBounds, const float (&constants)[9][4],
                       float* worldBounds);

// Session-owned, bounded shader proof cache. Retained COM references prevent a
// recycled pointer inheriting an earlier shader's proof; loss releases them.
// Constants and declaration are always current. No vertex-buffer reads here.
class LiquidDrawTransform
{
    struct Shader
    {
        IDirect3DVertexShader9* value;
        bool affine;
    };
    std::vector<Shader> m_shaders;

  public:
    LiquidDrawTransform() = default;
    ~LiquidDrawTransform();
    LiquidDrawTransform(const LiquidDrawTransform&) = delete;
    LiquidDrawTransform& operator=(const LiquidDrawTransform&) = delete;
    void Clear();
    bool Supported(IDirect3DDevice9*);
    bool Bounds(IDirect3DDevice9*, const LiquidDrawCamera&, const float* rawBounds, float* worldBounds);
};
} // namespace water
