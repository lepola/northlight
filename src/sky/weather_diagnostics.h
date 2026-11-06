#pragma once
#include <cmath>
#include "surface_frame.h"
#include "quality_settings.h"
#include "weather_state.h"
#include "world_lighting_capture.h"

// Explicit weather snapshots reuse the lighting capture's checked depth copy,
// MSAA color resolve and full state/constant restoration. No work is scheduled here.
namespace NorthlightWeatherDiagnostics {
struct Frame {
    const NorthlightSurfaceFrame* constants=nullptr;
    const NorthlightQuality::Settings* quality=nullptr;
    const NorthlightWeather::State* weather=nullptr;
    unsigned rendererFrame=0,width=0,height=0;
    unsigned sourceSamples=0;
    float layerGate=0,heat=0;
    bool skySeen=false,linearHdr=false,nameMaskRequired=false,lensExposureKnown=false,lensExposed=false;
    bool multisampleVerified=false;
    const char* unavailable=nullptr;
};
inline bool capture(IDirect3DDevice9* device,const std::string& directory,const Frame& frame,
                    IDirect3DTexture9* depth,IDirect3DSurface9* scene,IDirect3DSurface9* nameStencil){
    bool finiteConstants=frame.constants!=nullptr;
    if(frame.constants)for(const auto& row:frame.constants->c)for(float value:row)finiteConstants=std::isfinite(value)&&finiteConstants;
    const bool unverifiedMultisample=frame.sourceSamples&&!frame.multisampleVerified;
    const bool available=finiteConstants&&!frame.unavailable&&!unverifiedMultisample;
    bool depthSaved=false,sceneSaved=false,maskSaved=false;
    if(available){
        NorthlightLightingCapture::Capture record(device,directory,1,0,frame.rendererFrame,frame.width,frame.height,"weather-inputs");
        depthSaved=record.sampledDepthSaved("weather-depth",depth);
        sceneSaved=record.imageSaved("weather-scene",scene);
        if(frame.nameMaskRequired)maskSaved=record.nameStencilSaved("weather-name-mask",nameStencil);
        record.finish(depthSaved&&sceneSaved&&(!frame.nameMaskRequired||maskSaved));
    }
    const bool complete=available&&depthSaved&&sceneSaved&&(!frame.nameMaskRequired||maskSaved);
    FILE* file=NorthlightFiles::open((directory+"/capture-1-weather.json").c_str(),"wb");if(!file)return false;
    auto finite=[](float v){return std::isfinite(v)?v:0.f;};
    const auto* state=frame.weather;const auto* q=frame.quality;
    const char* preset=!q||q->preset==NorthlightQuality::Preset::Quality?"Quality":q->preset==NorthlightQuality::Preset::Balanced?"Balanced":"Performance";
    std::fprintf(file,"{\"schema\":\"northlight-weather-capture-v1\",\"renderer_frame\":%u,\"width\":%u,\"height\":%u,\"kind\":\"%s\",\"intensity\":%.9g,\"blend\":%.9g,\"layer_gate\":%.9g,\"heat\":%.9g,\"sky_seen\":%s,\"weather_enabled\":%s,\"lens_exposed\":%s,\"preset\":\"%s\",\"constants\":",
        frame.rendererFrame,frame.width,frame.height,state?NorthlightWeather::kindName(state->kind):"none",state?finite(state->intensity):0.f,state?finite(state->blend):0.f,
        finite(frame.layerGate),finite(frame.heat),frame.skySeen?"true":"false",q&&q->weather?"true":"false",frame.lensExposureKnown?(frame.lensExposed?"true":"false"):"null",preset);
    if(finiteConstants){std::fputc('[',file);for(unsigned i=0;i<10;++i){std::fprintf(file,"%s[",i?",":"");for(unsigned k=0;k<4;++k)std::fprintf(file,"%s%.9g",k?",":"",frame.constants->c[i][k]);std::fputc(']',file);}std::fputc(']',file);}
    else std::fputs("null",file);
    std::fputs(",\"quality\":",file);
    if(q)std::fprintf(file,"{\"Weather\":%u,\"RainFog\":%u,\"RainDropSize\":%u,\"SnowFlakeSize\":%u,\"RainLens\":%u,\"SnowLens\":%u,\"RainLensStrength\":%u,\"SnowLensStrength\":%u,\"RainLensSize\":%u,\"SnowLensSize\":%u,\"RainRivulets\":%u,\"SnowFrost\":%u,\"SnowFrostStrength\":%u,\"SandLens\":%u,\"SandLensStrength\":%u,\"SandLensSize\":%u,\"WeatherWindDirection\":%u,\"WeatherWindStrength\":%u,\"WeatherCameraMotion\":%u}",
        q->weather,q->rainFog,q->rainDropSize,q->snowFlakeSize,q->rainLens,q->snowLens,q->rainLensStrength,q->snowLensStrength,q->rainLensSize,q->snowLensSize,
        q->rainRivulets,q->snowFrost,q->snowFrostStrength,q->sandLens,q->sandLensStrength,q->sandLensSize,q->weatherWindDirection,q->weatherWindStrength,q->weatherCameraMotion);
    else std::fputs("null",file);
    std::fprintf(file,",\"depth_file\":%s,\"scene_file\":%s,\"name_mask_file\":%s,\"scene_linear_hdr\":%s,\"name_mask_required\":%s,\"stencil_exported\":%s,\"mask_resolved_coverage\":true,\"per_sample\":false,\"depth_reconstructed\":false,\"lens_state_exported\":false,\"whole_scene_replay\":false,\"fps_eligible\":false,\"source_samples\":%u,\"complete\":%s,\"status\":\"%s\",\"reason\":\"%s\"}\n",
        depthSaved?"\"capture-1-weather-depth.fgr\"":"null",sceneSaved?"\"capture-1-weather-scene.fgr\"":"null",maskSaved?"\"capture-1-weather-name-mask.fgr\"":"null",
        frame.linearHdr?"true":"false",frame.nameMaskRequired?"true":"false",maskSaved?"true":"false",frame.sourceSamples,complete?"true":"false",available?"captured":"unavailable",
        frame.unavailable?frame.unavailable:!finiteConstants?"non-finite-or-missing-context":unverifiedMultisample?"multisample-capture-not-verified":complete?"":"required-export-failed");
    const bool written=std::ferror(file)==0;return std::fclose(file)==0&&written&&complete;
}
}
