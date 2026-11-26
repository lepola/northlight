// Reflections: resolving each mirrored view into the reflection buffers, filling their gaps, and capturing a host pass to reflect.
#include "render/renderer_internal.h"

using namespace render_detail;

void Renderer::ReleaseFillChain(FillChain& chain)
{
    for (int level = 0; level < FillChain::kLevels; ++level)
        for (int i = 0; i < 2; ++i)
        {
            SafeRelease(chain.pull[level][i]);
            SafeRelease(chain.push[level][i]);
        }
}

namespace
{
bool SupportsCompactFill(IDirect3DDevice9* dev)
{
    D3DCAPS9 caps = {};
    D3DDISPLAYMODE mode = {};
    IDirect3D9* api = nullptr;
    if (FAILED(dev->GetDeviceCaps(&caps)) || !(caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS) ||
        FAILED(dev->GetDirect3D(&api)) || !api)
        return false;
    const bool supported = SUCCEEDED(api->GetAdapterDisplayMode(caps.AdapterOrdinal, &mode)) &&
        SUCCEEDED(api->CheckDeviceFormat(caps.AdapterOrdinal, caps.DeviceType, mode.Format,
                                        D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, D3DFMT_G16R16F));
    api->Release();
    return supported;
}
}

bool Renderer::EnsureFillChain(IDirect3DDevice9* dev, FillChain& chain, UINT width, UINT height)
{
    if (chain.pull[0][0] && chain.widths[0] == width && chain.heights[0] == height)
        return true;
    ReleaseFillChain(chain);
    for (int level = 0; level < FillChain::kLevels; ++level)
    {
        const UINT scale = 1u << level;
        chain.widths[level] = std::max((width + scale - 1) / scale, 1u);
        chain.heights[level] = std::max((height + scale - 1) / scale, 1u);
    }
    auto allocate = [&](D3DFORMAT hitFormat) {
        for (int level = 0; level < FillChain::kLevels; ++level)
        {
            for (int i = 0; i < 2; ++i)
            {
                const D3DFORMAT format = i == 0 ? D3DFMT_A16B16G16R16F : hitFormat;
                const auto create = [&](IDirect3DTexture9** out) {
                    return SUCCEEDED(dev->CreateTexture(chain.widths[level], chain.heights[level], 1,
                                                        D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, out, nullptr));
                };
                if (!create(&chain.pull[level][i]) || (level + 1 < FillChain::kLevels && !create(&chain.push[level][i])))
                {
                    ReleaseFillChain(chain);
                    return false;
                }
            }
        }
        return true;
    };
    D3DFORMAT hitFormat = D3DFMT_G16R16F;
    if (!SupportsCompactFill(dev) || !allocate(hitFormat))
    {
        hitFormat = D3DFMT_A16B16G16R16F;
        if (!allocate(hitFormat))
            return false;
    }
    WATER_LOG_INFO("refraction fill %ux%u, %s metadata", width, height,
                   hitFormat == D3DFMT_G16R16F ? "RG16F" : "RGBA16F");
    return true;
}

