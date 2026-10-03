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
#include "mirror_guarded_device.h"
#include "mirror_resources.h"
#include "tracked_buffers.h"
#include "signatures.h"
#include "compiled_shaders.h"
#include "projection.h"
#include "world_draw_domain.h"
#include "extension_guard.h"
#include "backend_policy.h"
#include "dxvk_compatibility.h"
#include "backend_loader.h"
#include "frame_intervals.h"
#include "effect_switches.h"
#include "diagnostics_switch.h"
#include "render_thread_probe.h"
#include "effects_buckets.h"
#include "draw_gates.h"
#include "translucent_depth.h"
#include "log_rotation.h"
#include "memory_guard.h"
#include "northlight_mem.h"

// 0.3.181 (r90): the DLL's one memcmp. This strong definition overrides zig compiler_rt's weak byte
// loop for every call site in all four TUs (libc++ included) and returns the same value for every
// input (northlight_mem.h). no_builtin: its body must not turn back into a memcmp call. Not exported.
extern "C" __attribute__((no_builtin("memcmp"),no_builtin("bcmp"))) int memcmp(const void* a,const void* b,size_t n){return NorthlightMem::compare(a,b,n);}

// Temporarily disable the water appearance passes.
// Keep the liquid mask: AO/GI need it to avoid treating water as solid terrain.
static constexpr bool kWaterEffectsEnabled = false;
static HMODULE selfModule;
static wchar_t rootPath[MAX_PATH];
// Game folder = directory of the host exe, resolved on first use (never in
// DllMain: the macOS proxy is preloaded under the loader lock from dlls.txt).
// backend() calls it first; every export reaches backend() before it logs.
static void ensureRootPath(){
    static const bool resolved=[]{
        wchar_t exe[MAX_PATH]={},self[MAX_PATH]={};
        const DWORD n=GetModuleFileNameW(nullptr,exe,MAX_PATH),m=GetModuleFileNameW(selfModule,self,MAX_PATH);
        const std::wstring root=NorthlightBackend::gameRoot(n&&n<MAX_PATH?exe:L"",m&&m<MAX_PATH?self:L"");
        if(root.size()<MAX_PATH)wcscpy(rootPath,root.c_str());
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
        sample.valid=true;
    }
    sample.tick=GetTickCount();
    sample.wallNanoseconds=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started).count());
    return sample;
}
template<class T> static void drop(T*& p) { if (p) { p->Release(); p = nullptr; } }
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
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(words.data());
    uint64_t h = 14695981039346656037ULL;
    for (UINT i = 0; i < size; ++i) h = (h ^ bytes[i]) * 1099511628211ULL;
    return h;
}

#include "saved_state.h"

#include "gpu_profile.h"
#include "water_renderer.h"
#include "world_renderer.h"
#include "celestial_disc_renderer.h"
#include "shadow_blob_filter.h"
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
static void finishDeviceFrame(IDirect3DDevice9* owner);
static void presentedDeviceFrame(IDirect3DDevice9* owner,const PresentTicks& ticks);
class SwapChain final : public ForwardIDirect3DSwapChain9 {
    LONG refs=1;
    IDirect3DDevice9* owner;
    NorthlightMirrorResources::Registry* resources;
public:
    SwapChain(IDirect3DSwapChain9* r,IDirect3DDevice9* d,NorthlightMirrorResources::Registry* registry):ForwardIDirect3DSwapChain9(r),owner(d),resources(registry){owner->AddRef();}
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
        MirrorGuard lock(resources->gate(),MirrorSite::SwapChain);resources->gate().noteFirst(resources->gate().swapPresentTid);PresentTicks ticks;finishDeviceFrame(owner);ticks.finish();
        const HRESULT hr=real->Present(src,dst,window,dirty,flags);ticks.present();presentedDeviceFrame(owner,ticks);return hr;
    }
};

class Device final : public GuardedMirrorDevice {
    LONG refs = 1;
    LONG diagnosticId=0;
    NorthlightFrameIntervals::Window frameIntervals;
    bool extensionFault=false;
    IDirect3D9* parent;
    DeviceMirror mirrorState;
    NorthlightMirrorResources::Registry mirrorResources;
    ExtensionDevice* ext;
    NorthlightStateBlockPool stateBlocks;
    static void mirrorEscape(void* context,const char* reason)noexcept{static_cast<DeviceMirror*>(context)->disable(reason);}
    static void bufferEscape(IDirect3DDevice9* owner,const char* reason)noexcept{static_cast<Device*>(owner)->mirrorState.disable(reason);}
    // 0.3.180 (D0, r88 §3.2): the gate's thread census. Relaxed reads of the gate's atomics only, so any
    // thread may log it: periodic (600 frames), at destroy, and once at the first foreign entry of a class.
    static void gateForeignReport(void* context,unsigned,std::uint32_t)noexcept{static_cast<Device*>(context)->logGateThreads("first-foreign");}
    void logGateThreads(const char* event)noexcept{
        const MirrorGate& g=mirrorState.gate;const bool first=g.firstReady.load(std::memory_order_acquire);
        auto foreign=[&](MirrorSite s){return unsigned(g.foreign[unsigned(s)].load(std::memory_order_relaxed));};
        auto tid=[](const std::atomic<std::uint32_t>& t){return (unsigned long)t.load(std::memory_order_relaxed);};
        if(NorthlightDiagnostics::enabled())logf("GATE threads device=%ld owner=%lu presentTid=%lu swapPresentTid=%lu drawTid=%lu foreignDevice=%u foreignRegistry=%u foreignResource=%u foreignStateBlock=%u foreignSwapChain=%u foreignRaw=%u foreignBuffer=%u ownerLocked=%u first=%lu/%s/%u frame=%u event=%s",
            diagnosticId,(unsigned long)g.ownerTid,tid(g.presentTid),tid(g.swapPresentTid),tid(g.drawTid),foreign(MirrorSite::Device),foreign(MirrorSite::Registry),foreign(MirrorSite::Resource),
            foreign(MirrorSite::StateBlock),foreign(MirrorSite::SwapChain),foreign(MirrorSite::Raw),foreign(MirrorSite::Buffer),unsigned(g.ownerLocked.load(std::memory_order_relaxed)),
            first?(unsigned long)g.firstTid:0ul,mirrorSiteName(first?g.firstSite:MirrorGate::Sites),first?unsigned(g.firstFrame):0u,unsigned(g.frame.load(std::memory_order_relaxed)),event);
    }

    std::unique_ptr<WorldRenderer> world;
    std::unique_ptr<NorthlightCelestialDiscRenderer> celestialDiscs;
    std::unique_ptr<NorthlightShadowBlobFilter> shadowBlobs;
    std::unique_ptr<NorthlightWaterRenderer> water;
    std::unique_ptr<NorthlightGpuProfile> gpuProfile;
    std::unique_ptr<NorthlightMemoryDiagnostics::Sampler> memoryDiagnostics; /* also feeds the functional memory guard */
    NorthlightMemoryGuard::Guard memoryGuard;
    IDirect3DTexture9 *scene = nullptr, *depthTex = nullptr, *ao = nullptr;
    IDirect3DSurface9 *sceneSurface = nullptr, *aoSurface = nullptr, *worldDepth = nullptr;
    IDirect3DPixelShader9 *aoPS = nullptr, *aoContactBloomPS = nullptr, *compositePS = nullptr;
    // Low bits: 1 terrain, 2 UI. kWaterTag: the water renderer holds a mask shader
    // for it (set at registration, where the water map changes), so non-water
    // draws need no second hash lookup.
    static constexpr int kTagMask=3,kWaterTag=4;
    std::unordered_map<IDirect3DVertexShader9*, int> vsTags;
    bool drawWaterVS=false; // beforeDraw() result for the current draw
    std::unordered_map<IDirect3DPixelShader9*, int> psTags;
    std::unordered_map<IDirect3DVertexShader9*, uint64_t> vsHashes;
    std::unordered_map<IDirect3DPixelShader9*, uint64_t> psHashes;
    unsigned blobSignatureReports=0;
    NorthlightMirrorAuditSchedule mirrorAuditSchedule;bool mirrorFallbackReported=false;
    UINT width = 0, height = 0;
    D3DFORMAT sceneFormat = D3DFMT_UNKNOWN;
    bool terrain = false, captured = false, applied = false, enabled = true;
    bool failed = false, projectionValid = false, key10 = false, key12=false;
    unsigned frame = 0, appliedFrames = 0, matchedTerrain = 0, matchedUI = 0, projectionRejects = 0;
    unsigned worldSkippedFrames=0;
    // 0.3.169: one line per run of skipped world frames, with the first and last skipReason().
    unsigned worldSkipRun=0,worldSkipEpisodes=0;DWORD worldSkipStart=0;const char* worldSkipFirst="";const char* worldSkipLast="";
    unsigned nonWorldCaptureRejects=0;
    unsigned drawCalls=0, missingVS=0, terrainDraws=0, terrainShadowDraws=0, viewportRejects=0, depthRejects=0, uiDraws=0, uiAfterTerrain=0;
    unsigned viewportReports=0;
    unsigned postEffectWorldDraws=0,postEffectSkinnedDraws=0;
    // 0.3.188 (task 3): per-sample-frame census of translucent Z-writing world draws (the ghost/stealth silhouette hypothesis); read-only, logged as TRANSLUCENT.
    // Positions are 1-based census draw indices (0 = none): the resolve can move to the first translucent
    // Z-writing draw only if lastOpaqueZ precedes it (firstTranslucentZSkinned: the same test for a trigger
    // limited to skinned draws); clearResolve marks a Clear(Z)-triggered resolve. Positions run over the
    // whole frame: with a Clear(Z) between world passes they mix passes, read them against clearResolve.
    unsigned translucentZWriteSkinned=0,translucentZWriteOther=0,translucentNoZWrite=0,depthOnlyPrepass=0,depthOnlyPrepassSkinned=0,opaqueZWriteAfterSkinned=0,opaqueZWriteAfterOther=0,waterZWriteAfterTranslucent=0;
    unsigned censusDraws=0,firstTranslucentZAt=0,firstTranslucentZSkinnedAt=0,firstDepthOnlyAt=0,lastOpaqueZAt=0,clearResolveAt=0;
    DWORD translucentSrcBlend=0,translucentDestBlend=0;
    // 0.3.188 (task 3) TranslucentActorDepth (translucent_depth.h): the depth resolve runs before the first translucent Z-writing actor draw
    // (once per frame, tried flag in earlyDepth); a later terrain draw clears `captured` (beforeDraw) and the UI-time resolve redoes it, the old behaviour.
    unsigned translucentActorDepth=1,earlyResolveAt=0,earlyResolveTotal=0,earlyResolveUndone=0,earlyResolveUndoneTotal=0;NorthlightTranslucentDepth::Frame earlyDepth;
    void resetTranslucentCensus(){translucentZWriteSkinned=translucentZWriteOther=translucentNoZWrite=depthOnlyPrepass=depthOnlyPrepassSkinned=opaqueZWriteAfterSkinned=opaqueZWriteAfterOther=waterZWriteAfterTranslucent=0;
        censusDraws=firstTranslucentZAt=firstTranslucentZSkinnedAt=firstDepthOnlyAt=lastOpaqueZAt=clearResolveAt=earlyResolveAt=earlyResolveUndone=0;translucentSrcBlend=translucentDestBlend=0;}
    LONGLONG cpuPrep=0,cpuCapture=0,cpuWaterCapture=0,cpuEffects=0,cpuMirrorAudit=0;
    NorthlightEffectsBuckets::Frame effectsBuckets;bool effectsBucketed=false; /* 0.3.175 (S3): this sample frame's effects split */
    unsigned long long cpuCaptureReads=0; /* 0.3.150: timer clock reads inside cpuCapture, sample frames (an outer pair adds one) */
    LARGE_INTEGER cpuFrequency={};
    // 0.3.149 (RenderProfile=1): per-frame wall times at Present by frame kind, in windows that
    // follow the DiagReplayProbe windows, and per-type extension call counts of profile sample
    // frames (DeviceMirror::rawMethodCalls).
    NorthlightRenderThreadProbe::FrameCostWindow frameCost;uint64_t frameCostWindow=0;NorthlightRenderThreadProbe::ProbeMode frameCostMode=NorthlightRenderThreadProbe::ProbeOff;
    unsigned frameStartDrawCalls=0;std::uint64_t frameStartAnswered=0,frameStartForwarded=0;unsigned long long frameStartScopeReads=0;
    // 0.3.154 drawGate A/B, RenderProfile sample frames only (both flags set once per frame in
    // finishFrameImpl from profiling(); RenderProfile=0 leaves them false). Sample frames alternate
    // T (today's timers) and U (the per-draw prep/capture/water-mask CpuScopes off); both log
    // sceneMs (previous Present done -> renderEffects entry) and exact counts: DRAWGATE ab.
    bool gateFrame=false,gateUntimed=false;
    struct GateCounts {unsigned prep=0,capture=0,water=0,wmo=0,fullPasses=0,audits=0,blobCalls=0,blobTextures=0,blobClaimed=0,bufferCreates=0,processVertices=0;};
    struct GateStart {unsigned missingVS=0,terrain=0,ui=0,shadowSwaps=0;std::uint64_t generations=0;};
    GateCounts gateCounts;GateStart gateStart;
    LARGE_INTEGER gateSceneEnd={};LONGLONG gatePresentDone=0;unsigned gateSceneDraws=0;unsigned long long gateSceneReads=0;
    std::recursive_mutex gateBenchLock; /* private, uncontended: the microbenchmark's lock */
    MirrorGate gateBenchGate; /* 0.3.180: private, for ownerNs (0.3.182: the real owner entry and exit on it) */
    const int debugMode = 0;int worldDebug=0;
    NorthlightEffectSwitches::Hotkeys effectKeys;
    float nearZ = .1f, farZ = 1000.f, scaleX = 1.f, scaleY = 1.f;
    float worldMinDepth=0.f,worldMaxDepth=1.f;

