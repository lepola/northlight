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
   Fence contract: the Hooks type provides IDirect3DQuery9* issue() (null on failure), bool done(IDirect3DQuery9*) and
   void release(IDirect3DQuery9*). place() tags the new span with issue() AT place() time, and issue() must return a
   fence that signals only after EVERY draw that reads this call's bytes has been submitted. The production hook is
   FrameFence below: ONE EVENT query per frame, shared by every ring and every call of the frame, whose Issue(END) is
   made by WorldRenderer::endFrame (renderer.cpp clearFrame, after all replay and terrain draws). Its done() is false
   for the frame's own, not yet issued, query, so a span placed this frame is never reusable this frame. One query per
   frame instead of one per call keeps DXVK 1.10.3/2.7.1/3.1.1 from considering a mid-frame submit (End of an event
   query runs ConsiderFlush(ImplicitStrongHint)) several times per frame, and the span's fence is exact instead of
   one call late. place() then reaps the finished spans with a non-flushing GetData(nullptr,0,0)==S_OK (never FLUSH,
   never a spin). At most MaxPending spans: contiguous spans of one fence are merged, and a new span beyond the limit
   is merged into the newest one (range union, the newer fence), so it stays pending at least as long as either.
   A failed CreateQuery/Issue makes the span pending forever (null/never-signalling fence): the old DISCARD-on-wrap
   behaviour. A ring whose bytes are drawn again on later frames without a new place() calls touch() every such frame.
   A freshly created buffer starts empty, so its first lock is NOOVERWRITE (creation zero-fills a direct
   buffer synchronously, see upload_lock.h) and charges nothing.
   The logic is host-testable with fake hooks. */
namespace NorthlightDynamicRing {
inline constexpr std::uint64_t MiB=1ull<<20;
inline constexpr size_t MaxPending=8;
inline UINT ringCapacity(std::uint64_t bytes){
    // Headroom of three more sets (at least 1 MiB, at most 8 MiB; the cap keeps the 32-bit address space in check): a span is
    // reusable only once the frame fence has completed (GPU-bound lag of up to 3 frames), so a ring of four sets (up to
    // ~2.67 MiB per call) never needs a DISCARD in a steady state with a lag of up to 3 frames.
    const std::uint64_t headroom=std::min<std::uint64_t>(std::max<std::uint64_t>(3*bytes,MiB),8*MiB);
    return UINT(((bytes+headroom)+MiB-1)/MiB*MiB);
}
struct Span {UINT begin=0,end=0;IDirect3DQuery9* query=nullptr;}; // query==nullptr: pending until a DISCARD clears it
struct Slot {UINT offset=0;DWORD flags=0;bool wrapped=false,discarded=false,fenceReuse=false;};
struct Ring {
    UINT capacity=0,cursor=0,peak=0;bool fresh=false;
    std::deque<Span> spans;
};
// Capacity more than twice what the recent peak needs, and large enough to matter.
inline bool oversized(const Ring& r,std::uint64_t recentPeak){return r.capacity>4*MiB&&std::uint64_t(r.capacity)>2*std::uint64_t(ringCapacity(recentPeak));}
template<class H> void clearSpans(Ring& r,H& h){for(auto& s:r.spans)if(s.query)h.release(s.query);r.spans.clear();}
// A new (or dropped) buffer: forget every span, start empty. capacity 0 = no buffer.
template<class H> void reset(Ring& r,UINT capacity,H& h){clearSpans(r,h);r.capacity=capacity;r.cursor=0;r.fresh=capacity!=0;if(!capacity)r.peak=0;}
template<class H> void reap(Ring& r,H& h){
    for(auto it=r.spans.begin();it!=r.spans.end();){if(it->query&&h.done(it->query)){h.release(it->query);it=r.spans.erase(it);}else ++it;}
}
// Reserves bytes (aligned to align, bytes <= r.capacity) and returns the Lock offset and flags. The caller Locks
// (offset,bytes,flags), copies, Unlocks and draws; the span is already tagged with the fence of the current frame.
template<class H> Slot place(Ring& r,UINT bytes,UINT align,H& h){
    reap(r,h);
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
    r.fresh=false;r.cursor=slot.offset+need;
    IDirect3DQuery9* q=h.issue();const UINT begin=slot.offset,end=slot.offset+need;
    if(!r.spans.empty()&&((q&&r.spans.back().query==q&&r.spans.back().end==begin)||r.spans.size()>=MaxPending)){
        auto& back=r.spans.back(); // same fence and contiguous, or the span list is full: union, the newer fence
        back.begin=std::min(back.begin,begin);back.end=std::max(back.end,end);
        if(back.query)h.release(back.query);back.query=q;
    }else r.spans.push_back({begin,end,q});
    return slot;
}
// A ring whose newest span is drawn AGAIN on a later frame without a new place() (the live terrain IB): re-tags that
// span with the current frame's fence, so it stays pending until the draws of this frame have been submitted.
template<class H> void touch(Ring& r,H& h){
    if(r.spans.empty())return;
    IDirect3DQuery9* q=h.issue();auto& back=r.spans.back();
    if(back.query)h.release(back.query);back.query=q;
}
// The real device: one EVENT query per frame, shared by all rings (see the header).
struct FrameFence {
    IDirect3DDevice9* device=nullptr;
    IDirect3DQuery9* current=nullptr;bool failed=false; // current: created lazily, not yet issued; failed: creation failed this frame
    IDirect3DQuery9* issue(){
        if(!current&&!failed&&device){if(FAILED(device->CreateQuery(D3DQUERYTYPE_EVENT,&current))||!current){current=nullptr;failed=true;}}
        if(current)current->AddRef();
        return current;
    }
    bool done(IDirect3DQuery9* q){return q!=current&&q->GetData(nullptr,0,0)==S_OK;}
    void release(IDirect3DQuery9* q){q->Release();}
    // End of the frame, after every draw that reads a span of this frame: the only Issue. A failed Issue leaves the query
    // never signalling, so its spans stay pending (DISCARD-on-wrap).
    void endFrame(){if(current){current->Issue(D3DISSUE_END);current->Release();current=nullptr;}failed=false;}
    // Reset/release paths: forget the unissued query without issuing.
    void drop(){if(current){current->Release();current=nullptr;}failed=false;}
};
}
