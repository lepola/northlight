#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
namespace NorthlightMaterialBytes {
// RGBA/BGRA share the alpha lane; partial trailing pixels are ignored.
inline std::uint8_t minimumAlpha(const std::uint8_t* bytes,std::size_t count){
    std::uint8_t low=255;std::size_t i=0;
#if defined(__SSE2__)
    auto lanes=_mm_set1_epi8(char(255));
    for(;count-i>=16;i+=16)lanes=_mm_min_epu8(lanes,_mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes+i)));
    lanes=_mm_min_epu8(lanes,_mm_srli_si128(lanes,8));lanes=_mm_min_epu8(lanes,_mm_srli_si128(lanes,4));
    low=std::uint8_t(unsigned(_mm_cvtsi128_si32(lanes))>>24);
#endif
    for(i+=3;i<count;i+=4)low=std::min(low,bytes[i]);
    return low;
}
// Complete pixels only; source and destination may be identical, otherwise disjoint.
inline void swapRedBlue(const std::uint8_t* source,std::uint8_t* target,std::size_t bytes){
    std::size_t i=0;
#if defined(__SSE2__)
    const auto keep=_mm_set1_epi32(int(0xff00ff00u)),red=_mm_set1_epi32(0x00ff0000),blue=_mm_set1_epi32(0x000000ff);
    for(;bytes-i>=16;i+=16){const auto p=_mm_loadu_si128(reinterpret_cast<const __m128i*>(source+i));
        const auto swapped=_mm_or_si128(_mm_and_si128(p,keep),_mm_or_si128(_mm_srli_epi32(_mm_and_si128(p,red),16),_mm_slli_epi32(_mm_and_si128(p,blue),16)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(target+i),swapped);
    }
#endif
    for(;bytes-i>=4;i+=4){std::uint32_t p;std::memcpy(&p,source+i,4);p=(p&0xff00ff00u)|((p&0xff0000u)>>16)|((p&255u)<<16);std::memcpy(target+i,&p,4);}
}
}
