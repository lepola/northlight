#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
using HRESULT=int32_t;using LONG=int32_t;using ULONG=uint32_t;using DWORD=uint32_t;using UINT=uint32_t;using INT=int32_t;using BOOL=int32_t;using WINBOOL=BOOL;using REFIID=const int&;
#define STDMETHODCALLTYPE
#define __uuidof(T) iid_##T
constexpr int iid_IUnknown=1,iid_IDirect3DStateBlock9=2,iid_IDirect3DDevice9=3;
constexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=-1,D3DERR_NOTFOUND=-2,E_NOINTERFACE=-3,E_POINTER=-4,E_OUTOFMEMORY=-5,E_FAIL=-6,S_OK=0;
constexpr BOOL TRUE=1,FALSE=0;
inline bool SUCCEEDED(HRESULT h){return h>=0;}inline bool FAILED(HRESULT h){return h<0;}
inline LONG InterlockedIncrement(LONG* p){return __sync_add_and_fetch(p,1);}inline LONG InterlockedDecrement(LONG* p){return __sync_sub_and_fetch(p,1);}
using D3DSAMPLERSTATETYPE=unsigned;using D3DRENDERSTATETYPE=unsigned;using D3DFORMAT=unsigned;using D3DPRIMITIVETYPE=unsigned;using D3DSTATEBLOCKTYPE=unsigned;
constexpr D3DSTATEBLOCKTYPE D3DSBT_ALL=1,D3DSBT_PIXELSTATE=2,D3DSBT_VERTEXSTATE=3;
constexpr D3DPRIMITIVETYPE D3DPT_TRIANGLELIST=4;constexpr D3DFORMAT D3DFMT_INDEX16=101;
constexpr D3DRENDERSTATETYPE D3DRS_ZENABLE=7,D3DRS_ALPHABLENDENABLE=27,D3DRS_ALPHATESTENABLE=15,D3DRS_POINTSIZE=154,D3DRS_ADAPTIVETESS_Y=181;
constexpr D3DSAMPLERSTATETYPE D3DSAMP_ADDRESSU=1,D3DSAMP_ADDRESSV=2;
struct D3DVIEWPORT9 {DWORD X=0,Y=0,Width=0,Height=0;float MinZ=0,MaxZ=1;};
struct D3DPRESENT_PARAMETERS {UINT BackBufferWidth=0,BackBufferHeight=0;};
struct IUnknown {virtual HRESULT QueryInterface(REFIID,void** out){if(out)*out=nullptr;return E_NOINTERFACE;}virtual ULONG AddRef()=0;virtual ULONG Release()=0;protected:virtual ~IUnknown()=default;};
struct IDirect3DVertexShader9:IUnknown{};struct IDirect3DPixelShader9:IUnknown{};struct IDirect3DVertexDeclaration9:IUnknown{};
struct IDirect3DVertexBuffer9:IUnknown{};struct IDirect3DIndexBuffer9:IUnknown{};struct IDirect3DBaseTexture9:IUnknown{};struct IDirect3DSurface9:IUnknown{};
struct IDirect3DDevice9;
struct IDirect3DStateBlock9:IUnknown {virtual HRESULT GetDevice(IDirect3DDevice9**)=0;virtual HRESULT Capture()=0;virtual HRESULT Apply()=0;};
#define MIRROR_API_METHODS(X) \
 X(SetVertexShader,(IDirect3DVertexShader9* a),(a)) \
 X(GetVertexShader,(IDirect3DVertexShader9** a),(a)) \
 X(SetPixelShader,(IDirect3DPixelShader9* a),(a)) \
 X(GetPixelShader,(IDirect3DPixelShader9** a),(a)) \
 X(SetVertexDeclaration,(IDirect3DVertexDeclaration9* a),(a)) \
 X(GetVertexDeclaration,(IDirect3DVertexDeclaration9** a),(a)) \
 X(SetFVF,(DWORD a),(a)) \
 X(GetFVF,(DWORD* a),(a)) \
 X(SetStreamSource,(UINT a,IDirect3DVertexBuffer9* b,UINT c,UINT d),(a,b,c,d)) \
 X(GetStreamSource,(UINT a,IDirect3DVertexBuffer9** b,UINT* c,UINT* d),(a,b,c,d)) \
 X(SetStreamSourceFreq,(UINT a,UINT b),(a,b)) \
 X(GetStreamSourceFreq,(UINT a,UINT* b),(a,b)) \
 X(SetIndices,(IDirect3DIndexBuffer9* a),(a)) \
 X(GetIndices,(IDirect3DIndexBuffer9** a),(a)) \
 X(SetTexture,(DWORD a,IDirect3DBaseTexture9* b),(a,b)) \
 X(GetTexture,(DWORD a,IDirect3DBaseTexture9** b),(a,b)) \
 X(SetSamplerState,(DWORD a,D3DSAMPLERSTATETYPE b,DWORD c),(a,b,c)) \
 X(GetSamplerState,(DWORD a,D3DSAMPLERSTATETYPE b,DWORD* c),(a,b,c)) \
 X(SetRenderState,(D3DRENDERSTATETYPE a,DWORD b),(a,b)) \
 X(GetRenderState,(D3DRENDERSTATETYPE a,DWORD* b),(a,b)) \
 X(SetViewport,(const D3DVIEWPORT9* a),(a)) \
 X(GetViewport,(D3DVIEWPORT9* a),(a)) \
 X(SetRenderTarget,(DWORD a,IDirect3DSurface9* b),(a,b)) \
 X(GetRenderTarget,(DWORD a,IDirect3DSurface9** b),(a,b)) \
 X(SetDepthStencilSurface,(IDirect3DSurface9* a),(a)) \
 X(GetDepthStencilSurface,(IDirect3DSurface9** a),(a)) \
 X(SetVertexShaderConstantF,(UINT a,const float* b,UINT c),(a,b,c)) \
 X(GetVertexShaderConstantF,(UINT a,float* b,UINT c),(a,b,c)) \
 X(SetVertexShaderConstantI,(UINT a,const int* b,UINT c),(a,b,c)) \
 X(GetVertexShaderConstantI,(UINT a,int* b,UINT c),(a,b,c)) \
 X(SetVertexShaderConstantB,(UINT a,const WINBOOL* b,UINT c),(a,b,c)) \
 X(GetVertexShaderConstantB,(UINT a,WINBOOL* b,UINT c),(a,b,c)) \
 X(SetPixelShaderConstantF,(UINT a,const float* b,UINT c),(a,b,c)) \
 X(GetPixelShaderConstantF,(UINT a,float* b,UINT c),(a,b,c)) \
 X(CreateStateBlock,(D3DSTATEBLOCKTYPE a,IDirect3DStateBlock9** b),(a,b)) \
 X(BeginStateBlock,(),()) \
 X(EndStateBlock,(IDirect3DStateBlock9** a),(a)) \
 X(DrawPrimitiveUP,(D3DPRIMITIVETYPE a,UINT b,const void* c,UINT d),(a,b,c,d)) \
 X(DrawPrimitive,(D3DPRIMITIVETYPE a,UINT b,UINT c),(a,b,c)) \
 X(DrawIndexedPrimitive,(D3DPRIMITIVETYPE a,INT b,UINT c,UINT d,UINT e,UINT f),(a,b,c,d,e,f)) \
 X(ProcessVertices,(UINT a,UINT b,UINT c,IDirect3DVertexBuffer9* d,IDirect3DVertexDeclaration9* e,DWORD f),(a,b,c,d,e,f)) \
 X(DrawIndexedPrimitiveUP,(D3DPRIMITIVETYPE a,UINT b,UINT c,UINT d,const void* e,D3DFORMAT f,const void* g,UINT h),(a,b,c,d,e,f,g,h)) \
 X(Reset,(D3DPRESENT_PARAMETERS* a),(a)) \
 X(SetSoftwareVertexProcessing,(WINBOOL a),(a))
struct IDirect3DDevice9:IUnknown {
#define API_PURE(n,p,a) virtual HRESULT n p=0;
 MIRROR_API_METHODS(API_PURE)
#undef API_PURE
};
struct ForwardIDirect3DDevice9:IDirect3DDevice9 {
 IDirect3DDevice9* real;explicit ForwardIDirect3DDevice9(IDirect3DDevice9* r):real(r){}
 HRESULT QueryInterface(REFIID i,void** o)override{return real->QueryInterface(i,o);}ULONG AddRef()override{return real->AddRef();}ULONG Release()override{return real->Release();}
#define API_FORWARD(n,p,a) HRESULT n p override{return real->n a;}
 MIRROR_API_METHODS(API_FORWARD)
#undef API_FORWARD
};
