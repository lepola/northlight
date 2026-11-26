#if defined(WATER_COOPERATIVE_BUILD)
#include "host_api.h"
#include "host_diagnostics.h"
#include "host_capture.h"
#include "version.h"
#include "host_environment.h"
#include "host_liquid.h"
#include "client/liquid.h"
#include "client/engine.h"
#include "frame/frame_snapshot.h"
#include "render/render_session.h"
#include "base/log.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <memory>
#include <type_traits>

static_assert(std::is_standard_layout<WaterFrameValues>::value, "host frame is a plain record");
static_assert(sizeof(WaterHostStamp) == 24, "host stamp layout");

namespace
{
std::atomic<unsigned> liveContexts{0}, liveFrames{0};
std::atomic<DWORD> captureThread{0};
template<class F> auto Boundary(F action) -> decltype(action())
{
    try { return action(); }
    catch (...) { return {}; }
}
water::FrameStamp Stamp(WaterHostStamp stamp) { return {stamp.session, stamp.generation, stamp.frame}; }
bool CameraValid(const WaterFrameValues& in)
{
    for (float value : in.cameraRelativeView) if (!std::isfinite(value)) return false;
    for (float value : in.glProjection) if (!std::isfinite(value)) return false;
    for (float value : in.cameraPosition) if (!std::isfinite(value)) return false;
    return in.viewport.Width && in.viewport.Height && std::isfinite(in.viewport.MinZ) &&
           std::isfinite(in.viewport.MaxZ) && in.viewport.MaxZ > in.viewport.MinZ;
}
}

struct WaterHostFrame
{
    std::atomic<unsigned> references{1};
    std::unique_ptr<const water::FrameSnapshot> snapshot;
    uint64_t captureEnd=0,captureTicks=0;unsigned captureThread=0;
};
struct WaterHostContext
{
    const DWORD thread = GetCurrentThreadId();
    IDirect3DDevice9* device;
    ConfigFile config;
    water::RenderSession session;
    WaterHostFrame* frame = nullptr;
    WaterHostFog fog = WaterHostNativeFog;
    float reflectedFogWeight = 0;
    WaterLampSamples lamps{};
    WaterSceneryPoseRequests sceneryPoses{};
    bool poseHost=false;
    LiquidClassifier liquids;
    bool slimePending = false, slimeOutdoors = false, outdoorSlimeSeen = false, waterSeen = false;
    LiquidKind surfaceKind = LiquidKind::Slime;
    RECT surfaceArea = {};
    int surfaceRamp = -1;
    bool surfaceBounded = false;
    unsigned liquidViewLogged = ~0u;
    WaterHostContext(IDirect3DDevice9* d, const char* settings) : device(d)
    {
        config.Load(settings);
        engine::ConfigureVisualTime(config.Get().timeOfDay,config.Get().dayCycleSeconds);
        session.SetOutputDirectory(config.Directory());
    }
    bool Owner() const { return thread == GetCurrentThreadId(); }
};