// Fills in what the chain's finest pull level does not know: pulled up level by level (each averaging what is known
// of the level below, fill_pull_ps) and pushed back down (each taking what it does not know from the level above,
// fill_push_ps), into the finest push level. Leaves the render targets, viewport, pixel shader, stages 0
// to 3 and the first constant changed.
bool Renderer::FillIn(IDirect3DDevice9* dev, FillChain& chain)
{
    constexpr int kLevels = FillChain::kLevels;
    auto levelViewport = [&](int level) {
        const D3DVIEWPORT9 vp = {0, 0, chain.widths[level], chain.heights[level], 0.0f, 1.0f};
        return SUCCEEDED(dev->SetViewport(&vp));
    };
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF)))return false;
    if(FAILED(BindPixelShader(dev, m_fillPullPs)))return false;
    for (int level = 1; level < kLevels; ++level)
    {
        if(!SetPairTargets(dev, chain.pull[level]))return false;
        if(!levelViewport(level))return false;
        const float finer[4] = {1.0f / chain.widths[level - 1], 1.0f / chain.heights[level - 1],
                                static_cast<float>(chain.widths[level - 1]),
                                static_cast<float>(chain.heights[level - 1])};
        if(FAILED(dev->SetPixelShaderConstantF(0, finer, 1)))return false;
        if(!BindPointTexture(dev, 0, chain.pull[level - 1][0]))return false;
        if(!BindPointTexture(dev, 1, chain.pull[level - 1][1]))return false;
        if(FAILED(DrawScreenTriangle(dev)))return false;
    }
    if(FAILED(BindPixelShader(dev, m_fillPushPs)))return false;
    for (int level = kLevels - 2; level >= 0; --level)
    {
        IDirect3DTexture9* const(&coarser)[2] = level + 1 == kLevels - 1 ? chain.pull[level + 1] : chain.push[level + 1];
        if(!SetPairTargets(dev, chain.push[level]))return false;
        if(!levelViewport(level))return false;
        const float here[4] = {1.0f / chain.widths[level], 1.0f / chain.heights[level], level == 0 ? 1.0f : 0.0f, 0.0f};
        if(FAILED(dev->SetPixelShaderConstantF(0, here, 1)))return false;
        if(!BindPointTexture(dev, 0, chain.pull[level][0]))return false;
        if(!BindPointTexture(dev, 1, chain.pull[level][1]))return false;
        if(!BindLinearTexture(dev, 2, coarser[0]))return false;
        if(!BindLinearTexture(dev, 3, coarser[1]))return false;
        if(FAILED(DrawScreenTriangle(dev)))return false;
    }
    for (DWORD stage = 0; stage < 4; ++stage)
        if(FAILED(dev->SetTexture(stage, nullptr)))return false;
    if(FAILED(dev->SetRenderTarget(1, nullptr)))return false;
    return true;
}

