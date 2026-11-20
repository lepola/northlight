#include "liquid.h"

#include "log.h"

#include <cstdint>
#include <cstring>

namespace
{
constexpr UINT kMinAveragedSize = 4;
constexpr float kMagmaMinRed = 0.3f;
constexpr float kMagmaRedOverBlue = 1.4f;
constexpr float kMagmaRedOverGreen = 1.1f;
// Slime is where green clearly leads and blue is low: Undercity's slime (0.24 0.48 0.06) and Felwood's colour ramp
// (0.14 0.47 0.09), while greenish-teal water like Booty Bay's ramp (0.23 0.42 0.36), grey-green (0.3 0.4 0.3) and
// blue water stay water.
constexpr DWORD kVerdictLifetimeMs = 1000;
constexpr float kSlimeMinGreen = 0.12f;
constexpr float kSlimeGreenOverRed = 1.45f;
constexpr float kSlimeGreenOverBlue = 1.6f;

struct ColorSum
{
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    unsigned count = 0;

    void Add565(uint16_t c)
    {
        red += ((c >> 11) & 31) / 31.0;
        green += ((c >> 5) & 63) / 63.0;
        blue += (c & 31) / 31.0;
        ++count;
    }
    void Add8888(const unsigned char* bgra)
    {
        blue += bgra[0] / 255.0;
        green += bgra[1] / 255.0;
        red += bgra[2] / 255.0;
        ++count;
    }
};

bool IsBlockCompressed(D3DFORMAT format)
{
    return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 || format == D3DFMT_DXT3 || format == D3DFMT_DXT4 ||
           format == D3DFMT_DXT5;
}

void SumBlocks(const D3DSURFACE_DESC& desc, const D3DLOCKED_RECT& locked, ColorSum& sum)
{
    const UINT blockBytes = desc.Format == D3DFMT_DXT1 ? 8 : 16;
    const UINT colorOffset = desc.Format == D3DFMT_DXT1 ? 0 : 8;
    for (UINT row = 0; row < desc.Height / 4; ++row)
    {
        const auto* line = static_cast<const unsigned char*>(locked.pBits) + row * locked.Pitch;
        for (UINT block = 0; block < desc.Width / 4; ++block)
        {
            uint16_t endpoints[2];
            std::memcpy(endpoints, line + block * blockBytes + colorOffset, sizeof(endpoints));
            sum.Add565(endpoints[0]);
            sum.Add565(endpoints[1]);
        }
    }
}

void SumPixels(const D3DSURFACE_DESC& desc, const D3DLOCKED_RECT& locked, ColorSum& sum)
{
    for (UINT row = 0; row < desc.Height; ++row)
    {
        const auto* line = static_cast<const unsigned char*>(locked.pBits) + row * locked.Pitch;
        for (UINT x = 0; x < desc.Width; ++x)
            sum.Add8888(line + x * 4);
    }
}

bool AverageColorOf(IDirect3DBaseTexture9* base, float* rgb, D3DSURFACE_DESC& desc)
{
    IDirect3DTexture9* texture = nullptr;
    if (base->GetType() != D3DRTYPE_TEXTURE ||
        FAILED(base->QueryInterface(IID_IDirect3DTexture9, reinterpret_cast<void**>(&texture))))
        return false;
    UINT level = 0;
    for (UINT i = 0; i < texture->GetLevelCount(); ++i)
    {
        D3DSURFACE_DESC levelDesc;
        if (SUCCEEDED(texture->GetLevelDesc(i, &levelDesc)) && levelDesc.Width >= kMinAveragedSize &&
            levelDesc.Height >= kMinAveragedSize)
            level = i;
    }
    bool ok = false;
    D3DLOCKED_RECT locked;
    ColorSum sum;
    if (SUCCEEDED(texture->GetLevelDesc(level, &desc)) && desc.Pool != D3DPOOL_DEFAULT &&
        (IsBlockCompressed(desc.Format) || desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8) &&
        SUCCEEDED(texture->LockRect(level, &locked, nullptr, D3DLOCK_READONLY)))
    {
        if (IsBlockCompressed(desc.Format))
            SumBlocks(desc, locked, sum);
        else
            SumPixels(desc, locked, sum);
        texture->UnlockRect(level);
        ok = sum.count > 0;
    }
    texture->Release();
    if (ok)
    {
        rgb[0] = static_cast<float>(sum.red / sum.count);
        rgb[1] = static_cast<float>(sum.green / sum.count);
        rgb[2] = static_cast<float>(sum.blue / sum.count);
    }
    return ok;
}
}

bool AverageTextureColor(IDirect3DBaseTexture9* texture, float* rgb, D3DSURFACE_DESC& desc)
{
    return texture && AverageColorOf(texture, rgb, desc);
}

const char* LiquidKindName(LiquidKind kind)
{
    switch (kind)
    {
    case LiquidKind::Magma:
        return "magma";
    case LiquidKind::Slime:
        return "slime";
    case LiquidKind::Unknown:
        return "unknown";
    case LiquidKind::Fade:
        return "fade";
    default:
        return "water";
    }
}

LiquidKind ClassifyLiquidColor(float red, float green, float blue)
{
    if (red > kMagmaMinRed && red > blue * kMagmaRedOverBlue && red > green * kMagmaRedOverGreen)
        return LiquidKind::Magma;
    if (green > kSlimeMinGreen && green > red * kSlimeGreenOverRed && green > blue * kSlimeGreenOverBlue)
        return LiquidKind::Slime;
    return LiquidKind::Water;
}

LiquidKind LiquidClassifier::Classify(IDirect3DBaseTexture9* texture)
{
    if (!texture)
        return LiquidKind::Water;
    const DWORD now = GetTickCount();
    auto found = m_kinds.find(texture);
    if (found != m_kinds.end() && now - found->second.checkedAt < kVerdictLifetimeMs)
        return found->second.kind;

    float rgb[3] = {};
    D3DSURFACE_DESC desc = {};
    LiquidKind kind = LiquidKind::Water;
    const bool averaged = AverageTextureColor(texture, rgb, desc);
    if (desc.Pool == D3DPOOL_DEFAULT)
        return kind;
    if (averaged)
        kind = ClassifyLiquidColor(rgb[0], rgb[1], rgb[2]);
    const bool known = found != m_kinds.end();
    if (!known || found->second.kind != kind)
    {
        if (averaged)
            WATER_LOG_INFO("liquid texture %p (%ux%u, format 0x%X): average colour %.2f %.2f %.2f -> %s%s",
                           static_cast<void*>(texture), desc.Width, desc.Height, static_cast<unsigned>(desc.Format),
                           rgb[0], rgb[1], rgb[2], LiquidKindName(kind), known ? " (its image changed)" : "");
        else
            WATER_LOG_INFO("liquid texture %p (format 0x%X) could not be read; treated as water",
                           static_cast<void*>(texture), static_cast<unsigned>(desc.Format));
    }
    if (known)
        found->second = {kind, now};
    else
    {
        texture->AddRef();
        m_kinds.emplace(texture, Verdict{kind, now});
    }
    return kind;
}

void LiquidClassifier::ReleaseAll()
{
    for (auto& entry : m_kinds)
        entry.first->Release();
    m_kinds.clear();
}
