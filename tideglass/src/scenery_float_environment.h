#pragma once
#include <xmmintrin.h>
namespace reflection {
// Worker preparation preserves the submitting environment. Numeric reuse keys
// include control bits, excluding sticky exception flags changed by arithmetic.
struct SceneryFloatEnvironment {
    unsigned sse=0;unsigned short x87=0;
    void Capture(){sse=_mm_getcsr();asm volatile("fnstcw %0":"=m"(x87));}
    void Apply() const {asm volatile("fnclex; fldcw %0"::"m"(x87));_mm_setcsr(sse);}
    bool SameControls(const SceneryFloatEnvironment& other) const {return x87==other.x87 && (sse&~63u)==(other.sse&~63u);}
};
}