namespace
{
WaterHostFrame* Publish(const FrameInputs& input)
{
    if (!CameraValid(input)) return nullptr;
    auto snapshot = water::FrameSnapshot::Capture(input);
    if (!snapshot) return nullptr;
    auto result = std::make_unique<WaterHostFrame>();
    result->snapshot = std::move(snapshot);
    ++liveFrames;
    return result.release();
}
void __cdecl Retain(WaterHostFrame* frame) { if (frame) ++frame->references; }
void __cdecl Release(WaterHostFrame* frame)
{
    if (frame && frame->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete frame;
        --liveFrames;
    }
}
WaterHostContext* __cdecl Create(IDirect3DDevice9* device, const char* settings)
{
    return Boundary([&]() -> WaterHostContext* {
        if (!device || !settings || !*settings) return nullptr;
        auto result = std::make_unique<WaterHostContext>(device, settings);
        LogRetain((result->config.Directory() + "water-core.log").c_str());
        // Once per process: later and recreated contexts leave the log as it was.
        static std::atomic<bool> announced{false};
        if (!announced.exchange(true))
            WATER_LOG_INFO("Tideglass " TIDEGLASS_VERSION "; cooperative core started; settings in %s", result->config.Directory().c_str());
        result->session.PrepareUnderwaterShaders(device, result->config.Get());
        ++liveContexts;
        return result.release();
    });
}
int __cdecl End(WaterHostContext* context)
{
    return Boundary([&] {
        if (!context || !context->Owner()) return 0;
        context->session.EndFrame();
        Release(context->frame);
        context->frame = nullptr;
        context->fog = WaterHostNativeFog;
        context->reflectedFogWeight = 0;
        context->lamps={};
        context->slimePending = context->slimeOutdoors = context->outdoorSlimeSeen = context->waterSeen = false;
        return 1;
    });
}
int __cdecl Destroy(WaterHostContext* context)
{
    return Boundary([&] {
        if (!context || !context->Owner()) return 0;
        End(context);
        context->session.ReleaseAll();
        delete context;
        LogRelease();
        --liveContexts;
        return 1;
    });
}
WaterHostFrame* __cdecl CopyFrame(const WaterFrameValues* values, uint32_t size)
{
    return Boundary([&]() -> WaterHostFrame* {
        if (!values || size != sizeof(*values)) return nullptr;
        FrameInputs input = {};
        static_cast<WaterFrameValues&>(input) = *values;
        return Publish(input);
    });
}
WaterHostFrame* __cdecl CaptureClientMapped(const WaterHostCamera* camera,WaterHostTextureMap map,void* mapContext)
{
    const bool measuring=water::FrameDiagnostics::CapturingInputs();
    const auto started=measuring?water::FrameDiagnostics::Now():0;
    // Exclusive input-capture stages, sampled only during a requested profile.
    // These run on the game thread; render-thread scopes cannot attribute them.
    uint64_t marks[7]={started};
    auto mark=[&](unsigned i){if(measuring)marks[i]=water::FrameDiagnostics::Now();};
    return Boundary([&]() -> WaterHostFrame* {
        if (!camera || camera->size != sizeof(*camera) || !engine::IsSupportedClient()) return nullptr;
        DWORD expected = 0;
        const auto thread = GetCurrentThreadId();
        captureThread.compare_exchange_strong(expected, thread);
        if (captureThread.load() != thread) return nullptr;
        engine::UpdateGround(.001, false);
        engine::UpdateVisualTime();
        mark(1);
        engine::CaptureLiquidIdentity(map,mapContext);
        mark(2);
        FrameInputs input = {};
        if (!engine::BuildFrameInputsFromCamera(camera->view, camera->projection, camera->viewport, input)) return nullptr;
        if (camera->nativeFogRangeKnown) {
            if (!std::isfinite(camera->nativeFogStart) || !std::isfinite(camera->nativeFogEnd) ||
                camera->nativeFogEnd <= camera->nativeFogStart) return nullptr;
            input.fogStart = camera->nativeFogStart;
            input.fogEnd = camera->nativeFogEnd;
        }
        mark(3);
        input.moverCount = engine::ReadMovers(input.cameraPosition, input.movers, kMaxMovers,
                                              input.boats, kMaxBoats, input.boatCount, input.reflectionObjects,&input.reflectionObjectCount);
        mark(4);
        std::vector<Splash> splashes(kMaxSplashes);
        input.splashCount = engine::ReadRainSplashes(input.cameraPosition, splashes.data(), kMaxSplashes, input.splashReach);
        input.splashes = splashes.data();
        mark(5);
        auto* result=Publish(input);
        mark(6);
        if(result&&measuring){
            result->captureEnd=marks[6];result->captureTicks=marks[6]-started;result->captureThread=GetCurrentThreadId();
            static DWORD lastLog=0;
            const DWORD now=GetTickCount();
            if(!lastLog||DWORD(now-lastLog)>=2000){
                lastLog=now;LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
                const double ms=1000.0/double(frequency.QuadPart);
                WATER_LOG_INFO("input capture parts ground_time_ms=%.4f liquids_ms=%.4f frame_ms=%.4f movers_ms=%.4f rain_ms=%.4f publish_ms=%.4f movers=%d lights=%u textures=%u",
                    (marks[1]-marks[0])*ms,(marks[2]-marks[1])*ms,(marks[3]-marks[2])*ms,
                    (marks[4]-marks[3])*ms,(marks[5]-marks[4])*ms,(marks[6]-marks[5])*ms,
                    input.moverCount,unsigned(input.nativeLiquids.lights.size()),unsigned(input.nativeLiquids.textures.size()));
            }
        }
        return result;
    });
}
WaterHostFrame* __cdecl CaptureClient(const WaterHostCamera* camera){return CaptureClientMapped(camera,nullptr,nullptr);}
int __cdecl Begin(WaterHostContext* context, WaterHostFrame* frame, WaterHostStamp* out)
{
    return Boundary([&] {
        if (!context || !context->Owner() || !frame || !out) return 0;
        // Retain before ending: a host may reuse the context's current handle.
        Retain(frame);
        std::unique_ptr<WaterHostFrame, decltype(&Release)> held(frame, &Release);
        if (!End(context)) return 0;
        {water::FrameDiagnostics::Scope scope(context->session.Diagnostics(),water::FrameDiagnostics::ConfigReload);context->config.ReloadIfChanged(true);}
        engine::ConfigureVisualTime(context->config.Get().timeOfDay,context->config.Get().dayCycleSeconds);
        context->session.ConfigureDiagnostics(context->config.Get(),0);
        if (!context->config.Get().enable) { context->session.DisableOwnedReflection(); return 0; }
        const auto stamp = context->session.BeginFrame(context->device);
        if (!stamp.session) return 0;
        context->frame = held.release();
        context->session.Diagnostics().Capture(frame->captureEnd,frame->captureTicks,frame->captureThread);
        context->session.ConfigureReflection(context->frame->snapshot->Inputs(), context->config.Get());
        if(context->session.Diagnostics().Bypass(context->config.Get())){End(context);return 0;}
        *out = {stamp.session, stamp.generation, stamp.frame};
        return 1;
    });
}
bool Current(WaterHostContext* context, WaterHostStamp stamp)
{
    if (!context || !context->Owner() || !context->frame) return false;
    const auto current = context->session.Frame();
    return current.session == stamp.session && current.generation == stamp.generation && current.frame == stamp.frame;
}
FrameInputs LiquidInputs(WaterHostContext* context)
{
    auto input = context->frame->snapshot->Inputs();
    input.sceneryPoses=context->poseHost?&context->sceneryPoses:nullptr;
    input.lamps=context->lamps;
    input.reflectedFogWeight = context->fog == WaterHostSpatialFog ? context->reflectedFogWeight : 0;
    input.stockFogPushed = input.stockFogPushed || context->fog == WaterHostFogCoversWater;
    // Some outdoor slime uses a native water type. Keep the immutable captured
    // frame intact; correct only this context's view after observing its draws.
    if (!input.nativeLiquids.known && context->outdoorSlimeSeen && !context->waterSeen && input.cameraInWater) {
        input.cameraInWater = false;
        input.cameraInSlime = true;
    }
    return input;
}
WaterHostLiquidKind __cdecl ClassifyLiquid(WaterHostContext* context, WaterHostStamp stamp, IDirect3DBaseTexture9* texture, const float* bounds, int* ramp)
{
    if (ramp) *ramp = -1;
    return Boundary([&] {
        if (!Current(context, stamp) || !texture || texture->GetType() != D3DRTYPE_TEXTURE)
            return WaterHostLiquidUnknown;
        const auto& names=context->frame->snapshot->Inputs().nativeLiquids;
        const auto kind=context->liquids.Classify(texture,names,bounds,ramp);
        if(context->config.Get().traceLiquids){
            static unsigned previous=~0u;
            const auto raw=reinterpret_cast<uintptr_t>(texture);
            unsigned entries=0,kinds=0;
            for(const auto& t:names.textures)if(t.identity==raw){++entries;kinds|=1u<<unsigned(t.kind);}
            const unsigned state=unsigned(kind)|(kinds<<4)|(unsigned(names.water[0])<<8)|(unsigned(names.water[1])<<12);
            if(state!=previous){
                WATER_LOG_INFO("host liquid texture kind=%s listed=%u kinds=%x ocean=%s (%.3f %.3f %.3f) river=%s (%.3f %.3f %.3f) textures=%u",LiquidKindName(kind),entries,kinds,
                    LiquidKindName(names.water[0]),names.deep[0][0],names.deep[0][1],names.deep[0][2],LiquidKindName(names.water[1]),names.deep[1][0],names.deep[1][1],names.deep[1][2],unsigned(names.textures.size()));
                previous=state;
            }
            // Each liquid draw once per chunk and judgement: its box's centre and size, its ramp, the slime share at the
            // centre and the judgement over the box.
            const int ramp=bounds&&names.known?names.Ramp(raw):-1;
            if(bounds){
                static std::vector<std::array<int,4>> logged;
                const float centre[3]={.5f*(bounds[0]+bounds[3]),.5f*(bounds[1]+bounds[4]),.5f*(bounds[2]+bounds[5])};
                const std::array<int,4> cell={int(std::floor(centre[0]/33.3333f)),int(std::floor(centre[1]/33.3333f)),ramp,int(kind)};
                if(logged.size()<4096&&std::find(logged.begin(),logged.end(),cell)==logged.end()){
                    logged.push_back(cell);
                    WATER_LOG_INFO("host liquid draw ramp=%d at (%.1f %.1f %.1f) size (%.1f %.1f %.1f) share=%.2f kind=%s",ramp,centre[0],centre[1],centre[2],
                        bounds[3]-bounds[0],bounds[4]-bounds[1],bounds[5]-bounds[2],ramp>=0?names.ShareAt(centre,unsigned(ramp)):-1.,LiquidKindName(kind));
                }
            }
        }
        switch (kind) {
        case LiquidKind::Water: return WaterHostLiquidWater;
        case LiquidKind::Magma: return WaterHostLiquidMagma;
        case LiquidKind::Slime: return WaterHostLiquidSlime;
        case LiquidKind::Fade: return WaterHostLiquidFade;
        case LiquidKind::Unknown: return WaterHostLiquidUnknown;
        }
        return WaterHostLiquidUnknown;
    });
}
bool ResourcesSize(const WaterHostResources* resources)
{
    return resources && (resources->size == offsetof(WaterHostResources, nativeWater) ||
        resources->size == offsetof(WaterHostResources, refractionScene) ||
        resources->size == offsetof(WaterHostResources, surfaceVisibility) || resources->size == sizeof(*resources));
}
bool LiquidResources(WaterHostContext* context, WaterHostStamp stamp, const WaterHostResources* resources)
{
    return Current(context, stamp) && ResourcesSize(resources) &&
        resources->depth && resources->boundDepth;
}
int __cdecl ObserveLiquidDraw(WaterHostContext* context, WaterHostStamp stamp, WaterHostLiquidKind kind, const float* bounds)
{
    return Boundary([&]{
        if (!Current(context,stamp) || kind < WaterHostLiquidWater || kind > WaterHostLiquidFade) return 0;
        context->outdoorSlimeSeen |= kind == WaterHostLiquidSlime || kind == WaterHostLiquidFade;
        context->waterSeen |= kind == WaterHostLiquidWater || kind == WaterHostLiquidFade;
        if(kind!=WaterHostLiquidMagma)context->session.ObserveLiquidDraw(bounds);
        return 1;
    });
}
int __cdecl ObserveLiquid(WaterHostContext* context, WaterHostStamp stamp, WaterHostLiquidKind kind)
{ return ObserveLiquidDraw(context,stamp,kind,nullptr); }
int __cdecl SceneBefore(WaterHostContext* context, WaterHostStamp stamp, IDirect3DTexture9** colour, IDirect3DTexture9** depth)
{
    if (colour) *colour = nullptr;
    if (depth) *depth = nullptr;
    return Boundary([&] {
        return colour && depth && Current(context, stamp) && context->session.SceneBeforeLiquid(*colour, *depth) ? 1 : 0;
    });
}
int __cdecl BeforeSurface(WaterHostContext* context, WaterHostStamp stamp, const WaterHostResources* resources, int outdoors, WaterHostLiquidKind kind, const float* bounds, int ramp)
{
    return Boundary([&] {
        if (!LiquidResources(context, stamp, resources) || context->slimePending || !context->config.Get().enable)
            return 0;
        const bool wanted = kind == WaterHostLiquidSlime || kind == WaterHostLiquidFade ? context->config.Get().slime :
            kind == WaterHostLiquidMagma && context->config.Get().lava &&
            context->frame->snapshot->Inputs().nativeLiquids.camera != LiquidKind::Magma;
        if (!wanted) return 0;
        if (kind == WaterHostLiquidMagma) engine::NoteLavaDrawn();
        auto& session = context->session;
        const auto surfaceKind = kind == WaterHostLiquidMagma ? LiquidKind::Magma : kind == WaterHostLiquidFade ? LiquidKind::Fade : LiquidKind::Slime;
        context->surfaceBounded = session.DeferredSurfaceArea(context->frame->snapshot->Inputs(), bounds, outdoors != 0, surfaceKind, context->surfaceArea,
            kind == WaterHostLiquidFade ? ramp : -1);
        if (context->surfaceBounded && (context->surfaceArea.left >= context->surfaceArea.right ||
                                        context->surfaceArea.top >= context->surfaceArea.bottom)) return 0;
        const bool ok = outdoors ? session.SnapshotSurfaceBase(context->device, resources->depth, resources->boundDepth,
            context->surfaceBounded ? &context->surfaceArea : nullptr) :
            session.SnapshotBeforeLiquid(context->device, resources->depth, resources->boundDepth, true);
        context->slimePending = ok;
        context->surfaceRamp = ramp;
        context->slimeOutdoors = outdoors != 0;
        context->surfaceKind = surfaceKind;
        return ok ? 1 : 0;
    });
}
int __cdecl AfterSurface(WaterHostContext* context, WaterHostStamp stamp, const WaterHostResources* resources, int drawn)
{
    return Boundary([&] {
        if (!Current(context, stamp) || !context->slimePending) return 0;
        context->slimePending = false;
        if (!drawn || !LiquidResources(context, stamp, resources)) return 0;
        const bool ok = context->session.CaptureAndUndoSurface(context->device, resources->depth, resources->boundDepth,
            LiquidInputs(context), context->config.Get(), context->slimeOutdoors, context->surfaceKind, true,
            context->surfaceBounded ? &context->surfaceArea : nullptr, context->surfaceRamp);
        context->outdoorSlimeSeen |= ok && context->slimeOutdoors && context->surfaceKind != LiquidKind::Magma;
        return ok ? 1 : 0;
    });
}
int __cdecl CompositeSurfaces(WaterHostContext* context, WaterHostStamp stamp, const WaterHostResources* resources)
{
    return Boundary([&] {
        if (!LiquidResources(context, stamp, resources) || context->slimePending) return 0;
        const bool slime = context->session.CompositeDeferredSlime(context->device, resources->depth, resources->boundDepth,
            LiquidInputs(context), context->config.Get());
        const bool lava = context->session.CompositeDeferredLava(context->device, resources->depth, resources->boundDepth,
            LiquidInputs(context), context->config.Get());
        return slime || lava ? 1 : 0;
    });
}
IDirect3DTexture9* __cdecl EffectsMask(WaterHostContext* context, WaterHostStamp stamp, const WaterHostResources* resources, IDirect3DTexture9* hostMask)
{
    return Boundary([&]() -> IDirect3DTexture9* {
        if (!LiquidResources(context, stamp, resources)) return nullptr;
        return context->session.LavaEffectsMask(context->device, resources->depth, resources->boundDepth,
            LiquidInputs(context), context->config.Get(), hostMask);
    });
}
int __cdecl Reflection(WaterHostContext* context, WaterHostStamp stamp, const WaterHostReflection* input)
{
    if (!Current(context, stamp) || !input || input->size != sizeof(*input) || !std::isfinite(input->height)) return 0;
    for (float value : input->projection) if (!std::isfinite(value)) return 0;
    water::ReflectionView view;
    view.colour = input->colour;
    view.depth = input->depth;
    view.height = input->height;
    view.projection = input->projection;
    return context->session.SetReflection(Stamp(stamp), view);
}
int __cdecl Fog(WaterHostContext* context, WaterHostStamp stamp, WaterHostFog fog)
{
    if (!Current(context, stamp) || (fog != WaterHostNativeFog && fog != WaterHostFogCoversWater && fog != WaterHostSpatialFog)) return 0;
    if(fog==WaterHostFogCoversWater&&context->config.Get().waterAtmosphere==1&&context->session.EnvironmentAvailable())return 0;
    context->fog = fog;
    return 1;
}
int EnvironmentVersion(WaterHostContext* context,WaterHostStamp stamp,const WaterHostEnvironment* input,uint32_t version)
{
    return Boundary([&]{
        if(!Current(context,stamp))return 0;
        // A late/stale producer cannot revoke a newer record on this context.
        const bool lamps=input&&version>=4&&input->size==sizeof(WaterHostLampEnvironment);
        const bool measured=lamps||(input&&version>=3&&input->size==sizeof(WaterHostFogEnvironment));
        if(input&&((!measured&&input->size!=sizeof(*input))||input->source.session!=stamp.session||input->source.generation!=stamp.generation||
           input->source.frame!=stamp.frame))return 0;
        if(!context->session.ClearEnvironment(Stamp(stamp)))return 0;
        context->reflectedFogWeight=0;
        context->lamps={};
        if(!input)return 1;
        if(version==1&&(input->values.backgroundContent&~15u)){
            context->session.SetEnvironment(Stamp(stamp),{},nullptr);return 0;
        }
        if(input->values.policy==WaterEnvironmentCoreOwnsDistance&&context->config.Get().waterAtmosphere==1&&context->fog==WaterHostFogCoversWater){
            context->session.SetEnvironment(Stamp(stamp),{},nullptr);return 0;
        }
        if(!context->session.SetEnvironment(Stamp(stamp),input->values,input->background))return 0;
        if(lamps){
            const auto& samples=reinterpret_cast<const WaterHostLampEnvironment*>(input)->lamps;
            unsigned count=0;
            for(unsigned i=0;i<4;++i){
                const auto& p=samples.positions[i];const auto& c=samples.colours[i];
                bool valid=p[3]>0 && p[3]<=2000 && c[3]>0;
                for(unsigned j=0;j<4;++j)valid=valid&&std::isfinite(p[j])&&std::isfinite(c[j])&&c[j]>=0&&c[j]<=10000;
                if(valid){
                    std::copy(p,p+4,context->lamps.positions[count]);
                    std::copy(c,c+4,context->lamps.colours[count]);
                    ++count;
                }
            }
        }
        if(measured){
            const float tau=reinterpret_cast<const WaterHostFogEnvironment*>(input)->viewOpticalDepth48;
            // An optical depth of 0.16 extinguishes about 15 percent of the light.
            if(std::isfinite(tau))context->reflectedFogWeight=std::clamp(tau/0.16f,0.f,1.f);
        }
        return 1;
    });
}
int __cdecl Environment(WaterHostContext* context,WaterHostStamp stamp,const WaterHostEnvironment* input)
{return EnvironmentVersion(context,stamp,input,kWaterEnvironmentABI);}
int __cdecl EnvironmentV3(WaterHostContext* context,WaterHostStamp stamp,const WaterHostEnvironment* input)
{return EnvironmentVersion(context,stamp,input,3);}
int __cdecl EnvironmentV2(WaterHostContext* context,WaterHostStamp stamp,const WaterHostEnvironment* input)
{return EnvironmentVersion(context,stamp,input,2);}
int __cdecl EnvironmentLegacy(WaterHostContext* context,WaterHostStamp stamp,const WaterHostEnvironment* input)
{return EnvironmentVersion(context,stamp,input,1);}
static int SurfaceStatusVersion(WaterHostContext* context,WaterHostStamp stamp,WaterHostSurfaceStatus* output,bool allowLamps)
{
    return Boundary([&]{
        const bool lamps=allowLamps&&output&&output->size==sizeof(WaterHostLampStatus);
        if(!output||(!lamps&&output->size!=sizeof(*output)))return 0;
        *output={output->size,WaterHostSurfaceUnchanged,0,WaterEnvironmentNotProvided};
        if(lamps)reinterpret_cast<WaterHostLampStatus*>(output)->wantsLamps=0;
        if(!Current(context,stamp))return 0;
        if(lamps){
            const auto& cfg=context->config.Get();
            reinterpret_cast<WaterHostLampStatus*>(output)->wantsLamps=cfg.enable&&cfg.fineHighlights&&cfg.specular>0;
        }
        const auto result=context->session.LastSurfaceResult();
        switch(result){
        case Renderer::SurfaceResult::Unchanged:output->outcome=WaterHostSurfaceUnchanged;break;
        case Renderer::SurfaceResult::Shaded:output->outcome=WaterHostSurfaceShaded;break;
        case Renderer::SurfaceResult::Recovered:output->outcome=WaterHostSurfaceRecovered;break;
        case Renderer::SurfaceResult::AbortFrame:output->outcome=WaterHostSurfaceAbortFrame;break;
        }
        output->distanceAtmosphereShaded=context->session.DistanceAtmosphereShaded()?1:0;
        output->fallback=context->config.Get().waterAtmosphere?context->session.EnvironmentFallback():WaterEnvironmentDisabled;
        return 1;
    });
}
int __cdecl SurfaceStatus(WaterHostContext* context,WaterHostStamp stamp,WaterHostSurfaceStatus* output)
{return SurfaceStatusVersion(context,stamp,output,false);}
int __cdecl LampStatus(WaterHostContext* context,WaterHostStamp stamp,WaterHostSurfaceStatus* output)
{return SurfaceStatusVersion(context,stamp,output,true);}
int DrawPass(WaterHostContext* context, WaterHostStamp stamp, WaterHostPass pass,
             const WaterHostResources* resources, int shade, uint32_t stencilBit)
{
    return Boundary([&] {
        if (!Current(context, stamp) || !ResourcesSize(resources)) return 0;
        auto input = LiquidInputs(context);
        const auto& cfg = context->config.Get();
        if(pass==WaterHostWorldDone&&cfg.traceLiquids){
            const unsigned state=unsigned(input.cameraInWater)|(unsigned(input.cameraInSlime)<<1)|
                (unsigned(context->outdoorSlimeSeen)<<2)|(unsigned(context->waterSeen)<<3);
            if(state!=context->liquidViewLogged){
                WATER_LOG_INFO("host liquid view water=%u slime=%u observed_slime=%u observed_water=%u native=%u camera=%s ocean=%s river=%s textures=%u",state&1,(state>>1)&1,(state>>2)&1,(state>>3)&1,unsigned(input.nativeLiquids.known),LiquidKindName(input.nativeLiquids.camera),LiquidKindName(input.nativeLiquids.water[0]),LiquidKindName(input.nativeLiquids.water[1]),unsigned(input.nativeLiquids.textures.size()));
                context->liquidViewLogged=state;
            }
        }
        if (!cfg.enable) return 0;
        auto* d = context->device;
        auto* t = resources->depth;
        auto* z = resources->boundDepth;
        auto& session = context->session;
        bool result = false;
        switch (pass) {
        case WaterHostBeforeModels: result = true; break; // retired; no current host asks for it
        case WaterHostAmbientOcclusion: result = session.AmbientOcclusion(d, t, z, input, cfg); break;
        case WaterHostRainRings: result = session.DrawRainRings(d, z, input, cfg); break;
        case WaterHostPuddles: result = session.Puddles(d, t, z, input, cfg); break;
        case WaterHostBeforeLiquid: result = session.SnapshotBeforeLiquid(d, t, z, cfg.surface || cfg.slime); break;
        case WaterHostCompositeWater: result = session.CompositeWater(d, t, z, input, cfg, shade != 0,
            resources->size >= offsetof(WaterHostResources, refractionScene) ? resources->nativeWater : nullptr,
            resources->size >= offsetof(WaterHostResources, surfaceVisibility) ? resources->refractionScene : nullptr,
            resources->size == sizeof(*resources) ? resources->surfaceVisibility : nullptr, stencilBit); break;
        case WaterHostWorldDone: result = session.Render(d, t, z, input, cfg, stencilBit); session.CaptureFinished(d); break;
        default: break;
        }
        return result ? 1 : 0;
    });
}
int __cdecl Draw(WaterHostContext* context, WaterHostStamp stamp, WaterHostPass pass,
                 const WaterHostResources* resources, int shade)
{ return DrawPass(context,stamp,pass,resources,shade,0); }

template<WaterHostPass pass> int __cdecl MaskedPass(WaterHostContext* context, WaterHostStamp stamp,
                                 const WaterHostResources* resources, uint32_t stencilBit)
{
    if(!stencilBit || stencilBit>0xff || (stencilBit & (stencilBit-1)))return 0;
    return DrawPass(context,stamp,pass,resources,1,stencilBit);
}

int __cdecl Lost(WaterHostContext* context)
{
    return Boundary([&] {
        if (!context || !context->Owner()) return 0;
        End(context);
        context->liquids.ReleaseAll();
        context->sceneryPoses={};
        context->session.LoseDevice();
        return 1;
    });
}
int __cdecl Restored(WaterHostContext* context)
{
    return context && context->Owner() && context->session.RestoreDevice(context->device);
}
int __cdecl CanUnload() { return liveContexts.load()==0&&liveFrames.load()==0&&engine::RestoreVisualTime(); }
int __cdecl ProfilePresentBegin(WaterHostContext* context,uint32_t source,uint64_t hostFrame)
{
    return Boundary([&]{if(!context||!context->Owner()||source<2||source>3)return 0;
        auto& session=context->session;
        {water::FrameDiagnostics::Scope scope(session.Diagnostics(),water::FrameDiagnostics::ConfigReload);context->config.ReloadIfChanged(true);}
        engine::ConfigureVisualTime(context->config.Get().timeOfDay,context->config.Get().dayCycleSeconds);
        session.ConfigureDiagnostics(context->config.Get(),source);
        session.Diagnostics().PresentBegin(hostFrame);return 1;});
}
int __cdecl ProfilePresentReady(WaterHostContext* context){return Boundary([&]{if(!context||!context->Owner())return 0;context->session.Diagnostics().PresentReady();return 1;});}
int __cdecl ProfilePresentEnd(WaterHostContext* context,int32_t result)
{
    return Boundary([&]{if(!context||!context->Owner())return 0;context->session.Diagnostics().PresentEnd(HRESULT(result));return 1;});
}
uint64_t __cdecl TraceBegin(WaterHostContext* context){return context&&context->Owner()?context->session.Diagnostics().BeginCall():0;}
int __cdecl TraceEnd(WaterHostContext* context,uint64_t token,const char* name){return Boundary([&]{if(!context||!context->Owner())return 0;context->session.Diagnostics().EndCall(token,name);return 1;});}
const WaterHostEnvironmentAPI environment={kWaterEnvironmentABI,sizeof(WaterHostEnvironmentAPI),Environment,LampStatus};
const WaterHostEnvironmentAPI environmentV3={3,sizeof(WaterHostEnvironmentAPI),EnvironmentV3,SurfaceStatus};
const WaterHostEnvironmentAPI environmentV2={2,sizeof(WaterHostEnvironmentAPI),EnvironmentV2,SurfaceStatus};
const WaterHostEnvironmentAPI environmentLegacy={1,sizeof(WaterHostEnvironmentAPI),EnvironmentLegacy,SurfaceStatus};
const WaterHostTraceAPI trace={kWaterTraceABI,sizeof(WaterHostTraceAPI),TraceBegin,TraceEnd};
const WaterHostDiagnosticsAPI diagnostics={kWaterDiagnosticsABI,sizeof(WaterHostDiagnosticsAPI),ProfilePresentBegin,ProfilePresentReady,ProfilePresentEnd};
int __cdecl CaptureHostPass(WaterHostContext* context,WaterHostStamp stamp,const char* name,int after)
{
    return Boundary([&]{return Current(context,stamp)&&name&&(after==0||after==1)&&
        context->session.CaptureHostPass(context->device,name,after!=0)?1:0;});
}
const WaterHostCaptureAPI captureAPI={kWaterCaptureABI,sizeof(WaterHostCaptureAPI),CaptureHostPass};
const WaterHostAPI api = {kWaterHostABI, sizeof(WaterHostAPI), Create, Destroy, CopyFrame, CaptureClient,
                          Retain, Release, Begin, Reflection, Fog, Draw, End, Lost, Restored, CanUnload};
const WaterHostLiquidAPI liquid = {kWaterLiquidABI, sizeof(WaterHostLiquidAPI), ClassifyLiquid, ObserveLiquid, BeforeSurface, AfterSurface, CompositeSurfaces, CaptureClientMapped, EffectsMask, SceneBefore, ObserveLiquidDraw, MaskedPass<WaterHostCompositeWater>, MaskedPass<WaterHostWorldDone>};
// ABI 6 callers read the unchanged prefix and continue to use the original scene.
const WaterHostLiquidAPI liquidLegacy = {6, offsetof(WaterHostLiquidAPI, scene_before), ClassifyLiquid, ObserveLiquid, BeforeSurface, AfterSurface, CompositeSurfaces, CaptureClientMapped, EffectsMask, nullptr, nullptr, nullptr, nullptr};
}

