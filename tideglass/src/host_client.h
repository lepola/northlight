#pragma once
#include "host_scenery_pose.h"
#include "host_api.h"
#include "host_diagnostics.h"
#include "host_capture.h"
#include "host_environment.h"
#include "host_liquid.h"
#include <utility>

// Host-side ownership helpers, shared by the two local adapters. A frame may be
// moved through the host's existing bounded queue; no second queue is created.
class WaterHostFrameRef
{
public:
    WaterHostFrameRef() = default;
    WaterHostFrameRef(const WaterHostAPI* api, WaterHostFrame* frame) : m_api(api), m_frame(frame) {}
    ~WaterHostFrameRef() { if (m_frame) m_api->release_frame(m_frame); }
    WaterHostFrameRef(const WaterHostFrameRef& other) : m_api(other.m_api), m_frame(other.m_frame)
    { if (m_frame) m_api->retain_frame(m_frame); }
    WaterHostFrameRef(WaterHostFrameRef&& other) noexcept : m_api(other.m_api), m_frame(std::exchange(other.m_frame, nullptr)) {}
    WaterHostFrameRef& operator=(WaterHostFrameRef other) noexcept { Swap(other); return *this; }
    void Swap(WaterHostFrameRef& other) noexcept { std::swap(m_api, other.m_api); std::swap(m_frame, other.m_frame); }
    WaterHostFrame* Get() const { return m_frame; }
    explicit operator bool() const { return m_frame != nullptr; }
private:
    const WaterHostAPI* m_api = nullptr;
    WaterHostFrame* m_frame = nullptr;
};

