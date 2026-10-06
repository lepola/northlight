#pragma once
// Bounded original vertex/index readback for the client's verified D9VK backend.
// Coordinates remain in the shader input basis. This does NOT establish a world
// transform, interpret skinning, or acquire geometry the game has never drawn.
#include <d3d9.h>
#include "upload_lock.h"
#include "lock_meter_readback.h"
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace northlight_geometry {
struct Position { float x,y,z; };
struct Mesh { std::vector<Position> positions; std::vector<std::uint32_t> indices; };
template<class T> struct ComRef {
    T* value=nullptr;
    ~ComRef(){if(value)value->Release();}
};

inline bool readIndexedPositions(IDirect3DDevice9* device,
    D3DPRIMITIVETYPE topology, INT baseVertex, UINT minVertex, UINT vertexCount,
    UINT startIndex, UINT primitiveCount, Mesh& output) {
    output.positions.clear(); output.indices.clear();
    if (!device || !primitiveCount || !vertexCount || vertexCount>131072 ||
        primitiveCount>262144 || (topology!=D3DPT_TRIANGLELIST && topology!=D3DPT_TRIANGLESTRIP)) return false;
    ComRef<IDirect3DVertexDeclaration9> declaration;
    if(FAILED(device->GetVertexDeclaration(&declaration.value))||!declaration.value)return false;
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]; UINT elementCount=MAXD3DDECLLENGTH+1;
    if(FAILED(declaration.value->GetDeclaration(elements,&elementCount)))return false;
    const D3DVERTEXELEMENT9* position=nullptr;
    for(UINT i=0;i<elementCount&&elements[i].Stream!=0xff;++i)
        if(elements[i].Usage==D3DDECLUSAGE_POSITION && elements[i].UsageIndex==0){position=&elements[i];break;}
    if(!position || position->Method!=D3DDECLMETHOD_DEFAULT ||
       (position->Type!=D3DDECLTYPE_FLOAT3&&position->Type!=D3DDECLTYPE_FLOAT4))return false;
    UINT frequency=0;
    if(FAILED(device->GetStreamSourceFreq(position->Stream,&frequency))||frequency!=1)return false;
    ComRef<IDirect3DVertexBuffer9> vertices; UINT streamOffset=0,stride=0;
    if(FAILED(device->GetStreamSource(position->Stream,&vertices.value,&streamOffset,&stride))||!vertices.value)return false;
    const unsigned componentBytes=position->Type==D3DDECLTYPE_FLOAT4?16:12;
    if(!stride||position->Offset+componentBytes>stride)return false;
    ComRef<IDirect3DIndexBuffer9> indices;
    if(FAILED(device->GetIndices(&indices.value))||!indices.value)return false;
    D3DINDEXBUFFER_DESC id={}; D3DVERTEXBUFFER_DESC vd={};
    if(FAILED(indices.value->GetDesc(&id))||FAILED(vertices.value->GetDesc(&vd)))return false;
    if(id.Format!=D3DFMT_INDEX16&&id.Format!=D3DFMT_INDEX32)return false;
    const UINT indexSize=id.Format==D3DFMT_INDEX16?2:4;
    const UINT indexCount=topology==D3DPT_TRIANGLELIST?primitiveCount*3:primitiveCount+2;
    const std::uint64_t indexOffset=std::uint64_t(startIndex)*indexSize;
    const std::uint64_t indexBytes=std::uint64_t(indexCount)*indexSize;
    if(indexOffset+indexBytes>id.Size)return false;
    void* raw=nullptr;
    if(FAILED(indices.value->Lock(UINT(indexOffset),UINT(indexBytes),&raw,NorthlightUpload::readBackLock()))||!raw)return false;
    NorthlightLockMeter::readBack(indices.value,indexBytes);
    std::vector<std::uint32_t> source(indexCount);
    bool valid=true;
    for(UINT i=0;i<indexCount;++i){
        if(indexSize==2){std::uint16_t value;std::memcpy(&value,(char*)raw+i*2,2);source[i]=value;}
        else std::memcpy(&source[i],(char*)raw+i*4,4);
        if(source[i]<minVertex||std::uint64_t(source[i])>=std::uint64_t(minVertex)+vertexCount)valid=false;
    }
    const HRESULT unlockIndices=indices.value->Unlock();
    if(!valid||FAILED(unlockIndices))return false;
    const std::int64_t firstVertex=std::int64_t(baseVertex)+minVertex;
    if(firstVertex<0)return false;
    const std::uint64_t firstByte=std::uint64_t(streamOffset)+std::uint64_t(firstVertex)*stride;
    const std::uint64_t lastByte=firstByte+std::uint64_t(vertexCount-1)*stride+position->Offset+componentBytes;
    if(lastByte>vd.Size||firstByte>=lastByte)return false;
    raw=nullptr;
    if(FAILED(vertices.value->Lock(UINT(firstByte),UINT(lastByte-firstByte),&raw,NorthlightUpload::readBackLock()))||!raw)return false;
    NorthlightLockMeter::readBack(vertices.value,lastByte-firstByte);
    output.positions.resize(vertexCount);
    for(UINT i=0;i<vertexCount;++i){
        float values[4]={0,0,0,1};
        std::memcpy(values,(char*)raw+std::size_t(i)*stride+position->Offset,componentBytes);
        if(!std::isfinite(values[0])||!std::isfinite(values[1])||!std::isfinite(values[2])||
           !std::isfinite(values[3])||std::fabs(values[3]-1.f)>.0001f)valid=false;
        output.positions[i]={values[0],values[1],values[2]};
    }
    const HRESULT unlockVertices=vertices.value->Unlock();
    if(!valid||FAILED(unlockVertices)){output.positions.clear();return false;}
    output.indices.reserve(std::size_t(primitiveCount)*3);
    for(UINT primitive=0;primitive<primitiveCount;++primitive){
        const UINT first=topology==D3DPT_TRIANGLELIST?primitive*3:primitive;
        auto a=source[first]-minVertex,b=source[first+1]-minVertex,c=source[first+2]-minVertex;
        if(topology==D3DPT_TRIANGLESTRIP&&(primitive&1))std::swap(a,b);
        if(a==b||b==c||c==a)continue;
        output.indices.push_back(a);output.indices.push_back(b);output.indices.push_back(c);
    }
    return !output.indices.empty();
}
} // namespace northlight_geometry
