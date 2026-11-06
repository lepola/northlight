#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace NorthlightFloatChecks {
// Positive IEEE-754 bit patterns have the same order as their magnitudes.
// Integer checks reject NaNs/infinities without evaluating their FP lanes.
template<std::size_t Count,std::uint32_t Maximum=0x7f7fffffu>
inline bool bounded(const float* values){
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);
    static_assert(Count&&Count%4==0&&Maximum<=0x7f7fffffu);
#if defined(__SSE2__)
    const auto sign=_mm_set1_epi32(0x7fffffff),limit=_mm_set1_epi32(int(Maximum));
    auto bad=_mm_setzero_si128();
    for(std::size_t i=0;i<Count;i+=4){const auto bits=_mm_castps_si128(_mm_loadu_ps(values+i));
        bad=_mm_or_si128(bad,_mm_cmpgt_epi32(_mm_and_si128(bits,sign),limit));}
    return _mm_movemask_epi8(bad)==0;
#else
    for(std::size_t i=0;i<Count;++i){std::uint32_t bits;std::memcpy(&bits,values+i,4);if((bits&0x7fffffffu)>Maximum)return false;}
    return true;
#endif
}
}
