#pragma once
// Narrow CPU-only D3D9 declarations, copied to a temporary d3d9.h by the runner.
#include <cstdint>
using DWORD=std::uint32_t;using UINT=unsigned;using ULONG=unsigned;using HRESULT=std::int32_t;using BOOL=int;using HANDLE=void*;
using D3DFORMAT=unsigned;using D3DMULTISAMPLE_TYPE=unsigned;using D3DPOOL=unsigned;using D3DTEXTUREFILTERTYPE=unsigned;
using D3DRENDERSTATETYPE=unsigned;using D3DPRIMITIVETYPE=unsigned;using D3DSTATEBLOCKTYPE=unsigned;
constexpr HRESULT D3D_OK=0,D3DERR_NOTFOUND=-2,E_FAIL=-1;
constexpr BOOL FALSE=0;
constexpr D3DFORMAT D3DFMT_A8R8G8B8=21;
constexpr D3DFORMAT D3DFMT_D24S8=75;
constexpr D3DPOOL D3DPOOL_DEFAULT=0;
constexpr DWORD D3DUSAGE_RENDERTARGET=1,D3DRS_ALPHABLENDENABLE=27,D3DRS_FOGENABLE=28,D3DRS_COLORWRITEENABLE1=190;
constexpr DWORD D3DVERTEXTEXTURESAMPLER0=257;
constexpr DWORD TRUE=1,D3DCMP_ALWAYS=8,D3DCMP_EQUAL=3,D3DSTENCILOP_KEEP=1,D3DSTENCILOP_ZERO=2,D3DSTENCILOP_REPLACE=3;
constexpr DWORD D3DRS_STENCILENABLE=52,D3DRS_STENCILFAIL=53,D3DRS_STENCILZFAIL=54,D3DRS_STENCILPASS=55,D3DRS_STENCILFUNC=56,
    D3DRS_STENCILREF=57,D3DRS_STENCILMASK=58,D3DRS_STENCILWRITEMASK=59,D3DRS_TWOSIDEDSTENCILMODE=185,
    D3DRS_ZENABLE=7,D3DRS_ZWRITEENABLE=14,D3DRS_ALPHATESTENABLE=15,D3DRS_SCISSORTESTENABLE=174,D3DRS_COLORWRITEENABLE=168,
    D3DRS_CULLMODE=22,D3DRS_FILLMODE=8,D3DRS_CLIPPLANEENABLE=152,D3DRS_MULTISAMPLEMASK=162,D3DRS_MULTISAMPLEANTIALIAS=161;
constexpr DWORD D3DFVF_XYZRHW=4,D3DCULL_NONE=1,D3DFILL_SOLID=3,D3DCLEAR_TARGET=1;
constexpr D3DPRIMITIVETYPE D3DPT_TRIANGLESTRIP=5;
constexpr D3DSTATEBLOCKTYPE D3DSBT_ALL=1;
constexpr D3DMULTISAMPLE_TYPE D3DMULTISAMPLE_NONE=0;
constexpr D3DTEXTUREFILTERTYPE D3DTEXF_NONE=0;
inline bool FAILED(HRESULT h){return h<0;}inline bool SUCCEEDED(HRESULT h){return h>=0;}
struct RECT {int left,top,right,bottom;};
struct D3DRECT {int x1,y1,x2,y2;};
struct D3DVIEWPORT9 {DWORD X=0,Y=0,Width=0,Height=0;float MinZ=0,MaxZ=1;};
struct D3DSURFACE_DESC {D3DFORMAT Format=0;unsigned Type=0;DWORD Usage=0;D3DPOOL Pool=0;D3DMULTISAMPLE_TYPE MultiSampleType=0;DWORD MultiSampleQuality=0;UINT Width=0,Height=0;};
struct IUnknown {virtual ~IUnknown()=default;virtual ULONG AddRef()=0;virtual ULONG Release()=0;};
struct IDirect3DSurface9:IUnknown {virtual HRESULT GetDesc(D3DSURFACE_DESC*)=0;};
struct IDirect3DBaseTexture9:IUnknown {};
struct IDirect3DTexture9:IDirect3DBaseTexture9 {virtual HRESULT GetSurfaceLevel(UINT,IDirect3DSurface9**)=0;};
struct IDirect3DPixelShader9:IUnknown {virtual HRESULT GetFunction(void*,UINT*)=0;};
struct IDirect3DVertexShader9:IUnknown {};
struct IDirect3DStateBlock9:IUnknown {virtual HRESULT Capture()=0;virtual HRESULT Apply()=0;};
struct IDirect3DDevice9 {
    virtual ~IDirect3DDevice9()=default;
    virtual HRESULT CreateTexture(UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*)=0;
    virtual HRESULT CreateRenderTarget(UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*)=0;
    virtual HRESULT CreatePixelShader(const DWORD*,IDirect3DPixelShader9**)=0;
    virtual HRESULT GetRenderTarget(DWORD,IDirect3DSurface9**)=0;
    virtual HRESULT SetRenderTarget(DWORD,IDirect3DSurface9*)=0;
    virtual HRESULT GetDepthStencilSurface(IDirect3DSurface9**)=0;
    virtual HRESULT GetViewport(D3DVIEWPORT9*)=0;
    virtual HRESULT SetViewport(const D3DVIEWPORT9*)=0;
    virtual HRESULT GetRenderState(DWORD,DWORD*)=0;
    virtual HRESULT SetRenderState(DWORD,DWORD)=0;
    virtual HRESULT GetPixelShader(IDirect3DPixelShader9**)=0;
    virtual HRESULT SetPixelShader(IDirect3DPixelShader9*)=0;
    virtual HRESULT GetTexture(DWORD,IDirect3DBaseTexture9**)=0;
    virtual HRESULT SetTexture(DWORD,IDirect3DBaseTexture9*){return E_FAIL;}
    virtual HRESULT SetDepthStencilSurface(IDirect3DSurface9*){return E_FAIL;}
    virtual HRESULT SetVertexShader(IDirect3DVertexShader9*){return E_FAIL;}
    virtual HRESULT SetFVF(DWORD){return E_FAIL;}
    virtual HRESULT SetStreamSourceFreq(UINT,UINT){return E_FAIL;}
    virtual HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE,IDirect3DStateBlock9**){return E_FAIL;}
    virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE,UINT,const void*,UINT){return E_FAIL;}
    virtual HRESULT Clear(DWORD,const D3DRECT*,DWORD,DWORD,float,DWORD){return E_FAIL;}
    virtual HRESULT ColorFill(IDirect3DSurface9*,const RECT*,DWORD)=0;
    virtual HRESULT StretchRect(IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE)=0;
};
