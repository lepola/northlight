
#pragma once
#include <cstdint>
using UINT=unsigned;using INT=int;using DWORD=unsigned;using HRESULT=std::int32_t;
constexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu),E_POINTER=HRESULT(0x80004003u);
inline bool FAILED(HRESULT value){return value<0;}
constexpr unsigned D3DUSAGE_DYNAMIC=0x200,D3DUSAGE_WRITEONLY=8,D3DLOCK_READONLY=0x10,D3DLOCK_NOOVERWRITE=0x1000;
enum D3DPOOL{D3DPOOL_DEFAULT=0,D3DPOOL_MANAGED=1};
constexpr unsigned MAXD3DDECLLENGTH=64,D3DDECLTYPE_FLOAT3=2,D3DDECLTYPE_FLOAT4=3,D3DDECLMETHOD_DEFAULT=0,D3DDECLUSAGE_POSITION=0;
enum D3DPRIMITIVETYPE{D3DPT_TRIANGLELIST=4,D3DPT_TRIANGLESTRIP=5};
enum D3DFORMAT{D3DFMT_INDEX16=101,D3DFMT_INDEX32=102};
struct D3DVERTEXELEMENT9{std::uint16_t Stream,Offset;std::uint8_t Type,Method,Usage,UsageIndex;};
struct D3DVERTEXBUFFER_DESC{DWORD Usage=0;D3DPOOL Pool=D3DPOOL_DEFAULT;UINT Size=0;};
struct D3DINDEXBUFFER_DESC{DWORD Usage=0;D3DPOOL Pool=D3DPOOL_DEFAULT;UINT Size=0;D3DFORMAT Format=D3DFMT_INDEX16;};
struct IRef{virtual unsigned AddRef()=0;virtual unsigned Release()=0;};
struct IDirect3DVertexBuffer9:IRef{virtual HRESULT GetDesc(D3DVERTEXBUFFER_DESC*)=0;virtual HRESULT Lock(UINT,UINT,void**,DWORD)=0;virtual HRESULT Unlock()=0;};
struct IDirect3DIndexBuffer9:IRef{virtual HRESULT GetDesc(D3DINDEXBUFFER_DESC*)=0;virtual HRESULT Lock(UINT,UINT,void**,DWORD)=0;virtual HRESULT Unlock()=0;};
struct IDirect3DVertexDeclaration9:IRef{virtual HRESULT GetDeclaration(D3DVERTEXELEMENT9*,UINT*)=0;};
struct IDirect3DDevice9{
virtual HRESULT GetVertexShaderConstantF(UINT,float*,UINT)=0;
virtual HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9**)=0;
virtual HRESULT GetStreamSourceFreq(UINT,UINT*)=0;
virtual HRESULT GetStreamSource(UINT,IDirect3DVertexBuffer9**,UINT*,UINT*)=0;
virtual HRESULT GetIndices(IDirect3DIndexBuffer9**)=0;
};
