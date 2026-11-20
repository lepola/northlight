#include "surface_depth.h"
#include "screen_pass.h"
#include "ps_surface_depth.h"
#include "vs_fullscreen.h"
#include <cmath>
#include <initializer_list>

namespace water
{
namespace
{
template<class T> void Release(T*& value) { if (value) value->Release(); value = nullptr; }
}
void SurfaceDepth::ReleaseDefaultPool()
{
    Release(m_state);
    Release(m_surface);
    Release(m_output);
    m_width = m_height = 0;
}
void SurfaceDepth::ReleaseAll()
{
    ReleaseDefaultPool();
    Release(m_vertex);
    Release(m_pixel);
    Release(m_declaration);
    m_device = nullptr;
}
bool SurfaceDepth::Ensure(IDirect3DDevice9* device, UINT width, UINT height)
{
    if (m_device && m_device != device) return false;
    m_device = device;
    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
    if ((!m_vertex && FAILED(p_vs_fullscreen.Create(device, &m_vertex))) ||
        (!m_pixel && FAILED(p_ps_surface_depth.Create(device, &m_pixel))) ||
        (!m_declaration && FAILED(device->CreateVertexDeclaration(elements, &m_declaration)))) return false;
    if (width != m_width || height != m_height) ReleaseDefaultPool();
    if (!m_output) {
        if (FAILED(device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F,
                                         D3DPOOL_DEFAULT, &m_output, nullptr)) ||
            FAILED(m_output->GetSurfaceLevel(0, &m_surface))) {
            ReleaseDefaultPool();
            return false;
        }
        m_width = width;
        m_height = height;
    }
    return EnsureScreenPassStateBlock(device, m_state);
}
IDirect3DTexture9* SurfaceDepth::Build(IDirect3DDevice9* device, IDirect3DTexture9* opaque,
                                    IDirect3DTexture9* mask, float nearPlane, float farPlane,
                                    float minimumDepth, float maximumDepth)
{
    if (!device || !opaque || !mask || opaque == m_output || mask == m_output ||
        !std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0 || farPlane <= nearPlane ||
        !std::isfinite(minimumDepth) || !std::isfinite(maximumDepth) || minimumDepth < 0 ||
        maximumDepth > 1 || maximumDepth <= minimumDepth) return nullptr;
    const double a = double(farPlane) / (double(farPlane) - nearPlane);
    const float parameters[4] = {float(a), float(-a * nearPlane), minimumDepth, maximumDepth - minimumDepth};
    if (!std::isfinite(parameters[0]) || !std::isfinite(parameters[1])) return nullptr;
    D3DSURFACE_DESC scene = {}, coverage = {};
    if (FAILED(opaque->GetLevelDesc(0, &scene)) || FAILED(mask->GetLevelDesc(0, &coverage)) ||
        !scene.Width || !scene.Height || scene.Width != coverage.Width || scene.Height != coverage.Height ||
        coverage.Format != D3DFMT_G32R32F || !Ensure(device, scene.Width, scene.Height)) return nullptr;
    ScreenPassState saved;
    if (!saved.Capture(device, m_state)) return nullptr;
    bool ok = SetScreenPassState(device, m_vertex, m_declaration);
    for (DWORD i = 1; i < 4; ++i) device->SetRenderTarget(i, nullptr);
    for (DWORD i = 0; i < 16; ++i) device->SetTexture(i, nullptr);
    ok = SUCCEEDED(device->SetRenderTarget(0, m_surface)) && ok;
    const D3DVIEWPORT9 viewport = {0, 0, scene.Width, scene.Height, 0, 1};
    const float grid[4] = {1.0f / scene.Width, 1.0f / scene.Height, nearPlane, farPlane};
    ok = SUCCEEDED(device->SetViewport(&viewport)) && SUCCEEDED(device->SetPixelShader(m_pixel)) &&
         SUCCEEDED(device->SetPixelShaderConstantF(0, parameters, 1)) &&
         SUCCEEDED(device->SetPixelShaderConstantF(1, grid, 1)) && ok;
    for (DWORD i = 0; i < 2; ++i) {
        ok = SUCCEEDED(device->SetTexture(i, i ? mask : opaque)) && ok;
        for (const auto& value : {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV})
            ok = SUCCEEDED(device->SetSamplerState(i, value, D3DTADDRESS_CLAMP)) && ok;
        for (const auto& value : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER})
            ok = SUCCEEDED(device->SetSamplerState(i, value, D3DTEXF_POINT)) && ok;
        ok = SUCCEEDED(device->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE)) &&
             SUCCEEDED(device->SetSamplerState(i, D3DSAMP_MAXMIPLEVEL, 0)) &&
             SUCCEEDED(device->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, FALSE)) && ok;
    }
    if (ok) ok = SUCCEEDED(DrawScreenTriangle(device));
    ok = saved.Restore() && ok;
    return ok ? m_output : nullptr;
}
}
