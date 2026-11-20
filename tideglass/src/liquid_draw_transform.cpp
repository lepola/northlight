#include "liquid_draw_transform.h"
#include "math/matrix.h"
#include "math/projection.h"
#include <limits>

namespace water
{
void LiquidDrawCamera::Set(const WaterFrameValues& frame)
{
    std::copy(frame.cameraRelativeView, frame.cameraRelativeView + 16, cameraRelativeView);
    std::copy(frame.glProjection, frame.glProjection + 16, glProjection);
    std::copy(frame.cameraPosition, frame.cameraPosition + 3, cameraPosition);
}
// The ordinary liquid vertex program first transforms POSITION0 by c5..8,
// then projects it with c0..3. Prove that small straight-line contract from the
// submitted program, not a texture identity or the apparent size of its box.
// Procedural/deformed or unfamiliar programs retain conservative visibility.
bool LiquidAffinePosition(const DWORD* words, std::size_t count)
{
    if (!words || count < 2 || count > 16384 || (words[0] != 0xfffe0200 && words[0] != 0xfffe0300))
        return false;
    const bool sm3 = words[0] == 0xfffe0300;
    const DWORD output = sm3 ? 0xe00f0000 : 0xc00f0000;
    // Authored semantic pattern; no client shader bytecode is embedded.
    const auto dst = [](unsigned type, unsigned index)
    { return DWORD(0x800f0000u | ((type & 7) << 28) | ((type & 24) << 8) | index); };
    const auto src = [](unsigned type, unsigned index, unsigned swizzle = 0xe4)
    { return DWORD(0x80000000u | ((type & 7) << 28) | ((type & 24) << 8) | (swizzle << 16) | index); };
    const DWORD r0 = dst(D3DSPR_TEMP, 0), r1 = dst(D3DSPR_TEMP, 1);
    const auto v = [&](unsigned channel) { return src(D3DSPR_INPUT, 0, channel * 0x55); };
    const auto c = [&](unsigned row) { return src(D3DSPR_CONST, row); };
    const auto t = [&](unsigned index, unsigned swizzle = 0xe4) { return src(D3DSPR_TEMP, index, swizzle); };
    const DWORD sequence[][5] = {
        {D3DSIO_MUL, r0, c(6), v(1), 0},          {D3DSIO_MAD, r0, v(0), c(5), t(0)},
        {D3DSIO_MAD, r0, v(2), c(7), t(0)},       {D3DSIO_ADD, r0, t(0), c(8), 0},
        {D3DSIO_MUL, r1, t(0, 0x55), c(1), 0},    {D3DSIO_MAD, r1, t(0, 0x00), c(0), t(1)},
        {D3DSIO_MAD, r1, t(0, 0xaa), c(2), t(1)}, {D3DSIO_MAD, output, t(0, 0xff), c(3), t(1)}};
    unsigned step = 0;
    bool position = false, declaredOutput = !sm3;
    const auto reg = [](DWORD t) { return t & 0x70001fff; };
    for (std::size_t at = 1; at < count;)
    {
        const DWORD token = words[at];
        const unsigned op = token & 65535;
        if (token == 65535)
            return at + 1 == count && step == 8 && position && declaredOutput;
        const unsigned n = op == 65534 ? (token >> 16) & 32767 : (token >> 24) & 15;
        if (n >= count - at)
            return false;
        const DWORD* a = words + at + 1;
        if (op == 65534)
        {
            at += n + 1;
            continue;
        }
        if (token & 0xf0000000)
            return false;
        if (op == 31)
        {
            if (n != 2 || step)
                return false;
            if (a[0] == 0x80000000 && a[1] == 0x900f0000)
                position = true;
            if (sm3 && a[0] == 0x80000000 && a[1] == output)
                declaredOutput = true;
        }
        else if (op == 81)
        {
            // Shader-local definitions override the device constants.
            if (n != 5 || reg(a[0]) < 0x20000009 || reg(a[0]) > 0x200000ff)
                return false;
        }
        else if (op == 0)
        {
            if (n)
                return false;
        }
        else if (step < 8)
        {
            const auto& want = sequence[step];
            const unsigned size = want[0] == 4 ? 4 : 3;
            if (op != want[0] || n != size || a[0] != want[1])
                return false;
            const bool swapped = step == 0 && a[1] == want[3] && a[2] == want[2];
            for (unsigned i = 1; i < n; ++i)
                if (!swapped && a[i] != want[i + 1])
                    return false;
            ++step;
        }
        else
        {
            // Only ordinary arithmetic after projection; no branches, calls,
            // relative outputs or a later write to the position output.
            unsigned arity = 0;
            switch (op)
            {
            case 1:
            case 6:
            case 7:
            case 14:
            case 15:
            case 16:
            case 19:
            case 35:
            case 36:
            case 46:
                arity = 2;
                break;
            case 2:
            case 3:
            case 5:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 17:
            case 32:
            case 33:
                arity = 3;
                break;
            case 4:
            case 18:
                arity = 4;
                break;
            case 37:
                arity = sm3 ? 2 : 4;
                break;
            default:
                return false;
            }
            if (n != arity || (a[0] & 0x2000) || reg(a[0]) == reg(output))
                return false;
        }
        at += n + 1;
    }
    return false;
}

// Convert an enclosing raw-vertex box using that proven program and this draw's
// constants. The projection must match the captured main camera (either view-Z
// convention); placement-local and already-world vertices then share one path.
bool LiquidWorldBounds(const LiquidDrawCamera& frame, const float* raw, const float (&constants)[9][4], float* result)
{
    if (!raw || !result)
        return false;
    float projection[16], view[16], inverse[16];
    if (!math::ClientToDeviceProjection(frame.glProjection, projection))
        return false;
    const float sign = constants[2][3] < 0 ? -1.f : 1.f;
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c)
        {
            const float actual = constants[r][c], expected = projection[r * 4 + c] * (r == 2 ? sign : 1);
            if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-4f * std::max(1.f, std::abs(expected)))
                return false;
            if (!std::isfinite(constants[5 + r][c]))
                return false;
        }
    if (constants[5][3] != 0 || constants[6][3] != 0 || constants[7][3] != 0 || constants[8][3] != 1)
        return false;
    std::copy(frame.cameraRelativeView, frame.cameraRelativeView + 16, view);
    view[12] = view[13] = view[14] = 0;
    if (!math::Invert4x4(view, inverse))
        return false;
    for (unsigned c = 0; c < 3; ++c)
        if (!std::isfinite(raw[c]) || !std::isfinite(raw[c + 3]) || raw[c] > raw[c + 3] ||
            !std::isfinite(frame.cameraPosition[c]))
            return false;
    for (unsigned corner = 0; corner < 8; ++corner)
    {
        double position[3] = {}, error[3] = {};
        for (unsigned c = 0; c < 3; ++c)
        {
            double magnitude = std::abs(constants[8][c]);
            position[c] = constants[8][c];
            for (unsigned r = 0; r < 3; ++r)
            {
                const double term = double(raw[r + ((corner & (1u << r)) ? 3 : 0)]) * constants[5 + r][c];
                position[c] += term;
                magnitude += std::abs(term);
            }
            error[c] = magnitude * 1e-6; // Covers the short float MUL/MAD chain and reassociation.
        }
        position[2] *= sign;
        for (unsigned c = 0; c < 3; ++c)
        {
            double world = frame.cameraPosition[c], pad = .02;
            for (unsigned r = 0; r < 3; ++r)
            {
                world += position[r] * inverse[r * 4 + c];
                pad += error[r] * std::abs(inverse[r * 4 + c]);
            }
            if (!std::isfinite(world) || !std::isfinite(pad) ||
                std::abs(world) + pad > std::numeric_limits<float>::max())
                return false;
            result[c] = corner ? std::min(result[c], float(world - pad)) : float(world - pad);
            result[c + 3] = corner ? std::max(result[c + 3], float(world + pad)) : float(world + pad);
        }
    }
    return true;
}

