#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace NorthlightStream {
// Oversized buffers need not keep a full mirror. Keep one active, exact byte
// interval; uploads still own their staging bytes until replay completes.
constexpr std::size_t RangeShadowBudgetBytes=std::size_t(8)<<20;
constexpr std::size_t RangeShadowItemBytes=std::size_t(4)<<20;
class BufferRangeShadow {
    std::unique_ptr<unsigned char[]> bytes_;
    std::size_t capacity_=0;
    std::uint32_t begin_=0,end_=0;
    bool discarded_=false;
public:
    std::size_t capacity()const{return capacity_;}
    bool covers(std::uint32_t off,std::uint32_t size)const{return bytes_&&off>=begin_&&std::uint64_t(off)+size<=end_;}
    bool canRead(std::uint32_t off,std::uint32_t size)const{
        if(covers(off,size))return true;
        // D3D9 DISCARD invalidates the entire buffer. Unknown bytes can be
        // initialized while every write since that DISCARD is still retained.
        return discarded_&&size<=RangeShadowItemBytes&&(begin_==end_||std::uint64_t(std::max(off+size,end_))-std::min(off,begin_)<=RangeShadowItemBytes);
    }
    void read(void* dst,std::uint32_t off,std::uint32_t size)const{
        if(covers(off,size)){std::memcpy(dst,bytes_.get()+off-begin_,size);return;}
        std::memset(dst,0,size);
        const auto first=std::max(off,begin_),last=std::min(off+size,end_);
        if(first<last)std::memcpy(static_cast<unsigned char*>(dst)+first-off,bytes_.get()+first-begin_,last-first);
    }
    void clear(){bytes_.reset();capacity_=0;invalidate();}
    void invalidate(){begin_=end_=0;discarded_=false;}
    void discard(){invalidate();discarded_=true;}
    // Unknown gaps stay unknown, except undefined bytes after DISCARD. Dropping
    // any written bytes revokes that exception until the next DISCARD.
    void remember(std::uint32_t off,std::uint32_t size,const void* src,std::size_t freeBudget){
        if(!size)return;
        if(size>RangeShadowItemBytes){clear();return;}
        const std::uint32_t end=off+size;
        const bool join=bytes_&&begin_<end_&&(discarded_||(off<=end_&&begin_<=end))&&std::uint64_t(std::max(end,end_))-std::min(off,begin_)<=RangeShadowItemBytes;
        if(!join&&begin_<end_)discarded_=false;
        const std::uint32_t first=join?std::min(off,begin_):off,last=join?std::max(end,end_):end;
        const std::size_t need=last-first;
        if(need>capacity_){
            std::size_t cap=4096;while(cap<need)cap*=2;
            // Include both allocations in the peak budget while growing.
            if(cap>freeBudget){clear();return;}
            std::unique_ptr<unsigned char[]> next;
            try{next.reset(new unsigned char[cap]);}catch(...){clear();return;}
            if(join)std::memcpy(next.get()+begin_-first,bytes_.get(),end_-begin_);
            bytes_=std::move(next);capacity_=cap;
        }else if(join&&first<begin_)std::memmove(bytes_.get()+begin_-first,bytes_.get(),end_-begin_);
        if(join&&off>end_)std::memset(bytes_.get()+end_-first,0,off-end_);
        if(join&&end<begin_)std::memset(bytes_.get()+end-first,0,begin_-end);
        std::memcpy(bytes_.get()+off-first,src,size);begin_=first;end_=last;
    }
};
}
