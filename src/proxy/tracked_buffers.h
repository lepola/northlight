#pragma once
#include <atomic>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <type_traits>
#include "capture_buffer_metadata.h"
#include "mirror_guard.h"
#include "lock_meter.h"
#include "replay_copies.h"

// Only client-created buffers are wrapped. The real device always receives
// real resources. Unknown/unwrapped resources return revision 0 (no fast cache).
// This does not intercept or patch the backend's vtables.
namespace NorthlightTrackedBuffers {
// The private token belongs to the underlying COM resource, not its address.
// Serialize first sight so concurrent legacy readers cannot replace each
// other's freshly assigned identity. Tracked batch reads never enter here.
inline std::atomic<std::uint64_t> identityClock{1};
inline std::mutex identityMutex;
inline std::uint64_t identity(void* buffer,bool index){
    if(!buffer)return 0;
    static const GUID token={0x6f1d2a4c,0x3b7e,0x4c21,{0x9a,0x55,0x0f,0x7e,0x21,0x66,0x9d,0x4b}};
    auto* resource=index?static_cast<IDirect3DResource9*>(static_cast<IDirect3DIndexBuffer9*>(buffer)):
                         static_cast<IDirect3DResource9*>(static_cast<IDirect3DVertexBuffer9*>(buffer));
    std::uint64_t value=0;DWORD size=sizeof value;
    if(SUCCEEDED(resource->GetPrivateData(token,&value,&size))&&size==sizeof value&&value)return value;
    std::lock_guard<std::mutex> guard(identityMutex);
    value=0;size=sizeof value;
    if(SUCCEEDED(resource->GetPrivateData(token,&value,&size))&&size==sizeof value&&value)return value;
    value=identityClock.fetch_add(1,std::memory_order_relaxed);
    if(!value||FAILED(resource->SetPrivateData(token,&value,sizeof value,0)))return 0;
    return value;
}
inline std::atomic<uint64_t> clock{1};
inline std::mutex mutex;
struct Record {
    void* raw=nullptr;void* exposed=nullptr;
    IUnknown* object=nullptr;
    std::atomic<uint64_t> revision{0};
    std::atomic<unsigned> locks{0};
    bool index=false;
    NorthlightCaptureMetadata::Info metadata;
    NorthlightReplayCopies::Slot copy; // 0.3.192 (CS): the replay-side CPU copy (replay_copies.h); attached only while the stream is active
};
inline std::unordered_map<void*,Record*> records;
// Lock-free input resolution for our own wrappers. The first Buffer class
// built for an interface publishes one immutable {final vtable, Record offset}
// pair. A live input (the caller owns a COM reference, so its registration
// cannot be erased) whose first word equals that vtable is such a Buffer; its
// raw/exposed fields are immutable. Anything else takes the registry lookup.
static constexpr bool kBufferShapeResolve=true;
struct ShapeInfo {const void* vtable;std::ptrdiff_t record;};
template<class T> struct Shape {
    static inline std::atomic<const ShapeInfo*> info{nullptr};
    static const Record* match(const T* input){
        if(!kBufferShapeResolve)return nullptr;
        const ShapeInfo* shape=info.load(std::memory_order_acquire);
        if(!shape||*reinterpret_cast<const void* const*>(input)!=shape->vtable)return nullptr;
        auto* found=reinterpret_cast<const Record*>(reinterpret_cast<const char*>(input)+shape->record);
        return found->exposed==input?found:nullptr;
    }
};
inline bool isWrapped(void* p){
    if(!p)return false;
    std::lock_guard<std::mutex> guard(mutex);auto it=records.find(p);
    return it!=records.end()&&it->second->exposed==p;
}
inline void captureMetadata(const NorthlightCaptureMetadata::Request* requests,NorthlightCaptureMetadata::Info* output,unsigned count){
    if(!output)return;
    for(unsigned i=0;i<count;++i)output[i]={};
    if(!requests||!count)return;
    std::lock_guard<std::mutex> guard(mutex);
    for(unsigned i=0;i<count;++i){
        if(!requests[i].buffer)continue;
        auto found=records.find(requests[i].buffer);
        if(found==records.end())continue;
        const auto& record=*found->second;
        if(record.index!=requests[i].index||!record.metadata.known)continue;
        output[i]=record.metadata;
        output[i].revision=record.locks.load(std::memory_order_acquire)?0:record.revision.load(std::memory_order_relaxed);   // 0.3.196 (task 12): acquire pairs with the unlock's release
    }
}
inline uint64_t version(void* raw,bool index){
    std::lock_guard<std::mutex> guard(mutex);
    auto it=records.find(raw);
    if(it==records.end()||it->second->index!=index||it->second->locks.load(std::memory_order_acquire))return 0;   // 0.3.196 (task 12): acquire pairs with the unlock's release
    return it->second->revision.load(std::memory_order_relaxed);
}
template<class T> T* unwrap(T* p){
    std::lock_guard<std::mutex> guard(mutex);auto it=records.find(p);
    return it==records.end()?p:static_cast<T*>(it->second->raw);
}
// The caller holds its normal COM input reference. Resolve classification and
// forwarding together, under the SAME registry lock and lifetime rules as the
// original isWrapped + unwrap pair. Never retain an unowned registry pointer.
template<class T> T* resolveInput(T* p,bool& wrapped){
    wrapped=false;if(!p)return nullptr;
    if(const Record* found=Shape<T>::match(p)){wrapped=true;return static_cast<T*>(found->raw);}
    std::lock_guard<std::mutex> guard(mutex);auto it=records.find(p);
    if(it==records.end())return p;
    wrapped=it->second->exposed==p;return static_cast<T*>(it->second->raw);
}
template<class T> void expose(T** out){
    if(!out||!*out)return;
    T* raw=*out;IUnknown* object=nullptr;
    {std::lock_guard<std::mutex> guard(mutex);auto it=records.find(raw);
     if(it!=records.end()){object=it->second->object;object->AddRef();*out=static_cast<T*>(it->second->exposed);}}
    if(object)raw->Release(); // exchange the real Get* reference for the wrapper reference
}
inline void written(void* p){
    std::lock_guard<std::mutex> guard(mutex);auto it=records.find(p);
    if(it!=records.end()){it->second->revision=clock.fetch_add(1);NorthlightReplayCopies::invalidate(it->second->copy,true);} // 0.3.192 (CS): a GPU-side write (ProcessVertices): the CPU copy is stale for good
}
inline void invalidateAll(){
    std::lock_guard<std::mutex> guard(mutex);
    for(auto& entry:records){entry.second->revision=clock.fetch_add(1);NorthlightReplayCopies::invalidate(entry.second->copy);}
}
template<class T,class Forward> class Buffer final:public Forward {
    LONG refs=1;IDirect3DDevice9* owner;Record record;
    void(*unsafeAccess)(IDirect3DDevice9*,const char*)=nullptr;
    // 0.3.180 (D0): the owner device's gate, for the thread census only (buffers take no gate). The
    // buffer holds a device reference, so the gate outlives every call that reports to it.
    MirrorGate* census=nullptr;
    // 0.3.192 (DXVK3): the size DXVK 3.x charges for a DISCARD of this buffer (DEFAULT|DYNAMIC only: direct-mapped), 0 otherwise.
    UINT discardBytes=0;
    NorthlightReplayCopies::PendingWrite pendingWrite; // 0.3.196 (task 12): this wrapper's outstanding write lock (Lock records, Unlock consumes; touched by the locking thread only)
public:
    bool hasCopySlotForTest()const{return record.copy.attached;} // the CPU copy slot exists only for buffers created while the stream is active
    Buffer(T* real,IDirect3DDevice9* device,bool index,void(*unsafe)(IDirect3DDevice9*,const char*)=nullptr,MirrorGate* gate=nullptr):Forward(real),owner(device),unsafeAccess(unsafe),census(gate){
        record.raw=real;record.exposed=static_cast<T*>(this);record.object=this;record.index=index;
        record.revision=clock.fetch_add(1);
        // Descriptor and identity are immutable for this resource lifetime.
        // Failure disables metadata acceleration, never ordinary wrapping.
        if constexpr(std::is_same<T,IDirect3DIndexBuffer9>::value){
            D3DINDEXBUFFER_DESC desc={};
            if(index&&SUCCEEDED(real->GetDesc(&desc))&&desc.Size){
                record.metadata.size=desc.Size;record.metadata.usage=desc.Usage;record.metadata.format=desc.Format;
                record.metadata.identity=identity(real,true);record.metadata.known=record.metadata.identity!=0;
                discardBytes=desc.Pool==D3DPOOL_DEFAULT&&(desc.Usage&D3DUSAGE_DYNAMIC)?desc.Size:0;
            }
        }else{
            D3DVERTEXBUFFER_DESC desc={};
            if(!index&&SUCCEEDED(real->GetDesc(&desc))&&desc.Size){
                record.metadata.size=desc.Size;record.metadata.usage=desc.Usage;record.metadata.format=desc.Format;
                record.metadata.identity=identity(real,false);record.metadata.known=record.metadata.identity!=0;
                discardBytes=desc.Pool==D3DPOOL_DEFAULT&&(desc.Usage&D3DUSAGE_DYNAMIC)?desc.Size:0;
            }
        }
        if(NorthlightReplayCopies::enabled.load(std::memory_order_relaxed))NorthlightReplayCopies::attach(record.copy,record.raw,record.exposed,this,record.metadata.size,&record.locks); // size 0 (descriptor unknown): never copied
        {std::lock_guard<std::mutex> guard(mutex);
         records.emplace(record.raw,&record);
         try{records.emplace(record.exposed,&record);}catch(...){records.erase(record.raw);NorthlightReplayCopies::detach(record.copy);throw;}}
        owner->AddRef();
        // Identical for every instance of this final class; first class wins.
        static const ShapeInfo shape={*reinterpret_cast<const void* const*>(static_cast<T*>(this)),
            reinterpret_cast<const char*>(&record)-reinterpret_cast<const char*>(static_cast<T*>(this))};
        const ShapeInfo* expected=nullptr;Shape<T>::info.compare_exchange_strong(expected,&shape,std::memory_order_acq_rel);
    }
    ~Buffer(){
        {std::lock_guard<std::mutex> guard(mutex);records.erase(record.raw);records.erase(record.exposed);}
        NorthlightReplayCopies::detach(record.copy);
        this->real->Release();owner->Release();
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{
        // Serialize the final reference with GetStreamSource/GetIndices lookup.
        if(census)census->noteBuffer();
        ULONG count;
        {std::lock_guard<std::mutex> guard(mutex);count=InterlockedDecrement(&refs);
         if(!count){records.erase(record.raw);records.erase(record.exposed);}}
        if(!count)delete this;return count;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{
        if(!out)return E_POINTER;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DResource9)||id==__uuidof(T)){*out=static_cast<T*>(this);AddRef();return S_OK;}
        *out=nullptr;return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out) override{
        if(!out)return D3DERR_INVALIDCALL;*out=owner;owner->AddRef();return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(const GUID& guid,void* data,DWORD* size) override{
        // Arbitrary private data can contain an unwrapped COM interface.
        // Extension metadata reads use the raw buffer directly and avoid this.
        if(unsafeAccess)unsafeAccess(owner,"buffer private-data access");
        return this->real->GetPrivateData(guid,data,size);
    }
    HRESULT STDMETHODCALLTYPE Lock(UINT offset,UINT size,void** data,DWORD flags) override{
        // Mark before entering the driver. Even failed writes conservatively
        // invalidate the old generation. READONLY never advances a generation.
        if(census)census->noteBuffer();
        // 0.3.196 (task 12): revision is relaxed: Lock/Unlock and the copy hooks run on one thread per mode (the replay thread with the stream on, else the game thread, see replay_copies.h
        // "Threads"); the readers (version/captureMetadata) take the registry mutex and check `locks` (acquire) first, which pairs with the release of locks.fetch_sub below.
        const bool wasUnsafe=record.revision.load(std::memory_order_relaxed)==0;
        if(!(flags&D3DLOCK_READONLY))record.revision.store(clock.fetch_add(1),std::memory_order_relaxed);
        if((flags&D3DLOCK_DISCARD)&&discardBytes)NorthlightLockMeter::discard(NorthlightLockMeter::Game,discardBytes);
        const unsigned lockedBefore=record.locks.fetch_add(1);
        HRESULT hr=this->real->Lock(offset,size,data,flags);
        // 0.3.192 (CS): the write the stream replays (or a pass-through lock the game writes through) is mirrored into the CPU copy at Unlock.
        if(record.copy.attached&&!(flags&D3DLOCK_READONLY)&&NorthlightReplayCopies::enabled.load(std::memory_order_relaxed)){
            if(FAILED(hr))NorthlightReplayCopies::invalidate(record.copy);else NorthlightReplayCopies::writeLocked(record.copy,pendingWrite,lockedBefore,offset,size,data?*data:nullptr);
        }
        if(FAILED(hr)){
            // A rejected write cannot repair tracking after an earlier failed
            // unlock. Keep the legacy exact-read path until a write succeeds.
            if(wasUnsafe)record.revision.store(0,std::memory_order_relaxed);
            record.locks.fetch_sub(1,std::memory_order_release);
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override{
        if(census)census->noteBuffer();
        const bool copies=record.copy.attached&&NorthlightReplayCopies::enabled.load(std::memory_order_relaxed);
        if(copies)NorthlightReplayCopies::beforeUnlock(record.copy,pendingWrite,record.locks.load(std::memory_order_relaxed)); // 0.3.192 (CS): the pointer is still mapped
        HRESULT hr=this->real->Unlock();
        if(copies&&FAILED(hr))NorthlightReplayCopies::invalidate(record.copy);
        if(SUCCEEDED(hr)&&record.locks.load(std::memory_order_relaxed))record.locks.fetch_sub(1,std::memory_order_release);
        else{record.revision.store(0,std::memory_order_relaxed);if(copies)NorthlightReplayCopies::invalidate(record.copy);} // tracking is unsafe until a subsequent successful write
        return hr;
    }
};
template<class T,class Forward> void wrap(T** out,IDirect3DDevice9* owner,bool index,void(*unsafe)(IDirect3DDevice9*,const char*)=nullptr,MirrorGate* census=nullptr) noexcept{
    if(!out||!*out)return;
    try{*out=new Buffer<T,Forward>(*out,owner,index,unsafe,census);}catch(...){/* original resource remains valid, untracked */}
}
}
