#include "shadow_transition.h"
#include "shadow_transition_math.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using HRESULT=int32_t;using DWORD=uint32_t;using UINT=unsigned;
constexpr HRESULT S_OK=0,E_FAIL=-1,E_OUTOFMEMORY=-2;
static bool SUCCEEDED(HRESULT h){return h>=0;}static bool FAILED(HRESULT h){return h<0;}
constexpr unsigned FALSE=0,TRUE=1;
enum {D3DUSAGE_RENDERTARGET=1,D3DFMT_R32F,D3DPOOL_DEFAULT,D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,
    D3DRS_ALPHABLENDENABLE,D3DRS_SCISSORTESTENABLE,D3DFVF_XYZRHW,D3DFVF_TEX1=16,
    D3DTSS_TEXCOORDINDEX,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE,
    D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE,
    D3DTADDRESS_CLAMP,D3DTEXF_POINT,D3DTEXF_NONE,D3DTEXF_LINEAR};
static const DWORD kWorldLightingFadeShader[]={1},kWorldFogFadeShader[]={2},kFogCloudsFadeShader[]={3};
struct Device;
struct Ref {
    unsigned refs=1,id;explicit Ref(unsigned i):id(i){}virtual ~Ref()=default;
    virtual unsigned Release(){assert(refs);return --refs;}
    unsigned AddRef(){assert(refs);return ++refs;}
};
struct IDirect3DSurface9:Ref {using Ref::Ref;};
struct IDirect3DPixelShader9:Ref {using Ref::Ref;};
struct IDirect3DTexture9:Ref {
    Device& device;IDirect3DSurface9* surface;
    IDirect3DTexture9(unsigned id,Device& d,IDirect3DSurface9* s):Ref(id),device(d),surface(s){}
    unsigned Release()override{const unsigned n=Ref::Release();if(!n)surface->Release();return n;}
    HRESULT GetSurfaceLevel(UINT level,IDirect3DSurface9** out);
};
template<class T>static void drop(T*& p){if(p){p->Release();p=nullptr;}}
struct Device {
    std::vector<std::unique_ptr<Ref>> objects;
    IDirect3DTexture9* textures[16]={};IDirect3DSurface9* target=nullptr,*depth=nullptr;
    IDirect3DPixelShader9* shader=nullptr;
    float constants[256][4]={};DWORD samplers[16][32]={},states[32]={};
    unsigned calls=0,resourceCalls=0,failResource=0,failCall=0,draws=0;bool partialFailure=false,failureHit=false;
    const char* failOperation=nullptr;
    struct Capture {unsigned first=0,second=0,target=0,shader=0;float constants[8]={};} capture;
    template<class T,class... A>T* make(A&&... a){
        auto p=std::make_unique<T>(unsigned(objects.size()+1),std::forward<A>(a)...);auto* raw=p.get();objects.push_back(std::move(p));return raw;
    }
    IDirect3DTexture9* texture(){return make<IDirect3DTexture9>(*this,make<IDirect3DSurface9>());}
    unsigned live()const{unsigned n=0;for(const auto& p:objects)n+=p->refs>0;return n;}
    bool resource(){++calls;return ++resourceCalls==failResource;}
    HRESULT operation(const char* name){++calls;const bool fail=calls==failCall||(failOperation&&std::strcmp(failOperation,name)==0);
        failureHit|=fail;return fail?E_FAIL:S_OK;}
    template<class T>static void assign(T*& slot,T* value){if(value)value->AddRef();if(slot)slot->Release();slot=value;}
    HRESULT CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,unsigned fmt,unsigned pool,IDirect3DTexture9** out,void*){
        assert(w==1024&&h==1024&&levels==1&&usage==D3DUSAGE_RENDERTARGET&&fmt==D3DFMT_R32F&&pool==D3DPOOL_DEFAULT);
        *out=nullptr;const bool fail=resource();if(!fail||partialFailure)*out=texture();return fail?E_OUTOFMEMORY:S_OK;
    }
    HRESULT CreatePixelShader(const DWORD*,IDirect3DPixelShader9** out){
        *out=nullptr;const bool fail=resource();if(!fail||partialFailure)*out=make<IDirect3DPixelShader9>();return fail?E_OUTOFMEMORY:S_OK;
    }
    HRESULT SetTexture(unsigned s,IDirect3DTexture9* t){assert(s<16);const auto hr=operation("texture");if(SUCCEEDED(hr))assign(textures[s],t);return hr;}
    HRESULT SetRenderTarget(unsigned slot,IDirect3DSurface9* t){assert(slot==0);const auto hr=operation("target");if(SUCCEEDED(hr))assign(target,t);return hr;}
    HRESULT SetDepthStencilSurface(IDirect3DSurface9* s){const auto hr=operation("depth");if(SUCCEEDED(hr))assign(depth,s);return hr;}
    HRESULT SetPixelShader(IDirect3DPixelShader9* s){const auto hr=operation("shader");if(SUCCEEDED(hr))assign(shader,s);return hr;}
    HRESULT SetPixelShaderConstantF(unsigned first,const float* data,unsigned count){
        assert(first+count<=256);const auto hr=operation("constant");if(SUCCEEDED(hr))std::memcpy(constants[first],data,count*16);return hr;
    }
    HRESULT SetRenderState(unsigned state,DWORD value){const auto hr=operation("state");if(SUCCEEDED(hr))states[state]=value;return hr;}
    HRESULT SetSamplerState(unsigned s,unsigned state,DWORD value){assert(s<16&&state<32);const auto hr=operation("sampler");if(SUCCEEDED(hr))samplers[s][state]=value;return hr;}
    HRESULT SetVertexShader(void*){return operation("vertex");}
    HRESULT SetFVF(DWORD){return operation("fvf");}
    HRESULT SetStreamSourceFreq(unsigned s,UINT freq){assert(s==0&&freq==1);return operation("freq");}
    HRESULT SetTextureStageState(unsigned,unsigned,DWORD){return operation("stage");}
    HRESULT quad(UINT w,UINT h){
        assert(w==1024&&h==1024);const auto hr=operation("draw");if(FAILED(hr))return hr;
        assert(target&&target->refs&&shader&&shader->refs&&textures[0]&&textures[1]);
        capture.first=textures[0]->id;capture.second=textures[1]->id;capture.target=target->id;capture.shader=shader->id;
        std::memcpy(capture.constants,constants[0],sizeof capture.constants);++draws;return S_OK;
    }
    void unbind(){for(auto*& t:textures)assign(t,static_cast<IDirect3DTexture9*>(nullptr));assign(target,static_cast<IDirect3DSurface9*>(nullptr));
        assign(depth,static_cast<IDirect3DSurface9*>(nullptr));assign(shader,static_cast<IDirect3DPixelShader9*>(nullptr));}
};
HRESULT IDirect3DTexture9::GetSurfaceLevel(UINT level,IDirect3DSurface9** out){
    assert(refs&&level==0);*out=nullptr;const bool fail=device.resource();if(!fail||device.partialFailure){surface->AddRef();*out=surface;}return fail?E_OUTOFMEMORY:S_OK;
}
struct World {
    Device* d;struct {bool shadows=true,gi=true,fog=true;} effects;bool failed=false;
    static constexpr UINT ShadowCacheMargin=128,ShadowCacheSize=1280;
    struct Key {bool valid=true;NorthlightWorldMath::ShadowFrame frame;} shadowCacheKey[4];
    IDirect3DTexture9 *shadowCache[4]={},*shadow[4]={},*neutralWhite=nullptr;
    IDirect3DPixelShader9* unionPS=nullptr;float sourceMatrices[2][2][16]={};unsigned logs=0;
    void logf(const char*,...){++logs;}
    HRESULT quad(UINT w,UINT h){return d->quad(w,h);}
#include "world_shadow_transition.inl"
    explicit World(Device& device):d(&device){
        neutralWhite=d->texture();unionPS=d->make<IDirect3DPixelShader9>();
        for(unsigned slot=0;slot<4;++slot){shadowCache[slot]=d->texture();shadow[slot]=d->texture();
            const auto frame=NorthlightWorldMath::shadowFrame({-10432,145,87},{.3f,-.4f,.7f},slot%2?192.f:48.f);
            shadowCacheKey[slot].frame=frame;shadowTransition[slot].working=frame;shadowTransition[slot].workingValid=true;
            NorthlightWorldMath::shadowMatrixFrom(frame,slot%2?192.f:48.f,sourceMatrices[slot/2][slot%2]);}
    }
    ~World(){d->unbind();releaseShadowTransitions();drop(neutralWhite);drop(unionPS);for(auto*& t:shadowCache)drop(t);for(auto*& t:shadow)drop(t);assert(!d->live());}
    void fade(unsigned slot){auto& state=shadowTransition[slot].state;state.request({1,0,0},0);assert(state.commit({1,0,0},0,false));
        state.request({0,1,0},10);assert(state.commit({0,1,0},10,true));state.request({0,0,1},510);assert(state.fading());}
};
static void inactive(){
    Device d;World w(d);const auto calls=d.calls;
    for(int i=0;i<100;++i){w.bindShadowTransition(0);w.bindShadowTransition(1);}
    assert(d.calls==calls&&d.resourceCalls==0);
    w.fade(0);w.effects.shadows=false;w.bindShadowTransition(0);assert(d.calls==calls);
    w.effects.shadows=true;w.shadowTransitionFailed=true;w.bindShadowTransition(0);assert(d.calls==calls);
    w.resetShadowTransitions();assert(d.calls==calls&&!w.shadowTransition[0].state.valid());
}
static void allocations(){
    unsigned checked=0;
    {Device d;World w(d);
     for(int slot=0;slot<4;++slot)assert(w.shadowTransitionResources(slot));
     const auto resources=d.resourceCalls,calls=d.calls;
     for(unsigned i=0;i<100;++i)assert(w.shadowTransitionResources(i%4));
     assert(d.resourceCalls==resources&&d.calls==calls);
     for(int slot=0;slot<4;++slot)assert(w.preserveShadowStatic(slot));
     assert(d.resourceCalls==resources); // First direction steps allocate nothing after initial-cache preparation.
    }
    for(bool partial:{false,true})for(unsigned fail=1;fail<=7;++fail){
        Device d;World w(d);const auto baseline=d.live();d.failResource=fail;d.partialFailure=partial;
        assert(!w.shadowTransitionResources(0));assert(w.shadowTransitionFailed&&w.shadowTransitionFailures==1&&w.logs==1);
        assert(!w.failed&&w.effects.shadows&&w.effects.gi&&w.effects.fog&&d.live()==baseline);
        const auto calls=d.calls;assert(!w.shadowTransitionResources(0));w.bindShadowTransition(0);assert(d.calls==calls);
        w.releaseShadowTransitions();assert(!w.shadowTransitionFailed&&d.calls==calls);++checked;
    }
    // OOM in a second slot retires an already allocated/active first slot too.
    for(unsigned fail=1;fail<=4;++fail){
        Device d;World w(d);const auto baseline=d.live();assert(w.shadowTransitionResources(0));w.fade(0);
        d.failResource=d.resourceCalls+fail;assert(!w.shadowTransitionResources(1));
        assert(!w.shadowTransition[0].state.fading()&&!w.failed&&d.live()==baseline);++checked;
    }
    std::printf("PASS shadow transition optional allocation: %u shader/texture/surface OOM and partial-COM-return cases, native effects retained, references balanced\n",checked);
}
static void capture(){
    for(int slot=0;slot<4;++slot){
        Device d;World w(d);auto& f=w.shadowTransition[slot];auto& key=w.shadowCacheKey[slot];
        f.working.cx+=3*f.working.texel;f.working.cy-=2*f.working.texel;f.working.cz+=7;
        assert(w.preserveShadowStatic(slot));assert(d.draws==1);
        assert(d.capture.first==w.neutralWhite->id&&d.capture.second==w.shadowCache[slot]->id&&d.capture.second!=w.shadow[slot]->id);
        assert(d.capture.target==f.historySurface->id&&d.capture.shader==w.unionPS->id);
        assert(!d.textures[0]&&!d.textures[1]);
        assert(d.capture.constants[0]==131&&d.capture.constants[1]==130&&d.capture.constants[3]==1280);
        assert(std::fabs(d.capture.constants[2]-7*NorthlightWorldMath::InverseShadowDepth)<1e-6);
        assert(!std::memcmp(&f.previous,&f.working,sizeof f.working));
        float expected[16];NorthlightWorldMath::shadowMatrixFrom(f.working,slot%2?192.f:48.f,expected);
        assert(!std::memcmp(expected,f.previousMatrix,sizeof expected));
        assert(d.states[D3DRS_ZENABLE]&&d.states[D3DRS_ZWRITEENABLE]);
        assert(d.samplers[0][D3DSAMP_MINFILTER]==D3DTEXF_LINEAR);
        const auto calls=d.calls;key.valid=false;assert(!w.preserveShadowStatic(slot)&&d.calls==calls);
        key.valid=true;f.workingValid=false;assert(!w.preserveShadowStatic(slot)&&d.calls==calls);
        f.workingValid=true;f.working.cx=key.frame.cx+129*f.working.texel;
        assert(!w.preserveShadowStatic(slot)&&d.calls==calls);
    }
    for(const char* operation:{"depth","target","constant","shader","draw","texture","state","sampler","vertex","fvf","freq","stage"}){
        Device d;World w(d);auto& f=w.shadowTransition[0];f.previousMatrix[0]=42;f.previous.cx=99;d.failOperation=operation;
        assert(!w.preserveShadowStatic(0));assert(f.previousMatrix[0]==42&&f.previous.cx==99);
        assert(!d.textures[0]&&!d.textures[1]&&!w.failed&&w.effects.shadows&&w.effects.gi&&w.effects.fog);
    }
    std::puts("PASS static history capture: neutral white + exact old static crop, no old combined map, checked failure paths, guards and unbinding");
}
static void everyCaptureFailure(){
    unsigned captureCalls=0;
    {Device d;World w(d);assert(w.shadowTransitionResources(0));const auto before=d.calls;
        assert(w.preserveShadowStatic(0));captureCalls=d.calls-before;}
    for(unsigned nth=1;nth<=captureCalls;++nth){
        Device d;World w(d);assert(w.shadowTransitionResources(0));auto& f=w.shadowTransition[0];
        f.previous.cx=99;f.previousMatrix[0]=42;d.failCall=d.calls+nth;
        assert(!w.preserveShadowStatic(0)&&d.failureHit);
        assert(f.previous.cx==99&&f.previousMatrix[0]==42);
        assert(!w.failed&&w.effects.shadows&&w.effects.gi&&w.effects.fog&&d.draws<=1);
        for(auto* texture:d.textures)if(texture)assert(texture->refs); // cleanup failure may retain a device reference
    }
    std::printf("PASS static history failures: all %u setup/draw/cleanup API positions reject history publication; native effects and COM ownership retained\n",captureCalls);
}
static void bindingsAndLifetime(){
    Device d;World w(d);assert(w.shadowTransitionResources(0)&&w.shadowTransitionResources(1));
    w.fade(0);w.fade(1);
    for(int slot=0;slot<2;++slot){auto& f=w.shadowTransition[slot];f.previous=f.working;
        NorthlightWorldMath::shadowMatrixFrom(f.previous,slot?192.f:48.f,f.previousMatrix);}
    w.bindShadowTransition(0);
    for(int slot=0;slot<2;++slot){const auto& f=w.shadowTransition[slot];assert(d.textures[6+slot]==f.history&&d.textures[8+slot]==f.dynamic);
        assert(!std::memcmp(d.constants[80+slot*4],f.previousMatrix,64));}
    assert(d.constants[89][0]==.5f&&d.constants[89][1]==.5f);
    float expected[16];const auto& near=w.shadowTransition[0];ShadowTransitionMath::reproject(near.previous,w.sourceMatrices[0][0],48,expected);
    assert(!std::memcmp(d.constants[90],expected,sizeof expected));
    // Reset removes all sampling eligibility but keeps optional allocations warm.
    const unsigned live=d.live(),calls=d.calls;w.resetShadowTransitions();assert(d.live()==live&&d.calls==calls);
    w.bindShadowTransition(0);assert(d.calls==calls);
    // Device bindings hold references just as COM does, so release cannot free a
    // currently bound object behind the device; subsequent inactive bind is a no-op.
    auto* held=d.textures[6];w.releaseShadowTransitions();assert(held->refs==1);
    assert(!w.shadowTransition[0].history);w.bindShadowTransition(0);assert(d.calls==calls);
    d.unbind();assert(held->refs==0);
    // A new resource epoch gets different live objects; it inherits no fade.
    assert(w.shadowTransitionResources(0));assert(w.shadowTransition[0].history!=held);
    const auto before=d.calls;w.bindShadowTransition(0);assert(d.calls==before);
    std::puts("PASS transition binding/lifetime: exact constants and resources, inactive zero calls, reset/release/recreate with COM binding references");
}
int main(){inactive();allocations();capture();everyCaptureFailure();bindingsAndLifetime();}