    // Diagnostic sample frame (every 120th; RenderProfile=1: every 127th, which reaches
    // every Near/FarShadowInterval phase, render_thread_probe.h): timing scopes, counters,
    // periodic lines. Never a rendering decision; Diagnostics=0 turns every sample off.
    bool sampled()const{return NorthlightRenderThreadProbe::sampleFrame(frame)&&NorthlightDiagnostics::enabled();}
    static bool diagnostics(){return NorthlightDiagnostics::enabled();}
    // 0.3.154: the per-draw timing scopes; off on drawGate U frames (gateUntimed needs RenderProfile).
    bool sampledDrawTimers()const{return sampled()&&!gateUntimed;}
    // stage is read when a fault is reported (0.3.187: drawHook names the step that was running).
    template<class Work> bool extensionWork(const char* const& stage,Work&& work) noexcept {
        if(extensionFault)return false;
        return NorthlightExtensionGuard::run(std::forward<Work>(work),[&](NorthlightExtensionGuard::Fault fault) noexcept {
            extensionFault=true;failed=true;enabled=false;
            MEMORYSTATUSEX memory={};memory.dwLength=sizeof memory;GlobalMemoryStatusEx(&memory);
            logf("EXTENSION fault stage=%s type=%u availableVirtualMiB=%llu; original game calls retained; restart required",stage,unsigned(fault),(unsigned long long)(memory.ullAvailVirtual>>20));
        });
    }
    void clearFrame() {
        if(!applied||failed)stateBlocks.clear();
        if(!enabled||extensionFault){stateBlocks.clear();if(world)world->releaseStateCache();if(water)water->releaseStateCache();}
        if(world)world->endFrame(!extensionFault);if(water)water->endFrame();if(celestialDiscs)celestialDiscs->endFrame();if(shadowBlobs)shadowBlobs->endFrame();
        drop(worldDepth); terrain = captured = applied = projectionValid = false;earlyDepth.reset();
    }
    void releaseResources() {
        stateBlocks.clear();clearFrame();
        drop(sceneSurface); drop(aoSurface); drop(scene); drop(depthTex); drop(ao);
        drop(aoPS); drop(aoContactBloomPS); drop(compositePS); width = height = 0;
    }
    bool error(HRESULT hr, const char* stage) {
        if (SUCCEEDED(hr)) return false;
        if (!failed) logf("DISABLED: %s failed HRESULT=0x%08lx", stage, (unsigned long)hr);
        failed = true; return true;
    }
    bool resources(UINT w, UINT h, D3DFORMAT format) {
        if (sceneSurface && aoSurface && depthTex && w == width && h == height && format == sceneFormat) return true;
        // Resource replacement must not invalidate this frame's retained depth surface.
        drop(sceneSurface); drop(scene);
        if (w!=width || h!=height) {
            drop(aoSurface); drop(ao); drop(depthTex); captured=false;
        }
        width = w; height = h; sceneFormat = format;
        if (!aoPS && error(ext->CreatePixelShader(kAoShader, &aoPS), "AO shader")) return false;
        if (!aoContactBloomPS && error(ext->CreatePixelShader(kAoContactBloomShader, &aoContactBloomPS), "AO contact bloom shader")) return false;
        if (!compositePS && error(ext->CreatePixelShader(kCompositeShader, &compositePS), "composite shader")) return false;
        if (error(ext->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &scene, nullptr), "scene texture")) return false;
        if (!depthTex && error(ext->CreateTexture(w, h, 1, D3DUSAGE_DEPTHSTENCIL, (D3DFORMAT)MAKEFOURCC('I','N','T','Z'), D3DPOOL_DEFAULT, &depthTex, nullptr), "INTZ depth texture")) return false;
        if (!ao && error(ext->CreateTexture(w/2, h/2, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &ao, nullptr), "half resolution AO texture")) return false;
        if (error(scene->GetSurfaceLevel(0, &sceneSurface), "scene surface")) return false;
        if (!aoSurface && error(ao->GetSurfaceLevel(0, &aoSurface), "AO surface")) return false;
        logf("Resources %ux%u, AO %ux%u, format=%u", w,h,w/2,h/2,unsigned(format));
        return true;
    }
    // Swap chain 0's back buffer description only changes through Reset.
    static constexpr bool kCacheBackBufferDesc=true;
    D3DSURFACE_DESC backDesc={};bool backDescKnown=false;
    bool fullViewport(D3DSURFACE_DESC& desc,D3DVIEWPORT9* viewport=nullptr) {
        desc={};
        IDirect3DSurface9* rt = nullptr;
        HRESULT rtHR;
        if(ext->peekRenderTarget(0,rt))rtHR=rt->GetDesc(&desc); // borrowed: no device write before GetDesc
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
            if(SUCCEEDED(backHR)&&back){backHR=back->GetDesc(&bd);drop(back);}
            else backHR=D3DERR_NOTFOUND;
            if(SUCCEEDED(backHR)){backDesc=bd;backDescKnown=true;}
        }
        D3DVIEWPORT9 vp={};HRESULT vpHR=ext->GetViewport(&vp);
        bool ok=SUCCEEDED(rtHR)&&SUCCEEDED(backHR)&&SUCCEEDED(vpHR)&&desc.Width>=640&&desc.Height>=360&&
            desc.Width == bd.Width && desc.Height == bd.Height && vp.X == 0 && vp.Y == 0 &&
            vp.Width == desc.Width && vp.Height == desc.Height &&
            std::isfinite(vp.MinZ)&&std::isfinite(vp.MaxZ)&&vp.MinZ>=0&&vp.MaxZ<=1&&vp.MaxZ>vp.MinZ;
        if(viewport)*viewport=vp;
        if(!ok){
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
        nearZ=q.nearZ; farZ=q.farZ; scaleX=q.scaleX; scaleY=q.scaleY;
        worldMinDepth=vp.MinZ;worldMaxDepth=vp.MaxZ;
        return true;
    }
    bool resolveDepth() {
        if (captured) return true;
        if (!terrain || !worldDepth || !depthTex || !projectionValid) return false;
        SavedState saved(ext,&stateBlocks);
        if (!saved.ok) return false;
        // D9VK implements AMD RESZ: POINTSIZE magic 0x7fa05000 copies/resolves
        // the bound depth surface into texture slot 0, including MSAA sample zero.
        if (error(ext->SetDepthStencilSurface(worldDepth), "bind scene depth")) return false;
        if (error(ext->SetTexture(0, depthTex), "bind INTZ resolve target")) return false;
        if(selectedBackend==NorthlightBackend::Kind::Native){
            // AMD's RESZ protocol requires a dummy draw to flush sampler binding
            // through the native D3D9 runtime. Disable ALL writes, then SavedState
            // restores the game state (including stream 0 changed by DrawPrimitiveUP).
            if(error(ext->SetVertexShader(nullptr),"RESZ dummy VS")||error(ext->SetPixelShader(nullptr),"RESZ dummy PS")||
               error(ext->SetFVF(D3DFVF_XYZ),"RESZ dummy FVF"))return false;
            const D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3};
            for(auto state:states)if(error(ext->SetRenderState(state,0),"RESZ disable writes"))return false;
            if(error(ext->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1),"RESZ sampler op")||
               error(ext->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE),"RESZ sampler arg")||
               error(ext->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE),"RESZ stage1"))return false;
            const float dummy[3]={0,0,0};
            if(error(ext->DrawPrimitiveUP(D3DPT_POINTLIST,1,dummy,sizeof dummy),"RESZ dummy draw"))return false;
        }
        ext->SetRenderState(D3DRS_POINTSIZE, 0x3f800000); // trigger on a state change
        if (error(ext->SetRenderState(D3DRS_POINTSIZE, 0x7fa05000), "RESZ")) return false;
        captured = true; return true;
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
    void renderEffects() {
        CpuScope cpu(sampled()?&cpuEffects:nullptr);
        // 0.3.175 (S3): RenderProfile sample frames split the same span into buckets (effects_buckets.h).
        effectsBucketed=sampled()&&NorthlightRenderThreadProbe::profiling();
        {static const unsigned drawMethods[4]={rawMethodIndex("DrawIndexedPrimitive"),rawMethodIndex("DrawPrimitive"),rawMethodIndex("DrawIndexedPrimitiveUP"),rawMethodIndex("DrawPrimitiveUP")};
         const std::uint32_t* counters[4];for(unsigned i=0;i<4;++i)counters[i]=drawMethods[i]<ExtensionDevice::RawMethods?&mirrorState.rawMethodCalls[drawMethods[i]]:nullptr;
         effectsBuckets.begin(effectsBucketed,&qpcNow,counters,4);}
        struct BucketsEnd {NorthlightEffectsBuckets::Frame& f;~BucketsEnd(){f.end();}} bucketsEnd{effectsBuckets};
        using NorthlightEffectsBuckets::Bucket;
        // 0.3.154: sceneMs end = the last entry before effects apply; the read is billed to cpuEffects.
        if(gateFrame&&!applied){QueryPerformanceCounter(&gateSceneEnd);gateSceneDraws=drawCalls;gateSceneReads=CpuScope::reads;}
        if (applied || !enabled || failed || !projectionValid) return;
        // RenderProfile: every applying frame (two clock reads), the FRAME cost effects series.
        CpuScope frameCostScope(!sampled()&&NorthlightRenderThreadProbe::profiling()?&cpuEffects:nullptr);
        // The scope outlives every SavedState, including nested depth passes.
        // Game captures remain outside; the device gate stays held throughout.
        ExtensionDevice::RawScope rawEffects(*ext);
        D3DSURFACE_DESC desc;
        if (!fullViewport(desc) || !resources(desc.Width, desc.Height, desc.Format) || !resolveDepth()) return;
        SavedState saved(ext,&stateBlocks);
        if (!saved.ok) return;
        if(diagnostics())gpuProfile->beginFrame(frame,NorthlightRenderThreadProbe::sampleFrame(frame)); // otherwise no queries; mark/endFrame are no-ops
        effectsBuckets.mark(Bucket::Setup);
        struct ProfileEnd { NorthlightGpuProfile* p; ~ProfileEnd(){p->endFrame();} } profileEnd{gpuProfile.get()};
        IDirect3DTexture9* waterMask=water?water->maskTextureForDepth(worldMinDepth,worldMaxDepth):nullptr;
        // discs and glare, then the wrap-ring visibility in the same group of
        // 1x1 passes before the scene copy; the veil follows water (below).
        const bool celestial=celestialDiscs&&world&&debugMode==0&&worldDebug==0;float sunElevation=0;
        if(celestial){
            NorthlightCelestial::Context sky;float inverseView[16],projection[3];
            if(world->celestialContext(sky,inverseView,projection)){sunElevation=sky.sun.direction[2];
                celestialDiscs->render(saved.targets[0],depthTex,waterMask,width,height,world->legacyFogParameters(),worldMinDepth,worldMaxDepth,nearZ,farZ,projection,inverseView,sky,celestialGlowHue());}
        }
        gpuProfile->mark("CelestialDiscs");
        if(celestial){celestialDiscs->renderRing();gpuProfile->mark("CelestialRing");}
        effectsBuckets.mark(Bucket::Celestial); /* discs, glare, the terrain mask prepare, the ring */
        if (error(ext->StretchRect(saved.targets[0], nullptr, sceneSurface, nullptr, D3DTEXF_NONE), "copy scene")) return;
        effectState();
        float constants[]={1.f/width,1.f/height,nearZ,farZ,scaleX,scaleY,.60f,(world&&world->ready()?0.f:.12f),.08f,2.f,float(debugMode),0,worldMinDepth,1.f/(worldMaxDepth-worldMinDepth),worldMaxDepth,0};

        float waterFlags[]={waterMask?1.f:0.f,0,0,0};
        auto bindEffects=[&](IDirect3DTexture9* ambient){
            ext->SetPixelShaderConstantF(4,waterFlags,1);ext->SetTexture(3,waterMask);
            ext->SetPixelShaderConstantF(0,constants,4);
            ext->SetTexture(0,scene); ext->SetTexture(1,depthTex); ext->SetTexture(2,ambient);
        };
        bindEffects(nullptr);
        if (error(ext->SetRenderTarget(0,aoSurface),"AO render target")) return;
        ext->SetPixelShader(constants[7]==0.f?aoContactBloomPS:aoPS);
        if (error(quad(width/2,height/2),"AO pass")) return;
        gpuProfile->mark("AO");effectsBuckets.mark(Bucket::AO);
        // 0.3.174 FOLD: with a ready world and no effect debug view, WorldComposite applies the
        // AO and bloom (AOContactBloom: bloom rgb, AO alpha) to the scene copy itself, so the
        // full-resolution composite and the world's colour copy are skipped. LEGACY (any other
        // frame) keeps the composite before the world, exactly as before.
        const bool fold=world&&debugMode==0&&world->ready();
        auto legacyComposite=[&]{
            if (error(ext->SetRenderTarget(0,saved.targets[0]),"composition render target")) return false;
            ext->SetTexture(2,ao); ext->SetPixelShader(compositePS);
            if (error(quad(width,height),"composition pass")) return false;
            gpuProfile->mark("AOComposite");effectsBuckets.mark(Bucket::Composite);return true;
        };
        if(!fold&&!legacyComposite())return;
        if(world&&debugMode==0){
            if(!world->render(saved.targets[0],depthTex,width,height,sceneFormat,nearZ,farZ,worldMinDepth,worldMaxDepth,worldDebug,gpuProfile.get(),waterMask,fold?scene:nullptr,fold?ao:nullptr)){
                if(++worldSkippedFrames<=8||(worldSkippedFrames%120==0&&diagnostics()))
                    logf("WORLD skipped frame=%u tick=%lu context=%d ready=%d count=%u reason=%s",frame,(unsigned long)GetTickCount(),world->hasContext(),world->ready(),worldSkippedFrames,world->lastSkipReason());
                worldSkipLast=world->lastSkipReason();if(!worldSkipRun++){worldSkipStart=GetTickCount();worldSkipFirst=worldSkipLast;}
            }else if(worldSkipRun){
                if(++worldSkipEpisodes<=32||diagnostics())logf("WORLD skip episode reason=%s last=%s frames=%u ms=%lu",worldSkipFirst,worldSkipLast,worldSkipRun,(unsigned long)(GetTickCount()-worldSkipStart));
                worldSkipRun=0;
            }
            // A folded frame the world did not composite (skipped or failed; it left the target
            // untouched): re-establish the effect state it changed, then the legacy composite.
            if(fold&&!world->composited){effectState();bindEffects(ao);if(!legacyComposite())return;}
        }
        if(kWaterEffectsEnabled&&world&&water&&debugMode==0&&worldDebug==0){NorthlightWaterContext waterContext;
            if(world->waterContext(waterContext,nearZ,farZ,worldMinDepth,worldMaxDepth))water->render(saved.targets[0],depthTex,width,height,sceneFormat,waterContext);
            gpuProfile->mark("Water");effectsBuckets.mark(Bucket::Water);
        }
        if(celestial){
            const auto& haze=world->horizonHazeConstants();
            celestialDiscs->renderVeil(saved.targets[0],NorthlightCelestialGlow::hazeAtSun(haze.haze[3],haze.shape[2],sunElevation),world->skyTransmittance());
            gpuProfile->mark("CelestialVeil");effectsBuckets.mark(Bucket::Veil);
        }
        applied = true; ++appliedFrames;
        if (appliedFrames==1) logf("FIRST EFFECT FRAME: near=%.5f far=%.2f scale=%.4f,%.4f depthRange=%.9g..%.9g (before UI)",nearZ,farZ,scaleX,scaleY,worldMinDepth,worldMaxDepth);
    }
    // One shader query/reference per original draw, shared with shadow capture.
    template<class Capture> void prepareDraw(Capture capture) {
        mirrorState.gate.noteFirst(mirrorState.gate.drawTid); /* 0.3.180 (D0): the census' first draw */
        dropTerrainShadowSwap();
        extensionWork("draw capture/effects",[&]{prepareDrawImpl(capture);});
    }
    template<class Capture> void prepareDrawImpl(Capture capture) {
        ++drawCalls;
        if(failed||!enabled)return;
        if(applied){
            if(sampled()){IDirect3DVertexShader9* late=nullptr;
                if(SUCCEEDED(ext->GetVertexShader(&late))&&late&&world){
                    if(world->isWorldShader(late))++postEffectWorldDraws;
                    if(world->isSkinnedShader(late))++postEffectSkinnedDraws;
                }drop(late);
            }return;
        }
        CpuScope cpu(sampledDrawTimers()?&cpuPrep:nullptr);if(gateFrame)++gateCounts.prep;
        IDirect3DVertexShader9* vs=nullptr;
        if(FAILED(ext->GetVertexShader(&vs))||!vs){++missingVS;drop(vs);return;}
        struct ShaderRelease {IDirect3DVertexShader9*& p;~ShaderRelease(){drop(p);}} shaderRelease{vs};
        beforeDraw(vs);planTerrainShadowSwap(vs);
        if(world&&!applied&&enabled&&!failed&&projectionValid){
            D3DVIEWPORT9 viewport={};DWORD depthEnabled=FALSE;
            const bool statesRead=SUCCEEDED(ext->GetViewport(&viewport))&&SUCCEEDED(ext->GetRenderState(D3DRS_ZENABLE,&depthEnabled));
            const bool worldDomain=statesRead&&NorthlightWorldDrawDomain::accepts(projectionValid,depthEnabled!=FALSE,
                worldMinDepth,worldMaxDepth,viewport.MinZ,viewport.MaxZ);
            if(worldDomain){if(gateFrame)++gateCounts.capture;
                if(translucentActorDepth&&terrain&&!captured&&!earlyDepth.tried&&NorthlightTranslucentDepth::shouldResolve(translucentActorDepth,drawWaterVS, /* 0.3.188 (2A): early depth for translucent actors, every frame, minimum state reads */
                    [&]{DWORD v=0;ext->GetRenderState(D3DRS_ZWRITEENABLE,&v);return v;},[&]{DWORD v=0;ext->GetRenderState(D3DRS_ALPHABLENDENABLE,&v);return v;},
                    [&]{DWORD v=0;ext->GetRenderState(D3DRS_COLORWRITEENABLE,&v);return v;},[&]{return world->isSkinnedShader(vs);})&&earlyDepth.attempt()){
                    ExtensionDevice::RawScope raw(*ext);
                    if(resolveDepth()){++earlyDepth.resolves;++earlyResolveTotal;if(sampled())earlyResolveAt=censusDraws+1;}}
                if(sampled()){DWORD ab=0,zw=0,cw=0,sb=0,db=0; /* 0.3.188 (task 3): read-only census before capture(vs) so no early return hides a draw; sample frames only */
                    ext->GetRenderState(D3DRS_ALPHABLENDENABLE,&ab);ext->GetRenderState(D3DRS_ZWRITEENABLE,&zw);ext->GetRenderState(D3DRS_COLORWRITEENABLE,&cw);ext->GetRenderState(D3DRS_SRCBLEND,&sb);ext->GetRenderState(D3DRS_DESTBLEND,&db);
                    const unsigned at=++censusDraws;const bool skin=world->isSkinnedShader(vs),after=firstTranslucentZAt!=0,color=(cw&7)!=0;
                    if(zw&&color&&drawWaterVS){if(after)++waterZWriteAfterTranslucent;}
                    else if(zw&&!color){++depthOnlyPrepass;if(skin)++depthOnlyPrepassSkinned;if(!firstDepthOnlyAt)firstDepthOnlyAt=at;}
                    else if(zw&&ab){if(skin)++translucentZWriteSkinned;else ++translucentZWriteOther;if(!after)firstTranslucentZAt=at,translucentSrcBlend=sb,translucentDestBlend=db;if(skin&&!firstTranslucentZSkinnedAt)firstTranslucentZSkinnedAt=at;}
                    else if(ab)++translucentNoZWrite;
                    else if(zw){lastOpaqueZAt=at;if(after){if(skin)++opaqueZWriteAfterSkinned;else ++opaqueZWriteAfterOther;}}}
                {const unsigned long long reads=CpuScope::reads;{CpuScope cap(sampledDrawTimers()?&cpuCapture:nullptr);capture(vs);}if(CpuScope::reads!=reads)cpuCaptureReads+=CpuScope::reads-reads-1;}
                if(mirrorState.active()&&mirrorAuditSchedule.afterWorldCapture(frame,
                    mirrorState.vsFloatKnown[0]&&mirrorState.vsFloatKnown[DeviceMirror::VsFloat-1])){
                    CpuScope auditCost(diagnostics()?&cpuMirrorAudit:nullptr); // the audit itself always runs
                    if(gateFrame)++gateCounts.audits;
                    if(const char* field=ext->audit())logf("MIRROR mismatch field=%s frame=%u; cache disabled, rendering retained",field,frame);
                }
            }
            else if(world->isWorldShader(vs)){
                if(++nonWorldCaptureRejects<=4||(nonWorldCaptureRejects%3600==0&&diagnostics()))
                    logf("WORLD non-caster draw rejected: worldDepth=%.7f..%.7f drawDepth=%.7f..%.7f z=%lu states=%d count=%u",worldMinDepth,worldMaxDepth,viewport.MinZ,viewport.MaxZ,(unsigned long)depthEnabled,statesRead,nonWorldCaptureRejects);
            }
        }
        drop(vs);
    }
    // Terrain draws run with the game's shadow term neutralised while the
    // extension's shadow maps were composited last frame, so the baked ADT
    // shadow (MCSH) and the game's own dynamic shadow map no longer double
    // the extension's shadow. Planned inside the guarded capture, applied
    // around the real draw (outside extensionWork, like the draw itself).
    IDirect3DPixelShader9 *shadowSwapOriginal=nullptr,*shadowSwapReplacement=nullptr;
    void planTerrainShadowSwap(IDirect3DVertexShader9* vs){
        if(!drawGates.terrainShadow)return; /* 0.3.187: implied by the line below at every draw of this frame */
        if(!world||applied||!enabled||failed||!terrain||debugMode!=0||worldDebug!=0||!world->terrainShadowActive())return;
        auto tag=vsTags.find(vs);if(tag==vsTags.end()||(tag->second&kTagMask)!=1)return;
        IDirect3DPixelShader9* ps=nullptr;if(FAILED(ext->GetPixelShader(&ps))||!ps)return;
        IDirect3DPixelShader9* replacement=world->terrainShadowReplacement(ps);
        if(!replacement){ps->Release();return;}
        shadowSwapOriginal=ps;shadowSwapReplacement=replacement;
    }
    void dropTerrainShadowSwap(){if(shadowSwapOriginal)shadowSwapOriginal->Release();shadowSwapOriginal=shadowSwapReplacement=nullptr;}
    template<class Draw> HRESULT terrainShadowDraw(bool claimed,Draw draw){
        if(!shadowSwapOriginal)return claimed?D3D_OK:draw(); /* 0.3.187: no swap planned (planTerrainShadowSwap sets both or neither) */
        IDirect3DPixelShader9* original=shadowSwapOriginal;IDirect3DPixelShader9* replacement=shadowSwapReplacement;
        shadowSwapOriginal=shadowSwapReplacement=nullptr;
        HRESULT hr=D3D_OK;
        if(!claimed&&replacement){{CpuScope swap(sampledHookTimer());ext->SetPixelShader(replacement);}hr=draw();{CpuScope swap(sampledHookTimer());ext->SetPixelShader(original);}++terrainShadowDraws;}
        else if(!claimed)hr=draw();
        if(original)original->Release();
        return hr;
    }
    // Liquid mask only (AO/GI/fog consume it). The depth identity, target
    // description and viewport read here are reused by the mask capture.
    template<class Draw> void captureWater(IDirect3DVertexShader9* vs,unsigned userPointer,Draw draw){
        if(!water||!drawWaterVS||!water->usable()||!world||!world->hasContext())return; // == recognizesVertex(vs)
        if(!projectionValid||!worldDepth)return;
        CpuScope cost(sampledDrawTimers()?&cpuWaterCapture:nullptr);if(gateFrame)++gateCounts.water;
        IDirect3DSurface9* currentDepth=nullptr;bool mainDepth=SUCCEEDED(ext->GetDepthStencilSurface(&currentDepth))&&currentDepth==worldDepth;drop(currentDepth);if(!mainDepth)return;
        D3DSURFACE_DESC desc={};D3DVIEWPORT9 viewport={};if(!fullViewport(desc,&viewport))return;
        IDirect3DPixelShader9* ps=nullptr;struct PixelRelease {IDirect3DPixelShader9*& p;~PixelRelease(){drop(p);}} releasePS{ps};if(SUCCEEDED(ext->GetPixelShader(&ps))&&ps)water->capture(vs,ps,desc.Width,desc.Height,worldDepth,viewport,userPointer,mirrorState.invalidations,draw);
    }
    // Blob shadows are filtered only while the mod draws actor shadows: effects (F10) and shadows (F9)
    // on, a world context, and 0.3.158 ActorShadows=1 (with 0 the game's blobs are the actor shadows).
    bool blobFilterActive()const{return shadowBlobs&&enabled&&effectKeys.settings.shadows&&!applied&&terrain&&!failed&&world&&world->hasContext()&&world->actorShadowsEnabled();}
    // 0.3.187 per-frame draw gates (draw_gates.h, FrameDrawGates=1): latched where every input can
    // rise, never inside a frame: at the end of finishFrameImpl (after F9/F10/F12, setEffects and the
    // retry's failed=false; clearFrame runs before that retry, so it is not the place), in Reset
    // (failed=false) and at device creation. Off: every gate open, the 0.3.184 per-draw work.
    NorthlightDrawGates::Frame drawGates;bool frameDrawGates=true;
    LONGLONG cpuDrawHooks=0; /* RenderProfile sample frames: per-draw work outside prepareDraw (CPU profile drawHooks=) */
    LONGLONG* hookTimer=nullptr; /* &cpuDrawHooks on a timed RenderProfile sample frame (frame, gateUntimed: set before the latch) */
    void latchDrawGates(){
        NorthlightDrawGates::Inputs in;
        in.sky=celestialDiscs!=nullptr;in.blobs=shadowBlobs!=nullptr;in.world=world!=nullptr;in.enabled=enabled;in.failed=failed;
        in.shadowsKey=effectKeys.settings.shadows;in.actorShadows=world&&world->actorShadowsEnabled();
        in.worldShadows=world&&world->shadowsRequested();in.debugOff=debugMode==0&&worldDebug==0;
        drawGates=NorthlightDrawGates::latch(frameDrawGates,in);
        hookTimer=sampledDrawTimers()&&NorthlightRenderThreadProbe::profiling()?&cpuDrawHooks:nullptr;
    }
    LONGLONG* sampledHookTimer()const{return hookTimer;}
    // Native sky claim: glare, then the native disc draw, for full-viewport sky draws of 1..4 primitives.
    void skyClaim(D3DPRIMITIVETYPE t,UINT count,bool& claimed){
        if(count<=4&&celestialDiscs&&enabled&&!applied&&celestialDiscs->nativeClaimPossible(t,count)){D3DSURFACE_DESC desc;if(fullViewport(desc)){
            claimed=celestialDiscs->claimNativeGlare(t,count);
            if(!claimed)claimed=celestialDiscs->claimNativeDraw(t,count,true,[&](const char* map,const float* camera){return world->celestialPalette(map,camera);});}}
    }
    void skyObserve(HRESULT hr,D3DPRIMITIVETYPE t,UINT count){
        if(count<=4&&celestialDiscs&&enabled&&!applied&&celestialDiscs->nativeObservePossible(hr,t,count)){D3DSURFACE_DESC desc;if(fullViewport(desc))celestialDiscs->observeNativeDraw(hr,t,count,true);}
    }
    void blobFilter(UINT count,bool& claimed){
        claimed=blobClaim(count);
        if(claimed&&blobSignatureReports<4){++blobSignatureReports;IDirect3DVertexShader9* bvs=nullptr;IDirect3DPixelShader9* bps=nullptr;ext->GetVertexShader(&bvs);ext->GetPixelShader(&bps);auto vi=vsHashes.find(bvs);auto pi=psHashes.find(bps);logf("SHADOWBLOB draw signature vs=%016llx ps=%016llx primitives=%u",(unsigned long long)(vi==vsHashes.end()?0:vi->second),(unsigned long long)(pi==psHashes.end()?0:pi->second),count);drop(bvs);drop(bps);}
    }
    // One game draw: capture, the native sky and blob claims, the real draw (outside every extension
    // region, so a fault keeps the game's call) and the sky observation. The capture and its order are
    // prepareDraw's in both paths. Gates on: capture and claims share one region (a fault in either
    // skipped the rest before too, via extensionFault), the sky work runs only for count<=4 while the
    // frame's sky gate is open, the blob test only while its gate is; the observation follows the draw,
    // so it keeps a region of its own. Gates off: the 0.3.184 regions one by one.
    template<class Capture,class Draw> HRESULT drawHook(D3DPRIMITIVETYPE t,UINT count,Capture capture,Draw draw){
        bool claimed=false;
        if(!frameDrawGates){
            prepareDraw(capture);
            {CpuScope hooks(sampledHookTimer());
                extensionWork("native sky claim",[&]{skyClaim(t,count,claimed);});
                if(!claimed&&blobFilterActive())extensionWork("blob shadow filter",[&]{blobFilter(count,claimed);});}
            const HRESULT hr=terrainShadowDraw(claimed,draw);
            if(!claimed){CpuScope hooks(sampledHookTimer());extensionWork("native sky observation",[&]{skyObserve(hr,t,count);});}
            return hr;
        }
        mirrorState.gate.noteFirst(mirrorState.gate.drawTid); /* prepareDraw's prologue */
        dropTerrainShadowSwap();
        const bool sky=count<=4&&drawGates.sky;
        const char* stage="draw capture/effects";
        extensionWork(stage,[&]{
            prepareDrawImpl(capture);
            CpuScope hooks(sampledHookTimer());
            if(sky){stage="native sky claim";skyClaim(t,count,claimed);}
            if(!claimed&&drawGates.blob){stage="blob shadow filter";if(blobFilterActive())blobFilter(count,claimed);}
        });
        const HRESULT hr=terrainShadowDraw(claimed,draw);
        if(sky&&!claimed){CpuScope hooks(sampledHookTimer());extensionWork("native sky observation",[&]{skyObserve(hr,t,count);});}
        return hr;
    }
    // 0.3.154: blob shadow claim with the profile frame's counts (claim() reads texture 0 for 1..256 primitives).
    bool blobClaim(UINT count){
        if(gateFrame){++gateCounts.blobCalls;if(count&&count<=256)++gateCounts.blobTextures;}
        const bool claimed=shadowBlobs->claim(count);if(gateFrame&&claimed)++gateCounts.blobClaimed;return claimed;
    }
    void beforeDraw(IDirect3DVertexShader9* vs) {
        auto it=vsTags.find(vs); const int entry=it==vsTags.end()?0:it->second; int tag=entry&kTagMask;
        drawWaterVS=(entry&kWaterTag)!=0;
        bool wmo=tag!=1&&world&&world->recognizesWmo(vs);if(gateFrame&&wmo)++gateCounts.wmo;
        if(wmo&&projectionValid&&world->hasContext())return; // allow recovery after a terrain context rejection
        if (tag==1||wmo) {
            if(tag==1)++terrainDraws;
            D3DSURFACE_DESC desc={};
            if (!fullViewport(desc)) return;
            if (!readProjection(wmo)) {
                if (++projectionRejects==1) {
                    float p[16]={};ext->GetVertexShaderConstantF(4,p,4);
                    logf("Projection rejected: [%g %g %g %g] [%g %g %g %g] [%g %g %g %g] [%g %g %g %g]",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
                }
                return;
            }
            if(wmo&&!world->wmoContext(vs))return;
            if(!wmo&&world)world->terrainContext();
            projectionValid=true;
            IDirect3DSurface9* ds=nullptr;
            if (FAILED(ext->GetDepthStencilSurface(&ds)) || !ds) {++depthRejects;return;}
            D3DSURFACE_DESC dd; ds->GetDesc(&dd);
            if (dd.Width!=desc.Width || dd.Height!=desc.Height) {++depthRejects;drop(ds);return;}
            if (dd.Format!=D3DFMT_D24S8 && dd.Format!=D3DFMT_D24X8 && dd.Format!=(D3DFORMAT)MAKEFOURCC('I','N','T','Z')) {
                logf("DISABLED: unsupported world depth format %u",unsigned(dd.Format));failed=true;drop(ds);return;
            }
            drop(worldDepth); worldDepth=ds;
            if(captured&&earlyDepth.resolves){++earlyResolveUndone;++earlyResolveUndoneTotal;} /* 0.3.188 (2A): a terrain draw after the early resolve: the UI-time resolve redoes it */
            terrain=true; captured=false;
            resources(desc.Width,desc.Height,desc.Format);
            if(gateFrame)++gateCounts.fullPasses;
        } else if (tag==2) {
            ++uiDraws;
            if(!terrain)return;
            ++uiAfterTerrain;
            IDirect3DPixelShader9* ps=nullptr;
            const bool borrowed=ext->peekPixelShader(ps); // identity lookup only
            if (!borrowed && (FAILED(ext->GetPixelShader(&ps)) || !ps)) return;
            auto itp=psTags.find(ps); bool isUI=itp!=psTags.end() && itp->second==2; if(!borrowed)drop(ps);
            // UI variant uses DP4 rows c0..3; constant clip-w confirms screen-space UI.
            float w[4];
            if (isUI && SUCCEEDED(ext->GetVertexShaderConstantF(3,w,1)) &&
                std::fabs(w[0])+std::fabs(w[1])+std::fabs(w[2])<.001f && std::fabs(w[3]-1.f)<.001f){
                if(sampled()){DWORD z=0,zw=0,blend=0;ext->GetRenderState(D3DRS_ZENABLE,&z);ext->GetRenderState(D3DRS_ZWRITEENABLE,&zw);ext->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);
                    logf("EFFECT boundary frame=%u draw=%u z=%lu zwrite=%lu blend=%lu",frame,drawCalls,(unsigned long)z,(unsigned long)zw,(unsigned long)blend);}
                renderEffects();
            }
        }
    }
