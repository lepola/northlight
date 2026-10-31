#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

// Portable register differencing for sequential replay draws. No D3D calls,
// heap allocation or floating-point comparison; NaN payloads and signed zero
// are preserved exactly as supplied by the application.
namespace NorthlightReplayConstants {
struct DirtySpan { uint32_t first=0,count=0; };
namespace Detail {
template<std::size_t Bytes> inline bool equalRegister(const void* a,const void* b)noexcept{
#if defined(__SSE2__)
    if constexpr(Bytes==16){
        __m128i x,y;std::memcpy(&x,a,16);std::memcpy(&y,b,16);
        return _mm_movemask_epi8(_mm_cmpeq_epi8(x,y))==0xffff;
    }
#endif
    return std::memcmp(a,b,Bytes)==0;
}
// Both float4 spans are already captured and valid. Chunk the long equal
// palette tails, while differing end registers avoid an unnecessary bulk scan.
__attribute__((noinline)) inline DirtySpan knownFloatSpan(const float* a,const float* b,unsigned count)noexcept{
    if(!count||!std::memcmp(a,b,size_t(count)*16))return {};
    unsigned first=0,last=count;constexpr unsigned Chunk=16;
    if(equalRegister<16>(a,b)){
        ++first;
        while(last-first>=Chunk&&!std::memcmp(a+first*4,b+first*4,Chunk*16))first+=Chunk;
        while(first<last&&equalRegister<16>(a+first*4,b+first*4))++first;
    }
    if(equalRegister<16>(a+(last-1)*4,b+(last-1)*4)){
        --last;
        while(last-first>=Chunk&&!std::memcmp(a+(last-Chunk)*4,b+(last-Chunk)*4,Chunk*16))last-=Chunk;
        while(last>first&&equalRegister<16>(a+(last-1)*4,b+(last-1)*4))--last;
    }
    return {first,last-first};
}
}

template<class T,std::size_t Count,std::size_t Width>
class DirtyRegisters {
    static_assert(std::is_trivially_copyable<T>::value,"Register elements must be trivially copyable");
    static_assert(Count>0&&Width>0,"Register bank dimensions must be nonzero");
    static_assert(Count<=UINT32_MAX,"Register count must fit the D3D9 UINT interface");
    static_assert(Width<=std::numeric_limits<std::size_t>::max()/sizeof(T)/Count,"Register bank size overflow");
    static constexpr std::size_t RegisterBytes=sizeof(T)*Width;
    static constexpr std::size_t BankBytes=RegisterBytes*Count;
    // Intentionally not value-initialized. No cached element is read before
    // the initial full copy, and reset needs no redundant large memset.
    std::array<T,Count*Width> values_;
    bool valid_=false;
public:
    // desired points to Count*Width elements. The returned first/count are
    // REGISTERS, not scalar elements. Upload desired+first*Width, count regs.
    // Cache updates immediately: caller must abort/reset this cache if its
    // subsequent backend upload fails. Call reset after any external register
    // write, state-block restore, reset or other operation invalidating the bank.
    DirtySpan update(const T* desired) noexcept {
        if(!valid_){
            std::memcpy(values_.data(),desired,BankBytes);valid_=true;
            return {0,static_cast<uint32_t>(Count)};
        }
#if defined(__SSE2__)
        if constexpr(RegisterBytes!=16)
#endif
            if(std::memcmp(values_.data(),desired,BankBytes)==0)return {};
        std::size_t first=0,last=Count;
        while(first<Count&&Detail::equalRegister<RegisterBytes>(values_.data()+first*Width,desired+first*Width))++first;
        if(first==Count)return {};
        while(last>first&&Detail::equalRegister<RegisterBytes>(values_.data()+(last-1)*Width,desired+(last-1)*Width))--last;
        std::memcpy(values_.data()+first*Width,desired+first*Width,(last-first)*RegisterBytes);
        return {static_cast<uint32_t>(first),static_cast<uint32_t>(last-first)};
    }
    void reset() noexcept {valid_=false;}
};
} // namespace NorthlightReplayConstants
