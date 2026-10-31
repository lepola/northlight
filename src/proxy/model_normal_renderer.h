#pragma once
#include <d3d9.h>
#include <unordered_map>
#include "model_normal_patch.h"
#include "native_material_capture_patch.h"

// Independent of AutoBump and actor-shadow capture budgets. Replay only opaque
// native depth writers; the consumer rejects captures hidden by final depth.
class NorthlightModelNormals {
    template<class T> static void release(T*& p){if(p)p->Release();p=nullptr;}
    struct Vertex {IDirect3DVertexShader9 *source=nullptr,*shader=nullptr;};
    struct Pixel {IDirect3DPixelShader9 *source=nullptr,*shader=nullptr;unsigned control=0;};
    IDirect3DDevice9* d;
    std::unordered_map<IDirect3DVertexShader9*,Vertex> vertices;
    std::unordered_map<IDirect3DPixelShader9*,Pixel> pixels[2];
    IDirect3DTexture9* normal=nullptr;
    IDirect3DSurface9 *surface=nullptr,*depth=nullptr,*frameTarget=nullptr;
    UINT width=0,height=0;unsigned draws=0;bool cleared=false,bad=false;
    struct State {
        inline static constexpr D3DRENDERSTATETYPE keys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,
            D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,
            D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,
            D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3,D3DRS_WRAP5,D3DRS_WRAP7};
        static constexpr unsigned Count=sizeof(keys)/sizeof(keys[0]);
        IDirect3DDevice9* d;IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
        IDirect3DSurface9 *rt[4]={},*ds=nullptr;IDirect3DVertexBuffer9* stream=nullptr;IDirect3DIndexBuffer9* index=nullptr;
        UINT offset=0,stride=0;D3DVIEWPORT9 viewport{};DWORD values[Count]={};float constant[4]={};unsigned control;bool changed=false,up;
        State(IDirect3DDevice9* device,unsigned c,bool user):d(device),control(c),up(user){}
        bool save(){
            if(FAILED(d->GetVertexShader(&vs))||FAILED(d->GetPixelShader(&ps))||FAILED(d->GetViewport(&viewport))||
               FAILED(d->GetDepthStencilSurface(&ds))||FAILED(d->GetPixelShaderConstantF(control,constant,1)))return false;
            for(unsigned i=0;i<4;++i){auto hr=d->GetRenderTarget(i,&rt[i]);if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return false;}
            for(unsigned i=0;i<Count;++i)if(FAILED(d->GetRenderState(keys[i],values+i)))return false;
            return !up||(SUCCEEDED(d->GetStreamSource(0,&stream,&offset,&stride))&&SUCCEEDED(d->GetIndices(&index)));
        }
        bool restore(){
            if(!changed)return true;changed=false;
            auto retry=[](auto call){return SUCCEEDED(call())||SUCCEEDED(call());};bool ok=true;
            ok=retry([&]{return d->SetDepthStencilSurface(nullptr);})&&ok;
            for(unsigned i=1;i<4;++i)ok=retry([&]{return d->SetRenderTarget(i,nullptr);})&&ok;
            for(unsigned i=0;i<4;++i)ok=retry([&]{return d->SetRenderTarget(i,rt[i]);})&&ok;
            ok=retry([&]{return d->SetDepthStencilSurface(ds);})&&ok;
            ok=retry([&]{return d->SetViewport(&viewport);})&&ok;
            ok=retry([&]{return d->SetVertexShader(vs);})&&ok;ok=retry([&]{return d->SetPixelShader(ps);})&&ok;
            ok=retry([&]{return d->SetPixelShaderConstantF(control,constant,1);})&&ok;
            for(unsigned i=0;i<Count;++i)ok=retry([&]{return d->SetRenderState(keys[i],values[i]);})&&ok;
            if(up){ok=retry([&]{return d->SetStreamSource(0,stream,offset,stride);})&&ok;ok=retry([&]{return d->SetIndices(index);})&&ok;}
            return ok;
        }
        ~State(){restore();release(vs);release(ps);for(auto*& p:rt)release(p);release(ds);release(stream);release(index);}
    };
    Vertex* vertex(IDirect3DVertexShader9* source){
        auto it=vertices.find(source);if(it!=vertices.end())return &it->second;if(vertices.size()>=512)return nullptr;
        Vertex v;v.source=source;source->AddRef();UINT bytes=0;
        if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&bytes%4==0){
            std::vector<DWORD> code(bytes/4);std::vector<std::uint32_t> patched;
            if(SUCCEEDED(source->GetFunction(code.data(),&bytes))&&NorthlightModelNormal::vertex(reinterpret_cast<const uint32_t*>(code.data()),code.size(),patched))
                d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&v.shader);
        }
        return &vertices.emplace(source,v).first->second;
    }
    Pixel* pixel(IDirect3DPixelShader9* source,bool alpha){
        auto& map=pixels[alpha];auto it=map.find(source);if(it!=map.end())return &it->second;if(map.size()>=512)return nullptr;
        Pixel v;v.source=source;source->AddRef();UINT bytes=0;
        if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&bytes%4==0){
            std::vector<DWORD> code(bytes/4);std::vector<std::uint32_t> patched;
            if(SUCCEEDED(source->GetFunction(code.data(),&bytes))&&NorthlightNativeMaterialCapture::patch(reinterpret_cast<const uint32_t*>(code.data()),code.size(),patched,v.control,alpha,false,true))
                d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&v.shader);
        }
        return &map.emplace(source,v).first->second;
    }
    bool targets(UINT w,UINT h){
        if(w!=width||h!=height){reset();width=w;height=h;}
        if(normal&&surface&&depth)return true;
        const auto bytes=uint64_t(w)*h*8;MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
        if(bytes>(64ull<<20)||!GlobalMemoryStatusEx(&memory)||memory.ullAvailVirtual<bytes*2+(128ull<<20))return false;
        if(FAILED(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&normal,nullptr))||
           FAILED(normal->GetSurfaceLevel(0,&surface))||FAILED(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr))){reset();return false;}
        return true;
    }
