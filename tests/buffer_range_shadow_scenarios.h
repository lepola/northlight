#pragma once
#include "buffer_range_shadow.h"

static void bufferRangeShadowModel(){
    BufferRangeShadow cache;
    constexpr UINT length=6u<<20;
    std::vector<unsigned char> expected(length,0),known(length,0),locked(8192);
    std::uint32_t random=713;
    auto next=[&](){random=random*1664525u+1013904223u;return random;};
    for(unsigned step=0;step<12000;++step){
        if(step%73==0){cache.discard();std::fill(known.begin(),known.end(),0);}
        const UINT size=1+next()%UINT(locked.size()),off=next()%(length-size);
        if(cache.canRead(off,size)){
            cache.read(locked.data(),off,size);
            for(UINT j=0;j<size;++j)if(known[off+j])CHECK(locked[j]==expected[off+j]);
        }else std::memcpy(locked.data(),expected.data()+off,size);
        // Partial writes must preserve the rest of the locked range.
        locked[0]=static_cast<unsigned char>(next());locked[size-1]=static_cast<unsigned char>(next());
        std::memcpy(expected.data()+off,locked.data(),size);std::memset(known.data()+off,1,size);
        cache.remember(off,size,locked.data(),step%97?RangeShadowBudgetBytes-cache.capacity():0);
        CHECK(cache.capacity()<=RangeShadowItemBytes);
        if(cache.covers(off,size)){
            cache.read(locked.data(),off,size);CHECK(!std::memcmp(locked.data(),expected.data()+off,size));
        }
        if(step%131==0){cache.invalidate();CHECK(!cache.canRead(off,size));}
    }
    // Adjacent and overlapping intervals can grow in either direction.
    cache.clear();unsigned char a[64],b[128];std::memset(a,0x31,sizeof a);
    cache.remember(128,64,a,RangeShadowBudgetBytes);cache.remember(64,64,a,RangeShadowBudgetBytes);
    CHECK(cache.covers(64,128));cache.read(b,64,128);for(auto c:b)CHECK(c==0x31);
    cache.invalidate();CHECK(!cache.canRead(64,128));
    cache.discard();CHECK(cache.canRead(8192,64));cache.read(a,8192,64);for(auto c:a)CHECK(c==0);
    cache.remember(8192,64,a,RangeShadowBudgetBytes);CHECK(cache.canRead(16384,64));
    cache.remember(16384,64,a,RangeShadowBudgetBytes);CHECK(cache.covers(8192,8256));
    // A failed growth drops all proof, including DISCARD's undefined remainder.
    cache.remember(20000,8192,locked.data(),0);CHECK(!cache.canRead(8192,64));
    cache.clear();CHECK(cache.capacity()==0);
}