extern "C" __declspec(dllexport) const WaterHostAPI* __cdecl WaterHostGetAPI(uint32_t abi, uint32_t size)
{
    return abi == kWaterHostABI && size == sizeof(WaterHostAPI) ? &api : nullptr;
}
extern "C" __declspec(dllexport) const WaterHostLiquidAPI* __cdecl WaterHostGetLiquidAPI(uint32_t abi, uint32_t size)
{
    if (abi == 6 && size == offsetof(WaterHostLiquidAPI, scene_before)) return &liquidLegacy;
    // ABI 7/8 keep the original prefix; their observations remain conservative.
    static const WaterHostLiquidAPI previous7 = {7, offsetof(WaterHostLiquidAPI, observe_draw), ClassifyLiquid, ObserveLiquid, BeforeSurface, AfterSurface, CompositeSurfaces, CaptureClientMapped, EffectsMask, SceneBefore, nullptr, nullptr, nullptr};
    static const WaterHostLiquidAPI previous8 = {8, offsetof(WaterHostLiquidAPI, observe_draw), ClassifyLiquid, ObserveLiquid, BeforeSurface, AfterSurface, CompositeSurfaces, CaptureClientMapped, EffectsMask, SceneBefore, nullptr, nullptr, nullptr};
    if(size==offsetof(WaterHostLiquidAPI, observe_draw))return abi==7?&previous7:abi==8?&previous8:nullptr;
    static const WaterHostLiquidAPI previous9 = {9, offsetof(WaterHostLiquidAPI, composite_masked_water), ClassifyLiquid, ObserveLiquid, BeforeSurface, AfterSurface, CompositeSurfaces, CaptureClientMapped, EffectsMask, SceneBefore, ObserveLiquidDraw, nullptr, nullptr};
    if(abi==9 && size==previous9.size)return &previous9;
    static const WaterHostLiquidAPI previous10 = {10, offsetof(WaterHostLiquidAPI, finish_masked_world), ClassifyLiquid, ObserveLiquid, BeforeSurface, AfterSurface, CompositeSurfaces, CaptureClientMapped, EffectsMask, SceneBefore, ObserveLiquidDraw, MaskedPass<WaterHostCompositeWater>, nullptr};
    if(abi==10 && size==previous10.size)return &previous10;
    return abi==kWaterLiquidABI && size==sizeof(WaterHostLiquidAPI)?&liquid:nullptr;
}
#endif