LiquidDrawTransform::~LiquidDrawTransform() { Clear(); }
void LiquidDrawTransform::Clear()
{
    for (auto& shader : m_shaders)
        shader.value->Release();
    m_shaders.clear();
}
bool LiquidDrawTransform::Supported(IDirect3DDevice9* device)
{
    IDirect3DVertexShader9* shader = nullptr;
    if (!device || FAILED(device->GetVertexShader(&shader)) || !shader)
        return false;
    bool found = false, affine = false;
    for (const auto& entry : m_shaders)
        if (entry.value == shader)
        {
            found = true;
            affine = entry.affine;
            break;
        }
    if (!found && m_shaders.size() < 64)
    {
        UINT bytes = 0;
        if (SUCCEEDED(shader->GetFunction(nullptr, &bytes)) && bytes >= 8 && bytes <= 65536 && !(bytes % 4))
        {
            std::vector<DWORD> words(bytes / 4);
            affine =
                SUCCEEDED(shader->GetFunction(words.data(), &bytes)) && LiquidAffinePosition(words.data(), bytes / 4);
        }
        shader->AddRef();
        m_shaders.push_back({shader, affine});
    }
    shader->Release();
    if (!affine)
        return false;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    if (FAILED(device->GetVertexDeclaration(&declaration)) || !declaration)
        return false;
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH + 1];
    UINT count = MAXD3DDECLLENGTH + 1;
    const bool read = SUCCEEDED(declaration->GetDeclaration(elements, &count));
    declaration->Release();
    if (!read)
        return false;
    for (unsigned i = 0; i < count; ++i)
    {
        const auto& e = elements[i];
        if (e.Usage == D3DDECLUSAGE_POSITION && e.UsageIndex == 0)
            return e.Stream == 0 && e.Offset == 0 && e.Method == D3DDECLMETHOD_DEFAULT &&
                   (e.Type == D3DDECLTYPE_FLOAT3 || e.Type == D3DDECLTYPE_FLOAT4);
    }
    return false;
}
bool LiquidDrawTransform::Bounds(IDirect3DDevice9* device, const LiquidDrawCamera& frame, const float* raw,
                                 float* output)
{
    float constants[9][4];
    return raw && Supported(device) && SUCCEEDED(device->GetVertexShaderConstantF(0, &constants[0][0], 9)) &&
           LiquidWorldBounds(frame, raw, constants, output);
}
} // namespace water