static void oversizedBufferRangeShadows(){
    gTrace.clear();Rig rig(true);auto& core=rig.core();auto& counters=core.q.stats;
    constexpr UINT length=40u<<20;
    IDirect3DVertexBuffer9* vb=nullptr;
    CHECK(rig.dev->CreateVertexBuffer(length,D3::kUsageDynamic,0,(D3DPOOL)0,&vb,nullptr)==D3D_OK);
    rig.sync();const auto initial=get(counters.syncCalls);void* data=nullptr;
    CHECK(vb->Lock(128,4096,&data,0)==D3D_OK);std::memset(data,0x39,4096);CHECK(vb->Unlock()==D3D_OK);
    // Previously each re-lock of this 40 MiB buffer drained the replay thread
    // twice, even though only a small, already known range was being touched.
    for(unsigned i=0;i<400;++i){
        CHECK(vb->Lock(128,4096,&data,D3::kLockNoOverwrite)==D3D_OK);
        CHECK(static_cast<unsigned char*>(data)[4095]==0x39);
        static_cast<unsigned char*>(data)[i%256]=static_cast<unsigned char>(i);
        CHECK(vb->Unlock()==D3D_OK);
    }
    CHECK(get(counters.syncCalls)==initial);
    // An unknown disjoint range still performs the exact synchronous lock.
    CHECK(vb->Lock(8192,64,&data,0)==D3D_OK);std::memset(data,0x57,64);CHECK(vb->Unlock()==D3D_OK);
    CHECK(get(counters.syncCalls)==initial+2);
    CHECK(vb->Lock(8192,64,&data,0)==D3D_OK&&static_cast<unsigned char*>(data)[63]==0x57);CHECK(vb->Unlock()==D3D_OK);
    CHECK(get(counters.syncCalls)==initial+2);
    rig.sync();const auto rotationStart=get(counters.syncCalls);
    for(unsigned frame=0;frame<16;++frame){
        for(unsigned chunk=0;chunk<32;++chunk){
            const UINT off=chunk*1024;
            CHECK(vb->Lock(off,1024,&data,chunk?D3::kLockNoOverwrite:D3::kLockDiscard)==D3D_OK);
            std::memset(data,static_cast<unsigned char>(frame+chunk),1024);CHECK(vb->Unlock()==D3D_OK);
        }
    }
    CHECK(get(counters.syncCalls)==rotationStart);
    // Read through the real backend, not the range mirror, and check every byte.
    CHECK(vb->Lock(0,32768,&data,D3::kLockReadOnly)==D3D_OK);
    for(unsigned i=0;i<32768;++i)CHECK(static_cast<unsigned char*>(data)[i]==15+i/1024);
    CHECK(vb->Unlock()==D3D_OK);
    CHECK(vb->Lock(0,64,&data,0)==D3D_OK);
    void* twice=nullptr;CHECK(vb->Lock(0,64,&twice,0)==D3DERR_INVALIDCALL);CHECK(vb->Unlock()==D3D_OK);
    CHECK(rig.dev->ProcessVertices(0,0,1,vb,nullptr,0)==D3D_OK);
    rig.sync();const auto afterGpu=get(counters.syncCalls);
    CHECK(vb->Lock(0,64,&data,0)==D3D_OK&&vb->Unlock()==D3D_OK);
    CHECK(get(counters.syncCalls)==afterGpu+2); // GPU-written buffers never reuse stale CPU bytes.
    vb->Release();rig.sync();
#ifndef NORTHLIGHT_RANGE_SHADOW_BASELINE
    CHECK(counters.rangeShadowBytes.load()==0);
    CHECK(get(counters.rangeShadowHits)>=400+16*31);
#endif
    IDirect3DIndexBuffer9* ib=nullptr;
    CHECK(rig.dev->CreateIndexBuffer(length,D3::kUsageDynamic,(D3DFORMAT)101,(D3DPOOL)0,&ib,nullptr)==D3D_OK);
    rig.sync();const auto ibStart=get(counters.syncCalls);
    CHECK(ib->Lock(2048,4096,&data,D3::kLockDiscard)==D3D_OK);std::memset(data,0x81,4096);CHECK(ib->Unlock()==D3D_OK);
    for(unsigned i=0;i<32;++i){CHECK(ib->Lock(3072,512,&data,0)==D3D_OK&&static_cast<unsigned char*>(data)[511]==0x81);CHECK(ib->Unlock()==D3D_OK);}
    CHECK(get(counters.syncCalls)==ibStart);
    CHECK(ib->Lock(3072,512,&data,D3::kLockReadOnly)==D3D_OK&&static_cast<unsigned char*>(data)[511]==0x81);CHECK(ib->Unlock()==D3D_OK);
    ib->Release();rig.sync();
#ifndef NORTHLIGHT_RANGE_SHADOW_BASELINE
    CHECK(counters.rangeShadowBytes.load()==0);
#endif
    std::printf("PASS oversized VB/IB exact bytes: repeated=400 append=512 index=32 unknown/GPU writes=sync memory=bounded\n");
    rig.finish();checkClean();gTrace.clear();
}

static void oversizedBufferPumpedWrite(){
    gTrace.clear();StreamDevice::Options options;options.budget=2u<<20;
    Rig rig(true,options);auto& queue=rig.core().q;
    IDirect3DVertexBuffer9* vb=nullptr;void* data=nullptr;
    CHECK(rig.dev->CreateVertexBuffer(40u<<20,D3::kUsageDynamic,0,(D3DPOOL)0,&vb,nullptr)==D3D_OK);
    CHECK(vb->Lock(0,131072,&data,D3::kLockDiscard)==D3D_OK);std::memset(data,0x68,131072);CHECK(vb->Unlock()==D3D_OK);rig.sync();
    static Rig* current;static IDirect3DVertexBuffer9* buffer;static Block* held;static unsigned calls;
    static HRESULT nestedLock,nestedUnlock;
    current=&rig;buffer=vb;calls=0;nestedLock=nestedUnlock=12345;
    held=queue.tryAllocBlock(1u<<20);CHECK(held);
    gKnobs.hold.store(true);rig.dev->BeginScene();
    pumpHook=[]{if(!calls++){
        void* nested=nullptr;nestedLock=buffer->Lock(0,64,&nested,0);nestedUnlock=buffer->Unlock();
        CHECK(current->dev->ProcessVertices(0,0,1,buffer,nullptr,0)==D3D_OK);
        current->core().q.freeBlock(held);held=nullptr;gKnobs.hold.store(false);
    }};
    const auto before=get(queue.stats.syncCalls);
    CHECK(vb->Lock(0,131072,&data,D3::kLockNoOverwrite)==D3D_OK);pumpHook=nullptr;
    CHECK(calls&&nestedLock==D3DERR_INVALIDCALL&&nestedUnlock==D3DERR_INVALIDCALL&&!held);
    CHECK(get(queue.stats.syncCalls)==before+1); // Revalidate after the pump: a real lock after the GPU write.
    CHECK(static_cast<unsigned char*>(data)[131071]==0x68);CHECK(vb->Unlock()==D3D_OK);
    CHECK(get(queue.stats.syncCalls)==before+2);
    vb->Release();rig.sync();rig.finish();checkClean();gTrace.clear();
    std::puts("PASS range shadow budget pump: nested Lock/Unlock refused, GPU write invalidates pending fast path");
}