class WaterHostModule
{
public:
    WaterHostModule() = default;
    WaterHostModule(const WaterHostModule&) = delete;
    WaterHostModule& operator=(const WaterHostModule&) = delete;
    ~WaterHostModule() { Close(); }
    bool Open(const wchar_t* absolutePath)
    {
        if (m_module || !absolutePath || !*absolutePath) return false;
        HMODULE module = LoadLibraryExW(absolutePath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        auto get = module ? reinterpret_cast<WaterHostGetAPIFn>(reinterpret_cast<void*>(GetProcAddress(module, "WaterHostGetAPI"))) : nullptr;
        const auto* api = get ? get(kWaterHostABI, sizeof(WaterHostAPI)) : nullptr;
        if (!api || api->abi != kWaterHostABI || api->size != sizeof(WaterHostAPI)) {
            if (module) FreeLibrary(module);
            return false;
        }
        auto diag=reinterpret_cast<WaterHostGetDiagnosticsAPIFn>(reinterpret_cast<void*>(GetProcAddress(module,"WaterHostGetDiagnosticsAPI")));
        m_diagnostics=diag?diag(kWaterDiagnosticsABI,sizeof(WaterHostDiagnosticsAPI)):nullptr;
        if(m_diagnostics&&(m_diagnostics->abi!=kWaterDiagnosticsABI||m_diagnostics->size!=sizeof(WaterHostDiagnosticsAPI)))m_diagnostics=nullptr;
        auto getCapture=reinterpret_cast<WaterHostGetCaptureAPIFn>(reinterpret_cast<void*>(GetProcAddress(module,"WaterHostGetCaptureAPI")));
        m_capture=getCapture?getCapture(kWaterCaptureABI,sizeof(WaterHostCaptureAPI)):nullptr;
        if(m_capture&&(m_capture->abi!=kWaterCaptureABI||m_capture->size!=sizeof(WaterHostCaptureAPI)||!m_capture->pass))m_capture=nullptr;
        auto getTrace=reinterpret_cast<WaterHostGetTraceAPIFn>(reinterpret_cast<void*>(GetProcAddress(module,"WaterHostGetTraceAPI")));
        m_trace=getTrace?getTrace(kWaterTraceABI,sizeof(WaterHostTraceAPI)):nullptr;
        if(m_trace&&(m_trace->abi!=kWaterTraceABI||m_trace->size!=sizeof(WaterHostTraceAPI)))m_trace=nullptr;
        m_poseRequests=reinterpret_cast<PoseRequestsFn>(reinterpret_cast<void*>(GetProcAddress(module,"WaterHostSceneryPoses")));
        auto getEnvironment=reinterpret_cast<WaterHostGetEnvironmentAPIFn>(reinterpret_cast<void*>(GetProcAddress(module,"WaterHostGetEnvironmentAPI")));
        m_environment=getEnvironment?getEnvironment(kWaterEnvironmentABI,sizeof(WaterHostEnvironmentAPI)):nullptr;
        if(!m_environment&&getEnvironment)m_environment=getEnvironment(3,sizeof(WaterHostEnvironmentAPI));
        if(!m_environment&&getEnvironment)m_environment=getEnvironment(2,sizeof(WaterHostEnvironmentAPI));
        if(m_environment&&((m_environment->abi!=kWaterEnvironmentABI&&m_environment->abi!=3&&m_environment->abi!=2)||m_environment->size!=sizeof(WaterHostEnvironmentAPI)||!m_environment->set_environment||!m_environment->surface_status))m_environment=nullptr;
        auto getLiquid=reinterpret_cast<WaterHostGetLiquidAPIFn>(reinterpret_cast<void*>(GetProcAddress(module,"WaterHostGetLiquidAPI")));
        m_liquid=getLiquid?getLiquid(kWaterLiquidABI,sizeof(WaterHostLiquidAPI)):nullptr;
        if(m_liquid&&(m_liquid->abi!=kWaterLiquidABI||m_liquid->size!=sizeof(WaterHostLiquidAPI)||!m_liquid->composite_masked_water))m_liquid=nullptr;
        if(!m_liquid&&getLiquid){
            const auto prefix=offsetof(WaterHostLiquidAPI,composite_masked_water);
            m_liquid=getLiquid(9,prefix);
            if(m_liquid&&(m_liquid->abi!=9||m_liquid->size!=prefix))m_liquid=nullptr;
        }
        if(m_liquid&&(
            !m_liquid->classify||!m_liquid->observe||!m_liquid->before_surface||!m_liquid->effects_mask||!m_liquid->after_surface||!m_liquid->composite||!m_liquid->capture_client||!m_liquid->observe_draw||!m_liquid->scene_before))m_liquid=nullptr;
        m_module = module;
        m_api = api;
        return true;
    }
    // The host must first stop capture, drain replay and destroy its contexts on
    // their owner thread. Refuse unload while CPU frames or GPU sessions survive.
    bool Close()
    {
        if (!m_module) return true;
        if (!m_api->can_unload()) return false;
        if (!FreeLibrary(m_module)) return false;
        m_module = nullptr;m_poseRequests=nullptr;
        m_api = nullptr;m_capture=nullptr;m_diagnostics=nullptr;m_trace=nullptr;m_environment=nullptr;m_liquid=nullptr;
        return true;
    }
    using PoseRequestsFn=WaterSceneryPoseRequests* (__cdecl*)(WaterHostContext*,uint32_t,uint32_t);
    PoseRequestsFn PoseRequests() const {return m_poseRequests;}
    const WaterHostEnvironmentAPI* Environment()const{return m_environment;}
    const WaterHostLiquidAPI* Liquid()const{return m_liquid;}
    const WaterHostCaptureAPI* CaptureAPI()const{return m_capture;}
    const WaterHostTraceAPI* Trace() const {return m_trace;}
    const WaterHostDiagnosticsAPI* Diagnostics() const {return m_diagnostics;}
    const WaterHostAPI* API() const { return m_api; }
    WaterHostFrameRef Capture(const WaterHostCamera& camera,WaterHostTextureMap map=nullptr,void* context=nullptr) const
    { return m_api ? WaterHostFrameRef(m_api,m_liquid?m_liquid->capture_client(&camera,map,context):m_api->capture_client(&camera)) : WaterHostFrameRef(); }
    WaterHostFrameRef Copy(const WaterFrameValues& values) const
    { return m_api ? WaterHostFrameRef(m_api, m_api->copy_frame(&values, sizeof(values))) : WaterHostFrameRef(); }
private:
    PoseRequestsFn m_poseRequests=nullptr;
    HMODULE m_module = nullptr;
    const WaterHostAPI* m_api = nullptr;
    const WaterHostDiagnosticsAPI* m_diagnostics=nullptr;
    const WaterHostTraceAPI* m_trace=nullptr;
    const WaterHostCaptureAPI* m_capture=nullptr;
    const WaterHostEnvironmentAPI* m_environment=nullptr;
    const WaterHostLiquidAPI* m_liquid=nullptr;
};
