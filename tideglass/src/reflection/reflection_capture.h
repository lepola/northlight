#pragma once
#include <d3d9.h>
#include "../../shaders/water_registers.hlsli"
#include <string>
#include <vector>
#include <cstdint>

namespace reflection
{
// Pass packet v2 stores exactly the bound inputs to a fullscreen production pass.
// Raw depth is sampled into R32F; all other formats are retained, row padding removed.
// Synchronous capture retains no resources. The explicit two-frame baseline burst
// keeps GPU copies until both frames end; disabled capture never calls these functions.
constexpr UINT kPacketVersion = 2;
constexpr UINT kMipTextureVersion = 3; // TextureHeader, level count, native rows for each explicitly stored mip
constexpr UINT kPassVersion = 9; // adds current surface-visibility availability c90
constexpr UINT kUniformRows = WATER_SLOT_cSurfaceVisibility + 1;
static_assert(kUniformRows == 91, "Changing the v9 wire layout requires a new version and a legacy reader");
struct TextureHeader
{
    UINT version, format, width, height, rowBytes;
};
struct PassHeader
{
    UINT version, width, height, outputs, boundStages;
    D3DVIEWPORT9 viewport;
    DWORD colorWrite[2];
    DWORD dither;
    float uniforms[kUniformRows][4];
    DWORD samplers[16][14];
};
// Bounded offline diagnosis of the first point projection. CaptureOnce only
// reads CPU-visible state and retains references. Save occurs after the probe;
// its three replay-loaded input textures must remain immutable until then.
// This is not a snapshot mechanism for mutable live inputs or render targets.
class ProjectionAudit
{
public:
    ~ProjectionAudit();
    ProjectionAudit() = default;
    ProjectionAudit(const ProjectionAudit&) = delete;
    ProjectionAudit& operator=(const ProjectionAudit&) = delete;
    void CaptureOnce(IDirect3DDevice9* dev);
    bool Save(IDirect3DDevice9* dev, const std::string& directory);
private:
    bool m_captured = false, m_ok = false;
    D3DVIEWPORT9 m_viewport = {};
    float m_uniforms[9][4] = {};
    DWORD m_states[32][2] = {}, m_samplers[3][14] = {};
    D3DSURFACE_DESC m_inputs[3] = {}, m_targets[2] = {}, m_depth = {};
    D3DVERTEXBUFFER_DESC m_stream = {};
    UINT m_offset = 0, m_stride = 0, m_frequency = 0, m_maxPrimitiveCount = 0, m_declCount = 64;
    D3DVERTEXELEMENT9 m_elements[64] = {};
    IDirect3DTexture9* m_textures[3] = {};
    IDirect3DVertexShader9* m_vs = nullptr;
    IDirect3DPixelShader9* m_ps = nullptr;
};
class CaptureBurst
{
public:
    ~CaptureBurst() { Discard(); }
    explicit CaptureBurst(uint64_t budget = kBudget) : m_budget(budget < kBudget ? budget : kBudget) {}
    CaptureBurst(const CaptureBurst&) = delete;
    CaptureBurst& operator=(const CaptureBurst&) = delete;
    static constexpr uint64_t kBudget = 1280ull * 1024 * 1024;
    bool Queue(IDirect3DDevice9* dev, IDirect3DTexture9* texture, const std::string& path);
    bool QueueSurface(IDirect3DDevice9* dev, IDirect3DSurface9* surface, const std::string& path);
    bool Flush(); // all readback/file writes happen here, after the second frame
    void Discard();
    uint64_t Bytes() const { return m_bytes; }
    double SubmitMs() const { return m_submitMs; }
    double FlushMs() const { return m_flushMs; }
    bool Failed() const { return m_failed; }
private:
    struct Entry { std::string path; IDirect3DTexture9* texture; };
    bool Reserve(uint64_t bytes);
    std::vector<Entry> m_entries;
    uint64_t m_bytes = 0;
    const uint64_t m_budget;
    double m_submitMs = 0, m_flushMs = 0;
    bool m_failed = false;
};
bool ReadPassHeader(const std::string& path, PassHeader& header);
bool SaveTexture(IDirect3DDevice9* dev, IDirect3DTexture9* texture, const std::string& path,
                 IDirect3DPixelShader9* rawDepthShader = nullptr, CaptureBurst* burst = nullptr);
bool CapturePassInputs(IDirect3DDevice9* dev, const std::string& directory,
                       IDirect3DPixelShader9* rawDepthShader, CaptureBurst* burst = nullptr);
bool CapturePassOutputs(IDirect3DDevice9* dev, const std::string& directory, const char* prefix = "output", CaptureBurst* burst = nullptr);
bool LoadTexture(IDirect3DDevice9* dev, const std::string& path, IDirect3DTexture9** texture, D3DPOOL pool = D3DPOOL_MANAGED);
}
