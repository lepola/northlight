#pragma once
#include "world_gi.h"
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
namespace NorthlightGI {
inline void packDistanceMoment(DistanceMoment& out,double sum,double square,double weight){
#if defined(__SSE2__)
    static_assert(sizeof(DistanceMoment)==2*sizeof(float),"packed distance moments");
    const auto result=_mm_cvtpd_ps(_mm_div_pd(_mm_set_pd(square,sum),_mm_set1_pd(weight)));
    _mm_storel_epi64(reinterpret_cast<__m128i*>(&out),_mm_castps_si128(result));
#else
    out.mean=float(sum/weight);out.meanSquare=float(square/weight);
#endif
}
}
