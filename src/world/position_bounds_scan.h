#pragma once
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace NorthlightPositionBounds {
namespace detail {
inline bool scanPositionsScalar(const void* data,std::size_t count,std::size_t stride,float* low,float* high){
    for(unsigned k=0;k<3;++k){low[k]=FLT_MAX;high[k]=-FLT_MAX;}
    for(std::size_t i=0;i<count;++i){
        float p[3];std::memcpy(p,static_cast<const unsigned char*>(data)+i*stride,sizeof p);
        for(unsigned k=0;k<3;++k){
            if(!std::isfinite(p[k]))return false;
            low[k]=std::min(low[k],p[k]);high[k]=std::max(high[k],p[k]);
        }
    }
    return true;
}
}
// Caller validates the complete strided byte range; only XYZ is read.
inline bool scanPositions(const void* data,std::size_t count,std::size_t stride,float* low,float* high){
#if defined(__SSE2__)
    __m128 lo=_mm_set1_ps(FLT_MAX),hi=_mm_set1_ps(-FLT_MAX);
    const auto exponent=_mm_set1_epi32(0x7f800000),magnitude=_mm_set1_epi32(0x7fffffff),zero=_mm_setzero_si128();
    for(std::size_t i=0;i<count;++i){
        float xyz[3];std::memcpy(xyz,static_cast<const unsigned char*>(data)+i*stride,sizeof xyz);
        const auto p=_mm_set_ps(0,xyz[2],xyz[1],xyz[0]);
        const auto bits=_mm_castps_si128(p),field=_mm_and_si128(bits,exponent);
        const auto subnormal=_mm_and_si128(_mm_cmpeq_epi32(field,zero),_mm_cmpgt_epi32(_mm_and_si128(bits,magnitude),zero));
        // Keep the prior scalar behavior for NaN/Inf and subnormals, including
        // DAZ/FTZ modes where packed min/max can round an input to signed zero.
        if(_mm_movemask_ps(_mm_castsi128_ps(_mm_or_si128(subnormal,_mm_cmpeq_epi32(field,exponent)))))
            return detail::scanPositionsScalar(data,count,stride,low,high);
        // MINPS/MAXPS select the second operand on equality. Keep the prior
        // bound second to preserve std::min/max's signed-zero tie behavior.
        lo=_mm_min_ps(p,lo);hi=_mm_max_ps(p,hi);
    }
    float a[4],b[4];_mm_storeu_ps(a,lo);_mm_storeu_ps(b,hi);
    std::memcpy(low,a,3*sizeof(float));std::memcpy(high,b,3*sizeof(float));
    return true;
#else
    return detail::scanPositionsScalar(data,count,stride,low,high);
#endif
}
}
