#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
namespace SkyMipHash {
constexpr unsigned MaxTextures=4;
constexpr std::size_t MaxBytes=1u*1024u*1024u;
struct Layout {unsigned rowBytes=0,rows=0;std::size_t bytes=0;};
inline bool layout(unsigned format,unsigned w,unsigned h,Layout& out){
 if(!w||!h||w>4096||h>4096)return false;
 unsigned row=0,rows=h;
 switch(format){
 case 21:case 22:row=w*4;break;
 case 0x31545844:row=((w+3)/4)*8;rows=(h+3)/4;break;
 case 0x33545844:case 0x35545844:row=((w+3)/4)*16;rows=(h+3)/4;break;
 default:return false;
 }
 const std::size_t n=std::size_t(row)*rows;if(n>MaxBytes)return false;
 out={row,rows,n};return true;
}
enum class Status { ok,unsupported,overflow,desc_failed,lock_failed,invalid_lock,unlock_failed };
inline const char* name(Status s){switch(s){case Status::ok:return "ok";case Status::unsupported:return "unsupported";case Status::overflow:return "overflow";case Status::desc_failed:return "desc_failed";case Status::lock_failed:return "lock_failed";case Status::invalid_lock:return "invalid_lock";case Status::unlock_failed:return "unlock_failed";}return "unknown";}
struct Budget {unsigned count=0;std::size_t bytes=0;};
struct Result {Status status=Status::unsupported;std::uint64_t hash=0;long lockHr=0,unlockHr=0;std::size_t bytes=0;};
// Lock and unlock are invoked once, paired even when the returned lock is invalid.
// The caller supplies HRESULT success handling and a row hash; no buffer or GPU target is allocated.
template<class Lock,class Unlock,class Hash> Result capture(bool managed,unsigned format,unsigned w,unsigned h,Budget& budget,Lock lock,Unlock unlock,Hash hash){
 Result r;Layout l;
 if(!managed||!layout(format,w,h,l))return r;
 if(budget.count>=MaxTextures||l.bytes>MaxBytes-budget.bytes){r.status=Status::overflow;return r;}
 ++budget.count;budget.bytes+=l.bytes;r.bytes=l.bytes;
 const void* bits=nullptr;int pitch=0;
 if(!lock(bits,pitch,r.lockHr)){r.status=Status::lock_failed;return r;}
 const bool valid=bits&&pitch>=0&&unsigned(pitch)>=l.rowBytes&&std::size_t(pitch)<=MaxBytes*2/l.rows;
 if(valid)r.hash=hash(static_cast<const unsigned char*>(bits),unsigned(pitch),l.rows,l.rowBytes);
 const bool unlocked=unlock(r.unlockHr);
 r.status=!unlocked?Status::unlock_failed:!valid?Status::invalid_lock:Status::ok;
 if(r.status!=Status::ok)r.hash=0;
 return r;
}
}
