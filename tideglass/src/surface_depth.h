#pragma once
#include <d3d9.h>

namespace water
{
// Converts a G32R32F (positive view distance, coverage) mask to raw D3D9 depth.
// The host's opaque depth and mask are read-only. The result is borrowed until
// the next Build/ReleaseDefaultPool; all calls belong to the device thread.
class SurfaceDepth
{
public:
    SurfaceDepth() = default;
    SurfaceDepth(const SurfaceDepth&) = delete;
    SurfaceDepth& operator=(const SurfaceDepth&) = delete;
    ~SurfaceDepth() { ReleaseAll(); }
    IDirect3DTexture9* Build(IDirect3DDevice9* device, IDirect3DTexture9* opaqueDepth,
                            IDirect3DTexture9* distanceCoverage, float nearPlane, float farPlane,
                            float minimumDepth, float maximumDepth);
    void ReleaseDefaultPool();
    void ReleaseAll();
private:
    bool Ensure(IDirect3DDevice9* device, UINT width, UINT height);
    IDirect3DDevice9* m_device = nullptr;
    IDirect3DTexture9* m_output = nullptr;
    IDirect3DSurface9* m_surface = nullptr;
    IDirect3DStateBlock9* m_state = nullptr;
    IDirect3DVertexShader9* m_vertex = nullptr;
    IDirect3DPixelShader9* m_pixel = nullptr;
    IDirect3DVertexDeclaration9* m_declaration = nullptr;
    UINT m_width = 0, m_height = 0;
};
}
