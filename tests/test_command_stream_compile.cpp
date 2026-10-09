// Compile-only check of src/generated/command_stream.inl and the stream headers against the real d3d9.h (32-bit Windows,
// zig c++ -target x86-windows-gnu): the macro classes, the dispatch and the sync switch must instantiate. Never linked or run.
#include <d3d9.h>
#include "command_queue.h"
#include "command_stream.inl"
#include "stream_device.h"
using namespace NorthlightStream;

struct HostBase {
    Queue q;
    Queue& streamQueue(){return q;}
    struct Scope{};Scope callScope(){return {};}   // 0.3.204 (task 21): the generated bodies open a timing scope
    template<Cmd C,class... A> bool redundant(CmdTag<C>,A&&...){return false;}
    template<Cmd C,class... A> void observe(CmdTag<C>,A&&...){}
    template<Cmd C,class... A> bool answer(CmdTag<C>,A&&...){return false;}
    template<Cmd C,class... A> typename MethodTraits<C>::Ret local(CmdTag<C>,A&&...){return typename MethodTraits<C>::Ret();}
    template<Cmd C,class... A> typename MethodTraits<C>::Ret syncCall(CmdTag<C> t,A... a){return runSync(q,t,a...);}
    template<Cmd C,class... A> typename MethodTraits<C>::Ret syncGet(CmdTag<C> t,A... a){return runSync(q,t,a...);}
};
// Abstract on purpose: the custom methods are hand-written in the real classes.
struct ProxyDevice:IDirect3DDevice9,HostBase{NORTHLIGHT_STREAM_DEVICE_METHODS};
struct ProxySwapChain:IDirect3DSwapChain9,HostBase{NORTHLIGHT_STREAM_SWAPCHAIN_METHODS};
struct ProxyVertexBuffer:IDirect3DVertexBuffer9,HostBase{NORTHLIGHT_STREAM_VERTEXBUFFER_METHODS};
struct ProxyIndexBuffer:IDirect3DIndexBuffer9,HostBase{NORTHLIGHT_STREAM_INDEXBUFFER_METHODS};
struct ProxySurface:IDirect3DSurface9,HostBase{NORTHLIGHT_STREAM_SURFACE_METHODS};
struct ProxyTexture:IDirect3DTexture9,HostBase{NORTHLIGHT_STREAM_TEXTURE_METHODS};
struct ProxyCubeTexture:IDirect3DCubeTexture9,HostBase{NORTHLIGHT_STREAM_CUBETEXTURE_METHODS};
struct ProxyVolumeTexture:IDirect3DVolumeTexture9,HostBase{NORTHLIGHT_STREAM_VOLUMETEXTURE_METHODS};
struct ProxyVolume:IDirect3DVolume9,HostBase{NORTHLIGHT_STREAM_VOLUME_METHODS};
struct ProxyVertexShader:IDirect3DVertexShader9,HostBase{NORTHLIGHT_STREAM_VERTEXSHADER_METHODS};
struct ProxyPixelShader:IDirect3DPixelShader9,HostBase{NORTHLIGHT_STREAM_PIXELSHADER_METHODS};
struct ProxyVertexDeclaration:IDirect3DVertexDeclaration9,HostBase{NORTHLIGHT_STREAM_VERTEXDECLARATION_METHODS};
struct ProxyStateBlock:IDirect3DStateBlock9,HostBase{NORTHLIGHT_STREAM_STATEBLOCK_METHODS};
struct ProxyQuery:IDirect3DQuery9,HostBase{NORTHLIGHT_STREAM_QUERY_METHODS};

struct Tr {
    IDirect3DDevice9* dev=nullptr;
    IDirect3DDevice9* device(){return dev;}
    template<class T> T* inner(T* p){return p;}
    template<class T> T* toProxy(T* p){return p;}
    void result(Cmd,HRESULT){}
    void skipped(Cmd){}
    IDirect3DDevice9* ext(){return nullptr;}
    template<class T> T* raw(T* p,bool& ok){ok=true;return p;}
    void direct(){}
};
// The whole stream (proxies, StreamState, StreamDevice, replay thread) instantiates against the real SDK header.
NorthlightStream::StreamDevice* buildStream(IDirect3DDevice9* target,IDirect3D9* parent,const D3DPRESENT_PARAMETERS* pp){
    StreamDevice::Options options;const char* reason=nullptr;return StreamDevice::make(target,parent,pp,options,&reason);
}
bool instantiate(const CommandHeader* h,SyncCall& sc){
    Tr tr;
    return dispatchGenerated(h,tr)||executeSync(sc,tr);
}