public:
    explicit NorthlightModelNormals(IDirect3DDevice9* device):d(device){}
    ~NorthlightModelNormals(){reset();for(auto& v:vertices){release(v.second.source);release(v.second.shader);}for(auto& map:pixels)for(auto& v:map){release(v.second.source);release(v.second.shader);}}
    void endFrame(){draws=0;cleared=bad=false;release(frameTarget);}
    void reset(){endFrame();release(surface);release(normal);release(depth);width=height=0;}
    IDirect3DTexture9* texture()const{return draws&&!bad?normal:nullptr;}
    template<class Draw> void capture(IDirect3DVertexShader9* source,Draw draw,bool userMemory=false){
        if(bad||!source)return;
        DWORD z=0,write=0,blend=0,mask=0,alpha=0,ref=0,func=0,comparison=0;
        if(FAILED(d->GetRenderState(D3DRS_ZENABLE,&z))||FAILED(d->GetRenderState(D3DRS_ZWRITEENABLE,&write))||
           FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||FAILED(d->GetRenderState(D3DRS_COLORWRITEENABLE,&mask))||!z||!write||blend||!(mask&7))return;
        if(FAILED(d->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha))||FAILED(d->GetRenderState(D3DRS_ALPHAREF,&ref))||
           FAILED(d->GetRenderState(D3DRS_ALPHAFUNC,&func))||FAILED(d->GetRenderState(D3DRS_ZFUNC,&comparison)))return;
        if((alpha&&func!=D3DCMP_GREATER&&func!=D3DCMP_GREATEREQUAL)||(comparison!=D3DCMP_LESS&&comparison!=D3DCMP_LESSEQUAL&&comparison!=D3DCMP_EQUAL))return;
        auto* vs=vertex(source);if(!vs||!vs->shader)return;
        IDirect3DPixelShader9* native=nullptr;if(FAILED(d->GetPixelShader(&native))||!native)return;
        auto* ps=pixel(native,alpha!=0);release(native);if(!ps||!ps->shader)return;
        State saved(d,ps->control,userMemory);if(!saved.save()||!saved.rt[0])return;
        D3DSURFACE_DESC desc{};if(FAILED(saved.rt[0]->GetDesc(&desc))||saved.viewport.X||saved.viewport.Y||desc.Width!=saved.viewport.Width||desc.Height!=saved.viewport.Height)return;
        if(frameTarget&&frameTarget!=saved.rt[0])return;
        if(!targets(desc.Width,desc.Height)){bad=true;return;}
        if(!frameTarget){frameTarget=saved.rt[0];frameTarget->AddRef();}
        saved.changed=true;
        bool ok=SUCCEEDED(d->SetDepthStencilSurface(nullptr));
        for(unsigned i=1;i<4;++i)ok=SUCCEEDED(d->SetRenderTarget(i,nullptr))&&ok;
        ok=SUCCEEDED(d->SetRenderTarget(0,surface))&&ok;ok=SUCCEEDED(d->SetDepthStencilSurface(depth))&&ok;
        ok=SUCCEEDED(d->SetViewport(&saved.viewport))&&ok;
        const DWORD values[]={TRUE,TRUE,D3DCMP_LESSEQUAL,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,15,0,0,0,0,0};
        for(unsigned i=0;i<State::Count;++i)ok=SUCCEEDED(d->SetRenderState(State::keys[i],values[i]))&&ok;
        if(ok&&!cleared){ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));cleared=ok;}
        const float control[]={0,48.f,alpha?(float(ref)+(func==D3DCMP_GREATER?.5f:0.f))/255.f:.001f,alpha?1.f:0.f};
        ok=SUCCEEDED(d->SetVertexShader(vs->shader))&&ok;ok=SUCCEEDED(d->SetPixelShader(ps->shader))&&ok;
        ok=SUCCEEDED(d->SetPixelShaderConstantF(ps->control,control,1))&&ok;
        if(ok)ok=SUCCEEDED(draw());
        ok=saved.restore()&&ok;if(ok)++draws;else bad=true;
    }
};
