#pragma once
#include <d3d9.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <vector>

// A liquid draw's vertices, as the host's draw call names them: a buffer draw reads stream 0 from base + first, a draw
// from client memory reads data with its stride. count: the vertices the draw uses (LiquidDrawVertices for a
// non-indexed draw). Shared by the hosts, which own the draw calls.
struct LiquidDraw {
    INT base=0;UINT first=0,count=0;const void* data=nullptr;UINT stride=0;
    UINT indexFirst=0,indexCount=0;const void* indices=nullptr;D3DFORMAT indexFormat=D3DFMT_UNKNOWN;
};

// The vertices a non-indexed draw of this many primitives uses.
inline UINT LiquidDrawVertices(D3DPRIMITIVETYPE type,UINT primitives)
{
    if(!primitives)return 0;
    const uint64_t count=primitives;
    uint64_t vertices=count;
    switch(type){
    case D3DPT_TRIANGLELIST:vertices=count*3;break;
    case D3DPT_TRIANGLESTRIP:case D3DPT_TRIANGLEFAN:vertices=count+2;break;
    case D3DPT_LINELIST:vertices=count*2;break;
    case D3DPT_LINESTRIP:vertices=count+1;break;
    default:break;
    }
    return vertices>std::numeric_limits<UINT>::max()?0:UINT(vertices);
}

// Standalone reads the device buffer directly. A queued host can supply its existing
// replay-position reader instead, avoiding driver readback while preserving draw order.
template<class Buffer> class LiquidBufferReader {
    Buffer* buffer;
    DWORD flags;
public:
    explicit LiquidBufferReader(Buffer* value,DWORD mode=D3DLOCK_READONLY):buffer(value),flags(mode){}
    HRESULT lock(UINT offset,UINT size,void** data){return buffer->Lock(offset,size,data,flags);}
    HRESULT unlock(){return buffer->Unlock();}
};

// Raw vertex bounds (low xyz, then high xyz), with position first in each vertex. Regional terrain liquid uses world
// coordinates; building liquid may use placement-local coordinates. Callers must verify the coordinate space before
// using these bounds for world-space visibility. The ocean and river styles are regional, so the
// core judges a draw over its whole extent, and works only on the part of the screen it covers. Device: a D3D9 device or
// a host's wrapper with the same GetStreamSource.
template<template<class> class Reader=LiquidBufferReader,class Device>
bool LiquidDrawBounds(Device* device,const LiquidDraw& draw,float* bounds)
{
    enum : UINT { kMaxVertices=1u<<16 };
    if(!bounds||!draw.count||draw.count>kMaxVertices)return false;
    bool valid=true;
    // The declared vertex range can include unused liquid-grid slots whose height is zero. Only the indices actually
    // drawn bound the surface; including those slots turns a flat pond into a huge box and defeats screen scissoring.
    std::vector<UINT> references;
    if(draw.indexCount){
        if(draw.indexCount>3*kMaxVertices)return false;
        auto indices=[&](const void* data,D3DFORMAT format){
            const UINT bytes=format==D3DFMT_INDEX16?2:format==D3DFMT_INDEX32?4:0;
            if(!data||!bytes)return false;
            // Adjacent triangles reuse the same grid vertices. Validate every index,
            // but reduce each referenced position only once. This is draw-local:
            // later buffer writes and replay-position readers still supply fresh data.
            std::array<std::uint64_t,kMaxVertices/64> seen;
            std::fill_n(seen.begin(),(draw.count+63)/64,std::uint64_t(0));
            references.reserve(std::min(draw.indexCount,draw.count));
            for(UINT i=0;i<draw.indexCount;++i){
                // Fixed copy widths compile to unaligned loads. A variable-sized
                // memcpy here otherwise calls the runtime for every triangle index.
                UINT index;
                if(bytes==2){std::uint16_t value;std::memcpy(&value,static_cast<const char*>(data)+size_t(i)*2,2);index=value;}
                else std::memcpy(&index,static_cast<const char*>(data)+size_t(i)*4,4);
                if(index<draw.first||uint64_t(index)-draw.first>=draw.count)return false;
                const UINT relative=index-draw.first;
                auto& word=seen[relative/64];const auto bit=std::uint64_t(1)<<(relative%64);
                if(!(word&bit)){word|=bit;references.push_back(relative);}
            }
            return true;
        };
        if(draw.indices){
            if(!indices(draw.indices,draw.indexFormat))return false;
        }else{
            IDirect3DIndexBuffer9* buffer=nullptr;
            if(FAILED(device->GetIndices(&buffer))||!buffer){if(buffer)buffer->Release();return false;}
            D3DINDEXBUFFER_DESC desc={};void* data=nullptr;
            bool ok=SUCCEEDED(buffer->GetDesc(&desc));
            const UINT bytes=desc.Format==D3DFMT_INDEX16?2:desc.Format==D3DFMT_INDEX32?4:0;
            const uint64_t start=uint64_t(draw.indexFirst)*bytes,size=uint64_t(draw.indexCount)*bytes;
            ok=ok&&bytes&&start+size<=desc.Size;
            {
                Reader<IDirect3DIndexBuffer9> read(buffer);
                const HRESULT locked=ok?read.lock(UINT(start),UINT(size),&data):E_FAIL;
                ok=SUCCEEDED(locked)&&indices(data,desc.Format);
                if(SUCCEEDED(locked)&&FAILED(read.unlock()))ok=false;
            }
            buffer->Release();
            if(!ok)return false;
        }
    }
    float* low=bounds;float* high=bounds+3;
    auto take=[&](const char* vertices,UINT stride){
        const UINT count=references.empty()?draw.count:UINT(references.size());
        for(UINT i=0;i<count;++i){
            const UINT vertex=references.empty()?i:references[i];
            float p[3];std::memcpy(p,vertices+size_t(vertex)*stride,sizeof p);
            for(unsigned c=0;c<3;++c){if(!std::isfinite(p[c]))valid=false;low[c]=i?std::min(low[c],p[c]):p[c];high[c]=i?std::max(high[c],p[c]):p[c];}
        }
    };
    if(draw.data){
        if(draw.stride<3*sizeof(float))return false;
        take(static_cast<const char*>(draw.data)+size_t(draw.first)*draw.stride,draw.stride);
    }else{
        IDirect3DVertexBuffer9* buffer=nullptr;UINT offset=0,stride=0;
        if(FAILED(device->GetStreamSource(0,&buffer,&offset,&stride))||!buffer||stride<3*sizeof(float)||int64_t(draw.first)+draw.base<0){
            if(buffer)buffer->Release();
            return false;
        }
        D3DVERTEXBUFFER_DESC desc={};
        const uint64_t start=uint64_t(offset)+uint64_t(int64_t(draw.first)+draw.base)*stride;
        const uint64_t size=uint64_t(draw.count-1)*stride+3*sizeof(float);
        void* data=nullptr;
        const bool inside=SUCCEEDED(buffer->GetDesc(&desc))&&start+size<=desc.Size;
        bool readOk=false;
        {
            Reader<IDirect3DVertexBuffer9> read(buffer);
            const HRESULT locked=inside?read.lock(UINT(start),UINT(size),&data):E_FAIL;
            readOk=SUCCEEDED(locked)&&data;
            if(readOk)take(static_cast<const char*>(data),stride);
            if(SUCCEEDED(locked)&&FAILED(read.unlock()))readOk=false;
        }
        buffer->Release();
        if(!readOk)return false;
    }
    for(unsigned c=0;c<6;++c)if(!std::isfinite(bounds[c]))return false;
    return valid;
}
