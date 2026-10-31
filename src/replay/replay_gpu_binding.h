#pragma once
#include "draw_snapshot.h"
namespace NorthlightReplayGPU {
struct Placement {UINT vertexBase=0,indexStart=0;};
// A read-only residency lookup never admits, uploads, waits or changes draw state.
struct ResidentSource {
    void* context=nullptr;
    bool (*lookup)(void*,const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>&,
                   IDirect3DVertexBuffer9* (&)[4],IDirect3DIndexBuffer9*&,Placement&)=nullptr;
    bool borrow(const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& mesh,
                IDirect3DVertexBuffer9* (&vertices)[4],IDirect3DIndexBuffer9*& indices,Placement& at)const{
        at={};return mesh&&mesh->sharedCapture&&lookup&&lookup(context,mesh,vertices,indices,at);
    }
};
template<class Cache> ResidentSource residentSource(Cache& cache){
    return {&cache,[](void* context,const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& mesh,
                      IDirect3DVertexBuffer9* (&vertices)[4],IDirect3DIndexBuffer9*& indices,Placement& at){
        return static_cast<Cache*>(context)->borrow(mesh,vertices,indices,at);
    }};
}
}
