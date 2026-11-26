// Diagnostics: the debug overlay, saved frames, pass timings and the depth probe.
#include "render/renderer_internal.h"

using namespace render_detail;

bool Renderer::EnsureProbeTargets(IDirect3DDevice9* dev)
{
    if (m_probeTarget)
        return true;
    if (SUCCEEDED(dev->CreateTexture(1, 1, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT,
                                     &m_probeTarget, nullptr)) &&
        SUCCEEDED(dev->CreateOffscreenPlainSurface(1, 1, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &m_probeReadback,
                                                   nullptr)))
        return true;
    SafeRelease(m_probeTarget);
    SafeRelease(m_probeReadback);
    return false;
}

void Renderer::DrawOverlay(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                           const FrameInputs& in, const Config& cfg, const float (&constants)[9][4])
{
    dev->SetRenderTarget(0, target);
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
    dev->SetPixelShaderConstantF(0, &constants[0][0], kFrameConstants);
    if (FAILED(BindPixelShader(dev, m_overlayPs))) return;
    BindPointTexture(dev, 0, depthTexture);
    BindPointTexture(dev, 1, m_snapshotValid ? m_snapshot : nullptr);
    DrawScreenTriangle(dev);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
}

bool Renderer::SaveRenderTarget(IDirect3DDevice9* dev, const std::string& path)
{
    if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        WATER_LOG_INFO("capture already exists; preserved %s", path.c_str());
        return false;
    }
    IDirect3DSurface9* target = nullptr;
    IDirect3DSurface9* copy = nullptr;
    D3DSURFACE_DESC desc = {};
    D3DLOCKED_RECT locked = {};
    bool ok = SUCCEEDED(dev->GetRenderTarget(0, &target)) && SUCCEEDED(target->GetDesc(&desc)) &&
              (desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_A8R8G8B8) &&
              SUCCEEDED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM,
                                                         &copy, nullptr)) &&
              SUCCEEDED(dev->GetRenderTargetData(target, copy)) &&
              SUCCEEDED(copy->LockRect(&locked, nullptr, D3DLOCK_READONLY));
    // Restoring a positive CaptureFrame can re-trigger an older request. Both
    // this exclusive open and the early check preserve evidence across restarts.
    const int fd = ok ? _open(path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE) : -1;
    FILE* file = fd >= 0 ? _fdopen(fd, "wb") : nullptr;
    if (fd >= 0 && !file) _close(fd);
    bool written = false;
    if (file)
    {
        BITMAPINFOHEADER info = {};
        info.biSize = sizeof(info);
        info.biWidth = static_cast<LONG>(desc.Width);
        info.biHeight = -static_cast<LONG>(desc.Height);
        info.biPlanes = 1;
        info.biBitCount = 32;
        BITMAPFILEHEADER header = {};
        header.bfType = 0x4D42;
        header.bfOffBits = sizeof(header) + sizeof(info);
        header.bfSize = header.bfOffBits + desc.Width * desc.Height * 4;
        written = std::fwrite(&header, sizeof(header), 1, file) == 1 &&
                  std::fwrite(&info, sizeof(info), 1, file) == 1;
        for (UINT row = 0; row < desc.Height; ++row)
            written = std::fwrite(static_cast<const char*>(locked.pBits) + row * locked.Pitch, 4, desc.Width, file) == desc.Width && written;
        written = std::fclose(file) == 0 && written;
    }
    if (ok)
        copy->UnlockRect();
    SafeRelease(copy);
    SafeRelease(target);
    if (written)
        WATER_LOG_INFO("captured the render target to %s", path.c_str());
    else
        WATER_LOG_ERROR("capture to %s failed (render target format %d)", path.c_str(), static_cast<int>(desc.Format));
    return written;
}

void Renderer::TimeBegin(int pass)
{
    if (m_timer)
        m_timer->Begin(static_cast<GpuTimer::Pass>(pass));
}

void Renderer::TimeEnd(int pass)
{
    if (m_timer)
        m_timer->End(static_cast<GpuTimer::Pass>(pass));
}

// Saves this frame as frame-<request>.bmp; or, asked for several, this and the frames after it as
// frame-<request>-<i>.bmp (to see what changes from one frame to the next).
void Renderer::Capture(IDirect3DDevice9* dev, int request, int frames)
{
    m_capturedRequest = request;
    if (m_captureLeft == 0)
    {
        m_captureLeft = frames;
        m_captureSaved = 0;
    }
    const std::string name = frames > 1 ? std::to_string(request) + "-" + std::to_string(m_captureSaved)
                                        : std::to_string(request);
    SaveRenderTarget(dev, OutputDirectory() + "frame-" + name + ".bmp");
    ++m_captureSaved;
    --m_captureLeft;
}

void Renderer::Probe(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in,
                     const float (&constants)[9][4])
{
    m_lastProbe = Ticks();
    if (!EnsureProbeTargets(dev))
        return;
    if(!SetTarget(dev, m_probeTarget))return;
    D3DVIEWPORT9 vp = {0, 0, 1, 1, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    dev->SetPixelShaderConstantF(0, &constants[0][0], kFrameConstants);
    if (FAILED(BindPixelShader(dev, m_probePs))) return;
    BindPointTexture(dev, 0, depthTexture);
    BindPointTexture(dev, 1, m_snapshotValid ? m_snapshot : nullptr);
    DrawScreenTriangle(dev);

    IDirect3DSurface9* surface = nullptr;
    D3DLOCKED_RECT locked = {};
    bool read = SUCCEEDED(m_probeTarget->GetSurfaceLevel(0, &surface)) &&
                SUCCEEDED(dev->GetRenderTargetData(surface, m_probeReadback)) &&
                SUCCEEDED(m_probeReadback->LockRect(&locked, nullptr, D3DLOCK_READONLY));
    SafeRelease(surface);
    if (!read)
    {
        WATER_LOG_INFO("probe: readback failed");
        return;
    }
    ProbeSample sample;
    std::memcpy(&sample, locked.pBits, sizeof(sample));
    m_probeReadback->UnlockRect();
    const float dx = sample.world[0] - in.cameraPosition[0];
    const float dy = sample.world[1] - in.cameraPosition[1];
    const float dz = sample.world[2] - in.cameraPosition[2];
    if (sample.waterThickness < -1.5f)
        WATER_LOG_INFO("probe: screen centre is sky or beyond the far clip; camera (%.2f %.2f %.2f)",
                       in.cameraPosition[0], in.cameraPosition[1], in.cameraPosition[2]);
    else
        WATER_LOG_INFO("probe: screen centre at world (%.2f %.2f %.2f), %.1f yd from the camera; water %s%.2f yd",
                       sample.world[0], sample.world[1], sample.world[2], std::sqrt(dx * dx + dy * dy + dz * dz),
                       sample.waterThickness < 0.0f ? "none " : "depth ",
                       sample.waterThickness < 0.0f ? 0.0f : sample.waterThickness);
}