// The water's reflections at the selected resolve resolution: the ray each reflects traced (reflection_ps), for the composite to
// read. Leaves the render targets, viewport, pixel shader and the first constants changed.
bool Renderer::DrawReflections(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in, const Config& cfg,
                               const float (&frame)[9][4], const float (&constants)[kWaterConstants][4], bool slime, bool* hasIndoorWater)
{
    // Only mirrored geometry reflects; without geometry each liquid shows the sky.
    if (!cfg.reflectionScenery) return false;
    D3DSURFACE_DESC full = {};
    if (!m_snapshot || FAILED(m_snapshot->GetLevelDesc(0, &full)))
        return false;
    const auto layout = reflection::ResolveLayout(full.Width,full.Height,in.viewport,cfg.reflectionResolution);
    const float pixelStep[4]={float(full.Width)/layout.width-1.f,float(full.Height)/layout.height-1.f,float(layout.width),float(layout.height)};
    if(FAILED(dev->SetPixelShaderConstantF(79,pixelStep,1)))return false;
    const UINT width = layout.width;
    const UINT height = layout.height;
    D3DSURFACE_DESC have = {};
    if (m_reflections[0] && (FAILED(m_reflections[0]->GetLevelDesc(0, &have)) || have.Width != width ||
                             have.Height != height))
    {
        SafeRelease(m_reflections[0]);
        SafeRelease(m_reflections[1]);
    }
    for (IDirect3DTexture9*& target : m_reflections)
        if (!target && FAILED(dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                                                 D3DPOOL_DEFAULT, &target, nullptr)))
            return Skip("reflection buffers could not be created");
    // The rays, cleared to 0 (no water) first: only water is drawn.
    if(!SetPairTargets(dev, m_reflections))return false;
    const D3DVIEWPORT9 whole = {0, 0, width, height, 0.0f, 1.0f};
    dev->SetViewport(&whole);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
    dev->SetViewport(&layout.viewport);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants);
    if(!BindWaterTextures(dev, depthTexture, in, cfg, slime))return false;
    if(!slime)BindSurfaceDetail(dev);
    auto* views=m_sceneryViews;
    const unsigned count=views?views->ViewCount():1;
    // One interval for the complete set of source views and their resolves.
    // Per-view intervals overflow the bounded query pool in multi-height scenes.
    GpuScope reflectionViews(m_timer,GpuTimer::WaterReflections);
    bool drawn=false;
    for(unsigned index=0;index<count;++index){
        RECT area{};float continuation[4]={1,0,0,0},coverageFade[4]={};
        if(views){
            if(!views->ViewArea(in,index,area))continue;
            water::SceneryImage image;
            const bool ready=views->DrawView(dev,in,index,image);
            if(!ready)return false;
            SetScenery(image.colour,image.depth,image.height,image.projection,
                       image.heights?nullptr:image.planes.data(),image.heights,image.underwater);
            m_sceneryViews=views;
            continuation[3]=image.receiverHeight;
            if(hasIndoorWater && image.hasIndoorWater)*hasIndoorWater=true;
            if(!slime || m_slime.outdoorThisFrame){
                continuation[0]=image.continuation[0];continuation[1]=image.continuation[1];
            }
            if(image.coverageWidth>0){
                const auto& edges=image.coverageEdges;
                const float halfX=(edges[2]-edges[0])*.5f,halfY=(edges[3]-edges[1])*.5f;
                coverageFade[0]=edges[0]+halfX;
                coverageFade[1]=edges[1]+halfY;
                coverageFade[2]=halfX-image.coverageWidth;
                coverageFade[3]=1.f/image.coverageWidth;
                continuation[2]=halfY-halfX; // zero in older square-coverage captures
            }
        }
        if (!m_sceneryColour || !m_sceneryDepth || frame[2][0]<=0 || frame[2][1]<=0)
            return Skip("scenery reflection missing or mismatched frame projection");
        const float mapping[4]={m_sceneryProjection[0]/frame[2][0],m_sceneryProjection[5]/frame[2][1],
            m_sceneryProjection[8]-frame[2][2]*(m_sceneryProjection[0]/frame[2][0]),
            m_sceneryProjection[9]-frame[2][3]*(m_sceneryProjection[5]/frame[2][1])};
        for(unsigned i=0;i<4;++i)if(!std::isfinite(mapping[i]))return Skip("invalid independent scenery projection");
        if(mapping[0]<.5f || mapping[0]>1.00001f || mapping[1]<.5f || mapping[1]>1.00001f ||
           std::fabs(mapping[2])>1 || std::fabs(mapping[3])>1)
            return Skip("unsupported independent scenery projection");
        D3DSURFACE_DESC geometry={};
        if (FAILED(m_sceneryDepth->GetLevelDesc(0,&geometry))) return false;
        const float grid[4]={float(geometry.Width),float(geometry.Height),1.0f/geometry.Width,1.0f/geometry.Height};
        const float plane[4]={m_sceneryHeight,m_sceneryProjection[10],m_sceneryProjection[14],m_sceneryHeightsActive?3.0f:(m_sceneryPlanesActive?2.0f:1.0f)};
        dev->SetPixelShaderConstantF(65,grid,1);dev->SetPixelShaderConstantF(66,plane,1);
        const float alignment[4]={cfg.reflectionSceneryAlign ? 1.0f:0.0f,cfg.reflectionSceneryShoreEdge ? 1.0f:0.0f,views?1.f:0.f,float(index+1)};
        dev->SetPixelShaderConstantF(67,alignment,1);
        dev->SetPixelShaderConstantF(68,m_sceneryPlanes,8);
        dev->SetPixelShaderConstantF(76,mapping,1);
        dev->SetPixelShaderConstantF(77,continuation,1);
        dev->SetPixelShaderConstantF(78,coverageFade,1);
        if (FAILED(BindPixelShader(dev, slime ? m_slimeReflectionPs : m_reflectionSceneryPs))) return false;
        BindPointTexture(dev,12,m_sceneryColour);BindPointTexture(dev,13,m_sceneryDepth);
        if(!BindPointTexture(dev,14,views?views->ReceiverMask():nullptr))return false;
        // Keep the first visible view as an exact replayable pass. The later
        // water composite captures the combined reflection. Repeating all shared
        // full-resolution inputs/outputs per tiny receiver makes captures unbounded.
        const std::string capture=m_reflectionCaptureDirectory.empty()? "":m_reflectionCaptureDirectory+
            "/rays";
        const bool captured=!drawn && !capture.empty() && reflection::CapturePassInputs(dev,capture,m_snapshotPs);
        ++m_work[0];
        if(views)area=reflection::ScaleArea(area,full.Width,full.Height,width,height);
        const bool resolved=SUCCEEDED(DrawScreenArea(dev,views?&area:nullptr));
        if(captured && resolved)reflection::CapturePassOutputs(dev,capture);
        if(!resolved)return false;
        drawn=true;
    }
    dev->SetTexture(12,nullptr);dev->SetTexture(13,nullptr);dev->SetTexture(14,nullptr);dev->SetRenderTarget(1,nullptr);
    return drawn;
}

