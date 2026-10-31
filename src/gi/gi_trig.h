#pragma once
#include <cmath>
namespace NorthlightGI {
inline void sincosAngle(float angle,float& sine,float& cosine){
#if defined(_WIN32) && defined(__SSE2__) && (defined(__clang__) || defined(__GNUC__))
    __builtin_sincosf(angle,&sine,&cosine);
#else
    sine=std::sin(angle);cosine=std::cos(angle);
#endif
}
}
