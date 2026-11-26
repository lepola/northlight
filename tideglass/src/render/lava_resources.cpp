#include "render/renderer.h"
#include "base/log.h"
#include "d3d_resource.h"

using water::SafeRelease;
using screen_pass::BindPointTexture;
using screen_pass::BindLinearTexture;
using screen_pass::SetTarget;

void Renderer::LavaResources::EndFrame()
{
    surface.thisFrame = surface.outdoorThisFrame = false;
    drewThisFrame = maskReady = ordinarySeen = felSeen = false;
    stir.advancedThisFrame = false;
}

void Renderer::LavaResources::ReleaseDefaultPool()
{
    ReleaseDeferred(surface);
    SafeRelease(mask);
    SafeRelease(halo);
    SafeRelease(haloColours);
    SafeRelease(visibleQuery);
    visiblePending = hidden = false;
    contactDue = 0.0;
    maskReady = false;
    SafeRelease(cells);
    SafeRelease(noise);
    SafeRelease(foam);
    SafeRelease(flow);
    ReleaseSurface(stir);
    EndFrame();
}

void Renderer::LavaResources::ReleaseManaged()
{
    SafeRelease(surfacePs);
    SafeRelease(stirPs);
    SafeRelease(maskPs);
    SafeRelease(haloPs);
    SafeRelease(glowPs);
    SafeRelease(bakeCellsPs);
    SafeRelease(bakeSlopePs);
    SafeRelease(bakeNoisePs);
    SafeRelease(bakeFoamPs);
    SafeRelease(bakeFlowPs);
    SafeRelease(rand);
    SafeRelease(table);
}

void Renderer::LavaResources::PollVisibility(IDirect3DDevice9* dev)
{
    // Last time's count of the lava's pixels, once the GPU has it. Never waits: lava that comes into sight costs a frame or
    // two of its stirring, which nothing shows.
    if (visiblePending)
    {
        DWORD pixels = 0;
        const HRESULT state = visibleQuery->GetData(&pixels, sizeof(pixels), 0);
        if (state != S_FALSE) visiblePending = false;
        if (state == S_OK) hidden = pixels == 0;
    }
    if (!visibleQuery && FAILED(dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, &visibleQuery)))
        visibleQuery = nullptr;
}

bool Renderer::LavaResources::EnsureHalo(IDirect3DDevice9* dev,UINT width,UINT height)
{
    auto ensure = [&](IDirect3DTexture9*& texture) {
        D3DSURFACE_DESC existing = {};
        if (texture && (FAILED(texture->GetLevelDesc(0, &existing)) || existing.Width != width || existing.Height != height))
            SafeRelease(texture);
        return texture || SUCCEEDED(dev->CreateTexture(width, height, 0, D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP,
                                                       D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &texture, nullptr));
    };
    return ensure(halo) && (!(felSeen && ordinarySeen) || ensure(haloColours));
}

bool Renderer::LavaResources::EnsureMask(IDirect3DDevice9* dev,UINT width,UINT height)
{
    D3DSURFACE_DESC existing = {};
    if (mask && (FAILED(mask->GetLevelDesc(0, &existing)) || existing.Width != width || existing.Height != height))
        SafeRelease(mask);
    if (!mask && FAILED(dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_G32R32F,
                                                 D3DPOOL_DEFAULT, &mask, nullptr)))
        return false;
    return true;
}

// The lava's patterns (lava_bake_*_ps), drawn once and again after a Reset, so the surface only samples them. Leaves the
// render targets as they are for the caller to set again.
bool Renderer::LavaResources::BakePatterns(Renderer& renderer,IDirect3DDevice9* dev)
{
    if (cells && noise && foam && flow)
        return true;
    auto bake = [&](IDirect3DTexture9*& target, UINT size, D3DFORMAT format, DWORD usage, IDirect3DPixelShader9*& shader,
                    IDirect3DTexture9* input) {
        if (FAILED(dev->CreateTexture(size, size, usage & D3DUSAGE_AUTOGENMIPMAP ? 0 : 1, usage, format, D3DPOOL_DEFAULT,
                                      &target, nullptr)) ||
            !SetTarget(dev, target))
            return false;
        const D3DVIEWPORT9 whole = {0, 0, size, size, 0.0f, 1.0f};
        const float constants[4] = {1.0f / size, 0.0f, 0.0f, 0.0f};
        if (FAILED(dev->SetViewport(&whole)) || FAILED(dev->SetPixelShaderConstantF(0, constants, 1)) ||
            FAILED(renderer.BindPixelShader(dev, shader)) || !BindLinearTexture(dev, 0, input))
            return false;
        for (DWORD axis : {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV})
            dev->SetSamplerState(0, static_cast<D3DSAMPLERSTATETYPE>(axis), D3DTADDRESS_WRAP);
        if (FAILED(DrawScreenTriangle(dev)))
            return false;
        if (usage & D3DUSAGE_AUTOGENMIPMAP)
            target->GenerateMipSubLevels();
        return true;
    };
    constexpr DWORD kMipped = D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP;
    // Two fixed tables of random numbers (mulberry32, as the lab's are): the bakes', and which cells have a bubble or a spark.
    const auto fillTable = [&](IDirect3DTexture9*& table, UINT size, uint32_t state) {
        if (table)
            return true;
        D3DLOCKED_RECT locked = {};
        if (FAILED(dev->CreateTexture(size,size,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&table,nullptr)))
            return false;
        if (FAILED(table->LockRect(0,&locked,nullptr,0))) {
            SafeRelease(table);
            return false;
        }
        const auto next = [&state]() {
            state += 0x6d2b79f5u;
            uint32_t t = (state ^ (state >> 15)) * (1u | state);
            t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
            return static_cast<unsigned char>((t ^ (t >> 14)) >> 24);
        };
        for (UINT y = 0; y < size; ++y)
        {
            auto* row = static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch;
            for (UINT x = 0; x < size; ++x)
            {
                // In the lab's order, red first; the texture holds blue first.
                const unsigned char r = next(), g = next(), b = next(), a = next();
                row[x * 4] = b;
                row[x * 4 + 1] = g;
                row[x * 4 + 2] = r;
                row[x * 4 + 3] = a;
            }
        }
        if (SUCCEEDED(table->UnlockRect(0))) return true;
        SafeRelease(table);
        return false;
    };
    bool ok = fillTable(table, 256, 67603) && fillTable(rand, 64, 1337) && BindPointTexture(dev, 1, table);
    IDirect3DTexture9* plates = nullptr;
    ok = ok && bake(plates, 1024, D3DFMT_A8R8G8B8, D3DUSAGE_RENDERTARGET, bakeCellsPs, nullptr) &&
         bake(cells, 1024, D3DFMT_A8R8G8B8, kMipped, bakeSlopePs, plates) &&
         bake(noise, 512, D3DFMT_A8R8G8B8, kMipped, bakeNoisePs, nullptr) &&
         bake(foam, 1024, D3DFMT_A8R8G8B8, kMipped, bakeFoamPs, nullptr) &&
         bake(flow, 512, D3DFMT_A16B16G16R16F, D3DUSAGE_RENDERTARGET, bakeFlowPs, nullptr);
    BindPointTexture(dev, 0, nullptr);
    BindPointTexture(dev, 1, nullptr);
    SafeRelease(plates);
    if (!ok)
    {
        SafeRelease(cells);
        SafeRelease(noise);
        SafeRelease(foam);
        SafeRelease(flow);
        return renderer.Skip("the lava's patterns could not be baked");
    }
    WATER_LOG_INFO("lava patterns baked");
    return true;
}
