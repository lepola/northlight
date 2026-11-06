// Exercise the actual WorldRenderer capture method against the real mirror
// and its counted backend. The runner extracts the method without rewriting it.
#include "device_mirror.h"
#include "mirror_resource_forwarders.h"
#include "equipment_lights.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <unordered_map>
#include <string>
static unsigned tick=1000;
struct Surface:NorthlightMirrorResources::ForwardIDirect3DSurface9 {
    unsigned refs=1;
    explicit Surface(MirrorGate& gate):ForwardIDirect3DSurface9(nullptr,gate,nullptr,nullptr){}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs>1);return --refs;}
};
struct Backend:ForwardIDirect3DDevice9 {
    IDirect3DSurface9* depth=nullptr;float constants[256][4]{};unsigned gets=0;bool failNext=false,failGet=false;
    Backend():ForwardIDirect3DDevice9(nullptr){}
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* value)override{depth=value;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** value)override{++gets;if(failGet){failGet=false;return D3DERR_INVALIDCALL;}*value=depth;if(depth)depth->AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT start,const float* values,UINT count)override{std::memcpy(constants[start],values,count*16);if(failNext){failNext=false;return D3DERR_INVALIDCALL;}return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT start,float* values,UINT count)override{++gets;if(failGet){failGet=false;return D3DERR_INVALIDCALL;}std::memcpy(values,constants[start],count*16);return D3D_OK;}
};
namespace NorthlightDrawSnapshot {
template<class T> struct Ref {T* p=nullptr;T** out(){return &p;}~Ref(){if(p)p->Release();}};
}
struct CaptureWorld {
    MirrorDevice* d;
    struct {const DeviceMirror* mirror;} constantEpochSource;
    NorthlightEquipmentLights::Tracker equipmentLights;
    NorthlightEquipmentLights::CaptureBudget equipmentCapture;
    std::unordered_map<IDirect3DVertexShader9*,unsigned> equipmentShaders;
    std::string equipmentMap;
    struct Scene {std::string map="Northrend";std::vector<NorthlightLocalLights::Light> localLights;} scene;
    Scene* active=&scene;
    struct {float inverseView[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},camera[3]={};} context;
    float projection[3]={1,1,1};bool enabled=true,knownContext=true,actor=true;
    bool equipmentLightsOn()const{return enabled;}
    bool hasContext()const{return knownContext;}
    bool isSkinnedShader(IDirect3DVertexShader9*)const{return actor;}
#define GetTickCount() tick
#include "equipment_capture_body.inc"
#undef GetTickCount
};
int main(){
    using namespace NorthlightEquipmentLights;
    Backend backend;DeviceMirror mirror;MirrorDevice d(&backend,&mirror);Surface depth(mirror.gate),other(mirror.gate);
    auto* shader=reinterpret_cast<IDirect3DVertexShader9*>(std::uintptr_t(1));
    CaptureWorld world{};world.d=&d;world.constantEpochSource.mirror=&mirror;world.equipmentShaders[shader]=1;
    float projection[16]={1,0,0,0,0,1,0,0,0,0,1,1,0,0,1,0};
    float bank[44]={};bank[0]=.97254908f;bank[1]=.92941183f;bank[2]=.627451f;bank[16]=3;bank[18]=8;bank[36]=.7f;bank[40]=.03f;
    auto capture=[&]{MirrorGuard guard(mirror.gate);world.captureEquipmentLights(shader,&depth);};
    d.SetDepthStencilSurface(&depth);d.SetVertexShaderConstantF(2,projection,4);d.SetVertexShaderConstantF(17,bank,11);
    assert(!cachedBank(&mirror)); // Values are neither getter-proven nor borrowed under a gate.
    capture();assert(world.equipmentLights.count==1&&world.equipmentCapture.reads==1);
    const auto gets=backend.gets;
    for(unsigned i=0;i<500;++i)capture();
    assert(backend.gets==gets&&world.equipmentCapture.cached==500&&world.equipmentLights.bankDuplicates==500);
    // Hundreds of changed and repeated earlier banks cannot hide a late lamp.
    // Sources beyond the shared visibility sphere still pass admission but never fill the live pool.
    for(unsigned i=0;i<400;++i){bank[16]=MaxDistance+100.f+i;d.SetVertexShaderConstantF(17,bank,11);capture();}
    bank[16]=2;bank[18]=6;bank[0]=.4f;bank[1]=.2f;bank[2]=.1f;
    d.SetVertexShaderConstantF(17,bank,11);capture();
    assert(backend.gets==gets&&world.equipmentLights.count==2&&world.equipmentLights.banks>MemoBanks);
    assert(world.equipmentLights.entries[1].source.position[0]==2&&world.equipmentLights.entries[1].source.color[0]==.4f);
    // Only this frame's client-evaluated values are used; movement and brightness
    // keys do not become a baked pose or an average across model sequences.
    ++tick;bank[16]=2.1f;bank[0]=.2f;bank[1]=.1f;bank[2]=.05f;
    d.SetVertexShaderConstantF(17,bank,11);capture();
    assert(world.equipmentLights.count==2&&world.equipmentLights.entries[1].source.position[0]==2.1f&&world.equipmentLights.entries[1].source.color[0]==.2f);
    world.equipmentLights.publish(tick);tick+=50;world.equipmentLights.publish(tick);
    bank[0]=bank[1]=bank[2]=0;d.SetVertexShaderConstantF(17,bank,11);capture();
    tick+=350;world.equipmentLights.publish(tick);tick+=100;world.equipmentLights.publish(tick);
    assert(!world.equipmentLights.count&&!world.equipmentLights.outputCount);
    // Known wrong depth/projection are rejected without backend getters.
    d.SetDepthStencilSurface(&other);IDirect3DSurface9* read=nullptr;d.GetDepthStencilSurface(&read);read->Release();
    auto before=backend.gets;auto refused=world.equipmentCapture.domainRejected;capture();
    assert(backend.gets==before&&world.equipmentCapture.domainRejected==refused+1);
    d.SetDepthStencilSurface(&depth);d.GetDepthStencilSurface(&read);read->Release();
    projection[0]=2;d.SetVertexShaderConstantF(2,projection,4);before=backend.gets;capture();assert(backend.gets==before&&!world.equipmentLights.count);
    projection[0]=1;d.SetVertexShaderConstantF(2,projection,4);
    // Failed writes invalidate provenance. Getter failure never revives stale rows.
    world.equipmentLights.nextFrame();bank[0]=.2f;bank[1]=.1f;bank[2]=.05f;d.SetVertexShaderConstantF(17,bank,11);capture();
    assert(world.equipmentLights.count==1);world.equipmentLights.reset();bank[16]=20;
    backend.failNext=true;assert(FAILED(d.SetVertexShaderConstantF(17,bank,11)));
    assert(!mirror.vsFloatKnown[17]);before=backend.gets;backend.failGet=true;capture();
    assert(!world.equipmentLights.count&&backend.gets==before+1&&!backend.failGet);
    // Disabled mirror keeps the old bounded native fallback, even under a crowd.
    mirror.enabled=false;world.equipmentCapture.nextFrame();before=backend.gets;
    for(unsigned i=0;i<1000;++i)capture();
    assert(world.equipmentCapture.reads==MaxReadsPerFrame&&backend.gets-before<=MaxReadsPerFrame*3);
    mirror.enabled=true;mirror.invalidate();world.equipmentCapture.nextFrame();
    bank[0]=.2f;bank[1]=.1f;bank[2]=.05f;d.SetVertexShaderConstantF(17,bank,11);capture();before=backend.gets;
    // Representative cyclic banks (twice the old 16-bank cutoff), with 512 fixed sources.
    for(unsigned i=0;i<512;++i){Native n{};n.position[0]=1000.f+i;n.color[0]=1;n.attenuation[1]=.7f;world.scene.localLights.push_back(light(n,i,1));}
    for(unsigned cycle:{32u,96u}){auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<100;++frame){world.equipmentLights.nextFrame();world.equipmentCapture.nextFrame();
            for(unsigned i=0;i<1000;++i){bank[16]=float(i%cycle)*1.1f;d.SetVertexShaderConstantF(17,bank,11);capture();}}
        auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100;
        assert(backend.gets==before);
        std::printf("NATIVE_LIGHT_CAPTURE_PASS cachedCandidates=1000 fixedSources=512 memoCycle=%u backendGets=0 millisecondsPerFrame=%.4f\n",cycle,ms);}
}
