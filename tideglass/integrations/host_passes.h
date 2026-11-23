#pragma once
#include "host_client.h"

class WaterHostPasses
{
public:
    WaterHostPasses() = default;
    WaterHostPasses(const WaterHostPasses&) = delete;
    WaterHostPasses& operator=(const WaterHostPasses&) = delete;
    ~WaterHostPasses() { Shutdown(); }
    bool Initialise(const WaterHostAPI* api, IDirect3DDevice9* device, const char* settings,const WaterHostDiagnosticsAPI* diagnostics=nullptr,const WaterHostTraceAPI* trace=nullptr,const WaterHostEnvironmentAPI* environment=nullptr,const WaterHostLiquidAPI* liquid=nullptr,const WaterHostCaptureAPI* capture=nullptr)
    {
        if (m_context) return m_device == device;
        if (!api || !device) return false;
        m_context = api->create(device, settings);
        if (!m_context) return false;
        m_api = api;m_diagnostics=diagnostics;m_trace=trace;m_environment=environment;m_liquidAPI=liquid;m_capture=capture;
        m_device = device;
        return true;
    }
    void PresentBegin(unsigned source,uint64_t frame){if(m_context&&m_diagnostics)m_diagnostics->present_begin(m_context,source,frame);}
    void PresentReady(){if(m_context&&m_diagnostics)m_diagnostics->present_ready(m_context);}
    void PresentEnd(HRESULT result){if(m_context&&m_diagnostics)m_diagnostics->present_end(m_context,int32_t(result));}
    uint64_t TraceBegin(){return m_context&&m_trace?m_trace->begin_call(m_context):0;}
    void TraceEnd(uint64_t token,const char* name){if(token&&m_context&&m_trace)m_trace->end_call(m_context,token,name);}
    bool Begin(const WaterHostFrameRef& frame)
    {
        End();
        return m_context && frame && m_api->begin_frame(m_context, frame.Get(), &m_stamp);
    }
    bool Current() const { return m_context && m_stamp.session; }
    WaterHostLiquidKind ClassifyLiquid(IDirect3DBaseTexture9* texture,const float* bounds=nullptr,int* ramp=nullptr)
    { return Current() && m_liquidAPI ? m_liquidAPI->classify(m_context,m_stamp,texture,bounds,ramp) : WaterHostLiquidUnknown; }
    bool ObserveLiquid(WaterHostLiquidKind kind,const float* bounds=nullptr)
    { return Current() && m_liquidAPI && m_liquidAPI->observe_draw(m_context,m_stamp,kind,bounds); }
    bool BeforeSurface(const WaterHostResources& resources,bool outdoors,WaterHostLiquidKind kind,const float* bounds=nullptr,int ramp=-1)
    { return Current() && m_liquidAPI && m_liquidAPI->before_surface(m_context,m_stamp,&resources,outdoors,kind,bounds,ramp); }
    bool AfterSurface(const WaterHostResources& resources,bool drawn)
    { return Current() && m_liquidAPI && m_liquidAPI->after_surface(m_context,m_stamp,&resources,drawn); }
    bool CompositeSurfaces(const WaterHostResources& resources)
    { return Current() && m_liquidAPI && m_liquidAPI->composite(m_context,m_stamp,&resources); }
    IDirect3DTexture9* EffectsMask(const WaterHostResources& resources,IDirect3DTexture9* hostMask)
    { return Current() && m_liquidAPI ? m_liquidAPI->effects_mask(m_context,m_stamp,&resources,hostMask) : nullptr; }
    bool SceneBeforeLiquid(IDirect3DTexture9*& colour, IDirect3DTexture9*& depth)
    {
        colour = depth = nullptr;
        return Current() && m_liquidAPI && m_liquidAPI->scene_before(m_context, m_stamp, &colour, &depth);
    }
    bool CapturePass(const char* name,bool after)
    { return Current()&&m_capture&&m_capture->pass(m_context,m_stamp,name,after?1:0); }
    WaterSceneryPoseRequests* PoseRequests(WaterHostModule::PoseRequestsFn query) const {return m_context && query?query(m_context,1,sizeof(WaterSceneryPoseRequests)):nullptr;}
    bool Ready() const { return m_context != nullptr; }
    bool Draw(WaterHostPass pass, const WaterHostResources& resources, bool shade = true)
    { return Current() && m_api->draw(m_context, m_stamp, pass, &resources, shade); }
    bool BeforeLiquid(const WaterHostResources& resources)
    {
        if (m_liquid) return true;
        m_liquid = Draw(WaterHostBeforeLiquid, resources);
        return m_liquid;
    }
    bool AfterLiquid(const WaterHostResources& resources, bool drewWater)
    {
        if (!m_liquid) return false;
        m_liquid = false;
        return Draw(WaterHostCompositeWater, resources, drewWater);
    }
    bool Environment(const WaterEnvironmentValues& values,IDirect3DTexture9* background,float viewOpticalDepth48=0,const WaterLampSamples& lamps={}){
        if(!Current()||!m_environment)return false;
        if(m_environment->abi>=4){
            const WaterHostLampEnvironment input={{{sizeof(WaterHostLampEnvironment),m_stamp,values,background},viewOpticalDepth48},lamps};
            return m_environment->set_environment(m_context,m_stamp,&input.fog.base)!=0;
        }
        if(m_environment->abi>=3){
            const WaterHostFogEnvironment input={{sizeof(WaterHostFogEnvironment),m_stamp,values,background},viewOpticalDepth48};
            return m_environment->set_environment(m_context,m_stamp,&input.base)!=0;
        }
        const WaterHostEnvironment input={sizeof(WaterHostEnvironment),m_stamp,values,background};
        return m_environment->set_environment(m_context,m_stamp,&input)!=0;
    }
    bool WantsEnvironment(bool& lamps){
        lamps=false;
        if(!Current()||!m_environment)return false;
        if(m_environment->abi>=4){
            WaterHostLampStatus status={{sizeof(WaterHostLampStatus),0,0,0},0};
            if(!m_environment->surface_status(m_context,m_stamp,&status.base))return false;
            lamps=status.wantsLamps!=0;
            return status.base.fallback!=WaterEnvironmentDisabled;
        }
        WaterHostSurfaceStatus status={};
        return SurfaceStatus(status)&&status.fallback!=WaterEnvironmentDisabled;
    }
    bool SurfaceStatus(WaterHostSurfaceStatus& status){
        status={sizeof(WaterHostSurfaceStatus),WaterHostSurfaceUnchanged,0,WaterEnvironmentNotProvided};
        return Current()&&m_environment&&m_environment->surface_status(m_context,m_stamp,&status);
    }
    bool Reflection(const WaterHostReflection& reflection)
    { return Current() && m_api->set_reflection(m_context, m_stamp, &reflection); }
    bool Fog(bool ready, bool spatialFog = false)
    {
        if (!Current()) return false;
        // Older cores reject the optional hint and still receive their usual fog policy.
        if (!ready && spatialFog && m_api->set_fog(m_context, m_stamp, WaterHostSpatialFog)) return true;
        return m_api->set_fog(m_context, m_stamp, ready ? WaterHostFogCoversWater : WaterHostNativeFog);
    }
    void End()
    {
        if (m_context) m_api->end_frame(m_context);
        m_stamp = {};
        m_liquid = false;
    }
    void Lost()
    {
        End();
        if (m_context) m_api->lost(m_context);
    }
    bool Restored() { return m_context && m_api->restored(m_context); }
    bool Shutdown()
    {
        if (!m_context) return true;
        if (!m_api->destroy(m_context)) return false;
        m_context = nullptr;
        m_api = nullptr;m_diagnostics=nullptr;m_trace=nullptr;m_environment=nullptr;m_liquidAPI=nullptr;m_capture=nullptr;
        m_device = nullptr;
        m_stamp = {};
        m_liquid = false;
        return true;
    }
private:
    const WaterHostAPI* m_api = nullptr;
    const WaterHostDiagnosticsAPI* m_diagnostics=nullptr;
    const WaterHostTraceAPI* m_trace=nullptr;
    const WaterHostEnvironmentAPI* m_environment=nullptr;
    const WaterHostLiquidAPI* m_liquidAPI=nullptr;
    const WaterHostCaptureAPI* m_capture=nullptr;
    WaterHostContext* m_context = nullptr;
    IDirect3DDevice9* m_device = nullptr;
    WaterHostStamp m_stamp = {};
    bool m_liquid = false;
};

template<class Host> class WaterDiagnosticCall {
public:
 WaterDiagnosticCall(Host& host,const char* name):m_host(host),m_name(name),m_token(host.TraceBegin()){}
 ~WaterDiagnosticCall(){m_host.TraceEnd(m_token,m_name);}
private:
 Host& m_host;const char* m_name;uint64_t m_token;
};
