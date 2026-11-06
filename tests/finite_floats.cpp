#include "finite_floats.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cfenv>
#include <cstdio>
#include <vector>
using namespace NorthlightFloatChecks;
static float value(std::uint32_t bits){float v;std::memcpy(&v,&bits,4);return v;}
template<std::size_t N,std::uint32_t Max=0x7f7fffffu>static unsigned check(){
 std::array<float,N> data{};const float limit=value(Max);unsigned checked=0;
 auto same=[&]{bool expected=true;for(float v:data)if(!std::isfinite(v)||std::fabs(v)>limit){expected=false;break;}assert((bounded<N,Max>(data.data())==expected));++checked;};
 const std::uint32_t mantissas[]={0,1,0x12345,0x3fffff,0x7fffff};
 for(unsigned sign=0;sign<2;++sign)for(unsigned exponent=0;exponent<256;++exponent)for(auto mantissa:mantissas)for(unsigned lane=0;lane<N;++lane){data.fill(0);data[lane]=value((sign<<31)|(exponent<<23)|mantissa);same();}
 for(auto bits:{Max-1,Max,Max+1,0x80000000u|Max,0x80000000u|(Max+1)})for(unsigned lane=0;lane<N;++lane){data.fill(1);data[lane]=value(bits);same();}
 std::uint32_t seed=126;for(unsigned round=0;round<50000;++round){for(auto& v:data){seed=seed*1664525u+1013904223u;v=value(seed);}same();}
 volatile std::uint32_t signaling=0x7f800001u;
 data.fill(1);data[N-1]=value(signaling);std::feclearexcept(FE_ALL_EXCEPT);assert(!(bounded<N,Max>(data.data())));assert(!std::fetestexcept(FE_INVALID));
 return checked;
}
int main(){const auto count=check<4>()+check<12>()+check<16,0x49742400u>();std::printf("FINITE_FLOATS_PASS checks=%u all_exponents signs mantissas lanes threshold_edges random SNAN_no_FP_exception\n",count);}
