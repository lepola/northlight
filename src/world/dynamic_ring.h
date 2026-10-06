#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <d3d9.h>
/* 0.3.192 (DXVK3): fence-checked append ring for our per-frame DYNAMIC|WRITEONLY upload buffers (replay bulk
   VB/IB, live terrain IB). DXVK 3.1.1 charges the FULL buffer size to m_discardMemoryCounter on every DISCARD of
   a direct-mapped buffer (d3d9_device.cpp:5526-5532) and throttles on it (ThrottleAllocation :4831-4858), so a
   frame that re-uploaded a few KB into an 8 MiB DISCARD buffer was charged 8 MiB. The ring appends each
   call's bytes at the cursor with NOOVERWRITE, reuses the start after a wrap once an EVENT query says the
   draws that read the old bytes have finished, and falls back to DISCARD (which renames the buffer, so
   in-flight draws keep the old slice) only while a query is still pending. Same bytes in the same draws:
   only the byte offset (and index start) of a call moves.
   Query handling: place() first issues the query of the PREVIOUS call's span (the draws that read it were
   submitted before this call; a query issued later than the reads is conservative: it can only complete
   late), then reaps the finished ones with a non-flushing GetData(nullptr,0,0)==S_OK (never FLUSH, never a
   spin). At most MaxPending spans: a new span beyond that is merged into the newest one (its range is the
   union and its query is re-issued, so the merged span stays pending at least as long as either). A failed
   CreateQuery/Issue makes the span pending forever (null query): the old DISCARD-on-wrap behaviour.
   A freshly created buffer starts empty, so its first lock is NOOVERWRITE (creation zero-fills a direct
   buffer synchronously, see upload_lock.h) and charges nothing.
   The Hooks type provides IDirect3DQuery9* issue() (null on failure), bool done(IDirect3DQuery9*) and
   void release(IDirect3DQuery9*), so the logic is host-testable with fake queries. */
namespace NorthlightDynamicRing {
inline constexpr std::uint64_t MiB=1ull<<20;
inline constexpr size_t MaxPending=8;
inline UINT ringCapacity(std::uint64_t bytes){
    const std::uint64_t headroom=std::min<std::uint64_t>(std::max<std::uint64_t>(bytes,MiB),8*MiB);
    return UINT(((bytes+headroom)+MiB-1)/MiB*MiB);
}
struct Span {UINT begin=0,end=0;IDirect3DQuery9* query=nullptr;}; // query==nullptr: pending until a DISCARD clears it
struct Slot {UINT offset=0;DWORD flags=0;bool wrapped=false,discarded=false,fenceReuse=false;};
struct Ring {
    UINT capacity=0,cursor=0,peak=0;bool fresh=false;
    std::deque<Span> spans;
    bool open=false;Span current; // placed by the last place(), query not issued yet
};
// Capacity more than twice what the recent peak needs, and large enough to matter.
inline bool oversized(const Ring& r,std::uint64_t recentPeak){return r.capacity>4*MiB&&std::uint64_t(r.capacity)>2*std::uint64_t(ringCapacity(recentPeak));}
template<class H> void clearSpans(Ring& r,H& h){for(auto& s:r.spans)if(s.query)h.release(s.query);r.spans.clear();r.open=false;}
// A new (or dropped) buffer: forget every span, start empty. capacity 0 = no buffer.
template<class H> void reset(Ring& r,UINT capacity,H& h){clearSpans(r,h);r.capacity=capacity;r.cursor=0;r.fresh=capacity!=0;if(!capacity)r.peak=0;}
template<class H> void closeOpen(Ring& r,H& h){
    if(!r.open)return;r.open=false;
    IDirect3DQuery9* q=h.issue();
    if(r.spans.size()>=MaxPending){auto& back=r.spans.back();back.begin=std::min(back.begin,r.current.begin);back.end=std::max(back.end,r.current.end);
        if(back.query)h.release(back.query);back.query=q;return;}
    r.spans.push_back({r.current.begin,r.current.end,q});
}
template<class H> void reap(Ring& r,H& h){
    for(auto it=r.spans.begin();it!=r.spans.end();){if(it->query&&h.done(it->query)){h.release(it->query);it=r.spans.erase(it);}else ++it;}
}
// Reserves bytes (aligned to align, bytes <= r.capacity) and returns the Lock offset and flags. The caller Locks
// (offset,bytes,flags), copies, Unlocks and draws; the next place()/closeOpen() issues the query for this span.
template<class H> Slot place(Ring& r,UINT bytes,UINT align,H& h){
    closeOpen(r,h);reap(r,h);
    r.peak=std::max(bytes,r.peak-r.peak/16);
    if(!align)align=1;
    const UINT need=(bytes+align-1)/align*align,at=(r.cursor+align-1)/align*align;
    const auto vacant=[&](UINT b,UINT e){for(const auto& s:r.spans)if(s.begin<e&&b<s.end)return false;return true;};
    Slot slot;
    if(std::uint64_t(at)+need<=r.capacity&&vacant(at,at+need)){slot.offset=at;slot.flags=D3DLOCK_NOOVERWRITE;}
    else{
        slot.wrapped=true;
        if(need<=r.capacity&&vacant(0,need)){slot.offset=0;slot.flags=D3DLOCK_NOOVERWRITE;slot.fenceReuse=true;}
        else{slot.offset=0;slot.flags=D3DLOCK_DISCARD;slot.discarded=true;clearSpans(r,h);}
    }
    r.fresh=false;r.cursor=slot.offset+need;r.current={slot.offset,slot.offset+need,nullptr};r.open=true;
    return slot;
}
// The real device: one EVENT query per span.
struct DeviceQueries {
    IDirect3DDevice9* device=nullptr;
    IDirect3DQuery9* issue(){
        IDirect3DQuery9* q=nullptr;if(!device||FAILED(device->CreateQuery(D3DQUERYTYPE_EVENT,&q))||!q)return nullptr;
        if(FAILED(q->Issue(D3DISSUE_END))){q->Release();return nullptr;}
        return q;
    }
    bool done(IDirect3DQuery9* q){return q->GetData(nullptr,0,0)==S_OK;}
    void release(IDirect3DQuery9* q){q->Release();}
};
}
