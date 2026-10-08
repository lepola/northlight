#pragma once
#include <d3d9.h>
#include "upload_lock.h"
#include "lock_meter_readback.h"
#include "replay_copies.h"
/* 0.3.192 (CS): the read side of replay_copies.h, a drop-in for the READONLY read-back Lock/Unlock pair of the capture sites. lock() returns the
   bytes of the buffer at the replay position without touching DXVK when the stream's CPU copy is valid; otherwise it makes one (a single
   whole-buffer read through the readBackLock() path, the copy then follows the replayed writes) or, when no copy is possible, takes exactly
   today's lock: Lock(off,size,&ptr,readBackLock()) + the LOCK METER read-back count. unlock() releases the pin or forwards Unlock. With the
   stream off (CommandStream=0) enabled is false: every call below is that lock and unlock, nothing else. The pointer is valid until unlock(). */
namespace NorthlightReplayCopies {
template<class B> class Reader {
    B* b;Slot* pinned=nullptr;
public:
    explicit Reader(B* buffer):b(buffer){}
    Reader(const Reader&)=delete;Reader& operator=(const Reader&)=delete;
    ~Reader(){if(pinned)unpin(*pinned);}   // an early return without unlock(): the pin goes, a real lock is left as the old code left it
    HRESULT lock(UINT off,UINT size,void** out){
        if(enabled.load(std::memory_order_relaxed)&&b){
            Slot* slot=nullptr;bool fill=false;
            if(const unsigned char* bytes=readOrBeginFill(b,off,size,slot,fill)){pinned=slot;*out=const_cast<unsigned char*>(bytes);served(size);return D3D_OK;}   // 0.3.196 (task 12): hit or begin the fill, one mutex acquisition
            if(fill){
                void* all=nullptr;bool ok=false;
                const HRESULT hr=b->Lock(0,slot->size,&all,NorthlightUpload::readBackLock());
                if(!FAILED(hr)){
                    NorthlightLockMeter::readBack(b,slot->size);
                    if(all){std::memcpy(slot->data.data(),all,slot->size);ok=true;}
                    if(FAILED(b->Unlock()))ok=false;
                }
                endFill(*slot,ok);
                if(ok){pinned=slot;*out=slot->data.data()+off;served(size);return D3D_OK;}
            }
            fallback();
        }
        const HRESULT hr=b->Lock(off,size,out,NorthlightUpload::readBackLock());
        if(!FAILED(hr))NorthlightLockMeter::readBack(b,size);
        return hr;
    }
    HRESULT unlock(){
        if(pinned){unpin(*pinned);pinned=nullptr;return D3D_OK;}
        return b->Unlock();
    }
};
}
