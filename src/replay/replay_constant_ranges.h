#pragma once
#include "replay_constants.h"
#include "shader_constant_usage.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>

// Bounded variant of replay_constants.h. A fresh cache is required after any
// external Set*Constant call, state-block restore, or reset. Shader switches do
// not invalidate it: shader-local DEF/DEFI/DEFB literals do not modify the D3D
// API constant bank. The metadata's bounding spans include any intervening
// registers; capture already reads those complete spans.
namespace NorthlightReplayConstants {
using ConstantRange = NorthlightShaderConstants::Range;

template<class T,std::size_t Count,std::size_t Width>
class BoundedDirtyRegisters {
    static_assert(std::is_trivially_copyable<T>::value,"Register elements must be trivially copyable");
    static_assert(Count>0&&Count<=UINT32_MAX&&Width>0,"Invalid register bank dimensions");
    static_assert(Width<=std::numeric_limits<std::size_t>::max()/sizeof(T)/Count,"Register bank size overflow");
    static constexpr std::size_t RegisterBytes=sizeof(T)*Width;
    // Unknown slots are never compared. Only the validity mask is initialized.
    std::array<T,Count*Width> values_;
    std::array<uint64_t,(Count+63)/64> valid_{};
    bool known(std::size_t index) const noexcept {return (valid_[index/64]&(uint64_t(1)<<(index%64)))!=0;}
    void mark(std::size_t first,std::size_t last) noexcept {
        for(std::size_t i=first;i<last;){
            const std::size_t boundary=std::min(last,(i/64+1)*64),length=boundary-i;
            const uint64_t mask=length==64?~uint64_t(0):((uint64_t(1)<<length)-1)<<(i%64);
            valid_[i/64]|=mask;i=boundary;
        }
    }
    bool knownRange(std::size_t first,std::size_t last) const noexcept {
        for(std::size_t i=first;i<last;){
            const std::size_t boundary=std::min(last,(i/64+1)*64),length=boundary-i;
            const uint64_t mask=length==64?~uint64_t(0):((uint64_t(1)<<length)-1)<<(i%64);
            if((valid_[i/64]&mask)!=mask)return false;
            i=boundary;
        }
        return true;
    }
public:
    // desired points to a full-bank-addressable buffer, but ONLY used is read.
    // One upload spans the first through last changed/unknown register within
    // used. Empty/unused banks perform no memory read, comparison, or upload.
    // As in DirtyRegisters, this commits optimistically: abort or reset after
    // an unsuccessful backend upload. Successful calls preserve validity of
    // untouched ranges, including when shader/range order is A -> B -> A.
    DirtySpan update(const T* desired,ConstantRange used) noexcept {
        if(!used.count)return {};
        assert(used.first<Count&&used.count<=Count-used.first&&desired);
        if(!desired||used.first>=Count||used.count>Count-used.first)return {};
        std::size_t first=used.first,last=first+used.count;
        if(knownRange(first,last)&&std::memcmp(values_.data()+first*Width,desired+first*Width,(last-first)*RegisterBytes)==0)return {};
        while(first<last&&known(first)&&std::memcmp(values_.data()+first*Width,desired+first*Width,RegisterBytes)==0)++first;
        while(last>first&&known(last-1)&&std::memcmp(values_.data()+(last-1)*Width,desired+(last-1)*Width,RegisterBytes)==0)--last;
        if(first==last)return {};
        std::memcpy(values_.data()+first*Width,desired+first*Width,(last-first)*RegisterBytes);
        mark(first,last);
        return {static_cast<uint32_t>(first),static_cast<uint32_t>(last-first)};
    }
    // Compare a captured bank with an immutable replacement span without first
    // copying the entire used range. Only the resulting dirty span is written
    // to values_; it is also the upload source, avoiding a second scratch copy.
    // Both sources must remain separate from this bank during the update.
    DirtySpan updateOverlay(const T* captured,ConstantRange used,
            ConstantRange overlay,const T* replacement) noexcept {
        if(!used.count)return {};
        const bool valid=captured&&replacement&&used.first<Count&&used.count<=Count-used.first&&
            overlay.first>=used.first&&overlay.first<=used.first+used.count&&
            overlay.count<=used.first+used.count-overlay.first;
        assert(valid);if(!valid)return {};
        const std::size_t begin=used.first,end=begin+used.count;
        const std::size_t patchFirst=overlay.first,patchLast=patchFirst+overlay.count;
        std::size_t first=end,last=begin;
        // Each segment has one contiguous source. Compare unchanged segments
        // and chunks in bulk, avoiding a per-register projection-selection
        // branch on sparse changes to a large relative-address palette.
        const auto inspect=[&](std::size_t a,std::size_t b,const T* source){
            if(a==b)return;
            const std::size_t origin=a;const bool allKnown=knownRange(a,b);
            if(allKnown&&!std::memcmp(values_.data()+a*Width,source,(b-a)*RegisterBytes))return;
            const auto same=[&](std::size_t lo,std::size_t hi){
                return (allKnown||knownRange(lo,hi))&&
                    !std::memcmp(values_.data()+lo*Width,source+(lo-origin)*Width,(hi-lo)*RegisterBytes);
            };
            constexpr std::size_t Chunk=16;
            while(b-a>=Chunk&&same(a,a+Chunk))a+=Chunk;
            while(a<b&&same(a,a+1))++a;
            while(b-a>=Chunk&&same(b-Chunk,b))b-=Chunk;
            while(b>a&&same(b-1,b))--b;
            if(a<b){first=std::min(first,a);last=std::max(last,b);}
        };
        inspect(begin,patchFirst,captured+begin*Width);
        inspect(patchFirst,patchLast,replacement);
        inspect(patchLast,end,captured+patchLast*Width);
        if(first>=last)return {};
        const auto source=[&](std::size_t i){return i>=patchFirst&&i<patchLast?
            replacement+(i-patchFirst)*Width:captured+i*Width;};
        const auto copy=[&](std::size_t a,std::size_t b){
            if(a<b)std::memcpy(values_.data()+a*Width,source(a),(b-a)*RegisterBytes);
        };
        copy(first,std::min(last,patchFirst));
        copy(std::max(first,patchFirst),std::min(last,patchLast));
        copy(std::max(first,patchLast),last);
        mark(first,last);
        return {static_cast<uint32_t>(first),static_cast<uint32_t>(last-first)};
    }
    // Read only known registers, normally the latest used range or dirty span.
    const T* data()const noexcept {return values_.data();}
    void reset() noexcept {valid_.fill(0);}
};

// The projection is the only difference between captured and desired floats.
// The bank retains exact desired values for every used register, including an
// unchanged range, so its stable data() pointer can serve the existing Pass API.
inline bool updateProjectedConstants(BoundedDirtyRegisters<float,256,4>& bank,
        const float* captured,const NorthlightShaderConstants::Usage& usage,
        unsigned projectionKind,const float* projectionRows,DirtySpan& dirty,const float* worldMatrix=nullptr) noexcept {
    dirty={};
    if(!captured||!projectionRows||(projectionKind!=1&&projectionKind!=2))return false;
    const auto range=usage.floats;
    const unsigned projectionStart=projectionKind==1?4:2;
    if(range.first>256||range.count>256-range.first||range.first>projectionStart||range.first+range.count<projectionStart+4)return false;
    float transposed[16];const float* replacement=projectionRows;
    if(projectionKind==1&&worldMatrix)replacement=worldMatrix; // DetailDoodad shadow VS consumes world POSITION0.
    else if(projectionKind==1){
        for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)
            std::memcpy(transposed+a*4+b,projectionRows+b*4+a,sizeof(float));
        replacement=transposed;
    }
    dirty=bank.updateOverlay(captured,range,{projectionStart,4},replacement);
    return true;
}

// Prepare only the span captured for this shader. The rest of desired remains
// uninitialized/untouched and MUST NOT be passed to a full-bank differencer.
// A relative-address shader uses Usage.floats={0,256}, preserving every bone.
// Original shaders with literal projection rows are conservatively captured
// but must still be rejected by the caller's existing identity/layout gate.
inline bool prepareFloatConstants(float* desired,const float* captured,
        const NorthlightShaderConstants::Usage& usage,unsigned projectionKind,
        const float* projectionRows) noexcept {
    if(!desired||!captured||!projectionRows||(projectionKind!=1&&projectionKind!=2))return false;
    const auto range=usage.floats;
    const unsigned projectionStart=projectionKind==1?4:2;
    if(range.first>256||range.count>256-range.first||range.first>projectionStart||range.first+range.count<projectionStart+4)return false;
    std::memcpy(desired+range.first*4,captured+range.first*4,std::size_t(range.count)*16);
    if(projectionKind==1){
        for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)desired[16+a*4+b]=projectionRows[b*4+a];
    }else std::memcpy(desired+8,projectionRows,16*sizeof(float));
    return true;
}
} // namespace NorthlightReplayConstants