// Host diagnostics share the active water packet and its completion checks.
bool Renderer::CaptureHostPass(IDirect3DDevice9* dev,const char* name,bool after)
{
    if(m_reflectionCaptureDirectory.empty()||!name)return false;
    size_t length=0;
    for(;name[length]&&length<48;++length){
        const char c=name[length];
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;
    }
    if(!length||length==48)return false;
    const std::string pass="host-"+std::string(name);
    const auto found=std::find(m_hostCapturePasses.begin(),m_hostCapturePasses.end(),pass);
    bool ok=false;
    if(after){
        if(found==m_hostCapturePasses.end())return false;
        ok=reflection::CapturePassOutputs(dev,m_reflectionCaptureDirectory+"/"+pass,"output",ActiveCaptureBurst());
    }else{
        if(found!=m_hostCapturePasses.end()||m_hostCapturePasses.size()>=8)return false;
        m_hostCapturePasses.push_back(pass);
        ok=reflection::CapturePassInputs(dev,m_reflectionCaptureDirectory+"/"+pass,m_snapshotPs,ActiveCaptureBurst());
    }
    m_hostCaptureFailed|=!ok;
    return ok;
}

void Renderer::CaptureFinished(IDirect3DDevice9* dev)
{
    // The bounded two-frame experiment queues replayable passes only; a final
    // image has no pass header and must not enter that queue's completion set.
    if(m_reflectionCaptureDirectory.empty()||m_reflectionCaptureBurst)return;
    const std::string directory=m_reflectionCaptureDirectory+"/finished";
    if(!CreateDirectoryA(directory.c_str(),nullptr)){m_hostCaptureFailed=true;return;}
    m_hostCaptureFailed|=!reflection::CapturePassOutputs(dev,directory,"output",ActiveCaptureBurst());
    // This is the finished host world, before UI, paired with this packet's
    // water and host passes. SaveRenderTarget preserves any previous evidence.
    m_hostCaptureFailed|=!SaveRenderTarget(dev,directory+"/view.bmp");
}

void Renderer::EndReflectionCapture(bool aborted)
{
    auto mark = [&](const std::string& directory, bool complete) {
        complete=complete&&!m_hostCaptureFailed;
        for(const auto& pass:m_hostCapturePasses)
            for(const char* marker:{"inputs.ok","outputs.ok"})
                complete=GetFileAttributesA((directory+"/"+pass+"/"+marker).c_str())!=INVALID_FILE_ATTRIBUTES&&complete;
        for (const char* pass : {"rays", "composite"})
        {
            for (const char* marker : {"inputs.ok","outputs.ok"})
                complete = GetFileAttributesA((directory+"/"+pass+"/"+marker).c_str()) != INVALID_FILE_ATTRIBUTES && complete;
        }
        FILE* file = std::fopen((directory+(complete ? "/frame.ok" : "/aborted.txt")).c_str(),"wb");
        if (file) { std::fprintf(file,"%s\n",complete ? "complete" : "incomplete or reset; do not replay as a sequence"); std::fclose(file); }
        WATER_LOG_INFO("reflection input frame %s: %s",directory.c_str(),complete ? "complete" : "incomplete");
        return complete;
    };
    if (m_reflectionCaptureBurst)
    {
        aborted = aborted || m_captureBurst.Failed();
        const bool capturedThisFrame = !m_reflectionCaptureDirectory.empty();
        if (!m_reflectionCaptureDirectory.empty())
        {
            m_burstFrames.push_back(m_reflectionCaptureDirectory);
            --m_reflectionCaptureRemaining; ++m_reflectionCaptureIndex;
            m_reflectionCaptureDirectory.clear();
        }
        if (!aborted && m_reflectionCaptureRemaining > 0)
        {
            if (capturedThisFrame) return;
            // A dry/empty frame between the two samples is a dropped frame,
            // not a consecutive sequence. Never wait across it and claim zero drops.
            aborted = true;
        }
        bool complete = !aborted && m_captureBurst.Flush();
        // Validate both whole frames before marking either complete. A pass
        // omitted by a failed/empty world frame must abort the entire pair.
        for (const auto& directory : m_burstFrames)
            for (const char* pass : {"rays","composite"})
            {
                    for (const char* marker : {"inputs.ok","outputs.ok"})
                    complete = GetFileAttributesA((directory+"/"+pass+"/"+marker).c_str()) != INVALID_FILE_ATTRIBUTES && complete;
            }
        for (const auto& directory : m_burstFrames)
        {
            mark(directory,complete);
            FILE* report = std::fopen((directory+"/burst.json").c_str(),"wb");
            if (report)
            {
                std::fprintf(report,"{\"complete\":%s,\"requested_frames\":2,\"attempted_frames\":%u,\"dropped_frames\":%u,"
                    "\"budget_bytes\":%llu,\"staged_bytes\":%llu,\"submit_cpu_ms\":%.6f,\"post_sequence_readback_ms\":%.6f}\n",
                    complete ? "true" : "false",static_cast<unsigned>(m_burstFrames.size()),
                    complete ? 0u : 2u,static_cast<unsigned long long>(reflection::CaptureBurst::kBudget),
                    static_cast<unsigned long long>(m_captureBurst.Bytes()),m_captureBurst.SubmitMs(),m_captureBurst.FlushMs());
                std::fclose(report);
            }
        }
        m_captureBurst.Discard(); m_burstFrames.clear(); m_reflectionCaptureBurst = false;
        if (!complete) m_reflectionCaptureRemaining = 0;
        return;
    }
    if (m_reflectionCaptureDirectory.empty()) return;
    const bool complete = mark(m_reflectionCaptureDirectory,!aborted);
    --m_reflectionCaptureRemaining; ++m_reflectionCaptureIndex;
    if (!complete) m_reflectionCaptureRemaining = 0;
    m_reflectionCaptureDirectory.clear();
}

