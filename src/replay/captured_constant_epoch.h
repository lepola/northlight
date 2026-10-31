#pragma once
#include <cstdint>
#include <limits>
// Test-only validation of serial-certified captures (replay_capture_constants.h).
#ifndef NORTHLIGHT_PALETTE_VALIDATE
#define NORTHLIGHT_PALETTE_VALIDATE 0
#endif

// A write serial is an exact unchanged-state proof, never a content hash.
// Different serials only mean "read/compare normally". Invalidation also
// changes the reset serial, so state-block restores cannot revive old proofs.
// floatBlock[b] is the floats serial of the last write touching c[16b..16b+15].
// Serials are monotonic within one reset, so an equal block serial proves that
// no accepted write touched the block between two stamps (identical rewrites
// included). A different serial means only "read/compare that block".
class ExtensionDevice;
namespace NorthlightConstantEpoch {
constexpr unsigned BlockRegisters=16,FloatBlocks=256/BlockRegisters;
struct Stamp {
    std::uint64_t reset=0,floats=0,floatWithout2=0,floatWithout4=0,booleans=0,integers=0;
    std::uint64_t floatBlock[FloatBlocks]={};
    bool valid=false,held=false,requestHeld=false; // Optional same-gate certificate, not an epoch word.
};
using Reader=bool(*)(void*,Stamp&);
using KnownSpan=bool(*)(void*,unsigned,unsigned,float*,Stamp&);
// Only ExtensionDevice can construct a source certified to read its mirror.
class TrustedSource {
    friend class ::ExtensionDevice;
    void* context_=nullptr;Reader reader_=nullptr;KnownSpan span_=nullptr;
    TrustedSource(void* context,Reader reader,KnownSpan span):context_(context),reader_(reader),span_(span){}
public:
    TrustedSource()=default;
    explicit operator bool()const{return reader_!=nullptr;}
    void* context()const{return context_;}
    Reader reader()const{return reader_;}
    KnownSpan span()const{return span_;}
};
// 0.3.180 (C1): the serial words alone. exact() and banks() also need both stamps valid; an in-place
// compare with a Clock's live words (whose valid flag is never set) asks its source's valid() instead.
inline bool sameWords(const Stamp& a,const Stamp& b){
    return a.reset==b.reset&&a.floats==b.floats&&a.booleans==b.booleans&&a.integers==b.integers;
}
inline bool exact(const Stamp& a,const Stamp& b){return a.valid&&b.valid&&sameWords(a,b);}
inline bool pose(const Stamp& a,const Stamp& b,unsigned projectionKind){
    return a.valid&&b.valid&&a.reset==b.reset&&a.booleans==b.booleans&&a.integers==b.integers&&
        ((projectionKind==1&&a.floatWithout4==b.floatWithout4)||
         (projectionKind==2&&a.floatWithout2==b.floatWithout2));
}
// Same reset and, for banks the capture reads, the same BOOL/INT serials.
inline bool sameBanks(const Stamp& a,const Stamp& b,bool booleans,bool integers){
    return a.reset==b.reset&&(!booleans||a.booleans==b.booleans)&&(!integers||a.integers==b.integers);
}
inline bool banks(const Stamp& a,const Stamp& b,bool booleans,bool integers){return a.valid&&b.valid&&sameBanks(a,b,booleans,integers);}
// Registers [first,last) spanning every block of [lo,hi) whose serial differs,
// clipped to [lo,hi). first==last proves the whole range unchanged. Callers
// must first establish banks(); the span is conservative (clean blocks between
// two dirty ones are included). An invalid range fails closed: all dirty.
inline void dirtySpan(const Stamp& a,const Stamp& b,unsigned lo,unsigned hi,unsigned& first,unsigned& last){
    first=last=lo;if(lo>=hi)return;
    if(hi>256){last=hi;return;}
    unsigned begin=FloatBlocks,end=0;
    for(unsigned block=lo/BlockRegisters;block<=(hi-1)/BlockRegisters;++block)
        if(a.floatBlock[block]!=b.floatBlock[block]){if(begin==FloatBlocks)begin=block;end=block+1;}
    if(begin==FloatBlocks)return;
    first=begin*BlockRegisters>lo?begin*BlockRegisters:lo;last=end*BlockRegisters<hi?end*BlockRegisters:hi;
}
class Clock {
    Stamp value_;
    bool overflow_=false;
    void advance(std::uint64_t& value){
        if(value==std::numeric_limits<std::uint64_t>::max())overflow_=true;
        else ++value;
    }
public:
    void invalidate(){advance(value_.reset);}
    void writeFloat(unsigned first,unsigned count){
        if(!count)return;
        advance(value_.floats);
        const std::uint64_t end=std::uint64_t(first)+count;
        if(first<2||end>6)advance(value_.floatWithout2);
        if(first<4||end>8)advance(value_.floatWithout4);
        // Registers >=256 (SWVP) are outside every captured range.
        if(first<256)for(unsigned block=first/BlockRegisters,last=unsigned((end<256?end:256)-1)/BlockRegisters;block<=last;++block)
            value_.floatBlock[block]=value_.floats;
    }
    void writeBool(unsigned count){if(count)advance(value_.booleans);}
    void writeInt(unsigned count){if(count)advance(value_.integers);}
    Stamp stamp(bool active)const{auto result=value_;result.valid=active&&!overflow_;return result;}
    // 0.3.180 (C1): the words in place (valid is never set here) and the overflow latch of stamp().
    const Stamp& live()const{return value_;}
    bool overflow()const{return overflow_;}
};
}
