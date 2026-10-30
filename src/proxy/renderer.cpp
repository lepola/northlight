// Client-local D3D9 extension. Read-only, signature-gated world context; no game launch code.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include "forwarders.h"
#include "device_mirror.h"
#include "mirror_audit_schedule.h"
#include "async_memory_diagnostics.h"
#include "geometry_memory.h"
#include "mirror_guarded_device.h"
#include "mirror_resources.h"
#include "tracked_buffers.h"
#include "mirror_write_hook.h"
#include "signatures.h"
#include "shader_tags.h"
#include "stream_hooks.h"
#include "settings_service.h"
#include "settings_overlay.h"
#include "stream_device.h"
#include "compiled_shaders.h"
#include "projection.h"
#include "world_draw_domain.h"
#include "extension_guard.h"
#include "backend_policy.h"
#include "upload_lock.h"
#include "lock_meter.h"
#include "replay_copies.h"
#include "dxvk_compatibility.h"
#include "backend_loader.h"
#include "frame_intervals.h"
#include "effect_switches.h"
#include "diagnostics_switch.h"
#include "render_thread_probe.h"
#include "effects_buckets.h"
#include "draw_gates.h"
#include "weather_state.h"
#include "ui_boundary.h"
#include "weather_detect.h"
#include "weather_memory.h"
#include "translucent_depth.h"
#include "name_stencil.h"
#include "log_rotation.h"
#include "memory_guard.h"
#include "address_space_snapshot.h"
#include "memory_switches.h"
#include "northlight_mem.h"
#include "m2_shader_families.h"
#include "ao_actor_stencil.h"
#include "ao_actor_policy.h"

// 0.3.181 (r90): the DLL's one memcmp. This strong definition overrides zig compiler_rt's weak byte
// loop for every call site in all four TUs (libc++ included) and returns the same value for every
// input (northlight_mem.h). no_builtin: its body must not turn back into a memcmp call. Not exported.
extern "C" __attribute__((no_builtin("memcmp"),no_builtin("bcmp"))) int memcmp(const void* a,const void* b,size_t n){return NorthlightMem::compare(a,b,n);}

static HMODULE selfModule;
static wchar_t rootPath[MAX_PATH];
static std::string settingsRecovery; // failure text of the startup journal recovery ("" = fine), logged by the first Device
// Game folder = directory of the host exe, resolved on first use (never in
// DllMain: the macOS proxy is preloaded under the loader lock from dlls.txt).
// backend() calls it first; every export reaches backend() before it logs.
static void ensureRootPath(){
    static const bool resolved=[]{
        wchar_t exe[MAX_PATH]={},self[MAX_PATH]={};
        const DWORD n=GetModuleFileNameW(nullptr,exe,MAX_PATH),m=GetModuleFileNameW(selfModule,self,MAX_PATH);
        const std::wstring root=NorthlightBackend::gameRoot(n&&n<MAX_PATH?exe:L"",m&&m<MAX_PATH?self:L"");
        if(root.size()<MAX_PATH)wcscpy(rootPath,root.c_str());
        // Before backend selection, MemorySwitches or any loader reads an INI: finish a settings commit a crash interrupted (one stat when none).
        if(rootPath[0])settingsRecovery=NorthlightSettings::Store::recoverAtStartup(std::filesystem::path(rootPath));
        return true;
    }();
    (void)resolved;
}
static FILE* logFile;
static LONG deviceSerial=0,liveDevices=0;
static NorthlightBackend::Kind selectedBackend=NorthlightBackend::Kind::Legacy;
// Called once, under logLock, before the first open truncates the log.
struct WindowsLogFiles {
    DWORD error=0;
    bool exists(const wchar_t* path){return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;}
    bool move(const wchar_t* from,const wchar_t* to){if(MoveFileExW(from,to,MOVEFILE_REPLACE_EXISTING))return true;error=GetLastError();return false;}
    bool readOnly(const wchar_t* path){const DWORD a=GetFileAttributesW(path);return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_READONLY)&&!(a&FILE_ATTRIBUTE_DIRECTORY);}
    bool makeWritable(const wchar_t* path){const DWORD a=GetFileAttributesW(path);return a!=INVALID_FILE_ATTRIBUTES&&SetFileAttributesW(path,a&~DWORD(FILE_ATTRIBUTE_READONLY))!=FALSE;}
    // First 16 KiB, shared with a live writer; unreadable = not a session.
    bool session(const wchar_t* path){
        HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE){error=GetLastError();return false;}
        static char head[16384];DWORD got=0;const bool ok=ReadFile(h,head,sizeof(head),&got,nullptr)!=FALSE;if(!ok)error=GetLastError();CloseHandle(h);
        return ok&&NorthlightLogRotation::recordedSession(head,got);}
};
static NorthlightLogRotation::Result logRotation=NorthlightLogRotation::Result::Missing;static DWORD logRotationError=0;
static void rotatePreviousLog(){
    wchar_t current[MAX_PATH],previous[MAX_PATH];
    if(swprintf(current,MAX_PATH,L"%lsnorthlight-renderer.log",rootPath)<0||swprintf(previous,MAX_PATH,L"%lsnorthlight-renderer.prev.log",rootPath)<0){logRotation=NorthlightLogRotation::Result::Failed;return;}
    WindowsLogFiles files;logRotation=NorthlightLogRotation::rotate(files,current,previous);logRotationError=files.error;
}
static SRWLOCK logLock=SRWLOCK_INIT;
static bool logRotated=false;
struct LogCost {
    unsigned long long calls=0,formattedBytes=0,ioErrors=0;
    LONGLONG wallTicks=0,maxTicks=0;
};
static LogCost logCost;
static LONGLONG logFrequency=0,logIntervalStart=0;
// Lines written by the calling thread (RenderProfile: frames that logged are left out of FRAME cost).
static thread_local unsigned long long threadLogLines=0;
static void logf(const char* fmt, ...) {
    LARGE_INTEGER start={};const bool timed=QueryPerformanceCounter(&start)!=0;
    // Render and worker threads share the stream. Hold one lock over opening,
    // the entire line, flushing and counters; report after releasing this lock.
    AcquireSRWLockExclusive(&logLock);
    if(!logFrequency){LARGE_INTEGER frequency={};if(QueryPerformanceFrequency(&frequency))logFrequency=frequency.QuadPart;}
    if(!logIntervalStart&&timed)logIntervalStart=start.QuadPart;
    if (!logFile) {
        if(!logRotated){logRotated=true;rotatePreviousLog();}
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%lsnorthlight-renderer.log", rootPath);
        logFile = _wfopen(path, L"w");
    }
    ++logCost.calls;++threadLogLines;
    if(logFile){
        va_list args;va_start(args,fmt);const int bytes=vfprintf(logFile,fmt,args);va_end(args);
        if(bytes>=0)logCost.formattedBytes+=unsigned(bytes);else ++logCost.ioErrors;
        if(fputc('\n',logFile)!=EOF)++logCost.formattedBytes;else ++logCost.ioErrors;
        if(fflush(logFile)!=0)++logCost.ioErrors;
    }else ++logCost.ioErrors;
    LARGE_INTEGER end={};
    if(timed&&QueryPerformanceCounter(&end)&&end.QuadPart>=start.QuadPart){
        const LONGLONG ticks=end.QuadPart-start.QuadPart;logCost.wallTicks+=ticks;
        if(ticks>logCost.maxTicks)logCost.maxTicks=ticks;
    }
    ReleaseSRWLockExclusive(&logLock);
}
static void reportLogCost(){
    AcquireSRWLockExclusive(&logLock);
    const LogCost sample=logCost;const LONGLONG frequency=logFrequency;
    LARGE_INTEGER now={};const bool timed=QueryPerformanceCounter(&now)!=0;
    const LONGLONG interval=timed&&logIntervalStart&&now.QuadPart>=logIntervalStart?now.QuadPart-logIntervalStart:0;
    logCost={};logIntervalStart=timed?now.QuadPart:0;
    ReleaseSRWLockExclusive(&logLock);
    // Aggregated caller wall time includes lock waits and can overlap between
    // threads. It is not CPU utilization. Bytes exclude text-mode CRLF growth.
    if(sample.calls&&frequency>0){const double ms=1000.0/double(frequency);
        logf("LOGGER intervalMs=%.3f calls=%llu formattedBytes=%llu callerWallMs=%.3f maxCallMs=%.3f ioErrors=%llu",
            interval*ms,sample.calls,sample.formattedBytes,sample.wallTicks*ms,sample.maxTicks*ms,sample.ioErrors);
    }
}
// CPU wall-time samples include driver calls but never wait on the GPU.
// 0.3.175 (S3): the clock of the effects buckets (effects_buckets.h), RenderProfile sample frames only.
static std::int64_t qpcNow(){LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;}
struct CpuScope {
    LONGLONG* elapsed;LARGE_INTEGER start={};
    static inline unsigned long long reads=0; /* clock reads of active scopes (RenderProfile: timer overhead) */
    explicit CpuScope(LONGLONG* output):elapsed(output){if(elapsed){QueryPerformanceCounter(&start);reads+=2;}}
    ~CpuScope(){if(elapsed){LARGE_INTEGER end;QueryPerformanceCounter(&end);*elapsed+=end.QuadPart-start.QuadPart;}}
};
static NorthlightMemoryDiagnostics::Sample queryMemoryDiagnostic(void*) {
    using Clock=std::chrono::steady_clock;
    const auto started=Clock::now();NorthlightMemoryDiagnostics::Sample sample;
    MEMORYSTATUSEX memory={};memory.dwLength=sizeof memory;
    if(GlobalMemoryStatusEx(&memory)){
        sample.availableVirtual=memory.ullAvailVirtual;sample.totalVirtual=memory.ullTotalVirtual;
        sample.availablePhysical=memory.ullAvailPhys;
        uintptr_t address=0;MEMORY_BASIC_INFORMATION region={};
        while(VirtualQuery(reinterpret_cast<const void*>(address),&region,sizeof region)==sizeof region){
            ++sample.regions;
            if(region.State==MEM_FREE){sample.totalFree+=region.RegionSize;sample.largestFree=std::max(sample.largestFree,std::uint64_t(region.RegionSize));}
            const std::uint64_t next=std::uint64_t(reinterpret_cast<uintptr_t>(region.BaseAddress))+region.RegionSize;
            if(next<=address||next>UINTPTR_MAX)break;address=uintptr_t(next);
        }
        sample.availableVirtual=NorthlightGeometryMemory::noteWalk(sample.availableVirtual,sample.totalFree); /* task 42: Wine overstates the API value */
        sample.valid=true;
    }
    sample.tick=GetTickCount();
    sample.wallNanoseconds=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started).count());
    return sample;
}
template<class T> static void drop(T*& p) { if (p) { p->Release(); p = nullptr; } }
// task 42 (MEMMAP): the log sink, the sampler thread's job entry (in-world points) and the process-level points that precede any Device.
static void memmapSink(const char* line){logf("%s",line);}
static void memmapJob(void*,const void* data,std::size_t size){
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL); // idempotent; the first job may precede the first query
    NorthlightMemMap::runJob(&memmapSink,data,size);
}
// task 42: MemMap=0 (default) = no walk. The process-level points precede WorldRenderer::loadQuality, so the first one reads the ini itself.
static bool memmapEnabled(){
    if(!NorthlightMemorySwitches::loaded()){std::string text;const bool has=NorthlightStream::readQualityText(rootPath,text);NorthlightMemorySwitches::configureFromText(text,has);}
    return NorthlightMemorySwitches::memMap();
}
static void memmapProcess(const char* point,long device){
    if(!memmapEnabled())return;
    try{
        char label[128];std::snprintf(label,sizeof label,"device=%ld point=%s frame=0 tick=%lu",device,point,(unsigned long)GetTickCount());
        NorthlightMemMap::snapshot(&memmapSink,label,"");
    }catch(...){}
}
// Sampler thread entry: the same walk, at below-normal priority (it also feeds the memory guard).
static NorthlightMemoryDiagnostics::Sample queryAddressSpace(void* context) {
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL); // one cheap call per walk (seconds apart)
    return queryMemoryDiagnostic(context);
}
template<size_t N> static bool contains(const uint64_t (&a)[N], uint64_t value) {
    for (auto x : a) if (x == value) return true;
    return false;
}
// FNV-1a over the bytecode; words keeps it for the water mask patch (no second fetch).
template<class T> static uint64_t shaderHash(T* shader, std::vector<DWORD>& words) {
    UINT size = 0;
    words.clear();
    if (!shader || FAILED(shader->GetFunction(nullptr, &size)) || size > 1024 * 1024) return 0;
    words.assign((size + 3) / 4, 0);
    if (FAILED(shader->GetFunction(words.data(), &size))) { words.clear(); return 0; }
    return NorthlightShaderTags::fnv1a(words.data(), size); // 0.3.192 (CS): shader_tags.h, shared with the stream's triggers
}

#include "saved_state.h"

#include "gpu_profile.h"
#include "gpu_frame_timer.h" // 0.3.200 (gpu budget)
#include "gpu_budget.h"
#include "water_renderer.h"
#include "world_renderer.h"
#include "client_latency_diagnostics.h"
static void logClientLatency(std::uint64_t frame,const char* reason){
    const auto s=NorthlightClientLatency::read(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),NorthlightWorldContext::readSelfLive);
    logf("CLIENT latency frame=%llu tick=%lu reason=%s verified=%d gxFixLagCvar=%d gxFixLagRuntime=%d",static_cast<unsigned long long>(frame),static_cast<unsigned long>(GetTickCount()),reason,int(s.verified),s.configured,s.runtime);
}
#include "celestial_disc_renderer.h"
#include "shadow_blob_filter.h"
#include "spell_layer.h"
#include "particle_light_capture.h"
#include "shader_read_set.h"
// The counter index of a raw extension method (the D3D calls line), ~0u if unknown.
static unsigned rawMethodIndex(const char* name){for(unsigned i=0;i<ExtensionDevice::RawMethods;++i)if(!std::strcmp(ExtensionDevice::rawMethodName(i),name))return i;return ~0u;}

// 0.3.154: D3DPERF_* export calls of a RenderProfile sample frame (flag set per frame by the Device).
static std::atomic<bool> perfCounting{false};static std::atomic<unsigned> perfCalls{0};
static void countPerf(){if(perfCounting.load(std::memory_order_relaxed))perfCalls.fetch_add(1,std::memory_order_relaxed);}
// 0.3.149, RenderProfile=1 only: Present entry, after the extension's frame finish,
// after the real Present (render_thread_probe.h FrameCostWindow). Off: no clock read.
struct PresentTicks {
    bool on=NorthlightRenderThreadProbe::profiling();LARGE_INTEGER entry={},finished={},done={};
    PresentTicks(){if(on)QueryPerformanceCounter(&entry);}
    void finish(){if(on)QueryPerformanceCounter(&finished);}
    void present(){if(on)QueryPerformanceCounter(&done);}
};
static void finishDeviceFrame(IDirect3DDevice9* owner,bool implicitChain);
static void presentedDeviceFrame(IDirect3DDevice9* owner,const PresentTicks& ticks);
class SwapChain final : public ForwardIDirect3DSwapChain9 {
    LONG refs=1;
    IDirect3DDevice9* owner;
    NorthlightMirrorResources::Registry* resources;
public:
    bool implicitChain; /* swap chain 0 (GetSwapChain(0)): the settings overlay draws only for its Present */
    SwapChain(IDirect3DSwapChain9* r,IDirect3DDevice9* d,NorthlightMirrorResources::Registry* registry,bool implicit):ForwardIDirect3DSwapChain9(r),owner(d),resources(registry),implicitChain(implicit){owner->AddRef();}
    ~SwapChain(){real->Release();owner->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DSwapChain9)){*out=this;AddRef();return S_OK;}
        *out=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out) override{if(!out)return D3DERR_INVALIDCALL;*out=owner;owner->AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT index,D3DBACKBUFFER_TYPE type,IDirect3DSurface9** out) override{
        MirrorGuard lock(resources->gate(),MirrorSite::SwapChain);HRESULT hr=real->GetBackBuffer(index,type,out);if(SUCCEEDED(hr))resources->wrap(out);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(IDirect3DSurface9* surface) override{
        MirrorGuard lock(resources->gate(),MirrorSite::SwapChain);return real->GetFrontBufferData(resources->unwrap(surface));
    }
    HRESULT STDMETHODCALLTYPE Present(const RECT* src,const RECT* dst,HWND window,const RGNDATA* dirty,DWORD flags) override{
        MirrorGuard lock(resources->gate(),MirrorSite::SwapChain);resources->gate().noteFirst(resources->gate().swapPresentTid);PresentTicks ticks;finishDeviceFrame(owner,implicitChain);ticks.finish();
        const HRESULT hr=real->Present(src,dst,window,dirty,flags);ticks.present();presentedDeviceFrame(owner,ticks);return hr;
    }
};

/* 0.3.212 (task 32) NameMask needs stencil bits in the game's depth buffer, which is D24X8. When the key is on (northlight-quality.ini, read once at CreateDevice with the stream keys by commandStreamRequested, the loader the WorldRenderer uses) and the device supports D24S8 for the back buffer format
   (CheckDeviceFormat D3DUSAGE_DEPTHSTENCIL, CheckDepthStencilMatch), the auto depth stencil in the present parameters (CreateDevice, Reset) and D24X8 CreateDepthStencilSurface are D24S8 instead (the StreamDevice does the same on the game
   thread before it records). Otherwise D24X8 stays (logged once; the mask then stays off while that surface is the world depth). */
static bool depthUpgradeSupported(IDirect3D9* d3d,UINT adapter,D3DDEVTYPE type,D3DFORMAT back){
    D3DDISPLAYMODE mode={};if(FAILED(d3d->GetAdapterDisplayMode(adapter,&mode)))return false;
    if(back==D3DFMT_UNKNOWN)back=mode.Format;
    return d3d->CheckDeviceFormat(adapter,type,mode.Format,D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_SURFACE,D3DFMT_D24S8)==D3D_OK&&d3d->CheckDepthStencilMatch(adapter,type,mode.Format,back,D3DFMT_D24S8)==D3D_OK;
}
/* Upgrades the present parameters' auto depth stencil; publishes the verdict for the surfaces created later. */
static void upgradeAutoDepth(IDirect3D9* d3d,UINT adapter,D3DDEVTYPE type,D3DPRESENT_PARAMETERS* pp,bool wanted,const char* where){
    static bool unavailableLogged=false,upgradedLogged=false;
    const bool ok=wanted&&pp&&depthUpgradeSupported(d3d,adapter,type,pp->BackBufferFormat);
    NorthlightStream::depthUpgrade.store(ok,std::memory_order_relaxed);
    if(wanted&&!ok&&!unavailableLogged){unavailableLogged=true;logf("STENCIL depth upgrade unavailable where=%s: D24S8 is not supported for this back buffer format; D24X8 stays",where);}
    if(ok&&pp->EnableAutoDepthStencil&&pp->AutoDepthStencilFormat==D3DFMT_D24X8){pp->AutoDepthStencilFormat=D3DFMT_D24S8;if(!upgradedLogged){upgradedLogged=true;logf("STENCIL depth upgraded D24X8->D24S8 where=%s",where);}}
}
class Device final : public GuardedMirrorDevice {
    LONG refs = 1;
    LONG diagnosticId=0;
    NorthlightFrameIntervals::Window frameIntervals;bool intervalFocus=true; /* every frame since the last FRAME interval line had focus */
    bool extensionFault=false;
    IDirect3D9* parent;
    DeviceMirror mirrorState;
    NorthlightMirrorResources::Registry mirrorResources;
    ExtensionDevice* ext;
    NorthlightStateBlockPool stateBlocks;
    static void mirrorEscape(void* context,const char* reason)noexcept{static_cast<DeviceMirror*>(context)->disable(reason);}
    static void bufferEscape(IDirect3DDevice9* owner,const char* reason)noexcept{static_cast<Device*>(owner)->mirrorState.disable(reason);}
    // Render-thread profiling census.
    // thread may log it: periodic (600 frames), at destroy, and once at the first foreign entry of a class.
    static void gateForeignReport(void* context,unsigned,std::uint32_t)noexcept{static_cast<Device*>(context)->logGateThreads("first-foreign");}
    void logGateThreads(const char* event)noexcept{
        const MirrorGate& g=mirrorState.gate;const bool first=g.firstReady.load(std::memory_order_acquire);
        auto foreign=[&](MirrorSite s){return unsigned(g.foreign[unsigned(s)].load(std::memory_order_relaxed));};
        auto tid=[](const std::atomic<std::uint32_t>& t){return (unsigned long)t.load(std::memory_order_relaxed);};
        if(NorthlightDiagnostics::enabled())logf("GATE threads device=%ld owner=%lu presentTid=%lu swapPresentTid=%lu drawTid=%lu foreignDevice=%u foreignRegistry=%u foreignResource=%u foreignStateBlock=%u foreignSwapChain=%u foreignRaw=%u foreignBuffer=%u ownerLocked=%u first=%lu/%s/%u frame=%u gameTid=%lu replayTid=%lu event=%s",
            diagnosticId,(unsigned long)g.ownerTid,tid(g.presentTid),tid(g.swapPresentTid),tid(g.drawTid),foreign(MirrorSite::Device),foreign(MirrorSite::Registry),foreign(MirrorSite::Resource),
            foreign(MirrorSite::StateBlock),foreign(MirrorSite::SwapChain),foreign(MirrorSite::Raw),foreign(MirrorSite::Buffer),unsigned(g.ownerLocked.load(std::memory_order_relaxed)),
            first?(unsigned long)g.firstTid:0ul,mirrorSiteName(first?g.firstSite:MirrorGate::Sites),first?unsigned(g.firstFrame):0u,unsigned(g.frame.load(std::memory_order_relaxed)),NorthlightStream::gameTid.load(std::memory_order_relaxed),NorthlightStream::replayTid.load(std::memory_order_relaxed),event); /* 0.3.192 (CS): 0/0 on the direct path */
    }

    std::shared_ptr<NorthlightSettings::Service> settingsService;
    std::unique_ptr<NorthlightOverlay::Overlay> overlay; /* Ctrl+Shift+F11 settings window (settings_overlay.h; [Renderer] SettingsOverlayKey); null with [Renderer] SettingsOverlay=0 */
    std::string settingsRevision;
    std::unique_ptr<WorldRenderer> world;
    std::unique_ptr<NorthlightCelestialDiscRenderer> celestialDiscs;
    std::unique_ptr<NorthlightShadowBlobFilter> shadowBlobs;
    std::unique_ptr<NorthlightWaterRenderer> water;
    std::unique_ptr<NorthlightGpuProfile> gpuProfile,gpuSceneProfile;
    bool gpuFrameStarted=false,gpuSceneSplit=false,gpuEffectsSplit=false;
    void gpuEffectsEnd(){
        if(gpuSceneSplit&&!gpuEffectsSplit){gpuSceneProfile->mark("Effects");gpuEffectsSplit=true;}
    }
    // 0.3.200 (gpu budget): GpuBudgetMs > 0 only: Northlight's GPU time on every effects frame (two timestamps, read back late, never waited on)
    // feeds the controller; its level goes to the world for the next frames. GpuBudgetMs=0: no query is created, the world stays at level 0.
    std::unique_ptr<NorthlightGpuFrameTimer> gpuTimer;NorthlightGpuBudget::Controller gpuBudget;std::int64_t gpuBudgetQpc=0;double gpuBudgetLastMs=0;
    // task 45: the GPU-bound signal (smoothed per-frame latency wait) and the CPU budget (ActorShadowCpuMs). cpuEffectsQpc: this frame's effects span in QPC ticks
    // (0 = the effects did not run), always measured: two QueryPerformanceCounter reads per applying frame; unlike cpuEffects it is not reset by the sample logging.
    NorthlightGpuBudget::WaitSignal gpuBound,replayWait;std::uint64_t replayWaitPrevNs=0,replaySkipPrev=0;std::int64_t boundQpc=0,cpuBudgetQpc=0,cpuEffectsQpc=0;NorthlightGpuBudget::Controller cpuBudget;double cpuBudgetLastMs=0;bool boundKnown=false,gpuLimited=true,cpuLimited=true,replayKnown=false;
    std::unique_ptr<NorthlightMemoryDiagnostics::Sampler> memoryDiagnostics; /* also feeds the functional memory guard */
    NorthlightMemoryGuard::Guard memoryGuard;
    IDirect3DTexture9 *scene = nullptr, *depthTex = nullptr, *ao = nullptr, *aoRaw = nullptr;
    IDirect3DSurface9 *sceneSurface = nullptr, *aoSurface = nullptr, *aoRawSurface = nullptr, *worldDepth = nullptr;
    D3DSURFACE_DESC worldDepthDesc={};bool worldDepthDescKnown=false; // 0.3.196 (task 12): worldDepth's desc (a held reference, so the object and its desc are fixed); cleared when worldDepth is dropped
    IDirect3DPixelShader9 *aoPS = nullptr, *aoBlurPS = nullptr, *aoContactBloomPS = nullptr, *contactBloomPS = nullptr, *compositePS = nullptr;
    std::unique_ptr<NorthlightAOActorStencil> aoActorMask;
    IDirect3DPixelShader9 *aoActorPreparePS=nullptr, *aoActorPS=nullptr, *aoActorAlphaPS=nullptr;
    unsigned aoActorFrame=~0u;bool aoActorOn=false,aoActorDrawKnown=false,aoActorDrawSkin=false,aoActorDepthWrite=false,aoActorUsed=false;
    bool aoActorShaderFailed=false,aoActorStencilPrepared=false;
    LONGLONG cpuAoActorCapture=0;
    bool aoActorShaders(bool fallback){
        if(aoActorShaderFailed)return false;
        HRESULT hr=S_OK;
        if(!aoActorPreparePS)hr=ext->CreatePixelShader(kAoActorPrepareShader,&aoActorPreparePS);
        auto& shader=fallback?aoActorAlphaPS:aoActorPS;
        if(SUCCEEDED(hr)&&!shader)hr=ext->CreatePixelShader(fallback?(aoMethod==3?kGtaoActorOnlyShader:kHbaoActorOnlyShader):
            (aoMethod==3?kGtaoActorContactBloomShader:kHbaoActorContactBloomShader),&shader);
        if(SUCCEEDED(hr))return true;
        aoActorShaderFailed=true;drop(aoActorPreparePS);drop(aoActorPS);drop(aoActorAlphaPS);
        logf("AO actor shaders unavailable HRESULT=0x%08lx; original AO retained",(unsigned long)hr);return false;
    }
    void aoActorBeginFrame(){
        if(aoActorFrame==frame)return;
        const bool wasOn=aoActorOn;
        aoActorFrame=frame;aoActorOn=NorthlightAOActorPolicy::enabled(enabled&&!failed,contactAO,aoMethod,aoStrength,aoRadius);
        if(wasOn&&!aoActorOn)aoActorMask->reset();
        aoActorMask->beginFrame(aoActorOn);aoActorUsed=aoActorStencilPrepared=false;
        cpuAoActorCapture=0;
    }
    template<class Draw> HRESULT aoActorNativeDraw(Draw draw){
        if(!aoActorOn||applied||failed||!enabled)return draw();
        // Only the native draw is marked. Geometry replays, shadow captures and
        // material captures have already finished; their stencil is untouched.
        if(!aoActorDrawKnown){
            if(!aoActorMask->valid())return draw();
            // Fixed-function glow/blob draws have no classified VS, but do not
            // change scene depth. Keep ownership through those draws; an
            // unknown writer to this depth surface must retain baseline AO.
            bool skip=false;
            {CpuScope cost(sampledDrawTimers()?&cpuAoActorCapture:nullptr);
                DWORD z=TRUE,write=TRUE;
                if(FAILED(ext->GetRenderState(D3DRS_ZENABLE,&z))||FAILED(ext->GetRenderState(D3DRS_ZWRITEENABLE,&write)))skip=true;
                else skip=z&&write&&boundDepthIs(worldDepth);
                if(skip)aoActorMask->invalidate();
            }
            if(skip)return draw();
        }
        {
            CpuScope cost(sampledDrawTimers()?&cpuAoActorCapture:nullptr);
            if(aoActorDrawSkin&&aoActorDepthWrite&&!aoActorStencilPrepared){
                aoActorStencilPrepared=true;
                // A later NameMask full-stencil clear would erase ownership. Run
                // its pending clear before the first actor stamp; both masks then
                // write only their own bits (actor 0x40, floating names 0x80).
                if(nameMask.armed()&&nameMask.needsClear()&&worldDepthDescKnown&&worldDepthDesc.Format==D3DFMT_D24S8)
                    if(!nameMaskReady()||!nameMaskClear())aoActorMask->invalidate();
            }
            aoActorMask->beginDraw(aoActorDrawSkin,aoActorDepthWrite,true,backSurface,worldDepth);
        }
        const HRESULT result=draw();
        {CpuScope cost(sampledDrawTimers()?&cpuAoActorCapture:nullptr);aoActorMask->endDraw(result);}
        return result;
    }
    // Low bits: 1 terrain, 2 UI. kWaterTag: the water renderer holds a mask shader
    // for it (set at registration, where the water map changes), so non-water
    // draws need no second hash lookup.
    // Task 34: bits 3..5 hold the M2 shader family (m2_shader_families.h, NorthlightSpellLayer::Family) of the vertex shader, set at registration.
    static constexpr int kTagMask=3,kWaterTag=4,kFamilyShift=3;
    std::unordered_map<IDirect3DVertexShader9*, int> vsTags;
    bool drawWaterVS=false,drawUiHashVS=false; // beforeDraw() results for the current draw (drawUiHashVS: uiTextVs, the UI hash or the font mod's learned UI VS)
    std::unordered_map<IDirect3DPixelShader9*, int> psTags;
    std::unordered_map<IDirect3DVertexShader9*, uint64_t> vsHashes;
    std::unordered_map<IDirect3DVertexShader9*, unsigned> vsMajor; // 0.3.202 (rain mask MRT): the shader model major version from the version token (0 unknown)
    // 0.3.196 (task 12): one-entry per-draw vertex shader classification. vsTags, vsHashes and the world's shader maps change only in
    // CreateVertexShader registration, which bumps vsGeneration first (nothing is ever erased, but a freed address can be registered again).
    struct VsClass {IDirect3DVertexShader9* vs=nullptr;std::uint32_t gen=~0u;int entry=0;bool wmo=false,world=false,skinned=false;unsigned family=0;};
    std::uint32_t vsGeneration=0;VsClass lastVs;unsigned long long vsCacheHits=0,vsCacheMisses=0;
    const VsClass& classifyVs(IDirect3DVertexShader9* vs){
        if(lastVs.vs==vs&&lastVs.gen==vsGeneration){++vsCacheHits;return lastVs;}
        ++vsCacheMisses;auto it=vsTags.find(vs);lastVs.vs=vs;lastVs.gen=vsGeneration;lastVs.entry=it==vsTags.end()?0:it->second;
        lastVs.wmo=world&&world->recognizesWmo(vs);lastVs.world=world&&world->isWorldShader(vs);lastVs.skinned=world&&world->isSkinnedShader(vs);lastVs.family=unsigned(lastVs.entry>>kFamilyShift)&7u;return lastVs;
    }
    std::unordered_map<IDirect3DPixelShader9*, uint64_t> psHashes;
    std::unordered_map<void*, NorthlightShaderReads::ReadSet> shaderReads; // vertex and pixel shaders by raw pointer: what each can read of the API state (scanned at creation, only with SpellLayer=1 (spellOn is fixed at device creation and only ever drops to false, so no shader is missed); a re-created shader at a freed address overwrites its entry, like vsHashes; the spell layer records only that)
    std::unordered_map<IDirect3DVertexShader9*, uint8_t> vsFogProven; // vertex shaders whose fog output is provably the c30 ramp (spell_fog_proof.h, scanned at creation; SpellLayer=1 only)
    NorthlightSpellFog::Freshness spellFogFreshness; // has the world render published a new fog estimate since the last frame end?
    std::unordered_map<IDirect3DPixelShader9*, int> psFogReg; // ps_3_0 shaders: the proven fog colour constant of the combiner epilogue (legacy_fog.h ps3FogColorRegister), -1 unknown (SpellLayer=1 only)
    std::unordered_map<IDirect3DPixelShader9*, unsigned> psMajor; // the shader model major version from the version token (0 unknown); the spell layer admits ps_2_0 and newer only
    unsigned blobSignatureReports=0;
    NorthlightMirrorAuditSchedule mirrorAuditSchedule;bool mirrorFallbackReported=false;
    UINT width = 0, height = 0;
    D3DFORMAT sceneFormat = D3DFMT_UNKNOWN, depthSnapshotFormat = D3DFMT_UNKNOWN;
    bool df24Refused = false; // the device refused a DF24 depth snapshot: INTZ for the session
    bool terrain = false, captured = false, applied = false, enabled = true;
    // 0.3.202 (rain): the rain streak alpha mask (RainMask, s13 of WorldComposite): created on the first mask draw at the effect resources' size; the
    // flags and the cached eligibility are per frame (clearFrame), the counters are read and zeroed by the WEATHER line.
    // 0.3.202 (rain mask MRT): the mask is written by the rain draws themselves through render target 1 (rainMrtPS); rainMrtBound: RT1 holds the mask target
    // (unbound lazily, rainMrtUnbind); rainDepth: the depth at the frame's first mask draw (INTZ), compared with the final depth by rainScrubPS in renderEffects.
    IDirect3DTexture9* rainMask=nullptr;IDirect3DSurface9* rainMaskSurface=nullptr;bool rainMaskFailed=false;IDirect3DSurface9* rainMaskMS=nullptr;D3DMULTISAMPLE_TYPE rainMaskMsType=D3DMULTISAMPLE_NONE;DWORD rainMaskMsQuality=0;D3DMULTISAMPLE_TYPE rainMaskWantType=D3DMULTISAMPLE_NONE;DWORD rainMaskWantQuality=0;bool rainMaskResolveLogged=false;HRESULT rainMaskResolveHr=S_OK;
    bool rainMaskCleared=false,rainMaskDrawn=false,rainMaskFrame=false,rainMaskOk=false;unsigned rainMaskDraws=0;bool rainMaskMismatchLogged=false; /* rainMaskCleared: this frame's mask start (depth snapshot + clear) ran */
    IDirect3DTexture9* rainDepth=nullptr;IDirect3DPixelShader9 *rainMrtPS=nullptr,*rainScrubPS=nullptr;bool rainMrtBound=false,rainDepthOk=false,rainCapsChecked=false,rainCapsOk=false,rainSkipLogged=false;unsigned rainMrtRuns=0,rainMaskSkips=0;DWORD rainMrtPrev[1]={};bool rainMrtPrevKnown[1]={};
    /* 0.3.203 (task 17): fallback effect boundary when the UI shaders are replaced by another module (font mods); learned from draw state, see uiFallbackDraw. */
    NorthlightUiBoundary::Arming uiFallback;bool hashBoundaryThisFrame=false,fallbackBoundaryThisFrame=false,uiFallbackNoCandidateLogged=false;unsigned uiFallbackBoundaries=0;std::uint64_t uiFallbackPublished=0; /* the value last stored in NorthlightStream::learnedUiVsHash */
    IDirect3DVertexShader9* triageVs=nullptr;IDirect3DPixelShader9* triagePs=nullptr;DWORD triageZw=0;float triageC3[4]={};unsigned triageDraws=0; /* most recent post-terrain draw rejected by zwrite or the c3/rows test while learning (identities only, never dereferenced) */
    bool failed = false, projectionValid = false, key10 = false, key12=false;
    // 0.3.200 (frame markers): diagnostics only, while northlight-frame-markers.txt exists in the game folder at device creation: two 40x40
    // squares at the left edge whose colour cycles with the frame number. E (upper) is filled right after the world effects, P (lower) right
    // before the real Present, so a screen recording shows which presented images went through the effects and the replay's Present, in order.
    bool frameMarkers=false;
public:
    // 0.3.200 (pipeline): with StreamFramesAhead >= 2 the replay thread never waits for the game, so the graphics card queue filled with
    // frames (DXVK allows BackBufferCount+1 in flight) and on macOS the window showed images still being written: horizontal tear bands
    // from frames several apart, a flicker while moving (gone with d3d9.maxFrameLatency = 1). A non-Ex device cannot ask DXVK for that,
    // so the replay does it: before each Present it waits until the graphics card finished the previous frame (one EVENT query issued
    // right after every Present). The game thread is not involved; it stays its frames ahead.
    bool limitGpuLatency=false;
    const NorthlightStream::Counters* streamStats=nullptr; // task 45: the command stream's counters (null without CommandStream), read-only for the CPU budget
private:
    IDirect3DQuery9* latencyQuery=nullptr;bool latencyPending=false,latencyFailed=false;std::uint64_t latencyWaits=0;double latencyWaitMs=0;double latencyLastMs=0; /* task 45: the last Present's wait, not reset by logging */
    void waitPreviousFrameGpu(){
        latencyLastMs=0;
        if(!latencyPending||!latencyQuery)return;latencyPending=false;
        if(latencyQuery->GetData(nullptr,0,D3DGETDATA_FLUSH)!=S_FALSE)return;
        const std::int64_t t0=qpcNow();const double perMs=cpuFrequency.QuadPart>0?1000.0/double(cpuFrequency.QuadPart):0;
        while(latencyQuery->GetData(nullptr,0,D3DGETDATA_FLUSH)==S_FALSE){if(double(qpcNow()-t0)*perMs>100.0)break;SwitchToThread();} /* a lost device or a stuck query never holds the replay */
        latencyLastMs=double(qpcNow()-t0)*perMs;++latencyWaits;latencyWaitMs+=latencyLastMs;
    }
    void markFrameGpu(){
        if(!latencyQuery&&!latencyFailed&&(FAILED(ext->CreateQuery(D3DQUERYTYPE_EVENT,&latencyQuery))||!latencyQuery)){latencyQuery=nullptr;latencyFailed=true;logf("CSTREAM GPU latency limit unavailable: no event query");}
        if(latencyQuery&&SUCCEEDED(latencyQuery->Issue(D3DISSUE_END)))latencyPending=true;
    }
    void frameMarker(unsigned slot,unsigned n){
        IDirect3DSurface9* bb=nullptr;if(FAILED(ext->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb))||!bb)return;
        static const D3DCOLOR palette[2][4]={{0xffff0000,0xff00ff00,0xff0000ff,0xffffffff},{0xffffff00,0xff00ffff,0xffff00ff,0xffff8000}};
        D3DSURFACE_DESC d={};bb->GetDesc(&d);const LONG top=LONG(d.Height/2)+LONG(slot)*50;
        const RECT r{0,top,40,top+40};ext->ColorFill(bb,&r,palette[slot&1][n&3]);
        bb->Release();
    }
    int traceWorld=-1; /* 0.3.200 (frame trace): this frame's world->render result (-1 not called, 0 skipped, 1 drawn) */
    unsigned frame = 0, appliedFrames = 0, matchedTerrain = 0, matchedUI = 0, projectionRejects = 0;
    unsigned worldSkippedFrames=0;
    // 0.3.169: one line per run of skipped world frames, with the first and last skipReason().
    unsigned worldSkipRun=0,worldSkipEpisodes=0;DWORD worldSkipStart=0;const char* worldSkipFirst="";const char* worldSkipLast="";
    unsigned nonWorldCaptureRejects=0;
    unsigned drawCalls=0, missingVS=0, terrainDraws=0, terrainShadowDraws=0, viewportRejects=0, depthRejects=0, uiDraws=0, uiAfterTerrain=0;
    unsigned viewportReports=0;
    unsigned postEffectWorldDraws=0,postEffectSkinnedDraws=0;
    // 0.3.188 (task 3): per-sample-frame census of the early depth resolve, read-only, logged as TRANSLUCENT. Positions are 1-based census draw
    // indices (0 = none), over the whole frame (with a Clear(Z) between world passes read them against clearResolve): lastOpaqueZ is the last
    // non-water Z-writing draw writing RGB effectively opaque (the undo rule); clearResolve marks a Clear(Z)-triggered resolve.
    unsigned censusDraws=0,lastOpaqueZAt=0,clearResolveAt=0;
    // 0.3.188 (task 3) early depth for translucent actors (translucent_depth.h), always on: the depth resolve runs before the first translucent skinned Z-writing draw.
    // Undo: while earlyDepth.earlyCaptured and `captured`, a terrain draw (beforeDraw) or a later effectively opaque Z+RGB or water Z draw (prepareDrawImpl) clears `captured`,
    // so the UI-time resolve redoes the depth, counts earlyResolveUndone and re-arms the latch (capped at Frame::kMaxAttempts per frame). A Clear(Z) resolve sets `captured` without earlyCaptured, so it is never counted.
    unsigned earlyResolves=0,earlyResolveAt=0,earlyResolveTotal=0,earlyResolveUndone=0,earlyResolveUndoneTotal=0;NorthlightTranslucentDepth::Frame earlyDepth;
    // 0.3.212 (task 32) NameMask (name_stencil.h): a floating-name draw (UI-hash vertex shader with a perspective projection, before the boundary) is drawn by the game with stencil REPLACE 0x80 enabled (the run of
    // consecutive names keeps the states set); after the frame's first mark, later depth-final draws clear it where they land; WorldComposite (and the legacy composite) rejects marked pixels with the world depth-stencil bound, so names keep the game's colour. Creation-time keys.
    static_assert(unsigned(NorthlightNameStencil::ZEnable)==unsigned(D3DRS_ZENABLE)&&unsigned(NorthlightNameStencil::ZWriteEnable)==unsigned(D3DRS_ZWRITEENABLE)&&unsigned(NorthlightNameStencil::AlphaTestEnable)==unsigned(D3DRS_ALPHATESTENABLE)&&
                unsigned(NorthlightNameStencil::AlphaRef)==unsigned(D3DRS_ALPHAREF)&&unsigned(NorthlightNameStencil::AlphaFunc)==unsigned(D3DRS_ALPHAFUNC)&&unsigned(NorthlightNameStencil::StencilEnable)==unsigned(D3DRS_STENCILENABLE)&&
        unsigned(NorthlightNameStencil::StencilFail)==unsigned(D3DRS_STENCILFAIL)&&unsigned(NorthlightNameStencil::StencilZFail)==unsigned(D3DRS_STENCILZFAIL)&&unsigned(NorthlightNameStencil::StencilPass)==unsigned(D3DRS_STENCILPASS)&&
        unsigned(NorthlightNameStencil::StencilFunc)==unsigned(D3DRS_STENCILFUNC)&&unsigned(NorthlightNameStencil::StencilRef)==unsigned(D3DRS_STENCILREF)&&unsigned(NorthlightNameStencil::StencilMask)==unsigned(D3DRS_STENCILMASK)&&
        unsigned(NorthlightNameStencil::StencilWriteMask)==unsigned(D3DRS_STENCILWRITEMASK)&&
        unsigned(NorthlightNameStencil::TwoSidedStencilMode)==unsigned(D3DRS_TWOSIDEDSTENCILMODE),"name stencil render state ids");
    static_assert(NorthlightNameStencil::kBlendSrcAlpha==D3DBLEND_SRCALPHA&&NorthlightNameStencil::kBlendSrcColor==D3DBLEND_SRCCOLOR&&NorthlightNameStencil::kCmpGreater==D3DCMP_GREATER&&NorthlightNameStencil::kCmpNotEqual==D3DCMP_NOTEQUAL&&
        NorthlightNameStencil::kCmpAlways==D3DCMP_ALWAYS&&NorthlightNameStencil::kOpKeep==D3DSTENCILOP_KEEP&&NorthlightNameStencil::kOpReplace==D3DSTENCILOP_REPLACE&&
        NorthlightTranslucentDepth::kBlendZero==D3DBLEND_ZERO&&NorthlightNameStencil::kBit==0x80,"name stencil values");
    /* Cost of the mask on sample frames only (sampledDrawTimers), CpuScopes like the other per-draw costs: the decision (every 8th world-domain call timed, scaled by calls/timed, so the timer does not dominate a
       cheap test), the name runs (begin, alpha guard, end) and the unmark state wraps. Gross: each scope includes its two clock reads (qpcNs on the CPU timers line). The composite bind is once per frame and not timed. */
    LONGLONG cpuNameDecide=0,cpuNameMark=0,cpuNameUnmark=0;unsigned nameDecideCalls=0,nameDecideTimed=0;bool nameRun=false; /* a run of consecutive name draws keeps STENCILENABLE on (the other stencil states of kMark stay set) */
    NorthlightNameStencil::Mask nameMask;int maskKind=0; /* maskKind: this draw is 1 a name that gets marked, 2 a depth-final draw that unmarks (set by prepareDrawImpl, consumed by nameMaskDraw) */
    IDirect3DSurface9* maskFormatFor=nullptr;unsigned long maskFormatValue=0;unsigned maskFormatLogs=0;bool maskStencil=false;const void* maskDsId=nullptr; /* identities only: the world depth the format verdict is for, and the one whose Clear(Z) also clears stencil */
    void resetTranslucentCensus(){censusDraws=lastOpaqueZAt=clearResolveAt=earlyResolves=earlyResolveAt=earlyResolveUndone=0;}
    LONGLONG cpuPrep=0,cpuCapture=0,cpuWaterCapture=0,cpuEffects=0,cpuMirrorAudit=0;
    NorthlightEffectsBuckets::Frame effectsBuckets;bool effectsBucketed=false; /* 0.3.175 (S3): this sample frame's effects split */
    unsigned long long cpuCaptureReads=0; /* 0.3.150: timer clock reads inside cpuCapture, sample frames (an outer pair adds one) */
    LARGE_INTEGER cpuFrequency={};
    // 0.3.149 (RenderProfile=1): per-frame wall times at Present by frame kind, in windows that
    // follow the DiagReplayProbe windows, and per-type extension call counts of profile sample
    // frames (DeviceMirror::rawMethodCalls).
    NorthlightRenderThreadProbe::FrameCostWindow frameCost;uint64_t frameCostWindow=0;NorthlightRenderThreadProbe::ProbeMode frameCostMode=NorthlightRenderThreadProbe::ProbeOff;
    NorthlightFrameStalls::Tracker stallFrames;NorthlightFrameStalls::Frame stallFrame;LONGLONG stallScene=0;std::uint64_t stallSceneIdle=0;
    unsigned frameStartDrawCalls=0;std::uint64_t frameStartAnswered=0,frameStartForwarded=0;unsigned long long frameStartScopeReads=0;
    // 0.3.154 drawGate A/B, RenderProfile sample frames only (both flags set once per frame in
    // finishFrameImpl from profiling(); RenderProfile=0 leaves them false). Sample frames alternate
    // T (today's timers) and U (the per-draw prep/capture/water-mask CpuScopes off); both log
    // sceneMs (previous Present done -> renderEffects entry) and exact counts: DRAWGATE ab.
    bool gateFrame=false,gateUntimed=false;
    struct GateCounts {unsigned prep=0,capture=0,water=0,wmo=0,fullPasses=0,audits=0,blobCalls=0,blobTextures=0,blobClaimed=0,bufferCreates=0,processVertices=0;};
    struct GateStart {unsigned missingVS=0,terrain=0,ui=0,shadowSwaps=0;std::uint64_t generations=0,vsHits=0,vsMisses=0;};
    GateCounts gateCounts;GateStart gateStart;
    LARGE_INTEGER gateSceneEnd={};LONGLONG gatePresentDone=0;unsigned gateSceneDraws=0;unsigned long long gateSceneReads=0;
    std::recursive_mutex gateBenchLock; /* private, uncontended: the microbenchmark's lock */
    MirrorGate gateBenchGate; /* 0.3.180: private, for ownerNs (0.3.182: the real owner entry and exit on it) */
    const int debugMode = 0;int worldDebug=0;
    NorthlightEffectSwitches::Hotkeys effectKeys;
    float nearZ = .1f, farZ = 1000.f, scaleX = 1.f, scaleY = 1.f, fogForward = 1.f;
    float worldMinDepth=0.f,worldMaxDepth=1.f;

    // Diagnostic sample frame (every 120th; RenderProfile=1: every 127th, which reaches
    // every Near/FarShadowInterval phase, render_thread_probe.h): timing scopes, counters,
    // periodic lines. Never a rendering decision; Diagnostics=0 turns every sample off.
    bool sampled()const{return NorthlightRenderThreadProbe::sampleFrame(frame)&&NorthlightDiagnostics::enabled();}
    static bool diagnostics(){return NorthlightDiagnostics::enabled();}
    // 0.3.154: the per-draw timing scopes; off on drawGate U frames (gateUntimed needs RenderProfile).
    bool sampledDrawTimers()const{return NorthlightRenderThreadProbe::profiling()&&sampled()&&!gateUntimed;}
    // stage is read when a fault is reported (0.3.187: drawHook names the step that was running).
    template<class Work> bool extensionWork(const char* const& stage,Work&& work) noexcept {
        if(extensionFault)return false;
        return NorthlightExtensionGuard::run(std::forward<Work>(work),[&](NorthlightExtensionGuard::Fault fault) noexcept {
            extensionFault=true;failed=true;enabled=false;if(spellOn){spell.reset();spellSync();} /* task 34: nothing pending survives a fault */
            MEMORYSTATUSEX memory={};memory.dwLength=sizeof memory;GlobalMemoryStatusEx(&memory);
            logf("EXTENSION fault stage=%s type=%u availableVirtualMiB=%llu; original game calls retained; restart required",stage,unsigned(fault),(unsigned long long)(memory.ullAvailVirtual>>20));
        });
    }
    void clearFrame() {
        if(gpuSceneProfile)gpuSceneProfile->endFrame();
        gpuFrameStarted=gpuSceneSplit=gpuEffectsSplit=false;
        if(!applied||failed)stateBlocks.clear();
        if(!enabled||extensionFault){stateBlocks.clear();if(world)world->releaseStateCache();if(water)water->releaseStateCache();}
        if(world)world->endFrame(!extensionFault);if(water)water->endFrame();if(celestialDiscs)celestialDiscs->endFrame();if(shadowBlobs)shadowBlobs->endFrame();
        drop(worldDepth);worldDepthDescKnown=false; rainMrtUnbind();terrain = captured = applied = projectionValid = false;rainMaskCleared=rainMaskDrawn=rainMaskFrame=rainMaskOk=rainDepthOk=false; /* 0.3.202 (rain mask MRT): RT1 never outlives a frame */earlyDepth.reset();resetTranslucentCensus(); /* the TRANSLUCENT line is logged before clearFrame */
    }
    void releaseResources() {
        if(aoActorMask)aoActorMask->reset();aoActorFrame=~0u;aoActorOn=false;
        drop(aoActorPreparePS);drop(aoActorPS);drop(aoActorAlphaPS);aoActorShaderFailed=false;
        rainMrtUnbind();stateBlocks.clear();clearFrame();maskFormatFor=nullptr;maskFormatValue=0;maskStencil=false;maskDsId=nullptr; /* 0.3.212 (task 32): the depth may change with the device */
        drop(sceneSurface); drop(aoSurface); drop(aoRawSurface); drop(scene); drop(depthTex); drop(ao); drop(aoRaw);drop(rainMaskMS);drop(rainMaskSurface);drop(rainMask);drop(rainDepth);rainMaskFailed=false; /* 0.3.202 (rain) */
        drop(aoPS); drop(aoBlurPS); drop(aoContactBloomPS); drop(contactBloomPS); drop(compositePS);drop(rainMrtPS);drop(rainScrubPS); width = height = 0;
    }
    bool error(HRESULT hr, const char* stage) {
        if (SUCCEEDED(hr)) return false;
        if (!failed) logf("DISABLED: %s failed HRESULT=0x%08lx", stage, (unsigned long)hr);
        failed = true; return true;
    }
    bool resources(UINT w, UINT h, D3DFORMAT format) {
        // DXVK 3's 1x depth+stencil RESZ copy can leave INTZ unchanged (proved
        // with a nonzero sentinel). DF24 copies depth alone, which is all our
        // snapshots consume; NameMask still uses the original world stencil.
        // DXVK 2.7.1 has the same ResolveZ and the same copy-path choice (a depth+stencil copy goes through the
        // shader copy on AMD drivers, depth alone through vkCmdCopyImage), so both packaged DXVKs take DF24.
        // A frame whose world depth was not bound (bindWorldDepth failed after projectionValid) keeps the current
        // format: deciding INTZ there recreated every effect resource, and DF24 again on the next frame. A DF24 the
        // device refused stays INTZ for the session (df24Refused).
        const D3DFORMAT intz=(D3DFORMAT)MAKEFOURCC('I','N','T','Z'),df24=(D3DFORMAT)MAKEFOURCC('D','F','2','4');
        const D3DFORMAT depthFormat=!worldDepthDescKnown&&depthSnapshotFormat!=D3DFMT_UNKNOWN?depthSnapshotFormat
            :NorthlightBackend::isPackagedDxvk(selectedBackend)&&!df24Refused&&worldDepthDescKnown&&worldDepthDesc.MultiSampleType==D3DMULTISAMPLE_NONE?df24:intz;
        // Method switches replace shaders while keeping the existing scene/depth targets.
        const DWORD* aoContactShaders[]={kAoContactBloomShader,kHdaoContactBloomShader,kHbaoPlusContactBloomShader,kGtaoContactBloomShader};
        if (!aoPS && error(ext->CreatePixelShader(kAoShader, &aoPS), "AO shader")) return false;
        if (!aoContactBloomPS && error(ext->CreatePixelShader(aoContactShaders[aoMethod], &aoContactBloomPS), "AO contact bloom shader")) return false;
        if (!aoBlurPS && error(ext->CreatePixelShader(kAoBlurShader, &aoBlurPS), "AO blur shader")) return false;
        if (!contactBloomPS && error(ext->CreatePixelShader(kContactBloomShader, &contactBloomPS), "contact bloom shader")) return false;
        if (!compositePS && error(ext->CreatePixelShader(kCompositeShader, &compositePS), "composite shader")) return false;
        if (sceneSurface && aoSurface && aoRawSurface && depthTex && w == width && h == height && format == sceneFormat && depthFormat == depthSnapshotFormat) return true;
        // Resource replacement must not invalidate this frame's retained depth surface.
        drop(sceneSurface); drop(scene);
        if (w!=width || h!=height || depthFormat!=depthSnapshotFormat) {
            rainMrtUnbind();rainDepthOk=rainMaskCleared=rainMaskDrawn=false;rainMaskFailed=false;
            drop(aoSurface); drop(ao); drop(aoRawSurface); drop(aoRaw); drop(depthTex); captured=false;drop(rainMaskMS);drop(rainMaskSurface);drop(rainMask);drop(rainDepth); /* 0.3.202 (rain): the mask follows the effect size */
        }
        width = w; height = h; sceneFormat = format;depthSnapshotFormat=depthFormat;
        if (error(ext->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &scene, nullptr), "scene texture")) return false;
        if (!depthTex) {
            HRESULT hr=ext->CreateTexture(w, h, 1, D3DUSAGE_DEPTHSTENCIL, depthSnapshotFormat, D3DPOOL_DEFAULT, &depthTex, nullptr);
            // A DXVK without the DF formats (d3d9.supportDFFormats off) refuses DF24: INTZ, not the effects disabled.
            if (FAILED(hr) && depthSnapshotFormat==df24) {
                logf("Depth snapshot DF24 refused HRESULT=0x%08lx: INTZ for this session", (unsigned long)hr);
                df24Refused=true;depthSnapshotFormat=intz;
                hr=ext->CreateTexture(w, h, 1, D3DUSAGE_DEPTHSTENCIL, depthSnapshotFormat, D3DPOOL_DEFAULT, &depthTex, nullptr);
            }
            if (error(hr, "depth snapshot texture")) return false;
        }
        if (!ao && error(ext->CreateTexture(w/2, h/2, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &ao, nullptr), "half resolution AO texture")) return false;
        if (!aoRaw && error(ext->CreateTexture(w/2, h/2, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &aoRaw, nullptr), "half resolution raw AO texture")) return false;
        if (error(scene->GetSurfaceLevel(0, &sceneSurface), "scene surface")) return false;
        if (!aoSurface && error(ao->GetSurfaceLevel(0, &aoSurface), "AO surface")) return false;
        if (!aoRawSurface && error(aoRaw->GetSurfaceLevel(0, &aoRawSurface), "raw AO surface")) return false;
        logf("Resources %ux%u, AO %ux%u, format=%u depthSnapshot=%s sourceMSAA=%u", w,h,w/2,h/2,unsigned(format),depthSnapshotFormat==(D3DFORMAT)MAKEFOURCC('D','F','2','4')?"DF24":"INTZ",unsigned(worldDepthDesc.MultiSampleType));
        return true;
    }
    // Swap chain 0's back buffer description only changes through Reset.
    static constexpr bool kCacheBackBufferDesc=true;
    // 0.3.196 (task 12): backSurface is swap chain 0's back buffer identity (no reference; cleared with backDescKnown in Reset, which frees it).
    D3DSURFACE_DESC backDesc={};bool backDescKnown=false;IDirect3DSurface9* backSurface=nullptr;
    // quiet: a probe (the UI boundary fallback while learning) that neither counts viewportRejects nor spends the VIEWPORT GATE log lines.
    bool fullViewport(D3DSURFACE_DESC& desc,D3DVIEWPORT9* viewport=nullptr,bool quiet=false) {
        desc={};
        IDirect3DSurface9* rt = nullptr;
        HRESULT rtHR;
        if(ext->peekRenderTarget(0,rt)){ // borrowed: no device write before GetDesc
            if(kCacheBackBufferDesc&&backDescKnown&&backSurface&&rt==backSurface){desc=backDesc;rtHR=D3D_OK;} // the render target is the back buffer itself: same object, same desc
            else rtHR=rt->GetDesc(&desc);
        }
        else{
            rtHR=ext->GetRenderTarget(0,&rt);
            if (SUCCEEDED(rtHR)&&rt) {rtHR=rt->GetDesc(&desc);drop(rt);}
            else rtHR=D3DERR_NOTFOUND;
        }
        D3DSURFACE_DESC bd={};
        HRESULT backHR=D3D_OK;
        if(kCacheBackBufferDesc&&backDescKnown)bd=backDesc;
        else{
            IDirect3DSurface9* back = nullptr;
            backHR=ext->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
            if(SUCCEEDED(backHR)&&back){backHR=back->GetDesc(&bd);backSurface=back;drop(back);}
            else backHR=D3DERR_NOTFOUND;
            if(SUCCEEDED(backHR)){backDesc=bd;backDescKnown=true;}else backSurface=nullptr;
        }
        D3DVIEWPORT9 vp={};HRESULT vpHR=ext->GetViewport(&vp);
        bool ok=SUCCEEDED(rtHR)&&SUCCEEDED(backHR)&&SUCCEEDED(vpHR)&&desc.Width>=640&&desc.Height>=360&&
            desc.Width == bd.Width && desc.Height == bd.Height && vp.X == 0 && vp.Y == 0 &&
            vp.Width == desc.Width && vp.Height == desc.Height &&
            std::isfinite(vp.MinZ)&&std::isfinite(vp.MaxZ)&&vp.MinZ>=0&&vp.MaxZ<=1&&vp.MaxZ>vp.MinZ;
        if(viewport)*viewport=vp;
        if(!ok&&!quiet){
            ++viewportRejects;
            if(viewportReports++<8)logf("VIEWPORT GATE: rt=%ux%u fmt=%u msaa=%u back=%ux%u vp=(%u,%u %ux%u z=%.9g..%.9g) HRESULTs=%08lx,%08lx,%08lx",desc.Width,desc.Height,unsigned(desc.Format),unsigned(desc.MultiSampleType),bd.Width,bd.Height,vp.X,vp.Y,vp.Width,vp.Height,vp.MinZ,vp.MaxZ,(unsigned long)rtHR,(unsigned long)backHR,(unsigned long)vpHR);
        }
        return ok;
    }
    bool readProjection(bool wmo=false) {
        float p[16],raw[16];
        if (FAILED(ext->GetVertexShaderConstantF(wmo?2:4, raw, 4))) return false;
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)p[r*4+c]=wmo?raw[c*4+r]:raw[r*4+c];
        // Verified Terrain shaders: clip = view.x*c4 + view.y*c5 + view.z*c6 + view.w*c7.
        Projection q;
        if (!decodeProjection(p,q)) return false;
        D3DVIEWPORT9 vp={};
        if(FAILED(ext->GetViewport(&vp))||!std::isfinite(vp.MinZ)||!std::isfinite(vp.MaxZ)||vp.MinZ<0||vp.MaxZ>1||vp.MaxZ<=vp.MinZ)return false;
        nearZ=q.nearZ; farZ=q.farZ; scaleX=q.scaleX; scaleY=q.scaleY;fogForward=p[11];
        worldMinDepth=vp.MinZ;worldMaxDepth=vp.MaxZ;
        return true;
    }
    bool resolveDepth() {
        if (captured) return true;
        if (!terrain || !worldDepth || !depthTex || !projectionValid) return false;
        if (!resolveDepthInto(depthTex, true)) return false;
        if(aoActorOn){CpuScope cost(sampledDrawTimers()?&cpuAoActorCapture:nullptr);aoActorMask->freeze(worldDepth);}
        captured = true; return true;
    }
    // 0.3.202 (rain mask MRT): resolveDepth's copy (the RESZ protocol below) into a depth snapshot texture. fatal: a failed step disables the effects (depthTex);
    // otherwise it only returns false (the rain depth snapshot). RT1 is unbound first: SavedState would restore it under a different draw.
    bool resolveDepthInto(IDirect3DTexture9* target, bool fatal) {
        rainMrtUnbind();
        if (!terrain || !worldDepth || !target || !projectionValid) return false;
        SavedState saved(ext,&stateBlocks);
        if (!saved.ok) return false;
        auto step=[&](HRESULT hr,const char* stage){return fatal?error(hr,stage):FAILED(hr);};
        // D9VK implements AMD RESZ: POINTSIZE magic 0x7fa05000 copies/resolves
        // the bound depth surface into texture slot 0, including MSAA sample zero.
        if (step(ext->SetDepthStencilSurface(worldDepth), "bind scene depth")) return false;
        if (step(ext->SetTexture(0, target), "bind depth resolve target")) return false;
        if(selectedBackend==NorthlightBackend::Kind::Native){
            // AMD's RESZ protocol requires a dummy draw to flush sampler binding
            // through the native D3D9 runtime. Disable ALL writes, then SavedState
            // restores the game state (including stream 0 changed by DrawPrimitiveUP).
            if(step(ext->SetVertexShader(nullptr),"RESZ dummy VS")||step(ext->SetPixelShader(nullptr),"RESZ dummy PS")||
               step(ext->SetFVF(D3DFVF_XYZ),"RESZ dummy FVF"))return false;
            const D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3};
            for(auto state:states)if(step(ext->SetRenderState(state,0),"RESZ disable writes"))return false;
            if(step(ext->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1),"RESZ sampler op")||
               step(ext->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE),"RESZ sampler arg")||
               step(ext->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE),"RESZ stage1"))return false;
            const float dummy[3]={0,0,0};
            if(step(ext->DrawPrimitiveUP(D3DPT_POINTLIST,1,dummy,sizeof dummy),"RESZ dummy draw"))return false;
        }
        ext->SetRenderState(D3DRS_POINTSIZE, 0x3f800000); // trigger on a state change
        if (step(ext->SetRenderState(D3DRS_POINTSIZE, 0x7fa05000), "RESZ")) return false;
        return true;
    }
    HRESULT quad(UINT w, UINT h) {
        struct Vertex { float x,y,z,rhw,u,v; };
        Vertex v[]={{-.5f,-.5f,0,1,0,0},{float(w)-.5f,-.5f,0,1,1,0},
                    {-.5f,float(h)-.5f,0,1,0,1},{float(w)-.5f,float(h)-.5f,0,1,1,1}};
        D3DVIEWPORT9 vp={0,0,w,h,0,1};
        HRESULT hr=ext->SetViewport(&vp); if (FAILED(hr)) return hr;
        return ext->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex));
    }
    // the zone/time glow hue the world renderer publishes (sun_hue.h), in the disc renderer's terms.
    NorthlightCelestialGlow::Hue celestialGlowHue()const{
        // The moon tint holds independently of the sun data (moonStrength); an
        // invalid sun read keeps today's sun colour (strength 0).
        NorthlightCelestialGlow::Hue hue;const auto& published=world->glowHue();
        for(unsigned k=0;k<3;++k)hue.moon[k]=published.moon[k];hue.moonStrength=published.moonStrength;
        if(!published.valid)return hue;
        for(unsigned k=0;k<3;++k){hue.sun[k]=published.sun[k];hue.sunCore[k]=published.sunCore[k];}
        hue.strength=published.strength;return hue;
    }
    // Fixed-function, render and sampler state of the effect passes (AO, legacy composite).
    // 0.3.174: also re-established before a legacy composite after a world render.
    void effectState() {
        ext->SetDepthStencilSurface(nullptr);
        for (int i=1; i<4; ++i) ext->SetRenderTarget(i,nullptr);
        ext->SetVertexShader(nullptr);
        ext->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        ext->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);
        ext->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        ext->SetRenderState(D3DRS_WRAP0,0);
        ext->SetStreamSourceFreq(0,1);
        DWORD pointSize=0;ext->GetRenderState(D3DRS_POINTSIZE,&pointSize);
        if (pointSize==MAKEFOURCC('A','2','M','1'))
            ext->SetRenderState(D3DRS_POINTSIZE,MAKEFOURCC('A','2','M','0'));
        const struct { D3DRENDERSTATETYPE state; DWORD value; } states[] = {
            {D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},
            {D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},
            {D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},
            {D3DRS_LIGHTING,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FILLMODE,D3DFILL_SOLID},
            {D3DRS_COLORWRITEENABLE,15},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},
            {D3DRS_MULTISAMPLEMASK,0xffffffff},{D3DRS_MULTISAMPLEANTIALIAS,TRUE}
        };
        for (auto s:states) ext->SetRenderState(s.state,s.value);
        for (int i=0;i<4;++i) {
            ext->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
            ext->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            ext->SetSamplerState(i,D3DSAMP_MINFILTER,i?D3DTEXF_POINT:D3DTEXF_LINEAR);
            ext->SetSamplerState(i,D3DSAMP_MAGFILTER,i?D3DTEXF_POINT:D3DTEXF_LINEAR);
            ext->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
            ext->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE);
        }
    }
    // 0.3.212 (task 32) NameMaskDebug: the stencil mask as 50 percent magenta over the finished frame. One full-screen quad into the game's target with the world depth-stencil bound, ZENABLE off, stencil EQUAL 0x80 (nothing written),
    // alpha blended, fixed function (the vertex colour, no pixel shader). effectState() puts the effect passes' state back afterwards; SavedState restores the game's.
    void nameMaskDebugView(IDirect3DSurface9* target){
        if(!target||!worldDepth||FAILED(ext->SetRenderTarget(0,target))||FAILED(ext->SetDepthStencilSurface(worldDepth)))return;
        const struct{D3DRENDERSTATETYPE state;DWORD value;} states[]={{D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,TRUE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},
            {D3DRS_SRCBLEND,D3DBLEND_SRCALPHA},{D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA},{D3DRS_BLENDOP,D3DBLENDOP_ADD},{D3DRS_COLORWRITEENABLE,15},{D3DRS_STENCILENABLE,TRUE},{D3DRS_STENCILFUNC,D3DCMP_EQUAL},{D3DRS_STENCILPASS,D3DSTENCILOP_KEEP},
            {D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP},{D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP},{D3DRS_STENCILREF,NorthlightNameStencil::kBit},{D3DRS_STENCILMASK,NorthlightNameStencil::kBit},{D3DRS_STENCILWRITEMASK,0},{D3DRS_TWOSIDEDSTENCILMODE,FALSE},
            {D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_SRGBWRITEENABLE,FALSE}};
        for(const auto& s:states)ext->SetRenderState(s.state,s.value);
        ext->SetVertexShader(nullptr);ext->SetPixelShader(nullptr);ext->SetTexture(0,nullptr);ext->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
        ext->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);ext->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        ext->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);ext->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);ext->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        struct Vertex{float x,y,z,rhw;DWORD color;};const DWORD magenta=0x80ff00ff;
        const Vertex v[]={{-.5f,-.5f,0,1,magenta},{float(width)-.5f,-.5f,0,1,magenta},{-.5f,float(height)-.5f,0,1,magenta},{float(width)-.5f,float(height)-.5f,0,1,magenta}};
        D3DVIEWPORT9 vp={0,0,width,height,0,1};
        if(SUCCEEDED(ext->SetViewport(&vp)))ext->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Vertex));
        effectState(); /* stencil off, blending off, no depth-stencil bound: the effect passes' state */
        ext->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    }
    void renderEffects() {
        nameMaskRunEnd(); /* 0.3.212 (task 32): the effect passes and SavedState meet the game's stencil states */
        CpuScope cpu(sampled()?&cpuEffects:nullptr);
        // 0.3.175 (S3): RenderProfile sample frames split the same span into buckets (effects_buckets.h).
        effectsBucketed=sampled()&&NorthlightRenderThreadProbe::profiling();
        {static const unsigned drawMethods[4]={rawMethodIndex("DrawIndexedPrimitive"),rawMethodIndex("DrawPrimitive"),rawMethodIndex("DrawIndexedPrimitiveUP"),rawMethodIndex("DrawPrimitiveUP")};
         const std::uint32_t* counters[4];for(unsigned i=0;i<4;++i)counters[i]=drawMethods[i]<ExtensionDevice::RawMethods?&mirrorState.rawMethodCalls[drawMethods[i]]:nullptr;
         effectsBuckets.begin(effectsBucketed,&qpcNow,counters,4);}
        struct BucketsEnd {NorthlightEffectsBuckets::Frame& f;~BucketsEnd(){f.end();}} bucketsEnd{effectsBuckets};
        using NorthlightEffectsBuckets::Bucket;
        // 0.3.154: sceneMs end = the last entry before effects apply; the read is billed to cpuEffects.
        if((gateFrame||NorthlightRenderThreadProbe::profiling())&&!applied){QueryPerformanceCounter(&gateSceneEnd);stallSceneIdle=streamStats?NorthlightStream::get(streamStats->replayIdleNs):0;gateSceneDraws=drawCalls;gateSceneReads=CpuScope::reads;}
        if (applied || !enabled || failed || !projectionValid) return;
        // task 45: the always-on effects span for the CPU budget (applying frames only; every return path below ends it).
        struct EffectsSpan { std::int64_t& out; std::int64_t start; ~EffectsSpan(){out=qpcNow()-start;} } effectsSpan{cpuEffectsQpc,qpcNow()};
        // RenderProfile: every applying frame (two clock reads), the FRAME cost effects series.
        CpuScope frameCostScope(!sampled()&&NorthlightRenderThreadProbe::profiling()?&cpuEffects:nullptr);
        // The scope outlives every SavedState, including nested depth passes.
        // Game captures remain outside; the device gate stays held throughout.
        rainMrtUnbind(); /* 0.3.202 (rain mask MRT) */
        ExtensionDevice::RawScope rawEffects(*ext);
        D3DSURFACE_DESC desc;
        if (!fullViewport(desc) || !resources(desc.Width, desc.Height, desc.Format) || !resolveDepth()) return;
        SavedState saved(ext,&stateBlocks);
        if (!saved.ok) return;
        // 0.3.200 (gpu budget): the frame timer brackets every effect pass (closed on every return path by the guard, after the profile's end).
        struct TimerEnd { NorthlightGpuFrameTimer* t; bool open; ~TimerEnd(){if(open)t->end();} } timerEnd{gpuTimer.get(),world&&world->gpuBudgetMs()&&gpuTimer->begin()};
        if(gpuSceneProfile&&!gpuSceneSplit){gpuSceneProfile->mark("GameScene");gpuSceneSplit=true;}
        if(diagnostics())gpuProfile->beginFrame(frame,NorthlightRenderThreadProbe::sampleFrame(frame)); // otherwise no queries; mark/endFrame are no-ops
        effectsBuckets.mark(Bucket::Setup);
        struct ProfileEnd { NorthlightGpuProfile* p;Device* self; ~ProfileEnd(){if(p){p->endFrame();self->gpuEffectsEnd();}} } profileEnd{spellOn?nullptr:gpuProfile.get(),this}; /* the spell layer replays right after this returns: spellReplay() closes the profile frame with its own "Spells" interval */
        IDirect3DTexture9* rainMaskTex=world&&debugMode==0?rainMaskPrepare():nullptr; /* 0.3.202 (rain mask MRT): the mask resolved and cut by the final depth (own SavedState) */
        if(water&&debugMode==0)water->resolveMask(); /* 0.3.214 (water MRT): decodes the frame's render target 1 data (own SavedState; false: no mask from it, maskTextureForDepth answers null) */
        IDirect3DTexture9* waterMask=water?water->maskTextureForDepth(worldMinDepth,worldMaxDepth):nullptr;
        gpuProfile->mark("PreCelestial"); /* task 45: the rain and water mask preparation (and GPU work still pending from the game's scene) end here, so CelestialDiscs bills the discs and the terrain mask alone */
        // discs and glare, then the wrap-ring visibility in the same group of
        // 1x1 passes before the scene copy; the veil follows water (below).
        const bool celestial=celestialDiscs&&world&&debugMode==0&&worldDebug==0;float sunElevation=0;
        if(celestial){
            NorthlightCelestial::Context sky;float inverseView[16],projection[3];
            if(world->celestialContext(sky,inverseView,projection)){sunElevation=sky.sun.direction[2]-sky.horizonSine; /* over the camera's far edge, as the composite's haze band */
                celestialDiscs->setWeatherGain(world->weatherEffects().discGain()*world->underwaterScale()); /* 0.3.198 (rain): the discs, glare and veil follow the weather */
                celestialDiscs->render(saved.targets[0],depthTex,waterMask,width,height,world->legacyFogParameters(),worldMinDepth,worldMaxDepth,nearZ,farZ,projection,inverseView,sky,celestialGlowHue());}
        }
        gpuProfile->mark("CelestialDiscs");
        if(celestial){celestialDiscs->renderRing();gpuProfile->mark("CelestialRing");}
        effectsBuckets.mark(Bucket::Celestial); /* discs, glare, the terrain mask prepare, the ring */
        if (error(ext->StretchRect(saved.targets[0], nullptr, sceneSurface, nullptr, D3DTEXF_NONE), "copy scene")) return;
        effectState();
        float constants[]={1.f/width,1.f/height,nearZ,farZ,scaleX,scaleY,aoStrength,(world&&world->ready()?0.f:.12f),.08f,aoRadius,float(debugMode),0,worldMinDepth,1.f/(worldMaxDepth-worldMinDepth),worldMaxDepth,0};

        float waterFlags[]={waterMask?1.f:0.f,0,0,0};
        auto bindEffects=[&](IDirect3DTexture9* ambient){
            ext->SetPixelShaderConstantF(4,waterFlags,1);ext->SetTexture(3,waterMask);
            ext->SetPixelShaderConstantF(0,constants,4);
            auto nativeFog=world?world->legacyFogConstants():NorthlightLegacyFog::Constants{};
            nativeFog.parameters[0]*=fogForward; // Position() reconstructs positive view distance.
            ext->SetPixelShaderConstantF(5,nativeFog.parameters,1);ext->SetPixelShaderConstantF(6,nativeFog.color,1);
            ext->SetTexture(0,scene); ext->SetTexture(1,depthTex); ext->SetTexture(2,ambient);
        };
        bindEffects(nullptr);
        if (!contactAO) {
            // 0.3.201 (task 18) ContactAO=0: no AO and no AOBlur. A ready world gets the bloom with AO 1 straight into `ao` (ContactBloom);
            // the legacy composite gets a neutral ambient (AO 1, no bounce) from a clear. SetRenderTarget resets the viewport; scissor and depth are off (effectState).
            if (constants[7]==0.f) {
                if (error(ext->SetRenderTarget(0,aoSurface),"contact bloom render target")) return;
                ext->SetPixelShader(contactBloomPS);
                if (error(quad(width/2,height/2),"contact bloom pass")) return;
                gpuProfile->mark("ContactBloom");
            } else {
                if (error(ext->SetRenderTarget(0,aoSurface),"neutral AO render target")) return;
                if (error(ext->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(255,0,0,0),1.f,0),"neutral AO clear")) return;
                gpuProfile->mark("AOClear");
            }
            effectsBuckets.mark(Bucket::AO);
        } else {
            IDirect3DTexture9* actorTexture=aoActorOn?aoActorMask->textureForDepth(worldDepth):nullptr;
            aoActorUsed=actorTexture&&aoActorShaders(constants[7]!=0.f);
            if(aoActorUsed){
                ext->SetTexture(4,actorTexture);
                ext->SetSamplerState(4,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);ext->SetSamplerState(4,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
                ext->SetSamplerState(4,D3DSAMP_MINFILTER,D3DTEXF_POINT);ext->SetSamplerState(4,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
                ext->SetSamplerState(4,D3DSAMP_MIPFILTER,D3DTEXF_NONE);ext->SetSamplerState(4,D3DSAMP_SRGBTEXTURE,FALSE);
                // Reuse the existing ping-pong target for normals/contact;
                // no extra model replay or intermediate normal texture is needed.
                if(error(ext->SetRenderTarget(0,aoSurface),"actor AO preparation target"))return;
                ext->SetPixelShader(aoActorPreparePS);
                if(error(quad(width/2,height/2),"actor AO preparation pass"))return;
                gpuProfile->mark("AOActorPrepare");
            }
            IDirect3DPixelShader9* contactShader=aoActorUsed?(constants[7]==0.f?aoActorPS:aoActorAlphaPS):aoContactBloomPS;
            if (error(ext->SetRenderTarget(0,aoRawSurface),"AO render target")) return;
            if(aoActorUsed)ext->SetTexture(2,ao);
            ext->SetPixelShader(constants[7]==0.f?contactShader:aoPS);
            if (error(quad(width/2,height/2),"AO pass")) return;
            if (constants[7]!=0.f && aoMethod) {
                // Keep the original fallback colour bounce in RGB; replace only AO.
                // Splitting these kernels keeps GTAO below the ps_3_0 slot limit.
                ext->SetRenderState(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_ALPHA);
                ext->SetPixelShader(contactShader);
                const HRESULT aoResult=quad(width/2,height/2);
                ext->SetRenderState(D3DRS_COLORWRITEENABLE,15);
                if (error(aoResult,"fallback AO alpha pass")) return;
            }
            gpuProfile->mark("AO");
            // The preparation target becomes the blur destination below.
            if(aoActorUsed){ext->SetTexture(2,nullptr);ext->SetTexture(4,nullptr);}
            // 0.3.201 (task 18): the raw IGN-rotated AO is denoised by a half-resolution bilateral pass into `ao`,
            // which every consumer (legacy composite, world fold) keeps reading. s2 is released afterwards.
            // Task 43: s0 (LINEAR) carries the raw AO too for the blur's 9-read bilinear early-out (AOBlur never reads Scene); the scene copy is rebound after.
            if (error(ext->SetRenderTarget(0,aoSurface),"AO blur render target")) return;
            bindEffects(aoRaw);ext->SetTexture(0,aoRaw);ext->SetPixelShader(aoBlurPS);
            if (error(quad(width/2,height/2),"AO blur pass")) return;
            ext->SetTexture(2,nullptr);ext->SetTexture(0,scene);
            gpuProfile->mark("AOBlur");effectsBuckets.mark(Bucket::AO);
        }
        // 0.3.174 FOLD: with a ready world and no effect debug view, WorldComposite applies the
        // AO and bloom (AOContactBloom: bloom rgb, AO alpha) to the scene copy itself, so the
        // full-resolution composite and the world's colour copy are skipped. LEGACY (any other
        // frame) keeps the composite before the world, exactly as before.
        const bool fold=world&&debugMode==0&&world->ready();
        /* 0.3.212 (task 32): this frame's name marks: the composite quads draw with the world depth-stencil bound and reject marked pixels; no marks, or the feature off: exactly the old calls */
        const bool maskOn=nameMask.armed()&&nameMask.hasMarks()&&maskStencil&&worldDepth&&worldDebug==0;nameMask.composited=maskOn;
        auto legacyComposite=[&]{
            if (error(ext->SetRenderTarget(0,saved.targets[0]),"composition render target")) return false;
            ext->SetTexture(2,ao); ext->SetPixelShader(compositePS);
            {NameMaskCompositeBind mask(ext,maskOn?worldDepth:nullptr); /* 0.3.212 (task 32): the same bind as WorldComposite */
             if (error(quad(width,height),"composition pass")) return false;}
            gpuProfile->mark("AOComposite");effectsBuckets.mark(Bucket::Composite);return true;
        };
        if(!fold&&!legacyComposite())return;
        if(world&&debugMode==0){
            traceWorld=0;
            world->setNameMask(maskOn?worldDepth:nullptr); /* 0.3.212 (task 32) */
            world->setRainMask(rainMaskTex); /* 0.3.202 (rain): RainMask for this frame's composite only (resolved and scrubbed at the top of renderEffects) */
            if(!world->render(saved.targets[0],depthTex,width,height,sceneFormat,nearZ,farZ,worldMinDepth,worldMaxDepth,worldDebug,gpuProfile.get(),waterMask,fold?scene:nullptr,fold?ao:nullptr)){
                if(++worldSkippedFrames<=8||(worldSkippedFrames%120==0&&diagnostics()))
                    logf("WORLD skipped frame=%u tick=%lu context=%d ready=%d count=%u reason=%s",frame,(unsigned long)GetTickCount(),world->hasContext(),world->ready(),worldSkippedFrames,world->lastSkipReason());
                worldSkipLast=world->lastSkipReason();if(!worldSkipRun++){worldSkipStart=GetTickCount();worldSkipFirst=worldSkipLast;
                    if(worldSkipEpisodes<32||diagnostics())logf("WORLD skip episode begin reason=%s",worldSkipFirst);}
            }else if(traceWorld=1,(worldDrawSeen?0:(worldDrawSeen=true,worldDrawFrame=frame,0)),(frameMarkers?frameMarker(0,frame):void()),worldSkipRun){
                if(++worldSkipEpisodes<=32||diagnostics())logf("WORLD skip episode reason=%s last=%s frames=%u ms=%lu",worldSkipFirst,worldSkipLast,worldSkipRun,(unsigned long)(GetTickCount()-worldSkipStart));
                worldSkipRun=0;
            }
            // A folded frame the world did not composite (skipped or failed; it left the target
            // untouched): re-establish the effect state it changed, then the legacy composite.
            if(fold&&!world->composited){effectState();bindEffects(ao);if(!legacyComposite())return;}
        }
        if(celestial){
            const auto& haze=world->horizonHazeConstants();
            celestialDiscs->renderVeil(saved.targets[0],NorthlightCelestialGlow::hazeAtSun(haze.haze[3],haze.shape[2],sunElevation),world->skyTransmittance());
            gpuProfile->mark("CelestialVeil");effectsBuckets.mark(Bucket::Veil);
        }
        if(maskOn&&nameMask.debug)nameMaskDebugView(saved.targets[0]); /* 0.3.212 (task 32): the last effect pass, the mask itself */
        applied = true; ++appliedFrames;
        if (appliedFrames==1) logf("FIRST EFFECT FRAME: near=%.5f far=%.2f scale=%.4f,%.4f depthRange=%.9g..%.9g (before UI)",nearZ,farZ,scaleX,scaleY,worldMinDepth,worldMaxDepth);
    }
    /* The guards of every mask decision: the game must not use stencil (a draw seen with STENCILENABLE on disables the feature for the session), the world depth must have stencil bits (checked once per surface and format; one without skips the mask while it is bound). */
    bool nameMaskReady(){
        if(!nameMask.stencilChecked){ /* once per frame: the game's own STENCILENABLE is then known FALSE for the frame's runs */
            DWORD stencilOn=FALSE;if(FAILED(ext->GetRenderState(D3DRS_STENCILENABLE,&stencilOn)))return false;
            if(stencilOn){nameMask.disable("the game uses stencil");logf("NAMEMASK disabled: the game draws with stencil enabled");return false;}
            nameMask.stencilChecked=true;
        }
        /* The verdict is per surface and format (a surface released and another created at the same address with another format gets a new one). A world depth without stencil bits (a surface the upgrade
           did not reach) only skips the mask while it is the world depth: no marks, no clears, no composite test; a later world depth with stencil arms it again. Logged per change, capped. */
        const unsigned long format=worldDepthDescKnown?(unsigned long)worldDepthDesc.Format:0ul;
        if(worldDepth!=maskFormatFor||format!=maskFormatValue){
            maskFormatFor=worldDepth;maskFormatValue=format;maskStencil=worldDepthDescKnown&&NorthlightNameStencil::hasStencil(format,MAKEFOURCC('I','N','T','Z'));maskDsId=maskStencil?static_cast<const void*>(worldDepth):nullptr;
            if(++maskFormatLogs<=16||diagnostics())logf("NAMEMASK world depth surface=%p format=%lu stencil=%d upgradedSurfaces=%u%s",static_cast<void*>(worldDepth),format,int(maskStencil),
                NorthlightStream::depthUpgrades.load(std::memory_order_relaxed),maskStencil?"":" (no stencil bits: no name mask while this surface is the world depth)");
        }
        return maskStencil;
    }
    // 0.3.212 (task 32): the mask decision of a world-domain draw (prepareDrawImpl, before the boundary). After the frame's first mark a depth-final draw (Z-writing opaque, or water Z) unmarks (kind 2). A name (kind 1) is a draw with the
    // UI-hash vertex shader and a perspective projection: the real UI draws with it too but with the screen-space c3=(0,0,0,1) the boundary itself waits for (beforeDraw), so c3 not being that is the whole difference; plus alpha blending
    // (nameBlend), the full viewport and the world depth bound. Screen UI never gets here marked: it fails the c3 test, and after the boundary prepareDrawImpl returns first.
    template<class Z,class B,class S,class D,class C> void nameMaskDecide(bool water,Z&& zw,B&& ab,S&& sb,D&& db,C&& cw,const D3DVIEWPORT9& viewport){
        if(!nameMaskReady())return;
        if(water||!drawUiHashVS){ /* the cheapest test first (beforeDraw's tag): all but the names stop at it */
            if(nameMask.hasMarks()&&NorthlightTranslucentDepth::isUndo(water,zw,ab,sb,db,cw)){maskKind=2;++nameMask.unmarkDraws;}
            return;
        }
        if(!NorthlightNameStencil::nameBlend(ab,sb,db,cw))return;
        float c3[4];if(FAILED(ext->GetVertexShaderConstantF(3,c3,1))||NorthlightUiBoundary::clipWOne(c3))return;
        if(viewport.X==0&&viewport.Y==0&&viewport.Width==width&&viewport.Height==height&&worldDepthBound())maskKind=1;
    }
    // 0.3.212 (task 32): a render state of the current draw. One the mirror knows is read in place: the draw hooks hold the gate, so no lock and no COM call (like weatherProbe's texture read);
    // an unknown one (mirror inactive or not yet observed) goes through GetRenderState as before.
    bool readDrawRenderState(D3DRENDERSTATETYPE t,DWORD& v){
        if(unsigned(t)<DeviceMirror::RenderStates&&mirrorState.active()&&mirrorState.renderStateKnown[t]){v=mirrorState.renderStates[t];return true;}
        return SUCCEEDED(ext->GetRenderState(t,&v));
    }
    DWORD drawRenderState(D3DRENDERSTATETYPE t){DWORD v=0;readDrawRenderState(t,v);return v;}
    // The bound depth buffer is the frame's world depth (an offscreen pass with its own depth is never marked).
    bool worldDepthBound(){return boundDepthIs(worldDepth);}
    bool boundDepthIs(const void* id){
        IDirect3DSurface9* current=nullptr;if(ext->peekDepthStencilSurface(current))return current==id;
        const bool same=SUCCEEDED(ext->GetDepthStencilSurface(&current))&&current==id;drop(current);return same;
    }
    // Sets a table of render states, keeping the values it replaced (read first, in place from the mirror when it knows them); maskRestore() puts them back. False: a read failed, nothing was changed.
    template<size_t N> bool maskApply(const NorthlightNameStencil::Set (&sets)[N],DWORD (&previous)[N]){
        for(size_t i=0;i<N;++i)if(!readDrawRenderState(D3DRENDERSTATETYPE(sets[i].state),previous[i]))return false;
        for(size_t i=0;i<N;++i)ext->SetRenderState(D3DRENDERSTATETYPE(sets[i].state),sets[i].value);
        return true;
    }
    template<size_t N> void maskRestore(const NorthlightNameStencil::Set (&sets)[N],const DWORD (&previous)[N]){for(size_t i=0;i<N;++i)ext->SetRenderState(D3DRENDERSTATETYPE(sets[i].state),previous[i]);}
    bool nameMaskFullViewport(const D3DVIEWPORT9& v)const{return worldDepthDescKnown&&v.X==0&&v.Y==0&&v.Width==worldDepthDesc.Width&&v.Height==worldDepthDesc.Height;} /* the world depth surface's size */
    // The first name of a frame without a Clear(Z) of the world depth seen yet is preceded by one stencil-only Clear (not with a scissor test on: the Clear would be cut; not with a partial viewport: Clear is clipped to it).
    bool nameMaskClear(){
        DWORD scissor=FALSE;D3DVIEWPORT9 viewport={};if(FAILED(ext->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor))||FAILED(ext->GetViewport(&viewport)))return false;
        const bool fullViewport=nameMaskFullViewport(viewport);D3DVIEWPORT9 full=viewport;full.X=full.Y=0;full.Width=worldDepthDesc.Width;full.Height=worldDepthDesc.Height;
        if(scissor)ext->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE); /* a scissor test would cut the clear: off for the call, back after */
        if(!fullViewport)ext->SetViewport(&full); /* Clear is clipped to the viewport: full depth size for the call, back after */
        const HRESULT cleared=ext->Clear(0,nullptr,D3DCLEAR_STENCIL,0,1.f,0);
        if(!fullViewport)ext->SetViewport(&viewport);
        if(scissor)ext->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
        if(FAILED(cleared)){nameMask.disable("stencil clear failed");logf("NAMEMASK disabled: stencil clear failed");return false;}
        nameMask.noteClear(true);return true;
    }
    // The run of name draws: kMark stays applied from the first name to the next draw that is not one. Everything that could draw or clear with stencil on ends it first (prepareDrawImpl for any draw but a UI-hash
    // vertex shader one, nameMaskDraw for any draw that is not a name, Clear, EndScene, Reset, renderEffects, the frame end). False: not started, nothing was changed.
    bool nameMaskRunBegin(){
        if(nameRun)return true;
        if(nameMask.needsClear()&&!nameMaskClear())return false;
        DWORD pass=0,ref=0; /* the probe: another state source (a state block the game applied, the composite or the debug view) may have changed the set since the last run */
        if(!readDrawRenderState(D3DRS_STENCILPASS,pass)||!readDrawRenderState(D3DRS_STENCILREF,ref))return false;
        if(pass!=NorthlightNameStencil::kMarkProbe[0].value||ref!=NorthlightNameStencil::kMarkProbe[1].value)
            for(const auto& set:NorthlightNameStencil::kMark)ext->SetRenderState(D3DRENDERSTATETYPE(set.state),DWORD(set.value));
        ext->SetRenderState(D3DRS_STENCILENABLE,TRUE);
        nameRun=true;++nameMask.runs;return true;
    }
    void nameMaskRunEnd(){
        if(!nameRun)return;
        nameRun=false;CpuScope c(sampledDrawTimers()?&cpuNameMark:nullptr);ext->SetRenderState(D3DRS_STENCILENABLE,FALSE); /* its value before the run: the guard saw FALSE this frame */
    }
    // prepareDrawImpl's first step: while a run is open, only a draw with the UI-hash vertex shader (a name, or the boundary which ends the run in renderEffects) may keep it; the extension's own draws for any other draw
    // (depth resolve, captures) must not meet the stencil states.
    void nameMaskRunBreak(){
        IDirect3DVertexShader9* vs=nullptr;
        if(!ext->peekVertexShader(vs)||!vs||!uiTextVs(vs,classifyVs(vs).entry&kTagMask))nameMaskRunEnd();
    }
    // The draw hook's last step: the game's draw (rain blend, blob and terrain shadow wrappers inside) with the name's stencil states set (run) and the alpha guard if its alpha test is off, or the unmark states around a depth-final draw.
    template<class Draw> HRESULT nameMaskDraw(bool rain,bool claimed,Draw draw){
        const int kind=maskKind;maskKind=0;
        if(kind!=1)nameMaskRunEnd();
        if(!kind)return rainBlendDraw(rain,claimed,draw);
        if(kind==2){
            DWORD previous[sizeof NorthlightNameStencil::kUnmark/sizeof NorthlightNameStencil::kUnmark[0]];bool set=false;
            extensionWork("name mask",[&]{CpuScope c(sampledDrawTimers()?&cpuNameUnmark:nullptr);set=maskApply(NorthlightNameStencil::kUnmark,previous);});
            const HRESULT hr=rainBlendDraw(rain,claimed,draw);
            if(set)extensionWork("name mask",[&]{CpuScope c(sampledDrawTimers()?&cpuNameUnmark:nullptr);maskRestore(NorthlightNameStencil::kUnmark,previous);});
            return hr;
        }
        DWORD alphaPrevious[sizeof NorthlightNameStencil::kAlphaGuard/sizeof NorthlightNameStencil::kAlphaGuard[0]];bool guard=false;
        if(claimed||!nameMask.armed())return rainBlendDraw(rain,claimed,draw);
        extensionWork("name mask",[&]{CpuScope c(sampledDrawTimers()?&cpuNameMark:nullptr);
            if(!nameMaskRunBegin())return;
            DWORD alphaTest=TRUE;if(readDrawRenderState(D3DRS_ALPHATESTENABLE,alphaTest)&&!alphaTest)guard=maskApply(NorthlightNameStencil::kAlphaGuard,alphaPrevious);});
        const HRESULT hr=rainBlendDraw(rain,claimed,draw);
        if(guard)extensionWork("name mask",[&]{CpuScope c(sampledDrawTimers()?&cpuNameMark:nullptr);maskRestore(NorthlightNameStencil::kAlphaGuard,alphaPrevious);});
        if(SUCCEEDED(hr)&&nameRun)nameMask.noteMark();
        return hr;
    }
    // One shader query/reference per original draw, shared with shadow capture.
    template<class Capture> void prepareDraw(Capture capture,UINT count) {
        mirrorState.gate.noteFirst(mirrorState.gate.drawTid); /* 0.3.180 (D0): the census' first draw */
        dropTerrainShadowSwap();
        extensionWork("draw capture/effects",[&]{prepareDrawImpl(capture,count);});
    }
    // task 34: a draw with no vertex shader bound (fixed function: ribbons, beams) returns from prepareDrawImpl early; the spell layer still needs its world domain, the
    // same test as the programmable path (viewport depth range and Z enable) and nothing else of what that path does.
    // DepthFuncAware: the spell layer's "depth tested" is ZENABLE on and ZFUNC not ALWAYS (the client's depth-off is ZFUNC ALWAYS); the world domain itself stays ZENABLE only (name and UI-hash draws are decided inside it).
    bool spellDepthTested(DWORD depthEnabled){
        if(depthEnabled==FALSE)return false;
        DWORD func=D3DCMP_LESSEQUAL;
        if(!zFuncAware||FAILED(ext->GetRenderState(D3DRS_ZFUNC,&func)))return true;
        return NorthlightWorldDrawDomain::depthTested(depthEnabled,func);
    }
    void spellFixedDomain(){
        if(!world||!projectionValid)return;
        D3DVIEWPORT9 viewport={};DWORD depthEnabled=FALSE;
        if(FAILED(ext->GetViewport(&viewport))||FAILED(ext->GetRenderState(D3DRS_ZENABLE,&depthEnabled)))return;
        spellDomain=NorthlightWorldDrawDomain::accepts(projectionValid,depthEnabled!=FALSE,worldMinDepth,worldMaxDepth,viewport.MinZ,viewport.MaxZ);spellZEnable=spellDepthTested(depthEnabled);
    }
    template<class Capture> void prepareDrawImpl(Capture capture,UINT count) {
        if(!gpuFrameStarted){
            gpuFrameStarted=true;
            if(gpuSceneProfile&&diagnostics())gpuSceneProfile->beginFrame(frame,NorthlightRenderThreadProbe::sampleFrame(frame));
        }
        aoActorBeginFrame();aoActorDrawKnown=aoActorDrawSkin=aoActorDepthWrite=false;
        ++drawCalls;maskKind=0;if(nameRun)nameMaskRunBreak();
        if(spellOn){spellDomain=false;spellVs=nullptr;} /* task 34: this draw's reads for spellDrawImpl, valid only if this function gets that far */
        if(failed||!enabled)return;
        if(applied){
            if(sampled()){IDirect3DVertexShader9* late=nullptr;const bool borrowed=ext->peekVertexShader(late); // 0.3.196 (task 12): identity lookup only
                if((borrowed||(SUCCEEDED(ext->GetVertexShader(&late))&&late))&&world){
                    const VsClass& vc=classifyVs(late);
                    if(vc.world)++postEffectWorldDraws;
                    if(vc.skinned)++postEffectSkinnedDraws;
                }if(!borrowed)drop(late);
            }return;
        }
        CpuScope cpu(sampledDrawTimers()?&cpuPrep:nullptr);if(gateFrame){++gateCounts.prep;weatherProbeDraw(count);} /* 0.3.198 (rain): RenderProfile sample frames only, pre-effects draws */
        IDirect3DVertexShader9* vs=nullptr;
        // 0.3.196 (task 12): borrowed (no reference). The shader stays bound through this hook (the game's draw is issued after it); capture AddRefs whatever it keeps,
        // after renderEffects applied=true stops further use of vs, and resolveDepth's SavedState restores the binding. Released only when not borrowed.
        const bool borrowedVS=ext->peekVertexShader(vs);
        if(!borrowedVS&&(FAILED(ext->GetVertexShader(&vs))||!vs)){++missingVS;drop(vs);if(spellOn)spellFixedDomain();return;}
        struct ShaderRelease {IDirect3DVertexShader9*& p;bool borrowed;~ShaderRelease(){if(!borrowed)drop(p);}} shaderRelease{vs,borrowedVS};
        if(spellOn)spellVs=vs; /* identity only */
        beforeDraw(vs);planTerrainShadowSwap(vs);
        if(world&&!applied&&enabled&&!failed&&projectionValid){
            D3DVIEWPORT9 viewport={};DWORD depthEnabled=FALSE;
            const bool statesRead=SUCCEEDED(ext->GetViewport(&viewport))&&SUCCEEDED(ext->GetRenderState(D3DRS_ZENABLE,&depthEnabled));
            const bool worldDomain=statesRead&&NorthlightWorldDrawDomain::accepts(projectionValid,depthEnabled!=FALSE,
                worldMinDepth,worldMaxDepth,viewport.MinZ,viewport.MaxZ);
            if(spellOn){spellDomain=worldDomain;spellZEnable=spellDepthTested(depthEnabled);} /* task 34 */
            if(worldDomain){if(gateFrame)++gateCounts.capture;
                /* 0.3.188 (task 3): lazy state reads for the early resolve, its undo and the census, each read at most once per draw */
                auto rs=[&](D3DRENDERSTATETYPE t){return [this,t,v=DWORD(0),known=false]()mutable{if(!known){v=drawRenderState(t);known=true;}return v;};};
                auto zw=rs(D3DRS_ZWRITEENABLE);auto ab=rs(D3DRS_ALPHABLENDENABLE);auto sb=rs(D3DRS_SRCBLEND);auto db=rs(D3DRS_DESTBLEND);auto cw=rs(D3DRS_COLORWRITEENABLE);
                if(aoActorOn){aoActorDrawKnown=true;aoActorDrawSkin=classifyVs(vs).skinned;aoActorDepthWrite=zw()!=FALSE;}
                if(terrain&&nameMask.armed()&&worldDebug==0&&(drawUiHashVS||nameMask.hasMarks())){ /* 0.3.212 (task 32): only a UI-hash draw can be a name, only a frame with marks can unmark */
                    const bool sampledTimed=sampledDrawTimers()&&(++nameDecideCalls&7)==1;if(sampledTimed)++nameDecideTimed;
                    CpuScope decision(sampledTimed?&cpuNameDecide:nullptr);nameMaskDecide(drawWaterVS,zw,ab,sb,db,cw,viewport);}
                if(captured&&earlyDepth.earlyCaptured&&NorthlightTranslucentDepth::isUndo(drawWaterVS,zw,ab,sb,db,cw)){captured=false;if(aoActorOn)aoActorMask->resume();earlyDepth.undo();++earlyResolveUndone;++earlyResolveUndoneTotal;} /* undo: later opaque or water Z draw after the early resolve */
                if(terrain&&!captured&&earlyDepth.armed()&&NorthlightTranslucentDepth::shouldResolve(drawWaterVS,zw,ab,sb,db,cw,[&]{return classifyVs(vs).skinned;})&&earlyDepth.attempt()){ /* 0.3.188 (2A): early depth for translucent actors, every frame, minimum state reads */
                    ExtensionDevice::RawScope raw(*ext);
                    if(resolveDepth()){earlyDepth.success();++earlyResolves;++earlyResolveTotal;if(sampled())earlyResolveAt=censusDraws+1;}}
                if(sampled()){const unsigned at=++censusDraws; /* 0.3.188 (task 3): read-only census before capture(vs) so no early return hides a draw; sample frames only */
                    if(!drawWaterVS&&NorthlightTranslucentDepth::opaqueZWrite(zw,ab,sb,db,cw))lastOpaqueZAt=at;}
                if(world->equipmentLightsOn())world->captureEquipmentLights(vs,worldDepth);
                {const unsigned long long reads=CpuScope::reads;{CpuScope cap(sampledDrawTimers()?&cpuCapture:nullptr);capture(vs);}if(CpuScope::reads!=reads)cpuCaptureReads+=CpuScope::reads-reads-1;}
                if(mirrorState.active()&&mirrorAuditSchedule.afterWorldCapture(frame,
                    mirrorState.vsFloatKnown[0]&&mirrorState.vsFloatKnown[DeviceMirror::VsFloat-1])){
                    CpuScope auditCost(diagnostics()?&cpuMirrorAudit:nullptr); // the audit itself always runs
                    if(gateFrame)++gateCounts.audits;
                    if(const char* field=ext->audit())logf("MIRROR mismatch field=%s frame=%u; cache disabled, rendering retained",field,frame);
                }
            }
            else if(classifyVs(vs).world){
                if(++nonWorldCaptureRejects<=4||(nonWorldCaptureRejects%3600==0&&diagnostics()))
                    logf("WORLD non-caster draw rejected: worldDepth=%.7f..%.7f drawDepth=%.7f..%.7f z=%lu states=%d count=%u",worldMinDepth,worldMaxDepth,viewport.MinZ,viewport.MaxZ,(unsigned long)depthEnabled,statesRead,nonWorldCaptureRejects);
            }
        }
        if(!borrowedVS)drop(vs);
    }
    // Terrain draws run with the game's shadow term neutralised while the
    // extension's shadow maps were composited last frame, so the baked ADT
    // shadow (MCSH) and the game's own dynamic shadow map no longer double
    // the extension's shadow. Planned inside the guarded capture, applied
    // around the real draw (outside extensionWork, like the draw itself).
    IDirect3DPixelShader9 *shadowSwapOriginal=nullptr,*shadowSwapReplacement=nullptr;
    IDirect3DVertexShader9 *shadowSwapVertexOriginal=nullptr,*shadowSwapVertexReplacement=nullptr;
    unsigned shadowSwapConstantBase=0,shadowSwapConstantCount=0;float shadowSwapConstants[24]={};
    void planTerrainShadowSwap(IDirect3DVertexShader9* vs){
        if(!drawGates.terrainShadow)return; /* 0.3.187: implied by the line below at every draw of this frame */
        if(!world||applied||!enabled||failed||!terrain||debugMode!=0||worldDebug!=0||!world->terrainShadowActive())return;
        if((classifyVs(vs).entry&kTagMask)!=1)return;
        IDirect3DPixelShader9* ps=nullptr;if(FAILED(ext->GetPixelShader(&ps))||!ps)return;
        IDirect3DPixelShader9* replacement=nullptr;IDirect3DVertexShader9* patchedVertex=nullptr;
        if(world->terrainShadowCoverage(vs,ps,patchedVertex,replacement,shadowSwapConstantBase,shadowSwapConstantCount,shadowSwapConstants)){
            shadowSwapVertexOriginal=vs;vs->AddRef();shadowSwapVertexReplacement=patchedVertex;
        }else replacement=world->terrainShadowReplacement(ps);
        if(!replacement){ps->Release();return;}
        shadowSwapOriginal=ps;shadowSwapReplacement=replacement;
    }
    void dropTerrainShadowSwap(){if(shadowSwapOriginal)shadowSwapOriginal->Release();shadowSwapOriginal=shadowSwapReplacement=nullptr;if(shadowSwapVertexOriginal)shadowSwapVertexOriginal->Release();shadowSwapVertexOriginal=shadowSwapVertexReplacement=nullptr;}

    template<class Draw> HRESULT terrainShadowDraw(bool claimed,Draw draw){
        if(!shadowSwapOriginal)return claimed?D3D_OK:draw(); /* 0.3.187: no swap planned (planTerrainShadowSwap sets both or neither) */
        IDirect3DPixelShader9* original=shadowSwapOriginal;IDirect3DPixelShader9* replacement=shadowSwapReplacement;
        IDirect3DVertexShader9* vertexOriginal=shadowSwapVertexOriginal;IDirect3DVertexShader9* vertexReplacement=shadowSwapVertexReplacement;
        shadowSwapOriginal=shadowSwapReplacement=nullptr;
        shadowSwapVertexOriginal=shadowSwapVertexReplacement=nullptr;
        HRESULT hr=D3D_OK;
        if(!claimed&&replacement&&vertexOriginal){
            float saved[24];bool bound=false;
            {CpuScope swap(sampledHookTimer());
             if(SUCCEEDED(ext->GetPixelShaderConstantF(shadowSwapConstantBase,saved,shadowSwapConstantCount))){
                 bound=SUCCEEDED(ext->SetVertexShader(vertexReplacement))&&SUCCEEDED(ext->SetPixelShader(replacement))&&SUCCEEDED(ext->SetPixelShaderConstantF(shadowSwapConstantBase,shadowSwapConstants,shadowSwapConstantCount));
                 if(!bound){ext->SetPixelShaderConstantF(shadowSwapConstantBase,saved,shadowSwapConstantCount);ext->SetPixelShader(original);ext->SetVertexShader(vertexOriginal);}
             }}
            hr=draw();
            if(bound){CpuScope swap(sampledHookTimer());ext->SetPixelShaderConstantF(shadowSwapConstantBase,saved,shadowSwapConstantCount);ext->SetPixelShader(original);ext->SetVertexShader(vertexOriginal);++terrainShadowDraws;}
        }
        else if(!claimed&&replacement){{CpuScope swap(sampledHookTimer());ext->SetPixelShader(replacement);}hr=draw();{CpuScope swap(sampledHookTimer());ext->SetPixelShader(original);}++terrainShadowDraws;}
        else if(!claimed)hr=draw();
        if(vertexOriginal)vertexOriginal->Release();
        if(original)original->Release();
        return hr;
    }
    // Liquid mask only (AO/GI/fog consume it). The depth identity, target
    // description and viewport read here are reused by the mask capture.
    // 0.3.214 (water MRT): a draw the MRT conditions hold for is only planned here (waterPending); waterMrtDraw binds render target 1 and the mask variants of its shaders
    // right before the game's own draw. Everything else keeps the second-draw pass, made here as before.
    template<class Draw> void captureWater(IDirect3DVertexShader9* vs,unsigned userPointer,Draw draw){
        if(!water||!drawWaterVS)return;
        /* a water draw that is not planned as MRT (any gate below) must not meet render target 1 left bound by the previous water draw */
        struct UnbindUnlessPlanned {Device* self;~UnbindUnlessPlanned(){if(!self->waterPending.valid&&self->water->isMrtBound())self->rainMrtUnbind();}} unbindUnlessPlanned{this};
        if(!water->usable()||!world||!world->hasContext())return; // == recognizesVertex(vs)
        if(!projectionValid||!worldDepth)return;
        CpuScope cost(sampledDrawTimers()?&cpuWaterCapture:nullptr);if(gateFrame)++gateCounts.water;
        IDirect3DSurface9* currentDepth=nullptr;bool mainDepth;if(ext->peekDepthStencilSurface(currentDepth))mainDepth=currentDepth==worldDepth; /* 0.3.196 (task 12): identity compare only, borrowed */
        else{mainDepth=SUCCEEDED(ext->GetDepthStencilSurface(&currentDepth))&&currentDepth==worldDepth;drop(currentDepth);}if(!mainDepth)return;
        D3DSURFACE_DESC desc={};D3DVIEWPORT9 viewport={};if(!fullViewport(desc,&viewport))return;
        IDirect3DPixelShader9* ps=nullptr;struct PixelRelease {IDirect3DPixelShader9*& p;~PixelRelease(){drop(p);}} releasePS{ps};
        if(FAILED(ext->GetPixelShader(&ps))||!ps)return;
        if(!waterCapsChecked)waterMrtCaps();
        const auto plan=water->plan(vs,ps,desc.Width,desc.Height,viewport);
        if(plan==NorthlightWaterRenderer::Plan::Skip)return;
        /* the twin must match the game's depth buffer too (D3D9: every bound target and the depth share multisample type and quality) */
        const bool sameMs=worldDepthDescKnown&&worldDepthDesc.MultiSampleType==desc.MultiSampleType&&worldDepthDesc.MultiSampleQuality==desc.MultiSampleQuality;
        if(plan==NorthlightWaterRenderer::Plan::Mrt&&sameMs){waterPending={vs,ps,userPointer,desc.Width,desc.Height,desc.MultiSampleType,desc.MultiSampleQuality,viewport,true};return;}
        if(plan==NorthlightWaterRenderer::Plan::Mrt&&!waterMsLogged){waterMsLogged=true;logf("WATER mask MRT skipped: render target ms=%u/%lu, depth ms=%u/%lu",unsigned(desc.MultiSampleType),(unsigned long)desc.MultiSampleQuality,worldDepthDescKnown?unsigned(worldDepthDesc.MultiSampleType):0u,worldDepthDescKnown?(unsigned long)worldDepthDesc.MultiSampleQuality:0ul);}
        if(water->isMrtBound())rainMrtUnbind(); /* the second draw and the game's own draw after it meet no render target 1 */
        water->capture(vs,ps,desc.Width,desc.Height,worldDepth,viewport,userPointer,mirrorState.invalidations,draw);
    }
    // 0.3.214 (water MRT): once per device, the caps the MRT mask needs: two render targets with independent write masks and bit depths (G32R32F next to the game's target),
    // blending on the extra target (the game's own blend applies to it: the format must be a post-pixel-shader blending render target) and pixel shader 2.0.
    // Without them every water draw keeps the second-draw pass.
    void waterMrtCaps(){
        waterCapsChecked=true;D3DCAPS9 caps={};const bool got=SUCCEEDED(ext->GetDeviceCaps(&caps));
        static constexpr DWORD kUsageQueryPostPixelShaderBlending=0x00080000; /* D3DUSAGE_QUERY_POSTPIXELSHADERBLENDING (the MinGW header lacks it) */
        IDirect3D9* d3d=nullptr;D3DDEVICE_CREATION_PARAMETERS creation={};D3DDISPLAYMODE mode={};
        const HRESULT formatHr=SUCCEEDED(ext->GetDirect3D(&d3d))&&d3d&&SUCCEEDED(ext->GetCreationParameters(&creation))&&SUCCEEDED(ext->GetDisplayMode(0,&mode))?
            d3d->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,mode.Format,D3DUSAGE_RENDERTARGET|kUsageQueryPostPixelShaderBlending,D3DRTYPE_SURFACE,D3DFMT_G32R32F):D3DERR_INVALIDCALL;
        drop(d3d);
        const bool ok=got&&SUCCEEDED(formatHr)&&caps.NumSimultaneousRTs>=2&&(caps.PrimitiveMiscCaps&D3DPMISCCAPS_INDEPENDENTWRITEMASKS)&&(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)&&(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING)&&caps.PixelShaderVersion>=D3DPS_VERSION(2,0);
        water->setMrtCaps(ok);
        if(!ok)logf("WATER mask MRT disabled: caps got=%d G32R32F-blending-target=0x%08lx simultaneousRTs=%lu independentWriteMasks=%d independentBitDepths=%d mrtBlending=%d pixelShader=0x%08lx (second draw per water draw)",int(got),(unsigned long)formatHr,(unsigned long)caps.NumSimultaneousRTs,int((caps.PrimitiveMiscCaps&D3DPMISCCAPS_INDEPENDENTWRITEMASKS)!=0),int((caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)!=0),int((caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING)!=0),(unsigned long)caps.PixelShaderVersion);
    }
    // The next draw uses a water vertex shader (an identity lookup of the bound shader; false when the binding is not known).
    bool nextDrawIsWater(){IDirect3DVertexShader9* v=nullptr;return ext->peekVertexShader(v)&&v&&water->hasVertex(v);}
    struct PendingWater {IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;unsigned userPointer=0;UINT width=0,height=0;D3DMULTISAMPLE_TYPE ms=D3DMULTISAMPLE_NONE;DWORD quality=0;D3DVIEWPORT9 viewport={};bool valid=false;};
    PendingWater waterPending;bool waterCapsChecked=false,waterMsLogged=false;
    // The game's draw (raw: its bare Draw call, chain: everything wrapped around it). A planned water draw runs as the one MRT draw; where beginMrt refuses (blend, fog,
    // resources) it is made the old way: the second draw first, then the game's.
    template<class Raw,class Chain> HRESULT waterMrtDraw(Raw raw,Chain chain){
        if(!waterPending.valid){if(water&&water->isMrtBound())rainMrtUnbind();return chain();} /* 0.3.215 (water MRT): render target 1 kept bound for a water draw captureWater never planned (capture skipped, a gate, another path): the game's own water shader meets no render target 1 */
        const PendingWater p=waterPending;waterPending.valid=false;
        bool masked=false;
        {CpuScope cost(sampledDrawTimers()?&cpuWaterCapture:nullptr);
            extensionWork("water mask",[&]{masked=water&&water->beginMrt(p.vs,p.ps,p.width,p.height,p.ms,p.quality,p.viewport);});
            if(!masked)extensionWork("water mask",[&]{
                if(water->isMrtBound())rainMrtUnbind();
                water->capture(p.vs,p.ps,p.width,p.height,worldDepth,p.viewport,p.userPointer,mirrorState.invalidations,raw);});}
        if(!masked)return chain();
        const HRESULT hr=chain();
        extensionWork("water mask",[&]{water->endMrt(SUCCEEDED(hr));});
        return hr;
    }
    // 0.3.193 BlobShadowStrength (held by the filter, read once at device creation): 100 = the filter is off and the
    // game's blobs are drawn as they are; 0 = they are skipped (0.3.154..0.3.192); 1..99 = drawn with a lighter
    // texture (skipped when the game's blend cannot be lightened). Applies only while the mod draws actor shadows: effects (F10) and shadows (F9) on, a world
    // context, and 0.3.158 ActorShadows=1 (with 0 the game's blobs are the actor shadows). The world must also be ready:
    // in maps without a world cache (dungeons, raids, battlegrounds) it draws no actor shadows, so the blobs stay as they are.
    bool blobFilterActive()const{return shadowBlobs&&shadowBlobs->active()&&enabled&&effectKeys.settings.shadows&&!applied&&terrain&&!failed&&world&&world->hasContext()&&world->ready()&&world->actorShadowsEnabled();}
    // 0.3.198 (rain): weather detection. weatherDetect.hot is the one candidate texture the draw hook compares
    // stage 0 with (weather_detect.h); the sample is what that comparison counted this frame. Both are touched
    // under the gate only, on whichever thread makes the draw (stream: replay thread; fallback: game thread).
    static_assert(NorthlightWeatherDetect::kFmtA8R8G8B8==D3DFMT_A8R8G8B8,"weather signature format");
    NorthlightWeatherDetect::Detector weatherDetect;NorthlightWeather::Tracker weatherTracker;NorthlightWeather::Sample weatherSample;
    unsigned weatherMistSkips=0,weatherMistUnknown=0,weatherMistOtherStage=0,weatherMistReports=0; /* 0.3.199 (rain mist): this frame's skipped mist draws and diagnostics */
    LONGLONG weatherTick=0;double weatherLogClock=0;NorthlightWeather::Kind weatherLoggedKind=NorthlightWeather::Kind::None;bool weatherLoggedBlendHigh=false,weatherLoggedBlendLow=false,weatherOffReported=false;int weatherMemoryState=-1;bool skyBandSeen=false;
    NorthlightWeatherMemory::Source weatherMemory;NorthlightWeatherMemory::CrossCheck weatherCross; /* the game's own weather state (memory); the detector above is its fallback and cross-check */
    // RenderProfile sample frames only: the largest draw before the effects (WEATHER probe), to calibrate detection.
    struct WeatherProbe {UINT count=0;const void* texture=nullptr;bool known=false,candidate=false,vs=false,ps=false;} weatherProbe;
    void weatherProbeDraw(UINT count){
        if(!applied&&weatherStateReports<4&&weatherDetect.hot&&mirrorState.textureKnown[0]&&mirrorState.textures[0]==weatherDetect.hot)weatherDrawStates(count); /* 0.3.199: sample frames only */
        if(applied||count<=weatherProbe.count)return;
        weatherProbe.count=count;weatherProbe.known=mirrorState.textureKnown[0];weatherProbe.texture=weatherProbe.known?mirrorState.textures[0]:nullptr; /* borrowed: never dereferenced */
        weatherProbe.candidate=weatherProbe.texture&&weatherDetect.isCandidate(weatherProbe.texture);weatherProbe.vs=mirrorState.vertexShaderKnown&&mirrorState.vertexShader;weatherProbe.ps=mirrorState.pixelShaderKnown&&mirrorState.pixelShader;
    }
    static void weatherLog(const char* line){logf("%s",line);}
    // 0.3.199: how the game draws its weather (the rain streaks took the colour of what is behind them in the game tests): the blend and
    // texture-stage states of a detected weather draw, logged for the first 4 such draws of a session, from the RenderProfile sample-frame probe
    // (weatherProbeDraw: no per-draw cost on other frames; reads, no writes).
    unsigned weatherStateReports=0;
    void weatherDrawStates(UINT count){
        ++weatherStateReports;
        DWORD rs[10]={};const D3DRENDERSTATETYPE ids[10]={D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,D3DRS_ZWRITEENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_TEXTUREFACTOR,D3DRS_LIGHTING};
        for(int i=0;i<10;++i)ext->GetRenderState(ids[i],&rs[i]);
        DWORD ts[2][6]={};const D3DTEXTURESTAGESTATETYPE tids[6]={D3DTSS_COLOROP,D3DTSS_COLORARG1,D3DTSS_COLORARG2,D3DTSS_ALPHAOP,D3DTSS_ALPHAARG1,D3DTSS_ALPHAARG2};
        for(int st=0;st<2;++st)for(int i=0;i<6;++i)ext->GetTextureStageState(st,tids[i],&ts[st][i]);
        IDirect3DPixelShader9* ps=nullptr;const bool hasPS=SUCCEEDED(ext->GetPixelShader(&ps))&&ps;drop(ps);
        logf("WEATHER draw states prims=%u blend=%lu src=%lu dst=%lu op=%lu alphaTest=%lu ref=%lu zwrite=%lu sepAlpha=%lu tfactor=%08lx lighting=%lu ps=%d "
             "stage0 color=%lu(%lu,%lu) alpha=%lu(%lu,%lu) stage1 color=%lu(%lu,%lu) alpha=%lu(%lu,%lu)",count,
             (unsigned long)rs[0],(unsigned long)rs[1],(unsigned long)rs[2],(unsigned long)rs[3],(unsigned long)rs[4],(unsigned long)rs[5],(unsigned long)rs[6],(unsigned long)rs[7],(unsigned long)rs[8],(unsigned long)rs[9],int(hasPS),
             (unsigned long)ts[0][0],(unsigned long)ts[0][1],(unsigned long)ts[0][2],(unsigned long)ts[0][3],(unsigned long)ts[0][4],(unsigned long)ts[0][5],
             (unsigned long)ts[1][0],(unsigned long)ts[1][1],(unsigned long)ts[1][2],(unsigned long)ts[1][3],(unsigned long)ts[1][4],(unsigned long)ts[1][5]);
    }
    // 0.3.187 per-frame draw gates (draw_gates.h, FrameDrawGates=1): latched where every input can
    // rise, never inside a frame: at the end of finishFrameImpl (after F9/F10/F12, setEffects and the
    // retry's failed=false; clearFrame runs before that retry, so it is not the place), in Reset
    // (failed=false) and at device creation. Off: every gate open, the 0.3.184 per-draw work.
    NorthlightDrawGates::Frame drawGates;bool frameDrawGates=true;bool contactAO=true;
    unsigned aoMethod=0;float aoStrength=.6f,aoRadius=2.f;
    LONGLONG cpuDrawHooks=0; /* RenderProfile sample frames: per-draw work outside prepareDraw (CPU profile drawHooks=) */
    LONGLONG* hookTimer=nullptr; /* &cpuDrawHooks on a timed RenderProfile sample frame (frame, gateUntimed: set before the latch) */
    void latchDrawGates(){
        NorthlightDrawGates::Inputs in;
        in.sky=celestialDiscs!=nullptr;in.blobs=shadowBlobs!=nullptr&&shadowBlobs->active();in.world=world!=nullptr;in.enabled=enabled;in.failed=failed;
        in.shadowsKey=effectKeys.settings.shadows;in.actorShadows=world&&world->actorShadowsEnabled();
        in.worldShadows=world&&world->shadowsRequested();in.debugOff=debugMode==0&&worldDebug==0;
        drawGates=NorthlightDrawGates::latch(frameDrawGates,in);
        hookTimer=sampledDrawTimers()&&NorthlightRenderThreadProbe::profiling()?&cpuDrawHooks:nullptr;
    }
    LONGLONG* sampledHookTimer()const{return hookTimer;}
    // Native sky claim: glare, then the native disc draw, for full-viewport sky draws of 1..4 primitives.
    void skyClaim(D3DPRIMITIVETYPE t,UINT count,bool& claimed){
        if(count<=4&&celestialDiscs&&enabled&&debugMode==0&&worldDebug==0&&!applied&&celestialDiscs->nativeClaimPossible(t,count)){D3DSURFACE_DESC desc;D3DVIEWPORT9 vp={};if(fullViewport(desc,&vp)&&NorthlightWorldDrawDomain::skyClaimPossible(worldMinDepth,worldMaxDepth,vp.MinZ,vp.MaxZ)){ /* viewport band pre-filter: no texture-identity read for a draw outside the sky band */
            claimed=celestialDiscs->claimNativeGlare(t,count);
            if(!claimed)claimed=celestialDiscs->claimNativeDraw(t,count,true,[&](const char* map,const float* camera){return world->celestialPalette(map,camera);});}}
    }
    void skyObserve(HRESULT hr,D3DPRIMITIVETYPE t,UINT count){
        if(count<=4&&celestialDiscs&&enabled&&!applied&&celestialDiscs->nativeObservePossible(hr,t,count)){D3DSURFACE_DESC desc;if(fullViewport(desc))celestialDiscs->observeNativeDraw(hr,t,count,true);}
    }
    // 0.3.193 faint blob: planned inside the guarded claim, applied around the real draw (outside
    // extensionWork, like the terrain shadow swap): texture 0 is shadowBlobs->faintTexture() for the draw and
    // the game's texture again after it. blobOriginal is the AddRef'd texture claim() handed back; a plan
    // holds it only when the faint texture exists.
    IDirect3DBaseTexture9* blobOriginal=nullptr;unsigned blobFaintDraws=0;
    void dropBlobFaint(){if(blobOriginal)blobOriginal->Release();blobOriginal=nullptr;}
    void planBlobFaint(IDirect3DBaseTexture9* original){
        if(!original)return;
        if(!shadowBlobs->faintTexture()){original->Release();return;}
        blobOriginal=original;
    }
    template<class Draw> HRESULT blobFaintDraw(bool claimed,Draw draw){
        if(!blobOriginal)return terrainShadowDraw(claimed,draw);
        IDirect3DBaseTexture9* original=blobOriginal;blobOriginal=nullptr;IDirect3DTexture9* faint=shadowBlobs->faintTexture();
        if(!faint){const HRESULT plain=terrainShadowDraw(claimed,draw);original->Release();return plain;}
        const HRESULT hr=terrainShadowDraw(claimed,[&]{
            {CpuScope swap(sampledHookTimer());ext->SetTexture(0,faint);}
            const HRESULT result=draw();
            {CpuScope swap(sampledHookTimer());ext->SetTexture(0,original);}
            ++blobFaintDraws;return result;});
        original->Release();
        return hr;
    }
    void blobFilter(UINT count,bool& claimed){
        const NorthlightShadowBlobFilter::Result verdict=blobClaim(count);
        claimed=verdict.claim==NorthlightShadowBlobFilter::Claim::Skip;
        if(verdict.claim==NorthlightShadowBlobFilter::Claim::Faint)planBlobFaint(verdict.original);
        if(claimed&&blobSignatureReports<4){++blobSignatureReports;IDirect3DVertexShader9* bvs=nullptr;IDirect3DPixelShader9* bps=nullptr;ext->GetVertexShader(&bvs);ext->GetPixelShader(&bps);auto vi=vsHashes.find(bvs);auto pi=psHashes.find(bps);logf("SHADOWBLOB draw signature vs=%016llx ps=%016llx primitives=%u",(unsigned long long)(vi==vsHashes.end()?0:vi->second),(unsigned long long)(pi==psHashes.end()?0:pi->second),count);drop(bvs);drop(bps);}
    }
    // 0.3.199 (rain): RainBlend. The game draws rain as a 2x modulate (DestColor/SrcColor, alpha only feeds the alpha test), so the streaks take the
    // background's colour. Around exactly the game's draw (a claimed draw never runs it) the blend becomes SrcAlpha/InvSrcAlpha/Add; the previous
    // values (the mirror answers the reads) come back right after. Texture stages, alpha test and Z stay the game's.
    // 0.3.199 (rain mist): armed draws only (it rains). Stage 0 bound to a mist texture -> skip (the 1:4 128x512 ARGB signature is the art
    // layer's own; the first game test skipped nothing with a 2x-modulate blend check, so any blend). Diagnostics for the WEATHER line:
    // draws with stage 0 unknown, mist bound on stages 1..3 (sample frames only), and the first 4 matches' draw states.
    bool mistDraw(UINT count){
        if(!mirrorState.textureKnown[0]){++weatherMistUnknown;return false;}
        if(!weatherDetect.isMist(mirrorState.textures[0])){
            if(gateFrame)for(unsigned st=1;st<4;++st)if(mirrorState.textureKnown[st]&&weatherDetect.isMist(mirrorState.textures[st])){++weatherMistOtherStage;break;} /* diagnostic: RenderProfile sample frames only */
            return false;
        }
        ++weatherMistSkips;weatherDetect.proveMist(mirrorState.textures[0]);
        if(weatherMistReports<4){++weatherMistReports;logf("WEATHER mist draw (skipped) applied=%d terrain=%d",int(applied),int(terrain));weatherDrawStates(count);--weatherStateReports;}
        return true;
    }
    // 0.3.202 (rain): RainMask. The rain streak coverage mask (red of an A8R8G8B8 the size of the effects, the streaks blended over each other, Z test the game's) lets WorldComposite leave
    // the streaks out of fog and haze. Eligible only in a terrain frame before the effects, at the effect size.
    // 0.3.202 (rain mask MRT): the mask is written by the SAME game draw through render target 1 (rainMrtPS writes oC1 = (1,1,1,streak alpha), RT1 colour-masked to
    // red, so RainBlend's SRCALPHA/INVSRCALPHA lays the streaks over each other in red; the blend state is the game's own, so RT0 keeps RainBlend's colour AND alpha:
    // a separate alpha blend would change RT0's alpha, which the game's glow reads), RT1 stays bound across consecutive rain draws (rainMrtUnbind runs before anything else that draws, clears, copies or
    // changes targets), so a frame costs a few pass breaks instead of two per rain draw and one per world draw. Objects drawn over the rain later are cut out by one
    // full-screen pass (rainMaskPrepare): rainDepth, the depth at the frame's first mask draw (rain writes no depth), against the final depth. MSAA scenes draw into a
    // multisampled mask target matching the game's RT/depth (D3D9 wants them to share type and quality), resolved once into the texture before the composite.
    // 0.3.214 (water MRT): render target 1 has two owners, never at once: the rain mask (above) and the water mask (water_renderer.h beginMrt). One unbind serves both.
    void rainMrtUnbind(){
        bool done=false;if(water&&water->isMrtBound()){water->unbindMrt();done=true;}
        if(rainMrtBound){if(!done)ext->SetRenderTarget(1,nullptr);rainMrtBound=false;}
    }
    bool rainMaskEligible(){
        IDirect3DSurface9* rt=nullptr;IDirect3DSurface9* ds=nullptr;bool ok=false;D3DSURFACE_DESC rd={},dd={};
        if(SUCCEEDED(ext->GetRenderTarget(0,&rt))&&rt){rt->GetDesc(&rd);ok=rd.Width==width&&rd.Height==height;dd.MultiSampleType=rd.MultiSampleType;dd.MultiSampleQuality=rd.MultiSampleQuality; /* no depth bound: the RT's own values */
            if(ok&&SUCCEEDED(ext->GetDepthStencilSurface(&ds))&&ds){ds->GetDesc(&dd);ok=dd.MultiSampleType==rd.MultiSampleType&&dd.MultiSampleQuality==rd.MultiSampleQuality;}
            if(ok){rainMaskWantType=rd.MultiSampleType;rainMaskWantQuality=rd.MultiSampleQuality;}}
        if(!ok&&!rainMaskMismatchLogged){rainMaskMismatchLogged=true;logf("WEATHER rain mask skipped: render target %ux%u ms=%u depthMs=%u, effects %ux%u",rd.Width,rd.Height,unsigned(rd.MultiSampleType),unsigned(dd.MultiSampleType),width,height);}
        drop(ds);drop(rt);return ok;
    }
    // Once per device: two render targets, independent write masks, separate alpha blend and ps_2_0; else the mask stays off.
    void rainMaskCaps(){
        rainCapsChecked=true;D3DCAPS9 caps={};const bool got=SUCCEEDED(ext->GetDeviceCaps(&caps));
        rainCapsOk=got&&caps.NumSimultaneousRTs>=2&&(caps.PrimitiveMiscCaps&D3DPMISCCAPS_INDEPENDENTWRITEMASKS)&&(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING)&&caps.PixelShaderVersion>=D3DPS_VERSION(2,0); /* RT1 is blended (MAX alpha) */
        if(!rainCapsOk)logf("WEATHER rain mask disabled: caps got=%d simultaneousRTs=%lu independentWriteMasks=%d mrtBlending=%d pixelShader=0x%08lx",int(got),(unsigned long)caps.NumSimultaneousRTs,int((caps.PrimitiveMiscCaps&D3DPMISCCAPS_INDEPENDENTWRITEMASKS)!=0),int((caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING)!=0),(unsigned long)caps.PixelShaderVersion);
    }
    bool rainMaskCreate(){
        HRESULT hr=ext->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&rainMask,nullptr);
        if(SUCCEEDED(hr))hr=rainMask->GetSurfaceLevel(0,&rainMaskSurface);
        if(SUCCEEDED(hr)&&rainMaskWantType!=D3DMULTISAMPLE_NONE)hr=ext->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,rainMaskWantType,rainMaskWantQuality,FALSE,&rainMaskMS,nullptr); /* the multisampled twin the draws go to */
        if(FAILED(hr)){drop(rainMaskMS);drop(rainMaskSurface);drop(rainMask);rainMaskFailed=true;logf("WEATHER rain mask disabled (%ux%u A8R8G8B8 target ms=%u) HRESULT=0x%08lx",width,height,unsigned(rainMaskWantType),(unsigned long)hr);return false;}
        rainMaskMsType=rainMaskWantType;rainMaskMsQuality=rainMaskWantQuality;
        if(!rainDepth)hr=ext->CreateTexture(width,height,1,D3DUSAGE_DEPTHSTENCIL,depthSnapshotFormat,D3DPOOL_DEFAULT,&rainDepth,nullptr); /* 0.3.202 (rain mask MRT): the depth snapshot */
        if(SUCCEEDED(hr)&&!rainMrtPS)hr=ext->CreatePixelShader(kRainMaskMrtShader,&rainMrtPS);
        if(SUCCEEDED(hr)&&!rainScrubPS)hr=ext->CreatePixelShader(kRainScrubShader,&rainScrubPS);
        if(FAILED(hr)){drop(rainMaskMS);drop(rainMaskSurface);drop(rainMask);drop(rainDepth);drop(rainMrtPS);drop(rainScrubPS);rainMaskFailed=true;logf("WEATHER rain mask disabled (rain depth snapshot / mask shaders %ux%u) HRESULT=0x%08lx",width,height,(unsigned long)hr);return false;}
        return true;
    }
    // The mask target is cleared with RT0 swapped away (the fallback when ColorFill is refused); the game's target, viewport and scissor come back.
    void rainMaskClearSwap(IDirect3DSurface9* maskTarget){
        IDirect3DSurface9* gameRT=nullptr;D3DVIEWPORT9 vp={};RECT scissor={};DWORD scissorOn=0;
        if(FAILED(ext->GetRenderTarget(0,&gameRT))||!gameRT||FAILED(ext->GetViewport(&vp))){drop(gameRT);return;}
        const bool scissorKnown=SUCCEEDED(ext->GetScissorRect(&scissor)); /* SetRenderTarget(0) resets the scissor rect too, and the mirror does not hold it */
        const bool scissorStateKnown=SUCCEEDED(ext->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissorOn));
        if(SUCCEEDED(ext->SetRenderTarget(0,maskTarget))){
            ext->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);ext->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0);if(scissorStateKnown)ext->SetRenderState(D3DRS_SCISSORTESTENABLE,scissorOn); /* the whole target: SetRenderTarget reset the viewport to it */
            ext->SetRenderTarget(0,gameRT);}
        ext->SetViewport(&vp);if(scissorKnown)ext->SetScissorRect(&scissor);drop(gameRT);
    }
    // The frame's first mask draw: depth snapshot (the RESZ copy, one pass break), then the mask target cleared (ColorFill first: no target swap), before RT1 is bound.
    // False: no mask this frame (rainDepthOk stays false; the later rain draws skip with reason 32).
    bool rainMaskStart(IDirect3DSurface9* maskTarget){
        rainMaskCleared=true;rainDepthOk=false;
        IDirect3DSurface9* bound=nullptr;const bool peeked=ext->peekDepthStencilSurface(bound);
        if(!peeked){bound=nullptr;if(FAILED(ext->GetDepthStencilSurface(&bound)))bound=nullptr;}
        const bool sameDepth=bound&&bound==worldDepth;if(!peeked)drop(bound);
        if(!sameDepth)return false; /* the snapshot is of the terrain depth; a rain draw into another depth buffer is not ours */
        bool snapped=false;{ExtensionDevice::RawScope raw(*ext);snapped=resolveDepthInto(rainDepth,false);}
        /* The snapshot (failed or not) ends with a state-block Apply, which forgets the mirror's stage-0 texture; the game does not set the rain texture again for its next
           rain draws, so without this read only the first rain draw of the frame matched (as at the 0.3.199 rain boundary): the rest lost RainBlend and the
           mask, and the weather intensity, which drives the rain fog, fell to one draw's worth. The read goes to the device and the mirror learns it back. */
        {IDirect3DBaseTexture9* stage0=nullptr;if(SUCCEEDED(ext->GetTexture(0,&stage0)))drop(stage0);}
        if(!snapped)return false;
        if(FAILED(ext->ColorFill(maskTarget,nullptr,0)))rainMaskClearSwap(maskTarget);
        rainDepthOk=true;return true;
    }
    // Everything that makes a rain draw eligible for the mask, else a reason mask (bit 1: target/size/MSAA, 2: caps, 4: game vertex shader, 8: game pixel shader, 16: stage setup,
    // 32: depth snapshot, 64: resources or RT1): the draw is then made as it is. True: RT1 holds the mask target, rainMrtPS and the MRT states are set (rainMrtEnd undoes them).
    bool rainMrtBegin(){
        if(applied||!terrain||!enabled||failed||!world||rainMaskFailed||!width||!height){rainMrtUnbind();return false;}
        if(!rainMaskFrame){rainMaskFrame=true;rainMaskOk=rainMaskEligible();}
        if(!rainCapsChecked)rainMaskCaps();
        unsigned why=0;bool vsBound=false,psBound=false;unsigned vsModel=0;DWORD spec=0;
        DWORD st[9]={~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u}; /* color op/arg1/arg2, alpha op/arg1/arg2, stage 1 color op, texcoord index, texture transform */
        if(!rainMaskOk)why|=1;
        if(!rainCapsOk)why|=2;
        if(!why){
            IDirect3DVertexShader9* v=nullptr;IDirect3DPixelShader9* p=nullptr;
            const bool borrowed=ext->peekVertexShader(v);if(!borrowed&&FAILED(ext->GetVertexShader(&v)))v=nullptr;
            if(v){vsBound=true;auto it=vsMajor.find(v);vsModel=it==vsMajor.end()?0:it->second;}if(!borrowed)drop(v);
            if(ext->peekPixelShader(p))psBound=true;else{if(SUCCEEDED(ext->GetPixelShader(&p))&&p)psBound=true;drop(p);}
            if(vsBound&&(vsModel<1||vsModel>2))why|=4; /* vs_1_x/vs_2_x write the fixed oD0/oT0/oFog the ps_2_0 reads (the game's rain has one); vs_3_0 needs a ps_3_0 */
            if(psBound)why|=8;
            if(!why){ /* stage 0 MODULATE texture x diffuse (colour and alpha), nothing on stage 1, no texture transform, no specular: what rainMrtPS reproduces */
                static const struct{DWORD stage;D3DTEXTURESTAGESTATETYPE type;} reads[9]={{0,D3DTSS_COLOROP},{0,D3DTSS_COLORARG1},{0,D3DTSS_COLORARG2},{0,D3DTSS_ALPHAOP},{0,D3DTSS_ALPHAARG1},{0,D3DTSS_ALPHAARG2},{1,D3DTSS_COLOROP},{0,D3DTSS_TEXCOORDINDEX},{0,D3DTSS_TEXTURETRANSFORMFLAGS}};
                bool known=SUCCEEDED(ext->GetRenderState(D3DRS_SPECULARENABLE,&spec));
                for(int i=0;i<9;++i)known=SUCCEEDED(ext->GetTextureStageState(reads[i].stage,reads[i].type,&st[i]))&&known;
                const bool arg2=(st[2]==D3DTA_DIFFUSE||st[2]==D3DTA_CURRENT),alphaArg2=(st[5]==D3DTA_DIFFUSE||st[5]==D3DTA_CURRENT);
                if(!(known&&st[0]==D3DTOP_MODULATE&&st[1]==D3DTA_TEXTURE&&arg2&&st[3]==D3DTOP_MODULATE&&st[4]==D3DTA_TEXTURE&&alphaArg2&&st[6]==D3DTOP_DISABLE&&st[7]==0&&st[8]==D3DTTFF_DISABLE&&spec==FALSE))why|=16;
            }
        }
        IDirect3DSurface9* maskTarget=nullptr;
        if(!why){
            if(rainMaskSurface&&(rainMaskMsType!=rainMaskWantType||rainMaskMsQuality!=rainMaskWantQuality)){rainMrtUnbind();drop(rainMaskMS);drop(rainMaskSurface);drop(rainMask);} /* the game's MSAA changed without a release */
            if(!rainMaskSurface&&!rainMaskCreate())why|=64;
            else{
                maskTarget=rainMaskMS?rainMaskMS:rainMaskSurface;
                if(!rainMaskCleared&&!rainMaskStart(maskTarget))why|=32;
                else if(!rainDepthOk)why|=32;
                else if(!rainMrtBound){
                    if(FAILED(ext->SetRenderTarget(1,maskTarget)))why|=64;
                    else{rainMrtBound=true;++rainMrtRuns;}
                }
            }
        }
        if(why){
            rainMrtUnbind();++rainMaskSkips;
            if(!rainSkipLogged){rainSkipLogged=true;logf("WEATHER rain mask skip: reason=%u vs=%d vsModel=%u ps=%d stage0 color=%ld(%ld,%ld) alpha=%ld(%ld,%ld) stage1 color=%ld texcoord=%ld texTransform=%ld specular=%lu",why,int(vsBound),vsModel,int(psBound),long(st[0]),long(st[1]),long(st[2]),long(st[3]),long(st[4]),long(st[5]),long(st[6]),long(st[7]),long(st[8]),(unsigned long)spec);}
            return false;
        }
        /* Only RT1's write mask changes: RT0's colour and alpha blend exactly as RainBlend's draw without the mask. */
        static const D3DRENDERSTATETYPE types[1]={D3DRS_COLORWRITEENABLE1};
        static const DWORD want[1]={D3DCOLORWRITEENABLE_RED};
        for(int i=0;i<1;++i){rainMrtPrevKnown[i]=SUCCEEDED(ext->GetRenderState(types[i],&rainMrtPrev[i]));ext->SetRenderState(types[i],want[i]);}
        ext->SetPixelShader(rainMrtPS);
        return true;
    }
    void rainMrtEnd(){
        static const D3DRENDERSTATETYPE types[1]={D3DRS_COLORWRITEENABLE1};
        for(int i=0;i<1;++i)if(rainMrtPrevKnown[i])ext->SetRenderState(types[i],rainMrtPrev[i]);
        ext->SetPixelShader(nullptr); /* eligibility: the game had no pixel shader bound (its vertex shader, if any, stays) */
    }
    // The rain draw: when eligible the same single game draw writes the mask through RT1; otherwise as it was (RT1 unbound first: a pixel shader that does not write oC1 must not meet it).
    template<class Draw> HRESULT rainMrtDraw(Draw draw){
        bool masked=false;
        extensionWork("rain mask",[&]{masked=rainMrtBegin();});
        if(!masked){rainMrtUnbind();return draw();}
        const HRESULT hr=draw();
        extensionWork("rain mask",[&]{rainMrtEnd();});
        if(SUCCEEDED(hr)){rainMaskDrawn=true;++rainMaskDraws;}
        return hr;
    }
    // renderEffects: the mask for this frame's composite, or nullptr. Resolves the MSAA twin, then one full-screen pass over the texture: pixels whose depth differs from rainDepth
    // (something was drawn over the rain) get alpha 0, the rest are discarded. Its own SavedState; effectState() sets the common pass state.
    IDirect3DTexture9* rainMaskPrepare(){
        rainMrtUnbind();
        if(!rainMaskDrawn||!rainDepthOk||!rainMask||!rainMaskSurface||!rainDepth||!rainScrubPS||!depthTex)return nullptr;
        if(rainMaskMS){const HRESULT rh=ext->StretchRect(rainMaskMS,nullptr,rainMaskSurface,nullptr,D3DTEXF_NONE);if(FAILED(rh)){rainMaskResolveHr=rh;return nullptr;}} /* logged from the weather update, not inside the AO..world span; averages the samples' alpha: the AA edge */
        SavedState scrub(ext,&stateBlocks);
        if(!scrub.ok)return nullptr;
        effectState();
        HRESULT hr=ext->SetRenderTarget(0,rainMaskSurface);
        if(SUCCEEDED(hr)){
            ext->SetTexture(0,depthTex);ext->SetTexture(1,rainDepth);
            for(DWORD s=0;s<2;++s){ext->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT);ext->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);ext->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);} /* both depths exact, texel for texel */
            ext->SetPixelShader(rainScrubPS);ext->SetRenderState(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED);
            hr=quad(width,height);
        }
        if(FAILED(hr)){rainMaskResolveHr=hr;return nullptr;}
        return rainMask;
    }
    template<class Draw> HRESULT rainBlendDraw(bool rain,bool claimed,Draw draw){
        if(!rain)return blobFaintDraw(claimed,draw);
        return blobFaintDraw(claimed,[&]{
            static const D3DRENDERSTATETYPE types[4]={D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP};
            static const DWORD want[4]={TRUE,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD};
            DWORD prev[4]={};bool changed[4]={};
            for(int i=0;i<4;++i)if(SUCCEEDED(ext->GetRenderState(types[i],&prev[i]))&&prev[i]!=want[i]){changed[i]=true;ext->SetRenderState(types[i],want[i]);}
            /* 0.3.202 (rain mask MRT): the one game draw also writes the mask (rainMrtDraw); the draw lambdas are pure ext->Draw* calls (also in the command-stream replay) */
            const HRESULT hr=rainMrtDraw(draw);
            for(int i=0;i<4;++i)if(changed[i])ext->SetRenderState(types[i],prev[i]);
            return hr;});
    }
    // One game draw: capture, the native sky and blob claims, the real draw (outside every extension
    // region, so a fault keeps the game's call) and the sky observation. The capture and its order are
    // prepareDraw's in both paths. Gates on: capture and claims share one region (a fault in either
    // skipped the rest before too, via extensionFault), the sky work runs only for count<=4 while the
    // frame's sky gate is open, the blob test only while its gate is; the observation follows the draw,
    // so it keeps a region of its own. Gates off: the 0.3.184 regions one by one.
    NorthlightParticleCapture::Capture particleCapture;bool particleOn=false;
    NorthlightParticleCapture::Prepared prepareParticleDraw(bool skipped){
        NorthlightParticleCapture::Prepared out;
        if(!particleOn||skipped||applied||!enabled||failed||!projectionValid||!world||!worldDepth)return out;
        NorthlightDrawSnapshot::Ref<IDirect3DVertexShader9> ownedVS;NorthlightDrawSnapshot::Ref<IDirect3DPixelShader9> ps;
        IDirect3DVertexShader9* vs=nullptr;
        if(!ext->peekVertexShader(vs)){if(FAILED(ext->GetVertexShader(ownedVS.out())))return out;vs=ownedVS.p;}
        if(!vs||!particleCapture.known(vs))return out;
        DWORD blend=0,destination=0;if(FAILED(ext->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||!blend||
            FAILED(ext->GetRenderState(D3DRS_DESTBLEND,&destination))||destination!=D3DBLEND_ONE)return out;
        D3DVIEWPORT9 viewport{};DWORD depth=0;
        if(FAILED(ext->GetViewport(&viewport))||FAILED(ext->GetRenderState(D3DRS_ZENABLE,&depth))||
            !NorthlightWorldDrawDomain::accepts(projectionValid,depth!=0,worldMinDepth,worldMaxDepth,viewport.MinZ,viewport.MaxZ)||
            !nameMaskFullViewport(viewport)||!worldDepthBound())return out;
        float inverse[16],camera[3],projection[3];
        if(!world->particleContext(inverse,camera,projection)||FAILED(ext->GetPixelShader(ps.out()))||!ps.p)return out;
        return particleCapture.prepare(ext,vs,ps.p,spellArgs,inverse,camera,projection,world->particleFrame());
    }
    void submitParticleDraw(const NorthlightParticleCapture::Prepared& prepared,bool accepted,bool deferred){
        if(particleOn&&world)NorthlightParticleCapture::Capture::submit(prepared,accepted,deferred,world->particleFrame());
    }
    template<class Capture,class Draw> HRESULT drawHook(D3DPRIMITIVETYPE t,UINT count,Capture capture,Draw draw){
        auto nativeDraw=[&]{return aoActorNativeDraw(draw);};
        bool claimed=false,rainBlend=false,mist=false;
        // 0.3.198 (rain): the whole per-draw cost of weather detection, before any other work and in both gate paths: one pointer comparison.
        if(weatherDetect.hot&&mirrorState.textureKnown[0]&&mirrorState.textures[0]==weatherDetect.hot){weatherSample.primitives+=count;++weatherSample.draws;rainBlend=weatherDetect.hotKind==NorthlightWeather::Kind::Rain&&world&&world->rainBlendSetting();} /* 0.3.199 (rain): counted after applied too (the rest of the rain); 0.3.202: no boundary here, the effects run at the UI again */
        else if(weatherDetect.mistArmed)mist=mistDraw(count); /* 0.3.199 (rain mist): a mist puff in rain is skipped; unarmed, one bool test */
        if(!skyBandSeen&&mirrorState.viewportKnown&&NorthlightWorldDrawDomain::classify(worldMinDepth,worldMaxDepth,mirrorState.viewport.MinZ,mirrorState.viewport.MaxZ)==NorthlightWorldDrawDomain::Band::Sky)skyBandSeen=true; /* WeatherMemory: the world drew in the sky band this frame (the mirror's viewport, no device read) */
        if(rainMrtBound&&!rainBlend)rainMrtUnbind(); /* 0.3.202 (rain mask MRT): every other draw (and all its capture/effect work) meets the game's targets only; dry frames one bool test */
        waterPending.valid=false; /* 0.3.214 (water MRT): set by this draw's captureWater only */
        if(water&&water->isMrtBound()&&(rainBlend||applied||!enabled||failed||!nextDrawIsWater()))rainMrtUnbind(); /* the water mask's target 1 stays bound across consecutive water draws only */
        if(!frameDrawGates){
            dropBlobFaint();prepareDraw(capture,count);
            {CpuScope hooks(sampledHookTimer());
                extensionWork("native sky claim",[&]{skyClaim(t,count,claimed);});
                if(!claimed&&blobFilterActive())extensionWork("blob shadow filter",[&]{blobFilter(count,claimed);});}
            NorthlightParticleCapture::Prepared particle;
            if(particleOn)extensionWork("particle light capture",[&]{particle=prepareParticleDraw(claimed||mist||rainBlend||blobOriginal||shadowSwapOriginal);});
            if(spellOn&&spellGate()&&spellDraw(claimed||mist||blobOriginal||shadowSwapOriginal,rainBlend||mist)){
                submitParticleDraw(particle,true,true);return D3D_OK; /* accepted current-frame record, replayed after effects */
            }
            const HRESULT hr=waterMrtDraw(nativeDraw,[&]{return nameMaskDraw(rainBlend,claimed||mist,nativeDraw);});
            submitParticleDraw(particle,SUCCEEDED(hr)&&!claimed&&!mist&&!blobOriginal&&!shadowSwapOriginal,false); /* 0.3.199 (rain mist): a mist draw is skipped; 0.3.212 (task 32): the name mask wraps the draw; 0.3.214 (water MRT): a water draw writes the mask itself */
            if(!claimed){CpuScope hooks(sampledHookTimer());extensionWork("native sky observation",[&]{skyObserve(hr,t,count);});}
            return hr;
        }
        mirrorState.gate.noteFirst(mirrorState.gate.drawTid); /* prepareDraw's prologue */
        dropTerrainShadowSwap();dropBlobFaint();
        const bool sky=count<=4&&drawGates.sky;
        const char* stage="draw capture/effects";
        extensionWork(stage,[&]{
            prepareDrawImpl(capture,count);
            CpuScope hooks(sampledHookTimer());
            if(sky){stage="native sky claim";skyClaim(t,count,claimed);}
            if(!claimed&&drawGates.blob){stage="blob shadow filter";if(blobFilterActive())blobFilter(count,claimed);}
        });
        NorthlightParticleCapture::Prepared particle;
        if(particleOn)extensionWork("particle light capture",[&]{particle=prepareParticleDraw(claimed||mist||rainBlend||blobOriginal||shadowSwapOriginal);});
        if(spellOn&&spellGate()&&spellDraw(claimed||mist||blobOriginal||shadowSwapOriginal,rainBlend||mist)){
            submitParticleDraw(particle,true,true);return D3D_OK;
        }
        const HRESULT hr=waterMrtDraw(nativeDraw,[&]{return nameMaskDraw(rainBlend,claimed||mist,nativeDraw);});
            submitParticleDraw(particle,SUCCEEDED(hr)&&!claimed&&!mist&&!blobOriginal&&!shadowSwapOriginal,false); /* 0.3.199 (rain mist): a mist draw is skipped; 0.3.212 (task 32): the name mask wraps the draw; 0.3.214 (water MRT): a water draw writes the mask itself */
        if(sky&&!claimed){CpuScope hooks(sampledHookTimer());extensionWork("native sky observation",[&]{skyObserve(hr,t,count);});}
        return hr;
    }
    // 0.3.154: blob shadow claim with the profile frame's counts (claim() reads texture 0 for 1..256 primitives).
    NorthlightShadowBlobFilter::Result blobClaim(UINT count){
        if(gateFrame){++gateCounts.blobCalls;if(count&&count<=256)++gateCounts.blobTextures;}
        const auto verdict=shadowBlobs->claim(count);if(gateFrame&&verdict.claim==NorthlightShadowBlobFilter::Claim::Skip)++gateCounts.blobClaimed;return verdict;
    }
    // ---- task 34: the spell layer (spell_layer.h; the wiring doc is at its top) ----
    // SpellLayer=1 (creation-time key): translucent M2 draws without a depth write are held back and replayed right after the effects at the boundary
    // (beforeDraw / uiFallbackDraw), exactly with the state they had. spellOn is the one bool every hook tests; with SpellLayer=0 nothing here runs.
    // Every layer call runs inside extensionWork (a fault drops the layer: reset()) on the thread that executes the device.
    NorthlightSpellLayer::Layer spell;NorthlightSpellLayer::FadeRun spellFade;NorthlightSpellLayer::DrawArgs spellArgs; /* spellArgs: set by the four draw overrides (SpellLayer=1 only) */
    bool spellOn=false;unsigned spellClasses=15,spellIdle=0;unsigned spellSampleFrame=~0u;bool spellSampleOn=false;
    bool zFuncAware=true; /* DepthFuncAware (a creation-time key): ZFUNC counts as part of depth-tested, and the UI fallback requires ZFUNC ALWAYS */
    bool spellDomain=false,spellZEnable=false;IDirect3DVertexShader9* spellVs=nullptr; /* prepareDrawImpl's reads of this draw (identities only; SpellLayer=1 only) */
    void* spellTexKey=nullptr;IDirect3DSurface9* spellTexFor=nullptr; /* the layer target's container texture (null: T is not a texture level), computed once per target */
    void* spellQuery[8]={};unsigned spellQueryFrame[8]={},spellQueryN=0,spellQueryUnknownFrame=0;bool spellQueryUnknown=false; /* open D3DQUERYTYPE_OCCLUSION brackets (raw queries), by Issue(BEGIN) frame; spellQueryUnknown: a bracket was lost to a full table (its frame in spellQueryUnknownFrame) */
    // SpellTrace: spellTraceLeft frames of SPELLTRACE lines, from the first world frame at or after frame 300 (Diagnostics only); spellTraceWindow: the next frame is traced.
    bool spellOptionsPending=false;
    unsigned spellTraceLeft=0,spellTraceSetting=0,spellDrawIndex=0;bool spellTraceWindow=false,spellTraceFrame=false,spellTraceSeen=false;
    LONGLONG spellReplayTicks=0,spellRecordTicks=0,spellGateTicks=0,spellFlushTicks=0;void* spellGlowTex=nullptr;unsigned spellGlowPassN=0,spellGlowLogs=0; /* spellGlowTex: the destination container of this frame's allowed glow copy (identity only; valid when spellGlowFrame==frame+1); spellGlowPassN: passes let through since that copy */ unsigned spellCopiesAllowed=0,spellGlowPasses=0,spellGlowFrame=0,spellGateDraws=0,spellGateTimed=0,spellGateSeq=0,spellRecords=0,spellFlushes=0; /* spellFlushTicks/spellFlushes: the hazard flushes of gate draws, every one timed and kept out of the gate time */ /* sampled frames: spellGateDraws world-draw gate calls, every 8th timed (spellGateTimed, spellGateTicks = their time minus the record time inside them); spellRecordTicks/spellRecords: every Layer::defer() */NorthlightSpellLayer::Mode spellFrameMode=NorthlightSpellLayer::Mode::Off;
    // Hazard diagnostics (Diagnostics=1): the layer's latest event is consumed after each layer call that can raise one; the first 64 of the session are
    // logged as SPELLHAZARD. spellWin* accumulate every frame since the previous SPELLLAYER line (that line itself describes one sampled frame).
    static constexpr unsigned kSpellHazardLogs=64;
    unsigned spellEventSeen=0,spellHazardLogs=0,spellWinFrames=0,spellWinDeferring=0,spellWinCooldown=0;bool spellInDraw=false;NorthlightSpellLayer::Counters spellWin;
    static const char* spellKindName(NorthlightSpellLayer::Kind k){static const char* const n[]={"ignore","opaque","translucentZ","defer","inplace","hazard"};return n[unsigned(k)];}
    static const char* spellInPlaceName(NorthlightSpellLayer::InPlaceKind k){static const char* const n[]={"none","water","rain","name","fade"};return n[unsigned(k)];}
    /* the layer's bulk reads of the device state cache (mirrorState): one guard per record, the arrays read in place, every unknown slot by the Get that teaches it */
    SyncedSlots spellSynced; /* the layer's 'Model equals the device' bits: the mirror's setters clear them (device_mirror.h), the layer sets them */
    NorthlightSpellLayer::MirrorView spellView;alignas(MirrorGuard) unsigned char spellGuard[sizeof(MirrorGuard)];
    static bool spellViewEnter(void* c){
        Device* d=static_cast<Device*>(c);MirrorGuard* g=new(d->spellGuard) MirrorGuard(d->mirrorState.gate,MirrorSite::Device);
        if(d->mirrorState.active())return true;
        g->~MirrorGuard();return false; /* disabled, recording a state block or in a raw scope: Get calls */
    }
    static void spellViewLeave(void* c){reinterpret_cast<MirrorGuard*>(static_cast<Device*>(c)->spellGuard)->~MirrorGuard();}
    void spellViewInit(){
        DeviceMirror& m=mirrorState;NorthlightSpellLayer::MirrorView& v=spellView;
        v.rs=m.renderStates;v.rsKnown=m.renderStateKnown;v.smp=m.samplers;v.smpKnown=m.samplerKnown;v.tex=m.textures;v.texKnown=m.textureKnown;
        v.vsF=m.vsFloat;v.vsFKnown=m.vsFloatKnown;v.psF=m.psFloat;v.psFKnown=m.psFloatKnown;v.vsI=m.vsInt;v.vsIKnown=m.vsIntKnown;v.vsB=m.vsBool;v.vsBKnown=m.vsBoolKnown;
        v.vs=&m.vertexShader;v.vsKnown=&m.vertexShaderKnown;v.ps=&m.pixelShader;v.psKnown=&m.pixelShaderKnown;v.decl=&m.declaration;v.declKnown=&m.declarationKnown;
        v.fvf=&m.fvf;v.fvfKnown=&m.fvfKnown;v.vp=&m.viewport;v.vpKnown=&m.viewportKnown;v.enter=&Device::spellViewEnter;v.leave=&Device::spellViewLeave;v.ctx=this;
    }
    static uint64_t spellClock(void*){LARGE_INTEGER t;QueryPerformanceCounter(&t);return uint64_t(t.QuadPart);}
    static bool spellTiming(void* c){return static_cast<Device*>(c)->sampledOnce();} /* the layer's drains time their save, upload and restore on sampled frames only */
    static unsigned spellTextureGeneration(void*){return NorthlightMirrorResources::generation.load(std::memory_order_relaxed);}
    static void spellMarkGuard(void*,void* raw,bool on){NorthlightTrackedBuffers::markSpellGuard(raw,on);}
    static void spellBufferHook(void* context,void* raw,UINT offset,UINT size,DWORD flags,DWORD usage,unsigned locks){static_cast<Device*>(context)->spellBufferWrite(raw,offset,size,flags,usage,locks);}
    static void spellTextureHook(void* context,void* texture){static_cast<Device*>(context)->spellTextureWrite(texture);}
    static void spellQueryHook(void* context,void* query,DWORD flags){static_cast<Device*>(context)->spellQueryIssue(static_cast<IDirect3DQuery9*>(query),flags);}
    void spellSync(){NorthlightMirrorWrite::pending.store(spell.pending()?1u:0u,std::memory_order_relaxed);} /* what the resource proxies test per texture write */
    void spellConfigure(){
        spellOn=world&&world->spellLayerSetting()!=0;
        if(!spellOn){logf("SPELLLAYER config on=0");return;}
        spellClasses=world->spellLayerClassesSetting();spellTraceLeft=spellTraceSetting=world->spellTraceSetting();
        NorthlightSpellLayer::Config cfg;cfg.on=true;cfg.classMask=spellClasses;
        cfg.conflictCopy=selectedBackend!=NorthlightBackend::Kind::Native; /* the copy-out read-back needs DXVK's direct-mapped dynamic buffers */
        cfg.readBackLockFlags=NorthlightUpload::readBackLock(); /* fixed before CreateDevice (backend()) */
        cfg.markGuard=&Device::spellMarkGuard;cfg.markCtx=this;cfg.savedPool=&stateBlocks;cfg.fogOn=world->spellFogSetting()!=0;cfg.fog=&world->spellFogFrame();cfg.fogLog=true; /* SpellFog: Northlight's fog folded into the recorded fog constants; the first samples per sub-class are kept for SPELLFOG (a bounded buffer) */
        spellViewInit();cfg.source=&spellView;cfg.synced=&spellSynced;mirrorState.synced=&spellSynced;cfg.dirtyDisableOnMismatch=true; /* fail safe: the first synced-bit mismatch the sampled-frame cross-check finds ends the dirty record for the session */
        cfg.clock=&Device::spellClock;cfg.timing=&Device::spellTiming;cfg.clockCtx=this;
        cfg.generation=&Device::spellTextureGeneration;cfg.genCtx=this; /* the render-target verdict of a texture is cached until a resource proxy is created or destroyed */
        spell.attach(ext);spell.configure(cfg);
        NorthlightTrackedBuffers::spellHook={&Device::spellBufferHook,this};
        NorthlightMirrorWrite::sink={&Device::spellTextureHook,&Device::spellQueryHook,this};NorthlightMirrorWrite::queries.store(1,std::memory_order_relaxed);
        spellFrameMode=spell.mode();
        logf("SPELLLAYER config on=1 classes=%u conflictCopy=%d backend=%s trace=%u arenaMiB=%u maxEntries=%u",spellClasses,int(cfg.conflictCopy),NorthlightBackend::name(selectedBackend),spellTraceLeft,unsigned(cfg.arenaBytes>>20),cfg.maxEntries);
    }
    void spellTeardown(){ /* before the extension device goes: drop every reference and every hook that points at this Device */
        if(!spellOn)return;
        spell.reset();spell.attach(nullptr);spellSync();spellQueryN=0;spellQueryUnknown=false;
        mirrorState.synced=nullptr; /* the mirror's setters stop clearing bits that are about to go away */
        if(NorthlightTrackedBuffers::spellHook.context==this)NorthlightTrackedBuffers::spellHook={};
        if(NorthlightMirrorWrite::sink.context==this){NorthlightMirrorWrite::queries.store(0,std::memory_order_relaxed);NorthlightMirrorWrite::pending.store(0,std::memory_order_relaxed);NorthlightMirrorWrite::sink={};}
        spellOn=false;
    }
    // SpellTrace lines only; every call site tests spellTraceFrame first (one bool when the trace is off).
    void spellEvent(const char* what,unsigned a=0,unsigned b=0,unsigned c=0){logf("SPELLTRACE frame=%u event=%s pending=%u a=%u b=%u c=%u",frame,what,spell.pending(),a,b,c);}
    // The bound pixel shader's model (psMajor) and hash, by identity lookup.
    const NorthlightShaderReads::ReadSet* spellReads(void* shader)const{const auto it=shaderReads.find(shader);return it==shaderReads.end()?nullptr:&it->second;}
    void spellPixelShader(unsigned* major,unsigned long long* hash,const NorthlightShaderReads::ReadSet** reads=nullptr,const void** identity=nullptr,int* fogReg=nullptr){
        IDirect3DPixelShader9* ps=nullptr;const bool b=ext->peekPixelShader(ps);if(!b&&FAILED(ext->GetPixelShader(&ps)))ps=nullptr;
        if(ps){
            if(reads)*reads=spellReads(static_cast<void*>(ps));
            if(identity)*identity=static_cast<const void*>(ps);
            if(major){auto it=psMajor.find(ps);*major=it==psMajor.end()?0u:it->second;}
            if(fogReg){auto it=psFogReg.find(ps);*fogReg=it==psFogReg.end()?-1:it->second;}
            if(hash){auto it=psHashes.find(ps);*hash=it==psHashes.end()?0ull:(unsigned long long)it->second;}
        }
        if(!b)drop(ps);
    }
    // After a layer call that can raise an event (a hazard that flushed, a refused defer): consume it. Diagnostics only, the first kSpellHazardLogs of the
    // session; detail(buf,size) formats the event's own fields (called only when it is logged). One unsigned compare otherwise.
    template<class F> void spellEventDone(const char* what,F&& detail){
        if(spell.eventSeq()==spellEventSeen)return;
        spellEventSeen=spell.eventSeq();
        if(!diagnostics()||spellHazardLogs>=kSpellHazardLogs)return;
        ++spellHazardLogs;char d[256];d[0]=0;detail(d,sizeof d);
        const NorthlightSpellLayer::Layer::Event& ev=spell.lastEvent();
        logf("SPELLHAZARD frame=%u draw=%u reason=%s by=%s pending=%u event=%s %s",frame,drawCalls,NorthlightSpellLayer::reasonName(ev.reason),spellInPlaceName(ev.by),ev.pending,what,d);
    }
    // The same for the draw that triggered the event: its facts (the DrawFacts the classifier saw) and shader hashes.
    void spellDrawEventDone(const NorthlightSpellLayer::DrawFacts& f){
        if(spell.eventSeq()==spellEventSeen)return;
        spellEventSeen=spell.eventSeq();
        if(!diagnostics()||spellHazardLogs>=kSpellHazardLogs)return;
        ++spellHazardLogs;
        unsigned long long vsHash=0,psHash=0;
        {auto it=vsHashes.find(spellVs);if(spellVs&&it!=vsHashes.end())vsHash=it->second;}
        spellPixelShader(nullptr,&psHash);
        const NorthlightSpellLayer::Layer::Event& ev=spell.lastEvent();
        logf("SPELLHAZARD frame=%u draw=%u reason=%s by=%s pending=%u family=%u skinned=%d fixed=%d zw=%d blend=%d src=%u dst=%u cw=%u domain=%d isTarget=%d vs=%016llx ps=%016llx",
             frame,drawCalls,NorthlightSpellLayer::reasonName(ev.reason),spellInPlaceName(ev.by),ev.pending,unsigned(f.family),int(f.skinned),int(f.fixedFunction),int(f.zWrite),int(f.blend),f.srcBlend,f.destBlend,f.colorWrite,int(f.worldDomain),int(f.rt0IsTarget),vsHash,psHash);
    }
    // An unknown order or resource dependency: the pending draws are replayed now, before the caller's operation.
    void spellFlush(NorthlightSpellLayer::Reason r,bool cooldown=true,NorthlightSpellLayer::InPlaceKind by=NorthlightSpellLayer::InPlaceKind::None,const char* what="flush",unsigned a=0,unsigned b=0){
        if(!spellOn||!spell.pending())return;
        nameMaskRunEnd(); /* 0.3.212 (task 32): a flush between two names (a copy, a texture write) must not replay with the name stencil on */
        extensionWork("spell hazard",[&]{ExtensionDevice::RawScope raw(*ext);spell.hazard(r,cooldown,by);});
        spellSync();
        if(spellTraceFrame)logf("SPELLTRACE frame=%u event=flush reason=%s by=%s",frame,NorthlightSpellLayer::reasonName(r),spellInPlaceName(by));
        if(!spellInDraw)spellEventDone(what,[&](char* buf,size_t n){std::snprintf(buf,n,"a=%u b=%u",a,b);}); /* a draw reports itself, with its facts */
    }
    void spellTextureWrite(void* texture){ /* a raw base texture was (or is about to be) written */
        if(!spellOn||!spell.pendingSamples(texture))return;
        if(spellTraceFrame)logf("SPELLTRACE frame=%u event=texwrite texture=%p",frame,texture);
        extensionWork("spell texture write",[&]{ExtensionDevice::RawScope raw(*ext);spell.onTextureWrite(texture);});
        spellSync();
        spellEventDone("texwrite",[&](char* buf,size_t n){std::snprintf(buf,n,"texture=%p",texture);});
    }
    void spellBufferWrite(void* raw,UINT offset,UINT size,DWORD flags,DWORD usage,unsigned locks){ /* Buffer::Lock, before the lock is forwarded, marked buffers only */
        if(!spellOn||!spell.pending())return;
        /* A write that conflicts with nothing (the ring append) needs no raw scope: no device call follows. A conflicting one copies the guarded bytes
           (raw Lock/Unlock of the buffers) or flushes (replay draws and Sets), so it runs in a RawScope like spellFlush and spellTextureWrite. */
        const bool conflict=spell.bufferWriteConflicts(raw,offset,size,flags);
        const unsigned conflicts=spell.frameCounters().conflicts,flushes=spell.frameCounters().flushes;
        if(conflict){
            extensionWork("spell buffer write",[&]{ExtensionDevice::RawScope scope(*ext);spell.onBufferWrite(raw,offset,size,flags,usage,locks);});
            spellSync();
            spellEventDone("bufwrite",[&](char* buf,size_t n){std::snprintf(buf,n,"buffer=%p offset=%u size=%u flags=0x%lx usage=0x%lx locks=%u",raw,offset,size,(unsigned long)flags,(unsigned long)usage,locks);});
        }
        if(spellTraceFrame)logf("SPELLTRACE frame=%u event=bufwrite buffer=%p offset=%u size=%u flags=0x%lx usage=0x%lx locks=%u result=%s",frame,raw,offset,size,(unsigned long)flags,(unsigned long)usage,locks,
            spell.frameCounters().flushes>flushes?"flushed":spell.frameCounters().conflicts>conflicts?"copied":"clear");
    }
    void spellQueryIssue(IDirect3DQuery9* query,DWORD flags){ /* Issue on a raw query (resource proxy), occlusion only */
        if(!spellOn||!query||query->GetType()!=D3DQUERYTYPE_OCCLUSION)return;
        unsigned at=0;while(at<spellQueryN&&spellQuery[at]!=query)++at;
        if(flags&D3DISSUE_BEGIN){
            if(at==spellQueryN){
                if(spellQueryN==8){ /* full: the oldest open bracket is forgotten, so an open query of unknown identity remains until the frame-end sweep */
                    for(unsigned i=1;i<8;++i){spellQuery[i-1]=spellQuery[i];spellQueryFrame[i-1]=spellQueryFrame[i];}--spellQueryN;at=spellQueryN;
                    spellQueryUnknown=true;spellQueryUnknownFrame=frame;
                    if(spellTraceFrame)logf("SPELLTRACE frame=%u event=occlusion_overflow oldest open bracket dropped, unknown open query until the sweep",frame);
                }
                ++spellQueryN;spellQuery[at]=query;
            }
            spellQueryFrame[at]=frame;
        }
        if((flags&D3DISSUE_END)&&at<spellQueryN){for(unsigned i=at+1;i<spellQueryN;++i){spellQuery[i-1]=spellQuery[i];spellQueryFrame[i-1]=spellQueryFrame[i];}--spellQueryN;}
        if(spellTraceFrame)logf("SPELLTRACE frame=%u event=occlusion issue=%lu open=%u unknown=%d pending=%u",frame,(unsigned long)flags,spellQueryN,int(spellQueryUnknown),spell.pending());
    }
    // Clear: only the part that touches the layer's target (colour) or depth is a hazard.
    void spellClear(DWORD flags){
        if(!spellOn||!spell.pending())return;
        bool hit=false;IDirect3DSurface9* s=nullptr;
        if((flags&D3DCLEAR_TARGET)&&spell.target()){const bool b=ext->peekRenderTarget(0,s);if(!b&&FAILED(ext->GetRenderTarget(0,&s)))s=nullptr;hit=s==spell.target();if(!b)drop(s);}
        if(!hit&&(flags&(D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL))&&spell.depth()){s=nullptr;const bool b=ext->peekDepthStencilSurface(s);if(!b&&FAILED(ext->GetDepthStencilSurface(&s)))s=nullptr;hit=s==spell.depth();if(!b)drop(s);}
        if(spellTraceFrame)spellEvent("clear",unsigned(flags),hit);
        if(hit)spellFlush(NorthlightSpellLayer::Reason::Clear,true,NorthlightSpellLayer::InPlaceKind::None,"clear",unsigned(flags),0);
    }
    // StretchRect / ColorFill / UpdateSurface / GetRenderTargetData with RAW surfaces. The layer's target or depth on either side: Copy; a surface of a texture a pending draw samples as the destination: TexWrite.
    void spellCopy(IDirect3DSurface9* source,IDirect3DSurface9* destination){
        if(!spellOn||!spell.pending())return;
        auto mine=[&](IDirect3DSurface9* x){return x&&(x==spell.target()||x==spell.depth());};
        if(spellTraceFrame)spellEvent("copy",mine(source),mine(destination));
        IDirect3DBaseTexture9* container=nullptr;
        if(destination&&FAILED(destination->GetContainer(__uuidof(IDirect3DBaseTexture9),reinterpret_cast<void**>(&container))))container=nullptr;
        struct Drop{IDirect3DBaseTexture9* p;~Drop(){if(p)p->Release();}} dropContainer{container};
        /* The layer's colour target copied into a texture (the game's glow copy, ffxGlow 1): allowed, the pending draws stay deferred. The copy then holds the world
           without them (they replay at B, after the glow passes), so they get no game glow; a depth source, a copy into the target or depth, or into a non-texture
           surface (GetRenderTargetData into system memory) is still the Copy hazard. */
        D3DSURFACE_DESC destDesc={};
        if(container&&source&&source==spell.target()&&!mine(destination)&&SUCCEEDED(destination->GetDesc(&destDesc))&&destDesc.Pool==D3DPOOL_DEFAULT){ /* a default-pool texture level only: not a managed or system-memory one (GetRenderTargetData) */
            ++spellCopiesAllowed;spellGlowFrame=frame+1;spellGlowTex=static_cast<void*>(container);spellGlowPassN=0;spellTextureWrite(static_cast<void*>(container));return;
        }
        if(mine(source)||mine(destination)){spellFlush(NorthlightSpellLayer::Reason::Copy,true,NorthlightSpellLayer::InPlaceKind::None,"copy",mine(source),mine(destination));return;}
        if(container)spellTextureWrite(static_cast<void*>(container));
    }
    void* spellTargetTexture(){
        IDirect3DSurface9* t=spell.target();if(!t)return nullptr;
        if(t==spellTexFor)return spellTexKey;
        spellTexFor=t;spellTexKey=nullptr;IDirect3DBaseTexture9* container=nullptr;
        if(SUCCEEDED(t->GetContainer(__uuidof(IDirect3DBaseTexture9),reinterpret_cast<void**>(&container)))&&container){spellTexKey=static_cast<void*>(container);container->Release();}
        return spellTexKey;
    }
    // Does a pixel sampler hold the texture `key`? Exact: the state cache's known stages are compared in place (owner-thread read, like spellRenderState; no guard, no call per
    // stage), and a stage it does not know is asked of the device as before.
    bool spellSamples(void* key){
        unsigned unknown=0xFFFFu;
        if(mirrorState.active()){
            unknown=0;
            for(unsigned stage=0;stage<DeviceMirror::Textures&&stage<16;++stage){
                if(mirrorState.textureKnown[stage]){if(static_cast<void*>(mirrorState.textures[stage])==key)return true;}
                else unknown|=1u<<stage;
            }
        }
        for(DWORD stage=0;unknown>>stage;++stage){
            if(!(unknown>>stage&1))continue;
            IDirect3DBaseTexture9* t=nullptr;
            if(ext->peekTexture(stage,t)){if(static_cast<void*>(t)==key)return true;continue;}
            if(SUCCEEDED(ext->GetTexture(stage,&t))&&t){const bool hit=static_cast<void*>(t)==key;t->Release();if(hit)return true;}
        }
        return false;
    }
    DWORD spellRenderState(D3DRENDERSTATETYPE type){ /* the mirror's answer without the guard and the call (the same owner-thread read drawHook makes of texture 0); unknown: the Get that learns it */
        if(mirrorState.active()&&mirrorState.renderStateKnown[type])return mirrorState.renderStates[type];
        DWORD v=0;ext->GetRenderState(type,&v);return v;
    }
    // The draw's facts for NorthlightSpellLayer::classifyWith: the cheap ones are filled in by the caller, the rest is read on first use, in the order the decision asks.
    // complete() reads everything left (before a log line or a trace prints the facts).
    struct SpellFacts{
        Device& d;NorthlightSpellLayer::DrawFacts f;void* texKey;DWORD cw=15;bool cwKnown=false;
        bool haveSamples=false,haveBlend=false,haveBlendRest=false,haveRt=false,haveDs=false,haveStencil=false;
        IDirect3DSurface9 *rt=nullptr,*ds=nullptr;bool rtB=true,dsB=true;
        SpellFacts(Device& dev,void* key):d(dev),texKey(key){}
        SpellFacts(const SpellFacts&)=delete;SpellFacts& operator=(const SpellFacts&)=delete;
        ~SpellFacts(){if(haveRt&&!rtB)drop(rt);if(haveDs&&!dsB)drop(ds);}
        bool samples(){if(!haveSamples){haveSamples=true;f.samplesRenderTargetTexture=f.pending&&texKey&&d.spellSamples(texKey);}return f.samplesRenderTargetTexture;}
        bool claimed()const{return f.claimed;}bool worldDomain()const{return f.worldDomain;}bool zWrite()const{return f.zWrite;}bool water()const{return f.water;}bool rain()const{return f.rain;}
        bool uiTag()const{return f.uiTag;}bool fadeRun()const{return f.fadeRun;}bool skinned()const{return f.skinned;}NorthlightSpellLayer::Family family()const{return f.family;}
        bool pending()const{return f.pending;}bool fixedFunction()const{return f.fixedFunction;}bool zEnable()const{return f.zEnable;}bool mrtBound()const{return f.mrtBound;}
        bool instanced()const{return f.instanced;}bool occlusionActive()const{return f.occlusionActive;}
        unsigned colorWrite(){if(!cwKnown){cw=d.spellRenderState(D3DRS_COLORWRITEENABLE);cwKnown=true;}f.colorWrite=cw;return cw;}
        bool blend(){if(!haveBlend){haveBlend=true;f.blend=d.spellRenderState(D3DRS_ALPHABLENDENABLE)!=0;}return f.blend;}
        void blendRest(){if(haveBlendRest)return;haveBlendRest=true;if(blend()){f.srcBlend=d.spellRenderState(D3DRS_SRCBLEND);f.destBlend=d.spellRenderState(D3DRS_DESTBLEND);f.blendOp=d.spellRenderState(D3DRS_BLENDOP);f.separateAlpha=d.spellRenderState(D3DRS_SEPARATEALPHABLENDENABLE)!=0;}}
        bool opaqueBlend(){if(!blend())return true;blendRest();return NorthlightSpellLayer::opaqueBlend(f);}
        unsigned subClass(){blendRest();return NorthlightSpellLayer::subClass(f);}
        bool stencil(){if(!haveStencil){haveStencil=true;f.stencil=d.spellRenderState(D3DRS_STENCILENABLE)!=0;}return f.stencil;}
        void loadRt(){
            if(haveRt)return;haveRt=true;
            rtB=d.ext->peekRenderTarget(0,rt);
            if(!rtB&&FAILED(d.ext->GetRenderTarget(0,&rt)))rt=nullptr;
            if(IDirect3DSurface9* target=d.spell.target())f.rt0IsTarget=rt==target;
            else if(rt){D3DSURFACE_DESC desc;f.rt0IsTarget=d.fullViewport(desc,nullptr,true);} /* would become the target: the full-viewport back-buffer-sized world target */
        }
        void loadDs(){
            if(haveDs)return;haveDs=true;
            dsB=d.ext->peekDepthStencilSurface(ds);
            if(!dsB&&FAILED(d.ext->GetDepthStencilSurface(&ds)))ds=nullptr;
            f.dsIsWorldDepth=ds&&ds==d.worldDepth;
        }
        bool rt0IsTarget(){loadRt();return f.rt0IsTarget;}
        bool dsIsWorldDepth(){loadDs();return f.dsIsWorldDepth;}
        void complete(){samples();loadRt();loadDs();colorWrite();blendRest();stencil();}
    };
    // The per-draw step, after prepareDraw/claims and before the real draw. true: the draw was deferred (the caller skips the real call and returns D3D_OK).
    // Cheap by construction: nothing pending needs only ZWRITEENABLE (a Z-writing draw ends there) and ALPHABLENDENABLE; a pending frame classifies every draw
    // (a hazard, the fade reorder or a Z-writing draw that may sample the target). SpellTrace frames classify and log every world draw.
    bool sampledOnce(){if(spellSampleFrame!=frame){spellSampleFrame=frame;spellSampleOn=sampled();}return spellSampleOn;} /* sampled() once per frame, not per world draw */
    bool spellDrawImpl(bool claimed,bool rain){
        using namespace NorthlightSpellLayer;
        const bool timed=sampledOnce();
        if(timed)++spellGateDraws;
        struct GateTimer{ /* sampled frames, every 8th call: the call's time minus the record time inside it (the scope's own cost is taken off at the log line) */
            Device& d;bool on;LARGE_INTEGER t0;LONGLONG rec0,flush0;
            GateTimer(Device& dev,bool o):d(dev),on(o),rec0(dev.spellRecordTicks),flush0(dev.spellFlushTicks){t0.QuadPart=0;if(on){QueryPerformanceCounter(&t0);++d.spellGateTimed;}}
            ~GateTimer(){if(on){LARGE_INTEGER t1;QueryPerformanceCounter(&t1);d.spellGateTicks+=(t1.QuadPart-t0.QuadPart)-(d.spellRecordTicks-rec0)-(d.spellFlushTicks-flush0);}}
        } gateTimer{*this,timed&&((spellGateSeq++&7)==0)};
        if(spellTraceWindow&&!spellTraceFrame){spellTraceFrame=true;spellTraceSeen=false;spellDrawIndex=0;logf("SPELLTRACE begin frame=%u left=%u mode=%s",frame,spellTraceLeft,modeName(spell.mode()));}
        const bool trace=spellTraceFrame;
        IDirect3DVertexShader9* const vs=spellVs;
        const VsClass* vc=vs?&classifyVs(vs):nullptr;
        const bool skinned=vc&&vc->skinned,water=vs&&drawWaterVS,uiTag=vc&&(vc->entry&kTagMask)==2;
        const Family family=vc?Family(vc->family):Family::None;
        auto rd=[&](D3DRENDERSTATETYPE type){return spellRenderState(type);};
        const bool zWrite=rd(D3DRS_ZWRITEENABLE)!=0;
        DWORD cw=15;bool cwKnown=false;
        auto colorWrite=[&]{if(!cwKnown){cw=rd(D3DRS_COLORWRITEENABLE);cwKnown=true;}return cw;};
        const bool fade=spellFade.update(skinned,zWrite,skinned&&zWrite?colorWrite():15u);
        if(!trace&&!spell.armed()){ /* a Capacity fallback: the frame is drawn the old way, but the deferrable draws are counted (the pre-filter of the early outs below; the real demand of the frame) */
            if(!zWrite&&spellDomain&&!claimed&&!water&&!rain&&!uiTag&&!(fade&&skinned)&&((family!=Family::None&&family!=Family::Ripples)||!vs)&&rd(D3DRS_ALPHABLENDENABLE))spell.noteCandidate();
            return false;
        }
        const bool pend=spell.pending();
        void* texKey=pend?spellTargetTexture():nullptr;
        const bool glowActive=pend&&spellGlowFrame==frame+1&&spellGlowTex; /* an allowed glow copy happened this frame while draws were pending */
        if(!trace){
            if(!pend&&(zWrite||!spellDomain))return false;
            if(!pend&&((family==Family::None&&vs)||family==Family::Ripples||claimed||water||rain||uiTag||(fade&&skinned)||!rd(D3DRS_ALPHABLENDENABLE)))return false; /* no vertex shader: fixed function, a candidate */
            if(pend&&zWrite&&spellDomain&&!texKey&&!glowActive)return false; /* Opaque / TranslucentZ: in place, nothing to do */
        }
        SpellFacts facts(*this,texKey);DrawFacts& f=facts.f;
        f.zEnable=spellZEnable;f.zWrite=zWrite;f.worldDomain=spellDomain;f.family=family;f.skinned=skinned;f.water=water;f.uiTag=uiTag;f.rain=rain;f.claimed=claimed;f.fixedFunction=!vs;f.fadeRun=fade;f.pending=pend;
        f.mrtBound=rainMrtBound;f.occlusionActive=spellQueryN!=0||spellQueryUnknown;
        facts.cw=cw;facts.cwKnown=cwKnown;
        const Result r=classifyWith(facts,spellClasses); /* the facts are read in the order the decision asks, the rest never */
        /* The glow exemption (glowVerdict in spell_layer.h): a screen-space pass that samples the glow copy is let through; a world draw that samples it is a Sample hazard. */
        NorthlightSpellLayer::GlowVerdict glow=NorthlightSpellLayer::GlowVerdict::None;
        const bool orderHazard=r.kind==Kind::Hazard&&r.reason==Reason::Order;
        if(glowActive&&(facts.worldDomain()||orderHazard)&&spellSamples(spellGlowTex)){
            D3DSURFACE_DESC glowDesc;
            glow=NorthlightSpellLayer::glowVerdict(true,orderHazard,facts.worldDomain(),facts.zWrite(),true,!facts.worldDomain()&&fullViewport(glowDesc,nullptr,true),spellGlowPassN);
        }
        const bool logFacts=trace||(diagnostics()&&spellHazardLogs<kSpellHazardLogs);
        const bool flushing=glow==NorthlightSpellLayer::GlowVerdict::SampleHazard||(glow==NorthlightSpellLayer::GlowVerdict::None&&r.kind==Kind::Hazard);
        if(logFacts&&(r.kind==Kind::Defer||flushing))facts.complete(); /* before a flush or a refused defer drains the layer and clears its target: the facts the lines print are the draw's */
        bool deferred=false;
        if(glow==NorthlightSpellLayer::GlowVerdict::Pass){
            ++spellGlowPasses;++spellGlowPassN;
            if(trace)logf("SPELLTRACE frame=%u event=glowpass draw=%u pass=%u glowTex=%p",frame,spellDrawIndex+1,spellGlowPassN,spellGlowTex);
            if(diagnostics()&&spellGlowLogs<8){
                ++spellGlowLogs;facts.complete();D3DVIEWPORT9 gvp={};ext->GetViewport(&gvp);
                logf("SPELLGLOW frame=%u draw=%u fixed=%d blend=%d src=%u dst=%u cw=%u vp=%lux%lu glowTex=%p",frame,drawCalls,int(f.fixedFunction),int(f.blend),f.srcBlend,f.destBlend,f.colorWrite,(unsigned long)gvp.Width,(unsigned long)gvp.Height,spellGlowTex);
            }
        }
        else if(glow==NorthlightSpellLayer::GlowVerdict::SampleHazard){CpuScope flush(timed?&spellFlushTicks:nullptr); /* sampled frames only */if(timed)++spellFlushes;spellInDraw=true;spellFlush(Reason::Sample,true,InPlaceKind::None);spellInDraw=false;}
        else switch(r.kind){
        case Kind::Defer:{
            facts.loadRt();facts.loadDs();IDirect3DSurface9* const rt=facts.rt;IDirect3DSurface9* const ds=facts.ds;
            if(rt){spellArgs.psMajor=0;spellArgs.psReads=nullptr;spellArgs.psFor=nullptr;spellArgs.psFogReg=-1;spellPixelShader(&spellArgs.psMajor,nullptr,&spellArgs.psReads,&spellArgs.psFor,&spellArgs.psFogReg);spellArgs.vsReads=vs?spellReads(static_cast<void*>(vs)):nullptr;spellArgs.vsFor=static_cast<const void*>(vs);spellArgs.vsFog=0;spellArgs.vsHash=0;if(vs){auto pit=vsFogProven.find(vs);if(pit!=vsFogProven.end())spellArgs.vsFog=pit->second;if(diagnostics()&&spell.fogSampleRoom()){auto hit=vsHashes.find(vs);if(hit!=vsHashes.end())spellArgs.vsHash=hit->second;}} /* the hash only for a SPELLFOG sample that will be logged */CpuScope record(timed?&spellRecordTicks:nullptr); /* sampled frames only */if(timed)++spellRecords;deferred=spell.defer(spellArgs,rt,ds,r.sub);}
            break;}
        case Kind::Hazard:{CpuScope flush(timed?&spellFlushTicks:nullptr); /* sampled frames only */if(timed)++spellFlushes;spellInDraw=true;spellFlush(r.reason,true,r.inPlace);spellInDraw=false;break;} /* r.inPlace: water, rain or a name drawn after a deferred draw */
        case Kind::InPlace:if(pend)spell.noteInPlace(r.inPlace);break;
        default:break;
        }
        spellSync();
        if(logFacts)facts.complete(); /* InPlace and Ignore draws print their facts too (no drain in between) */
        spellDrawEventDone(f); /* a refused defer or a flushing hazard of this draw: SPELLHAZARD with the draw's facts */
        if(trace)spellTraceDraw(f,r,deferred,pend,facts.rt,facts.ds);
        return deferred;
    }
    // One SPELLTRACE line for a world draw: from the first colour-writing draw without a depth write of the frame, and every draw while draws are pending.
    void spellTraceDraw(const NorthlightSpellLayer::DrawFacts& f,const NorthlightSpellLayer::Result& r,bool deferred,bool pendedBefore,IDirect3DSurface9* rt,IDirect3DSurface9* ds){
        ++spellDrawIndex;
        if(f.worldDomain&&!f.zWrite&&(f.colorWrite&7))spellTraceSeen=true;
        if(!((f.worldDomain&&spellTraceSeen)||pendedBefore))return;
        auto rd=[&](D3DRENDERSTATETYPE type){DWORD v=0;ext->GetRenderState(type,&v);return (unsigned long)v;};
        unsigned long long vsHash=0,psHash=0;
        {auto it=vsHashes.find(spellVs);if(spellVs&&it!=vsHashes.end())vsHash=it->second;}
        {IDirect3DPixelShader9* ps=nullptr;const bool b=ext->peekPixelShader(ps);if(!b&&FAILED(ext->GetPixelShader(&ps)))ps=nullptr;
         auto it=psHashes.find(ps);if(ps&&it!=psHashes.end())psHash=it->second;if(!b)drop(ps);}
        D3DVIEWPORT9 vp={};ext->GetViewport(&vp);
        IDirect3DVertexBuffer9* vb=nullptr;UINT voff=0,vstride=0;int dynamic=-1;
        if(spellArgs.type==NorthlightSpellLayer::DrawType::Primitive||spellArgs.type==NorthlightSpellLayer::DrawType::Indexed){
            if(SUCCEEDED(ext->GetStreamSource(0,&vb,&voff,&vstride))&&vb){D3DVERTEXBUFFER_DESC vd={};if(SUCCEEDED(vb->GetDesc(&vd)))dynamic=(vd.Usage&D3DUSAGE_DYNAMIC)!=0;}
        }
        IDirect3DIndexBuffer9* ib=nullptr;if(spellArgs.type==NorthlightSpellLayer::DrawType::Indexed)ext->GetIndices(&ib);
        logf("SPELLTRACE frame=%u draw=%u kind=%s sub=%u inplace=%s reason=%s deferred=%d pending=%u family=%u skinned=%d fixed=%d water=%d ui=%d rain=%d claimed=%d fade=%d vs=%016llx ps=%016llx "
             "zen=%d zw=%d zfunc=%lu atest=%lu aref=%lu blend=%d src=%u dst=%u op=%u sep=%d cw=%u stencil=%d domain=%d rt0=%p ds=%p isTarget=%d isDepth=%d vp=%lux%lu type=%d prim=%u count=%u base=%d min=%u num=%u first=%u vb0=%p off=%u stride=%u dyn=%d ib=%p",
             frame,spellDrawIndex,spellKindName(r.kind),r.sub,spellInPlaceName(r.inPlace),NorthlightSpellLayer::reasonName(r.reason),int(deferred),spell.pending(),unsigned(f.family),int(f.skinned),int(f.fixedFunction),int(f.water),int(f.uiTag),int(f.rain),int(f.claimed),int(f.fadeRun),
             vsHash,psHash,int(f.zEnable),int(f.zWrite),rd(D3DRS_ZFUNC),rd(D3DRS_ALPHATESTENABLE),rd(D3DRS_ALPHAREF),int(f.blend),f.srcBlend,f.destBlend,f.blendOp,int(f.separateAlpha),f.colorWrite,int(f.stencil),int(f.worldDomain),
             static_cast<void*>(rt),static_cast<void*>(ds),int(f.rt0IsTarget),int(f.dsIsWorldDepth),(unsigned long)vp.Width,(unsigned long)vp.Height,int(spellArgs.type),unsigned(spellArgs.prim),spellArgs.primCount,spellArgs.baseVertex,spellArgs.minIndex,spellArgs.numVertices,spellArgs.first,
             static_cast<void*>(vb),voff,vstride,dynamic,static_cast<void*>(ib));
        drop(vb);drop(ib);
    }
    bool spellGate()const{return terrain&&projectionValid&&!applied&&enabled&&!failed&&world&&debugMode==0&&worldDebug==0&&(spell.armed()||spell.wantsCandidates()||spellTraceWindow||spellTraceFrame);}
    bool spellDraw(bool claimed,bool rain){
        bool deferred=false;
        extensionWork("spell layer",[&]{deferred=spellDrawImpl(claimed,rain);});
        return deferred;
    }
    // At the effects boundary, right after renderEffects() returned (also when it bailed out): the deferred draws, after the composite and the veil
    // passes, before the UI draw. Closes the frame for the layer.
    void spellReplay(){
        if(!spellOn)return;
        unsigned replayed=0;
        extensionWork("spell replay",[&]{
            CpuScope timer(sampledOnce()?&spellReplayTicks:nullptr);
            const unsigned n=spell.pending();replayed=n;
            if(spellTraceFrame)logf("SPELLTRACE frame=%u event=boundary draw=%u pending=%u",frame,drawCalls,n);
            if(n){ExtensionDevice::RawScope raw(*ext);spell.replay();}else spell.replay();
            if(spellTraceFrame&&n)logf("SPELLTRACE frame=%u event=replay count=%u replayed=%u skipped=%u",frame,n,spell.frameCounters().replayedDraws,spell.frameCounters().replaySkipped);
        });
        spellSync();
        if(diagnostics()){if(replayed)gpuProfile->mark("Spells");gpuProfile->endFrame();gpuEffectsEnd();} /* GPU time of the replay (sampled frames only; renderEffects left the profile frame open for it) */
    }
    // Frame end (finishFrameImpl, after the frame's diagnostics lines): the layer's frame boundary, the SPELLLAYER line, the idle trim, the trace window.
    void spellFrameEnd(){
        if(!spellOn)return;
        extensionWork("spell frame end",[&]{
            spellFlush(NorthlightSpellLayer::Reason::Frame,false,NorthlightSpellLayer::InPlaceKind::None,"frameend"); /* Present before the boundary: draw them now, no cooldown */
            const bool wasSampled=sampled();const NorthlightSpellLayer::Mode mode=spellFrameMode;
            const NorthlightSpellLayer::Layer::Memory spellMemAtFrame=wasSampled&&diagnostics()?spell.memory():NorthlightSpellLayer::Layer::Memory{};
            if(spell.frameCounters().totalDeferred())spellIdle=0;else if(++spellIdle==600)spell.trim(0);
            spell.fogFresh(world&&spellFogFreshness.next(world->spellFogFrame().stamp)); /* the next frame's records fold the estimate only if this frame's world render published it */
            if(world){const float (*ramps)[4]=nullptr;const unsigned n=spell.fogRampsSeen(ramps);for(unsigned i=0;i<n;++i)world->spellFogHint(ramps[i],ramps[i][3]!=0.f);} /* SpellFog: falling-ramp constants for the world render to fit ahead of the records */
            spell.beginFrame(NorthlightStream::memoryPressure.load(std::memory_order_relaxed));spellFrameMode=spell.mode();
            spellSync();spellTexFor=nullptr;spellTexKey=nullptr;
            for(unsigned i=0;i<spellQueryN;){if(frame-spellQueryFrame[i]>2){for(unsigned k=i+1;k<spellQueryN;++k){spellQuery[k-1]=spellQuery[k];spellQueryFrame[k-1]=spellQueryFrame[k];}--spellQueryN;}else ++i;}
            if(spellQueryUnknown&&frame-spellQueryUnknownFrame>2)spellQueryUnknown=false; /* same 2-frame sweep as the table: a lost bracket cannot hold the layer off forever */
            spellFade.reset();
            const NorthlightSpellLayer::Counters& c=spell.lastFrameCounters();
            if(diagnostics()){ /* window totals: every frame, not only the sampled one */
                ++spellWinFrames;if(c.totalDeferred())++spellWinDeferring;if(mode==NorthlightSpellLayer::Mode::Cooldown)++spellWinCooldown;spellWin.add(c);
            }
            NorthlightSpellLayer::Reason first;
            if(spell.takeFirstHazard(first))logf("SPELLLAYER first hazard reason=%s frame=%u",NorthlightSpellLayer::reasonName(first),frame);
            if(wasSampled&&diagnostics()){
                auto reasons=[](const NorthlightSpellLayer::Counters& k,char* out,size_t size){
                    size_t at=0;out[0]=0;
                    for(unsigned i=0;i<NorthlightSpellLayer::kReasons;++i)if(k.hazards[i]){const int w=std::snprintf(out+at,size-at,"%s%s:%u",at?",":"",NorthlightSpellLayer::reasonName(NorthlightSpellLayer::Reason(i)),k.hazards[i]);if(w>0)at=std::min(size-1,at+size_t(w));}
                    if(!at)std::snprintf(out,size,"none");
                };
                char hazards[200],winHazards[200];reasons(c,hazards,sizeof hazards);reasons(spellWin,winHazards,sizeof winHazards);
                const double us=cpuFrequency.QuadPart>0?1e6/double(cpuFrequency.QuadPart):0.0;
                const NorthlightSpellLayer::Layer::PhaseTicks phase=spell.phaseTicks(); /* the drains' (B, flushes, splits) state save, upload and restore of this sampled frame */
                double timerUs=1e9; /* what one timing scope costs here (two clock reads): the least of a few probes, taken off the gate and record sums once per scope */
                for(int k=0;k<5;++k){LONGLONG sink=0;LARGE_INTEGER a,b;QueryPerformanceCounter(&a);for(int i=0;i<64;++i){CpuScope probe(&sink); /* sampled frame: calibration */}QueryPerformanceCounter(&b);timerUs=std::min(timerUs,double(b.QuadPart-a.QuadPart)*us/64.0);}
                /* net sums: the gate was timed on every 8th call (scaled to all calls), both minus the scopes' own cost, never below 0; cpuDeferUs is their sum */
                const double gateUs=spellGateTimed?std::max(0.0,double(spellGateTicks)*us-double(spellGateTimed)*timerUs)*double(spellGateDraws)/double(spellGateTimed):0.0;
                const double flushUs=std::max(0.0,double(spellFlushTicks)*us-double(spellFlushes)*timerUs); /* hazard flushes of gate draws, all timed */
                const double recordUs=std::max(0.0,double(spellRecordTicks>=LONGLONG(phase.check)?spellRecordTicks-LONGLONG(phase.check):0)*us-double(spellRecords)*timerUs); /* the dirty cross-check's full build (sampled frames) is not part of the record time */
                const NorthlightSpellLayer::Layer::Memory spellMem=spellMemAtFrame; /* taken before the frame's trim/beginFrame released the chunks */
                /* cpuGateUs and cpuDeferUs (= gate + record) are ESTIMATES: the gate was timed on every 8th call (gateTimed of gateDraws) and scaled up, with the calibrated scope cost (timerScopeUs, the least of 5 probes) taken
                   off per scope and the record and flush time inside the timed calls taken out; cpuRecordUs is every record. cpuFlushUs is the (unscaled) time of the hazard flushes of gate draws; their save, upload and
                   restore are also in cpuSaveUs/cpuUploadUs/cpuRestoreUs and are not in cpuGateUs. */
                logf("SPELLLAYER frame=%u mode=%s deferred=%u/%u/%u/%u fixed=%u replayed=%u fadeInPlace=%u orderBy=%u/%u/%u hazards=%s flushes=%u conflicts=%u conflictBytes=%llu materialized=%u records=%u recordHigh=%u replaySets=%u arenaHigh=%u replaySkipped=%u discarded=%u cooldown=%u cpuDeferUs=%.1f cpuReplayUs=%.1f capBy=%u/%u/%u/%u overflowAt=%u overflowCandidates=%u memKiB=%u uploadKiB=%u deltaWords=%llu cpuRecordUs=%.1f cpuGateUs=%.1f gateDraws=%u gateTimed=%u recordCalls=%u cpuFlushUs=%.1f timerScopeUs=%.3f drains=%u cpuSaveUs=%.1f cpuUploadUs=%.1f cpuRestoreUs=%.1f restoreSets=%u restoreFallbacks=%u texSplits=%u copiesAllowed=%u glowPasses=%u recordSlots=%llu dirtyMismatches=%u cpuCheckUs=%.1f stride0Refused=%u checkAllocFails=%u fogCapped=%u fogEntries=%u fogFlat=%u fogRefused=%u fogUnproven=%u fogNoRead=%u fogNoPsReg=%u fogOtherBlend=%u fogBareSkip=%u fogFit=%.5f,%.4f,%.3f fogT=%.3f "
                     "window frames=%u deferring=%u cooldown=%u deferred=%u hazards=%s flushes=%u conflicts=%u overflowFrames=%u overflowCandidates=%u texSplits=%u drains=%u dirtyMismatches=%u",
                    frame,NorthlightSpellLayer::modeName(mode),c.deferred[0],c.deferred[1],c.deferred[2],c.deferred[3],c.fixed,c.replayedDraws,c.inPlace[3],c.orderBy[0],c.orderBy[1],c.orderBy[2],hazards,c.flushes,c.conflicts,(unsigned long long)c.conflictBytes,c.materialized,c.recorded,c.recordHigh,c.replaySets,c.arenaHigh,c.replaySkipped,c.discarded,spell.cooldownLeft(),
                    gateUs+recordUs,double(spellReplayTicks)*us,c.capBy[0],c.capBy[1],c.capBy[2],c.capBy[3],c.overflowAt,c.overflowCandidates,unsigned(spellMem.total()>>10),unsigned(spellMem.uploads>>10),(unsigned long long)c.deltaWords,recordUs,gateUs,spellGateDraws,spellGateTimed,spellRecords,flushUs,timerUs,
                    c.drains,double(phase.save)*us,double(phase.upload)*us,double(phase.restore)*us,c.restoreSets,c.restoreFallbacks,c.texSplits,spellCopiesAllowed,spellGlowPasses,(unsigned long long)c.recordSlots,c.dirtyMismatches,double(phase.check)*us,c.stride0Refused,c.checkAllocFails,c.fogCapped,c.fogEntries,c.fogFlat,c.fogRefused,c.fogUnproven,c.fogNoRead,c.fogNoPsReg,c.fogOtherBlend,c.fogBareSkip,spell.fogLastFit()[0],spell.fogLastFit()[1],spell.fogLastFit()[2],world?world->spellFogFrame().transmittance(NorthlightSpellFog::kRange):1.f,
                    spellWinFrames,spellWinDeferring,spellWinCooldown,spellWin.totalDeferred(),winHazards,spellWin.flushes,spellWin.conflicts,spellWin.overflowFrames,spellWin.overflowCandidates,spellWin.texSplits,spellWin.drains,spellWin.dirtyMismatches);
                spellWin=NorthlightSpellLayer::Counters{};spellWinFrames=spellWinDeferring=spellWinCooldown=0;
            }
            {const NorthlightSpellLayer::Layer::FogSample* fs=nullptr;const unsigned fn=spell.takeFogSamples(fs); /* SPELLFOG: the first 16 entries per sub-class and kind (ramp, flat, refused) and session; the game's fog constant and colour, what the record carries instead */
             if(diagnostics())for(unsigned i=0;i<fn;++i){const auto& x=fs[i];
                 logf("SPELLFOG frame=%u sub=%u kind=%s vs=%016llx game=%.5f,%.4f,%.3f gameColor=%.3f,%.3f,%.3f emit=%.5f,%.4f,%.3f emitColor=%.3f,%.3f,%.3f",frame,x.sub,x.kind==1?"flat":x.kind==2?"refused":"ramp",(unsigned long long)x.vsHash,x.game[0],x.game[1],x.game[2],x.hasColor?x.gameColor[0]:0.f,x.hasColor?x.gameColor[1]:0.f,x.hasColor?x.gameColor[2]:0.f,x.emit[0],x.emit[1],x.emit[2],x.hasColor?x.emitColor[0]:0.f,x.hasColor?x.emitColor[1]:0.f,x.hasColor?x.emitColor[2]:0.f);}}
            {NorthlightSpellLayer::DeltaDiff dd;if(spell.takeDirtyMismatch(dd)&&diagnostics()) /* once per session: the synced-bit record disagreed with the full compare (a setter that does not clear its bit?) */
                logf("SPELLDIRTY frame=%u kind=%s index=%u dirty=0x%llx full=0x%llx dirtyRecord=off",frame,dd.kind,dd.index,(unsigned long long)dd.dirty,(unsigned long long)dd.full);}
            {const bool next=diagnostics()&&NorthlightRenderThreadProbe::sampleFrame(frame+1);spell.dirtyCheck(next);} /* the next frame is sampled: cross-check the dirty record on it */
            spellReplayTicks=spellRecordTicks=spellGateTicks=spellFlushTicks=0;spellGateDraws=spellGateTimed=spellRecords=spellFlushes=spellCopiesAllowed=spellGlowPasses=0;spell.clearPhaseTicks();
            if(spellTraceFrame){logf("SPELLTRACE end frame=%u left=%u",frame,spellTraceLeft-1);spellTraceFrame=false;if(spellTraceLeft)--spellTraceLeft;}
            spellTraceWindow=spellTraceLeft&&diagnostics()&&frame+1>=300;
        });
    }
    // 0.3.196 (task 12): beforeDraw's world depth step (extracted unchanged for testing). False: the draw is rejected (beforeDraw returns).
    bool bindWorldDepth(const D3DSURFACE_DESC& desc){
        IDirect3DSurface9* ds=nullptr;D3DSURFACE_DESC dd;
        // 0.3.196 (task 12): the bound depth is the one worldDepth already holds (a reference, so same object and desc): reuse its cached desc, keep the reference.
        const bool sameDepth=worldDepth&&worldDepthDescKnown&&ext->peekDepthStencilSurface(ds)&&ds==worldDepth;
        if(sameDepth){ds=nullptr;dd=worldDepthDesc;}
        else{
            ds=nullptr;
            if (FAILED(ext->GetDepthStencilSurface(&ds)) || !ds) {++depthRejects;return false;}
            ds->GetDesc(&dd);
        }
        if (dd.Width!=desc.Width || dd.Height!=desc.Height) {++depthRejects;drop(ds);return false;}
        if (dd.Format!=D3DFMT_D24S8 && dd.Format!=D3DFMT_D24X8 && dd.Format!=(D3DFORMAT)MAKEFOURCC('I','N','T','Z')) {
            logf("DISABLED: unsupported world depth format %u",unsigned(dd.Format));failed=true;drop(ds);return false;
        }
        if(!sameDepth){drop(worldDepth);worldDepthDescKnown=false;worldDepth=ds;worldDepthDesc=dd;worldDepthDescKnown=true;}
        return true;
    }
    // 0.3.212 (task 32): a vertex shader that can draw world text (a name): the stock UI hash (tag 2), or, while the UI boundary fallback is armed, the VS it learned. Font mods that replace the UI shaders
    // (Lexara, TweakWoW2 with HD Font on) swap the one UI VS the game draws its screen UI and its 3D text with, so the learned VS is the one their names are drawn with; their VS still takes the game's
    // projection in c0..c3, so the c3 test of nameMaskDecide holds as with the stock VS. Before the fallback arms (about 2 s of world frames) their names are not marked, as their effects have not started either.
    bool uiTextVs(IDirect3DVertexShader9* vs,int tag)const{return tag==2||(vs&&uiFallback.armed()&&reinterpret_cast<std::uintptr_t>(vs)==uiFallback.learnedVs());}
    void beforeDraw(IDirect3DVertexShader9* vs) {
        const VsClass& vc=classifyVs(vs); const int entry=vc.entry; int tag=entry&kTagMask;
        drawWaterVS=(entry&kWaterTag)!=0;drawUiHashVS=uiTextVs(vs,tag);
        bool wmo=tag!=1&&vc.wmo;if(gateFrame&&wmo)++gateCounts.wmo;
        if(wmo&&projectionValid&&world->hasContext()&&world->hasOriginalFog())return;
        const bool model=tag!=1&&!wmo&&vc.skinned&&(!projectionValid||!world->hasContext());
        if (tag==1||wmo||model) {
            if(tag==1)++terrainDraws;
            D3DSURFACE_DESC desc={};D3DVIEWPORT9 viewport={};
            if (!fullViewport(desc,&viewport)) return;
            if(model){
                DWORD z=FALSE,zf=D3DCMP_ALWAYS;
                if(FAILED(ext->GetRenderState(D3DRS_ZENABLE,&z))||FAILED(ext->GetRenderState(D3DRS_ZFUNC,&zf))||
                   !NorthlightWorldDrawDomain::depthTested(z,zf)||
                   NorthlightWorldDrawDomain::classify(0.f,NorthlightWorldDrawDomain::kWorldMaxZ,viewport.MinZ,viewport.MaxZ)!=NorthlightWorldDrawDomain::Band::World)return;
            }
            if (!readProjection(wmo||model)) {
                if (++projectionRejects==1) {
                    float p[16]={};ext->GetVertexShaderConstantF(4,p,4);
                    logf("Projection rejected: [%g %g %g %g] [%g %g %g %g] [%g %g %g %g] [%g %g %g %g]",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
                }
                return;
            }
            if(wmo&&!world->wmoContext(vs))return;
            if(model&&!world->modelContext(vs))return;
            if(tag==1&&world)world->terrainContext();
            projectionValid=true;
            if(!bindWorldDepth(desc))return;
            if(captured&&earlyDepth.earlyCaptured){earlyDepth.undo();++earlyResolveUndone;++earlyResolveUndoneTotal;} /* 0.3.188 (2A): a terrain draw after the early resolve: the UI-time resolve redoes it */
            if(captured&&aoActorOn)aoActorMask->resume();
            terrain=true; captured=false;
            resources(desc.Width,desc.Height,desc.Format);
            if(gateFrame)++gateCounts.fullPasses;
        } else if (tag==2) {
            ++uiDraws;
            if(!terrain)return; // Login/loading UI can use stock shaders even when the in-world UI is replaced by a font mod.
            ++uiAfterTerrain;
            // Fonts can use a rare PS on the stock UI VS. Confirm screen-space
            // state and the main target instead of filtering the text shader.
            DWORD zw=1,zf=D3DCMP_ALWAYS,z=1;float rows[16];D3DSURFACE_DESC desc{};
            if (SUCCEEDED(ext->GetRenderState(D3DRS_ZWRITEENABLE,&zw)) &&
                (!zFuncAware||(SUCCEEDED(ext->GetRenderState(D3DRS_ZFUNC,&zf))&&SUCCEEDED(ext->GetRenderState(D3DRS_ZENABLE,&z)))) &&
                SUCCEEDED(ext->GetVertexShaderConstantF(0,rows,4)) &&
                NorthlightUiBoundary::accepts(zw!=0,rows,zf==D3DCMP_ALWAYS,z!=0) &&
                fullViewport(desc,nullptr,true)){
                hashBoundaryThisFrame=true;
                if(sampled()){DWORD z=0,zw=0,blend=0;ext->GetRenderState(D3DRS_ZENABLE,&z);ext->GetRenderState(D3DRS_ZWRITEENABLE,&zw);ext->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);
                    logf("EFFECT boundary frame=%u draw=%u kind=ui z=%lu zwrite=%lu blend=%lu",frame,drawCalls,(unsigned long)z,(unsigned long)zw,(unsigned long)blend);}
                renderEffects();spellReplay();
            }
        }
        uiFallbackDraw(vc,vs);
    }
    // 0.3.203 (task 17): fallback boundary for font mods that replace the UI VS and PS (or only the PS): the first screen-space draw after the world with
    // ZWRITE off, full-target viewport and ortho rows, by a VS learned over 120 frames without a hash boundary. Every PS on that UI VS is eligible,
    // including infrequent FPS text. UI can disable Z or use ZFUNC ALWAYS.
    // On stock the whole step is one bool test per draw: collecting() needs two world frames in a row without the hash boundary, and the first one disarms it for good.
    void uiFallbackDraw(const VsClass& vc,IDirect3DVertexShader9* vs){
        if(!uiFallback.collecting()||!terrain||applied||hashBoundaryThisFrame||fallbackBoundaryThisFrame||vc.wmo)return;
        if(vc.world||drawWaterVS)return;
        const bool armed=uiFallback.armed();
        if(!armed&&(!enabled||failed))return; /* learning counts only frames that would draw effects (endFrame: Neutral) */
        IDirect3DPixelShader9* ps=nullptr;
        const bool borrowed=ext->peekPixelShader(ps); // identity lookup only
        if(!borrowed&&FAILED(ext->GetPixelShader(&ps)))return;
        const std::uintptr_t vsId=reinterpret_cast<std::uintptr_t>(vs),psId=reinterpret_cast<std::uintptr_t>(ps);
        if(!borrowed)drop(ps);
        if(armed&&!uiFallback.isBoundary(vsId,psId))return;
        DWORD zw=0;if(FAILED(ext->GetRenderState(D3DRS_ZWRITEENABLE,&zw)))return;
        DWORD zf=D3DCMP_ALWAYS,z=1;if(zFuncAware&&(FAILED(ext->GetRenderState(D3DRS_ZFUNC,&zf))||FAILED(ext->GetRenderState(D3DRS_ZENABLE,&z))))return;
        float c[16]={};if(FAILED(ext->GetVertexShaderConstantF(0,c,4)))return;
        if(!NorthlightUiBoundary::accepts(zw!=0,c,zf==D3DCMP_ALWAYS,z!=0)){if(!armed){triageVs=vs;triagePs=ps;triageZw=zw;for(int i=0;i<4;++i)triageC3[i]=c[12+i];++triageDraws;}return;}
        D3DSURFACE_DESC desc={};
        if(!fullViewport(desc,nullptr,true))return; // last, and quiet: minimap and portrait targets are expected here
        if(!armed){uiFallback.noteCandidate(vsId,psId);return;}
        fallbackBoundaryThisFrame=true;++uiFallbackBoundaries;
        if(sampled()){DWORD z=0,blend=0;ext->GetRenderState(D3DRS_ZENABLE,&z);ext->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);
            logf("EFFECT boundary frame=%u draw=%u kind=ui-fallback z=%lu zwrite=%lu blend=%lu",frame,drawCalls,(unsigned long)z,(unsigned long)zw,(unsigned long)blend);}
        renderEffects();spellReplay();
    }
    unsigned long long uiFallbackVsHash(std::uintptr_t id)const{auto it=vsHashes.find(reinterpret_cast<IDirect3DVertexShader9*>(id));return it==vsHashes.end()?0ull:(unsigned long long)it->second;}
    // The one writer of NorthlightStream::learnedUiVsHash (read by StreamDevice::onDraw on the game thread): the learned VS hash while armed, else 0.
    // Called after every change of uiFallback's state.
    void uiFallbackPublish(){
        const std::uint64_t want=uiFallback.armed()?uiFallbackVsHash(uiFallback.learnedVs()):0;
        if(want!=uiFallbackPublished){uiFallbackPublished=want;NorthlightStream::learnedUiVsHash.store(want,std::memory_order_relaxed);}
    }
    template<class T> void uiFallbackForget(T* obj){uiFallback.forget(reinterpret_cast<std::uintptr_t>(obj));uiFallbackPublish();}
    // 0.3.203 (task 17): per-frame fallback bookkeeping, before clearFrame() resets terrain/applied.
    void uiFallbackEndFrame(){
        using K=NorthlightUiBoundary::Arming::FrameKind;
        if(uiFallback.disarmed()){hashBoundaryThisFrame=fallbackBoundaryThisFrame=false;return;} /* stock after its first world boundary */
        const bool active=uiFallback.collecting(); /* stock never collects: its disarm below stays silent */
        const K kind=(!enabled||failed)?K::Neutral:hashBoundaryThisFrame?K::WorldHash:fallbackBoundaryThisFrame?K::WorldFallback:!terrain?K::NoWorld:applied?K::Neutral:K::WorldMissed;
        auto ph=[&](std::uintptr_t id){auto it=psHashes.find(reinterpret_cast<IDirect3DPixelShader9*>(id));return it==psHashes.end()?0ull:(unsigned long long)it->second;};
        auto result=uiFallback.endFrame(kind);
        uiFallbackPublish();
        if(uiFallback.disarmed()){
            if(active)logf("EFFECT boundary fallback disarmed: stock UI shaders seen frame=%u where=world",frame);
            hashBoundaryThisFrame=fallbackBoundaryThisFrame=false;return;
        }
        if(result==NorthlightUiBoundary::Arming::Result::Armed)
            logf("EFFECT boundary fallback active: UI shaders replaced by another module vs=%016llx ps=%016llx draws/frame=%.1f",uiFallbackVsHash(uiFallback.learnedVs()),ph(uiFallback.learnedPs(0)),double(uiFallback.drawsPerFrame()));
        else if(result==NorthlightUiBoundary::Arming::Result::Lost)logf("EFFECT boundary fallback lost; relearning");
        if(uiFallback.learning()&&uiFallback.missedWorldFrames()>=600&&!uiFallbackNoCandidateLogged){
            uiFallbackNoCandidateLogged=true;
            logf("EFFECT boundary fallback: no candidate missedWorldFrames=%u dropped=%u triageDraws=%u triage vs=%016llx ps=%016llx zwrite=%lu c3=[%g %g %g %g]",uiFallback.missedWorldFrames(),uiFallback.droppedPairs(),triageDraws,
                uiFallbackVsHash(reinterpret_cast<std::uintptr_t>(triageVs)),ph(reinterpret_cast<std::uintptr_t>(triagePs)),(unsigned long)triageZw,triageC3[0],triageC3[1],triageC3[2],triageC3[3]);
            NorthlightUiBoundary::Arming::Pair top[3];const unsigned n=uiFallback.topCandidates(top,3);
            for(unsigned i=0;i<n;++i)logf("EFFECT boundary fallback candidate vs=%016llx ps=%016llx count=%u",uiFallbackVsHash(top[i].vs),ph(top[i].ps),top[i].count);
        }
        hashBoundaryThisFrame=fallbackBoundaryThisFrame=false;
    }
public:
    // 0.3.192 (CS): the replay thread takes over as owner thread (called on it, before its first command and before CreateDevice returns).
    void adoptOwnerThread(){mirrorState.gate.ownerTid=MirrorGuard::threadId();}
    // 0.3.192 (CS): direct replay (stream mode). The extension device runs the same MirrorDevice methods on the same real device and mirror as the
    // Device's own non-overridden ones; the replay thread calls it with RAW pointers for the methods the generator's DIRECT list names.
    IDirect3DDevice9* extensionDevice(){return ext;}
    // The backend object this Device would pass on for an exposed resource (what mirrorResources.unwrap / NorthlightTrackedBuffers::resolveInput
    // return), or nullptr when that is not a pure tracked unwrap: the stream then replays that proxy's calls through the Device.
    IUnknown* rawOfExposed(IUnknown* exposed,NorthlightStream::Kind kind){
        using K=NorthlightStream::Kind;if(!exposed)return nullptr;
        bool wrapped=false;
        if(kind==K::VertexBuffer){auto* raw=NorthlightTrackedBuffers::resolveInput(static_cast<IDirect3DVertexBuffer9*>(exposed),wrapped);return wrapped?static_cast<IUnknown*>(raw):nullptr;}
        if(kind==K::IndexBuffer){auto* raw=NorthlightTrackedBuffers::resolveInput(static_cast<IDirect3DIndexBuffer9*>(exposed),wrapped);return wrapped?static_cast<IUnknown*>(raw):nullptr;}
        return reinterpret_cast<IUnknown*>(mirrorResources.rawOf(reinterpret_cast<std::uintptr_t>(exposed),false));
    }
    bool setExclusiveOwner(bool on){return mirrorState.gate.setExclusive(on);} /* 0.3.192 (CS): one thread makes every Device call (mirror_guard.h) */
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) override{Guard mirrorLock(mirrorState.gate);return ext->SetCursorProperties(XHotSpot, YHotSpot, mirrorResources.unwrap(pCursorBitmap));}
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer);if(SUCCEEDED(hr)){mirrorResources.wrap(ppBackBuffer);}return hr;}
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);if(SUCCEEDED(hr)){NorthlightReplayDrawState::noteTextureFormat(Format);weatherDetect.noteCreate(static_cast<IDirect3DBaseTexture9*>(*ppTexture),Width,Height,Levels,Format); /* 0.3.198 (rain): the raw pointer, before wrap */mirrorResources.wrap(ppTexture);}return hr;}
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle);if(SUCCEEDED(hr)){NorthlightReplayDrawState::noteTextureFormat(Format);weatherDetect.forget(static_cast<IDirect3DBaseTexture9*>(*ppVolumeTexture)); /* 0.3.198 (rain): address reuse */mirrorResources.wrap(ppVolumeTexture);}return hr;}
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);if(SUCCEEDED(hr)){NorthlightReplayDrawState::noteTextureFormat(Format);weatherDetect.forget(static_cast<IDirect3DBaseTexture9*>(*ppCubeTexture)); /* 0.3.198 (rain): address reuse */mirrorResources.wrap(ppCubeTexture);}return hr;}
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, WINBOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);if(SUCCEEDED(hr)){mirrorResources.wrap(ppSurface);}return hr;}
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, WINBOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override{Guard mirrorLock(mirrorState.gate);{const D3DFORMAT upgraded=D3DFORMAT(NorthlightStream::upgradedDepthFormat(unsigned(Format)));if(upgraded!=Format){Format=upgraded;static bool logged=false;if(!logged){logged=true;logf("STENCIL depth upgraded D24X8->D24S8 where=CreateDepthStencilSurface");}}} /* 0.3.212 (task 32): depthUpgrade serves both NameMask and actor AO, including the StreamDevice path */HRESULT hr=ext->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);if(SUCCEEDED(hr)){mirrorResources.wrap(ppSurface);}return hr;}
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9 *src_surface, const RECT *src_rect, IDirect3DSurface9 *dst_surface, const POINT *dst_point) override{Guard mirrorLock(mirrorState.gate);rainMrtUnbind(); /* 0.3.202 (rain mask MRT) */IDirect3DSurface9* rawSource=mirrorResources.unwrap(src_surface);IDirect3DSurface9* rawDestination=mirrorResources.unwrap(dst_surface);if(spellOn)spellCopy(rawSource,rawDestination);return ext->UpdateSurface(rawSource, src_rect, rawDestination, dst_point);}
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) override{Guard mirrorLock(mirrorState.gate);rainMrtUnbind(); /* 0.3.202 (rain mask MRT) */IDirect3DBaseTexture9* rawDestination=mirrorResources.unwrap(pDestinationTexture);if(spellOn)spellTextureWrite(static_cast<void*>(rawDestination));return ext->UpdateTexture(mirrorResources.unwrap(pSourceTexture), rawDestination);}
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override{Guard mirrorLock(mirrorState.gate);rainMrtUnbind(); /* 0.3.202 (rain mask MRT) */IDirect3DSurface9* rawSource=mirrorResources.unwrap(pRenderTarget);IDirect3DSurface9* rawDestination=mirrorResources.unwrap(pDestSurface);if(spellOn)spellCopy(rawSource,rawDestination);return ext->GetRenderTargetData(rawSource, rawDestination);}
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) override{Guard mirrorLock(mirrorState.gate);return ext->GetFrontBufferData(iSwapChain, mirrorResources.unwrap(pDestSurface));}
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9 *src_surface, const RECT *src_rect, IDirect3DSurface9 *dst_surface, const RECT *dst_rect, D3DTEXTUREFILTERTYPE filter) override{Guard mirrorLock(mirrorState.gate);rainMrtUnbind(); /* 0.3.202 (rain mask MRT) */IDirect3DSurface9* rawSource=mirrorResources.unwrap(src_surface);IDirect3DSurface9* rawDestination=mirrorResources.unwrap(dst_surface);if(spellOn)spellCopy(rawSource,rawDestination);return ext->StretchRect(rawSource, src_rect, rawDestination, dst_rect, filter);}
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9 *surface, const RECT *rect, D3DCOLOR color) override{Guard mirrorLock(mirrorState.gate);rainMrtUnbind(); /* 0.3.202 (rain mask MRT) */IDirect3DSurface9* rawSurface=mirrorResources.unwrap(surface);if(spellOn)spellCopy(nullptr,rawSurface);return ext->ColorFill(rawSurface, rect, color);}
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle);if(SUCCEEDED(hr)){mirrorResources.wrap(ppSurface);}return hr;}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) override{Guard mirrorLock(mirrorState.gate);return ext->SetRenderTarget(RenderTargetIndex, mirrorResources.unwrap(pRenderTarget));}
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->GetRenderTarget(RenderTargetIndex, ppRenderTarget);if(SUCCEEDED(hr)){mirrorResources.wrap(ppRenderTarget);}return hr;}
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) override{Guard mirrorLock(mirrorState.gate);return ext->SetDepthStencilSurface(mirrorResources.unwrap(pNewZStencil));}
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->GetDepthStencilSurface(ppZStencilSurface);if(SUCCEEDED(hr)){mirrorResources.wrap(ppZStencilSurface);}return hr;}
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage, IDirect3DBaseTexture9** ppTexture) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->GetTexture(Stage, ppTexture);if(SUCCEEDED(hr)){mirrorResources.wrap(ppTexture);}return hr;}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture) override{Guard mirrorLock(mirrorState.gate);return ext->SetTexture(Stage, mirrorResources.unwrap(pTexture));}
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **declaration) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->CreateVertexDeclaration(elements, declaration);if(SUCCEEDED(hr)){mirrorResources.wrap(declaration);}return hr;}
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) override{Guard mirrorLock(mirrorState.gate);return ext->SetVertexDeclaration(mirrorResources.unwrap(pDecl));}
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->GetVertexDeclaration(ppDecl);if(SUCCEEDED(hr)){mirrorResources.wrap(ppDecl);}return hr;}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* pShader) override{Guard mirrorLock(mirrorState.gate);return ext->SetVertexShader(mirrorResources.unwrap(pShader));}
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** ppShader) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->GetVertexShader(ppShader);if(SUCCEEDED(hr)){mirrorResources.wrap(ppShader);}return hr;}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* pShader) override{Guard mirrorLock(mirrorState.gate);return ext->SetPixelShader(mirrorResources.unwrap(pShader));}
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** ppShader) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->GetPixelShader(ppShader);if(SUCCEEDED(hr)){mirrorResources.wrap(ppShader);}return hr;}
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->CreateQuery(Type, ppQuery);if(SUCCEEDED(hr)){mirrorResources.wrap(ppQuery);}return hr;}
    // 0.3.192 (MEMMAP): address-space snapshot at a device lifecycle point (see address_space_snapshot.h). Always on, never throws.
    // task 42: the in-world points (world-entry, memory-deferred, periodic) are queued to the async memory sampler's thread (memmapAsync):
    // the render thread only formats the label and the tally and copies them into the sampler's job slot.
    // Tally: lock-meter gauges, stream flags, mirror registry; in-world also the stream's atomic byte counters and the world renderer's own
    // plain reads (memmapTally). Not reachable without new synchronization (so skipped, and said so in the line): static shadow cpu/gpu,
    // local scene retained/flat/bvh, replay caches, retirement queue bytes, live proxies/tracked buffer counts.
    void memmapTally(char* tally,std::size_t size,bool inWorld){
        const auto& m=NorthlightLockMeter::state();
        int w=std::snprintf(tally,size,"mirrorResources=%zu copyResidentMiB=%.1f copyResidentBuffers=%llu copyLargeMiB=%.1f copyCapMiB=%.1f streamActive=%d memoryPressure=%d liveDevices=%ld",
            mirrorResources.size(),double(m.copyResidentBytes.load(std::memory_order_relaxed))/1048576.0,(unsigned long long)m.copyResidentBuffers.load(std::memory_order_relaxed),
            double(m.copyLargeBytes.load(std::memory_order_relaxed))/1048576.0,double(m.copyCapBytes.load(std::memory_order_relaxed))/1048576.0,
            int(NorthlightStream::streamActive.load(std::memory_order_relaxed)),int(NorthlightStream::memoryPressure.load(std::memory_order_relaxed)),(long)liveDevices);
        if(!inWorld||w<=0||std::size_t(w)>=size)return;
        if(NorthlightStream::StreamCore* core=NorthlightStream::activeCore.load(std::memory_order_acquire)){
            const auto& st=core->q.stats;auto mb=[](std::int64_t v){return double(v>0?v:0)/1048576.0;};
            w+=std::snprintf(tally+w,size-std::size_t(w)," streamQueueMiB=%.1f streamBufShadowMiB=%.1f streamLargeShadowMiB=%.1f streamTexShadowMiB=%.1f streamRingMiB=%.1f streamRetiredMiB=%.1f streamZcPatchMiB=%.1f",
                double(core->q.reservedBytes())/1048576.0,mb(st.shadowBytes.load(std::memory_order_relaxed)),mb(st.largeShadowBytes.load(std::memory_order_relaxed)),mb(st.texShadowBytes.load(std::memory_order_relaxed)),
                mb(st.ringBytes.load(std::memory_order_relaxed)),mb(st.retiredBytes.load(std::memory_order_relaxed)),mb(st.zcPatchBytes.load(std::memory_order_relaxed)));
        }
        if(w>0&&std::size_t(w)<size&&world){tally[w++]=' ';tally[w]=0;const int n=world->memmapTally(tally+w,size-std::size_t(w));if(n>0)w+=n;}
        if(w>0&&std::size_t(w)<size)std::snprintf(tally+w,size-std::size_t(w)," tallySkipped=staticShadowCpuGpu,localScene,replayCaches(need locks)");
    }
    void memmap(const char* point){
        if(!NorthlightMemorySwitches::memMap())return; /* task 42: MemMap=0, no walk, no line */
        try{
            char label[128],tally[768];
            std::snprintf(label,sizeof label,"device=%ld point=%s frame=%u tick=%lu",diagnosticId,point,frame,(unsigned long)GetTickCount());
            memmapTally(tally,sizeof tally,false);
            NorthlightMemMap::snapshot(&memmapSink,label,tally);
        }catch(...){}
    }
    // Queues an in-world point on the sampler thread (no walk here); dropped when the sampler is gone or still has a job waiting.
    void memmapAsync(const char* point){
        if(!NorthlightMemorySwitches::memMap())return;
        try{
            if(!memoryDiagnostics)return;
            NorthlightMemMap::Job job;
            std::snprintf(job.label,sizeof job.label,"device=%ld point=%s frame=%u tick=%lu",diagnosticId,point,frame,(unsigned long)GetTickCount());
            memmapTally(job.tally,sizeof job.tally,true);job.requestTick=GetTickCount(); /* every value the job needs is copied here, on the render thread */
            if(!memoryDiagnostics->requestJob(&job,sizeof job))logf("MEMMAP skipped %s reason=sampler-busy",job.label);
        }catch(...){}
    }
    // Frame end (finishFrame): one relaxed load per frame and a clock read every 32nd frame. Points only once the world renderer has drawn:
    // world-entry 600 frames after its first draw, memory-deferred on the first memory refusal/stall of this device and then at most every
    // 10 minutes, periodic every 10 minutes after world-entry.
    void memmapSchedule(){
        if(!NorthlightMemorySwitches::memMap()||!worldDrawSeen)return; /* task 42: MemMap=0 = one relaxed load; deferredRequest stays set and is ignored */
        const bool deferred=NorthlightMemMap::deferredRequest.load(std::memory_order_relaxed)&&NorthlightMemMap::deferredRequest.exchange(false,std::memory_order_relaxed);
        if(!deferred&&(frame&31))return;
        const DWORD now=GetTickCount();const char* point=nullptr;
        if(deferred&&(!memmapDeferredSeen||now-memmapDeferredAt>=600000)){point="memory-deferred";memmapDeferredSeen=true;memmapDeferredAt=now;}
        else if(!memmapEntryDone){if(frame-worldDrawFrame>=600){point="world-entry";memmapEntryDone=true;memmapPeriodicAt=now+600000;}}
        else if(int(now-memmapPeriodicAt)>=0){point="periodic";memmapPeriodicAt=now+600000;}
        if(point)memmapAsync(point);
    }
    bool worldDrawSeen=false,memmapEntryDone=false,memmapDeferredSeen=false;unsigned worldDrawFrame=0;DWORD memmapPeriodicAt=0,memmapDeferredAt=0;
    Device(IDirect3DDevice9* d,IDirect3D9* p):GuardedMirrorDevice(d,&mirrorState),parent(p),mirrorResources(this,mirrorState.gate,&mirrorEscape,&mirrorState,d),ext(new ExtensionDevice(d,&mirrorState)),stateBlocks(ext) {
        mirrorState.gate.ownerTid=MirrorGuard::threadId(); /* 0.3.180 (D0): the CreateDevice caller */
        mirrorState.gate.reportContext=this;mirrorState.gate.report=&gateForeignReport;
        parent->AddRef(); QueryPerformanceFrequency(&cpuFrequency);
        try{settingsService=NorthlightSettings::acquire(std::filesystem::path(rootPath));settingsRevision=settingsService->revision();if(!settingsRecovery.empty())logf("SETTINGS startup recovery incomplete: %s",settingsRecovery.c_str());}
        catch(const std::exception& e){logf("SETTINGS unavailable: %s",e.what());}
        gpuProfile=std::make_unique<NorthlightGpuProfile>(ext);gpuSceneProfile=std::make_unique<NorthlightGpuProfile>(ext,"GPU frame",2);gpuTimer=std::make_unique<NorthlightGpuFrameTimer>(ext); world=std::make_unique<WorldRenderer>(ext);world->setEffectsBuckets(&effectsBuckets);
        world->setConstantEpochSource({&mirrorState.constantEpoch,&mirrorState}); /* 0.3.180 (C1): read in place under the draw's gate */
        char skyRoot[MAX_PATH*3];WideCharToMultiByte(CP_UTF8,0,rootPath,-1,skyRoot,sizeof skyRoot,nullptr,nullptr);celestialDiscs=std::make_unique<NorthlightCelestialDiscRenderer>(ext,std::string(skyRoot)+"world-cache/celestial");celestialDiscs->setTerrainSource([this]{return world->celestialTerrainGeneration();},[this](unsigned body,const float* matrix){return world->drawCelestialTerrain(body,matrix);},[this](unsigned body){world->noteCelestialTerrainReuse(body);});celestialDiscs->setIdentityMap([this](std::uintptr_t exposed){return mirrorResources.rawOf(exposed,!mirrorState.enabled);});shadowBlobs=std::make_unique<NorthlightShadowBlobFilter>(ext,world->blobShadowStrength());shadowBlobs->setTexturePeek([](void* e,DWORD stage,IDirect3DBaseTexture9*& out){return static_cast<ExtensionDevice*>(e)->peekTexture(stage,out);},ext); /* 0.3.196 (task 12): borrowed stage-0 identity */water=std::make_unique<NorthlightWaterRenderer>(ext); logf("D3D9 device wrapped. Ctrl+Shift+F7 fog; F8 GI; F9 shadows; F10 all effects; F12 world debug (all with Ctrl+Shift); Ctrl+Shift+F11 settings. Components start ON; GI cache stays warm.");
        {wchar_t config[MAX_PATH],key[64]={};char hotkey[64]={};const bool named=swprintf(config,MAX_PATH,L"%lsnorthlight-renderer.ini",rootPath)>=0; /* read once, here */
         const bool on=!named||GetPrivateProfileIntW(L"Renderer",L"SettingsOverlay",1,config)!=0; /* 0 = no window hook at all */
         if(named&&GetPrivateProfileStringW(L"Renderer",L"SettingsOverlayKey",L"",key,64,config))WideCharToMultiByte(CP_UTF8,0,key,-1,hotkey,sizeof hotkey,nullptr,nullptr); /* empty = Ctrl+Shift+F11 */
         overlay=NorthlightOverlay::create(ext,on,hotkey,settingsService,[](const char* line){logf("%s",line);});}
        particleOn=world->equipmentLightsOn();
        NorthlightParticleUploads::enabled.store(particleOn,std::memory_order_relaxed);
        NorthlightParticleTextures::enabled.store(particleOn,std::memory_order_relaxed);
        contactAO=world->contactAO(); /* 0.3.201 (task 18) */
        aoActorMask=std::make_unique<NorthlightAOActorStencil>(ext,NorthlightBackend::isPackagedDxvk(selectedBackend));
        aoMethod=world->aoMethod();aoStrength=world->aoStrength();aoRadius=world->aoRadius();
        logf("AO method=%s enabled=%u strength=%.3f radiusYards=%.2f",NorthlightQuality::aoName(aoMethod),unsigned(contactAO),aoStrength,aoRadius);
        spellConfigure(); /* task 34: SpellLayer, SpellLayerClasses, SpellTrace (creation-time keys) */
        zFuncAware=world->depthFuncAware();
        nameMask.setting=world->nameMask();nameMask.debug=world->nameMaskDebug(); /* 0.3.212 (task 32) */
        frameDrawGates=world->frameDrawGates();latchDrawGates(); /* 0.3.187: after the renderers exist */
        {wchar_t markers[MAX_PATH];if(swprintf(markers,MAX_PATH,L"%lsnorthlight-frame-markers.txt",rootPath)>0&&GetFileAttributesW(markers)!=INVALID_FILE_ATTRIBUTES){frameMarkers=true;logf("FRAMEMARKERS on: E after the world effects, P before Present");}}
        weatherDetect.sink=&weatherLog; /* 0.3.198 (rain) */
        // The async sweep feeds the memory guard (always) and the periodic MEMORY line
        // (Diagnostics only). Allocation admission stays synchronous in WorldRenderer.
        try{memoryDiagnostics=std::make_unique<NorthlightMemoryDiagnostics::Sampler>(&queryAddressSpace);}
        catch(...){logf("MEMORY async sampler unavailable; memory guard and periodic diagnostics skipped");}
        if(memoryDiagnostics)memoryDiagnostics->setJob(&memmapJob);
        NorthlightMemMap::deferredRequest.store(false,std::memory_order_relaxed); /* a refusal of an earlier device's world is not this device's */
        diagnosticId=InterlockedIncrement(&deviceSerial);
        logf("DEVICE lifetime event=create id=%ld live=%ld tick=%lu",diagnosticId,InterlockedIncrement(&liveDevices),(unsigned long)GetTickCount());
        memmap("create");
    }
    ~Device() {
        logf("DEVICE lifetime event=destroy-begin id=%ld tick=%lu",diagnosticId,(unsigned long)GetTickCount());
        memmap("destroy-begin");
        logGateThreads("destroy");
        overlay.reset(); /* unhooks the window procedure (if still ours) and releases its device objects, before ext goes */
        memoryDiagnostics.reset();
        spellTeardown();
        dropBlobFaint();shadowBlobs.reset();celestialDiscs.reset();gpuProfile.reset();gpuSceneProfile.reset();gpuTimer.reset();drop(latencyQuery);water.reset();world.reset();releaseResources();
        stateBlocks.clear();ext->Release();ext=nullptr;
        const ULONG backendReferences=real->Release();parent->Release();
        logf("DEVICE lifetime event=destroy-end id=%ld live=%ld backendReleaseCount=%lu tick=%lu",diagnosticId,InterlockedDecrement(&liveDevices),(unsigned long)backendReferences,(unsigned long)GetTickCount());
        memmap("destroy-end");
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override { Guard mirrorLock(mirrorState.gate);
        if (!out) return E_POINTER;
        if (id==__uuidof(IUnknown)||id==__uuidof(IDirect3DDevice9)) { *out=this;AddRef();return S_OK; }
        *out=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override {auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** out) override { Guard mirrorLock(mirrorState.gate);if(!out)return D3DERR_INVALIDCALL;*out=parent;parent->AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pp) override { Guard mirrorLock(mirrorState.gate);
        nameMaskRunEnd();nameMask.stencilChecked=false; /* 0.3.212 (task 32) */
        rainMrtUnbind(); /* 0.3.202 (rain mask MRT) */
        NorthlightMirrorResources::generation.fetch_add(1,std::memory_order_relaxed); /* task 34: Reset drops the render-target verdicts of every texture */
        if(spellOn){spell.reset();spellSync();spellQueryN=0;spellQueryUnknown=false;spellTexFor=nullptr;spellTexKey=nullptr;spellFade.reset();} /* task 34: no state block, upload buffer or surface may outlive the device state */
        frameIntervals.reset();frameCost.reset();stallFrames.reset();mirrorResources.queryProfile={};backDescKnown=false;backSurface=nullptr;
        NorthlightParticleTextures::cache().clear();NorthlightTrackedBuffers::invalidateAll();gpuProfile->reset();gpuSceneProfile->reset();gpuFrameStarted=gpuSceneSplit=gpuEffectsSplit=false;gpuTimer->reset();gpuBudgetQpc=0;boundQpc=0;cpuBudgetQpc=0;cpuEffectsQpc=0;gpuBound.reset();replayWait.reset();replaySkipPrev=0;replayWaitPrevNs=0;drop(latencyQuery);latencyPending=false;latencyFailed=false; releaseResources(); if(world)world->reset();if(water)water->reset();if(celestialDiscs)celestialDiscs->reset();if(shadowBlobs)shadowBlobs->reset(); weatherDetect.reset();weatherSample={}; /* 0.3.198 (rain) */failed=false;latchDrawGates();
        if(pp&&world){D3DDEVICE_CREATION_PARAMETERS cp={};if(SUCCEEDED(ext->GetCreationParameters(&cp)))upgradeAutoDepth(parent,cp.AdapterOrdinal,cp.DeviceType,pp,world->nameMask()||world->contactAO(),"Reset");} /* 0.3.212 (task 32) */
        if(overlay)overlay->invalidate(); /* its D3DPOOL_DEFAULT buffers and font texture; recreated by its next draw */
        HRESULT hr=ext->Reset(pp); logf("Reset HRESULT=0x%08lx",(unsigned long)hr); return hr;
    }
    // Frame boundary only (after clearFrame()): no draw of the finished frame
    // still references a trimmed cache. Warnings are logged with Diagnostics=0.
    void trimMemory(const NorthlightMemoryDiagnostics::Sample& trigger){
        LARGE_INTEGER start={},end={};QueryPerformanceCounter(&start);
        WorldRenderer::MemoryTrim t;if(world)t=world->trimMemory();
        QueryPerformanceCounter(&end);const DWORD now=GetTickCount();memoryGuard.trimCompleted(now);
        const double ms=cpuFrequency.QuadPart>0?1000.0*double(end.QuadPart-start.QuadPart)/double(cpuFrequency.QuadPart):0.0;
        logf("MEMORY guard trim frame=%u exactWalk=1 beforeAvailableVirtualMiB=%llu beforeLargestFreeMiB=%llu regions=%llu freedEstimateMiB=%.1f terrainMiB=%.1f snapshotMiB=%.1f indexMiB=%.1f poolMiB=%.1f gpuCacheMiB=%.1f workerLocalGeometry=1 ms=%.3f trims=%u tick=%lu",frame,
            (unsigned long long)(trigger.availableVirtual>>20),(unsigned long long)(trigger.largestFree>>20),(unsigned long long)trigger.regions,
            double(t.total())/1048576,double(t.terrain)/1048576,double(t.snapshots)/1048576,double(t.indices)/1048576,double(t.pool)/1048576,double(t.gpu)/1048576,ms,memoryGuard.trims(),(unsigned long)now);
        try{if(memoryDiagnostics)memoryDiagnostics->request();}catch(...){} /* after-trim sample */
    }
    // RenderProfile: one line per frame kind present in the window, then all frames:
    // mean,p50,p95,p99,max. With DiagReplayProbe the windows are the probe's (mode=).
    // Cost per frame: 5 clock reads (3 at Present, 2 around renderEffects).
    void logFrameCost(){
        namespace P=NorthlightRenderThreadProbe;P::Report r=frameCost.take();
        LARGE_INTEGER a={},b={},t={};QueryPerformanceCounter(&a);for(unsigned i=0;i<32;++i)QueryPerformanceCounter(&t);QueryPerformanceCounter(&b);
        const double qpcNs=cpuFrequency.QuadPart>0?double(b.QuadPart-a.QuadPart)*1e9/double(cpuFrequency.QuadPart)/33.0:0.0;
        for(unsigned k=0;k<=P::Kinds;++k){const P::Summary* s=k<P::Kinds?r.kinds[k]:r.all;if(!s[0].count)continue;
            if(P::profiling())logf("FRAME cost device=%ld frame=%u window=%llu probeMode=%s kind=%s frames=%u windowFrames=%u excluded=%u frameMs=%.3f,%.3f,%.3f,%.3f,%.3f effectsMs=%.3f,%.3f,%.3f,%.3f,%.3f finishMs=%.3f,%.3f,%.3f,%.3f,%.3f presentMs=%.3f,%.3f,%.3f,%.3f,%.3f clockReadsPerFrame=5 timerUsPerFrame=%.2f",
                diagnosticId,frame,(unsigned long long)frameCostWindow,P::probeModeName(frameCostMode),P::kindName(k),s[0].count,r.frames,r.excluded,
                s[P::Frame].mean,s[P::Frame].p50,s[P::Frame].p95,s[P::Frame].p99,s[P::Frame].max,s[P::Effects].mean,s[P::Effects].p50,s[P::Effects].p95,s[P::Effects].p99,s[P::Effects].max,
                s[P::Finish].mean,s[P::Finish].p50,s[P::Finish].p95,s[P::Finish].p99,s[P::Finish].max,s[P::Present].mean,s[P::Present].p50,s[P::Present].p95,s[P::Present].p99,s[P::Present].max,5*qpcNs/1000.0);
        }
    }
    // 0.3.154 microbenchmark (DRAWGATE lines, at log time): GateBenchIters x each per-draw primitive,
    // ns per op including the loop. The gate is held here, so the lock is a private recursive_mutex.
    static constexpr unsigned GateBenchIters=256;
    struct GateBench {double lockNs=-1,findNs=-1,getVsNs=-1,qpcNs=-1,ownerNs=-1,weatherNs=-1;unsigned findHits=0;};
    GateBench gateBench(){
        GateBench b;if(cpuFrequency.QuadPart<=0)return b;const double ns=1e9/double(cpuFrequency.QuadPart);
        LARGE_INTEGER t0={},t1={},t={};
        QueryPerformanceCounter(&t0);for(unsigned i=0;i<GateBenchIters;++i){gateBenchLock.lock();gateBenchLock.unlock();}QueryPerformanceCounter(&t1);
        b.lockNs=double(t1.QuadPart-t0.QuadPart)*ns/GateBenchIters;
        // 0.3.182 (D1): a MirrorGuard on the private gate, the owner's elided entry and exit as every game call
        // takes it (tid compare, xchg, seq_cst load, TLS held set/restore, census check, release store).
        QueryPerformanceCounter(&t0);for(unsigned i=0;i<GateBenchIters;++i){MirrorGuard probe(gateBenchGate);}QueryPerformanceCounter(&t1);
        b.ownerNs=double(t1.QuadPart-t0.QuadPart)*ns/GateBenchIters;
        IDirect3DVertexShader9* bound=nullptr;ext->GetVertexShader(&bound);IDirect3DVertexShader9* volatile key=bound;std::size_t hits=0;
        QueryPerformanceCounter(&t0);for(unsigned i=0;i<GateBenchIters;++i){IDirect3DVertexShader9* k=key;hits+=vsTags.find(k)!=vsTags.end();}QueryPerformanceCounter(&t1);
        b.findNs=double(t1.QuadPart-t0.QuadPart)*ns/GateBenchIters;drop(bound);
        const std::uint64_t answered=mirrorState.answered,forwarded=mirrorState.forwarded; /* the MIRROR line keeps the game's counts */
        QueryPerformanceCounter(&t0);for(unsigned i=0;i<GateBenchIters;++i){IDirect3DVertexShader9* v=nullptr;ext->GetVertexShader(&v);drop(v);}QueryPerformanceCounter(&t1);
        b.getVsNs=double(t1.QuadPart-t0.QuadPart)*ns/GateBenchIters;mirrorState.answered=answered;mirrorState.forwarded=forwarded;
        QueryPerformanceCounter(&t0);for(unsigned i=0;i<GateBenchIters;++i)QueryPerformanceCounter(&t);QueryPerformanceCounter(&t1);
        b.qpcNs=double(t1.QuadPart-t0.QuadPart)*ns/(GateBenchIters+1);
        // 0.3.198 (rain): the draw hook's weather comparison, hot set and matching (the worst case), on a throwaway sample.
        {const void* volatile benchHot=&b;const void* volatile benchTexture=&b;NorthlightWeather::Sample sample;
            QueryPerformanceCounter(&t0);for(unsigned i=0;i<GateBenchIters;++i){const void* hot=benchHot;if(hot&&!applied&&benchTexture==hot){sample.primitives+=i;++sample.draws;}}QueryPerformanceCounter(&t1);
            b.weatherNs=double(t1.QuadPart-t0.QuadPart)*ns/GateBenchIters;}
        b.findHits=unsigned(hits);return b;
    }
    // 0.3.154: one DRAWGATE ab line per RenderProfile sample frame; -1 = not measured.
    void logDrawGate(unsigned sampleFrame,bool frameApplied,double qpcNs,unsigned long long frameReads){
        const double ms=1000.0/double(cpuFrequency.QuadPart);const bool timed=!gateUntimed;
        const bool scene=gateSceneEnd.QuadPart&&gatePresentDone&&gateSceneEnd.QuadPart>=gatePresentDone;
        // 0.3.180 (D0): the owner's outer gate entries by site class; gameCalls is their sum (every lock site is a MirrorGuard now).
        // 0.3.182 (D1): they are elided (the mutex only while a foreign call is in flight: GATE threads ownerLocked).
        MirrorGate& gate=mirrorState.gate;
        const unsigned acqDevice=gate.takeAcquired(MirrorSite::Device),acqRegistry=gate.takeAcquired(MirrorSite::Registry),acqResource=gate.takeAcquired(MirrorSite::Resource),
            acqOther=gate.takeAcquired(MirrorSite::StateBlock)+gate.takeAcquired(MirrorSite::SwapChain)+gate.takeAcquired(MirrorSite::Raw);
        const unsigned gameCalls=acqDevice+acqRegistry+acqResource+acqOther,perf=perfCalls.load(std::memory_order_relaxed);
        const unsigned long long generations=NorthlightTrackedBuffers::clock.load(std::memory_order_relaxed)-gateStart.generations;
        const GateBench b=gateBench();const GateCounts& n=gateCounts;
        if(NorthlightRenderThreadProbe::profiling())logf("DRAWGATE ab frame=%u mode=%c perDrawTimers=%u applied=%u far=%u captureSkipped=%u draws=%u sceneDraws=%u sceneMs=%.4f sceneReads=%llu frameReads=%llu qpcNs=%.1f "
            "drawGateMs=%.4f prepMs=%.4f captureMs=%.4f waterMs=%.4f effectsMs=%.4f p=%u c=%u w=%u missingVS=%u terrain=%u wmo=%u ui=%u fullPasses=%u shadowSwaps=%u audits=%u "
            "blobCalls=%u blobTextures=%u blobClaimed=%u gameCalls=%u perfCalls=%u bufferGenerations=%llu bufferCreates=%u processVertices=%u benchIters=%u lockNs=%.1f findNs=%.1f findHits=%u getVsNs=%.1f qpcBackNs=%.1f ownerNs=%.1f acqDevice=%u acqRegistry=%u acqResource=%u acqOther=%u vsCacheHits=%llu vsCacheMisses=%llu weatherDraws=%u weatherPrims=%u weatherNs=%.1f",
            sampleFrame,timed?'T':'U',unsigned(timed),unsigned(frameApplied),unsigned(world&&world->lastFarDrawn()),unsigned(world&&world->captureSkippedLastFrame()),drawCalls-frameStartDrawCalls,scene?gateSceneDraws-frameStartDrawCalls:0u,
            scene?double(gateSceneEnd.QuadPart-gatePresentDone)*ms:-1.0,scene?gateSceneReads-frameStartScopeReads:0ull,frameReads,qpcNs,
            timed?std::max(0.0,double(cpuPrep-cpuCapture-cpuEffects)*ms):-1.0,timed?cpuPrep*ms:-1.0,timed?cpuCapture*ms:-1.0,timed?cpuWaterCapture*ms:-1.0,cpuEffects*ms,
            n.prep,n.capture,n.water,missingVS-gateStart.missingVS,terrainDraws-gateStart.terrain,n.wmo,uiDraws-gateStart.ui,n.fullPasses,terrainShadowDraws-gateStart.shadowSwaps,n.audits,
            n.blobCalls,n.blobTextures,n.blobClaimed,gameCalls,perf,generations,n.bufferCreates,n.processVertices,GateBenchIters,b.lockNs,b.findNs,b.findHits,b.getVsNs,b.qpcNs,
            b.ownerNs,acqDevice,acqRegistry,acqResource,acqOther,vsCacheHits-gateStart.vsHits,vsCacheMisses-gateStart.vsMisses,weatherSample.draws,weatherSample.primitives,b.weatherNs); /* 0.3.198 (rain): derived from the sample, no per-draw counter */
    }
    // The settings window over the finished frame (game UI included): once per presented frame of the implicit swap chain, on both
    // Present paths (Device and SwapChain reach finishFrame), last before the real Present. Closed with nothing pending: atomic loads
    // only. Its own failures turn only the overlay off.
    void drawOverlay(bool implicitChain){
        if(!implicitChain||!overlay||!overlay->visible())return;
        ExtensionDevice::RawScope raw(*ext);SavedState saved(ext,&stateBlocks);
        if(saved.ok)overlay->draw();
    }
    void finishFrame(bool implicitChain=true) {
        Guard mirrorLock(mirrorState.gate);
        rainMrtUnbind(); /* 0.3.202 (rain mask MRT): Present, both paths (Device and SwapChain) */
        if(spellOn)spellFlush(NorthlightSpellLayer::Reason::Frame,false,NorthlightSpellLayer::InPlaceKind::None,"present"); /* task 34: Present before the boundary */
        struct InvalidateOnReturn {DeviceMirror& state;~InvalidateOnReturn(){state.invalidate();}} invalidate{mirrorState};
        if(extensionFault){clearFrame();drawOverlay(implicitChain);return;}
        extensionWork("frame finish",[&]{finishFrameImpl();});
        drawOverlay(implicitChain);
    }
    // 0.3.198 (rain): frame end, on the finishing thread, under the gate: the frame's sample into the tracker, the state to the
    // world, the hot candidate rotated on every frame it counted no draws, and the WEATHER lines.
    void weatherFrame(unsigned sampleFrame){
        namespace W=NorthlightWeather;
        if(gateFrame){logWeatherProbe(sampleFrame);weatherProbe={};}
        LARGE_INTEGER now={};double dt=0;
        if(cpuFrequency.QuadPart>0&&QueryPerformanceCounter(&now)){if(weatherTick)dt=double(now.QuadPart-weatherTick)/double(cpuFrequency.QuadPart);weatherTick=now.QuadPart;}
        if(!mirrorState.enabled){weatherDetect.setOff(true);if(!weatherOffReported){weatherOffReported=true;logf("WEATHER detect=off (mirror inactive)");}}
        weatherSample.kind=weatherSample.draws?weatherDetect.hotKind:W::Kind::None;
        { // the game's own weather state is the primary source when its proofs and reads succeed; the detector is the fallback and the cross-check
            const bool memoryWanted=NorthlightWeatherMemory::enabled()&&(!world||world->weatherMemorySetting()); /* WeatherMemory=0: no proofs and no reads at all */
            const NorthlightWeatherMemory::Reading memory=memoryWanted?weatherMemory.frame(NorthlightWorldContext::readSelf):NorthlightWeatherMemory::Reading{};
            const bool memoryCounts=NorthlightWeatherMemory::counts(memoryWanted,memory.valid,mirrorState.enabled,skyBandSeen);skyBandSeen=false; /* indoors the client draws no sky and no weather: the frame then goes the draw-based way (hold, fade) */
            weatherSample.memory=memoryCounts;weatherSample.memoryKind=memory.kind;weatherSample.memoryIntensity=memory.intensity;
            if(memoryWanted&&int(memory.valid)!=weatherMemoryState){weatherMemoryState=int(memory.valid);logf("WEATHER memory %s (proofs=%d reads=%u failures=%u kind=%s storm=%.3f intensity=%.3f)",memory.valid?"valid":"invalid, texture detector in use",int(weatherMemory.proofsOk()),weatherMemory.reads,weatherMemory.failures,W::kindName(memory.kind),memory.stormIntensity,memory.intensity);}
            const auto sort=weatherCross.frame(memoryCounts,memory.intensity>0?memory.kind:W::Kind::None,weatherSample.kind);
            if(sort!=NorthlightWeatherMemory::CrossCheck::Result::Agree)logf("WEATHER memory and texture detector disagree: %s (memory kind=%s storm=%.3f intensity=%.3f, texture kind=%s draws=%u)",NorthlightWeatherMemory::CrossCheck::name(sort),W::kindName(memory.kind),memory.stormIntensity,memory.intensity,W::kindName(weatherSample.kind),weatherSample.draws);
        }
        weatherTracker.frame(weatherSample,float(dt));const W::State& st=weatherTracker.state();
        const bool counted=weatherSample.draws!=0;const W::Sample frameSample=weatherSample;weatherSample={};
        weatherDetect.endFrame(counted); /* hot drew: it stays; otherwise the next candidate (the tracker's hold bridges the rotation) */
        if(world)world->setWeather(st);
        // 0.3.199 (rain mist): armed for the next frame while the weather is rain (the tracker's hold bridges detection gaps) and RainBlend is on.
        weatherDetect.mistArmed=mirrorState.enabled&&st.kind==W::Kind::Rain&&world&&world->rainBlendSetting()&&weatherDetect.mistCount();
        const unsigned mistSkips=weatherMistSkips,mistUnknown=weatherMistUnknown,mistOther=weatherMistOtherStage;weatherMistSkips=weatherMistUnknown=weatherMistOtherStage=0;
        const unsigned rainMaskCount=rainMaskDraws,rainMrtRunCount=rainMrtRuns,rainMaskSkipCount=rainMaskSkips;rainMaskDraws=rainMrtRuns=rainMaskSkips=0; /* 0.3.202 (rain mask MRT) */
        if(FAILED(rainMaskResolveHr)&&!rainMaskResolveLogged){rainMaskResolveLogged=true;logf("WEATHER rain mask resolve failed HRESULT=0x%08lx",(unsigned long)rainMaskResolveHr);}
        const bool high=st.blend>=W::kBlendLogHigh,low=st.blend>=W::kBlendLogLow;
        weatherLogClock+=dt;
        const bool change=st.kind!=weatherLoggedKind||high!=weatherLoggedBlendHigh||low!=weatherLoggedBlendLow;
        if(change||(diagnostics()&&st.kind!=W::Kind::None&&weatherLogClock>=10.0)){
            weatherLogClock=0;weatherLoggedKind=st.kind;weatherLoggedBlendHigh=high;weatherLoggedBlendLow=low;
            logf("WEATHER kind=%s prims=%u draws=%u intensity=%.3f blend=%.3f candidates=%u generation=%u hot=%p mists=%u mistArmed=%d mistSkips=%u mistUnknownStage0=%u mistOtherStage=%u mistOverflows=%u rainMask=%u rainMrtRuns=%u rainMaskSkips=%u",W::kindName(st.kind),frameSample.primitives,frameSample.draws,st.intensity,st.blend,weatherDetect.count(),weatherDetect.generation,weatherDetect.hot,weatherDetect.mistCount(),int(weatherDetect.mistArmed),mistSkips,mistUnknown,mistOther,weatherDetect.mistOverflows,rainMaskCount,rainMrtRunCount,rainMaskSkipCount);
        }
    }
    void logWeatherProbe(unsigned sampleFrame){
        logf("WEATHER probe frame=%u maxDrawPrims=%u tex=%p known=%d candidate=%d vs=%d ps=%d hot=%p hotDraws=%u tall=%u overflows=%u",sampleFrame,weatherProbe.count,weatherProbe.texture,int(weatherProbe.known),int(weatherProbe.candidate),int(weatherProbe.vs),int(weatherProbe.ps),weatherDetect.hot,weatherSample.draws,weatherDetect.tallSeen,weatherDetect.overflows);
    }
    // 0.3.200 (gpu budget): once per frame: a finished timer reading (if any) updates the controller and the world's level; a level change is
    // logged (the first 32 always, later with Diagnostics), the state on RenderProfile sample frames. GpuBudgetMs=0: level 0, nothing measured.
    void gpuBudgetFrame(){
        // task 45: this frame's GPU latency wait (the previous Present's), smoothed per frame. Known only with the latency limit (command stream, framesAhead>=2);
        // unknown: the budgets lighten as before 45. Not GPU-bound = the CPU is the limit, so the GPU budget buys nothing and the CPU budget acts.
        const std::int64_t boundNow=qpcNow();
        const float boundDt=boundQpc&&cpuFrequency.QuadPart>0?float(double(boundNow-boundQpc)/double(cpuFrequency.QuadPart)):0.f;
        if(limitGpuLatency&&!latencyFailed)gpuBound.update(float(latencyLastMs),boundDt);else gpuBound.reset();
        boundQpc=boundNow;
        boundKnown=limitGpuLatency&&!latencyFailed;gpuLimited=!boundKnown||gpuBound.bound();
        // The game thread's wait for this thread (Present + backpressure), from the stream's relaxed atomic counters: when the game never waits, this thread is not what limits the frame.
        replayKnown=streamStats!=nullptr;
        if(replayKnown){const std::uint64_t waitNs=NorthlightStream::get(streamStats->presentNs)+NorthlightStream::get(streamStats->backpressureNs);
            // A frame the replay skipped (StreamFrameSkip: it fell two frames behind, so the game did not have to wait) is a replay-bound frame too: it counts as a full capped wait.
            const std::uint64_t skips=NorthlightStream::get(streamStats->skippedFrames);const bool skippedNow=replaySkipPrev&&skips>replaySkipPrev;replaySkipPrev=skips;
            replayWait.update(skippedNow?NorthlightGpuBudget::MaxWaitMs:replayWaitPrevNs&&waitNs>=replayWaitPrevNs?float(double(waitNs-replayWaitPrevNs)*1e-6):0.f,boundDt);replayWaitPrevNs=waitNs;}
        else replayWait.reset();
        cpuLimited=(!replayKnown||replayWait.bound())&&(!boundKnown||!gpuBound.bound());
        const unsigned budget=world?world->gpuBudgetMs():0;
        if(!budget){gpuBudget.reset();gpuBudgetQpc=0;if(world)world->setGpuBudgetLevel(0);return;}
        double ms=0;
        if(gpuTimer&&gpuTimer->poll(ms)){const std::int64_t now=qpcNow();
            const float dt=gpuBudgetQpc&&cpuFrequency.QuadPart>0?float(double(now-gpuBudgetQpc)/double(cpuFrequency.QuadPart)):0.f;gpuBudgetQpc=now;gpuBudgetLastMs=ms;
            const unsigned before=gpuBudget.level;world->setGpuBudgetLevel(gpuBudget.update(float(budget),float(ms),dt,gpuLimited));
            if(gpuBudget.level!=before&&(gpuBudget.changes<=32||diagnostics()))
                logf("GPUBUDGET level %u -> %u smoothed=%.3f budget=%u changes=%u",before,gpuBudget.level,double(gpuBudget.smoothed),budget,gpuBudget.changes);}
        if(limitGpuLatency&&sampled()&&NorthlightRenderThreadProbe::profiling()){logf("CSTREAM GPU latency waits=%llu waitMs=%.1f",(unsigned long long)latencyWaits,latencyWaitMs);latencyWaits=0;latencyWaitMs=0;}
        if(sampled()&&NorthlightRenderThreadProbe::profiling())logf("GPUBUDGET ms=%.3f smoothed=%.3f budget=%.1f level=%u gpuWait=%.2f gpuBound=%d",gpuBudgetLastMs,double(gpuBudget.smoothed),double(budget),gpuBudget.level,double(gpuBound.smoothed),boundKnown?int(gpuBound.bound()):-1);
    }
    // task 45: ActorShadowCpuMs. The effects span of this frame (renderEffects from the applied check to its return: the world render incl. the actor shadow replay,
    // job joins and the effect passes' D3D calls; NOT the capture of the frame's draws, which is spread over per-draw hooks and only timed on sample frames) feeds a
    // second controller (same hysteresis); its level sets the effective actor shadow radius. It lightens only while the frame is not GPU-bound (or the signal is unknown).
    void cpuBudgetFrame(){
        const unsigned budget=world?world->actorShadowCpuMs():0;
        if(!budget){cpuBudget.reset();cpuBudgetQpc=0;cpuEffectsQpc=0;if(world)world->setCpuBudgetLevel(0);return;}
        if(cpuEffectsQpc>0&&cpuFrequency.QuadPart>0){const std::int64_t now=qpcNow();
            const float dt=cpuBudgetQpc?float(double(now-cpuBudgetQpc)/double(cpuFrequency.QuadPart)):0.f;cpuBudgetQpc=now;
            const double ms=double(cpuEffectsQpc)*1000.0/double(cpuFrequency.QuadPart);cpuBudgetLastMs=ms;cpuEffectsQpc=0;
            const unsigned before=cpuBudget.level;world->setCpuBudgetLevel(cpuBudget.update(float(budget),float(ms),dt,cpuLimited));
            if(cpuBudget.level!=before&&(cpuBudget.changes<=32||diagnostics()))
                logf("CPUBUDGET level %u -> %u smoothed=%.3f budget=%u radius=%u changes=%u",before,cpuBudget.level,double(cpuBudget.smoothed),budget,world->effectiveActorShadowRadius(),cpuBudget.changes);}
        if(sampled()&&NorthlightRenderThreadProbe::profiling())logf("CPUBUDGET ms=%.3f smoothed=%.3f budget=%.1f level=%u radius=%u gpuBound=%d replayWait=%.2f replayBound=%d",cpuBudgetLastMs,double(cpuBudget.smoothed),double(budget),cpuBudget.level,world->effectiveActorShadowRadius(),boundKnown?int(gpuBound.bound()):-1,double(replayWait.smoothed),replayKnown?int(replayWait.bound()):-1);
    }
    void finishFrameImpl() {
        nameMaskRunEnd(); /* 0.3.212 (task 32) */
        const auto frameEffects=effectKeys.settings;const bool frameEnabled=enabled;
        // Focus also goes on the FRAME interval line (0 if any frame since the last line lacked it): a background
        // window runs at the client's own frame cap (maxFPSBk), which is not renderer cost.
        D3DDEVICE_CREATION_PARAMETERS cp={}; ext->GetCreationParameters(&cp);
        const bool focus=GetForegroundWindow()==cp.hFocusWindow;intervalFocus&=focus;
        LARGE_INTEGER intervalTick={};NorthlightFrameIntervals::Report intervalReport;
        if(diagnostics()&&QueryPerformanceCounter(&intervalTick)&&frameIntervals.sample(intervalTick.QuadPart,cpuFrequency.QuadPart,intervalReport)){
            logf("FRAME interval device=%ld frame=%u tick=%lu enabled=%d samples=%u meanMs=%.3f p50Ms=%.3f p95Ms=%.3f maxMs=%.3f fps=%.2f gi=%u shadows=%u fog=%u focus=%d",diagnosticId,frame,(unsigned long)GetTickCount(),enabled,intervalReport.count,intervalReport.meanMs,intervalReport.p50Ms,intervalReport.p95Ms,intervalReport.maxMs,1000.0/intervalReport.meanMs,unsigned(frameEffects.gi),unsigned(frameEffects.shadows),unsigned(frameEffects.fog),int(intervalFocus));intervalFocus=true;}
        if(diagnostics()){gpuProfile->poll();if(gpuSceneProfile)gpuSceneProfile->poll();}
        if(diagnostics()&&frame%600==0)logClientLatency(frame,"periodic");
        gpuBudgetFrame();cpuBudgetFrame(); /* task 45 */
        // Poll only while this game owns foreground focus.
        bool modifiers=(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_SHIFT)&0x8000);
        bool k10=focus&&modifiers&&(GetAsyncKeyState(VK_F10)&0x8000);
        const unsigned componentKeys=((GetAsyncKeyState(VK_F7)&0x8000)?NorthlightEffectSwitches::Fog:0u)|
            ((GetAsyncKeyState(VK_F8)&0x8000)?NorthlightEffectSwitches::GI:0u)|
            ((GetAsyncKeyState(VK_F9)&0x8000)?NorthlightEffectSwitches::Shadows:0u);
        if(effectKeys.poll(focus,modifiers,componentKeys)){
            const auto& settings=effectKeys.settings;
            if(world)world->setEffects(settings);
            frameIntervals.reset();frameCost.reset();stallFrames.reset();mirrorResources.queryProfile={};
            logf("Effects components tick=%lu master=%u GI=%s shadows=%s fog=%s giCache=warm",
                (unsigned long)GetTickCount(),unsigned(enabled),settings.gi?"ON":"OFF",settings.shadows?"ON":"OFF",settings.fog?"ON":"OFF");
        }
        bool retry=k10&&!key10&&!enabled;
        if(k10&&!key10){enabled=!enabled;frameIntervals.reset();frameCost.reset();stallFrames.reset();mirrorResources.queryProfile={};logf("Effects %s tick=%lu",enabled?"ON":"OFF",(unsigned long)GetTickCount());}
        bool k12=focus&&modifiers&&(GetAsyncKeyState(VK_F12)&0x8000);
        if(k12&&!key12){worldDebug=(worldDebug+1)%4;logf("World debug %d tick=%lu (0 normal,1 shadows,2 GI,3 volume)",worldDebug,(unsigned long)GetTickCount());}
        key12=k12;key10=k10;
        uiFallbackEndFrame();
        if(frame==1||(frame%600==0&&diagnostics()))logf("frame=%u applied=%u terrainShaders=%u uiShaders=%u projectionRejects=%u terrainThisFrame=%d depth=%d draws=%u missingVS=%u terrainDraws=%u terrainShadowDraws=%u viewportRejects=%u depthRejects=%u uiDraws=%u uiAfterTerrain=%u uiFallback=%u uiFallbackState=%c",frame,appliedFrames,matchedTerrain,matchedUI,projectionRejects,terrain,captured,drawCalls,missingVS,terrainDraws,terrainShadowDraws,viewportRejects,depthRejects,uiDraws,uiAfterTerrain,uiFallbackBoundaries,uiFallback.disarmed()?'d':uiFallback.armed()?'a':'l');
        // The address-space sweep is asynchronous; the render thread only compares
        // its result. Trims run below, after clearFrame(), at the frame boundary.
        bool memoryTrim=false;int memoryCaps=-1,memoryWorld=-1; /* -1 unchanged, 0 full, 1 half (stream, replay copies); memoryWorld: 1 in any pressure episode (the world's caches and pool shrink, no lock cost), 0 after recovery */
        NorthlightMemoryDiagnostics::Sample memorySample; /* exact full walk (never an admission witness bound) */
        if(memoryDiagnostics){
            try{
                const DWORD now=GetTickCount();
                if(memoryGuard.requestDue(now))memoryDiagnostics->request();
                auto& sample=memorySample;
                if(memoryDiagnostics->take(sample)){
                    if(sample.valid){
                        // The geometry builder's unmet floor also enters pressure (memory_guard.h demand).
                        const std::uint64_t demand=world?world->geometryMemoryDemand.load(std::memory_order_relaxed):0;
                        const auto decision=memoryGuard.update(sample.availableVirtual,sample.largestFree,DWORD(sample.tick),demand);
                        memoryTrim=decision.trim;
                        if(decision.afterTrim)logf("MEMORY guard after-trim exactWalk=1 availableVirtualMiB=%llu largestFreeMiB=%llu regions=%llu pressure=%u trims=%u sampleTick=%llu",
                            (unsigned long long)(sample.availableVirtual>>20),(unsigned long long)(sample.largestFree>>20),(unsigned long long)sample.regions,unsigned(memoryGuard.pressure()),memoryGuard.trims(),(unsigned long long)sample.tick);
                        if(decision.entered||decision.recovered||decision.capsChanged){
                            if(decision.entered||decision.recovered)memoryWorld=decision.entered?1:0;
                            if(decision.capsChanged)memoryCaps=decision.halfCaps?1:0; /* 0.3.216 (issue #66 follow-up): a demand-only episode keeps full caps (halving them dropped the buffer shadows and made every lock a sync round trip) */
                            logf("MEMORY guard %s exactWalk=1 availableVirtualMiB=%llu largestFreeMiB=%llu enterBelowMiB=%llu/%llu recoverAboveMiB=%llu/%llu caps=%s entries=%u trims=%u reason=%s demandMiB=%llu",decision.recovered?"recovered":"pressure",
                                (unsigned long long)(sample.availableVirtual>>20),(unsigned long long)(sample.largestFree>>20),
                                (unsigned long long)(memoryGuard.policy().enterLargest>>20),(unsigned long long)(memoryGuard.policy().enterAvailable>>20),
                                (unsigned long long)(memoryGuard.policy().recoverLargest>>20),(unsigned long long)(memoryGuard.recoverAvailable()>>20),
                                memoryGuard.capsHalved()?"half":"full",memoryGuard.entries(),memoryGuard.trims(),decision.demand?"geometry-demand":"address-space",(unsigned long long)(demand>>20));
                        }
                        if(decision.report&&diagnostics())logf("MEMORY frame=%u availableVirtualMiB=%llu largestFreeMiB=%llu availablePhysicalMiB=%llu device=%ld totalVirtualMiB=%llu virtualQueryFreeMiB=%llu liveDevices=%ld tick=%lu async=1 sampleTick=%llu sampleAgeMs=%lu sampleWallMs=%.3f regions=%llu pressure=%u minAvailableMiB=%llu minLargestMiB=%llu trims=%u exactWalk=1",frame,
                            (unsigned long long)(sample.availableVirtual>>20),(unsigned long long)(sample.largestFree>>20),(unsigned long long)(sample.availablePhysical>>20),diagnosticId,
                            (unsigned long long)(sample.totalVirtual>>20),(unsigned long long)(sample.totalFree>>20),InterlockedCompareExchange(&liveDevices,0,0),(unsigned long)now,
                            (unsigned long long)sample.tick,(unsigned long)(now-DWORD(sample.tick)),double(sample.wallNanoseconds)/1e6,(unsigned long long)sample.regions,
                            unsigned(memoryGuard.pressure()),(unsigned long long)(memoryGuard.minAvailable()>>20),(unsigned long long)(memoryGuard.minLargest()>>20),memoryGuard.trims());
                    }
                    else if(diagnostics())logf("MEMORY sample failed frame=%u device=%ld async=1",frame,diagnosticId);
                }
            }catch(...){memoryDiagnostics.reset();logf("MEMORY async sampler stopped; memory guard and periodic diagnostics skipped");}
        }
        const unsigned sampleFrame=frame;LONGLONG cleanup=0;
        // Read before clearFrame() resets the water renderer's frame counters.
        const unsigned waterRedraws=water?water->frameCaptures():0,waterMrt=water?water->frameMrtDraws():0,waterDegraded=water?water->frameMrtDegraded():0,waterDraws=waterRedraws+waterMrt,waterScans=water?water->frameMaskScans():0,waterClears=water?water->frameClears():0,waterReadFailures=water?water->frameReadFailures():0,waterBinds=water?water->frameMrtBinds():0,waterLongestRun=water?water->frameMrtLongestRun():0;
        if(sampled())logf("EFFECT trailing world frame=%u worldDraws=%u skinnedDraws=%u",frame,postEffectWorldDraws,postEffectSkinnedDraws);
        if(sampled()&&aoActorOn&&aoActorFrame==frame)logf("AO_ACTOR frame=%u draws=%u valid=%u used=%u captureCpuUs=%.1f",frame,aoActorMask->capturedDraws(),unsigned(aoActorMask->valid()),unsigned(aoActorUsed),
            sampledDrawTimers()&&cpuFrequency.QuadPart>0?double(cpuAoActorCapture)*1e6/double(cpuFrequency.QuadPart):-1.0);
        if(sampled()&&nameMask.armed()){
            const double us=cpuFrequency.QuadPart>0?1e6/double(cpuFrequency.QuadPart):0.0;
            const double decide=nameDecideTimed?double(cpuNameDecide)*double(nameDecideCalls)/double(nameDecideTimed)*us:0.0,mark=double(cpuNameMark)*us,unmark=double(cpuNameUnmark)*us;
            logf("NAMEMASK frame=%u marked=%u runs=%u unmarked=%u composite=%d depthFormat=%lu stencil=%d decisions=%u decisionsTimed=%u decideUs=%.1f markUs=%.1f unmarkUs=%.1f cpuUs=%.1f",frame,nameMask.marks,nameMask.runs,nameMask.unmarkDraws,int(nameMask.composited),(unsigned long)(worldDepthDescKnown?worldDepthDesc.Format:D3DFMT_UNKNOWN),int(maskStencil),nameDecideCalls,nameDecideTimed,decide,mark,unmark,decide+mark+unmark);
        }
        cpuNameDecide=cpuNameMark=cpuNameUnmark=0;nameDecideCalls=nameDecideTimed=0;
        nameMask.endFrame(); /* 0.3.212 (task 32): marks never outlive the frame (the next frame's first Clear(Z) clears the stencil) */
        if(sampled())logf("TRANSLUCENT frame=%u draws=%u lastOpaqueZ=%u clearResolve=%u earlyResolve=%u earlyResolveAt=%u earlyResolveTotal=%u earlyResolveUndone=%u earlyResolveUndoneTotal=%u",frame,censusDraws,lastOpaqueZAt,clearResolveAt,earlyResolves,earlyResolveAt,earlyResolveTotal,earlyResolveUndone,earlyResolveUndoneTotal);
        postEffectWorldDraws=postEffectSkinnedDraws=0;
        spellFrameEnd(); /* task 34: after the frame's diagnostics lines */
        const bool sampledFrame=sampled(),frameApplied=applied;
        // 0.3.200 (frame trace): with Diagnostics, 240 consecutive frames out of every 1800 get one line each: whether the effects ran, the world drew,
        // and through which context (1 terrain + global light, 2 terrain native light, 3 WMO, 4 model), to see frame-to-frame alternation in the log.
        if(diagnostics()&&frame%1800<240)logf("FRAMETRACE frame=%u tick=%lu applied=%d enabled=%d projection=%d terrain=%d world=%d context=%u fog=%u skip=%s",frame,(unsigned long)GetTickCount(),
            int(applied),int(enabled),int(projectionValid),int(terrain),traceWorld,world?world->frameTraceContext():0u,world?world->frameTraceFog():0u,traceWorld==0&&world?world->lastSkipReason():"-");
        traceWorld=-1;
        {CpuScope cpu(sampledFrame?&cleanup:nullptr);clearFrame();}
        if(settingsService)if(auto update=settingsService->after(settingsRevision)){
            world->applyLiveSettings(update->config);dropBlobFaint();shadowBlobs->setStrength(world->blobShadowStrength());
            contactAO=world->contactAO();aoStrength=world->aoStrength();aoRadius=world->aoRadius();
            if(aoMethod!=world->aoMethod()){aoMethod=world->aoMethod();drop(aoContactBloomPS);drop(aoActorPS);drop(aoActorAlphaPS);aoActorShaderFailed=false;} /* method shaders are recreated before the next AO pass */
            frameDrawGates=world->frameDrawGates();zFuncAware=world->depthFuncAware();nameMask.debug=world->nameMaskDebug();
            if(spellOn){spellOptionsPending=true;if(spellTraceSetting!=world->spellTraceSetting()){spellTraceSetting=world->spellTraceSetting();spellTraceLeft=spellTraceSetting;}}
            settingsRevision=update->revision;settingsService->acknowledge(*update);
            logf("SETTINGS frame %s revision=%s (creation-time changes require restart)",update->preview?"preview":"saved",settingsRevision.c_str());}
        if(spellOptionsPending&&world&&spell.liveOptions(world->spellLayerClassesSetting(),world->spellFogSetting()!=0)){spellClasses=world->spellLayerClassesSetting();spellOptionsPending=false;} /* declined while work is pending: retried each frame boundary */
        if(memoryWorld>=0&&world)world->setMemoryPressure(memoryWorld==1);
        if(memoryCaps>=0&&NorthlightReplayCopies::enabled.load(std::memory_order_relaxed))NorthlightReplayCopies::setPressure(memoryCaps==1); /* 0.3.192 (CS): replay thread: halves the CPU copies' cap and evicts down to it */
        if(memoryCaps>=0)NorthlightStream::memoryPressure.store(memoryCaps==1,std::memory_order_relaxed); /* 0.3.192 (CS): the stream's game side halves its queue budget at its next Present */
        if(memoryTrim)trimMemory(memorySample);
        if(NorthlightRenderThreadProbe::profiling()&&cpuFrequency.QuadPart>0){namespace P=NorthlightRenderThreadProbe;
            // Windows: the probe's (flushed when it changes), else 600 kept frames. Sample frames are excluded.
            const bool probe=world&&P::probing();const uint64_t window=probe?world->probeWindow():0;
            if(frameCost.count()>=(probe?P::FrameCostWindow::Capacity:600u)||(probe&&window!=frameCostWindow))logFrameCost();
            frameCostWindow=window;frameCostMode=probe?world->probeMode():P::ProbeOff;
            frameCost.close(double(cpuEffects)*1000.0/double(cpuFrequency.QuadPart),P::kindOf(frameApplied,world&&world->lastNearDrawn(),world&&world->lastFarDrawn()),sampledFrame);
            stallFrame={};stallFrame.id=sampleFrame;stallFrame.kind=P::kindOf(frameApplied,world&&world->lastNearDrawn(),world&&world->lastFarDrawn());
            stallFrame.effectsMs=double(cpuEffects)*1000.0/double(cpuFrequency.QuadPart);stallFrame.applied=frameApplied;stallFrame.sampled=sampledFrame;stallFrame.focus=focus;stallScene=gateSceneEnd.QuadPart;
        }
        if(retry){if(world)world->recover();if(water)water->recover();if(failed){releaseResources();failed=false;logf("Effects retry requested after frame cleanup");}}
        if(sampledFrame&&cpuFrequency.QuadPart>0){
            double ms=1000.0/double(cpuFrequency.QuadPart);
            const bool drawTimed=NorthlightRenderThreadProbe::profiling(); // the per-draw scopes (prep, capture, water) need RenderProfile: -1 = not measured
            // 0.3.154: U frames have no per-draw timers; their numbers are on the DRAWGATE ab line.
            if(!gateUntimed)logf("CPU profile frame=%u drawGate=%.3fms capture=%.3fms effects=%.3fms cleanup=%.3fms total=%.3fms enabled=%d gi=%u shadows=%u fog=%u captureSkipped=%u drawHooks=%.3fms frameDrawGates=%u",sampleFrame,drawTimed?std::max(0.0,double(cpuPrep-cpuCapture-cpuEffects)*ms):-1.0,drawTimed?cpuCapture*ms:-1.0,cpuEffects*ms,cleanup*ms,drawTimed?(cpuPrep+cleanup)*ms:-1.0,frameEnabled,unsigned(frameEffects.gi),unsigned(frameEffects.shadows),unsigned(frameEffects.fog),unsigned(world&&world->captureSkippedLastFrame()),
                NorthlightRenderThreadProbe::profiling()?cpuDrawHooks*ms:-1.0,unsigned(frameDrawGates)); /* 0.3.187: drawHooks -1 = not measured (RenderProfile=0) */
            // captureWorld+captureWater=capture. qpcNs: one QueryPerformanceCounter call,
            // so a profile can subtract timer overhead (2 calls per CpuScope).
            LARGE_INTEGER a={},b={},t={};QueryPerformanceCounter(&a);for(unsigned i=0;i<32;++i)QueryPerformanceCounter(&t);QueryPerformanceCounter(&b);
            const double qpcNs=double(b.QuadPart-a.QuadPart)*1e9/double(cpuFrequency.QuadPart)/33.0;
            if(!gateUntimed)logf("CPU capture split frame=%u captureWorld=%.3fms captureWater=%.3fms qpcNs=%.1f",sampleFrame,drawTimed?std::max(0.0,double(cpuCapture-cpuWaterCapture)*ms):-1.0,drawTimed?cpuWaterCapture*ms:-1.0,qpcNs);
            logf("WATER mask draws=%u deferred=%u clears=%u scans=%u readFailures=%u mrt=%u redraw=%u degraded=%u binds=%u longestRun=%u",waterDraws,0u,waterClears,waterScans,waterReadFailures,waterMrt,waterRedraws,waterDegraded,waterBinds,waterLongestRun); /* 0.3.215: binds = render target 1 binds for the water mask, longestRun = the most MRT draws under one bind (both per frame like the rest); 0.3.214 (water MRT): draws = mrt (the game draw wrote the mask itself) + redraw (second draws; degraded = planned as mrt, made as second draws) */
            if(NorthlightRenderThreadProbe::profiling()){
                // Extension D3D9 calls of this frame by method, inside renderEffects' raw scope (all
                // cascades, the point cube, full-screen passes, DiagReplayProbe); capture reads are mirror answers.
                char calls[2048];size_t used=0;unsigned rawTotal=0;calls[0]=0;
                for(unsigned i=0;i<ExtensionDevice::RawMethods;++i){const unsigned n=mirrorState.rawMethodCalls[i];if(!n)continue;rawTotal+=n;
                    const int wrote=std::snprintf(calls+used,sizeof calls-used," %s=%u",ExtensionDevice::rawMethodName(i),n);
                    if(wrote>0)used=std::min(sizeof calls-1,used+size_t(wrote));}
                // 0.3.175 (S3): this frame's effects buckets (ms and the mod's draw calls each); sum == the effects span.
                if(effectsBucketed){const double tick=1000.0/double(cpuFrequency.QuadPart);char buckets[1400];size_t at=0;buckets[0]=0;double sum=0;unsigned draws=0;
                    for(unsigned b=0;b<NorthlightEffectsBuckets::Count;++b){const double bucketMs=double(effectsBuckets.ticks(b))*tick;sum+=bucketMs;draws+=effectsBuckets.drawCalls(b);
                        const int wrote=std::snprintf(buckets+at,sizeof buckets-at," %s=%.3f/%u",NorthlightEffectsBuckets::name(b),bucketMs,effectsBuckets.drawCalls(b));if(wrote>0)at=std::min(sizeof buckets-1,at+size_t(wrote));}
                    logf("EFFECTS buckets frame=%u applied=%d effectsMs=%.3f sumMs=%.3f spanMs=%.3f reads=%u draws=%u (ms/draw calls)%s",sampleFrame,int(frameApplied),double(cpuEffects)*tick,sum,
                        double(effectsBuckets.total())*tick,effectsBuckets.reads(),draws,buckets);}
                // 0.3.200 (jobs): the effects' CPU split of this frame (ReplayJobs): jobMs ran on the job workers, joinWaitMs the renderer thread spent
                // at the joins (jobs it ran itself while waiting included), replayMs the effects' wall time on the renderer thread (effects= above).
                if(world){const auto j=world->takeJobStats();unsigned earlyCulls=0,earlySkipped=0;world->takeCullEarly(earlyCulls,earlySkipped); /* 0.3.217: first-slot cull kicks before the static draws / skipped (bounds not finished), since the last sample */
                    logf("JOBS frame=%u jobs=%u jobMs=%.3f joinWaitMs=%.3f replayMs=%.3f earlyCullKicks=%u earlyCullSkipped=%u",sampleFrame,j.jobs,double(j.workerNs)*1e-6,double(j.waitNs)*1e-6,double(cpuEffects)*ms,earlyCulls,earlySkipped);}
                logf("D3D calls frame=%u mirrorAudit=%u counted=%d total=%u gameDraws=%u mirrorAnswered=%llu mirrorForwarded=%llu captureSkipped=%u probeRan=%u probeMode=%s%s",sampleFrame,unsigned(sampleFrame%120==60),int(mirrorState.rawCounting),rawTotal,drawCalls-frameStartDrawCalls,
                    (unsigned long long)(mirrorState.answered-frameStartAnswered),(unsigned long long)(mirrorState.forwarded-frameStartForwarded),unsigned(world&&world->captureSkippedLastFrame()),
                    unsigned(world&&world->replayProbeRanThisFrame),NorthlightRenderThreadProbe::probeModeName(world?world->probeMode():NorthlightRenderThreadProbe::ProbeOff),calls);
                // Timer overhead of this sample frame's scoped timings (capture phases report their own clockReads).
                // mirrorAudit=1: this 127-frame sample is also the functional mirror audit frame (frame%120==60).
                const unsigned long long reads=CpuScope::reads-frameStartScopeReads;
                // 0.3.150: capture net of its timers: reads inside cpuCapture (CpuScopes and the capture-phase subset) x qpcNs.
                const unsigned long long captureReads=cpuCaptureReads+(world?world->capturePhaseReadsLastFrame():0);
                if(!gateUntimed)logf("CPU timers frame=%u mirrorAudit=%u cpuScopeClockReads=%llu qpcNs=%.1f timerOverheadMs=%.3f captureClockReads=%llu captureNetMs=%.3f",sampleFrame,unsigned(sampleFrame%120==60),reads,qpcNs,double(reads)*qpcNs/1e6,
                    captureReads,drawTimed?std::max(0.0,double(cpuCapture)*ms-double(captureReads)*qpcNs/1e6):-1.0);
                if(gateFrame)logDrawGate(sampleFrame,frameApplied,qpcNs,reads);
            }
        }
        if(!mirrorState.enabled&&!mirrorFallbackReported){mirrorFallbackReported=true;
            logf("MIRROR fallback frame=%u reason=%s; original device queries retained",frame,mirrorState.disableReason?mirrorState.disableReason:"unknown");}
        if(sampleFrame%600==0&&diagnostics()){
            logf("MIRROR frame=%u enabled=%d answered=%llu forwarded=%llu invalidations=%llu checks=%llu mismatches=%llu checkedFields=%llu checkedRegisters=%llu emptyChecks=%llu rawScopes=%llu rawCalls=%llu resources=%zu auditMs=%.3f writeThrough=%llu learnedSlots=%llu distrustedSlots=%llu peeks=%llu proxyShapes=%u",frame,mirrorState.enabled,
                (unsigned long long)mirrorState.answered,(unsigned long long)mirrorState.forwarded,(unsigned long long)mirrorState.invalidations,
                (unsigned long long)mirrorState.checks,(unsigned long long)mirrorState.mismatches,
                (unsigned long long)mirrorState.checkedFields,(unsigned long long)mirrorState.checkedRegisters,(unsigned long long)mirrorState.emptyChecks,
                (unsigned long long)mirrorState.rawScopes,(unsigned long long)mirrorState.rawCalls,mirrorResources.size(),cpuFrequency.QuadPart>0?1000.0*double(cpuMirrorAudit)/double(cpuFrequency.QuadPart):0.0,
                (unsigned long long)mirrorState.writeThrough,(unsigned long long)mirrorState.learnedSlots,(unsigned long long)mirrorState.distrustedSlots,(unsigned long long)mirrorState.peeks,mirrorResources.shapes());
            cpuMirrorAudit=0;
            logGateThreads("periodic");
            reportLogCost();
        }
        weatherFrame(sampleFrame);
        cpuPrep=cpuCapture=cpuWaterCapture=cpuEffects=0;cpuCaptureReads=0;cpuDrawHooks=0;
        ++frame;mirrorState.gate.frame.store(frame,std::memory_order_relaxed); /* 0.3.180: the census' frame */
        if(frame==300)memmap(diagnosticId==1?"baseline-frame300":"frame300"); /* 0.3.192 (MEMMAP): allocations settled; device 1 is the baseline */
        memmapSchedule(); /* task 42: world-entry / memory-deferred / periodic, queued to the sampler thread */
        // Per-type call counts run only through a RenderProfile sample frame.
        mirrorState.rawCounting=sampled()&&NorthlightRenderThreadProbe::profiling();
        if(mirrorState.rawCounting){std::memset(mirrorState.rawMethodCalls,0,sizeof mirrorState.rawMethodCalls);
            frameStartDrawCalls=drawCalls;frameStartAnswered=mirrorState.answered;frameStartForwarded=mirrorState.forwarded;frameStartScopeReads=CpuScope::reads;}
        // 0.3.154: drawGate A/B flags of the next frame; U = top bit of a multiplicative hash of the sample
        // index (parity phase-locks to FarShadowInterval: 16 of 21 far frames would have been U).
        gateFrame=mirrorState.rawCounting;gateUntimed=gateFrame&&(((frame/NorthlightRenderThreadProbe::ProfilePeriod)*2654435761u)>>31);
        mirrorState.gate.counting.store(gateFrame,std::memory_order_relaxed);perfCounting.store(gateFrame,std::memory_order_relaxed);
        if(gateFrame){gateCounts={};gateStart={missingVS,terrainDraws,uiDraws,terrainShadowDraws,NorthlightTrackedBuffers::clock.load(std::memory_order_relaxed),vsCacheHits,vsCacheMisses};
            gateSceneEnd={};gateSceneDraws=0;gateSceneReads=0;for(unsigned s=0;s<MirrorGate::Sites;++s)mirrorState.gate.takeAcquired(MirrorSite(s));perfCalls.store(0,std::memory_order_relaxed);}
        latchDrawGates(); /* 0.3.187: the next frame's draw gates, after every input above */
    }
    HRESULT STDMETHODCALLTYPE EndScene() override { Guard mirrorLock(mirrorState.gate);nameMaskRunEnd();rainMrtUnbind(); /* 0.3.202 (rain mask MRT) */if(spellOn)spellFlush(NorthlightSpellLayer::Reason::Frame,false,NorthlightSpellLayer::InPlaceKind::None,"endscene");return GuardedMirrorDevice::EndScene(); }
    HRESULT STDMETHODCALLTYPE Present(const RECT* src,const RECT* dst,HWND wnd,const RGNDATA* dirty) override { Guard mirrorLock(mirrorState.gate);mirrorState.gate.noteFirst(mirrorState.gate.presentTid);rainMrtUnbind(); /* 0.3.202 (rain mask MRT) */
        PresentTicks ticks;const unsigned markerFrame=frame;finishFrame();ticks.finish();
        if(frameMarkers)frameMarker(1,markerFrame);
        if(limitGpuLatency)waitPreviousFrameGpu();
        const HRESULT hr=ext->Present(src,dst,wnd,dirty);
        if(limitGpuLatency)markFrameGpu();ticks.present();presented(ticks);return hr;
    }
    // After the real Present, under the gate. Arithmetic only; windows are logged by finishFrame.
    void presented(const PresentTicks& t){
        if(t.on)frameCost.presented(t.entry.QuadPart,t.finished.QuadPart,t.done.QuadPart,cpuFrequency.QuadPart,threadLogLines);
        if(t.on){
            stallFrame.queries=mirrorResources.queryProfile;
            if(stallFrames.presented(stallFrame,t.entry.QuadPart,t.finished.QuadPart,t.done.QuadPart,cpuFrequency.QuadPart,stallScene,threadLogLines,streamStats!=nullptr,stallSceneIdle,streamStats?NorthlightStream::get(streamStats->replayIdleNs):0)){
                const auto report=stallFrames.take();
                if(report.slow)logf("FRAME slow window end=%u frames=%u slow=%u retained=%u",stallFrame.id,report.frames,report.slow,report.count);
                for(unsigned i=0;i<report.count;++i){const auto& f=report.worst[i];const auto& q=f.queries;
                    logf("FRAME slow frame=%u kind=%s applied=%u focus=%u sampled=%u logged=%u phases=%u totalMs=%.3f sceneMs=%.3f effectsMs=%.3f tailMs=%.3f finishMs=%.3f presentMs=%.3f queryCalls=%u queryEvent=%u queryOcclusion=%u queryFlush=%u queryPending=%u queryErrors=%u queryWallMs=%.3f queryMaxMs=%.3f querySpans=%u querySpanMs=%.3f sceneQueueIdleMs=%.3f frameQueueIdleMs=%.3f",
                        f.id,NorthlightRenderThreadProbe::kindName(f.kind),unsigned(f.applied),unsigned(f.focus),unsigned(f.sampled),unsigned(f.logged),unsigned(f.phases),f.totalMs,f.sceneMs,f.effectsMs,f.tailMs,f.finishMs,f.presentMs,
                        q.calls,q.events,q.occlusion,q.flush,q.pending,q.errors,double(q.wallNs)*1e-6,double(q.maxCallNs)*1e-6,q.completed,double(q.pendingNs)*1e-6,f.sceneIdleMs,f.frameIdleMs);
                }
            }
        }else stallFrames.reset();
        mirrorResources.queryProfile={};gateSceneEnd={};
        if(t.on)gatePresentDone=t.done.QuadPart; /* 0.3.154: sceneMs start of the next frame */
    }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT index,IDirect3DSwapChain9** out) override { Guard mirrorLock(mirrorState.gate);
        HRESULT hr=ext->GetSwapChain(index,out);
        if(SUCCEEDED(hr)&&out&&*out)*out=new SwapChain(*out,this,&mirrorResources,index==0);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pp,IDirect3DSwapChain9** out) override { Guard mirrorLock(mirrorState.gate);
        HRESULT hr=ext->CreateAdditionalSwapChain(pp,out);
        if(SUCCEEDED(hr)&&out&&*out)*out=new SwapChain(*out,this,&mirrorResources,false);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* rects,DWORD flags,D3DCOLOR color,float z,DWORD stencil) override { Guard mirrorLock(mirrorState.gate);
        nameMaskRunEnd(); /* 0.3.212 (task 32): a run of names never spans a Clear */
        rainMrtUnbind(); /* 0.3.202 (rain mask MRT): a Clear would clear the mask target too */
        /* 0.3.212 (task 32): the game's Clear(Z) of the world depth clears the name marks with it (the stencil flag is added to the same call) while the frame has none; with marks, a stencil clear in a later Clear(Z) is dropped (the game
           does not use stencil) so the marks reach the composite. A game clear that sets the mask bit disables the feature. */
        if((flags&D3DCLEAR_ZBUFFER)&&nameMask.armed()&&maskDsId&&boundDepthIs(maskDsId)){
            if((flags&D3DCLEAR_STENCIL)&&(stencil&NorthlightNameStencil::kBit)){nameMask.disable("the game clears stencil with the mask bit");logf("NAMEMASK disabled: the game clears stencil with the mask bit set");}
            else if(nameMask.hasMarks())flags&=~DWORD(D3DCLEAR_STENCIL);
            else{ /* only a full clear (no rects) with the scissor test off clears all of the stencil; any other clear is left alone and the first mark brings its own */
                DWORD scissor=TRUE;D3DVIEWPORT9 viewport={}; /* Clear is clipped to the viewport: only a full-size viewport makes it the full clear */
                if(n==0&&SUCCEEDED(ext->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor))&&!scissor&&SUCCEEDED(ext->GetViewport(&viewport))&&nameMaskFullViewport(viewport)){if(!(flags&D3DCLEAR_STENCIL)){flags|=D3DCLEAR_STENCIL;stencil=0;}nameMask.noteClear(false);}
            }
        }
        if ((flags&D3DCLEAR_ZBUFFER)&&terrain&&(!captured||earlyDepth.earlyCaptured)&&!applied&&enabled&&!failed) {
            IDirect3DSurface9* ds=nullptr;ext->GetDepthStencilSurface(&ds);
            /* 0.3.188 (task 3): a Clear(Z) after the early resolve freezes it (no later undo would find the cleared depth useful); census position of either */
            if(ds==worldDepth&&(captured?(earlyDepth.freeze(),true):resolveDepth())&&sampled()&&!clearResolveAt)clearResolveAt=censusDraws+1;drop(ds);
        }
        if((flags&D3DCLEAR_STENCIL)&&aoActorOn&&!captured&&aoActorMask->valid()&&boundDepthIs(worldDepth))
            aoActorMask->invalidate(); // Unknown stencil clears cannot supply a depth-matched ownership mask.
        if(spellOn)spellClear(flags); /* task 34 */
        return ext->Clear(n,rects,flags,color,z,stencil);
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT size,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* shared) override { Guard mirrorLock(mirrorState.gate);
        HRESULT hr=ext->CreateVertexBuffer(size,usage,fvf,pool,out,shared);
        if(SUCCEEDED(hr)&&!shared)NorthlightTrackedBuffers::wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(out,this,false,&bufferEscape,&mirrorState.gate);
        if(gateFrame&&SUCCEEDED(hr)&&!shared)++gateCounts.bufferCreates; /* 0.3.154: one generation each */
        if(SUCCEEDED(hr)&&out&&*out&&!NorthlightTrackedBuffers::isWrapped(*out))mirrorState.disable("unwrapped vertex buffer");
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT size,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DIndexBuffer9** out,HANDLE* shared) override { Guard mirrorLock(mirrorState.gate);
        HRESULT hr=ext->CreateIndexBuffer(size,usage,format,pool,out,shared);
        if(SUCCEEDED(hr)&&!shared)NorthlightTrackedBuffers::wrap<IDirect3DIndexBuffer9,ForwardIDirect3DIndexBuffer9>(out,this,true,&bufferEscape,&mirrorState.gate);
        if(gateFrame&&SUCCEEDED(hr)&&!shared)++gateCounts.bufferCreates;
        if(SUCCEEDED(hr)&&out&&*out&&!NorthlightTrackedBuffers::isWrapped(*out))mirrorState.disable("unwrapped index buffer");
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream,IDirect3DVertexBuffer9* buffer,UINT offset,UINT stride) override { Guard mirrorLock(mirrorState.gate);
        bool wrapped=false;auto* raw=NorthlightTrackedBuffers::resolveInput(buffer,wrapped);
        if(buffer&&!wrapped)mirrorState.disable("raw vertex buffer input");
        return ext->SetStreamSource(stream,raw,offset,stride);
    }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT stream,IDirect3DVertexBuffer9** buffer,UINT* offset,UINT* stride) override { Guard mirrorLock(mirrorState.gate);
        HRESULT hr=ext->GetStreamSource(stream,buffer,offset,stride);if(SUCCEEDED(hr)){NorthlightTrackedBuffers::expose(buffer);if(buffer&&*buffer&&!NorthlightTrackedBuffers::isWrapped(*buffer))NorthlightTrackedBuffers::wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(buffer,this,false,&bufferEscape,&mirrorState.gate);if(buffer&&*buffer&&!NorthlightTrackedBuffers::isWrapped(*buffer))mirrorState.disable("vertex buffer exposure");}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* buffer) override { Guard mirrorLock(mirrorState.gate);bool wrapped=false;auto* raw=NorthlightTrackedBuffers::resolveInput(buffer,wrapped);if(buffer&&!wrapped)mirrorState.disable("raw index buffer input");return ext->SetIndices(raw);}
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** buffer) override { Guard mirrorLock(mirrorState.gate);
        HRESULT hr=ext->GetIndices(buffer);if(SUCCEEDED(hr)){NorthlightTrackedBuffers::expose(buffer);if(buffer&&*buffer&&!NorthlightTrackedBuffers::isWrapped(*buffer))NorthlightTrackedBuffers::wrap<IDirect3DIndexBuffer9,ForwardIDirect3DIndexBuffer9>(buffer,this,true,&bufferEscape,&mirrorState.gate);if(buffer&&*buffer&&!NorthlightTrackedBuffers::isWrapped(*buffer))mirrorState.disable("index buffer exposure");}return hr;
    }
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT src,UINT dest,UINT count,IDirect3DVertexBuffer9* buffer,IDirect3DVertexDeclaration9* decl,DWORD flags) override { Guard mirrorLock(mirrorState.gate);
        if(buffer&&!NorthlightTrackedBuffers::isWrapped(buffer))mirrorState.disable("raw ProcessVertices buffer");
        // 0.3.192 (DXVK3): NOOVERWRITE read-backs skip the needsReadback wait of a buffer ProcessVertices wrote; the game is not expected to use it.
        if(NorthlightLockMeter::processVertices()&&NorthlightUpload::ReadBackNoOverwrite.load(std::memory_order_relaxed))logf("LOCK METER warning: ProcessVertices called while the NOOVERWRITE read-back flag is on (first call src=%u dest=%u count=%u)",src,dest,count);
        if(gateFrame)++gateCounts.processVertices; /* 0.3.154: up to two generations */
        NorthlightTrackedBuffers::written(buffer);
        auto* rawBuffer=NorthlightTrackedBuffers::unwrap(buffer);
        if(spellOn&&spell.pending()){extensionWork("spell buffer write",[&]{spell.onBufferGpuWrite(static_cast<void*>(rawBuffer));});spellSync();spellEventDone("gpuwrite",[&](char* buf,size_t n){std::snprintf(buf,n,"buffer=%p",static_cast<void*>(rawBuffer));});} /* task 34: a GPU-side write to a guarded buffer */
        HRESULT hr=ext->ProcessVertices(src,dest,count,rawBuffer,mirrorResources.unwrap(decl),flags);
        mirrorState.invalidate();
        NorthlightTrackedBuffers::written(buffer);return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** out) override { Guard mirrorLock(mirrorState.gate);
        HRESULT hr=ext->CreateVertexShader(code,out);
        if(SUCCEEDED(hr)&&out&&*out)extensionWork("vertex shader registration",[&]{++vsGeneration;std::vector<DWORD> words;auto h=shaderHash(*out,words);int tag=NorthlightShaderTags::deviceTag(h);vsHashes[*out]=h;if(spellOn){shaderReads[static_cast<void*>(*out)]=NorthlightShaderReads::scan(reinterpret_cast<const uint32_t*>(words.data()),words.size());{bool bare=false;const bool ok=NorthlightSpellFog::vsFogProven(reinterpret_cast<const uint32_t*>(words.data()),words.size(),&bare);vsFogProven[*out]=uint8_t(ok?(bare?2:1):0);}}uiFallbackForget(*out);vsMajor[*out]=!words.empty()&&(words[0]>>16)==0xFFFEu?unsigned((words[0]>>8)&0xFFu):0u;if(world){world->registerShader(*out,h);world->registerEquipmentShader(*out,m2ShaderFamily(h)==3?words.data():nullptr,m2ShaderFamily(h)==3?words.size():0);if(particleOn)particleCapture.registerVertex(*out,m2ShaderFamily(h));}if(water)water->registerVertex(*out,h,words.data(),words.size());vsTags[*out]=tag|(water&&water->hasVertex(*out)?kWaterTag:0)|int(m2ShaderFamily(h)<<kFamilyShift);if(tag==1)++matchedTerrain;else if(tag==2)++matchedUI;});
        if(SUCCEEDED(hr))mirrorResources.wrap(out);return hr;
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out) override { Guard mirrorLock(mirrorState.gate);
        HRESULT hr=ext->CreatePixelShader(code,out);
        if(SUCCEEDED(hr)&&out&&*out)extensionWork("pixel shader registration",[&]{std::vector<DWORD> words;auto h=shaderHash(*out,words);uiFallbackForget(*out);psTags[*out]=contains(kUiPS,h)?2:0;psHashes[*out]=h;if(spellOn)shaderReads[static_cast<void*>(*out)]=NorthlightShaderReads::scan(reinterpret_cast<const uint32_t*>(words.data()),words.size());psMajor[*out]=!words.empty()&&(words[0]>>16)==0xFFFFu?unsigned((words[0]>>8)&0xFFu):0u;if(spellOn)psFogReg[*out]=psMajor[*out]==3?NorthlightLegacyFog::ps3FogColorRegister(reinterpret_cast<const uint32_t*>(words.data()),words.size()):-1;if(water)water->registerPixel(*out,h,words.data(),words.size());if(world)world->registerPixelShader(*out);if(particleOn)particleCapture.registerPixel(*out,words.data(),words.size());});
        if(SUCCEEDED(hr))mirrorResources.wrap(out);return hr;
    }
    // 0.3.187: the four draw entry points share drawHook(); each passes its capture and its real draw.
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE t,UINT start,UINT count) override { Guard mirrorLock(mirrorState.gate);
        auto draw=[&]{return ext->DrawPrimitive(t,start,count);};
        if(spellOn||particleOn)spellArgs=NorthlightSpellLayer::DrawArgs::primitive(t,start,count);
        return drawHook(t,count,[&](IDirect3DVertexShader9* vs){world->capture(t,0,0,0,start,count,false,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::NoUserPointer,draw);},draw);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT base,UINT min,UINT vertices,UINT start,UINT count) override { Guard mirrorLock(mirrorState.gate);
        auto draw=[&]{return ext->DrawIndexedPrimitive(t,base,min,vertices,start,count);};
        if(spellOn||particleOn)spellArgs=NorthlightSpellLayer::DrawArgs::indexed(t,base,min,vertices,start,count);
        return drawHook(t,count,[&](IDirect3DVertexShader9* vs){world->capture(t,base,min,vertices,start,count,true,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::NoUserPointer,draw);},draw);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT count,const void* data,UINT stride) override { Guard mirrorLock(mirrorState.gate);
        auto draw=[&]{return ext->DrawPrimitiveUP(t,count,data,stride);};
        if(spellOn||particleOn)spellArgs=NorthlightSpellLayer::DrawArgs::primitiveUP(t,count,data,stride);
        return drawHook(t,count,[&](IDirect3DVertexShader9* vs){world->captureUP(t,0,0,count,nullptr,D3DFMT_UNKNOWN,data,stride,false,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::UserVertices,draw);},draw);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT min,UINT vertices,UINT count,const void* indices,D3DFORMAT fmt,const void* data,UINT stride) override { Guard mirrorLock(mirrorState.gate);
        auto draw=[&]{return ext->DrawIndexedPrimitiveUP(t,min,vertices,count,indices,fmt,data,stride);};
        if(spellOn||particleOn)spellArgs=NorthlightSpellLayer::DrawArgs::indexedUP(t,min,vertices,count,indices,fmt,data,stride);
        return drawHook(t,count,[&](IDirect3DVertexShader9* vs){world->captureUP(t,min,vertices,count,indices,fmt,data,stride,true,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::UserVerticesAndIndices,draw);},draw);}
};

static void finishDeviceFrame(IDirect3DDevice9* owner,bool implicitChain) {static_cast<Device*>(owner)->finishFrame(implicitChain);}
static void presentedDeviceFrame(IDirect3DDevice9* owner,const PresentTicks& ticks) {static_cast<Device*>(owner)->presented(ticks);}

static const char* tclCallerModule(void* address); /* 0.3.204 (task 21): defined below */
class Factory final : public ForwardIDirect3D9 {
    LONG refs=1;
public:
    explicit Factory(IDirect3D9* p):ForwardIDirect3D9(p){}
    ~Factory(){real->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3D9)){*out=this;AddRef();return S_OK;}
        *out=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    // 0.3.192 (CS): wraps the Device in the command stream; on any failure the game keeps the Device directly (the worker core
    // budgets were read with the stream flag set, a harmless one core less).
    IDirect3DDevice9* startStream(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS* pp,unsigned framesAhead,unsigned frameSkip,bool occlusionLocal){
        Device* target=static_cast<Device*>(device);const char* reason="unknown";
        NorthlightStream::StreamDevice::Options options;
        options.framesAhead=framesAhead;options.budget=NorthlightStream::budgetForFramesAhead(framesAhead); /* 0.3.200 (pipeline): StreamFramesAhead, read with CommandStream; the queue budget grows with it (bounded) */
        options.frameSkip=frameSkip; /* 0.3.200 (frame skip): StreamFrameSkip, read with CommandStream */
        options.occlusionLocal=occlusionLocal; /* StreamOcclusionLocal, read with CommandStream */
        options.capture=&NorthlightStream::capture;
        options.threadStart=[target]{target->adoptOwnerThread();};
        options.log=[](const char* line){logf("%s",line);};
        options.queryCaller=[](std::uint64_t frame,void* query,void* caller){
            if(!NorthlightDiagnostics::enabled())return;
            logf("CSTREAM EVENT caller=%s frame=%llu query=%p",tclCallerModule(caller),static_cast<unsigned long long>(frame),query);
            logClientLatency(frame,"event-wait");
        };
        options.diagnostics=&NorthlightDiagnostics::enabled;
        options.readBackLock=&NorthlightUpload::readBackLock;
        options.callerModule=&tclCallerModule; /* 0.3.204 (task 21): who calls TestCooperativeLevel (CSTREAM line, Diagnostics only) */
        options.extension=target->extensionDevice();options.rawOf=[target](IUnknown* exposed,unsigned kind){return target->rawOfExposed(exposed,NorthlightStream::Kind(kind));};
        NorthlightStream::StreamDevice* stream=nullptr;
        const bool exclusiveOwner=target->setExclusiveOwner(true); /* before the replay thread exists: its calls take the cheap owner entry; a foreign call stays safe */
        try{stream=NorthlightStream::StreamDevice::make(device,this,pp,std::move(options),&reason);}catch(...){reason="exception";}
        if(!stream){NorthlightStream::streamActive.store(false,std::memory_order_relaxed);NorthlightReplayCopies::enabled.store(false,std::memory_order_relaxed);target->setExclusiveOwner(false);target->adoptOwnerThread(); /* the gate owner is this thread again (a replay thread that ran has been joined) */
            logf("CSTREAM disabled reason=%s",reason);return device;}
        target->streamStats=&stream->streamCore().q.stats; /* task 45: read-only (relaxed loads) by the CPU budget */
        target->limitGpuLatency=stream->framesAhead()>=2; /* 0.3.200 (pipeline): see Device::limitGpuLatency */
        logf("CSTREAM active gameTid=%lu replayTid=%lu exclusiveGate=%d framesAhead=%u frameSkip=%u occlusionLocal=%d budgetMiB=%u gpuLatencyLimit=%d",NorthlightStream::gameTid.load(),NorthlightStream::replayTid.load(),int(exclusiveOwner),stream->framesAhead(),frameSkip,int(occlusionLocal),unsigned(NorthlightStream::budgetForFramesAhead(framesAhead)>>20),int(target->limitGpuLatency));
        return stream;
    }
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter,D3DDEVTYPE type,HWND window,DWORD flags,D3DPRESENT_PARAMETERS* pp,IDirect3DDevice9** out) override {
        if(selectedBackend!=NorthlightBackend::Kind::Legacy){
            D3DDISPLAYMODE mode={};D3DCAPS9 caps={};D3DADAPTER_IDENTIFIER9 id={};
            real->GetAdapterIdentifier(adapter,0,&id);
            bool ok=SUCCEEDED(real->GetAdapterDisplayMode(adapter,&mode))&&SUCCEEDED(real->GetDeviceCaps(adapter,type,&caps));
            auto format=[&](D3DFORMAT f,DWORD usage,D3DRESOURCETYPE rt){return ok&&real->CheckDeviceFormat(adapter,type,mode.Format,usage,rt,f)==D3D_OK;};
            bool intz=format((D3DFORMAT)MAKEFOURCC('I','N','T','Z'),D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_TEXTURE);
            bool resz=format((D3DFORMAT)MAKEFOURCC('R','E','S','Z'),D3DUSAGE_RENDERTARGET,D3DRTYPE_SURFACE);
            bool floatRT=format(D3DFMT_A16B16G16R16F,D3DUSAGE_RENDERTARGET,D3DRTYPE_TEXTURE)&&format(D3DFMT_R32F,D3DUSAGE_RENDERTARGET,D3DRTYPE_TEXTURE);
            bool sm3=caps.VertexShaderVersion>=D3DVS_VERSION(3,0)&&caps.PixelShaderVersion>=D3DPS_VERSION(3,0);
            logf("Backend capabilities: adapter=%u GPU=%s vendor=%04lx device=%04lx INTZ=%d RESZ=%d floatRT=%d SM3=%d MSAA=%u",adapter,id.Description,id.VendorId,id.DeviceId,intz,resz,floatRT,sm3,pp?unsigned(pp->MultiSampleType):0);
            if(!ok||!intz||!resz||!floatRT||!sm3){
                if(out)*out=nullptr;
                logf("UNSUPPORTED BACKEND: required depth/shader/target features missing; no quality-reducing fallback");
                MessageBoxW(window,L"This graphics driver does not expose the features required by the mod (INTZ, RESZ, SM3, floating-point targets). See northlight-renderer.log. Try the other backend or a supported physical GPU. Virtual GPUs may lack these features.",L"Northlight renderer: unsupported driver",MB_OK|MB_ICONERROR);
                return D3DERR_NOTAVAILABLE;
            }
        }
        unsigned framesAhead=1,frameSkip=0;bool nameMaskWanted=false,occlusionLocal=false,contactAOWanted=false;const bool stream=NorthlightStream::commandStreamRequested(rootPath,&framesAhead,&frameSkip,&nameMaskWanted,&occlusionLocal,&contactAOWanted); /* 0.3.192 (CS): CommandStream=0 or an unreadable ini is the old path below; 0.3.200 (pipeline): StreamFramesAhead from the same read; 0.3.212 (task 32): NameMask too */
        upgradeAutoDepth(real,adapter,type,pp,nameMaskWanted||contactAOWanted,"CreateDevice"); // Shared 24-bit depth + reserved stencil; no added bytes over D24X8.
        if(stream)NorthlightStream::streamActive.store(true,std::memory_order_relaxed); /* before the Device exists: worker core budgets read it once */
        if(stream)NorthlightReplayCopies::enabled.store(true,std::memory_order_relaxed); /* 0.3.192 (CS): before the first game buffer is wrapped: the replay-side CPU copies of replay_copies.h; never on with CommandStream=0 */
        if(stream)flags|=D3DCREATE_MULTITHREADED; /* DXVK's window-proc hook may touch the swap chain on the game thread while the replay thread presents */
        memmapProcess("pre-create",InterlockedCompareExchange(&deviceSerial,0,0)+1); /* task 42 (MEMMAP): what the backend's CreateDevice (and our Device) add shows in the create diff */
        HRESULT hr=real->CreateDevice(adapter,type,window,flags,pp,out);
        logf("CreateDevice HRESULT=0x%08lx flags=0x%lx",(unsigned long)hr,(unsigned long)flags);
        if(SUCCEEDED(hr)&&out&&*out)*out=new Device(*out,this);
        if(SUCCEEDED(hr)&&out&&*out&&stream)*out=startStream(*out,pp,framesAhead,frameSkip,occlusionLocal);
        /* task 45: the game's own thread (the CreateDevice caller) at ABOVE_NORMAL, the replay thread's level, so the replay and its effects no longer
           preempt it for the cores (Wine on macOS maps thread priorities to Mach thread policies). Only the scheduling of this thread changes. */
        if(SUCCEEDED(hr)){const BOOL raised=SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);logf("GAME thread priority ABOVE_NORMAL %s (tid=%lu priority=%d)",raised?"set":"failed",(unsigned long)GetCurrentThreadId(),GetThreadPriority(GetCurrentThread()));}
        return hr;
    }
};

static void configureDxvkCompatibility(const NorthlightBackendLoader::Inspection& info){
    // Do not edit dxvk.conf or global launcher/Wine settings. Only this client
    // process receives the required D3D9 compatibility options. Explicit
    // DXVK_CONFIG assignments remain last and can override them for diagnosis.
    static bool applied=false;if(applied)return;applied=true;
    try {
        const DWORD needed=GetEnvironmentVariableA("DXVK_CONFIG",nullptr,0);
        std::string existing;
        if(needed){
            std::vector<char> buffer(size_t(needed)+1);
            const DWORD copied=GetEnvironmentVariableA("DXVK_CONFIG",buffer.data(),DWORD(buffer.size()));
            if(copied>=buffer.size()){logf("DXVK compatibility configuration changed concurrently; defaults not applied");return;}
            existing.assign(buffer.data(),copied);
        }
        const auto combined=NorthlightDxvkCompatibility::config(existing);
        const BOOL set=SetEnvironmentVariableA("DXVK_CONFIG",combined.c_str());
        // DXVK 1.10.x reads only dxvk.conf/DXVK_CONFIG_FILE: the variable is then inert.
        // 0.3.189: DXVK 2.x reads cachedDynamicBuffers, >=3.0 cachedWriteOnlyBuffers; each ignores the other's key.
        logf("DXVK compatibility process defaults applied=%u vendor=1002 cachedDynamicBuffers(2.x)=True cachedWriteOnlyBuffers(>=3.0)=True explicitInlineOverrides=%u dxvk=%s DXVK_CONFIG supported=%u",
             unsigned(set!=FALSE),unsigned(!existing.empty()),info.dxvkVersion.empty()?"unknown":info.dxvkVersion.c_str(),unsigned(info.dxvkConfigEnv));
    }catch(...){logf("DXVK compatibility defaults unavailable: allocation failure");}
}
// Win32 adapter for NorthlightBackendLoader. Reads files and loads libraries only.
struct Win32BackendSys {
    using Module=HMODULE;
    NorthlightBackend::Kind kind;std::wstring selfPath;BY_HANDLE_FILE_INFORMATION selfId={};bool haveSelfId=false;
    static bool fileId(const std::wstring& path,BY_HANDLE_FILE_INFORMATION& info,unsigned long& error){
        HANDLE h=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE){error=GetLastError();return false;}
        const bool ok=GetFileInformationByHandle(h,&info)!=FALSE;if(!ok)error=GetLastError();CloseHandle(h);
        return ok&&!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
    }
    explicit Win32BackendSys(NorthlightBackend::Kind k):kind(k){
        wchar_t path[MAX_PATH*2]={};const DWORD n=GetModuleFileNameW(selfModule,path,MAX_PATH*2);
        if(n&&n<MAX_PATH*2)selfPath=path;
        unsigned long error=0;haveSelfId=!selfPath.empty()&&fileId(selfPath,selfId,error);
    }
    const std::wstring& self(){return selfPath;}
    std::wstring full(const std::wstring& path){
        wchar_t out[MAX_PATH*2]={};const DWORD n=GetFullPathNameW(path.c_str(),MAX_PATH*2,out,nullptr);
        return n&&n<MAX_PATH*2?std::wstring(out):std::wstring();
    }
    int identity(const std::wstring& path,unsigned long& error){
        BY_HANDLE_FILE_INFORMATION info={};if(!fileId(path,info,error))return -1;
        return haveSelfId&&info.dwVolumeSerialNumber==selfId.dwVolumeSerialNumber&&info.nFileIndexHigh==selfId.nFileIndexHigh&&info.nFileIndexLow==selfId.nFileIndexLow;
    }
    bool read(const std::wstring& path,std::vector<unsigned char>& bytes){
        HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE)return false;
        LARGE_INTEGER size={};DWORD got=0;
        bool ok=GetFileSizeEx(h,&size)&&size.QuadPart>0&&size.QuadPart<=(64ll<<20);
        if(ok){try{bytes.resize(size_t(size.QuadPart));}catch(...){ok=false;}}
        ok=ok&&ReadFile(h,bytes.data(),DWORD(bytes.size()),&got,nullptr)&&got==bytes.size();
        CloseHandle(h);return ok;
    }
    void beforeLoad(const std::wstring&,const NorthlightBackendLoader::Inspection& info){
        // 0.3.175: DXVK_ASYNC is left to the runtime (WoWSilicon enables it). Async forks may skip a
        // draw while a pipeline compiles; the user test showed far fewer hitches, so it is not forced off.
        // Only Backend=dxvk/dxvk2 (the Windows package's DXVK: 3.1.1 default, 2.7.1 as dxvk2) gets the
        // vendor/buffer defaults; legacy (the macOS WoWSilicon DXVK) keeps today's runtime unchanged.
        if(NorthlightBackend::isPackagedDxvk(kind))configureDxvkCompatibility(info);
    }
    HMODULE load(const std::wstring& path,unsigned long& error){HMODULE m=LoadLibraryW(path.c_str());error=m?0:GetLastError();return m;}
    bool isSelf(HMODULE m){return m==selfModule;}
    void* proc(HMODULE m,const char* name){FARPROC f=GetProcAddress(m,name);void* p=nullptr;static_assert(sizeof(p)==sizeof(f));std::memcpy(&p,&f,sizeof p);return p;}
    bool ownedBySelf(void* address){
        HMODULE owner=nullptr;
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCWSTR>(address),&owner)&&owner==selfModule;
    }
    void release(HMODULE m){FreeLibrary(m);}
};
// 0.3.204 (task 21): "module+0xoffset" of a TestCooperativeLevel caller's return address (a diagnostic line, a few calls in a session; game thread only).
static const char* tclCallerModule(void* address){
    static char text[300];
    HMODULE m=nullptr;
    if(!address||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCWSTR>(address),&m)){
        std::snprintf(text,sizeof text,"(no module)+0x%llx",(unsigned long long)reinterpret_cast<std::uintptr_t>(address));return text;}
    wchar_t path[MAX_PATH]={};const DWORD len=GetModuleFileNameW(m,path,MAX_PATH);const wchar_t* base=path;
    for(const wchar_t* c=path;*c;++c)if(*c==L'\\'||*c==L'/')base=c+1;
    std::snprintf(text,sizeof text,"%ls+0x%llx",len?base:L"(unknown)",(unsigned long long)(reinterpret_cast<std::uintptr_t>(address)-reinterpret_cast<std::uintptr_t>(m)));
    return text;
}
static std::wstring systemDirectory(){
    wchar_t dir[MAX_PATH]={};const UINT n=GetSystemDirectoryW(dir,MAX_PATH);
    return n&&n<MAX_PATH-10?std::wstring(dir):std::wstring();
}
static std::wstring systemD3d9(){return NorthlightBackend::defaultPath(NorthlightBackend::Kind::Native,L"",systemDirectory());}
static void logAttempts(const std::vector<NorthlightBackendLoader::Attempt>& attempts){
    for(const auto& a:attempts)
        logf("BACKEND candidate=%ls result=%s error=%lu ours=%u dxvk=%s DXVK_CONFIG=%u scanned=%u",a.path.c_str(),NorthlightBackendLoader::name(a.outcome),
             a.error,unsigned(a.info.ours),a.info.dxvk?(a.info.dxvkVersion.empty()?"yes":a.info.dxvkVersion.c_str()):"no",unsigned(a.info.dxvkConfigEnv),unsigned(a.info.read));
}
// Host exe path only: nothing depends on which wow.exe runs (not read, not hashed).
static void logHostExecutable(const std::wstring& selfPath){
    wchar_t exe[MAX_PATH*2]={};const DWORD n=GetModuleFileNameW(nullptr,exe,MAX_PATH*2);
    logf("HOST exe=%ls module=%ls; executable is never modified",n&&n<MAX_PATH*2?exe:L"(unknown)",selfPath.c_str());
}
static std::string fileSha(const std::wstring& path){
    std::vector<unsigned char> bytes;Win32BackendSys sys(NorthlightBackend::Kind::Invalid);
    if(!sys.read(path,bytes))return "unreadable";
    NorthlightBackendLoader::Sha256 h;h.update(bytes.data(),bytes.size());return h.hex();
}
// Where this proxy sits and how the game reached it; the game-folder d3d9.dll
// (WoWSilicon's DXVK on macOS) against the backend copy that is actually used.
static void logProxyPlacement(Win32BackendSys& sys,const std::wstring& root,const NorthlightBackendLoader::Attempt& backendAttempt,bool loaded){
    const auto where=NorthlightBackend::location(sys.selfPath,root);
    std::vector<unsigned char> dlls;const bool listed=sys.read(root+L"dlls.txt",dlls)&&NorthlightBackend::dllsListed(std::string(dlls.begin(),dlls.end()));
    logf("PROXY module=%ls root=%ls (host exe directory) location=%s dlls.txt entry mods/d3d9.dll=%u",sys.selfPath.c_str(),root.c_str(),NorthlightBackend::name(where),unsigned(listed));
    if(where==NorthlightBackend::Location::Other)logf("PROXY WARNING: this module is neither <game>\\d3d9.dll nor <game>\\mods\\d3d9.dll; config, logs and world-cache are read from the host exe directory");
    const std::wstring game=root+L"d3d9.dll";unsigned long error=0;const int id=sys.identity(game,error);
    if(id<0){logf("GAME d3d9.dll absent (error=%lu)",error);return;}
    if(id>0){logf("GAME d3d9.dll = this proxy");return;}
    const std::string gameSha=fileSha(game);
    logf("GAME d3d9.dll sha256=%s backend sha256=%s match=%u",gameSha.c_str(),loaded?backendAttempt.info.sha256.c_str():"(none)",unsigned(loaded&&gameSha==backendAttempt.info.sha256));
    if(loaded&&gameSha!=backendAttempt.info.sha256)
        logf("GAME WARNING: the game-folder d3d9.dll differs from the backend copy (launcher update or backend switch?). The renderer keeps using %ls.",backendAttempt.path.c_str());
}
static HMODULE backend() {
    static HMODULE cached=[]() -> HMODULE {
    ensureRootPath();
    wchar_t config[MAX_PATH]={},value[64]={},custom[MAX_PATH]={};
    swprintf(config,MAX_PATH,L"%lsnorthlight-renderer.ini",rootPath);
    DWORD attrs=GetFileAttributesW(config),configError=GetLastError();
    bool exists=attrs!=INVALID_FILE_ATTRIBUTES;
    GetPrivateProfileStringW(L"Renderer",L"Backend",L"",value,64,config);
    GetPrivateProfileStringW(L"Renderer",L"BackendPath",L"",custom,MAX_PATH,config);
    selectedBackend=NorthlightBackend::parse(value,exists);
    if(!exists&&configError!=ERROR_FILE_NOT_FOUND&&configError!=ERROR_PATH_NOT_FOUND)
        selectedBackend=NorthlightBackend::Kind::Invalid;
    const auto configured=selectedBackend;
    Win32BackendSys sys(configured);
    const std::wstring root(rootPath),system=systemD3d9(),overridden=NorthlightBackend::overridePath(custom,root);
    const auto candidates=NorthlightBackend::candidates(configured,overridden,root,systemDirectory());
    NorthlightBackendLoader::Result<HMODULE> result=NorthlightBackendLoader::load(sys,candidates,system);
    HMODULE module=result.module;
    memmapProcess("backend-loaded",0); /* task 42 (MEMMAP): right after the backend DLL load, before Direct3DCreate9 and any backend CreateDevice; diffs against pre-create/create */
    const auto& last=result.attempts.empty()?NorthlightBackendLoader::Attempt{}:result.attempts.back();
    // Only DXVK keeps the legacy (unchecked, no RESZ dummy draw) rules; every
    // other runtime, including the system fallback, gets the native rules.
    if(module&&(result.fallback||(configured==NorthlightBackend::Kind::Legacy&&!last.info.dxvk)))selectedBackend=NorthlightBackend::Kind::Native;
    logf("Northlight renderer 0.3.216; reference sun look (sun glow hue from native/sunHalo band, soft-shoulder glare, veil, sun-tinted haze), native sun/moon suppressed (F1b), lamps dimmed to 30 pct in direct sun, native moon02 skipped by texture identity, no game bytes in the DLL, MEMREAD self-read profile (RenderProfile), soft sun removal in shadow, jump-stable shadow anchor, geometry coverage hold with travel lead, steadier animated shadow edges (near 5x5 tent, still-camera shadow history), native blob shadows kept at BlobShadowStrength (faint texture under modulate blend), bilinear lighting history, near capture reserve for the player and companions, remembered rigid prop shadows (drawn-by-game states, windowed held), AO and bloom folded into the world composite, ground normals reject object tops, both wide samples, batched celestial terrain mask, DXVK async left to the runtime, render-thread terrain upload and rigid bookkeeping trims, moon without the horizon stall, art layer bands retimed to the sun and moon, actor prepare on a worker, trimmed prepare handoff, in-place capture constants, gate thread census, predicted snapshot lookups, word-wise memcmp, owner-thread gate elision; abandoned-frame prepare quarantine; removal smoothing on matching normals in its own pass (35/50 degree gate); per-frame draw gates; translucent depth census; early depth for translucent actors; DXVK 3.1.1 default, dxvk2 (2.7.1) by choice only, no automatic fallback; AO depth texel snap; shadow cascades follow camera zoom and collision; reduced terrain shadow reach under address-space pressure; command-stream replay thread; draw-hook lookup caches; lighter replay retire; fresh texture shadows evict only stale keeps; soft local-light cap with fades; blended GI re-publication; Forever-style rain (storm light bands, weather draw detection); moving fog clouds; GPU budget control; frames ahead and frame skipping in the command stream; replay jobs; AO denoise pass; rain mask (MRT, depth scrub), effect boundary at the UI in rain; UI boundary fallback for font mods; TestCooperativeLevel answered on the game thread; two large buffer shadows (36 MiB allowance); lamp fog glow capped once over all lamps; texture readback diagnostics, texture readbacks without a queue drain; sky light held across the weather rescale; colour-guided lighting upsample and colour-picked contact normals at wall and actor feet; actor shadow map with soft contact-hardening filter; uncached maps skip geometry builds; nearest-first actor capture (radius gate, distance cut), stride-aligned replay bulk; floating names keep the game's colour (stencil mask); instance map names with an apostrophe or a space reach the world context; lamps in view first, screen-coherent lamp batches (16-lamp passes); backend=%s path=%ls loaded=%d error=%lu",
         NorthlightBackend::name(configured),last.path.c_str(),module!=nullptr,module?0ul:(last.error?last.error:(unsigned long)ERROR_INVALID_PARAMETER));
    logAttempts(result.attempts);
    logHostExecutable(sys.selfPath);
    logf("BACKEND selected=%ls runtime=%s rules=%s BackendPath=%ls fallback=%u",module?last.path.c_str():L"(none)",
         last.info.dxvk?(last.info.dxvkVersion.empty()?"DXVK (unknown version)":last.info.dxvkVersion.c_str()):"non-DXVK",NorthlightBackend::name(selectedBackend),overridden.empty()?L"(default)":overridden.c_str(),unsigned(result.fallback));
    // 0.3.192 (DXVK3): the NOOVERWRITE read-back flag only for the backend that actually loaded and is DXVK >= 3 (see upload_lock.h).
    NorthlightUpload::ReadBackNoOverwrite.store(module&&!result.fallback&&last.info.dxvk&&NorthlightBackend::dxvkMajor(last.info.dxvkVersion)>=3,std::memory_order_relaxed);
    logf("LOCK METER read-back lock flags readBackLock=0x%x (NOOVERWRITE only on a loaded DXVK >= 3 backend)",unsigned(NorthlightUpload::readBackLock()));
    if(result.fallback&&module)logf("BACKEND SELF-LOAD REFUSED: the configured backend resolves to this proxy (or another Northlight build); fell back to the system d3d9 runtime. Fix northlight-renderer.ini Backend/BackendPath.");
    else if(result.fallback)logf("BACKEND SELF-LOAD REFUSED: the configured backend resolves to this proxy (or another Northlight build) and the system d3d9 fallback FAILED too (expected under Wine with d3d9=n: the system d3d9 is builtin; the fallback is Windows-only). Fix northlight-renderer.ini Backend/BackendPath.");
    if(module&&last.info.dxvk&&!NorthlightBackend::isPackagedDxvk(configured))
        logf("DXVK compatibility defaults not applied: Backend=%s keeps the current runtime rules (no vendor 1002, no cachedDynamicBuffers/cachedWriteOnlyBuffers override)",NorthlightBackend::name(configured));
    logProxyPlacement(sys,root,last,module!=nullptr);
    logf("LOG previous session log northlight-renderer.prev.log rotation=%s error=%lu",NorthlightLogRotation::name(logRotation),(unsigned long)logRotationError);
    if(!module)MessageBoxW(nullptr,
        L"Renderer backend could not be loaded. Check northlight-renderer.ini and northlight-renderer.log. No other backend was selected. Restore the previous package or install the correct x86 DXVK DLL.",
        L"Northlight renderer",MB_OK|MB_ICONERROR);
    return module;
    }();
    return cached;
}
// A backend proxy that resolved "d3d9.dll" by name calls back into this
// module. Never re-enter backend(): forward the inner call to the system runtime.
static HMODULE recursionBackend() {
    static HMODULE cached=[]() -> HMODULE {
        Win32BackendSys sys(NorthlightBackend::Kind::Native);NorthlightBackendLoader::Attempt a;
        HMODULE module=NorthlightBackendLoader::attempt(sys,systemD3d9(),a);
        logf("BACKEND RECURSION: the backend called back into this d3d9.dll; inner calls are forwarded unwrapped to %ls result=%s error=%lu",a.path.c_str(),NorthlightBackendLoader::name(a.outcome),a.error);
        return module;
    }();
    return cached;
}
template<class T> static T procedure(const char* name) {
    HMODULE module=NorthlightBackendLoader::ExportScope::reentered()?recursionBackend():backend();
    FARPROC raw=module?GetProcAddress(module,name):nullptr;
    T fn=nullptr;static_assert(sizeof(fn)==sizeof(raw));std::memcpy(&fn,&raw,sizeof(fn));return fn;
}
#define NORTHLIGHT_EXPORT NorthlightBackendLoader::ExportScope exportScope
extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdk) {
    NORTHLIGHT_EXPORT;
    auto fn=procedure<IDirect3D9*(WINAPI*)(UINT)>("Direct3DCreate9");
    if(!fn)return nullptr;
    // The outer (game) call wraps once; a re-entered call returns the raw runtime.
    IDirect3D9* p=fn(sdk);
    return p&&!NorthlightBackendLoader::ExportScope::reentered()?new Factory(p):p;
}
extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT sdk,IDirect3D9Ex** out) {
    NORTHLIGHT_EXPORT;
    auto fn=procedure<HRESULT(WINAPI*)(UINT,IDirect3D9Ex**)>("Direct3DCreate9Ex");
    logf("Direct3D9Ex requested: forwarding without effects (use this client's normal D3D9 path).");
    if(!fn)return D3DERR_NOTAVAILABLE;
    return fn(sdk,out);
}
extern "C" int WINAPI D3DPERF_BeginEvent(D3DCOLOR c,LPCWSTR text){NORTHLIGHT_EXPORT;countPerf();auto fn=procedure<int(WINAPI*)(D3DCOLOR,LPCWSTR)>("D3DPERF_BeginEvent");return fn?fn(c,text):-1;}
extern "C" int WINAPI D3DPERF_EndEvent(){NORTHLIGHT_EXPORT;countPerf();auto fn=procedure<int(WINAPI*)()>("D3DPERF_EndEvent");return fn?fn():-1;}
extern "C" DWORD WINAPI D3DPERF_GetStatus(){NORTHLIGHT_EXPORT;countPerf();auto fn=procedure<DWORD(WINAPI*)()>("D3DPERF_GetStatus");return fn?fn():0;}
extern "C" BOOL WINAPI D3DPERF_QueryRepeatFrame(){NORTHLIGHT_EXPORT;countPerf();auto fn=procedure<BOOL(WINAPI*)()>("D3DPERF_QueryRepeatFrame");return fn?fn():FALSE;}
extern "C" void WINAPI D3DPERF_SetMarker(D3DCOLOR c,LPCWSTR text){NORTHLIGHT_EXPORT;countPerf();auto fn=procedure<void(WINAPI*)(D3DCOLOR,LPCWSTR)>("D3DPERF_SetMarker");if(fn)fn(c,text);}
extern "C" void WINAPI D3DPERF_SetOptions(DWORD options){NORTHLIGHT_EXPORT;countPerf();auto fn=procedure<void(WINAPI*)(DWORD)>("D3DPERF_SetOptions");if(fn)fn(options);}
extern "C" void WINAPI D3DPERF_SetRegion(D3DCOLOR c,LPCWSTR text){NORTHLIGHT_EXPORT;countPerf();auto fn=procedure<void(WINAPI*)(D3DCOLOR,LPCWSTR)>("D3DPERF_SetRegion");if(fn)fn(c,text);}
// Remaining named d3d9.dll exports (same ordinals as the system DLL and DXVK), so
// other modules that bind to the game-folder d3d9.dll by name still resolve.
// Unwrapped pass-through; 9On12 is not this client's rendering path.
extern "C" IDirect3D9* WINAPI Direct3DCreate9On12(UINT sdk,void* args,UINT count){NORTHLIGHT_EXPORT;auto fn=procedure<IDirect3D9*(WINAPI*)(UINT,void*,UINT)>("Direct3DCreate9On12");return fn?fn(sdk,args,count):nullptr;}
extern "C" HRESULT WINAPI Direct3DCreate9On12Ex(UINT sdk,void* args,UINT count,IDirect3D9Ex** out){NORTHLIGHT_EXPORT;auto fn=procedure<HRESULT(WINAPI*)(UINT,void*,UINT,IDirect3D9Ex**)>("Direct3DCreate9On12Ex");return fn?fn(sdk,args,count,out):D3DERR_NOTAVAILABLE;}
extern "C" void* WINAPI Direct3DShaderValidatorCreate9(){NORTHLIGHT_EXPORT;auto fn=procedure<void*(WINAPI*)()>("Direct3DShaderValidatorCreate9");return fn?fn():nullptr;}
extern "C" int WINAPI Direct3D9EnableMaximizedWindowedModeShim(UINT value){NORTHLIGHT_EXPORT;auto fn=procedure<int(WINAPI*)(UINT)>("Direct3D9EnableMaximizedWindowedModeShim");return fn?fn(value):0;}
extern "C" int WINAPI DebugSetLevel(){NORTHLIGHT_EXPORT;auto fn=procedure<int(WINAPI*)()>("DebugSetLevel");return fn?fn():0;}
extern "C" void WINAPI DebugSetMute(){NORTHLIGHT_EXPORT;auto fn=procedure<void(WINAPI*)()>("DebugSetMute");if(fn)fn();}
extern "C" void WINAPI PSGPError(void* data,UINT id,UINT value){NORTHLIGHT_EXPORT;auto fn=procedure<void(WINAPI*)(void*,UINT,UINT)>("PSGPError");if(fn)fn(data,id,value);}
extern "C" void WINAPI PSGPSampleTexture(void* data,UINT stage,void* coords,UINT count,void* result){NORTHLIGHT_EXPORT;auto fn=procedure<void(WINAPI*)(void*,UINT,void*,UINT,void*)>("PSGPSampleTexture");if(fn)fn(data,stage,coords,count,result);}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){
        // Loader lock (and, on macOS, DivxDecoder's dlls.txt preload): record the handle only.
        selfModule=instance;DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
