#pragma once
#include <d3d9.h>
#include "memory_footprint.h"

// Logical resource sizes, not resident VRAM or measured system backing. The
// runtime/driver chooses placement, padding, renaming and managed residency.
// Query descriptions only: never Lock, read back, flush or issue GPU queries.
namespace NorthlightResourceMemory {
enum Group {Geometry,ProbeTextures,ShadowMaps,PostTargets,Noise,Dynamic,GroupCount};
struct Inventory {
    NorthlightMemoryFootprint::Counter identities;
    uint64_t pools[4]={},groups[GroupCount]={};unsigned unknown=0;
    static uint64_t texelBytes(D3DFORMAT format){
        switch(format){
        case D3DFMT_L8:case D3DFMT_A8:return 1;
        case D3DFMT_R16F:case D3DFMT_D16:case D3DFMT_A8L8:return 2;
        case D3DFMT_R32F:case D3DFMT_G16R16F:case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:case D3DFMT_D24S8:case D3DFMT_D24X8:case D3DFMT_D32:return 4;
        case D3DFMT_A16B16G16R16F:case D3DFMT_G32R32F:return 8;
        case D3DFMT_A32B32G32R32F:return 16;
        default:return 0;
        }
    }
    void add(D3DPOOL pool,Group group,uint64_t bytes){
        if(unsigned(pool)>=4){++unknown;return;}pools[pool]+=bytes;groups[group]+=bytes;
    }
    template<class Texture> void texture(Texture* resource,Group group){
        if(!identities.visit(resource))return;
        for(UINT level=0;level<resource->GetLevelCount();++level){
            D3DSURFACE_DESC desc={};if(FAILED(resource->GetLevelDesc(level,&desc))){++unknown;continue;}
            const auto size=texelBytes(desc.Format);if(!size){++unknown;continue;}
            add(desc.Pool,group,uint64_t(desc.Width)*desc.Height*size);
        }
    }
    template<class Volume> void volume(Volume* resource,Group group){
        if(!identities.visit(resource))return;
        for(UINT level=0;level<resource->GetLevelCount();++level){
            D3DVOLUME_DESC desc={};if(FAILED(resource->GetLevelDesc(level,&desc))){++unknown;continue;}
            const auto size=texelBytes(desc.Format);if(!size){++unknown;continue;}
            add(desc.Pool,group,uint64_t(desc.Width)*desc.Height*desc.Depth*size);
        }
    }
    template<class Cube> void cube(Cube* resource,Group group){
        if(!identities.visit(resource))return;
        for(UINT level=0;level<resource->GetLevelCount();++level){
            D3DSURFACE_DESC desc={};if(FAILED(resource->GetLevelDesc(level,&desc))){++unknown;continue;}
            const auto size=texelBytes(desc.Format);if(!size){++unknown;continue;}
            add(desc.Pool,group,6ull*desc.Width*desc.Height*size);
        }
    }
    template<class Surface> void surface(Surface* resource,Group group){
        if(!identities.visit(resource))return;
        D3DSURFACE_DESC desc={};if(FAILED(resource->GetDesc(&desc))){++unknown;return;}
        const auto size=texelBytes(desc.Format);
        if(!size||desc.MultiSampleType==D3DMULTISAMPLE_NONMASKABLE){++unknown;return;}
        const auto samples=desc.MultiSampleType==D3DMULTISAMPLE_NONE?1u:unsigned(desc.MultiSampleType);
        add(desc.Pool,group,uint64_t(desc.Width)*desc.Height*samples*size);
    }
    template<class Buffer,class Description> void buffer(Buffer* resource,Group group,Description desc){
        if(!identities.visit(resource))return;
        if(FAILED(resource->GetDesc(&desc))){++unknown;return;}add(desc.Pool,group,desc.Size);
    }
    bool complete()const{return !unknown&&identities.complete();}
};
}
