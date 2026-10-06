#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Test the production immediate readMesh method against mutable fake D3D buffers.

The separate Windows cross-compile validates actual D3D interfaces. This native
fixture supplies just those interfaces, allowing mutation/lifetime/failure tests
without starting Wine, a rendering device, or the game.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib,json,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
stub=r'''
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
'''
harness=r'''
#include "terrain_capture_bounds.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightTerrainCapture;
template<class Interface,class Description>struct Buffer:Interface{
    unsigned refs=1,locks=0,unlocks=0;bool failLock=false;Description desc;std::vector<unsigned char> bytes;
    unsigned AddRef()override{return ++refs;}unsigned Release()override{return --refs;}
    HRESULT GetDesc(Description* out)override{*out=desc;out->Size=unsigned(bytes.size());return D3D_OK;}
    HRESULT Lock(UINT offset,UINT size,void** out,DWORD flags)override{
        assert(flags==D3DLOCK_READONLY);if(failLock)return HRESULT(0x80004005u);
        assert(std::uint64_t(offset)+size<=bytes.size());++locks;*out=bytes.data()+offset;return D3D_OK;
    }
    HRESULT Unlock()override{++unlocks;assert(unlocks<=locks);return D3D_OK;}
};
struct Declaration:IDirect3DVertexDeclaration9{
    unsigned refs=1;
    unsigned AddRef()override{return ++refs;}unsigned Release()override{return --refs;}
    HRESULT GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count)override{
        assert(*count>=2);*count=2;out[0]={0,4,D3DDECLTYPE_FLOAT3,0,0,0};out[1]={0xff,0,0,0,0,0};return D3D_OK;
    }
};
struct Device:IDirect3DDevice9{
    Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC> vb;
    Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC> ib;
    Declaration declaration;float view[16]={};
    Device(){for(int i=0;i<4;++i)view[i*5]=1;vb.desc.Usage=ib.desc.Usage=D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY;vb.bytes.resize(16+8*24);setIndices16();}
    void setIndices16(){ib.desc.Format=D3DFMT_INDEX16;std::uint16_t idx[]={123,7,8,9};ib.bytes.resize(sizeof idx);std::memcpy(ib.bytes.data(),idx,sizeof idx);}
    void setIndices32(){ib.desc.Format=D3DFMT_INDEX32;std::uint32_t idx[]={123,7,8,9};ib.bytes.resize(sizeof idx);std::memcpy(ib.bytes.data(),idx,sizeof idx);}
    void put(unsigned actualIndex,Position p){std::memcpy(vb.bytes.data()+16+actualIndex*24+4,&p,12);}
    HRESULT GetVertexShaderConstantF(UINT first,float*out,UINT count)override{assert(first==0&&count==4);std::memcpy(out,view,64);return 0;}
    HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9**out)override{*out=&declaration;declaration.AddRef();return 0;}
    HRESULT GetStreamSourceFreq(UINT stream,UINT*out)override{assert(stream==0);*out=1;return 0;}
    HRESULT GetStreamSource(UINT stream,IDirect3DVertexBuffer9**out,UINT*offset,UINT*stride)override{assert(stream==0);*out=&vb;vb.AddRef();*offset=16;*stride=24;return 0;}
    HRESULT GetIndices(IDirect3DIndexBuffer9**out)override{*out=&ib;ib.AddRef();return 0;}
};
int main(){
    Device d;constexpr double origin=17066.666666666666,step=100./3.;int x=547,y=820;
    float loX=float(origin-(y+1)*step),hiX=float(origin-y*step),loY=float(origin-(x+1)*step),hiY=float(origin-x*step);
    d.put(4,{loX,loY,3});d.put(5,{hiX,loY,4});d.put(6,{loX,hiY,5});
    FrameCache cache;MeshSnapshot first,second;Diagnostics info;
    assert(cache.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,first,&info));
    assert(info.reason==RejectReason::None&&info.dynamicSnapshot);
    assert(first.positions.size()==3&&first.indices==std::vector<std::uint32_t>({0,1,2}));
    assert(first.bounds.chunks.size()==1&&first.bounds.chunks[0].x==x&&first.bounds.chunks[0].y==y);
    assert(first.positions[0].z==3&&d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1);
    // Same VB/IB, same offsets and draw: overwrite models the next DISCARD data.
    d.put(4,{loX,loY,31});
    assert(cache.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,second,&info));
    assert(first.positions[0].z==3&&second.positions[0].z==31);assert(d.vb.locks==2);
    d.setIndices32();
    assert(cache.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,second,&info));
    assert(second.positions[0].z==31&&second.indices.size()==3);
    float expected[16];std::memcpy(expected,d.view,64);d.view[12]=1;
    assert(!cache.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,expected,second,&info));
    assert(info.reason==RejectReason::ViewMismatch&&info.viewComponent==12&&info.viewDelta==1);
    d.view[12]=0;unsigned invalid=100;std::memcpy(d.ib.bytes.data()+4,&invalid,4);
    assert(!cache.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,second,&info));
    assert(info.reason==RejectReason::IndexRange&&second.positions.empty());
    d.setIndices16();d.vb.failLock=true;
    assert(!cache.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,second,&info));
    assert(info.reason==RejectReason::VertexLock&&info.hr==HRESULT(0x80004005u));d.vb.failLock=false;
    Limits limits;limits.maxReadBytesPerFrame=1;FrameCache tiny(limits);
    assert(!tiny.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,second,&info));
    assert(info.reason==RejectReason::ByteBudget);
    assert(d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
    assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1);
    cache.clearFrame();assert(cache.bytesRead()==0);
    // UP pointer denotes vertex zero even when the API's minimum is nonzero.
    std::vector<unsigned char> userVertices(10*24);
    auto putUP=[&](unsigned index,Position p){std::memcpy(userVertices.data()+index*24+4,&p,12);};
    putUP(7,{loX,loY,71});putUP(8,{hiX,loY,81});putUP(9,{loX,hiY,91});
    const std::uint16_t indices16[]={7,8,9};const std::uint32_t indices32[]={7,8,9};
    assert(cache.readMeshUP(&d,D3DPT_TRIANGLELIST,7,3,1,indices16,D3DFMT_INDEX16,userVertices.data(),24,d.view,first,&info));
    assert(info.userPointerSnapshot&&first.positions[0].z==71&&first.positions[2].z==91);
    putUP(7,{loX,loY,72});
    assert(cache.readMeshUP(&d,D3DPT_TRIANGLELIST,7,3,1,indices32,D3DFMT_INDEX32,userVertices.data(),24,d.view,second,&info));
    assert(first.positions[0].z==71&&second.positions[0].z==72);
    assert(!cache.readMeshUP(&d,D3DPT_TRIANGLELIST,8,2,1,indices16,D3DFMT_INDEX16,userVertices.data(),24,d.view,second,&info));
    assert(info.reason==RejectReason::IndexRange);
    assert(!cache.readMeshUP(&d,D3DPT_TRIANGLELIST,7,3,1,indices16,D3DFMT_INDEX16,userVertices.data(),8,d.view,second,&info));
    assert(info.reason==RejectReason::Stride);
    // Nonindexed UP needs neither a bound index nor a bound vertex buffer.
    putUP(0,{loX,loY,1});putUP(1,{hiX,loY,2});putUP(2,{loX,hiY,3});putUP(3,{hiX,hiY,4});
    assert(cache.readPrimitiveUP(&d,D3DPT_TRIANGLELIST,1,userVertices.data(),24,d.view,second,&info));
    assert(second.indices.size()==3&&second.positions[0].z==1);
    assert(cache.readPrimitiveUP(&d,D3DPT_TRIANGLESTRIP,2,userVertices.data(),24,d.view,second,&info));
    assert(second.indices==std::vector<std::uint32_t>({0,1,2,2,1,3}));
    assert(second.bounds.chunks.size()==1);
    assert(d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
    assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1);
    std::puts("Passed: dynamic buffer snapshots remain immutable; repeated draw re-reads mutations; base/stride/offset, 16/32-bit indices, diagnostics, budgets, locks and references.");
    std::puts("Passed: indexed UP absolute MinVertexIndex semantics, 16/32-bit user indices, copied memory mutation, nonindexed UP triangle lists/strips, range/stride rejection.");
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-terrain-snapshot-') as directory:
    directory=Path(directory);(directory/'d3d9.h').write_text(stub);(directory/'test.cpp').write_text(harness)
    subprocess.run(['clang++','-std=c++17','-O2','-Wall','-Wextra','-I',str(directory),*fp.test_include_flags(),str(directory/'test.cpp'),'-o',str(directory/'test')],check=True)
    result=subprocess.run([str(directory/'test')],check=True,capture_output=True,text=True);print(result.stdout,end='')
report={'source_sha256':hashlib.sha256(fp.src('terrain_capture_bounds.h').read_bytes()).hexdigest(),
        'dynamic_snapshot_mutation_test':True,'base_stride_offset_index16_index32_test':True,
        'reject_reason_hresult_test':True,'lock_reference_balance_test':True,
        'indexed_UP_minimum_vertex_absolute_index_test':True,'indexed_UP_index16_index32_test':True,
        'nonindexed_UP_list_strip_test':True,'UP_memory_mutation_test':True,
        'byte_budget_test':True,'native_fake_D3D_fixture':True,'game_launched':False}
(fp.output_dir()/'terrain-snapshot-validation.json').write_text(json.dumps(report,indent=2)+'\n')