public:
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) override{Guard mirrorLock(mirrorState.gate);return ext->SetCursorProperties(XHotSpot, YHotSpot, mirrorResources.unwrap(pCursorBitmap));}
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer);if(SUCCEEDED(hr)){mirrorResources.wrap(ppBackBuffer);}return hr;}
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);if(SUCCEEDED(hr)){NorthlightReplayDrawState::noteTextureFormat(Format);mirrorResources.wrap(ppTexture);}return hr;}
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle);if(SUCCEEDED(hr)){NorthlightReplayDrawState::noteTextureFormat(Format);mirrorResources.wrap(ppVolumeTexture);}return hr;}
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);if(SUCCEEDED(hr)){NorthlightReplayDrawState::noteTextureFormat(Format);mirrorResources.wrap(ppCubeTexture);}return hr;}
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, WINBOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);if(SUCCEEDED(hr)){mirrorResources.wrap(ppSurface);}return hr;}
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, WINBOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override{Guard mirrorLock(mirrorState.gate);HRESULT hr=ext->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);if(SUCCEEDED(hr)){mirrorResources.wrap(ppSurface);}return hr;}
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9 *src_surface, const RECT *src_rect, IDirect3DSurface9 *dst_surface, const POINT *dst_point) override{Guard mirrorLock(mirrorState.gate);return ext->UpdateSurface(mirrorResources.unwrap(src_surface), src_rect, mirrorResources.unwrap(dst_surface), dst_point);}
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) override{Guard mirrorLock(mirrorState.gate);return ext->UpdateTexture(mirrorResources.unwrap(pSourceTexture), mirrorResources.unwrap(pDestinationTexture));}
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override{Guard mirrorLock(mirrorState.gate);return ext->GetRenderTargetData(mirrorResources.unwrap(pRenderTarget), mirrorResources.unwrap(pDestSurface));}
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) override{Guard mirrorLock(mirrorState.gate);return ext->GetFrontBufferData(iSwapChain, mirrorResources.unwrap(pDestSurface));}
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9 *src_surface, const RECT *src_rect, IDirect3DSurface9 *dst_surface, const RECT *dst_rect, D3DTEXTUREFILTERTYPE filter) override{Guard mirrorLock(mirrorState.gate);return ext->StretchRect(mirrorResources.unwrap(src_surface), src_rect, mirrorResources.unwrap(dst_surface), dst_rect, filter);}
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9 *surface, const RECT *rect, D3DCOLOR color) override{Guard mirrorLock(mirrorState.gate);return ext->ColorFill(mirrorResources.unwrap(surface), rect, color);}
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
    Device(IDirect3DDevice9* d,IDirect3D9* p):GuardedMirrorDevice(d,&mirrorState),parent(p),mirrorResources(this,mirrorState.gate,&mirrorEscape,&mirrorState,d),ext(new ExtensionDevice(d,&mirrorState)),stateBlocks(ext) {
        mirrorState.gate.ownerTid=MirrorGuard::threadId(); /* 0.3.180 (D0): the CreateDevice caller */
        mirrorState.gate.reportContext=this;mirrorState.gate.report=&gateForeignReport;
        parent->AddRef(); QueryPerformanceFrequency(&cpuFrequency); gpuProfile=std::make_unique<NorthlightGpuProfile>(ext); world=std::make_unique<WorldRenderer>(ext);world->setEffectsBuckets(&effectsBuckets);
        world->setConstantEpochSource({&mirrorState.constantEpoch,&mirrorState}); /* 0.3.180 (C1): read in place under the draw's gate */
        char skyRoot[MAX_PATH*3];WideCharToMultiByte(CP_UTF8,0,rootPath,-1,skyRoot,sizeof skyRoot,nullptr,nullptr);celestialDiscs=std::make_unique<NorthlightCelestialDiscRenderer>(ext,std::string(skyRoot)+"world-cache/celestial");celestialDiscs->setTerrainSource([this]{return world->celestialTerrainGeneration();},[this](unsigned body,const float* matrix){return world->drawCelestialTerrain(body,matrix);},[this](unsigned body){world->noteCelestialTerrainReuse(body);});celestialDiscs->setIdentityMap([this](std::uintptr_t exposed){return mirrorResources.rawOf(exposed,!mirrorState.enabled);});shadowBlobs=std::make_unique<NorthlightShadowBlobFilter>(ext);water=std::make_unique<NorthlightWaterRenderer>(ext); logf("D3D9 device wrapped. Ctrl+Shift+F7 fog; F8 GI; F9 shadows; F10 all effects; F12 world debug (all with Ctrl+Shift). F11 unassigned. Components start ON; GI cache stays warm.");
        translucentActorDepth=world->translucentActorDepth();frameDrawGates=world->frameDrawGates();latchDrawGates(); /* 0.3.187: after the renderers exist */
        // The async sweep feeds the memory guard (always) and the periodic MEMORY line
        // (Diagnostics only). Allocation admission stays synchronous in WorldRenderer.
        try{memoryDiagnostics=std::make_unique<NorthlightMemoryDiagnostics::Sampler>(&queryAddressSpace);}
        catch(...){logf("MEMORY async sampler unavailable; memory guard and periodic diagnostics skipped");}
        diagnosticId=InterlockedIncrement(&deviceSerial);
        logf("DEVICE lifetime event=create id=%ld live=%ld tick=%lu",diagnosticId,InterlockedIncrement(&liveDevices),(unsigned long)GetTickCount());
    }
    ~Device() {
        logf("DEVICE lifetime event=destroy-begin id=%ld tick=%lu",diagnosticId,(unsigned long)GetTickCount());
        logGateThreads("destroy");
        memoryDiagnostics.reset();
        shadowBlobs.reset();celestialDiscs.reset();gpuProfile.reset();water.reset();world.reset();releaseResources();
        stateBlocks.clear();ext->Release();ext=nullptr;
        const ULONG backendReferences=real->Release();parent->Release();
        logf("DEVICE lifetime event=destroy-end id=%ld live=%ld backendReleaseCount=%lu tick=%lu",diagnosticId,InterlockedDecrement(&liveDevices),(unsigned long)backendReferences,(unsigned long)GetTickCount());
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
        frameIntervals.reset();frameCost.reset();backDescKnown=false;
        NorthlightTrackedBuffers::invalidateAll();gpuProfile->reset(); releaseResources(); if(world)world->reset();if(water)water->reset();if(celestialDiscs)celestialDiscs->reset();if(shadowBlobs)shadowBlobs->reset(); failed=false;latchDrawGates();
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
    struct GateBench {double lockNs=-1,findNs=-1,getVsNs=-1,qpcNs=-1,ownerNs=-1;unsigned findHits=0;};
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
            "blobCalls=%u blobTextures=%u blobClaimed=%u gameCalls=%u perfCalls=%u bufferGenerations=%llu bufferCreates=%u processVertices=%u benchIters=%u lockNs=%.1f findNs=%.1f findHits=%u getVsNs=%.1f qpcBackNs=%.1f ownerNs=%.1f acqDevice=%u acqRegistry=%u acqResource=%u acqOther=%u",
            sampleFrame,timed?'T':'U',unsigned(timed),unsigned(frameApplied),unsigned(world&&world->lastFarDrawn()),unsigned(world&&world->captureSkippedLastFrame()),drawCalls-frameStartDrawCalls,scene?gateSceneDraws-frameStartDrawCalls:0u,
            scene?double(gateSceneEnd.QuadPart-gatePresentDone)*ms:-1.0,scene?gateSceneReads-frameStartScopeReads:0ull,frameReads,qpcNs,
            timed?std::max(0.0,double(cpuPrep-cpuCapture-cpuEffects)*ms):-1.0,timed?cpuPrep*ms:-1.0,timed?cpuCapture*ms:-1.0,timed?cpuWaterCapture*ms:-1.0,cpuEffects*ms,
            n.prep,n.capture,n.water,missingVS-gateStart.missingVS,terrainDraws-gateStart.terrain,n.wmo,uiDraws-gateStart.ui,n.fullPasses,terrainShadowDraws-gateStart.shadowSwaps,n.audits,
            n.blobCalls,n.blobTextures,n.blobClaimed,gameCalls,perf,generations,n.bufferCreates,n.processVertices,GateBenchIters,b.lockNs,b.findNs,b.findHits,b.getVsNs,b.qpcNs,
            b.ownerNs,acqDevice,acqRegistry,acqResource,acqOther);
    }
    void finishFrame() {
        Guard mirrorLock(mirrorState.gate);
        struct InvalidateOnReturn {DeviceMirror& state;~InvalidateOnReturn(){state.invalidate();}} invalidate{mirrorState};
        if(extensionFault){resetTranslucentCensus();clearFrame();return;}
        extensionWork("frame finish",[&]{finishFrameImpl();});
    }
    void finishFrameImpl() {
        const auto frameEffects=effectKeys.settings;const bool frameEnabled=enabled;
        LARGE_INTEGER intervalTick={};NorthlightFrameIntervals::Report intervalReport;
        if(diagnostics()&&QueryPerformanceCounter(&intervalTick)&&frameIntervals.sample(intervalTick.QuadPart,cpuFrequency.QuadPart,intervalReport))
            logf("FRAME interval device=%ld frame=%u tick=%lu enabled=%d samples=%u meanMs=%.3f p50Ms=%.3f p95Ms=%.3f maxMs=%.3f fps=%.2f gi=%u shadows=%u fog=%u",diagnosticId,frame,(unsigned long)GetTickCount(),enabled,intervalReport.count,intervalReport.meanMs,intervalReport.p50Ms,intervalReport.p95Ms,intervalReport.maxMs,1000.0/intervalReport.meanMs,unsigned(frameEffects.gi),unsigned(frameEffects.shadows),unsigned(frameEffects.fog));
        if(diagnostics())gpuProfile->poll();
        // Poll only while this game owns foreground focus.
        D3DDEVICE_CREATION_PARAMETERS cp={}; ext->GetCreationParameters(&cp);
        bool focus=GetForegroundWindow()==cp.hFocusWindow;
        bool modifiers=(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_SHIFT)&0x8000);
        bool k10=focus&&modifiers&&(GetAsyncKeyState(VK_F10)&0x8000);
        const unsigned componentKeys=((GetAsyncKeyState(VK_F7)&0x8000)?NorthlightEffectSwitches::Fog:0u)|
            ((GetAsyncKeyState(VK_F8)&0x8000)?NorthlightEffectSwitches::GI:0u)|
            ((GetAsyncKeyState(VK_F9)&0x8000)?NorthlightEffectSwitches::Shadows:0u);
        if(effectKeys.poll(focus,modifiers,componentKeys)){
            const auto& settings=effectKeys.settings;
            if(world)world->setEffects(settings);
            frameIntervals.reset();frameCost.reset();
            logf("Effects components tick=%lu master=%u GI=%s shadows=%s fog=%s giCache=warm",
                (unsigned long)GetTickCount(),unsigned(enabled),settings.gi?"ON":"OFF",settings.shadows?"ON":"OFF",settings.fog?"ON":"OFF");
        }
        bool retry=k10&&!key10&&!enabled;
        if(k10&&!key10){enabled=!enabled;frameIntervals.reset();frameCost.reset();logf("Effects %s tick=%lu",enabled?"ON":"OFF",(unsigned long)GetTickCount());}
        bool k12=focus&&modifiers&&(GetAsyncKeyState(VK_F12)&0x8000);
        if(k12&&!key12){worldDebug=(worldDebug+1)%4;logf("World debug %d tick=%lu (0 normal,1 shadows,2 GI,3 volume)",worldDebug,(unsigned long)GetTickCount());}
        key12=k12;key10=k10;
        if(frame==1||(frame%600==0&&diagnostics()))logf("frame=%u applied=%u terrainShaders=%u uiShaders=%u projectionRejects=%u terrainThisFrame=%d depth=%d draws=%u missingVS=%u terrainDraws=%u terrainShadowDraws=%u viewportRejects=%u depthRejects=%u uiDraws=%u uiAfterTerrain=%u",frame,appliedFrames,matchedTerrain,matchedUI,projectionRejects,terrain,captured,drawCalls,missingVS,terrainDraws,terrainShadowDraws,viewportRejects,depthRejects,uiDraws,uiAfterTerrain);
        // The address-space sweep is asynchronous; the render thread only compares
        // its result. Trims run below, after clearFrame(), at the frame boundary.
        bool memoryTrim=false;int memoryCaps=-1; /* -1 unchanged, 0 full, 1 half */
        NorthlightMemoryDiagnostics::Sample memorySample; /* exact full walk (never an admission witness bound) */
        if(memoryDiagnostics){
            try{
                const DWORD now=GetTickCount();
                if(memoryGuard.requestDue(now))memoryDiagnostics->request();
                auto& sample=memorySample;
                if(memoryDiagnostics->take(sample)){
                    if(sample.valid){
                        const auto decision=memoryGuard.update(sample.availableVirtual,sample.largestFree,DWORD(sample.tick));
                        memoryTrim=decision.trim;
                        if(decision.afterTrim)logf("MEMORY guard after-trim exactWalk=1 availableVirtualMiB=%llu largestFreeMiB=%llu regions=%llu pressure=%u trims=%u sampleTick=%llu",
                            (unsigned long long)(sample.availableVirtual>>20),(unsigned long long)(sample.largestFree>>20),(unsigned long long)sample.regions,unsigned(memoryGuard.pressure()),memoryGuard.trims(),(unsigned long long)sample.tick);
                        if(decision.entered||decision.recovered){
                            memoryCaps=decision.entered?1:0;
                            logf("MEMORY guard %s exactWalk=1 availableVirtualMiB=%llu largestFreeMiB=%llu enterBelowMiB=%llu/%llu recoverAboveMiB=%llu/%llu caps=%s entries=%u trims=%u",decision.entered?"pressure":"recovered",
                                (unsigned long long)(sample.availableVirtual>>20),(unsigned long long)(sample.largestFree>>20),
                                (unsigned long long)(memoryGuard.policy().enterLargest>>20),(unsigned long long)(memoryGuard.policy().enterAvailable>>20),
                                (unsigned long long)(memoryGuard.policy().recoverLargest>>20),(unsigned long long)(memoryGuard.policy().recoverAvailable>>20),
                                decision.entered?"half":"full",memoryGuard.entries(),memoryGuard.trims());
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
        const unsigned waterDraws=water?water->frameCaptures():0,waterScans=water?water->frameMaskScans():0,waterClears=water?water->frameClears():0,waterReadFailures=water?water->frameReadFailures():0;
        if(sampled())logf("EFFECT trailing world frame=%u worldDraws=%u skinnedDraws=%u",frame,postEffectWorldDraws,postEffectSkinnedDraws);
        if(sampled())logf("TRANSLUCENT frame=%u zwriteSkinned=%u zwriteOther=%u noZWrite=%u depthOnly=%u depthOnlySkinned=%u opaqueZAfterSkinned=%u opaqueZAfterOther=%u waterZAfter=%u firstBlend=%lu/%lu draws=%u firstTranslucentZ=%u firstTranslucentZSkinned=%u firstDepthOnly=%u lastOpaqueZ=%u clearResolve=%u earlyResolve=%u earlyResolveAt=%u earlyResolveTotal=%u earlyResolveUndone=%u earlyResolveUndoneTotal=%u",frame,translucentZWriteSkinned,translucentZWriteOther,translucentNoZWrite,depthOnlyPrepass,depthOnlyPrepassSkinned,opaqueZWriteAfterSkinned,opaqueZWriteAfterOther,waterZWriteAfterTranslucent,(unsigned long)translucentSrcBlend,(unsigned long)translucentDestBlend,censusDraws,firstTranslucentZAt,firstTranslucentZSkinnedAt,firstDepthOnlyAt,lastOpaqueZAt,clearResolveAt,earlyDepth.resolves,earlyResolveAt,earlyResolveTotal,earlyResolveUndone,earlyResolveUndoneTotal);
        resetTranslucentCensus();
        postEffectWorldDraws=postEffectSkinnedDraws=0;
        const bool sampledFrame=sampled(),frameApplied=applied;
        {CpuScope cpu(sampledFrame?&cleanup:nullptr);clearFrame();}
        if(memoryCaps>=0&&world)world->setMemoryPressure(memoryCaps==1);
        if(memoryTrim)trimMemory(memorySample);
        if(NorthlightRenderThreadProbe::profiling()&&cpuFrequency.QuadPart>0){namespace P=NorthlightRenderThreadProbe;
            // Windows: the probe's (flushed when it changes), else 600 kept frames. Sample frames are excluded.
            const bool probe=world&&P::probing();const uint64_t window=probe?world->probeWindow():0;
            if(frameCost.count()>=(probe?P::FrameCostWindow::Capacity:600u)||(probe&&window!=frameCostWindow))logFrameCost();
            frameCostWindow=window;frameCostMode=probe?world->probeMode():P::ProbeOff;
            frameCost.close(double(cpuEffects)*1000.0/double(cpuFrequency.QuadPart),P::kindOf(frameApplied,world&&world->lastNearDrawn(),world&&world->lastFarDrawn()),sampledFrame);
        }
        if(retry){if(world)world->recover();if(water)water->recover();if(failed){releaseResources();failed=false;logf("Effects retry requested after frame cleanup");}}
        if(sampledFrame&&cpuFrequency.QuadPart>0){
            double ms=1000.0/double(cpuFrequency.QuadPart);
            // 0.3.154: U frames have no per-draw timers; their numbers are on the DRAWGATE ab line.
            if(!gateUntimed)logf("CPU profile frame=%u drawGate=%.3fms capture=%.3fms effects=%.3fms cleanup=%.3fms total=%.3fms enabled=%d gi=%u shadows=%u fog=%u captureSkipped=%u drawHooks=%.3fms frameDrawGates=%u",sampleFrame,std::max(0.0,double(cpuPrep-cpuCapture-cpuEffects)*ms),cpuCapture*ms,cpuEffects*ms,cleanup*ms,(cpuPrep+cleanup)*ms,frameEnabled,unsigned(frameEffects.gi),unsigned(frameEffects.shadows),unsigned(frameEffects.fog),unsigned(world&&world->captureSkippedLastFrame()),
                NorthlightRenderThreadProbe::profiling()?cpuDrawHooks*ms:-1.0,unsigned(frameDrawGates)); /* 0.3.187: drawHooks -1 = not measured (RenderProfile=0) */
            // captureWorld+captureWater=capture. qpcNs: one QueryPerformanceCounter call,
            // so a profile can subtract timer overhead (2 calls per CpuScope).
            LARGE_INTEGER a={},b={},t={};QueryPerformanceCounter(&a);for(unsigned i=0;i<32;++i)QueryPerformanceCounter(&t);QueryPerformanceCounter(&b);
            const double qpcNs=double(b.QuadPart-a.QuadPart)*1e9/double(cpuFrequency.QuadPart)/33.0;
            if(!gateUntimed)logf("CPU capture split frame=%u captureWorld=%.3fms captureWater=%.3fms qpcNs=%.1f",sampleFrame,std::max(0.0,double(cpuCapture-cpuWaterCapture)*ms),cpuWaterCapture*ms,qpcNs);
            logf("WATER mask draws=%u deferred=%u clears=%u scans=%u readFailures=%u",waterDraws,0u,waterClears,waterScans,waterReadFailures);
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
                logf("D3D calls frame=%u mirrorAudit=%u counted=%d total=%u gameDraws=%u mirrorAnswered=%llu mirrorForwarded=%llu captureSkipped=%u probeRan=%u probeMode=%s%s",sampleFrame,unsigned(sampleFrame%120==60),int(mirrorState.rawCounting),rawTotal,drawCalls-frameStartDrawCalls,
                    (unsigned long long)(mirrorState.answered-frameStartAnswered),(unsigned long long)(mirrorState.forwarded-frameStartForwarded),unsigned(world&&world->captureSkippedLastFrame()),
                    unsigned(world&&world->replayProbeRanThisFrame),NorthlightRenderThreadProbe::probeModeName(world?world->probeMode():NorthlightRenderThreadProbe::ProbeOff),calls);
                // Timer overhead of this sample frame's scoped timings (capture phases report their own clockReads).
                // mirrorAudit=1: this 127-frame sample is also the functional mirror audit frame (frame%120==60).
                const unsigned long long reads=CpuScope::reads-frameStartScopeReads;
                // 0.3.150: capture net of its timers: reads inside cpuCapture (CpuScopes and the capture-phase subset) x qpcNs.
                const unsigned long long captureReads=cpuCaptureReads+(world?world->capturePhaseReadsLastFrame():0);
                if(!gateUntimed)logf("CPU timers frame=%u mirrorAudit=%u cpuScopeClockReads=%llu qpcNs=%.1f timerOverheadMs=%.3f captureClockReads=%llu captureNetMs=%.3f",sampleFrame,unsigned(sampleFrame%120==60),reads,qpcNs,double(reads)*qpcNs/1e6,
                    captureReads,std::max(0.0,double(cpuCapture)*ms-double(captureReads)*qpcNs/1e6));
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
        cpuPrep=cpuCapture=cpuWaterCapture=cpuEffects=0;cpuCaptureReads=0;cpuDrawHooks=0;
        ++frame;mirrorState.gate.frame.store(frame,std::memory_order_relaxed); /* 0.3.180: the census' frame */
        // Per-type call counts run only through a RenderProfile sample frame.
        mirrorState.rawCounting=sampled()&&NorthlightRenderThreadProbe::profiling();
        if(mirrorState.rawCounting){std::memset(mirrorState.rawMethodCalls,0,sizeof mirrorState.rawMethodCalls);
            frameStartDrawCalls=drawCalls;frameStartAnswered=mirrorState.answered;frameStartForwarded=mirrorState.forwarded;frameStartScopeReads=CpuScope::reads;}
        // 0.3.154: drawGate A/B flags of the next frame; U = top bit of a multiplicative hash of the sample
        // index (parity phase-locks to FarShadowInterval: 16 of 21 far frames would have been U).
        gateFrame=mirrorState.rawCounting;gateUntimed=gateFrame&&(((frame/NorthlightRenderThreadProbe::ProfilePeriod)*2654435761u)>>31);
        mirrorState.gate.counting.store(gateFrame,std::memory_order_relaxed);perfCounting.store(gateFrame,std::memory_order_relaxed);
        if(gateFrame){gateCounts={};gateStart={missingVS,terrainDraws,uiDraws,terrainShadowDraws,NorthlightTrackedBuffers::clock.load(std::memory_order_relaxed)};
            gateSceneEnd={};gateSceneDraws=0;gateSceneReads=0;for(unsigned s=0;s<MirrorGate::Sites;++s)mirrorState.gate.takeAcquired(MirrorSite(s));perfCalls.store(0,std::memory_order_relaxed);}
        latchDrawGates(); /* 0.3.187: the next frame's draw gates, after every input above */
    }
    HRESULT STDMETHODCALLTYPE Present(const RECT* src,const RECT* dst,HWND wnd,const RGNDATA* dirty) override { Guard mirrorLock(mirrorState.gate);mirrorState.gate.noteFirst(mirrorState.gate.presentTid);
        PresentTicks ticks;finishFrame();ticks.finish();
        const HRESULT hr=ext->Present(src,dst,wnd,dirty);ticks.present();presented(ticks);return hr;
    }
    // After the real Present, under the gate. Arithmetic only; windows are logged by finishFrame.
    void presented(const PresentTicks& t){
        if(t.on)frameCost.presented(t.entry.QuadPart,t.finished.QuadPart,t.done.QuadPart,cpuFrequency.QuadPart,threadLogLines);
        if(t.on)gatePresentDone=t.done.QuadPart; /* 0.3.154: sceneMs start of the next frame */
    }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT index,IDirect3DSwapChain9** out) override { Guard mirrorLock(mirrorState.gate);
        HRESULT hr=ext->GetSwapChain(index,out);
        if(SUCCEEDED(hr)&&out&&*out)*out=new SwapChain(*out,this,&mirrorResources);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pp,IDirect3DSwapChain9** out) override { Guard mirrorLock(mirrorState.gate);
        HRESULT hr=ext->CreateAdditionalSwapChain(pp,out);
        if(SUCCEEDED(hr)&&out&&*out)*out=new SwapChain(*out,this,&mirrorResources);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* rects,DWORD flags,D3DCOLOR color,float z,DWORD stencil) override { Guard mirrorLock(mirrorState.gate);
        if ((flags&D3DCLEAR_ZBUFFER)&&terrain&&!captured&&!applied&&enabled&&!failed) {
            IDirect3DSurface9* ds=nullptr;ext->GetDepthStencilSurface(&ds);
            if(ds==worldDepth&&resolveDepth()&&sampled()&&!clearResolveAt)clearResolveAt=censusDraws+1;drop(ds); /* 0.3.188 (task 3): census position of a Clear(Z) resolve */
        }
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
        if(gateFrame)++gateCounts.processVertices; /* 0.3.154: up to two generations */
        NorthlightTrackedBuffers::written(buffer);
        HRESULT hr=ext->ProcessVertices(src,dest,count,NorthlightTrackedBuffers::unwrap(buffer),mirrorResources.unwrap(decl),flags);
        mirrorState.invalidate();
        NorthlightTrackedBuffers::written(buffer);return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** out) override { Guard mirrorLock(mirrorState.gate);
        HRESULT hr=ext->CreateVertexShader(code,out);
        if(SUCCEEDED(hr)&&out&&*out)extensionWork("vertex shader registration",[&]{std::vector<DWORD> words;auto h=shaderHash(*out,words);int tag=contains(kTerrainVS,h)?1:contains(kUiVS,h)?2:0;vsHashes[*out]=h;if(world)world->registerShader(*out,h);if(water)water->registerVertex(*out,h,words.data(),words.size());vsTags[*out]=tag|(water&&water->hasVertex(*out)?kWaterTag:0);if(tag==1)++matchedTerrain;else if(tag==2)++matchedUI;});
        if(SUCCEEDED(hr))mirrorResources.wrap(out);return hr;
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out) override { Guard mirrorLock(mirrorState.gate);
        HRESULT hr=ext->CreatePixelShader(code,out);
        if(SUCCEEDED(hr)&&out&&*out)extensionWork("pixel shader registration",[&]{std::vector<DWORD> words;auto h=shaderHash(*out,words);psTags[*out]=contains(kUiPS,h)?2:0;psHashes[*out]=h;if(water)water->registerPixel(*out,h,words.data(),words.size());if(world)world->registerPixelShader(*out);});
        if(SUCCEEDED(hr))mirrorResources.wrap(out);return hr;
    }
    // 0.3.187: the four draw entry points share drawHook(); each passes its capture and its real draw.
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE t,UINT start,UINT count) override { Guard mirrorLock(mirrorState.gate);
        auto draw=[&]{return ext->DrawPrimitive(t,start,count);};
        return drawHook(t,count,[&](IDirect3DVertexShader9* vs){world->capture(t,0,0,0,start,count,false,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::NoUserPointer,draw);},draw);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT base,UINT min,UINT vertices,UINT start,UINT count) override { Guard mirrorLock(mirrorState.gate);
        auto draw=[&]{return ext->DrawIndexedPrimitive(t,base,min,vertices,start,count);};
        return drawHook(t,count,[&](IDirect3DVertexShader9* vs){world->capture(t,base,min,vertices,start,count,true,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::NoUserPointer,draw);},draw);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT count,const void* data,UINT stride) override { Guard mirrorLock(mirrorState.gate);
        auto draw=[&]{return ext->DrawPrimitiveUP(t,count,data,stride);};
        return drawHook(t,count,[&](IDirect3DVertexShader9* vs){world->captureUP(t,0,0,count,nullptr,D3DFMT_UNKNOWN,data,stride,false,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::UserVertices,draw);},draw);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT min,UINT vertices,UINT count,const void* indices,D3DFORMAT fmt,const void* data,UINT stride) override { Guard mirrorLock(mirrorState.gate);
        auto draw=[&]{return ext->DrawIndexedPrimitiveUP(t,min,vertices,count,indices,fmt,data,stride);};
        return drawHook(t,count,[&](IDirect3DVertexShader9* vs){world->captureUP(t,min,vertices,count,indices,fmt,data,stride,true,vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(vs,NorthlightWaterRenderer::UserVerticesAndIndices,draw);},draw);}
};

static void finishDeviceFrame(IDirect3DDevice9* owner) {static_cast<Device*>(owner)->finishFrame();}
static void presentedDeviceFrame(IDirect3DDevice9* owner,const PresentTicks& ticks) {static_cast<Device*>(owner)->presented(ticks);}

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
        HRESULT hr=real->CreateDevice(adapter,type,window,flags,pp,out);
        logf("CreateDevice HRESULT=0x%08lx flags=0x%lx",(unsigned long)hr,(unsigned long)flags);
        if(SUCCEEDED(hr)&&out&&*out)*out=new Device(*out,this);
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
        logf("DXVK compatibility process defaults applied=%u vendor=1002 cachedDynamicBuffers=True explicitInlineOverrides=%u dxvk=%s DXVK_CONFIG supported=%u",
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
        // Only Backend=dxvk (Windows DXVK 2.7.1) gets the vendor/buffer defaults;
        // legacy (the macOS WoWSilicon DXVK) keeps today's runtime unchanged.
        if(kind==NorthlightBackend::Kind::Dxvk)configureDxvkCompatibility(info);
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
    const auto result=NorthlightBackendLoader::load(sys,candidates,system);
    HMODULE module=result.module;
    const auto& last=result.attempts.empty()?NorthlightBackendLoader::Attempt{}:result.attempts.back();
    // Only DXVK keeps the legacy (unchecked, no RESZ dummy draw) rules; every
    // other runtime, including the system fallback, gets the native rules.
    if(module&&(result.fallback||(configured==NorthlightBackend::Kind::Legacy&&!last.info.dxvk)))selectedBackend=NorthlightBackend::Kind::Native;
    logf("Northlight renderer 0.3.188; reference sun look (sun glow hue from native/sunHalo band, soft-shoulder glare, veil, sun-tinted haze), native sun/moon suppressed (F1b), lamps dimmed to 30 pct in direct sun, native moon02 skipped by texture identity, no game bytes in the DLL, MEMREAD self-read profile (RenderProfile), soft sun removal in shadow, jump-stable shadow anchor, geometry coverage hold with travel lead, steadier animated shadow edges (near 5x5 tent, still-camera shadow history), native blob shadows identified in 16-bit A1R5G5B5 uploads, bilinear lighting history, near capture reserve for the player and companions, remembered rigid prop shadows (drawn-by-game states, windowed held), AO and bloom folded into the world composite, ground normals reject object tops, both wide samples, batched celestial terrain mask, DXVK async left to the runtime, render-thread terrain upload and rigid bookkeeping trims, moon without the horizon stall, art layer bands retimed to the sun and moon, actor prepare on a worker, trimmed prepare handoff, in-place capture constants, gate thread census, predicted snapshot lookups, word-wise memcmp, owner-thread gate elision; abandoned-frame prepare quarantine; removal smoothing on matching normals in its own pass (35/50 degree gate); per-frame draw gates; translucent depth census; backend=%s path=%ls loaded=%d error=%lu",
         NorthlightBackend::name(configured),last.path.c_str(),module!=nullptr,module?0ul:(last.error?last.error:(unsigned long)ERROR_INVALID_PARAMETER));
    logAttempts(result.attempts);
    logHostExecutable(sys.selfPath);
    logf("BACKEND selected=%ls runtime=%s rules=%s BackendPath=%ls fallback=%u",module?last.path.c_str():L"(none)",
         last.info.dxvk?(last.info.dxvkVersion.empty()?"DXVK (unknown version)":last.info.dxvkVersion.c_str()):"non-DXVK",NorthlightBackend::name(selectedBackend),overridden.empty()?L"(default)":overridden.c_str(),unsigned(result.fallback));
    if(result.fallback&&module)logf("BACKEND SELF-LOAD REFUSED: the configured backend resolves to this proxy (or another Northlight build); fell back to the system d3d9 runtime. Fix northlight-renderer.ini Backend/BackendPath.");
    else if(result.fallback)logf("BACKEND SELF-LOAD REFUSED: the configured backend resolves to this proxy (or another Northlight build) and the system d3d9 fallback FAILED too (expected under Wine with d3d9=n: the system d3d9 is builtin; the fallback is Windows-only). Fix northlight-renderer.ini Backend/BackendPath.");
    if(module&&last.info.dxvk&&configured!=NorthlightBackend::Kind::Dxvk)
        logf("DXVK compatibility defaults not applied: Backend=%s keeps the current runtime rules (no vendor 1002, no cachedDynamicBuffers override)",NorthlightBackend::name(configured));
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
    IDirect3D9* p=fn(sdk);return p&&!NorthlightBackendLoader::ExportScope::reentered()?new Factory(p):p;
}
extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT sdk,IDirect3D9Ex** out) {
    NORTHLIGHT_EXPORT;
    auto fn=procedure<HRESULT(WINAPI*)(UINT,IDirect3D9Ex**)>("Direct3DCreate9Ex");
    logf("Direct3D9Ex requested: forwarding without effects (use this client's normal D3D9 path).");
    return fn?fn(sdk,out):D3DERR_NOTAVAILABLE;
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
