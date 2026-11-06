#include "material_byte_ops.h"
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>
int main(){std::mt19937 rng(832746);unsigned cases=0;
 for(unsigned n=0;n<=4096;++n)for(unsigned offset=0;offset<16;++offset){std::vector<std::uint8_t> source(n+offset),target(n+offset,0xa5),expected=target;for(auto& b:source)b=std::uint8_t(rng());
  std::uint8_t low=255;for(unsigned i=3;i<n;i+=4)low=std::min(low,source[offset+i]);assert(low==NorthlightMaterialBytes::minimumAlpha(n?source.data()+offset:nullptr,n));
  for(unsigned i=0;i+4<=n;i+=4){expected[offset+i]=source[offset+i+2];expected[offset+i+1]=source[offset+i+1];expected[offset+i+2]=source[offset+i];expected[offset+i+3]=source[offset+i+3];}
  NorthlightMaterialBytes::swapRedBlue(n?source.data()+offset:nullptr,n?target.data()+offset:nullptr,n);assert(target==expected);
  auto inplace=source;NorthlightMaterialBytes::swapRedBlue(n?inplace.data()+offset:nullptr,n?inplace.data()+offset:nullptr,n);for(unsigned i=0;i+4<=n;i+=4)for(unsigned k=0;k<4;++k)assert(inplace[offset+i+k]==expected[offset+i+k]);++cases;
 }
 std::printf("MATERIAL_BYTE_OPS_PASS cases=%u exactAlpha=1 exactSwizzle=1 inplace=1 timings=0\n",cases);
}