#if defined(WATER_COOPERATIVE_BUILD)
extern "C" __declspec(dllexport) const WaterHostDiagnosticsAPI* __cdecl WaterHostGetDiagnosticsAPI(uint32_t abi,uint32_t size)
{ return abi==kWaterDiagnosticsABI&&size==sizeof(WaterHostDiagnosticsAPI)?&diagnostics:nullptr; }
#endif

#if defined(WATER_COOPERATIVE_BUILD)
extern "C" __declspec(dllexport) const WaterHostCaptureAPI* __cdecl WaterHostGetCaptureAPI(uint32_t abi,uint32_t size)
{return abi==kWaterCaptureABI&&size==sizeof(WaterHostCaptureAPI)?&captureAPI:nullptr;}
extern "C" __declspec(dllexport) const WaterHostTraceAPI* __cdecl WaterHostGetTraceAPI(uint32_t abi,uint32_t size)
{return abi==kWaterTraceABI&&size==sizeof(WaterHostTraceAPI)?&trace:nullptr;}
#endif

#if defined(WATER_COOPERATIVE_BUILD)
extern "C" __declspec(dllexport) WaterSceneryPoseRequests* __cdecl WaterHostSceneryPoses(WaterHostContext* context,uint32_t abi,uint32_t size)
{
    if(!context || !context->Owner() || abi!=1 || size!=sizeof(WaterSceneryPoseRequests))return nullptr;
    context->poseHost=true;
    return &context->sceneryPoses;
}
extern "C" __declspec(dllexport) const WaterHostEnvironmentAPI* __cdecl WaterHostGetEnvironmentAPI(uint32_t abi,uint32_t size)
{
    if(size!=sizeof(WaterHostEnvironmentAPI))return nullptr;
    return abi==kWaterEnvironmentABI?&environment:abi==3?&environmentV3:abi==2?&environmentV2:abi==1?&environmentLegacy:nullptr;
}
#endif
