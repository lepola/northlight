#include "screen_pass.h"
#include "d3d_resource.h"

HRESULT DrawScreenTriangle(IDirect3DDevice9* dev)
{
    static const float triangle[3][4] = {
        {-1.0f, -1.0f, 0.0f, 1.0f}, {-1.0f, 3.0f, 0.0f, 1.0f}, {3.0f, -1.0f, 0.0f, 1.0f}};
    return dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle, sizeof(triangle[0]));
}

HRESULT DrawScreenArea(IDirect3DDevice9* dev, const RECT* area)
{
    if (area && (area->left >= area->right || area->top >= area->bottom)) return D3D_OK;
    if (area && FAILED(dev->SetScissorRect(area))) return E_FAIL;
    if (FAILED(dev->SetRenderState(D3DRS_SCISSORTESTENABLE, area ? TRUE : FALSE))) return E_FAIL;
    const HRESULT drawn = DrawScreenTriangle(dev);
    const HRESULT restored = dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    return FAILED(drawn) ? drawn : restored;
}

bool SetScreenPassState(IDirect3DDevice9* dev, IDirect3DVertexShader9* vs, IDirect3DVertexDeclaration9* decl)
{
    bool ok = SUCCEEDED(dev->SetDepthStencilSurface(nullptr)) && SUCCEEDED(dev->SetVertexShader(vs)) &&
              SUCCEEDED(dev->SetVertexDeclaration(decl)) && SUCCEEDED(dev->SetStreamSourceFreq(0, 1));
    const struct { D3DRENDERSTATETYPE name; DWORD value; } states[] = {
        {D3DRS_ZENABLE, D3DZB_FALSE}, {D3DRS_ZWRITEENABLE, FALSE}, {D3DRS_ALPHATESTENABLE, FALSE},
        {D3DRS_ALPHABLENDENABLE, FALSE}, {D3DRS_SEPARATEALPHABLENDENABLE, FALSE}, {D3DRS_CULLMODE, D3DCULL_NONE},
        {D3DRS_STENCILENABLE, FALSE}, {D3DRS_TWOSIDEDSTENCILMODE, FALSE}, {D3DRS_SCISSORTESTENABLE, FALSE},
        {D3DRS_COLORWRITEENABLE, 0xF}, {D3DRS_SRGBWRITEENABLE, FALSE}, {D3DRS_FOGENABLE, FALSE},
        {D3DRS_CLIPPLANEENABLE, 0}, {D3DRS_FILLMODE, D3DFILL_SOLID}};
    for (const auto& state : states) ok = SUCCEEDED(dev->SetRenderState(state.name, state.value)) && ok;
    return ok;
}

bool EnsureScreenPassStateBlock(IDirect3DDevice9* device, IDirect3DStateBlock9*& state)
{
    if (state) return true;
    if (!device || FAILED(device->CreateStateBlock(D3DSBT_ALL, &state))) {
        if (state) state->Release();
        state = nullptr;
        return false;
    }
    return state != nullptr;
}

bool ScreenPassState::Capture(IDirect3DDevice9* device, IDirect3DStateBlock9* state)
{
    if (!Restore() || !device || !state) return false;
    m_device = device;
    m_state = state;
    if (FAILED(device->GetRenderTarget(0, &color[0])) || !color[0]) return false;
    m_targets = 1;
    for (UINT i = 1; i < 4; ++i) {
        const auto result = device->GetRenderTarget(i, &color[i]);
        if (FAILED(result) && result != D3DERR_NOTFOUND) break;
        m_targets = i + 1;
    }
    const auto depthResult = device->GetDepthStencilSurface(&depth);
    if ((FAILED(depthResult) && depthResult != D3DERR_NOTFOUND) ||
        FAILED(device->GetStreamSource(0, &m_stream, &m_offset, &m_stride)) || FAILED(state->Capture())) return false;
    m_captured = true;
    return true;
}

bool ScreenPassState::Restore()
{
    bool ok = true;
    if (m_captured) {
        auto step = [&](HRESULT result) { ok = SUCCEEDED(result) && ok; };
        step(m_device->SetDepthStencilSurface(nullptr));
        for (UINT i = 1; i < m_targets; ++i) step(m_device->SetRenderTarget(i, nullptr));
        step(m_device->SetRenderTarget(0, color[0]));
        for (UINT i = 1; i < m_targets; ++i)
            if (color[i]) step(m_device->SetRenderTarget(i, color[i]));
        step(m_device->SetDepthStencilSurface(depth));
        step(m_state->Apply());
        step(m_device->SetStreamSource(0, m_stream, m_offset, m_stride));
    }
    for (auto*& target : color) { if (target) target->Release(); target = nullptr; }
    if (depth) depth->Release();
    if (m_stream) m_stream->Release();
    depth = nullptr;
    m_stream = nullptr;
    m_device = nullptr;
    m_state = nullptr;
    m_captured = false;
    if (!ok && m_frameRestoreFailed) *m_frameRestoreFailed=true;
    return ok;
}

namespace screen_pass {
bool BindPointTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex)
{
    bool ok=SUCCEEDED(dev->SetTexture(stage,tex));
    const struct { D3DSAMPLERSTATETYPE name; DWORD value; } states[] = {
        {D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},
        {D3DSAMP_ADDRESSW,D3DTADDRESS_CLAMP},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},
        {D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},
        {D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_MAXMIPLEVEL,0}};
    for(const auto& state:states)ok=SUCCEEDED(dev->SetSamplerState(stage,state.name,state.value))&&ok;
    return ok;
}

bool BindLinearTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex)
{
    bool ok=BindPointTexture(dev,stage,tex);
    ok=SUCCEEDED(dev->SetSamplerState(stage,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR))&&ok;
    return SUCCEEDED(dev->SetSamplerState(stage,D3DSAMP_MINFILTER,D3DTEXF_LINEAR))&&ok;
}

bool SetTarget(IDirect3DDevice9* dev, IDirect3DTexture9* tex, DWORD slot)
{
    IDirect3DSurface9* surface=nullptr;
    const bool have=tex&&SUCCEEDED(tex->GetSurfaceLevel(0,&surface))&&surface;
    const bool bound=have&&SUCCEEDED(dev->SetRenderTarget(slot,surface));
    water::SafeRelease(surface);return bound;
}
}
