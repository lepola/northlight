#pragma once
// 0.3.181 (r90): the renderer DLL's memcmp. zig's compiler_rt memcmp (memcmp.zig), which every memcmp
// call in the DLL reached (418 sites, libc++ included), compares one byte per iteration. compare() is
// its exact replacement: (int)a[i]-(int)b[i] over unsigned bytes at the first differing byte i, else 0,
// for every input; it never reads outside [a,a+n) or [b,b+n). SSE2 (the DLL's floor already: compiler_rt's
// memcpy uses movups): 64 bytes per iteration, then 32/16, then one overlapping 16-byte load at n-16;
// 8..15 bytes use bounded 8-byte SIMD loads; shorter inputs and non-SSE2 use 4-byte words. renderer.cpp's TU
// defines the strong extern "C" memcmp on it; host tests call compare() directly.
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

// Test-only counterfactuals (tests/test_fast_memcmp.py); 0 in the DLL. 1: the sign only (+-1);
// 2: a signed-char difference; 3: no overlapping tail; 4: the 16-byte tail loaded at i (over-reads).
#ifndef NORTHLIGHT_MEMCMP_COUNTERFACTUAL
#define NORTHLIGHT_MEMCMP_COUNTERFACTUAL 0
#endif

// Folded into the DLL's memcmp (no call inside it).
#define NORTHLIGHT_MEM_INLINE inline __attribute__((always_inline))
namespace NorthlightMem {
// compiler_rt's loop, kept as the reference and for the in-situ benchmark (keyEqLegacyNs): the DLL's
// memcmp is no longer this. no_builtin: the loop must not be recognized as a memcmp call.
__attribute__((noinline,no_builtin("memcmp"),no_builtin("bcmp")))
inline int byteCompare(const void* pa,const void* pb,std::size_t n){
    const unsigned char* a=static_cast<const unsigned char*>(pa);const unsigned char* b=static_cast<const unsigned char*>(pb);
    for(std::size_t i=0;i<n;++i)if(a[i]!=b[i])return int(a[i])-int(b[i]);
    return 0;
}
namespace Detail {
NORTHLIGHT_MEM_INLINE int difference(const unsigned char* a,const unsigned char* b,std::size_t i){
    if(NORTHLIGHT_MEMCMP_COUNTERFACTUAL==1)return a[i]<b[i]?-1:1;
    if(NORTHLIGHT_MEMCMP_COUNTERFACTUAL==2)return int(static_cast<signed char>(a[i]))-int(static_cast<signed char>(b[i]));
    return int(a[i])-int(b[i]);
}
NORTHLIGHT_MEM_INLINE std::uint32_t load32(const unsigned char* p){std::uint32_t v;std::memcpy(&v,p,4);return v;}
// Little-endian: the lowest set byte of x^y is the first differing byte.
NORTHLIGHT_MEM_INLINE int words(const unsigned char* a,const unsigned char* b,std::size_t n){
    std::size_t i=0;
    if(n>=4){
        for(;n-i>=4;i+=4){const std::uint32_t d=load32(a+i)^load32(b+i);if(d)return difference(a,b,i+(__builtin_ctz(d)>>3));}
        if(i<n&&NORTHLIGHT_MEMCMP_COUNTERFACTUAL!=3){const std::size_t j=n-4;const std::uint32_t d=load32(a+j)^load32(b+j);if(d)return difference(a,b,j+(__builtin_ctz(d)>>3));}
        return 0;
    }
    for(;i<n;++i)if(a[i]!=b[i])return difference(a,b,i);
    return 0;
}
#if defined(__SSE2__)
NORTHLIGHT_MEM_INLINE unsigned unequal8(const unsigned char* a,const unsigned char* b){
    return unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(a)),_mm_loadl_epi64(reinterpret_cast<const __m128i*>(b)))))^0xFFFFu;
}
NORTHLIGHT_MEM_INLINE unsigned unequal16(const unsigned char* a,const unsigned char* b){
    return unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a)),_mm_loadu_si128(reinterpret_cast<const __m128i*>(b)))))^0xFFFFu;
}
#endif
}
NORTHLIGHT_MEM_INLINE int compare(const void* pa,const void* pb,std::size_t n){
    const unsigned char* a=static_cast<const unsigned char*>(pa);const unsigned char* b=static_cast<const unsigned char*>(pb);
#if defined(__SSE2__)
    if(n<8)return Detail::words(a,b,n);
    if(n<16){
        unsigned m=Detail::unequal8(a,b);if(m)return Detail::difference(a,b,__builtin_ctz(m));
        if(n>8){const std::size_t j=n-8;m=Detail::unequal8(a+j,b+j);if(m)return Detail::difference(a,b,j+__builtin_ctz(m));}
        return 0;
    }
    std::size_t i=0;unsigned m;
    for(;n-i>=64;i+=64){
        const __m128i e0=_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a+i)),_mm_loadu_si128(reinterpret_cast<const __m128i*>(b+i)));
        const __m128i e1=_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a+i+16)),_mm_loadu_si128(reinterpret_cast<const __m128i*>(b+i+16)));
        const __m128i e2=_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a+i+32)),_mm_loadu_si128(reinterpret_cast<const __m128i*>(b+i+32)));
        const __m128i e3=_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a+i+48)),_mm_loadu_si128(reinterpret_cast<const __m128i*>(b+i+48)));
        if(_mm_movemask_epi8(_mm_and_si128(_mm_and_si128(e0,e1),_mm_and_si128(e2,e3)))!=0xFFFF){
            m=unsigned(_mm_movemask_epi8(e0))^0xFFFFu;
            if(!m){m=unsigned(_mm_movemask_epi8(e1))^0xFFFFu;i+=16;}
            if(!m){m=unsigned(_mm_movemask_epi8(e2))^0xFFFFu;i+=16;}
            if(!m){m=unsigned(_mm_movemask_epi8(e3))^0xFFFFu;i+=16;}
            return Detail::difference(a,b,i+__builtin_ctz(m));
        }
    }
    if(n-i>=32){
        const __m128i e0=_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a+i)),_mm_loadu_si128(reinterpret_cast<const __m128i*>(b+i)));
        const __m128i e1=_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a+i+16)),_mm_loadu_si128(reinterpret_cast<const __m128i*>(b+i+16)));
        if(_mm_movemask_epi8(_mm_and_si128(e0,e1))!=0xFFFF){
            m=unsigned(_mm_movemask_epi8(e0))^0xFFFFu;
            if(!m){m=unsigned(_mm_movemask_epi8(e1))^0xFFFFu;i+=16;}
            return Detail::difference(a,b,i+__builtin_ctz(m));
        }
        i+=32;
    }
    if(n-i>=16){if((m=Detail::unequal16(a+i,b+i)))return Detail::difference(a,b,i+__builtin_ctz(m));i+=16;}
    if(i<n&&NORTHLIGHT_MEMCMP_COUNTERFACTUAL!=3){
        const std::size_t j=NORTHLIGHT_MEMCMP_COUNTERFACTUAL==4?i:n-16; /* bytes [n-16,i) are known equal */
        if((m=Detail::unequal16(a+j,b+j)))return Detail::difference(a,b,j+__builtin_ctz(m));
    }
    return 0;
#else
    return Detail::words(a,b,n);
#endif
}
}
