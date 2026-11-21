#pragma once
#include <d3d9.h>

// Shared by production passes, input readback and offline replay. The caller owns
// shader/binding/target/state setup and restoration; this only submits the triangle.
HRESULT DrawScreenTriangle(IDirect3DDevice9* dev);
// Caller owns a screen-pass state block. A null area draws the whole target.
HRESULT DrawScreenArea(IDirect3DDevice9* dev, const RECT* area);
bool SetScreenPassState(IDirect3DDevice9* dev, IDirect3DVertexShader9* vs, IDirect3DVertexDeclaration9* decl);
bool EnsureScreenPassStateBlock(IDirect3DDevice9* device, IDirect3DStateBlock9*& state);

// A pass borrows its reusable state block and owns the references returned by
// GetRenderTarget/GetDepthStencilSurface/GetStreamSource until restoration.
class ScreenPassState
{
public:
    ScreenPassState() = default;
    explicit ScreenPassState(bool& frameRestoreFailed) : m_frameRestoreFailed(&frameRestoreFailed) {}
    ScreenPassState(const ScreenPassState&) = delete;
    ScreenPassState& operator=(const ScreenPassState&) = delete;
    ~ScreenPassState() { Restore(); }
    bool Capture(IDirect3DDevice9* device, IDirect3DStateBlock9* state);
    bool Restore();
    IDirect3DSurface9* color[4] = {};
    IDirect3DSurface9* depth = nullptr;
private:
    IDirect3DDevice9* m_device = nullptr;
    IDirect3DStateBlock9* m_state = nullptr;
    IDirect3DVertexBuffer9* m_stream = nullptr;
    UINT m_offset = 0, m_stride = 0, m_targets = 1;
    bool m_captured = false;
    bool* m_frameRestoreFailed = nullptr;
};

namespace screen_pass {
// The rollback must retain the original sample values at geometry edges.
bool SaveMultisampleColour(IDirect3DDevice9* device, IDirect3DSurface9* source,
                           const D3DSURFACE_DESC& desc, IDirect3DSurface9*& backup);
bool BindPointTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex);
bool BindLinearTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex);
bool SetTarget(IDirect3DDevice9* dev, IDirect3DTexture9* tex, DWORD slot = 0);
}