void Renderer::BeginReflectionCapture(const Config& cfg, const FrameInputs& in)
{
    if (cfg.reflectionCapture <= 0) { EndReflectionCapture(true); m_reflectionCaptureRemaining = 0; return; }
    if (cfg.reflectionCapture != m_reflectionCaptureRequest)
    {
        m_reflectionCaptureRequest = cfg.reflectionCapture;
        m_reflectionCaptureRemaining = cfg.reflectionCaptureFrames;
        m_reflectionCaptureIndex = 0;
    }
    if (m_reflectionCaptureRemaining <= 0 || !m_reflectionCaptureDirectory.empty()) return;
    const std::string directory = OutputDirectory() + "reflection-" + std::to_string(cfg.reflectionCapture) +
                                  "-" + std::to_string(m_reflectionCaptureIndex);
    if (!CreateDirectoryA(directory.c_str(), nullptr))
    {
        WATER_LOG_ERROR("reflection capture directory already exists or cannot be created: %s", directory.c_str());
        m_reflectionCaptureRemaining = 0;
        return;
    }
    m_reflectionCaptureDirectory = directory;
    m_hostCapturePasses.clear();m_hostCaptureFailed=false;
    if (cfg.reflectionCaptureBurst || m_reflectionCaptureBurst)
    {
        // The named experiment captures baseline inputs, not the expensive
        // candidate intermediates or arbitrary-length recordings.
        if (!cfg.reflectionCaptureBurst || cfg.reflectionCaptureFrames != 2 || cfg.reflectionScenery)
        {
            WATER_LOG_ERROR("two-frame burst requires baseline mode, two frames and diagnostics off");
            EndReflectionCapture(true); m_reflectionCaptureRemaining = 0; return;
        }
        if (m_reflectionCaptureIndex == 0) { m_captureBurst.Discard(); m_reflectionCaptureBurst = true; }
    }
    CopyFileA((OutputDirectory() + "water.ini").c_str(), (directory + "/water.ini").c_str(), TRUE);
    FILE* manifest = std::fopen((directory + "/manifest.json").c_str(), "wb");
    if (manifest)
    {
        std::fprintf(manifest, "{\"version\":2,\"frame\":%u,\"world_frame\":%u,\"sequence_index\":%d,\"capture_mode\":\"%s\",\"camera\":[%.9g,%.9g,%.9g],"
                              "\"clock_seconds\":%.9g,\"normal_inputs\":\"final surface detail; downstream reflection only\","
                              "\"depth\":\"raw R32F copied from INTZ; inverse-depth mapping in uniforms\","
                              "\"color\":\"native format, sampler sRGB state retained\"}\n",
                     m_frame, m_worldFrame, m_reflectionCaptureIndex, m_reflectionCaptureBurst ? "two-frame GPU staging" : "synchronous", in.cameraPosition[0], in.cameraPosition[1], in.cameraPosition[2],
                     LiquidSeconds(cfg));
        std::fclose(manifest);
    }
    WATER_LOG_INFO("reflection input capture %s (readback cost excluded from performance comparisons)", directory.c_str());
}
